#!/usr/bin/env bash
# ============================================================================
# setup_host.sh — Подготовка Arch Linux хоста под iOS-разработку через Xcode
# ============================================================================
# ЧТО ДЕЛАЕТ:
#   1. Проверяет CPU-флаги виртуализации (vmx/svm), включает KVM
#      (modprobe kvm_intel / kvm_amd), проверяет /dev/kvm.
#   2. Ставит пакеты: docker, qemu-desktop, libvirt, virt-manager,
#      dnsmasq, bridge-utils, xorriso, git, openssh, rsync.
#   3. Добавляет пользователя в группы docker/libvirt/kvm,
#      включает systemctl enable --now docker.service и libvirtd.service.
#   4. Опционально (--theme): ставит KDE + качает WhiteSur-gtk-theme и
#      WhiteSur-icon-theme ИЗ ИСХОДНИКОВ и оформляет Arch «фулл как macOS».
#
# ЗАПУСК (на Arch Linux):
#   bash setup_host.sh [--theme] [--yes]
# После выполнения ПЕРЕЗАЙДИ в сессию (группы) или выполни newgrp docker.
# ============================================================================
set -euo pipefail

THEME=0
ASSUME_YES=""
for arg in "$@"; do
    case "$arg" in
        --theme) THEME=1 ;;
        --yes|-y) ASSUME_YES="--noconfirm" ;;
        --help|-h)
            echo "Usage: bash setup_host.sh [--theme] [--yes]"
            echo "  --theme  KDE + WhiteSur из исходников (вид как macOS)"
            echo "  --yes    не спрашивать подтверждений pacman"
            exit 0
            ;;
        *) echo "Неизвестный флаг: $arg (см. --help)" >&2; exit 1 ;;
    esac
done

if [[ ! -f /etc/arch-release ]]; then
    echo "ERROR: это не Arch Linux (/etc/arch-release отсутствует)." >&2
    echo "Скрипт использует pacman и рассчитан только на Arch." >&2
    exit 1
fi

# Реального пользователя определяем даже под sudo.
REAL_USER="${SUDO_USER:-$USER}"
if [[ "$REAL_USER" == "root" ]]; then
    echo "ERROR: запускай НЕ от root (нужен обычный пользователь + sudo)." >&2
    echo "Правильно: bash setup_host.sh (sudo спросят внутри)." >&2
    exit 1
fi

# --- 1. Аппаратная виртуализация -------------------------------------------
echo "==> [1/4] Проверка виртуализации CPU..."
if grep -Eq '^(flags.*)(vmx|svm)' /proc/cpuinfo; then
    echo "CPU-флаги виртуализации найдены: $(grep -m1 -o -E 'vmx|svm' /proc/cpuinfo)"
else
    echo "ERROR: в /proc/cpuinfo нет vmx/svm. Включи VT-x / AMD-V в BIOS/UEFI." >&2
    exit 1
fi

if grep -q GenuineIntel /proc/cpuinfo; then
    KVM_MOD="kvm_intel"
elif grep -q AuthenticAMD /proc/cpuinfo; then
    KVM_MOD="kvm_amd"
else
    KVM_MOD="kvm"
fi
echo "Модуль KVM: $KVM_MOD"
sudo modprobe kvm 2>/dev/null || true
sudo modprobe "$KVM_MOD" 2>/dev/null || true

if [[ ! -e /dev/kvm ]]; then
    echo "ERROR: /dev/kvm не появился после modprobe $KVM_MOD." >&2
    echo "Проверь BIOS (VT-x/AMD-V) и что ядро собрано с CONFIG_KVM." >&2
    exit 1
fi
echo "OK: /dev/kvm существует ($(ls -l /dev/kvm | awk '{print $1, $3, $4}'))"

# MSR-игнор как в официальном гайде Docker-OSX (иначе macOS-гость капризничает).
if [[ -f /sys/module/kvm/parameters/ignore_msrs ]]; then
    echo 1 | sudo tee /sys/module/kvm/parameters/ignore_msrs >/dev/null
    echo "OK: kvm ignore_msrs=1"
fi

# --- 2. Пакеты ---------------------------------------------------------------
echo "==> [2/4] Установка пакетов..."
sudo pacman -Syu $ASSUME_YES --needed \
    docker qemu-desktop libvirt virt-manager dnsmasq bridge-utils \
    edk2-ovmf xorriso git base-devel openssh rsync

# --- 3. Группы и службы --------------------------------------------------------
echo "==> [3/4] Группы и службы..."
sudo usermod -aG docker,libvirt,kvm "$REAL_USER"
echo "OK: $REAL_USER добавлен в группы docker, libvirt, kvm"

sudo systemctl enable --now docker.service
sudo systemctl enable --now libvirtd.service
echo "OK: службы docker.service и libvirtd.service включены и запущены"

if ! sudo systemctl is-active --quiet docker.service; then
    echo "ERROR: docker.service не активен. Смотри: sudo journalctl -u docker" >&2
    exit 1
fi
echo "OK: docker отвечает: $(sudo docker version --format '{{.Server.Version}}' 2>/dev/null || echo '?')"

# --- 4. Опционально: визуал «фулл как macOS» из исходников ----------------------
if [[ "$THEME" == "1" ]]; then
    echo "==> [4/4] Тема macOS: KDE + WhiteSur из исходников..."
    sudo pacman -S $ASSUME_YES --needed \
        plasma-desktop plasma-workspace sddm konsole dolphin plank \
        sassc glib2

    SRC_DIR="$HOME/src"
    mkdir -p "$SRC_DIR"
    if [[ ! -d "$SRC_DIR/WhiteSur-gtk-theme" ]]; then
        git clone --depth=1 https://github.com/vinceliuice/WhiteSur-gtk-theme.git \
            "$SRC_DIR/WhiteSur-gtk-theme"
    else
        (cd "$SRC_DIR/WhiteSur-gtk-theme" && git pull --ff-only || true)
    fi
    if [[ ! -d "$SRC_DIR/WhiteSur-icon-theme" ]]; then
        git clone --depth=1 https://github.com/vinceliuice/WhiteSur-icon-theme.git \
            "$SRC_DIR/WhiteSur-icon-theme"
    else
        (cd "$SRC_DIR/WhiteSur-icon-theme" && git pull --ff-only || true)
    fi

    # Ставим темы как обычный пользователь (НЕ под sudo — ставится в ~).
    (cd "$SRC_DIR/WhiteSur-gtk-theme" && ./install.sh)
    (cd "$SRC_DIR/WhiteSur-icon-theme" && ./install.sh)

    # Иконки WhiteSur для текущей Plasma-сессии + Plank в автозапуск.
    kwriteconfig5 --file kdeglobals --group Icons --key Theme WhiteSur 2>/dev/null || true
    mkdir -p "$HOME/.config/autostart"
    cat > "$HOME/.config/autostart/plank.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Plank
Exec=plank
X-GNOME-Autostart-enabled=true
EOF
    echo "OK: WhiteSur установлен из исходников."
    echo "Осталось вручную (1 минута):"
    echo "  1. Правый клик по рабочему столу -> Изменить фон -> обои WhiteSur"
    echo "     (лежат в $SRC_DIR/WhiteSur-gtk-theme/.../wallpaper/)."
    echo "  2. Правый клик по панели -> Добавить виджеты -> 'Глобальное меню',"
    echo "     нижнюю панель удалить (её заменяет Plank)."
    echo "  3. Правый клик по Plank -> Параметры -> тема WhiteSur."
    echo "  4. Используй X11-сессию Plasma (Plank не работает под Wayland)."
fi

echo "=================================================================="
echo "Готово. ВАЖНО: перелогинься (или newgrp docker), чтобы подхватить группы:"
echo "  newgrp docker"
echo "Проверка:  docker run --rm hello-world"
echo "Дальше:    bash run_xcode_container.sh start   # macOS Monterey в Docker"
echo "=================================================================="
