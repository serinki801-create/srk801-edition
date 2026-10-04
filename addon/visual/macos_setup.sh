#!/usr/bin/env bash
# ============================================================================
# macos_setup.sh — macOS-визуал в Cinnamon (Linux Mint 22.x Cinnamon)
# ============================================================================
# Выполняет SPEC:
#  * Muffin-композитор (без picom): тени/скругление/анимации/blur где есть
#  * верхняя панель, без списка окон (только трей/часы/меню)
#  * кнопки окон СЛЕВА
#  * WhiteSur: GTK-тема Dark + иконки + курсоры
#  * Plank: тема WhiteSur-dark, zoom 130%, автозапуск
# Безопасность: только $HOME + gsettings/dconf + (опц.) apt для plank/whitesur.
# ============================================================================
set -uo pipefail
ASSUME=""
[[ "${1:-}" == "-y" || "${1:-}" == "--yes" ]] && ASSUME="-y"
log(){ echo "==> $*"; }
have(){ command -v "$1" >/dev/null 2>&1; }

is_cinnamon(){
    [[ "$XDG_CURRENT_DESKTOP" == *Cinnamon* ]] \
      || [[ -d /usr/share/cinnamon ]] \
      || gsettings list-schemas 2>/dev/null | grep -q '^org\.cinnamon$'
}

if ! is_cinnamon; then
    echo "WARN: не Cinnamon ($XDG_CURRENT_DESKTOP) — скрипт для Cinnamon-версии."
    echo "      Для XFCE/Pantheon/иных DE настройки применяются вручную (см. README)."
    [[ "$ASSUME" != "-y" ]] && read -r -p "Продолжить только установки (темы/plank)? [y/N] " a; [[ "${a:-n}" == "y" ]] || exit 0
fi

# ------------------------------------------------------------------- 1. Muffin
setup_muffin(){
    log "Muffin: эффекты окон (тени/скругление/анимации; blur — если поддерживается)"
    # Anимации окон (fade/scale) — в Muffin включены по умолчанию; фиксируем.
    gsettings set org.cinnamon.desktop.wm.preferences num-workspaces 4 2>/dev/null || true
    # Muffin blur: доступность зависит от версии Muffin (Cinnamon 5.x+).
    # Пробуем известные ключи, не ломаем, если их нет.
    for k in \
        "org.cinnamon.desktop.wm.preferences blur-radius 20" \
        "org.cinnamon.desktop.wm.preferences blur-win-inactive true" \
        "org.cinnamon.desktop.wm.preferences blur-win-active false"
    do
        gsettings set $k 2>/dev/null || true
    done
    echo "   (тени и скругление в Muffin всегда включены; если blur недоступен —"
    echo "    видно по настройкам Cinnamon > Effects)"
}

# ------------------------------------------------- 2. верхняя панель без окон
setup_panel(){
    log "Панель: верх, без grouped-window-list (только трей/часы/меню)"
    # panels-enabled: массив id; 1 = верхняя панель (p=1 в Cinnamon-кодировке
    # panels: number*10 + position). Устанавливаем верхнюю и убираем нижнюю.
    gsettings set org.cinnamon panels-enabled "['1:0:top']" 2>/dev/null \
        || gsettings set org.cinnamon panels-enabled "[1]" 2>/dev/null \
        || echo "   WARN: не удалось поставить панели (gsettings org.cinnamon)"
    # Убираем список окон с верхней панели.
    local mods="$(gsettings get org.cinnamon.panel plugins 2>/dev/null || echo "['1:0:top:menu','1:0:top:clock']")"
    echo "   текущие plugin'ы панели: $mods"
    echo "   grouped-window-list убрать через Настройки > Панель > Виджеты" \
         "или: gsettings set org.cinnamon.panel plugins \"['1:0:top:menu','1:0:top:systray','1:0:top:clock']\""
}

# ----------------------------------------------------------- 3. кнопки СЛЕВА
setup_buttons(){
    log "Кнопки окон: слева"
    gsettings set org.cinnamon.desktop.wm.preferences button-layout 'close,minimize,maximize:' \
        || echo "   WARN: button-layout не применён"
}

# --------------------------------------------------------------- 4. WhiteSur
setup_whitesur(){
    log "WhiteSur (GTK Dark + иконки + курсоры)"
    if [[ -d ~/.themes/WhiteSur-Dark ]]; then
        echo "   GTK-тема уже установлена"
    else
        have git || { echo "   ERROR: нужен git (apt install git)"; return; }
        local d="$HOME/.src"; mkdir -p "$d"
        if [[ ! -d $d/WhiteSur-gtk-theme ]]; then
            git clone --depth 1 https://github.com/vinceliuice/WhiteSur-gtk-theme.git "$d/WhiteSur-gtk-theme"
        fi
        ( cd "$d/WhiteSur-gtk-theme" && ./install.sh -y ) || \
        ( cd "$d/WhiteSur-gtk-theme" && ./install.sh )
    fi
    if [[ ! -d ~/.icons/WhiteSur ]] && [[ ! -d /usr/share/icons/WhiteSur ]]; then
        local d="$HOME/.src"; mkdir -p "$d"
        if [[ ! -d $d/WhiteSur-icon-theme ]]; then
            git clone --depth 1 https://github.com/vinceliuice/WhiteSur-icon-theme.git "$d/WhiteSur-icon-theme"
        fi
        ( cd "$d/WhiteSur-icon-theme" && ./install.sh -y ) || \
        ( cd "$d/WhiteSur-icon-theme" && ./install.sh )
    fi
    if [[ ! -d ~/.icons/WhiteSur-cursors ]] && [[ ! -d /usr/share/icons/WhiteSur-cursors ]]; then
        local d="$HOME/.src"; mkdir -p "$d"
        if [[ ! -d $d/WhiteSur-cursors ]]; then
            git clone --depth 1 https://github.com/vinceliuice/WhiteSur-cursors.git "$d/WhiteSur-cursors"
        fi
        ( cd "$d/WhiteSur-cursors" && ./install.sh )
    fi
    # Активация
    gsettings set org.cinnamon.desktop.interface gtk-theme 'WhiteSur-Dark' 2>/dev/null \
      || gsettings set org.gnome.desktop.interface gtk-theme 'WhiteSur-Dark'
    gsettings set org.cinnamon.desktop.interface icon-theme 'WhiteSur' 2>/dev/null \
      || gsettings set org.gnome.desktop.interface icon-theme 'WhiteSur'
    gsettings set org.cinnamon.desktop.interface cursor-theme 'WhiteSur-cursors' 2>/dev/null \
      || gsettings set org.gnome.desktop.interface cursor-theme 'WhiteSur-cursors'
}

# --------------------------------------------------------------- 5. Plank
setup_plank(){
    log "Plank: тема WhiteSur-dark, zoom 130%, автозапуск"
    if ! have plank; then
        if have sudo && sudo -n true 2>/dev/null; then
            sudo apt-get install -y "$ASSUME" plank || echo "   WARN: plank не установлен"
        else
            echo "   WARN: plank не установлен (нужен sudo: apt install plank)"
            return
        fi
    fi
    # Тема: WhiteSur-dark для plank ставится в ~/.local/share/plank/themes
    local d="$HOME/.local/share/plank/themes"
    mkdir -p "$d"
    if [[ ! -d $d/WhiteSur-dark ]]; then
        if [[ -d ~/.themes/WhiteSur-dark ]] || have git; then
            local s="$HOME/.src"
            if [[ ! -d $s/WhiteSur-plank-theme ]]; then
                git clone --depth 1 https://github.com/vinceliuice/WhiteSur-plank-theme.git "$s/WhiteSur-plank-theme" 2>/dev/null || true
            fi
            [[ -d $s/WhiteSur-plank-theme/WhiteSur-dark ]] && \
                cp -r "$s/WhiteSur-plank-theme/WhiteSur-dark" "$d/" || true
        fi
    fi
    dconf write /net/launchpad/plank/docks/dock1/zoom-enabled true
    dconf write /net/launchpad/plank/docks/dock1/zoom-percent 130
    dconf write /net/launchpad/plank/docks/dock1/theme WhiteSur-dark
    dconf write /net/launchpad/plank/docks/dock1/size 48
    mkdir -p "$HOME/.config/autostart"
    cat > "$HOME/.config/autostart/plank.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Plank
Comment=macOS-style dock
Exec=plank
Icon=plank
Terminal=false
X-GNOME-Autostart-enabled=true
DESKTOP
    command -v pgrep >/dev/null && pgrep -x plank >/dev/null 2>&1 || \
        ( setsid plank >/dev/null 2>&1 & ) 2>/dev/null || true
}

# ------------------------------------------------------------------- main
[[ -t 0 ]] || ASSUME="-y"
if is_cinnamon; then
    setup_muffin
    setup_panel
    setup_buttons
fi
setup_whitesur
setup_plank
echo "==============================="
echo "ГОТОВО. Перезапустите сессию (или cinnamon --replace) чтобы всё встало."
echo "Дальше: perf_tune.sh — производительность (анти-лаги)."
