#!/usr/bin/env bash
# ============================================================================
# verify_ipa.sh — верификация .ipa БЕЗ устройства и БЕЗ Mac (8 пунктов).
# Падает (exit 1) на ПЕРВОМ же провале с внятным сообщением, какой именно.
# Ставка: __swift5_typeref в секциях бинаря = Swift отработал по-настоящему,
# а не было тихой подмены ObjC. Вызывать: ./verify_ipa.sh packages/*.ipa
# ============================================================================
set -uo pipefail

APP_NAME="${APP_NAME:-HelloHybrid}"
IPA="${1:-}"
[[ -n "$IPA" && -f "$IPA" ]] || { echo "VERIFY FAIL #0: нет .ipa (usage: $0 packages/*.ipa)"; exit 1; }

export THEOS="${THEOS:-$HOME/theos}"
TC="$THEOS/toolchain/linux/iphone/bin"

# Резолв llvm-инструментов: тулчейн Theos -> PATH -> xcrun (macOS-раннер).
resolve_tool() {
    if [[ -x "$TC/$1" ]]; then echo "$TC/$1"; return 0; fi
    if command -v "$1" >/dev/null 2>&1; then echo "$1"; return 0; fi
    if command -v xcrun >/dev/null 2>&1 && xcrun --find "$1" >/dev/null 2>&1; then
        echo "xcrun $1"; return 0
    fi
    return 1
}
OBJDUMP="$(resolve_tool llvm-objdump)" || { echo "VERIFY FAIL #0: нет llvm-objdump (ни в тулчейне, ни в PATH, ни через xcrun)"; exit 1; }
if command -v llvm-nm >/dev/null 2>&1 || [[ -x "$TC/llvm-nm" ]]; then
    NM="$(resolve_tool llvm-nm)"; NM_M=""
else
    NM="nm -m"; NM_M="1"  # Darwin nm
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
unzip -qo "$IPA" -d "$WORK" || { echo "VERIFY FAIL #0: $IPA не распаковывается (битый zip)"; exit 1; }

BIN="$WORK/Payload/$APP_NAME.app/$APP_NAME"
PLIST="$WORK/Payload/$APP_NAME.app/Info.plist"
[[ -f "$BIN" ]] || { echo "VERIFY FAIL #0: нет бинаря Payload/$APP_NAME.app/$APP_NAME"; exit 1; }
[[ -f "$PLIST" ]] || { echo "VERIFY FAIL #6: нет Info.plist в бандле"; exit 1; }

# --- 1. Mach-O ARM64 MH_EXECUTE (не x86_64, не arm64e) ---
HDR="$($OBJDUMP --macho --private-headers "$BIN" 2>/dev/null)"
echo "$HDR" | grep -q "ARM64" || { echo "VERIFY FAIL #1: нет cpuType ARM64"; exit 1; }
echo "$HDR" | grep -qiE "x86_64|arm64e" && { echo "VERIFY FAIL #1: бинарь не чистый ARM64 (x86_64/arm64e)"; exit 1; }
echo "$HDR" | grep -q "EXECUTE" || { echo "VERIFY FAIL #1: нет fileType MH_EXECUTE"; exit 1; }
echo "VERIFY OK #1: Mach-O 64-bit ARM64 MH_EXECUTE"

# --- 2. Swift-секции: минимум __swift5_typeref и __swift5_reflstr ---
SECTS="$($OBJDUMP --macho --section-headers "$BIN" 2>/dev/null | grep -oE '__swift5[a-z0-9_]*' | sort -u)"
echo "$SECTS" | grep -q "__swift5_typeref" || { echo "VERIFY FAIL #2: нет __swift5_typeref — Swift НЕ компилировался"; exit 1; }
echo "$SECTS" | grep -q "__swift5_reflstr" || { echo "VERIFY FAIL #2: нет __swift5_reflstr — Swift НЕ компилировался"; exit 1; }
echo "VERIFY OK #2: swift-секции: $(echo "$SECTS" | tr '\n' ' ')"

# --- 3. dylib: libswiftCore + UIKit ---
DYLIBS="$($OBJDUMP --macho --private-headers "$BIN" 2>/dev/null | grep -oE 'name /[^ ]+\.dylib|name /System/Library/Frameworks/[^ ]+' | sort -u)"
echo "$DYLIBS" | grep -q "/usr/lib/swift/libswiftCore.dylib" || { echo "VERIFY FAIL #3: нет LC_LOAD_DYLIB /usr/lib/swift/libswiftCore.dylib"; exit 1; }
echo "$DYLIBS" | grep -q "UIKit.framework/UIKit" || { echo "VERIFY FAIL #3: нет LC_LOAD_DYLIB UIKit.framework/UIKit"; exit 1; }
echo "VERIFY OK #3: dylib на месте (libswiftCore + UIKit)"

# --- 4. Рантайм НЕ вшит (см. A4: с 12.2 он в самой ОС) ---
[[ -d "$WORK/Payload/$APP_NAME.app/Frameworks" ]] && { echo "VERIFY FAIL #4: бандл содержит Frameworks — рантайм вшит, запрещено"; exit 1; }
echo "VERIFY OK #4: Frameworks/ нет — рантайм системный"

# --- 5. Подпись на месте (после ldid -S) ---
echo "$HDR" | grep -q "LC_CODE_SIGNATURE" || { echo "VERIFY FAIL #5: нет LC_CODE_SIGNATURE — бинарь не подписан"; exit 1; }
echo "VERIFY OK #5: LC_CODE_SIGNATURE present"

# --- 6. Info.plist: executable, deployment, identifier ---
PLIST_OK="$(APP_NAME="$APP_NAME" python3 - "$PLIST" <<'EOF'
import os, plistlib, sys
want = os.environ.get('APP_NAME', '')
p = plistlib.load(open(sys.argv[1], 'rb'))
exe = p.get('CFBundleExecutable', '')
bid = p.get('CFBundleIdentifier', '')
mos = str(p.get('MinimumOSVersion', '99'))
try:
    ok = (exe == want
          and len(bid) > 0
          and tuple(int(x) for x in mos.split('.')) <= tuple(int(x) for x in "27.0.1".split('.')))
except Exception:
    ok = False
print('PLIST_OK' if ok else f'PLIST_BAD exe={exe!r} want={want!r} bid={bid!r} mos={mos!r}')
EOF
)"
# Имя бинаря обязано совпадать с APP_NAME (а не захардкоженным литералом).
[[ "$PLIST_OK" == PLIST_OK ]] || { echo "VERIFY FAIL #6: Info.plist невалиден ($PLIST_OK)"; exit 1; }
echo "VERIFY OK #6: Info.plist (executable/bundle-id/min-os) корректен"

# --- 7. ObjC-часть на месте ---
if [[ -n "$NM_M" ]]; then NOBJ="$($NM "$BIN" 2>/dev/null | grep -c '_OBJC_CLASS_\$_')"; else NOBJ="$($NM "$BIN" 2>/dev/null | grep -c '_OBJC_CLASS_\$_')"; fi
[[ "$NOBJ" -gt 0 ]] || { echo "VERIFY FAIL #7: нет _OBJC_CLASS_\$_ — ObjC-часть отсутствует"; exit 1; }
echo "VERIFY OK #7: ObjC-классов в бинаре: $NOBJ"

# --- 8. Сводка: размер, секции, dylib, хэш ---
SIZE="$(stat -c%s "$BIN" 2>/dev/null || stat -f%z "$BIN")"
SHA="$(sha256sum "$IPA" 2>/dev/null | cut -d' ' -f1 || shasum -a 256 "$IPA" | cut -d' ' -f1)"
echo "VERIFY OK #8: summary size=${SIZE}B swift_sections=[$(echo "$SECTS" | tr '\n' ',' | sed 's/,$//')] dylibs=$(echo "$DYLIBS" | wc -l) sha256=$SHA"
echo "VERIFY ALL PASSED: $IPA"
