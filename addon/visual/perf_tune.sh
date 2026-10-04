#!/usr/bin/env bash
# ============================================================================
# perf_tune.sh — "нулевой лаг" на этом железе
# ============================================================================
# Режимы:
#   fast    — максимальная плавность на слабом железе (дёшево для GPU):
#             композитор минимально, plank зум выключен, анимации короткие
#   balance — дефолт: тени/скругление ON, blur и тяжёлые эффекты OFF
#   pretty  — полный визуал (blur-эффекты если DE их умеет)
# Сторонние процессы не трогаются; настройки — только пользовательские.
# ============================================================================
set -uo pipefail
MODE="${1:-balance}"
log(){ echo "==> $*"; }
have(){ command -v "$1" >/dev/null 2>&1; }

is_xfce(){ [[ -n "${XDG_CURRENT_DESKTOP:-}" && "$XDG_CURRENT_DESKTOP" == *XFCE* ]] || pgrep -x xfce4-session >/dev/null; }
is_cinnamon(){ [[ -n "${XDG_CURRENT_DESKTOP:-}" && "$XDG_CURRENT_DESKTOP" == *Cinnamon* ]] || pgrep -x cinnamon >/dev/null; }

case "$MODE" in
fast|balance|pretty) ;;
*) echo "режим: fast|balance|pretty (сейчас: $MODE)"; exit 1 ;;
esac

# ------------------------------------------------------------ композитор
# stack: xfwm4 (рамки/трафик-лайт тема WhiteSur) + picom (fade/тени/blur).
# Два композитора одновременно не работают: xfwm4-композинг всегда OFF,
# эффекты — только picom (glx+vsync на наличии GPU, иначе xrender).
if is_xfce; then
    log "XFCE: режим $MODE (xfwm4-композинг OFF, эффекты через picom)"
    xfconf-query -c xfwm4 -p /general/use_compositing -s false
    xfconf-query -c xfwm4 -p /general/theme -s WhiteSur
fi

if have picom; then
    case "$MODE" in
    fast)
        pkill -x picom 2>/dev/null || true
        log "picom: выключен (максимальная скорость)"
        ;;
    balance|pretty)
        # генерируем конфиг целиком (идемпотентно):
        # blur: balance — только неактивные окна; pretty — все окна
        PCONF="$HOME/.config/picom/picom.conf"
        mkdir -p "$(dirname $PCONF)"
        if [[ "$MODE" == "balance" ]]; then
            BLUR='blur: {
  enabled = true;
  method = "dual_kawase";
  strength = 5;
  radius = 3;
  steps = 2;
  windows = [ "focused = false" ];
}'
        else
            BLUR='blur: {
  enabled = true;
  method = "dual_kawase";
  strength = 5;
  radius = 3;
  steps = 2;
}'
        fi
        cat > $PCONF <<PCONF
# CoreAnimation-style: плавный fade открытия/закрытия/сворачивания,
# тени, frost-размытие. Генерация: perf_tune.sh ($MODE)
backend = "glx";
vsync = true;
use_damage = true;
fade = true;
fade-in-step = 0.08;
fade-out-step = 0.10;
fade-in-delta = 6;
fade-out-delta = 8;
shadow = true;
shadow-radius = 8;
shadow-opacity = 0.32;
shadow-offset-x = -4;
shadow-offset-y = -4;
shadow-exclude = [ "window_type = \\"dock\\"", "class_g = \\"Plank\\"" ];
$BLUR
window-rule = [ "dock:shade|border:0", "class_g = 'Plank':fade:0" ];
PCONF
        pkill -x picom 2>/dev/null; sleep 1
        ( setsid picom -b --config=$PCONF >/dev/null 2>&1 & )
        log "picom: запущен (glx+vsync, fade ~130 мс, тени, blur=$MODE)"
        ;;
    esac
fi

if is_cinnamon; then
    log "Cinnamon/Muffin: режим $MODE"
    # Muffin: анимации окон
    case "$MODE" in
    fast)
        # короткие анимации: гасим "лишние" fade через wm-настройки, если есть
        gsettings set org.cinnamon.desktop.wm.preferences num-workspaces 2 2>/dev/null || true
        ;;
    *)
        gsettings set org.cinnamon.desktop.wm.preferences num-workspaces 4 2>/dev/null || true
        ;;
    esac
fi

# ------------------------------------------------------------------ Plank
case "$MODE" in
fast)
    dconf write /net/launchpad/plank/docks/dock1/zoom-enabled false
    dconf write /net/launchpad/plank/docks/dock1/opacity 100
    ;;
balance)
    dconf write /net/launchpad/plank/docks/dock1/zoom-enabled true
    dconf write /net/launchpad/plank/docks/dock1/zoom-percent 130
    ;;
pretty)
    dconf write /net/launchpad/plank/docks/dock1/zoom-enabled true
    dconf write /net/launchpad/plank/docks/dock1/zoom-percent 140
    ;;
esac

# ------------------------------------------------------ системные мелочи
# Скрепленные-честные правки (swap, частоты CPU, профиль питания) требуют
# sudo и осознанно НЕ выполняются автоматически.
echo "ГОТОВО ($MODE)."
echo "Проверка: откройте 5 окон, alt-tab'ьте, запустите сборку (ipabuild) — всё должно идти ровно."
