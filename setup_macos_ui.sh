#!/usr/bin/env bash
# ============================================================================
# setup_macos_ui.sh — Arch Linux + XFCE в стиле macOS (для ASUS X555L и др.)
# ============================================================================
# ПОЧЕМУ XFCE, а не KDE: X555L — это обычно 4 ГБ RAM + HDD. XFCE ест ~500 МБ
# против ~1 ГБ у Plasma, а WhiteSur рисует XFCE один в один как macOS.
#
# ЧТО ДЕЛАЕТ:
#   1. Ставит: xfce4, lightdm (+greeter), plank, capitaine-cursors,
#      appmenu-gtk-module, git, sassc, glib2.
#   2. Качает ИЗ ИСХОДНИКОВ vinceliuice/WhiteSur-gtk-theme и
#      vinceliuice/WhiteSur-icon-theme (--depth=1) и запускает ./install.sh.
#   3. Применяет темы через xfconf (если есть X-сессия), ставит Plank в
#      автозапуск, ставит обои WhiteSur.
#   4. Опционально (--dm): включает lightdm как display manager.
#
# ЗАПУСК (Arch, обычным пользователем):
#   bash setup_macos_ui.sh [--dm] [--yes]
# ============================================================================
set -euo pipefail

WITH_DM=0
ASSUME_YES=""
for arg in "$@"; do
    case "$arg" in
        --dm) WITH_DM=1 ;;
        --yes|-y) ASSUME_YES="--noconfirm" ;;
        --help|-h)
            echo "Usage: bash setup_macos_ui.sh [--dm] [--yes]"
            exit 0 ;;
        *) echo "Неизвестный флаг: $arg" >&2; exit 1 ;;
    esac
done

if [[ ! -f /etc/arch-release ]]; then
    echo "ERROR: это не Arch Linux." >&2; exit 1
fi
if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    echo "ERROR: запускай НЕ от root (нужен пользователь + sudo)." >&2; exit 1
fi

echo "==> [1/4] Пакеты GUI..."
sudo pacman -Syu $ASSUME_YES --needed \
    xfce4 lightdm lightdm-gtk-greeter plank git sassc glib2
# Необязательное, но полезное — по одному, чтобы один missing не валил всё:
sudo pacman -S $ASSUME_YES --needed capitaine-cursors \
    || echo "WARN: нет capitaine-cursors, курсоры останутся дефолтными."
sudo pacman -S $ASSUME_YES --needed appmenu-gtk-module \
    || echo "WARN: нет appmenu-gtk-module, global menu пропустим."

echo "==> [2/4] WhiteSur из исходников..."
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
(cd "$SRC_DIR/WhiteSur-gtk-theme" && ./install.sh)
(cd "$SRC_DIR/WhiteSur-icon-theme" && ./install.sh)

# Имена установленных вариантов (Dark предпочтительнее для «как мак»).
GTK_THEME="$(ls -d ~/.themes/WhiteSur* 2>/dev/null | grep -i dark | head -1 || true)"
[[ -z "$GTK_THEME" ]] && GTK_THEME="$(ls -d ~/.themes/WhiteSur* 2>/dev/null | head -1 || true)"
GTK_THEME="$(basename "$GTK_THEME" 2>/dev/null || echo WhiteSur-Dark)"
ICON_THEME="$(ls -d ~/.local/share/icons/WhiteSur* 2>/dev/null | head -1 || true)"
ICON_THEME="$(basename "$ICON_THEME" 2>/dev/null || echo WhiteSur)"
echo "Тема GTK: $GTK_THEME | Иконки: $ICON_THEME"

echo "==> [3/4] Применение (xfconf + Plank-автозапуск + обои)..."
mkdir -p ~/.config/autostart
cat > ~/.config/autostart/plank.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Plank
Exec=plank
X-GNOME-Autostart-enabled=true
EOF

if [[ -n "${DISPLAY:-}" ]] && command -v xfconf-query >/dev/null 2>&1; then
    xfconf-query -c xsettings -p /Net/ThemeName -s "$GTK_THEME" || true
    xfconf-query -c xsettings -p /Net/IconThemeName -s "$ICON_THEME" || true
    if [[ -d /usr/share/icons/capitaine-cursors || -d ~/.icons/capitaine-cursors ]]; then
        xfconf-query -c xsettings -p /Gtk/CursorThemeName -s "capitaine-cursors" || true
    fi
    xfconf-query -c xfwm4 -p /general/theme -s "$GTK_THEME" || true
    # Обои WhiteSur на все рабочие столы/мониторы:
    WP="$(find "$SRC_DIR/WhiteSur-gtk-theme" -iname '*.jpg' -o -iname '*.png' 2>/dev/null | head -1 || true)"
    if [[ -n "$WP" ]]; then
        while IFS= read -r prop; do
            xfconf-query -c xfce4-desktop -p "$prop" -s "$WP" || true
        done < <(xfconf-query -c xfce4-desktop -l 2>/dev/null | grep 'last-image' || true)
        echo "Обои: $WP"
    fi
else
    echo "WARN: нет X-сессии (DISPLAY пуст) — темы применятся при следующем"
    echo "входе автоматически? Нет. Зайди в XFCE и выполни вручную:"
    echo "  xfconf-query -c xsettings -p /Net/ThemeName -s \"$GTK_THEME\""
    echo "  xfconf-query -c xsettings -p /Net/IconThemeName -s \"$ICON_THEME\""
fi

if [[ "$WITH_DM" == "1" ]]; then
    echo "==> [4/4] Включаю lightdm..."
    sudo systemctl enable lightdm.service
    echo "OK: lightdm включится после перезагрузки."
else
    echo "==> [4/4] Пропуск DM (флаг --dm не передан). Твой текущий DM не тронут."
    echo "Если DM вообще нет: sudo systemctl enable lightdm.service"
fi

cat <<'EOF'
==================================================================
Готово. Финальные 3 клика для «фулл как мак»:
  1. Верхняя панель уже есть (XFCE). Правый клик по ней -> Панель ->
     Параметры панели -> Строка 0, автоматом скрывать: никогда.
  2. Нижнюю панель-док удали (правый клик -> Удалить) — её заменяет Plank.
     Правый клик по Plank -> Параметры -> тема из WhiteSur (уже ставится
     вместе с GTK-паком), значки 48px, увеличение при наведении: вкл.
  3. Global Menu (опционально): нужен пакет xfce4-appmenu-plugin из AUR
     (вручную: git clone https://aur.archlinux.org/xfce4-appmenu-plugin.git
     && makepkg -si), затем добавить апплет на верхнюю панель.
Перелогинься, чтобы всё применилось.
==================================================================
EOF
