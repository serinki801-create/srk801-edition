#!/usr/bin/env bash
# ============================================================================
# verify_ipa.sh — верификация .ipa БЕЗ устройства и БЕЗ Mac (пункты 0–9).
# Падает (exit 1) на ПЕРВОМ же провале, не предупреждает.
# Ставка: __swift5_typeref в секциях бинаря = Swift отработал по-настоящему,
# а не было тихой подмены ObjC. Вызывать: ./verify_ipa.sh packages/*.ipa
#
# Инструменты: Apple otool (в тулчейне Theos; это LLVM-бэкенд — тот же парсер,
# что у llvm-otool, которого в тулчейне нет как отдельного бинаря) и llvm-nm.
# Без LIEF: его Python-API меняет имена атрибутов между релизами.
# Зависимости: unzip, python3, otool|llvm-otool, llvm-nm|nm. Проверяются ниже
# с названием пакета, а не bare «command not found».
# ============================================================================
set -uo pipefail

APP_NAME="${APP_NAME:-HelloHybrid}"
IPA="${1:-}"
[[ -n "$IPA" && -f "$IPA" ]] || { echo "VERIFY FAIL #0: нет .ipa (usage: $0 packages/*.ipa)"; exit 1; }

export THEOS="${THEOS:-$HOME/theos}"
TC="$THEOS/toolchain/linux/iphone/bin"

# --- явные зависимости (с названием пакета) ---
command -v unzip >/dev/null 2>&1 || { echo "VERIFY FAIL #0: нет unzip (пакет: unzip)"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "VERIFY FAIL #0: нет python3 (пакет: python3)"; exit 1; }
if [[ -x "$TC/otool" ]]; then OTOOL="$TC/otool";
elif command -v llvm-otool >/dev/null 2>&1; then OTOOL="llvm-otool";
elif command -v otool >/dev/null 2>&1; then OTOOL="otool";
else echo "VERIFY FAIL #0: нет otool/llvm-otool (пакет: theos toolchain, иначе cctools)"; exit 1; fi
if [[ -x "$TC/llvm-nm" ]]; then NM="$TC/llvm-nm";
elif command -v llvm-nm >/dev/null 2>&1; then NM="llvm-nm";
elif [[ "$(uname)" == "Darwin" ]]; then NM="nm -m";
else echo "VERIFY FAIL #0: нет llvm-nm (пакет: theos toolchain, иначе llvm)"; exit 1; fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# --- 0. Распаковка, единственный Payload/*.app, CFBundleExecutable ---
unzip -qo "$IPA" -d "$WORK" || { echo "VERIFY FAIL #0: $IPA не распаковывается (битый zip)"; exit 1; }
mapfile -t APPS < <(find "$WORK/Payload" -maxdepth 1 -mindepth 1 -type d -name "*.app")
[[ "${#APPS[@]}" -eq 1 ]] || { echo "VERIFY FAIL #0: ожидался ровно один Payload/*.app, найдено: ${#APPS[@]}"; exit 1; }
APPDIR="${APPS[0]}"
EXE="$(APP="$APPDIR" python3 -c "import plistlib,os;print(plistlib.load(open(os.path.join(os.environ['APP'],'Info.plist'),'rb')).get('CFBundleExecutable',''))" 2>/dev/null)"
[[ -n "$EXE" ]] || { echo "VERIFY FAIL #0: не прочли CFBundleExecutable через plistlib"; exit 1; }
BIN="$APPDIR/$EXE"
[[ -f "$BIN" ]] || { echo "VERIFY FAIL #0: нет бинаря $BIN"; exit 1; }
echo "VERIFY OK #0: один Payload/*.app, executable=$EXE"

# --- 1. Mach-O ARM64 MH_EXECUTE (не x86_64, не arm64e) ---
# otool -h на thin-бинаре: строка Mach header (fat-заголовков у thin нет,
# поэтому -f тут пуст — смотрим -h, это задокументировано, а не подгон).
MH="$($OTOOL -h "$BIN" 2>/dev/null | grep -E "0xfeedfacf|0xcafebabe" | head -1)"
echo "$MH" | grep -qw "16777228" || { echo "VERIFY FAIL #1: нет cputype ARM64 (16777228)"; exit 1; }
echo "$MH" | grep -qw "16777223" && { echo "VERIFY FAIL #1: бинарь x86_64"; exit 1; }
SUB="$(echo "$MH" | awk '{print $3}')"
[[ "$SUB" == "0" ]] || { echo "VERIFY FAIL #1: cpusubtype=$SUB — это arm64e, нужен чистый arm64 (0)"; exit 1; }
echo "$MH" | awk '{print $5}' | grep -qw "2" || { echo "VERIFY FAIL #1: нет filetype MH_EXECUTE (2)"; exit 1; }
echo "VERIFY OK #1: Mach-O ARM64 (cpusubtype 0), MH_EXECUTE"

# --- 2. Swift-секции: typeref + reflstr + proto (все три, иначе FAIL) ---
SECTS="$($OTOOL -l "$BIN" 2>/dev/null | grep -oE 'sectname __swift5[a-z0-9_]*' | awk '{print $2}' | sort -u)"
echo "$SECTS" | grep -qx "__swift5_typeref" || { echo "VERIFY FAIL #2: нет __swift5_typeref — Swift НЕ компилировался"; exit 1; }
echo "$SECTS" | grep -qx "__swift5_reflstr" || { echo "VERIFY FAIL #2: нет __swift5_reflstr — Swift НЕ компилировался"; exit 1; }
echo "$SECTS" | grep -qx "__swift5_proto" || { echo "VERIFY FAIL #2: нет __swift5_proto — Swift НЕ компилировался"; exit 1; }
echo "VERIFY OK #2: swift-секции: $(echo "$SECTS" | tr '\n' ' ')"

# --- 3. ObjC-секции связки ---
OBJSECTS="$($OTOOL -l "$BIN" 2>/dev/null | grep -oE 'sectname __objc_[a-z_]+' | awk '{print $2}' | sort -u)"
[[ -n "$OBJSECTS" ]] || { echo "VERIFY FAIL #3: нет __objc_* секций — ObjC-сторона связки отсутствует"; exit 1; }
echo "VERIFY OK #3: objc-секции: $(echo "$OBJSECTS" | tr '\n' ' ')"

# --- 4. Системные зависимости: libswiftCore + UIKit ---
DYLIBS="$($OTOOL -L "$BIN" 2>/dev/null | grep -oE '/[^ ]+' | sort -u)"
echo "$DYLIBS" | grep -qx "/usr/lib/swift/libswiftCore.dylib" || { echo "VERIFY FAIL #4: нет /usr/lib/swift/libswiftCore.dylib"; exit 1; }
echo "$DYLIBS" | grep -qx "/System/Library/Frameworks/UIKit.framework/UIKit" || { echo "VERIFY FAIL #4: нет UIKit.framework"; exit 1; }
echo "VERIFY OK #4: dylib на месте (libswiftCore + UIKit)"

# --- 5. Рантайм НЕ вшит ---
[[ -d "$APPDIR/Frameworks" ]] && { echo "VERIFY FAIL #5: бандл содержит Frameworks — рантайм вшит, запрещено"; exit 1; }
echo "VERIFY OK #5: Frameworks/ нет — рантайм системный"

# --- 6. Подпись жива (после ldid -S) ---
$OTOOL -l "$BIN" 2>/dev/null | grep -q "cmd LC_CODE_SIGNATURE" || { echo "VERIFY FAIL #6: нет LC_CODE_SIGNATURE — бинарь не подписан"; exit 1; }
echo "VERIFY OK #6: LC_CODE_SIGNATURE present"

# --- 7. ObjC-классы в бинаре ---
NOBJ="$($NM "$BIN" 2>/dev/null | grep -c '_OBJC_CLASS_\$_')"
[[ "$NOBJ" -gt 0 ]] || { echo "VERIFY FAIL #7: нет _OBJC_CLASS_\$_ — ObjC-часть отсутствует"; exit 1; }
echo "VERIFY OK #7: ObjC-классов в бинаре: $NOBJ"

# --- 8. Info.plist: identifier, min-os, executable ---
PLIST_OK="$(APP="$APPDIR" python3 - <<'EOF'
import plistlib, os
d = os.environ['APP']
try:
    p = plistlib.load(open(os.path.join(d, 'Info.plist'), 'rb'))
    exe = p.get('CFBundleExecutable', '')
    bid = p.get('CFBundleIdentifier', '')
    mos = str(p.get('MinimumOSVersion', '99'))
    real = os.path.basename(d).replace('.app', '')
    ok = (len(bid) > 0
          and tuple(int(x) for x in mos.split('.')) <= (13, 0)
          and len(exe) > 0
          and os.path.isfile(os.path.join(d, exe)))
except Exception as e:
    ok = False
    exe, bid, mos = '?', '?', '?'
print('PLIST_OK' if ok else f'PLIST_BAD exe={exe!r} bid={bid!r} mos={mos!r}')
EOF
)"
[[ "$PLIST_OK" == PLIST_OK ]] || { echo "VERIFY FAIL #8: Info.plist невалиден ($PLIST_OK)"; exit 1; }
echo "VERIFY OK #8: Info.plist (identifier/min-os <13.0/executable совпадает с файлом)"

# --- 9. Сводка ---
IPA_SIZE="$(stat -c%s "$IPA" 2>/dev/null || stat -f%z "$IPA")"
BIN_SIZE="$(stat -c%s "$BIN" 2>/dev/null || stat -f%z "$BIN")"
SHA="$(sha256sum "$IPA" 2>/dev/null | cut -c1-16 || shasum -a 256 "$IPA" | cut -c1-16)"
echo "VERIFY OK #9: ipa=${IPA_SIZE}B sha256=${SHA}… bin=${BIN_SIZE}B swift=[$(echo "$SECTS" | tr '\n' ',' | sed 's/,$//')] dylibs=$(echo "$DYLIBS" | wc -l)"
echo "VERIFY ALL PASSED: $IPA"
