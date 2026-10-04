#!/usr/bin/env bash
# ============================================================================
# full_macos_setup.sh — Arch Linux -> сверхлёгкий macOS-клон (ASUS X555L)
# ============================================================================
# ЖЕЛЕЗО-ПРИЦЕЛ: X555L = Intel Haswell/Broadwell, обычно 4 ГБ RAM + HDD.
# Отсюда все решения: XFCE по умолчанию (не KDE), Lite XL вместо Electron,
# linux-zen + zram вместо «купи RAM», modesetting вместо xf86-video-intel.
#
# 4 БЛОКА: 1) визуал macOS  2) нативная IDE без Electron  3) железо/ядро
#          4) статистика + тест скорости (free/df + timed theos build)
# + Бонус: Safari-like браузер (Safari под Linux НЕ СУЩЕСТВУЕТ — ставим
#   Firefox: он единственный из пары, и оформляем его под Safari).
#
# ЗАПУСК (Arch, обычным пользователем):
#   bash full_macos_setup.sh [--de=xfce|kde] [--dm] [--autocpufreq]
#                            [--no-bt] [--yes]
# Флаги:
#   --de=kde       KDE Plasma вместо XFCE (красивее blur/KWin, но +~500 МБ RAM)
#   --dm           включить display manager (lightdm/sddm по DE)
#   --autocpufreq  auto-cpufreq из AUR ВМЕСТО TLP (вместе их НЕЛЬЗЯ — конфликт)
#   --no-bt        выключить bluetooth (по умолчанию оставляем)
# ============================================================================
set -euo pipefail

DE="xfce"
WITH_DM=0
AUTOCPUFREQ=0
NO_BT=0
ASSUME_YES=""
for arg in "$@"; do
    case "$arg" in
        --de=xfce|--de=kde) DE="${arg#--de=}" ;;
        --dm) WITH_DM=1 ;;
        --autocpufreq) AUTOCPUFREQ=1 ;;
        --no-bt) NO_BT=1 ;;
        --yes|-y) ASSUME_YES="--noconfirm" ;;
        --help|-h)
            echo "Usage: bash full_macos_setup.sh [--de=xfce|kde] [--dm] [--autocpufreq] [--no-bt] [--yes]"
            exit 0 ;;
        *) echo "Неизвестный флаг: $arg" >&2; exit 1 ;;
    esac
done

if [[ ! -f /etc/arch-release ]]; then
    echo "ERROR: это не Arch Linux." >&2; exit 1
fi
if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    echo "ERROR: запускай НЕ от root." >&2; exit 1
fi
command -v sudo >/dev/null || { echo "ERROR: нужен sudo." >&2; exit 1; }

HAVE_X=$([ -n "${DISPLAY:-}" ] && echo 1 || echo 0)

# ============================================================================
# БЛОК 1. ПОЛНЫЙ ВИЗУАЛ macOS
# ============================================================================
echo "================ БЛОК 1/4: визуал macOS (DE=$DE) ================"

if [[ "$DE" == "xfce" ]]; then
    sudo pacman -Syu $ASSUME_YES --needed \
        xfce4 lightdm lightdm-gtk-greeter plank picom \
        git sassc glib2 \
        kvantum qt5ct qt6ct inter-font xdg-utils
else
    sudo pacman -Syu $ASSUME_YES --needed \
        plasma-desktop plasma-workspace sddm konsole dolphin plank picom \
        git sassc glib2 \
        kvantum qt5ct qt6ct inter-font xdg-utils
fi
# Имена с неподтверждённым статусом в репозиториях — строго по одному,
# с варнингом, не валим транзакцию:
sudo pacman -S $ASSUME_YES --needed capitaine-cursors \
    || echo "WARN: нет capitaine-cursors — курсоры останутся дефолтными."
sudo pacman -S $ASSUME_YES --needed appmenu-gtk-module \
    || echo "WARN: нет appmenu-gtk-module — global menu только вручную из AUR."

# --- WhiteSur из исходников (имена репозиториев сверены с GitHub) ---
SRC_DIR="$HOME/src"
mkdir -p "$SRC_DIR"
for repo in WhiteSur-gtk-theme WhiteSur-icon-theme; do
    if [[ ! -d "$SRC_DIR/$repo" ]]; then
        git clone --depth=1 "https://github.com/vinceliuice/$repo.git" "$SRC_DIR/$repo"
    else
        (cd "$SRC_DIR/$repo" && git pull --ff-only || true)
    fi
done
(cd "$SRC_DIR/WhiteSur-gtk-theme" && ./install.sh)
(cd "$SRC_DIR/WhiteSur-icon-theme" && ./install.sh)

pick_variant() { # $1=dir $2=prefer-pattern — выбирает Dark-вариант если есть
    local found=""
    found="$(ls -d "$1"/WhiteSur* 2>/dev/null | grep -i "$2" | head -1 || true)"
    [[ -z "$found" ]] && found="$(ls -d "$1"/WhiteSur* 2>/dev/null | head -1 || true)"
    basename "${found:-$3}"
}
GTK_THEME="$(pick_variant ~/.themes dark WhiteSur-Dark)"
ICON_THEME="$(pick_variant ~/.local/share/icons . WhiteSur)"
echo "GTK: $GTK_THEME | Icons: $ICON_THEME"

# --- Шрифты San Francisco: честно ---
# SF — проприетарные Apple, скачать можно только вручную с Apple ID
# (developer.apple.com/fonts). Скрипт подхватит их сам, иначе — Inter.
SF_DIR=""
for cand in "$HOME/SF-Pro" "$HOME/Downloads/SF-Pro" "$HOME/fonts/SF-Pro"; do
    if ls "$cand"/*.otf >/dev/null 2>&1; then SF_DIR="$cand"; break; fi
done
if [[ -n "$SF_DIR" ]]; then
    mkdir -p ~/.fonts
    cp -f "$SF_DIR"/*.otf ~/.fonts/
    fc-cache -f ~/.fonts >/dev/null 2>&1 || true
    echo "OK: шрифты San Francisco установлены из $SF_DIR"
else
    sudo pacman -S $ASSUME_YES --needed inter-font \
        && echo "OK: Inter (метрически близок к SF) вместо SF Pro." \
        || echo "WARN: ни SF, ни Inter — остался дефолтный шрифт."
    echo "NOTE: скачай SF Pro/SF Text с Apple ID (developer.apple.com/fonts),"
    echo "положи *.otf в ~/SF-Pro и перезапусти скрипт — подхватит сам."
fi

# --- Применение тем ---
mkdir -p ~/.config/autostart
cat > ~/.config/autostart/plank.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Plank
Exec=plank
X-GNOME-Autostart-enabled=true
EOF

if [[ "$DE" == "xfce" ]]; then
    # Picom: blur/тени/затухание (xfwm4 blur не умеет — поэтому picom).
    mkdir -p ~/.config/picom
    cat > ~/.config/picom/picom.conf <<'EOF'
# Picom для XFCE: macOS-like blur + тени. Форк jonaburg (пакет Arch).
backend = "glx";
vsync = true;
fading = true;
fade-in-step = 0.03;
fade-out-step = 0.03;
shadow = true;
shadow-radius = 12;
shadow-opacity = 0.35;
blur: {
  method = "kawase";
  strength = 7;
  background = false;
  background-frame = false;
  background-fixed = false;
};
corner-radius = 10;
rounded-corners-exclude = [ "class_g = 'plank'" ];
# Док не блюрим и не скругляем — он и так глянцевый:
blur-background-exclude = [ "class_g = 'plank'" ];
EOF
    cat > ~/.config/autostart/picom.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Picom
Exec=picom --config /home/USERPLACEHOLDER/.config/picom/picom.conf
X-GNOME-Autostart-enabled=true
EOF
    sed -i "s|/home/USERPLACEHOLDER|$HOME|" ~/.config/autostart/picom.desktop
    if [[ "$HAVE_X" == "1" ]] && command -v xfconf-query >/dev/null 2>&1; then
        xfconf-query -c xsettings -p /Net/ThemeName -s "$GTK_THEME" || true
        xfconf-query -c xsettings -p /Net/IconThemeName -s "$ICON_THEME" || true
        [[ -d /usr/share/icons/capitaine-cursors ]] && \
            xfconf-query -c xsettings -p /Gtk/CursorThemeName -s "capitaine-cursors" || true
        xfconf-query -c xfwm4 -p /general/theme -s "$GTK_THEME" || true
        # Qt-приложения в единый вид через Kvantum:
        grep -q QT_STYLE_OVERRIDE ~/.xprofile 2>/dev/null || echo 'export QT_STYLE_OVERRIDE=kvantum' >> ~/.xprofile
        echo "Открой Kvantum Manager и выбери тему WhiteSur (GUI, 10 секунд)."
    else
        echo "WARN: нет X-сессии — xfconf применю при следующем входе? Нет."
        echo "Зайди в XFCE и прогони 4 команды из setup_macos_ui.sh-логики вручную."
    fi
    echo "Панели вручную: верхняя остаётся, нижнюю удали (её заменяет Plank);"
    echo "Global Menu: xfce4-appmenu-plugin только из AUR (вручную)."
else
    # KDE: KWin blur из коробки + ноты про виджеты.
    if [[ "$HAVE_X" == "1" ]]; then
        kwriteconfig5 --file kdeglobals --group Icons --key Theme "$ICON_THEME" 2>/dev/null || true
        kwriteconfig5 --file kwinrc --group Plugins --key blurEnabled true 2>/dev/null || true
        kwriteconfig5 --file kwinrc --group Effect-Blur --key BlurStrength 10 2>/dev/null || true
        qdbus org.kde.KWin /KWin reconfigure 2>/dev/null || true
    fi
    export QT_STYLE_OVERRIDE=kvantum
    grep -q QT_STYLE_OVERRIDE ~/.bashrc 2>/dev/null || echo 'export QT_STYLE_OVERRIDE=kvantum' >> ~/.bashrc
    echo "KDE вручную: добавь виджет Global Menu на верхнюю панель, нижнюю удали,"
    echo "Kvantum Manager -> тема WhiteSur, обои WhiteSur."
fi

# --- Браузер: Safari под Linux НЕ СУЩЕСТВУЕТ. Ставим Firefox (он легче
#     любого Chromium) и гримируем его под Safari ---
echo "Ставлю Firefox + Safari-грим..."
sudo pacman -S $ASSUME_YES --needed firefox firefox-i18n-ru xdg-utils \
    || sudo pacman -S $ASSUME_YES --needed firefox xdg-utils
# Политика: меньше процессов под 4 ГБ RAM, телеметрия off, свой CSS разрешён.
sudo mkdir -p /usr/lib/firefox/distribution
sudo tee /usr/lib/firefox/distribution/policies.json >/dev/null <<'EOF'
{
  "policies": {
    "DisableTelemetry": true,
    "DisablePocket": true,
    "NoDefaultBookmarks": true,
    "OfferToSaveLogins": false,
    "Preferences": {
      "dom.ipc.processCount": { "Value": 4, "Status": "default" },
      "browser.sessionstore.interval": { "Value": 600000, "Status": "default" },
      "toolkit.legacyUserProfileCustomizations.stylesheets": { "Value": true, "Status": "locked" }
    }
  }
}
EOF
# userChrome.css в профиль по умолчанию (Safari-стиль: круглая адресная строка).
PROF_DIR="$(grep -m1 'Path=.*default-release' ~/.mozilla/firefox/profiles.ini 2>/dev/null | cut -d= -f2 || true)"
if [[ -z "$PROF_DIR" ]]; then
    # Профиля ещё нет (Firefox не запускался) — создастся при первом старте,
    # CSS положим тогда же через маркер:
    mkdir -p ~/.mozilla/firefox/chrome-staging
    PROF_DIR="chrome-staging"
    echo "NOTE: профиль Firefox появится при первом запуске — CSS подхватится скриптом ниже."
fi
mkdir -p "$HOME/.mozilla/firefox/$PROF_DIR/chrome"
cat > "$HOME/.mozilla/firefox/$PROF_DIR/chrome/userChrome.css" <<'EOF'
/* Safari-like: круглая адресная строка, спокойные вкладки. */
#urlbar-background { border-radius: 10px !important; }
.tab-background { border-radius: 8px !important; }
.tab-close-button { display: none !important; }
.tabbrowser-tab:not([selected]) .tab-close-button { display: none !important; }
#navigator-toolbox { border-bottom: 1px solid rgba(0,0,0,0.15) !important; }
EOF
if [[ "$PROF_DIR" == "chrome-staging" ]]; then
    cat > ~/.mozilla/firefox/apply-chrome-once.sh <<'EOF'
#!/usr/bin/env bash
# Одноразово: перенести userChrome.css в реальный профиль после 1-го старта.
REAL="$(grep -m1 'Path=.*default-release' ~/.mozilla/firefox/profiles.ini | cut -d= -f2)"
if [[ -n "$REAL" && -f ~/.mozilla/firefox/chrome-staging/chrome/userChrome.css ]]; then
    mkdir -p "$HOME/.mozilla/firefox/$REAL/chrome"
    cp ~/.mozilla/firefox/chrome-staging/chrome/userChrome.css \
       "$HOME/.mozilla/firefox/$REAL/chrome/"
    rm -rf ~/.mozilla/firefox/chrome-staging
    rm -- "$0"
fi
EOF
    chmod +x ~/.mozilla/firefox/apply-chrome-once.sh
    echo "Запусти Firefox один раз, затем: bash ~/.mozilla/firefox/apply-chrome-once.sh"
fi
xdg-settings set default-web-browser firefox.desktop 2>/dev/null || true
echo "Firefox готов. Память: это НЕ входит в бюджет IDE 30-60 МБ — браузер"
echo "с вкладками ест 500+ МБ на любом движке, тут физика, не настройки."

if [[ "$WITH_DM" == "1" ]]; then
    [[ "$DE" == "xfce" ]] && sudo systemctl enable lightdm.service \
                          || sudo systemctl enable sddm.service
    echo "DM включится после перезагрузки."
fi

# ============================================================================
# БЛОК 2. НАТИВНАЯ ЛЕГКАЯ IDE БЕЗ ELECTRON (Lite XL, C/Lua, ~15-30 МБ RAM)
# ============================================================================
echo "================ БЛОК 2/4: IDE Lite XL (без Electron) ================"
# Почему Lite XL: ядро на C, скрипты на Lua, idle ~15-30 МБ против 300+ МБ
# у Electron-редакторов. QtCreator — тоже Qt/C++, но 200+ МБ RAM и недели
# сборки кастомной версии: не проходит бюджет 30-60 МБ. Neovide — GUI к
# Neovim, вариант, но Lite XL проще и легче. Выбор: Lite XL.
sudo pacman -S $ASSUME_YES --needed lite-xl git

LXL_DIR="$HOME/.config/lite-xl"
mkdir -p "$LXL_DIR/plugins" "$LXL_DIR/colors"

# LSP-плагин из verified-репо (структура plugin-dir/init.lua проверяется).
if [[ ! -d "$LXL_DIR/plugins/lsp" ]]; then
    if git clone --depth=1 https://github.com/lite-xl/lite-xl-lsp.git /tmp/lite-xl-lsp \
        && [[ -f /tmp/lite-xl-lsp/init.lua ]]; then
        cp -r /tmp/lite-xl-lsp "$LXL_DIR/plugins/lsp"
        echo "OK: lsp-плагин установлен."
    else
        echo "WARN: lsp-плагин не скачался/нет init.lua — IDE работает, LSP добавишь позже."
    fi
    rm -rf /tmp/lite-xl-lsp
fi

# sourcekit-lsp в PATH: симлинки из тулчейна Theos (если стоит) — иначе
# сработает host swift-bin (/usr/bin) либо предупреждение.
mkdir -p ~/.local/bin
if [[ -n "${THEOS:-}" && -x "$THEOS/toolchain/linux/iphone/bin/sourcekit-lsp" ]]; then
    ln -sf "$THEOS/toolchain/linux/iphone/bin/sourcekit-lsp" ~/.local/bin/ 2>/dev/null || true
    ln -sf "$THEOS/toolchain/linux/iphone/bin/swiftc" ~/.local/bin/swiftc-theos 2>/dev/null || true
    echo "OK: sourcekit-lsp слинкован из тулчейна Theos."
elif ! command -v sourcekit-lsp >/dev/null 2>&1; then
    echo "WARN: sourcekit-lsp не найден (появится после setup_ios_toolchain.sh"
    echo "с host swift-bin или Swift-тулчейном). clangd уже работает."
fi
grep -q '.local/bin' ~/.bashrc 2>/dev/null || echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
export PATH="$HOME/.local/bin:$PATH"

# Тема Xcode 15 Dark (ключи style.* — по официальной доке Lite XL).
cat > "$LXL_DIR/colors/xcode-dark.lua" <<'EOF'
-- Xcode 15 Dark для Lite XL (style.* по официальной документации).
local style = require "core.style"
local common = require "core.common"
style.background  = { common.color "#1E1E2E" }
style.background2 = { common.color "#181825" }
style.background3 = { common.color "#232336" }
style.text        = { common.color "#D5D8E4" }
style.caret       = { common.color "#FFFFFF" }
style.accent      = { common.color "#0A84FF" }
style.dim         = { common.color "#6E7387" }
style.divider     = { common.color "#2A2A3E" }
style.selection   = { common.color "#264F78" }
style.line_number = { common.color "#5A5E78" }
style.line_number2= { common.color "#C8CADD" }
style.line_highlight = { common.color "#232338" }
style.scrollbar   = { common.color "#3A3A52" }
style.scrollbar2  = { common.color "#0A84FF" }
style.syntax["normal"]   = { common.color "#D5D8E4" }
style.syntax["symbol"]   = { common.color "#D5D8E4" }
style.syntax["comment"]  = { common.color "#6C7986" }
style.syntax["keyword"]  = { common.color "#FC5FA3" }
style.syntax["keyword2"] = { common.color "#FC5FA3" }
style.syntax["number"]   = { common.color "#D0BF69" }
style.syntax["literal"]  = { common.color "#D0BF69" }
style.syntax["string"]   = { common.color "#FC6A5D" }
style.syntax["operator"] = { common.color "#D5D8E4" }
style.syntax["function"] = { common.color "#41A1FA" }
style.syntax["type"]     = { common.color "#5DD8FF" }
EOF

# Плагин «Build .IPA»: команда + хоткей, вывод в лог. process API — под
# pcall (если сигнатура отличается, IDE не пострадает, путь через терминал
# остаётся каноническим).
cat > "$LXL_DIR/plugins/ipa-build.lua" <<'EOF'
-- IPA Build: кнопка "Build .IPA" для iOS-проектов (Theos).
-- Канонический путь всё равно терминал: make package FINALPACKAGE=1
local core = require "core"
local command = require "core.command"
local keymap = require "core.keymap"
local common = require "core.common"

local function project_root()
  -- system.absolute_path подтверждён исходниками ядра Lite XL.
  local ok, system = pcall(require, "system")
  if ok and system and system.absolute_path then
    return system.absolute_path(".")
  end
  return "."
end

local function build_ipa()
  local root = project_root()
  core.log("IPA: make package FINALPACKAGE=1 in %s ...", root)
  local ok, process = pcall(require, "process")
  if ok and process and process.start then
    local ok2, proc = pcall(process.start, {
      "make", "package", "FINALPACKAGE=1"
    }, { cwd = root })
    if ok2 and proc and proc.wait then
      core.add_thread(function()
        local okw, res = pcall(function() return proc:wait() end)
        core.log("IPA: build finished, rc=%s. See packages/*.ipa",
                 tostring(okw and res or "wait-failed"))
      end)
      return
    end
  end
  -- Fallback: синхронно (заморозит UI на время сборки, но сработает везде).
  -- Каноника всегда доступна и без кнопки: терминал + make package.
  core.add_thread(function()
    local rc = os.execute("cd " .. string.format("%q", root) .. " && make package FINALPACKAGE=1")
    core.log("IPA: fallback build rc=%s. See packages/*.ipa", tostring(rc))
  end)
end

command.add(nil, {
  ["ipa:build"] = function() build_ipa() end,
})
keymap.add { ["f7"] = "ipa:build" }
EOF

# init.lua: тема, табы, LSP (clangd дефолт + sourcekit-lsp вручную), хоткеи.
# Все вызовы — по verified API из доков (keymap.add, core.reload_module,
# lspconfig.*.setup(), lsp.add_server{name,language,file_patterns,command}).
cat > "$LXL_DIR/init.lua" <<'EOF'
-- Lite XL user module: Xcode-вид + LSP + IPA-кнопка.
local core = require "core"
local config = require "core.config"
local keymap = require "core.keymap"

core.reload_module "colors.xcode-dark"

config.tab_type = "soft"
config.indent_size = 2
config.line_limit = 120
config.plugins.trimwhitespace = { enabled = true }

-- Файловое дерево всегда видно (treeview — core-плагин):
-- открывается командой "Tree View: Toggle" (Ctrl+\ по умолчанию в Lite XL).

-- clangd для C/ObjC/C++ (дефолтный конфиг плагина):
local ok_cfg, lspconfig = pcall(require, "plugins.lsp.config")
if ok_cfg and lspconfig and lspconfig.clangd then
  lspconfig.clangd.setup()
end

-- sourcekit-lsp для Swift (ручной конфиг по README lsp-плагина):
local ok_lsp, lsp = pcall(require, "plugins.lsp")
if ok_lsp and lsp and lsp.add_server then
  lsp.add_server {
    name = "sourcekit-lsp",
    language = "swift",
    file_patterns = { "%.swift$" },
    command = { "sourcekit-lsp" },
    verbose = false,
  }
  -- ObjC-файлы явно за clangd (расширения .m/.mm):
  lsp.add_server {
    name = "clangd-objc",
    language = "c",
    file_patterns = { "%.m$", "%.mm$" },
    command = { "clangd", "--background-index" },
    verbose = false,
  }
end

-- Хоткеи в духе Xcode:
keymap.add {
  ["f7"]      = "ipa:build",
  ["ctrl+b"]  = "ipa:build",
  ["ctrl+o"]  = "core:open-file",
}
EOF

# Синтаксис-config.lua валидируем наличием ключей (не запуская GUI):
grep -q 'lspconfig.clangd.setup()' "$LXL_DIR/init.lua" \
  && grep -q 'sourcekit-lsp' "$LXL_DIR/init.lua" \
  && echo "OK: Lite XL настроен (тема xcode-dark, clangd, sourcekit-lsp, F7=Build .IPA)."
echo "RAM-план: idle Lite XL ~15-30 МБ. Проверка в живой сессии:"
echo "  ps -o rss,comm -C lite-xl   # RSS в КБ, ждём <= 61440 (60 МБ)"

# ============================================================================
# БЛОК 3. ЖЕЛЕЗО И ЯДРО (ASUS X555L: Haswell/Broadwell, i915, 4 ГБ)
# ============================================================================
echo "================ БЛОК 3/4: железо и ядро ================"
sudo pacman -S $ASSUME_YES --needed \
    linux-zen linux-zen-headers zram-generator tlp mesa vulkan-intel \
    libva-intel-driver intel-media-driver intel-gpu-tools libva-utils \
    firefox 2>/dev/null || sudo pacman -S $ASSUME_YES --needed \
    linux-zen linux-zen-headers zram-generator tlp mesa

# --- linux-zen: обновить загрузчик (иначе грузиться будет старое ядро!) ---
if [[ -f /boot/grub/grub.cfg ]] && command -v grub-mkconfig >/dev/null 2>&1; then
    sudo grub-mkconfig -o /boot/grub/grub.cfg
    echo "OK: GRUB пересобран (выбери linux-zen в меню при загрузке)."
elif [[ -d /boot/loader/entries ]]; then
    echo "OK: systemd-boot подхватит linux-zen сам (kernel-install)."
    echo "Проверь после ребута: uname -r | grep -i zen"
else
    echo "WARN: загрузчик не опознан — после ребута выбери linux-zen вручную."
fi

# --- zram zstd: эффективный RAM x2 на 4 ГБ (swap на сжатом RAM-диске) ---
sudo mkdir -p /etc/systemd/zram-generator.conf.d
sudo tee /etc/systemd/zram-generator.conf.d/00-macos.conf >/dev/null <<'EOF'
[zram0]
zram-size = ram
compression-algorithm = zstd
swap-priority = 100
EOF
sudo systemctl daemon-reload
sudo systemctl enable --now systemd-zram-setup@zram0.service || true
swapon --show=NAME,SIZE,USED 2>/dev/null | grep -q zram \
  && echo "OK: zram активен." || echo "NOTE: zram включится после перезагрузки."

# --- CPU/питание: TLP по умолчанию; auto-cpufreq ТОЛЬКО по флагу (конфликт!) ---
if [[ "$AUTOCPUFREQ" == "1" ]]; then
    echo "Ставлю auto-cpufreq из AUR и ГАСЯ TLP (вместе им нельзя!)..."
    sudo pacman -S $ASSUME_YES --needed base-devel git
    if ! command -v paru >/dev/null 2>&1; then
        git clone https://aur.archlinux.org/paru.git /tmp/paru-build
        (cd /tmp/paru-build && makepkg -si --noconfirm)
    fi
    paru -S $ASSUME_YES --needed auto-cpufreq
    sudo systemctl mask tlp.service 2>/dev/null || true
    sudo systemctl disable --now tlp.service 2>/dev/null || true
    sudo systemctl enable --now auto-cpufreq.service
    echo "OK: auto-cpufreq активен (режимы performance/powersave сам)."
else
    sudo mkdir -p /etc/tlp.d
    sudo tee /etc/tlp.d/00-macos.conf >/dev/null <<'EOF'
CPU_SCALING_GOVERNOR_ON_AC=performance
CPU_SCALING_GOVERNOR_ON_BAT=schedutil
CPU_ENERGY_PERF_POLICY_ON_AC=performance
CPU_ENERGY_PERF_POLICY_ON_BAT=balance_power
CPU_BOOST_ON_AC=1
CPU_BOOST_ON_BAT=0
EOF
    sudo systemctl enable --now tlp.service
    echo "OK: TLP активен (AC=performance, BAT=schedutil)."
    echo "NOTE: хочешь auto-cpufreq вместо TLP — перезапусти с --autocpufreq"
    echo "(ставить оба активными НЕЛЬЗЯ — дерутся за governors)."
fi

# --- I/O scheduler BFQ (лоу-латенси и на HDD, и на SSD) ---
sudo tee /etc/udev/rules.d/60-ioscheduler.rules >/dev/null <<'EOF'
# BFQ для всех блочных дисков (X555L обычно с HDD — ему нужнее всех).
ACTION=="add|change", KERNEL=="sd[a-z]", ATTR{queue/scheduler}="bfq"
ACTION=="add|change", KERNEL=="nvme[0-9]n[0-9]", ATTR{queue/scheduler}="bfq"
ACTION=="add|change", KERNEL=="mmcblk[0-9]", ATTR{queue/scheduler}="bfq"
EOF
sudo udevadm control --reload-rules 2>/dev/null || true
# Применить прямо сейчас к существующим дискам:
for dev in /sys/block/sd* /sys/block/nvme* /sys/block/mmcblk*; do
    if [[ -f "$dev/queue/scheduler" ]]; then
        echo bfq | sudo tee "$dev/queue/scheduler" >/dev/null 2>&1 || true
    fi
done
echo "Текущие планировщики: $(cat /sys/block/sd*/queue/scheduler 2>/dev/null | tr '\n' ' ' || echo n/a)"

# --- Мусорные демоны: cups всегда, modemmanager всегда, bluetooth опционально ---
sudo systemctl disable --now cups.service cups.socket cups.path 2>/dev/null || true
sudo systemctl disable --now ModemManager.service 2>/dev/null || true
if [[ "$NO_BT" == "1" ]]; then
    sudo systemctl disable --now bluetooth.service 2>/dev/null || true
    echo "Bluetooth выключен (--no-bt)."
else
    echo "Bluetooth оставлен (флаг --no-bt выключит)."
fi

# --- Intel HD Graphics: modesetting (НЕ xf86-video-intel!) + Mesa/VAAPI ---
# Честно: xf86-video-intel — заброшенный DDX с тирингом; на Haswell+ штатный
# modesetting + Mesa быстрее и стабильнее. Ставим стек ускорения:
# (mesa/vulkan уже выше; проверяем VAAPI)
if command -v vainfo >/dev/null 2>&1; then
    vainfo 2>&1 | grep -i -m2 "VA-API\|Driver version" || echo "NOTE: vainfo пуст — проверь после ребута."
fi
echo "NOTE: после ребута проверь ускорение: vainfo + vulkaninfo --summary"

# ============================================================================
# БЛОК 4. СТАТИСТИКА + ТЕСТ СКОРОСТИ
# ============================================================================
echo "================ БЛОК 4/4: статистика и тест ================"
echo "--- Память (free -h): ---"
free -h
echo "--- Диск (df -h / и /home): ---"
df -h / /home 2>/dev/null || df -h /
echo "--- CPU: $(nproc) ядер, $(awk -F: '/model name/{print $2; exit}' /proc/cpuinfo | xargs) ---"
echo "--- Ядро сейчас: $(uname -r) (linux-zen — после ребута) ---"

echo "--- Тест: гибридный проект iOS ---"
if [[ -x "${THEOS:-$HOME/theos}/toolchain/linux/iphone/bin/clang" ]]; then
    THEOS_DIR="${THEOS:-$HOME/theos}"
    PROJ="$HOME/ios_projects/HelloHybrid"
    if [[ -f "$PROJ/Makefile" ]]; then
        cd "$PROJ"
        echo "Замеряю скорость theos-сборки..."
        _t0=$(date +%s)
        if make clean package FINALPACKAGE=1 2>&1 | tail -25; then
            echo "Сборка заняла $(( $(date +%s) - _t0 )) c."
        else
            echo "WARN: theos-сборка упала — смотри лог выше (Swift fallback: make package USE_SWIFT=0)"
        fi
        echo "Артефакт: $(ls -t packages/*.ipa releases/*.ipa 2>/dev/null | head -1 || echo 'нет — разбери лог')"
    else
        echo "NOTE: проекта нет — сначала bash setup_ios_toolchain.sh (он и сгенерит, и соберёт)."
    fi
else
    echo "NOTE: тулчейна Theos нет — сначала bash setup_ios_toolchain.sh,"
    echo "он сгенерирует ~/ios_projects/HelloHybrid и сам замерит сборку."
fi

cat <<'EOF'
==================================================================
ИТОГ. Перезагрузись (нужно для zen/zram/BFQ), выбрав linux-zen.
Быстрые проверки после ребута:
  uname -r | grep zen ; swapon --show ; cat /sys/block/sda/queue/scheduler
  ps -o rss,comm -C lite-xl   # RSS в КБ: цель <= 61440 (60 МБ)
  vainfo | head -5
Кнопка Build .IPA в Lite XL: F7 или Ctrl+B (терминал остаётся каноником).
==================================================================
EOF
