#!/usr/bin/env bash
# ============================================================================
# mint_ios_setup.sh — Linux Mint 22.3: станция сборки .ipa (AltStore/Sideloadly)
# ============================================================================
# Модули:
#   --toolchain  Theos + iPhoneOS.sdk + ldid + package_ipa.sh + HelloHybrid
#   --ide        Lite XL (C/Lua, ~20 МБ, НЕ Electron) + clangd + Ctrl+B
#   --visual     Plank + Picom (backend="glx", vsync=true) + gsettings-темы
#   --all        всё (по умолчанию если флагов нет)
#   --with-swift=local   тир A: doctor-проверка локального кросс-стека
#                            (тулчейн Theos уже содержит swift-frontend;
#                            ставить нечего — только проверка согласованности)
#   --with-swift=remote  тир B (единственный гарантированный): gh +
#                            кэш SDK + задача ipa:build(remote) в tasks.json
#   --with-swift         без значения = remote
#   --doctor             только swift_doctor() и выход
#   --remote-build[=путь]  запуск удалённой сборки (для tasks.json/CI)
#   --uninstall  полный откат в стандартный вид Mint (БЕЗ драйверов/grub)
#
# Правило безопасности: трогаем только $HOME, ~/.config, ~/.local, ~/.bashrc.
# Ядро, модули, firmware, grub, TLP/cpufreq — НЕ ТРОГАЕМ. Перезагрузка не нужна.
# ============================================================================
set -euo pipefail

DO_TOOLCHAIN=0; DO_IDE=0; DO_VISUAL=0; DO_UNINSTALL=0; DO_DOCTOR=0
WITH_SWIFT=""; REMOTE_BUILD=""
ASSUME_YES=""
for arg in "$@"; do
    case "$arg" in
        --toolchain) DO_TOOLCHAIN=1 ;;
        --ide) DO_IDE=1 ;;
        --visual) DO_VISUAL=1 ;;
        --all) DO_TOOLCHAIN=1; DO_IDE=1; DO_VISUAL=1 ;;
        --uninstall) DO_UNINSTALL=1 ;;
        --with-swift=local|--with-swift=remote) WITH_SWIFT="${arg#--with-swift=}" ;;
        --with-swift) WITH_SWIFT="remote" ;;
        --doctor) DO_DOCTOR=1 ;;
        --remote-build=*) REMOTE_BUILD="${arg#--remote-build=}" ;;
        --remote-build) REMOTE_BUILD="$PWD" ;;
        --yes|-y) ASSUME_YES="-y" ;;
        --help|-h) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "Неизвестный флаг: $arg" >&2; exit 1 ;;
    esac
done
[[ "$DO_TOOLCHAIN$DO_IDE$DO_VISUAL$DO_UNINSTALL" == "0000" && -z "$WITH_SWIFT" && "$DO_DOCTOR" == "0" && -z "$REMOTE_BUILD" ]] && DO_TOOLCHAIN=1 && DO_IDE=1 && DO_VISUAL=1

# Проверка sudo (не падаем, если пароля нет — сообщим и попросим руками).
need_sudo() {
    if ! sudo -n true 2>/dev/null; then
        echo "НЕТ passwordless sudo. Установи вручную:"
        echo "  sudo apt update && sudo apt install -y $*"
        return 1
    fi
    sudo apt-get update -qq || true
    sudo apt-get install $ASSUME_YES "$@"
}

export THEOS="${THEOS:-$HOME/theos}"
PROJ_DIR="$HOME/ios_projects/HelloHybrid"
mkdir -p "$HOME/ios_projects" "$HOME/.local/bin" "$HOME/.local/share" "$HOME/.config"

# --- helpers ------------------------------------------------------------------
have() { command -v "$1" >/dev/null 2>&1; }
log()  { echo "==> $*"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_CACHE="$HOME/.cache/srk801-sdk"

# --- SWIFT DOCTOR ---------------------------------------------------------------
# Проверяет согласованность локального Swift-стека. При несовпадении версий —
# отказ продолжать с указанием, какую версию тянуть. Вызывается автоматически
# перед любой Swift-сборкой (remote) и по флагу --doctor.
swift_doctor() {
    # Правило тира A: ПАРУЕМ ТУЛЧЕЙН → SDK, а не наоборот.
    # Self-built swift-frontend из apple/swift не собираем (часы работы,
    # ломается на каждом релизе Xcode) — пункты ниже решают бесплатно.
    local tc_root tc_sf tc_ver sdk choice choice_sw cur_sw gh_state pair verdict
    tc_root="$THEOS/toolchain/linux/iphone"
    tc_sf="$tc_root/bin/swift-frontend"   # == <toolchain>/usr/bin/swift-frontend в нотации Theos-доков
    if [[ ! -x "$tc_sf" ]]; then
        echo "тулчейн: $tc_root  swift ?"
        echo "sdk:     -   swift-version ?"
        echo "пара:    mismatch (нет swift-frontend в тулчейне)"
        echo "gh:      нет"
        echo "verdict: none"
        echo "подсказка: доустанови тулчейн Theos (With Swift support) либо иди в тир B: gh auth login"
        return 1
    fi
    tc_ver="$("$tc_sf" --version 2>/dev/null | grep -m1 -oE 'Swift version [0-9]+\.[0-9]+' | awk '{print $3}')"
    echo "тулчейн: $tc_root  swift ${tc_ver:-?}"
    # Перебор SDK от СТАРОГО к НОВОМУ, первый чья swift-версия <= версии тулчейна.
    choice=""; choice_sw=""
    for sdk in $(ls -d "$THEOS"/sdks/iPhoneOS*.sdk 2>/dev/null | sort -V); do
        cur_sw="$(grep -m1 -oE 'Apple Swift version [0-9]+\.[0-9]+' \
            $(find "$sdk" -name 'arm64-apple-ios.swiftinterface' 2>/dev/null | head -1) 2>/dev/null \
            | grep -oE '[0-9]+\.[0-9]+$')"
        [[ -z "$cur_sw" ]] && continue
        if ver_le "$cur_sw" "$tc_ver"; then choice="$sdk"; choice_sw="$cur_sw"; break; fi
    done
    if [[ -z "$choice" ]]; then
        # Ничего не подошло — показываем самый старый как мешающий.
        sdk="$(ls -d "$THEOS"/sdks/iPhoneOS*.sdk 2>/dev/null | sort -V | head -1)"
        if [[ -n "$sdk" ]]; then
            cur_sw="$(grep -m1 -oE 'Apple Swift version [0-9]+\.[0-9]+' \
                $(find "$sdk" -name 'arm64-apple-ios.swiftinterface' 2>/dev/null | head -1) 2>/dev/null \
                | grep -oE '[0-9]+\.[0-9]+$')"
            echo "sdk:     $(basename "$sdk")   swift-version ${cur_sw:-?}"
            echo "пара:    mismatch (модули новее тулчейна: SDK Swift ${cur_sw:-?} > toolchain Swift $tc_ver)"
        else
            echo "sdk:     -   swift-version ?"
            echo "пара:    mismatch (SDK нет вовсе)"
        fi
        echo "подсказка: тяни SDK эпохи тулчейна (для Swift $tc_ver — xybp888/iOS-SDKs iOS 14–15, зеркало theos/sdks),"
        echo "           НЕ свежий iOS 26/27 SDK и НЕ пересобирай компилятор."
    else
        echo "sdk:     $(basename "$choice")   swift-version $choice_sw"
        # kabiroberai 5.3.2 + SDK эпохи iOS 14–15 — ВАЛИДНАЯ ПАРА, не баг.
        echo "пара:    ok"
    fi
    if have gh && gh auth status >/dev/null 2>&1; then gh_state="установлен/авторизован";
    elif have gh; then gh_state="установлен, нет авторизации (gh auth login)";
    else gh_state="нет"; fi
    echo "gh:      $gh_state"
    if [[ -n "$choice" ]]; then verdict="local";
    elif [[ "$gh_state" == "установлен/авторизован" ]]; then verdict="remote";
    else verdict="none"; fi
    echo "verdict: $verdict"
    [[ -n "$choice" ]] && return 0
    return 1
}

# Сравнение версий major.minor: ver_le A B == 1 если A <= B.
ver_le() {
    local a1 a2 b1 b2
    IFS=. read -r a1 a2 <<< "$1"; IFS=. read -r b1 b2 <<< "$2"
    a1=${a1:-0}; a2=${a2:-0}; b1=${b1:-0}; b2=${b2:-0}
    if (( a1 < b1 )); then return 0; fi
    if (( a1 > b1 )); then return 1; fi
    (( a2 <= b2 ))
}

# --- SDK CACHE (тир B) ------------------------------------------------------------
# Кэш скачанного SDK: маркер <версия>.ok с sha256 контрольного файла.
# Не перекачивает на каждый билд — сверка по сумме, не по дате.
ensure_sdk_cache() {
    local sdk ver sum marker
    sdk="$(ls -d "$THEOS"/sdks/*.sdk 2>/dev/null | head -1)"
    if [[ -z "$sdk" ]]; then
        log "[swift] SDK нет — качаю через install-sdk (один раз, дальше кэш)..."
        "$THEOS/bin/install-sdk" latest
        sdk="$(ls -d "$THEOS"/sdks/*.sdk 2>/dev/null | head -1)"
        [[ -n "$sdk" ]] || { echo "ERROR: SDK не встал." >&2; return 1; }
    fi
    mkdir -p "$SDK_CACHE"
    ver="$(basename "$sdk" .sdk)"
    if [[ -f "$sdk/SDKSettings.plist" ]]; then sum="$(sha256sum "$sdk/SDKSettings.plist" | cut -d' ' -f1)";
    else sum="$(find "$sdk" -maxdepth 2 -type f | sort | sha256sum | cut -d' ' -f1)"; fi
    marker="$SDK_CACHE/$ver.ok"
    if [[ -f "$marker" ]] && [[ "$(cat "$marker")" == "$sum" ]]; then
        echo "OK: SDK кэш hit ($ver), скачивание пропущено."
    else
        echo "$sum" > "$marker"
        echo "OK: SDK $ver зафиксирован в кэше ($marker)."
    fi
}

# --- REMOTE SWIFT BUILD (тир B) -----------------------------------------------------
# Linux делает всё остальное, Swift едет на macos-14 раннер и возвращается
# готовыми .ipa. Таймаут ожидания — 20 минут, потом статус, а не висение.
remote_swift_build() {
    local proj="${1:-$PWD}" target="${2:-arm64-apple-ios13.0}" id st co
    [[ -d "$proj" ]] || { echo "ERROR: нет проекта $proj" >&2; return 1; }
    have gh || { echo "ERROR: нет gh. Ставлю..." >&2; need_sudo gh || true; }
    have gh || { echo "ERROR: поставь gh (sudo apt install gh) и авторизуйся: gh auth login — https://github.com/settings/tokens" >&2; return 1; }
    gh auth status >/dev/null 2>&1 || { echo "ERROR: gh не авторизован. Выполни: gh auth login" >&2; return 1; }
    # doctor — для отчёта (какая пара/вердикт); тир B от локальной пары не зависит,
    # поэтому здесь только показываем, не гейтим. Гейт — gh auth выше.
    swift_doctor || log "[swift-remote] локальной пары нет — идём в тир B (это нормально)"
    ensure_sdk_cache || return 1
    cd "$SCRIPT_DIR" || return 1
    log "[swift-remote] запуск workflow (target=$target)..."
    gh workflow run swift-ios-build.yml -f project_path="$proj" -f target="$target" || return 1
    sleep 15
    id="$(gh run list --workflow=swift-ios-build.yml --limit 1 --json databaseId -q '.[0].databaseId')"
    [[ -n "$id" && "$id" != "null" ]] || { echo "ERROR: не нашёл run id." >&2; return 1; }
    echo "[swift-remote] run id=$id, жду (таймаут 20 мин)..."
    for ((i = 0; i < 1200; i += 20)); do
        st="$(gh run view "$id" --json status -q .status 2>/dev/null)"
        [[ "$st" == "completed" ]] && break
        sleep 20
    done
    co="$(gh run view "$id" --json conclusion -q .conclusion 2>/dev/null)"
    if [[ "$co" != "success" ]]; then
        echo "REMOTE FAIL: run $id завершился статусом '${co:-timeout/unknown}'." >&2
        echo "Логи: gh run view $id --log-failed" >&2
        return 1
    fi
    mkdir -p "$proj/packages"
    gh run download "$id" -n ipa -D "$proj/packages" || return 1
    echo "REMOTE OK: $proj/packages/$(ls -t "$proj/packages" | head -1)"
}

# --- SWIFT MODE (интеграция флага --with-swift) --------------------------------------
mod_swift() {
    case "$WITH_SWIFT" in
        local)
            log "[swift] тир A: doctor-проверка перед установкой/сборкой..."
            swift_doctor || exit 1
            echo "OK: локальный Swift-стек согласован — можно собирать."
            ;;
        remote)
            log "[swift] тир B (гарантированный): gh + кэш + задача в tasks.json..."
            have gh || need_sudo gh || true
            have gh || { echo "ERROR: gh не встал. Вручную: sudo apt install gh && gh auth login" >&2; exit 1; }
            ensure_sdk_cache
            # Задача ipa:build(remote) в tasks.json демо-проекта (идемпотентно).
            if have python3 && [[ -f "$HOME/ios_projects/HelloHybrid/.vscode/tasks.json" ]]; then
                python3 - "$HOME/ios_projects/HelloHybrid/.vscode/tasks.json" <<'EOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
tasks = d.setdefault('tasks', [])
if not any(t.get('label') == 'ipa:build(remote)' for t in tasks):
    tasks.append({
        "label": "ipa:build(remote)",
        "type": "shell",
        "command": "bash /home/abzal/hobby-os/mint_ios_setup.sh --remote-build",
        "options": {"cwd": "${workspaceFolder}"},
        "group": "build",
        "problemMatcher": [],
        "presentation": {"reveal": "always", "panel": "shared"}
    })
    json.dump(d, open(p, 'w'), indent=2, ensure_ascii=False)
    print('OK: задача ipa:build(remote) добавлена в tasks.json')
else:
    print('OK: задача ipa:build(remote) уже есть (или нет tasks.json)')
EOF
            fi
            echo "OK: для удалённой сборки: bash $0 --remote-build [проект] (таймаут 20 мин)"
            ;;
    esac
}

# --- TOOLCHAIN ------------------------------------------------------------------
mod_toolchain() {
    log "[toolchain] зависимости сборки Theos..."
    need_sudo build-essential fakeroot rsync curl perl zip git libxml2 libtinfo6 make cmake xz-utils || true

    log "[toolchain] git clone --recursive theos..."
    if [[ ! -d "$THEOS" ]]; then
        git clone --recursive https://github.com/theos/theos.git "$THEOS"
    else
        (cd "$THEOS" && git submodule update --init --recursive || true)
    fi
    grep -q '>>> mini-ios >>>' ~/.bashrc 2>/dev/null || cat >> ~/.bashrc <<'EOF'

# >>> mini-ios >>>
export THEOS=$HOME/theos
export PATH="$THEOS/toolchain/linux/iphone/bin:$THEOS/bin:$HOME/ios-toolchain/bin:$HOME/.local/bin:$PATH"
# <<< mini-ios <<<
EOF
    export PATH="$THEOS/toolchain/linux/iphone/bin:$THEOS/bin:$HOME/ios-toolchain/bin:$HOME/.local/bin:$PATH"

    log "[toolchain] toolchain (L1ghtmann llvm-project, static iOS clang)..."
    if [[ ! -x "$THEOS/toolchain/linux/iphone/bin/clang" ]]; then
        ARCH="$(uname -m)"
        curl -sL "https://github.com/L1ghtmann/llvm-project/releases/latest/download/iOSToolchain-$ARCH.tar.xz" \
            | tar -xJ -C "$THEOS/toolchain/"
    fi
    [[ -x "$THEOS/toolchain/linux/iphone/bin/clang" ]] \
        && echo "OK: $($THEOS/toolchain/linux/iphone/bin/clang --version | head -1)" \
        || { echo "ERROR: toolchain не встал." >&2; exit 1; }

    log "[toolchain] patched iPhoneOS.sdk -> $THEOS/sdks/ ..."
    if ! find "$THEOS/sdks" -maxdepth 1 -name '*.sdk' -type d | grep -q .; then
        "$THEOS/bin/install-sdk" latest || {
            curl -fsSL "https://github.com/theos/theos/releases/download/sdk-latest/iPhoneOS.sdk.tar.xz" \
                | tar -xJ -C "$THEOS/sdks/" || \
            echo "ERROR: SDK не встал; поставь вручную в \$THEOS/sdks/." >&2
        }
    fi
    ls -ld "$THEOS"/sdks/*.sdk 2>/dev/null || ls -ld "$THEOS/sdks"/* || true

    log "[toolchain] ldid (ad-hoc подпись)..."
    if ! have ldid; then
        if need_sudo ldid; then
            echo "OK: ldid из apt."
        else
            echo "ldid в apt нет — собираю ProcursusTeam/ldid в ~/ios-toolchain/bin ..."
            mkdir -p ~/src ~/ios-toolchain
            [[ -d ~/src/ldid ]] || git clone --depth=1 https://github.com/ProcursusTeam/ldid.git ~/src/ldid
            cmake -S ~/src/ldid -B ~/src/ldid/build -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_INSTALL_PREFIX="$HOME/ios-toolchain"
            cmake --build ~/src/ldid/build -j"$(nproc)"
            cmake --install ~/src/ldid/build
            export PATH="$HOME/ios-toolchain/bin:$PATH"
        fi
    fi
    have ldid && echo "OK: $(ldid --version 2>&1 | head -1)" || echo "WARN: ldid не найден — подпись будет пропущена."

    log "[toolchain] package_ipa.sh (Theos staged .app -> Payload -> ldid -S -> .ipa)..."
    mkdir -p "$PROJ_DIR" || { echo "ERROR: не могу создать $PROJ_DIR" >&2; exit 1; }
    cat > "$PROJ_DIR/package_ipa.sh" <<'PEOF'
#!/usr/bin/env bash
# package_ipa.sh: собирает Mach-O arm64 через Theos, отправляет в Payload/App.app,
# подписывает ldid -S (ad-hoc), zip'ит в .ipa. Использовать из папки проекта:
#   cd ~/ios_projects/HelloHybrid && ./package_ipa.sh
set -euo pipefail
export THEOS="${THEOS:-$HOME/theos}"
export PATH="$THEOS/toolchain/linux/iphone/bin:$THEOS/bin:$HOME/ios-toolchain/bin:$HOME/.local/bin:$PATH"
APP_NAME="HelloHybrid"
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$PROJ/packages"
mkdir -p "$OUT"
cd "$PROJ"
# IOS_DEPLOY_TARGET (напр. arm64-apple-ios13.0 или 13.0) приходит из Tier-B
# workflow; локально не задан — используется TARGET из Makefile проекта.
MAKE_EXTRA=()
if [[ -n "${IOS_DEPLOY_TARGET:-}" ]]; then
    DEPLOY_VER="${IOS_DEPLOY_TARGET##*[^0-9.]}"
    DEPLOY_VER="${DEPLOY_VER:-13.0}"
    MAKE_EXTRA+=(TARGET="iphone:clang:latest:$DEPLOY_VER")
    echo "[IPA] deploy override: ${MAKE_EXTRA[*]}"
fi
echo "[IPA] make package FINALPACKAGE=1 ..."
if ! make package FINALPACKAGE=1 "${MAKE_EXTRA[@]}"; then
    echo "[IPA] WARN: make package упал — пробую голый make (без переупаковки)..."
    make clean >/dev/null 2>&1 || true
    make FINALPACKAGE=1 "${MAKE_EXTRA[@]}" || {
        echo "[IPA] ERROR: сборка не взлетела. Если в логе 'module compiled with Swift X.Y' —" >&2
        echo "[IPA] ERROR: неверная пара тулчейн/SDK, чинится НЕ фолбэком: bash mint_ios_setup.sh --doctor" >&2
        exit 1
    }
fi
BUNDLE="$(find .theos _ -type d -name "$APP_NAME.app" 2>/dev/null | head -1 || true)"
if [[ -z "$BUNDLE" ]]; then
    echo "[IPA] ERROR: не найден staged .app в .theos/" >&2
    exit 1
fi
echo "[IPA] staged app: $BUNDLE"
STAMP="$(date +%Y%m%d-%H%M%S)"
PAYLOAD="$PROJ/Payload"
rm -rf "$PAYLOAD" "$OUT/${APP_NAME}_${STAMP}.ipa"
mkdir -p "$PAYLOAD"
[[ -f "$BUNDLE/Info.plist" ]] || cp "$PROJ/Info.plist" "$BUNDLE/Info.plist" || true
cp -r "$BUNDLE" "$PAYLOAD/${APP_NAME}.app"
if command -v ldid >/dev/null 2>&1; then
    ldid -S "$PAYLOAD/${APP_NAME}.app/$APP_NAME" || true
    echo "[IPA] подписано ldid -S"
else
    echo "[IPA] WARN: ldid отсутствует — подпись пропущена."
fi
(cd "$PROJ" && zip -qry "$OUT/${APP_NAME}_${STAMP}.ipa" Payload)
rm -rf "$PAYLOAD"
echo "[IPA] ГОТОВО: $OUT/${APP_NAME}_${STAMP}.ipa"
ls -la "$OUT/${APP_NAME}_${STAMP}.ipa"
# Верификация без устройства и без Mac: провал любого пункта = провал сборки.
APP_NAME="$APP_NAME" "$PROJ/verify_ipa.sh" "$OUT/${APP_NAME}_${STAMP}.ipa"
PEOF
    chmod +x "$PROJ_DIR/package_ipa.sh"
    cat > "$PROJ_DIR/verify_ipa.sh" <<'VEOF'
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
VEOF
    chmod +x "$PROJ_DIR/verify_ipa.sh"

    log "[toolchain] тестовый проект HelloHybrid (UIViewController/UIView)..."
    mkdir -p "$PROJ_DIR"
    cd "$PROJ_DIR"
    cat > Makefile <<'EOF'
export TARGET = iphone:clang:latest:13.0
export ARCHS = arm64

include $(THEOS)/makefiles/common.mk

APPLICATION_NAME = HelloHybrid
HelloHybrid_FILES = $(wildcard *.m)
# Эта версия Theos требует SWIFT_FILES отдельно от FILES (см. instance/rules.mk:
# правило компиляции Swift читает именно $(SWIFT_FILES)); оба списка линкуются
# в один бинарь таргета.
HelloHybrid_SWIFT_FILES = $(wildcard *.swift)
HelloHybrid_FRAMEWORKS = UIKit Foundation CoreGraphics
HelloHybrid_CFLAGS = -fobjc-arc
HelloHybrid_SWIFTFLAGS = -import-objc-header $(PWD)/Bridging-Header.h \
                         -target arm64-apple-ios13.0
HelloHybrid_INSTALL_TARGET_PROCESSES = HelloHybrid
HelloHybrid_CODESIGN_FLAGS = -S
HelloHybrid_RESOURCE_FILES = Info.plist

include $(THEOS_MAKE_PATH)/application.mk
EOF
    cat > control <<'EOF'
Package: com.example.hellohybrid
Name: HelloHybrid
Version: 1.0.0
Architecture: iphoneos-arm64
Description: Минимальный UIViewController/UIView демо для проверки .ipa-сборщика
Maintainer: Mint <mint@localhost>
Author: Mint <mint@localhost>
Section: Applications
Depends: firmware (>= 13.0)
EOF
    cat > Info.plist <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>com.example.hellohybrid</string>
  <key>CFBundleExecutable</key><string>HelloHybrid</string>
  <key>CFBundleName</key><string>HelloHybrid</string>
  <key>CFBundleVersion</key><string>1.0.0</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>MinimumOSVersion</key><string>13.0</string>
  <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
  <key>UILaunchScreen</key><dict/>
  <key>UIRequiredDeviceCapabilities</key><array><string>arm64</string></array>
</dict>
</plist>
EOF
    cat > main.m <<'EOF'
#import <UIKit/UIKit.h>

@interface HelloHybridAppDelegate : UIResponder <UIApplicationDelegate>
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([HelloHybridAppDelegate class]));
    }
}
EOF
    cat > HelloHybridAppDelegate.m <<'EOF'
#import <UIKit/UIKit.h>
#import <HelloHybrid-Swift.h>

@interface HelloVC : UIViewController
@end

@implementation HelloVC
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemYellowColor];

    // Направление ObjC→Swift: Counter приехал из сгенерированного
    // HelloHybrid-Swift.h (этого файла нет в исходниках — его обязан
    // сгенерировать Theos/swiftc при сборке, см. проверку в логе).
    Counter *c = [[Counter alloc] init];
    (void)[c bump];
    (void)[c bump];
    NSString *t = [NSString stringWithFormat:@"%@\nn=%ld answer=%ld",
        [Counter describe], (long)c.n, (long)[Counter answer]];

    UILabel *l = [[UILabel alloc] initWithFrame:CGRectMake(20, 200, 340, 120)];
    l.numberOfLines = 0;
    l.text = t;
    [self.view addSubview:l];
}
@end

@interface HelloHybridAppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@end

@implementation HelloHybridAppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application; (void)options;
    _window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    _window.rootViewController = [[HelloVC alloc] init];
    [_window makeKeyAndVisible];
    return YES;
}
@end
EOF
    cat > Greeter.h <<'EOF'
#import <Foundation/Foundation.h>

// Чистый ObjC-хелпер без Swift-типов (иначе циклическая зависимость
// через bridging header). Swift вызывает Greeter.greeting() — это
// направление Swift→ObjC.
@interface Greeter : NSObject
+ (NSString *)greeting;
@end
EOF
    cat > Greeter.m <<'EOF'
#import "Greeter.h"

@implementation Greeter
+ (NSString *)greeting {
    return @"Hello from Objective-C";
}
@end
EOF
    cat > Counter.swift <<'EOF'
import UIKit

// Направление Swift→ObjC: Counter дергает ObjC-класс Greeter,
// который виден сюда только через Bridging-Header.h.
@objcMembers
public class Counter: NSObject {
    @objc public var n: Int = 0

    @objc public func bump() -> Int {
        n += 1
        return n
    }

    @objc public static func describe() -> String {
        return "swift says: " + Greeter.greeting()
    }

    @objc public static func answer() -> Int { return 42 }
}
EOF
    cat > Bridging-Header.h <<'EOF'
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "Greeter.h"
EOF

    log "[toolchain] smoke: ./package_ipa.sh"
    ./package_ipa.sh || {
        echo "package_ipa.sh упал (включая verify_ipa.sh). Тихого ObjC-фолбэка нет:" >&2
        echo "если в логе 'module compiled with Swift X.Y' — неверная пара тулчейн/SDK." >&2
        echo "Чинится НЕ фолбэком: bash mint_ios_setup.sh --doctor" >&2
        exit 1
    }
    echo "ГОТОВО [toolchain]: $PROJ_DIR + packages/*.ipa"
}

# --- IDE -------------------------------------------------------------------------
mod_ide() {
    log "[ide] идёт установка Lite XL (portable, C/Lua)..."
    if [[ ! -x "$HOME/.local/bin/lite-xl" ]]; then
        TMP="$(mktemp -d)"
        curl -fsSL https://github.com/lite-xl/lite-xl/releases/download/v2.1.8/lite-xl-v2.1.8-linux-x86_64-portable.tar.gz -o "$TMP/l.tgz" \
            && tar -xzf "$TMP/l.tgz" -C "$TMP" \
            && L="$(find "$TMP" -maxdepth 2 -type f -name 'lite-xl' | head -1)" \
            && [[ -n "$L" ]] && D="$(dirname "$L")" \
            && mkdir -p "$HOME/.local/bin" "$HOME/.local/share/lite-xl" \
            && cp "$L" "$HOME/.local/bin/lite-xl" && chmod +x "$HOME/.local/bin/lite-xl" \
            && cp -r "$D/data/"* "$HOME/.local/share/lite-xl/" \
            ; rm -rf "$TMP"
    fi
    have lite-xl || [[ -x "$HOME/.local/bin/lite-xl" ]] || { echo "WARN: lite-xl не встал — fallback: Geany."; need_sudo geany || true; }

    log "[ide] clangd..."
    have clangd || need_sudo clangd || true

    log "[ide] тема Xcode-dark + Ctrl+B плагин..."
    mkdir -p ~/.config/lite-xl/colors ~/.config/lite-xl/plugins
    [[ -f ~/.config/lite-xl/colors/xcode-dark.lua ]] || cat > ~/.config/lite-xl/colors/xcode-dark.lua <<'EOF'
local style = require "core.style"
style.background = { 30, 30, 32 }
style.background2 = { 38, 38, 42 }
style.text = { 220, 220, 224 }
style.caret = { 10, 132, 255 }
style.selection = { 62, 80, 120 }
style.line_number = { 120, 120, 128 }
style.line_highlight = { 44, 44, 48 }
style.scrollbar = { 90, 90, 96 }
return style
EOF
    [[ -f ~/.config/lite-xl/init.lua ]] || cat > ~/.config/lite-xl/init.lua <<'EOF'
-- clangd дефолтом для C/C++/ObjC; sourcekit-lsp подхватится, если стоит host-Swift
local ok, lspc = pcall(require, "plugins.lsp.config")
if ok then
  lspc.clangd = { command = { "clangd", "--background-index" } }
end
EOF
    [[ -f ~/.config/lite-xl/plugins/ipa-build.lua ]] || cat > ~/.config/lite-xl/plugins/ipa-build.lua <<'EOF'
local command = require "core.command"
local keymap = require "core.keymap"
local core = require "core"
command.add(nil, {
    ["ipa:build"] = function()
        local dir = (os.getenv("HOME") or "~") .. "/ios_projects/HelloHybrid"
        local okp, proc = pcall(require, "core.process")
        if okp then
            local p = proc.start({ "bash", "-lc", "cd '" .. dir .. "' && ./package_ipa.sh" })
            p:wait()
            core.log("ipa:build завершён")
        else
            os.execute("bash -lc \"cd '" .. dir .. "' && ./package_ipa.sh\"")
        end
    end,
})
keymap.add({ ["ctrl+b"] = "ipa:build" })
EOF
    echo "OK: [ide] Lite XL + clangd + Ctrl+B (package_ipa.sh в ~/ios_projects/HelloHybrid)."
}

# --- VISUAL ----------------------------------------------------------------------
mod_visual() {
    log "[visual] Plank + Picom..."
    need_sudo plank picom wmctrl || true

    log "[visual] picom.conf: backend glx, vsync true, без flicker..."
    mkdir -p ~/.config
    cat > ~/.config/picom.conf <<'EOF'
# picom: прозрачное стекло/blur для Linux Mint (Cinnamon/XFCE).
# БЕЗ лагов на слабом железе: backend glx, vsync true, фиксированные step.
backend = "glx";
vsync = true;
use-damage = true;
crop-shadow-to-monitor = true;
corner-radius = 12;
round-borders = 1;
shadow = true;
shadow-radius = 18;
shadow-opacity = 0.45;
shadow-offset-x = -15;
shadow-offset-y = -15;
fading = true;
fade-in-step = 0.06;
fade-out-step = 0.06;
fade-delta = 8;
inactive-opacity = 0.96;
active-opacity = 1.0;
frame-opacity = 0.9;
EOF
    echo "OK: ~/.config/picom.conf"

    log "[visual] автозапуск Plank..."
    mkdir -p ~/.config/autostart
    cat > ~/.config/autostart/plank.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Plank
Exec=plank
X-GNOME-Autostart-enabled=true
EOF

    log "[visual] gsettings Cinnamon (best-effort)..."
    have gsettings && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] && {
        gsettings set org.cinnamon.desktop.interface gtk-theme "WhiteSur-Dark" 2>/dev/null || true
        gsettings set org.cinnamon.desktop.interface icon-theme "WhiteSur" 2>/dev/null || true
        gsettings set org.cinnamon.desktop.wm.preferences theme "WhiteSur-Dark" 2>/dev/null || true
        gsettings set org.cinnamon.desktop.interface cursor-theme "capitaine-cursors" 2>/dev/null || true
        echo "OK: темы применены (если пакеты WhiteSur установлены)."
    } || echo "WARN: gsettings/DE отсутствует — примени темы вручную в Параметрах."
    echo "OK: [visual] picom запускается: picom -b (или автостарт в session manager)."
}

# --- UNINSTALL --------------------------------------------------------------------
mod_uninstall() {
    log "[uninstall] откат userspace-изменений..."
    # Сгенерированные *-Swift.h (не коммитить генерат — и не оставлять после сноса).
    find "$HOME/ios_projects" -name '*-Swift.h' -delete 2>/dev/null || true
    rm -rf "$THEOS" "$HOME/ios_projects" "$HOME/ios-toolchain" \
           "$HOME/.cache/srk801-sdk" \
           ~/.config/lite-xl ~/.config/picom.conf ~/.config/autostart/plank.desktop
    rm -f ~/.local/bin/lite-xl
    rm -rf ~/.local/share/lite-xl ~/.local/share/applications/lite-xl.desktop
    sed -i '/# >>> mini-ios >>>/,/# <<< mini-ios <<</d' ~/.bashrc || true
    if have gsettings && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
        gsettings set org.cinnamon.desktop.interface gtk-theme "Mint-Y-Dark" 2>/dev/null || true
        gsettings set org.cinnamon.desktop.interface icon-theme "Mint-Y" 2>/dev/null || true
        gsettings set org.cinnamon.desktop.wm.preferences theme "Mint-Y-Dark" 2>/dev/null || true
        gsettings set org.cinnamon.desktop.interface cursor-theme "DMZ-White" 2>/dev/null || true
    fi
    echo "Откатил look & configs. Mint снова выглядит как Mint."
    echo "Апстакдриные пакеты (plank/picom/geany/lite-xl) НЕ удалён — при желании:"
    echo "  sudo apt remove plank picom geany"
    echo "Драйвера/ядро/GRUB НЕ ТРОНУТЫ."
}

# --- main --------------------------------------------------------------------------
[[ "$DO_UNINSTALL" == "1" ]] && { mod_uninstall; exit 0; }
[[ "$DO_DOCTOR" == "1" ]] && { swift_doctor; exit $?; }
[[ -n "$REMOTE_BUILD" ]] && { remote_swift_build "$REMOTE_BUILD"; exit $?; }
[[ -n "$WITH_SWIFT" ]] && mod_swift
[[ "$DO_TOOLCHAIN" == "1" ]] && mod_toolchain
[[ "$DO_IDE" == "1" ]] && mod_ide
[[ "$DO_VISUAL" == "1" ]] && mod_visual
echo "==============================================="
echo "ГОТОВО. Ядро/драйвера/GRUB не тронуты — откат: $0 --uninstall"
