#!/usr/bin/env bash
# ============================================================================
# arch_macos_build.sh — Кастомный Arch ISO в стиле macOS + Docker-OSX
# ============================================================================
# ЧЕСТНО О ВОЗМОЖНОСТЯХ (прочти перед запуском):
#   1. Этот скрипт НЕ делает из Linux macOS и НЕ ставит Xcode на Linux.
#      Xcode существует только для macOS и под Linux не запускается никак.
#   2. Что скрипт РЕАЛЬНО делает:
#      а) собирает Arch ISO (archiso) с KDE Plasma + тема WhiteSur +
#         Global Menu + Plank Dock — визуально близко к macOS;
#      б) готовит Docker-OSX (macOS в KVM-виртуалке), ВНУТРИ которой можно
#         поставить Xcode с Apple ID и собирать .ipa (нужны: /dev/kvm,
#         4+ CPU, 8+ ГБ RAM, SSD; по EULA Apple виртуализация macOS
#         разрешена только на оборудовании Apple).
#   3. Если Мака нет вообще, для .ipa проще облако: EAS Build / Codemagic
#      (собирают iOS без своего Мака, нужен Apple Developer аккаунт).
#
# Запуск: только на Arch Linux, bash arch_macos_build.sh [--docker-only]
# ============================================================================
set -euo pipefail

PROFILE_DIR="$HOME/archlive-macos"
DOCKER_ONLY=0
if [[ "${1:-}" == "--docker-only" ]]; then
    DOCKER_ONLY=1
fi

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: нужна команда '$1'. Установи её и повтори." >&2
        exit 1
    }
}

if [[ ! -f /etc/arch-release ]]; then
    echo "ERROR: это не Arch Linux (/etc/arch-release отсутствует)." >&2
    echo "Сборка archiso возможна только на Arch. Для Docker-OSX части" >&2
    echo "запусти: bash $0 --docker-only (нужны docker + /dev/kvm)." >&2
    exit 1
fi

if [[ "$DOCKER_ONLY" == "0" ]]; then
    echo "==> [1/4] Установка archiso..."
    sudo pacman -Syu --needed --noconfirm archiso git base-devel

    echo "==> [2/4] Профиль releng -> $PROFILE_DIR"
    rm -rf "$PROFILE_DIR"
    cp -r /usr/share/archiso/configs/releng/ "$PROFILE_DIR"

    echo "==> [3/4] Пакеты: KDE Plasma + SDDM + Plank + Docker + шрифты"
    cat >> "$PROFILE_DIR/packages.x86_64" <<'EOF'
plasma-desktop
plasma-workspace
sddm
konsole
dolphin
plank
docker
docker-compose
git
firefox
noto-fonts
noto-fonts-emoji
networkmanager
EOF

    echo "==> [4/4] Кастомизация live-среды (WhiteSur + автологин + Plank)"
    mkdir -p "$PROFILE_DIR/airootfs/etc/sddm.conf.d" \
             "$PROFILE_DIR/airootfs/etc/xdg/autostart" \
             "$PROFILE_DIR/airootfs/usr/share/wallpapers"
    # Автологин live-пользователя в KDE (X11 — Plank дружит только с X11).
    cat > "$PROFILE_DIR/airootfs/etc/sddm.conf.d/autologin.conf" <<'EOF'
[Autologin]
User=arch
Session=plasma.desktop
EOF
    # Plank при старте сессии.
    cat > "$PROFILE_DIR/airootfs/etc/xdg/autostart/plank.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Plank
Exec=plank
X-GNOME-Autostart-enabled=true
EOF
    # Скрипт внешнего вида: запускается ВНУТРИ live-системы один раз.
    cat > "$PROFILE_DIR/airootfs/root/apply-macos-look.sh" <<'EOF'
#!/usr/bin/env bash
# WhiteSur + Global Menu + Plank. Запуск внутри live-сессии (есть интернет).
set -euo pipefail
if ! command -v paru >/dev/null 2>&1; then
    git clone https://aur.archlinux.org/paru.git /tmp/paru
    (cd /tmp/paru && makepkg -si --noconfirm)
fi
paru -S --noconfirm whitesur-gtk-theme whitesur-icon-theme whitesur-cursors
# GTK/иконки/курсоры:
kwriteconfig5 --file kdeglobals --group Icons --key Theme WhiteSur
kwriteconfig5 --file kdeglobals --group General --key ColorScheme WhiteSurDark 2>/dev/null || true
# Обои WhiteSur (первые найденные):
WP=$(find /usr/share/themes /usr/share/wallpapers -iname '*whitesur*' 2>/dev/null | head -1 || true)
if [[ -n "$WP" ]]; then
    plasma-apply-wallpaperimage "$WP" 2>/dev/null || true
fi
echo "Готово. Вручную добавь на верхнюю панель виджет 'Global Menu',"
echo "а нижнюю панель удали — её роль играет Plank (уже в автозапуске)."
echo "Сессия должна быть X11 (Plank не работает под Wayland)."
EOF
    chmod +x "$PROFILE_DIR/airootfs/root/apply-macos-look.sh"

    echo "==> Сборка ISO (долго, нужен интернет и ~8 ГБ места)..."
    sudo mkarchiso -v -w /tmp/archiso-tmp -o ~/archlive-out "$PROFILE_DIR"
    echo "ISO готов: ~/archlive-out/"
fi

# --- Docker-OSX (работает на любом дистрибутиве с docker + KVM) ----------------
echo "==> Docker-OSX: проверка предусловий..."
if [[ ! -e /dev/kvm ]]; then
    echo "WARNING: /dev/kvm отсутствует — macOS в Docker без KVM не взлетит." >&2
    echo "Включи VT-x/AMD-V в BIOS и поставь KVM. Продолжаю генерацию скриптов."
fi
if ! command -v docker >/dev/null 2>&1; then
    echo "Ставлю docker (Arch)..."
    sudo pacman -S --needed --noconfirm docker
    sudo systemctl enable --now docker
    sudo usermod -aG docker "$USER" || true
fi

cat > "$HOME/run-macosx.sh" <<'EOF'
#!/usr/bin/env bash
# Запуск macOS (Monterey) в Docker: внутри — App Store -> Xcode -> .ipa.
# Первый запуск скачивает ~20 ГБ образ. Нужно: /dev/kvm, 4+ CPU, 8+ ГБ RAM.
set -euo pipefail
docker pull sickcodes/docker-osx:latest
# NOTE: по EULA Apple запускай это только на оборудовании Apple.
docker run -it \
    --device /dev/kvm \
    -p 50922:10022 \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -e "DISPLAY=${DISPLAY:-:0.0}" \
    -e GENERATE_UNIQUE=true \
    -e MASTER_PLIST_URL='https://raw.githubusercontent.com/sickcodes/osx-serial-generator/master/config-custom.plist' \
    --memory 8G --cpus 4 \
    sickcodes/docker-osx:latest
echo "Внутри macOS: войди с Apple ID, поставь Xcode из App Store,"
echo "собирай .ipa (Product -> Archive). Нужен Apple Developer аккаунт."
EOF
chmod +x "$HOME/run-macosx.sh"
echo "Скрипт создан: $HOME/run-macosx.sh"
echo "Альтернатива без Мака и виртуалок: EAS Build / Codemagic (облачная сборка .ipa)."
