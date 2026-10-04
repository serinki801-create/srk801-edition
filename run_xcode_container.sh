#!/usr/bin/env bash
# ============================================================================
# run_xcode_container.sh — macOS Monterey в Docker + headless-сборка .ipa
# ============================================================================
# Образ: sickcodes/docker-osx:latest + SHORTNAME=monterey
# (тега :auto под Monterey нет — :auto это предсобранная Catalina;
#  документированный путь для Monterey — :latest + SHORTNAME).
# CPU-модель Haswell-noTSX: безопасна для Intel 4-5 поколения (хост может
# быть Haswell/Broadwell; Present-флаги kvm/invtsc — из официального README).
#
# ИСПОЛЬЗОВАНИЕ:
#   bash run_xcode_container.sh start [--gui]  # первый раз — С --gui
#       (ставишь macOS через установщик, создаёшь пользователя, включаешь
#        Remote Login, ставишь Xcode; дальше GUI не нужен)
#   bash run_xcode_container.sh ssh            # shell в macOS по SSH
#   bash run_xcode_container.sh sync           # ~/ios_projects -> гость
#   bash run_xcode_container.sh build <dir> <scheme> [config] [team-id]
#       # headless: archive + exportArchive по SSH, .ipa забирается назад
#   bash run_xcode_container.sh export-template  # шаблон ExportOptions.plist
#   bash run_xcode_container.sh status|stop|logs
#
# ПЕРЕМЕННЫЕ ОКРУЖЕНИЯ (всё переопределяемо):
#   MAC_SSH_USER — пользователь macOS (созданный при установке), по умолч. user
#   TEAM_ID      — Apple Team ID для подписи (иначе спросит флагом)
#   CPU_MODEL    — переопределить CPU-модель QEMU (пусто = Haswell-noTSX)
# ============================================================================
set -euo pipefail

IMAGE="sickcodes/docker-osx:latest"
SHORTNAME="monterey"
CONTAINER="xcode-monterey"
SSH_PORT="50922"
PROJECTS_DIR="$HOME/ios_projects"
BUILD_SUBDIR="build"
MAC_SSH_USER="${MAC_SSH_USER:-user}"
TEAM_ID="${TEAM_ID:-}"
CPU_MODEL="${CPU_MODEL:-Haswell-noTSX}"
CPUID_FLAGS="${CPUID_FLAGS:-kvm=on,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on}"
MASTER_PLIST_URL="https://raw.githubusercontent.com/sickcodes/osx-serial-generator/master/config-custom.plist"

# --- Ресурсы: 80% RAM хоста, все ядра ----------------------------------------
TOTAL_KB=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
RAM_GB=$((TOTAL_KB / 1024 / 1024 * 80 / 100))
((RAM_GB < 4)) && RAM_GB=4   # минимум, иначе QEMU не взлетит
CORES=$(nproc)
echo "Ресурсы контейнера: RAM=${RAM_GB}G (80% хоста), CPU=${CORES} ядер" >&2

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: нет команды '$1' (bash setup_host.sh ставит зависимости)." >&2
        exit 1
    }
}

ssh_guest() {
    # $1... — команда внутри macOS. Ключи вперёд (ssh-copy-id — см. гайд),
    # иначе спросит пароль пользователя macOS.
    ssh -p "$SSH_PORT" \
        -o StrictHostKeyChecking=accept-new \
        -o ConnectTimeout=15 \
        "${MAC_SSH_USER}@127.0.0.1" "$@"
}

wait_tcp() {
    # Ждём порт без внешних зависимостей (bash /dev/tcp). $1=порт, $2=таймаут.
    local port="$1" timeout="${2:-120}" waited=0
    while ! (exec 3<>/dev/tcp/127.0.0.1/"$port") 2>/dev/null; do
        sleep 5
        waited=$((waited + 5))
        if ((waited >= timeout)); then
            return 1
        fi
        echo "  ... жду порт $port (${waited}s)" >&2
    done
    return 0
}

cmd_start() {
    need_cmd docker
    local gui=0
    [[ "${1:-}" == "--gui" ]] && gui=1
    if [[ ! -e /dev/kvm ]]; then
        echo "ERROR: нет /dev/kvm — сначала bash setup_host.sh" >&2
        exit 1
    fi
    mkdir -p "$PROJECTS_DIR"
    if docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
        echo "Контейнер $CONTAINER уже существует — запускаю..."
        docker start "$CONTAINER"
    else
        echo "Качаю образ $IMAGE (первый раз ~20+ ГБ, долго)..."
        docker pull "$IMAGE"
        local x11_args=()
        if ((gui)); then
            [[ -z "${DISPLAY:-}" ]] && {
                echo "ERROR: --gui требует запущенный X11 (нет \$DISPLAY)." >&2
                exit 1
            }
            x11_args=(-v /tmp/.X11-unix:/tmp/.X11-unix -e "DISPLAY=${DISPLAY}")
            echo "Режим GUI (разовая установка macOS/Xcode)."
        else
            echo "Headless-режим (без X11/DISPLAY — максимум скорости)."
        fi
        # Пустой массив под bash 5.x раскрывается в ноль аргументов — ок.
        docker run -d --name "$CONTAINER" \
            --device /dev/kvm \
            -p "${SSH_PORT}:10022" \
            "${x11_args[@]}" \
            -e "RAM=${RAM_GB}" \
            -e "SMP=${CORES}" \
            -e "CORES=${CORES}" \
            -e "CPU=${CPU_MODEL}" \
            -e "CPUID_FLAGS=${CPUID_FLAGS}" \
            -e GENERATE_UNIQUE=true \
            -e "MASTER_PLIST_URL=${MASTER_PLIST_URL}" \
            -e "SHORTNAME=${SHORTNAME}" \
            "$IMAGE"
    fi
    echo "Жду SSH гостя на порту $SSH_PORT (первая загрузка — минуты)..."
    if wait_tcp "$SSH_PORT" 1200; then
        echo "OK: гость отвечает. Проверка SSH-ключей..."
        if ssh_guest 'echo SSH-OK' 2>/dev/null; then
            echo "OK: SSH без пароля работает (ключи настроены)."
        else
            echo "SSH требует пароль/настройку. Если это ПЕРВЫЙ запуск:"
            echo "  1. Подключись GUI/VNC и заверши установку macOS,"
            echo "     создай пользователя, включи Remote Login"
            echo "     (Системные настройки -> Основные -> Общий доступ)."
            echo "  2. С хоста: ssh-copy-id -p $SSH_PORT $MAC_SSH_USER@127.0.0.1"
            echo "     (или укажи своего пользователя: MAC_SSH_USER=имя $0 ssh)"
        fi
    else
        echo "WARNING: порт $SSH_PORT не дождался за 20 мин. Смотри логи:" >&2
        echo "  bash $0 logs" >&2
    fi
}

cmd_sync() {
    need_cmd rsync
    mkdir -p "$PROJECTS_DIR"
    echo "Синхронизация $PROJECTS_DIR -> macOS:~/ios_projects/ ..."
    rsync -avz --progress -e "ssh -p $SSH_PORT -o StrictHostKeyChecking=accept-new" \
        "$PROJECTS_DIR/" "${MAC_SSH_USER}@127.0.0.1:ios_projects/"
    echo "OK: проекты в госте."
}

cmd_export_template() {
    local team="${1:-TEAMID10}"
    cat > "$PROJECTS_DIR/ExportOptions.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>app-store</string>
    <!-- app-store | ad-hoc | enterprise | development -->
    <key>teamID</key>
    <string>${team}</string>
    <key>uploadSymbols</key>
    <true/>
    <key>compileBitcode</key>
    <false/>
</dict>
</plist>
EOF
    echo "Шаблон записан: $PROJECTS_DIR/ExportOptions.plist (teamID=$team)"
    echo "Методы: app-store (App Store), ad-hoc (тестовые девайсы),"
    echo "  enterprise (корпоративный), development (локальная отладка)."
}

cmd_build() {
    need_cmd rsync
    local projdir="${1:?Usage: $0 build <project-dir> <scheme> [config] [team-id]}"
    local scheme="${2:?Usage: $0 build <project-dir> <scheme> [config] [team-id]}"
    local config="${3:-Release}"
    local team="${4:-$TEAM_ID}"
    [[ -z "$team" ]] && {
        echo "ERROR: нужен Team ID: TEAM_ID=... $0 build ... или 4-м аргументом." >&2
        exit 1
    }
    [[ -d "$projdir" ]] || {
        echo "ERROR: нет каталога проекта: $projdir" >&2
        exit 1
    }
    local projname
    projname=$(basename "$projdir")
    local stamp
    stamp=$(date +%Y%m%d-%H%M%S)
    local xcarchive="ios_projects/build/${projname}-${stamp}.xcarchive"
    local exportpath="ios_projects/build/ipa-${stamp}"

    cmd_sync
    cmd_export_template "$team" >/dev/null
    rsync -avz -e "ssh -p $SSH_PORT -o StrictHostKeyChecking=accept-new" \
        "$PROJECTS_DIR/ExportOptions.plist" \
        "${MAC_SSH_USER}@127.0.0.1:ios_projects/build/" >/dev/null

    echo "==> [1/2] xcodebuild archive (может занять 10-60 мин первый раз)..."
    ssh_guest "cd ios_projects/$(basename "$projdir") && \
        xcodebuild -scheme '$scheme' -configuration '$config' \
        -archivePath ~/'$xcarchive' archive"

    echo "==> [2/2] xcodebuild -exportArchive -> .ipa ..."
    ssh_guest "xcodebuild -exportArchive \
        -archivePath ~/'$xcarchive' \
        -exportPath ~/'$exportpath' \
        -exportOptionsPlist ~/ios_projects/build/ExportOptions.plist"

    mkdir -p "$PROJECTS_DIR/$BUILD_SUBDIR"
    rsync -avz -e "ssh -p $SSH_PORT -o StrictHostKeyChecking=accept-new" \
        "${MAC_SSH_USER}@127.0.0.1:${exportpath#ios_projects/}/" \
        "$PROJECTS_DIR/$BUILD_SUBDIR/ipa-${stamp}/" \
        --include='*.ipa' --exclude='*'
    echo "=================================================================="
    echo "Готово: $PROJECTS_DIR/$BUILD_SUBDIR/ipa-${stamp}/"
    ls -lh "$PROJECTS_DIR/$BUILD_SUBDIR/ipa-${stamp}/" || true
}

case "${1:-}" in
    start) shift; cmd_start "$@" ;;
    stop) docker stop "$CONTAINER" ;;
    logs) docker logs -f "$CONTAINER" ;;
    status)
        docker ps --filter "name=$CONTAINER" --format '{{.Names}}: {{.Status}}'
        wait_tcp "$SSH_PORT" 10 && echo "SSH-порт открыт." || echo "SSH-порт закрыт."
        ;;
    ssh) shift; ssh_guest "$@" ;;
    sync) cmd_sync ;;
    build) shift; cmd_build "$@" ;;
    export-template) shift; mkdir -p "$PROJECTS_DIR"; cmd_export_template "${1:-}" ;;
    *)
        echo "Usage: bash $0 {start [--gui]|stop|logs|status|ssh [cmd]|sync|build <dir> <scheme> [config] [team-id]|export-template [team-id]}"
        exit 1
        ;;
esac
