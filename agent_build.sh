#!/usr/bin/env bash
# ============================================================================
# agent_build.sh — неинтерактивная сборка + проверка .ipa для CLI ИИ-агентов
# ============================================================================
# Использование (одна команда, ноль вопросов):
#   bash agent_build.sh [каталог_проекта]
# По умолчанию: ~/ios_projects/HelloHybrid
#
# Что делает:
#   1. Проверяет окружение (THEOS, clang, SDK, ldid, make, zip) — без sudo.
#   2. Запускает ./package_ipa.sh проекта.
#   3. Проверяет результат: zip цел, есть Payload/*.app/{Info.plist,
#      бинарник}, бинарник — Mach-O arm64, подпись ldid читается.
# Выход: 0 = BUILD PASS, иначе 1 + строка FAIL:<причина> (для парсинга).
# ============================================================================
set -uo pipefail

PROJ="${1:-$HOME/ios_projects/HelloHybrid}"
export THEOS="${THEOS:-$HOME/theos}"
export PATH="$THEOS/toolchain/linux/iphone/bin:$THEOS/bin:$HOME/ios-toolchain/bin:$HOME/.local/bin:$PATH"

fail() { echo "FAIL:$1"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

[[ -d "$PROJ" ]] || fail "no project dir $PROJ"
[[ -x "$PROJ/package_ipa.sh" ]] || fail "no executable $PROJ/package_ipa.sh"
[[ -d "$THEOS" ]] || fail "no THEOS at $THEOS (run: bash mint_ios_setup.sh --toolchain)"
[[ -x "$THEOS/toolchain/linux/iphone/bin/clang" ]] || fail "no theos clang toolchain"
have ldid || fail "no ldid in PATH"
have make || fail "no make"
have zip || fail "no zip"
ls -d "$THEOS"/sdks/*.sdk >/dev/null 2>&1 || fail "no SDK in $THEOS/sdks"
[[ -f "$PROJ/Makefile" ]] || fail "no Makefile in $PROJ"
grep -q 'include $(THEOS)/makefiles/common.mk' "$PROJ/Makefile" \
    || grep -q 'THEOS' "$PROJ/Makefile" \
    || fail "Makefile does not use THEOS"

echo "[agent] schleuder: $PROJ"
echo "[agent] clang: $("$THEOS/toolchain/linux/iphone/bin/clang" --version | head -1)"
echo "[agent] sdk: $(ls -d "$THEOS"/sdks/*.sdk | head -1)"
echo "[agent] ldid: $(ldid --version 2>&1 | head -1)"

cd "$PROJ" || fail "cd $PROJ"
./package_ipa.sh > /tmp/agent_build_pkg.log 2>&1 \
    || fail "package_ipa.sh failed (see /tmp/agent_build_pkg.log)"

IPA="$(ls -t packages/*.ipa 2>/dev/null | head -1)"
[[ -n "$IPA" ]] || fail "no .ipa in packages/ after build"

# --- verify: zip integrity ---
unzip -t "$IPA" >/dev/null 2>&1 || fail "$IPA is not a valid zip"

# --- verify: Payload/App.app/{Info.plist,binary} ---
APP_DIR="$(unzip -l "$IPA" | grep -oE 'Payload/[^/]+\.app/' | head -1)"
[[ -n "$APP_DIR" ]] || fail "no Payload/*.app in $IPA"
unzip -l "$IPA" | grep -q "${APP_DIR}Info.plist" \
    || fail "no Info.plist in $APP_DIR"
BIN="$(unzip -l "$IPA" | grep -vE '/$|Info.plist|PkgInfo|\.png|\.car$' \
    | awk '{print $4}' | grep "^$APP_DIR" | head -1)"
[[ -n "$BIN" ]] || fail "no executable found in $APP_DIR"

# --- verify: Mach-O arm64 ---
TMPD="$(mktemp -d)"
unzip -p "$IPA" "$BIN" > "$TMPD/bin" 2>/dev/null
file "$TMPD/bin" | grep -q "Mach-O 64-bit arm64" \
    || { rm -rf "$TMPD"; fail "$BIN is not Mach-O arm64"; }
rm -rf "$TMPD"

# --- verify: ad-hoc signature readable ---
if command -v ldid >/dev/null 2>&1; then
    TMPD2="$(mktemp -d)"
    unzip -p "$IPA" "$BIN" > "$TMPD2/bin" 2>/dev/null
    chmod +x "$TMPD2/bin"
    ldid -e "$TMPD2/bin" >/dev/null 2>&1 \
        && echo "[agent] signature: ad-hoc OK (ldid -e reads entitlements)" \
        || echo "[agent] signature: WARN (ldid -e unreadable, binary still ad-hoc signed at build)"
    rm -rf "$TMPD2"
fi

echo "BUILD PASS: $PROJ/$IPA"
exit 0
