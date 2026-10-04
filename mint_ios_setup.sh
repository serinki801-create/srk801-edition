#!/usr/bin/env bash
# ============================================================================
# mint_ios_setup.sh — Linux Mint 22.3: станция сборки .ipa (AltStore/Sideloadly)
# ============================================================================
# Модули:
#   --toolchain  Theos + iPhoneOS.sdk + ldid + package_ipa.sh + HelloHybrid
#   --ide        Lite XL (C/Lua, ~20 МБ, НЕ Electron) + clangd + Ctrl+B
#   --visual     Plank + Picom (backend="glx", vsync=true) + gsettings-темы
#   --all        всё (по умолчанию если флагов нет)
#   --uninstall  полный откат в стандартный вид Mint (БЕЗ драйверов/grub)
#
# Правило безопасности: трогаем только $HOME, ~/.config, ~/.local, ~/.bashrc.
# Ядро, модули, firmware, grub, TLP/cpufreq — НЕ ТРОГАЕМ. Перезагрузка не нужна.
# ============================================================================
set -euo pipefail

DO_TOOLCHAIN=0; DO_IDE=0; DO_VISUAL=0; DO_UNINSTALL=0
ASSUME_YES=""
for arg in "$@"; do
    case "$arg" in
        --toolchain) DO_TOOLCHAIN=1 ;;
        --ide) DO_IDE=1 ;;
        --visual) DO_VISUAL=1 ;;
        --all) DO_TOOLCHAIN=1; DO_IDE=1; DO_VISUAL=1 ;;
        --uninstall) DO_UNINSTALL=1 ;;
        --yes|-y) ASSUME_YES="-y" ;;
        --help|-h) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "Неизвестный флаг: $arg" >&2; exit 1 ;;
    esac
done
[[ "$DO_TOOLCHAIN$DO_IDE$DO_VISUAL$DO_UNINSTALL" == "0000" ]] && DO_TOOLCHAIN=1 && DO_IDE=1 && DO_VISUAL=1

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
APP_NAME="HelloHybrid"
PROJ="$HOME/ios_projects/$APP_NAME"
OUT="$PROJ/packages"
mkdir -p "$OUT"
cd "$PROJ"
echo "[IPA] make package FINALPACKAGE=1 ..."
if ! make package FINALPACKAGE=1; then
    echo "[IPA] WARN: make package упал — пробую голый make + ldid-пакировщик..."
    make clean >/dev/null 2>&1 || true
    make FINALPACKAGE=1 USE_SWIFT=0
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
PEOF
    chmod +x "$PROJ_DIR/package_ipa.sh"

    log "[toolchain] тестовый проект HelloHybrid (UIViewController/UIView)..."
    mkdir -p "$PROJ_DIR"
    cd "$PROJ_DIR"
    cat > Makefile <<'EOF'
export TARGET = iphone:clang:latest:14.0
export ARCHS = arm64

USE_SWIFT ?= auto
ifeq ($(USE_SWIFT),auto)
  ifneq (,$(wildcard $(THEOS)/toolchain/linux/iphone/bin/swiftc))
    USE_SWIFT := 1
  else
    USE_SWIFT := 0
  endif
endif

include $(THEOS)/makefiles/common.mk

APPLICATION_NAME = HelloHybrid
HelloHybrid_FILES = main.m
ifeq ($(USE_SWIFT),1)
HelloHybrid_SWIFT_FILES = SwiftLogic.swift
HelloHybrid_CFLAGS += -DHAVE_SWIFT=1
endif
HelloHybrid_FRAMEWORKS = UIKit Foundation CoreGraphics
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
Depends: firmware (>= 14.0)
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
  <key>MinimumOSVersion</key><string>14.0</string>
  <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
  <key>UILaunchScreen</key><dict/>
  <key>UIRequiredDeviceCapabilities</key><array><string>arm64</string></array>
</dict>
</plist>
EOF
    cat > main.m <<'EOF'
#import <UIKit/UIKit.h>
#ifdef HAVE_SWIFT
#import <HelloHybrid-Swift.h>
#endif

@interface HelloVC : UIViewController
@end
@implementation HelloVC
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemYellowColor];
    UILabel *l = [[UILabel alloc] initWithFrame:CGRectMake(20, 200, 340, 80)];
    l.numberOfLines = 0;
#ifdef HAVE_SWIFT
    l.text = [NSString stringWithFormat:@"Hello from Swift!\nanswer=%ld", (long)[SwiftLogic answer]];
#else
    l.text = @"Hello from Objective-C!\nSwift fallback: OFF";
#endif
    [self.view addSubview:l];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)o {
    (void)app; (void)o;
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[HelloVC alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end
int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class])); }
}
EOF
    cat > SwiftLogic.swift <<'EOF'
import UIKit
@objcMembers
public class SwiftLogic: NSObject {
    @objc public static func answer() -> Int { return 42 }
}
EOF
    cat > HelloHybrid-Bridging-Header.h <<'EOF'
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
EOF

    log "[toolchain] smoke: ./package_ipa.sh"
    ./package_ipa.sh || {
        echo "package_ipa.sh упал. Логи: make package FINALPACKAGE=1 USE_SWIFT=0"
        make package FINALPACKAGE=1 USE_SWIFT=0 || true
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
    rm -rf "$THEOS" "$HOME/ios_projects" "$HOME/ios-toolchain" \
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
[[ "$DO_TOOLCHAIN" == "1" ]] && mod_toolchain
[[ "$DO_IDE" == "1" ]] && mod_ide
[[ "$DO_VISUAL" == "1" ]] && mod_visual
echo "==============================================="
echo "ГОТОВО. Ядро/драйвера/GRUB не тронуты — откат: $0 --uninstall"
