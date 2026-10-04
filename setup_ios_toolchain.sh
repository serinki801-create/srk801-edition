#!/usr/bin/env bash
# ============================================================================
# setup_ios_toolchain.sh — Нативный iOS-тулчейн на Arch Linux (БЕЗ VM/Docker)
# ============================================================================
# КОНВЕЙЕР: clang (Arch) + Theos (официальный installer) + iPhoneOS.sdk +
# Swift-toolchain (kabiroberai, ставит сам installer) + ld64 + rcodesign
# (cargo) -> .app -> ad-hoc/real подпись -> Payload.zip -> .ipa
#
# ЧТО ДЕЛАЕТ:
#   1. Зависимости pacman (включая ncurses, cmake, rust, autoconf-набор для
#      запасного cctools) + host-Swift через paru/AUR (best-effort).
#   2. Шим update-alternatives (его зовёт install-theos, а в Arch его нет).
#   3. THEOS + PATH в ~/.bashrc (идемпотентно).
#   4. Официальный Theos installer НЕ от root, с ответом 'y' (Swift-toolchain).
#   5. Проверка: toolchain clang, iPhoneOS.sdk, swiftc; fallback cctools-port
#      ТОЛЬКО если в toolchain нет Apple-линковщика.
#   6. cargo install apple-codesign -> rcodesign (ad-hoc + real certs).
#   7. Генерация трибридного демо ~/ios_projects/HelloHybrid
#      (main.m + SwiftLogic.swift + Theos-Makefile + control + Info.plist).
#   8. Smoke test: make package FINALPACKAGE=1, иначе fallback-сборка;
#      печать итогового пути .ipa.
#
# ЗАПУСК (Arch, обычным пользователем, НЕ root):
#   bash setup_ios_toolchain.sh [--no-swift] [--yes]
# ============================================================================
set -euo pipefail

WITH_SWIFT_TOOLCHAIN="y"
ASSUME_YES=""
for arg in "$@"; do
    case "$arg" in
        --no-swift) WITH_SWIFT_TOOLCHAIN="n" ;;
        --yes|-y) ASSUME_YES="--noconfirm" ;;
        --help|-h)
            echo "Usage: bash setup_ios_toolchain.sh [--no-swift] [--yes]"
            echo "  --no-swift  облегчённый тулчейн без Swift (только ObjC)"
            exit 0 ;;
        *) echo "Неизвестный флаг: $arg" >&2; exit 1 ;;
    esac
done

if [[ ! -f /etc/arch-release ]]; then
    echo "ERROR: это не Arch Linux." >&2; exit 1
fi
if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
    echo "ERROR: НЕ запускай от root: официальный install-theos отказывается" >&2
    echo "работать под root (exit 1). Запускай обычным пользователем." >&2
    exit 1
fi
command -v sudo >/dev/null || { echo "ERROR: нужен sudo." >&2; exit 1; }

export THEOS="${THEOS:-$HOME/theos}"
PROJ_DIR="$HOME/ios_projects/HelloHybrid"

# --- 1. Зависимости -----------------------------------------------------------
echo "==> [1/8] Зависимости pacman..."
sudo pacman -Syu $ASSUME_YES --needed \
    base-devel clang llvm lld make perl git fakeroot zip unzip xz \
    openssl openssh rsync cmake ninja pkg-config ncurses libbsd libxml2 \
    curl autoconf automake libtool rust ncurses
# libtapi note: для запасного cctools без tapi линковка .tbd не взлетит —
# поэтому cctools только fallback, основной линковщик — из тулчейна Theos.

# Host-Swift (для подсветки/SPM, НЕ для iOS — iOS-Swift даёт installer):
# swift-language есть ТОЛЬКО в AUR (в extra его нет!), берём swift-bin.
if ! command -v swiftc >/dev/null 2>&1; then
    echo "Host-Swift через AUR (best-effort, не валит установку)..."
    if ! command -v paru >/dev/null 2>&1; then
        if (git clone https://aur.archlinux.org/paru.git /tmp/paru-build 2>/dev/null \
            && cd /tmp/paru-build && makepkg -si --noconfirm); then
            echo "OK: paru установлен."
        else
            echo "WARN: paru не собрался, host-Swift пропускаем (iOS-Swift не пострадает)."
        fi
    fi
    if command -v paru >/dev/null 2>&1; then
        paru -S $ASSUME_YES --needed swift-bin \
            && echo "OK: host swift: $(swiftc --version 2>/dev/null | head -1)" \
            || echo "WARN: swift-bin не встал, продолжаем без host-Swift."
    fi
fi

# --- 2. Шим update-alternatives -------------------------------------------------
# install-theos unconditionally calls `update-alternatives --set fakeroot`
# (Debian-специфика). В Arch этой команды нет -> installer упал бы с exit 10.
if ! command -v update-alternatives >/dev/null 2>&1; then
    echo "Ставлю шим update-alternatives (нужен install-theos)..."
    sudo tee /usr/local/bin/update-alternatives >/dev/null <<'EOF'
#!/usr/bin/env bash
# Minimal shim for Theos installer (Arch has no Debian alternatives system).
if [[ "${1:-}" == "--set" && -n "${2:-}" && -n "${3:-}" ]]; then
    if [[ -e "$3" && ! -e "/usr/bin/$2" ]]; then ln -sf "$3" "/usr/bin/$2"; fi
    exit 0
fi
echo "update-alternatives shim: unsupported args: $*" >&2
exit 0
EOF
    sudo chmod +x /usr/local/bin/update-alternatives
fi

# --- 3. THEOS + PATH в .bashrc (идемпотентно) ------------------------------------
echo "==> [2/8] Переменные окружения..."
touch ~/.bashrc
if ! grep -q '>>> ios-toolchain (setup_ios_toolchain.sh) >>>' ~/.bashrc; then
    cat >> ~/.bashrc <<'EOF'

# >>> ios-toolchain (setup_ios_toolchain.sh) >>>
export THEOS=$HOME/theos
export PATH="$HOME/ios-toolchain/bin:$HOME/.cargo/bin:$PATH"
# <<< ios-toolchain <<<
EOF
    echo "OK: блок добавлен в ~/.bashrc"
else
    echo "OK: блок уже есть в ~/.bashrc"
fi
export PATH="$HOME/ios-toolchain/bin:$HOME/.cargo/bin:$PATH"

# --- 4. Официальный Theos installer -----------------------------------------------
echo "==> [3/8] Theos installer (официальный, тулчейн Swift: $WITH_SWIFT_TOOLCHAIN)..."
if [[ -x "$THEOS/toolchain/linux/iphone/bin/clang" ]]; then
    echo "OK: тулчейн уже на месте, installer пропускаем (обновление: \$THEOS/bin/update-theos)."
else
    # Качаем отдельно и проверяем: пустой вывод curl + bash -c "" был бы
    # тихим no-op (set -e его не поймает). Так — громкая ошибка.
    curl -fsSL https://raw.githubusercontent.com/theos/theos/master/bin/install-theos \
        -o /tmp/install-theos.sh
    [[ -s /tmp/install-theos.sh ]] || { echo "ERROR: не скачался install-theos." >&2; exit 1; }
    # Installer интерактивен ровно в одном месте (Swift toolchain y/n).
    printf '%s\n' "$WITH_SWIFT_TOOLCHAIN" | bash /tmp/install-theos.sh
    rm -f /tmp/install-theos.sh
fi
[[ -x "$THEOS/toolchain/linux/iphone/bin/clang" ]] \
    || { echo "ERROR: нет $THEOS/toolchain/linux/iphone/bin/clang после installer." >&2; exit 1; }
echo "OK: $($THEOS/toolchain/linux/iphone/bin/clang --version | head -1)"
echo "SDK: $(ls -d "$THEOS"/sdks/iPhoneOS*.sdk 2>/dev/null | head -1 || echo 'НЕ НАЙДЕН')"

# --- 5. Apple-линковщик: проверка + fallback cctools-port ---------------------------
echo "==> [4/8] Проверка Apple-линковщика..."
TC_LD="$(find "$THEOS/toolchain" \( -name 'ld64' -o -name 'ld' \) -type f 2>/dev/null | head -1 || true)"
if [[ -n "$TC_LD" ]]; then
    echo "OK: линковщик из тулчейна Theos: $TC_LD"
else
    echo "В тулчейне нет ld — собираю cctools-port (tpoechtrager) как fallback..."
    echo "Это ~10-20 минут на ASUS X555L. Флаги сверены с issue #3/#6 апстрима:"
    echo "configure --target=arm-apple-darwin (именно arm, НЕ arm64/arm64 —"
    echo "иначе config.sub падает), а -arch arm64 отдаём уже clang-обёртке."
    mkdir -p ~/src ~/ios-toolchain
    if [[ ! -d ~/src/cctools-port ]]; then
        git clone --depth=1 https://github.com/tpoechtrager/cctools-port.git ~/src/cctools-port
    fi
    cd ~/src/cctools-port/cctools
    ./configure --prefix="$HOME/ios-toolchain" --target=arm-apple-darwin
    make -j"$(nproc)"
    make install
    TC_LD="$(find "$HOME/ios-toolchain" \( -name 'ld64' -o -name 'ld' \) -type f 2>/dev/null | head -1 || true)"
    [[ -n "$TC_LD" ]] || { echo "ERROR: cctools собрался, но ld не найден." >&2; exit 1; }
    echo "OK: fallback-линковщик: $TC_LD"
fi

# --- 6. Подпись: rcodesign из cargo ---------------------------------------------------
echo "==> [5/8] Подпись (apple-codesign -> rcodesign)..."
if ! command -v rcodesign >/dev/null 2>&1; then
    # Сборка ~5-15 минут; нужен cargo (пакет rust уже стоит), cmake+perl есть.
    cargo install apple-codesign
    export PATH="$HOME/.cargo/bin:$PATH"
fi
rcodesign --version
echo "OK: голый 'rcodesign sign <path>' = ad-hoc подпись (без сертификата);"
echo "с флагом --p12-file — настоящая подпись твоим Apple-сертификатом."

# --- 7. Трибридный демо-проект ----------------------------------------------------------
echo "==> [6/8] Демо-проект $PROJ_DIR ..."
mkdir -p "$PROJ_DIR"
cd "$PROJ_DIR"

cat > Makefile <<'EOF'
# HelloHybrid — трибрид Objective-C + Swift -> .ipa (Theos, Linux, no Mac)
# Сборка:  make package FINALPACKAGE=1   -> packages/*.ipa (или см. fallback)
# Swift:   HelloHybrid_SWIFT_FILES включается только если в тулчейне Theos
#          есть swiftc (kabiroberai toolchain из installer). Иначе — чистый
#          ObjC-билд с HAVE_SWIFT=0 (main.m содержит ObjC-fallback).
export TARGET = iphone:clang:latest:14.0
export ARCHS = arm64
export THEOS_PACKAGE_SCHEME = rootless

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

include $(THEOS_MAKE_PATH)/application.mk
EOF

cat > control <<'EOF'
Package: com.example.hellohybrid
Name: HelloHybrid
Version: 1.0.0
Architecture: iphoneos-arm64
Description: Hybrid ObjC+Swift demo app, built natively on Arch Linux
Maintainer: Arch User <arch@localhost>
Author: Arch User <arch@localhost>
Section: Applications
Depends: firmware (>= 14.0)
EOF

cat > Info.plist <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.example.hellohybrid</string>
    <key>CFBundleExecutable</key>
    <string>HelloHybrid</string>
    <key>CFBundleName</key>
    <string>HelloHybrid</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>MinimumOSVersion</key>
    <string>14.0</string>
    <key>UIDeviceFamily</key>
    <array><integer>1</integer><integer>2</integer></array>
    <key>UILaunchScreen</key>
    <dict/>
    <key>UIRequiredDeviceCapabilities</key>
    <array><string>arm64</string></array>
</dict>
</plist>
EOF

# Без ARC/MRR-специфики: только ivars, без @property — компилируется и так, и так.
cat > main.m <<'EOF'
// HelloHybrid — точка входа Objective-C + окно UIKit.
// При HAVE_SWIFT=1 логику отдаём SwiftLogic (Swift), иначе ObjC-fallback.
#import <UIKit/UIKit.h>

#ifdef HAVE_SWIFT
#import <HelloHybrid-Swift.h>
#endif

@interface AppDelegate : UIResponder <UIApplicationDelegate> {
    UIWindow *_window;
}
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application; (void)options;
    _window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    UIViewController *vc = [[UIViewController alloc] init];
    vc.view.backgroundColor = [UIColor whiteColor];

    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(20, 120, 340, 160)];
    label.numberOfLines = 0;
#ifdef HAVE_SWIFT
    label.text = [NSString stringWithFormat:@"%@\nanswer = %ld",
        [SwiftLogic greeting], (long)[SwiftLogic answer]];
    vc.view.backgroundColor = [SwiftLogic accentColor];
#else
    label.text = @"Hello from Objective-C\n(ObjC fallback: Swift toolchain absent)";
#endif
    [vc.view addSubview:label];

    _window.rootViewController = vc;
    [_window makeKeyAndVisible];
    return YES;
}

@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
EOF

cat > SwiftLogic.swift <<'EOF'
// SwiftLogic — Swift-часть гибрида. Компилируется Theos (XXX_SWIFT_FILES)
// только при Swift-тулчейне; требует public/open для видимости из ObjC.
// Swift runtime встроен в iOS >= 12.2, deployment у нас 14.0 — ок.
import UIKit

@objcMembers
public class SwiftLogic: NSObject {
    @objc public static func greeting() -> String {
        return "Hello from Swift (Theos, Linux-built)"
    }

    @objc public static func accentColor() -> UIColor {
        return UIColor.systemTeal
    }

    @objc public static func answer() -> Int {
        return 42
    }
}
EOF

# Имя обязано быть XXX-Bridging-Header.h — Theos подхватывает автоматически.
cat > HelloHybrid-Bridging-Header.h <<'EOF'
// Bridging header: что из ObjC видно в Swift (Theos импортирует сам).
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
EOF

# Fallback-упаковщик: из staged .app делает подписанный .ipa вручную.
# Нужен, только если `make package` не дал packages/*.ipa (напр., theos
# упёрся в подпись без ldid). Подпись: CERT_P12 -> настоящая, иначе ad-hoc.
cat > package_ipa.sh <<'EOF'
#!/usr/bin/env bash
# Ручная сборка .ipa из .app, собранного Theos: Payload + подпись + zip.
set -euo pipefail
cd "$(dirname "$0")"
APP_NAME="HelloHybrid"
VERSION="1.0.0"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="releases/${APP_NAME}_${VERSION}_${STAMP}.ipa"

BIN="$(find .theos _ -type f -name "$APP_NAME" 2>/dev/null | grep -v dSYM | head -1 || true)"
if [[ -z "$BIN" ]]; then
    # Ищем среди staged .app каталогов любой Mach-O бинарь с нашим именем.
    APPDIR="$(find .theos _ -type d -name '*.app' 2>/dev/null | head -1 || true)"
    [[ -n "$APPDIR" ]] || { echo "ERROR: staged .app не найден. Сначала make." >&2; exit 1; }
    BIN="$APPDIR/$APP_NAME"
    [[ -f "$BIN" ]] || BIN="$(find "$APPDIR" -type f ! -name '*.plist' ! -name '*.png' | head -1)"
fi
[[ -f "$BIN" ]] || { echo "ERROR: бинарь $APP_NAME не найден." >&2; exit 1; }

rm -rf Payload "$OUT"
mkdir -p Payload releases "$APP_NAME.app"
cp -f "$BIN" "$APP_NAME.app/$APP_NAME"
cp -f Info.plist "$APP_NAME.app/Info.plist"
mv "$APP_NAME.app" Payload/

if [[ -n "${CERT_P12:-}" && -f "$CERT_P12" ]]; then
    echo "Настоящая подпись: $CERT_P12"
    : "${CERT_PASS:?нужен CERT_PASS (пароль .p12) в окружении}"
    PASSFILE="$(mktemp)"; printf '%s' "$CERT_PASS" > "$PASSFILE"
    rcodesign sign --p12-file "$CERT_P12" --p12-password-file "$PASSFILE" \
        "Payload/$APP_NAME.app"
    rm -f "$PASSFILE"
else
    echo "Ad-hoc подпись (rcodesign, без сертификата)..."
    rcodesign sign "Payload/$APP_NAME.app"
fi
rm -f "$OUT"
zip -qry "$OUT" Payload
echo "IPA: $(pwd)/$OUT"
EOF
chmod +x package_ipa.sh

# --- 8. Smoke test ------------------------------------------------------------------
echo "==> [7/8] Smoke test: make package FINALPACKAGE=1 ..."
if make package FINALPACKAGE=1; then
    echo "OK: theos-пакет собран (ObjC+Swift)."
else
    echo "WARN: гибридная сборка упала. Ретрай: чистый ObjC (USE_SWIFT=0)..."
    if make clean >/dev/null 2>&1 && make package FINALPACKAGE=1 USE_SWIFT=0; then
        echo "OK: ObjC-пакет собран (Swift-тулчейн чинить отдельно, см. гайд)."
    else
        echo "WARN: make package упал (часто — подпись без ldid)."
        echo "Fallback: make (только билд) + package_ipa.sh (Payload+подпись)..."
        make clean >/dev/null 2>&1 || true
        make FINALPACKAGE=1 USE_SWIFT=0
    fi
fi

echo "==> [8/8] Поиск .ipa ..."
IPA="$(ls -t packages/*.ipa 2>/dev/null | head -1 || true)"
if [[ -z "$IPA" ]]; then
    echo "Theos .ipa нет — запускаю ручной упаковщик..."
    ./package_ipa.sh
    IPA="$(ls -t releases/*.ipa 2>/dev/null | head -1 || true)"
fi
[[ -n "${IPA:-}" ]] || { echo "ERROR: .ipa не получен." >&2; exit 1; }

cat <<EOF
==================================================================
ГОТОВО. Финальный пакет:
  $PROJ_DIR/$IPA
Команда пересборки:  cd $PROJ_DIR && make package FINALPACKAGE=1
Установка:
  - Jailbreak:        scp $IPA root@iphone: && dpkg -i (или Filza);
  - Без джейла:       AltStore/Sideloadly + бесплатный Apple ID
                      (переподпишет, 7 дней) либо свой сертификат.
  - В App Store:      нужен Xcode-archive flow -> см. run_xcode_container.sh
                      (нативная сборка НЕ даёт App Store .ipa с правами).
Перелогинься (или source ~/.bashrc), чтобы подхватить \$THEOS и PATH.
==================================================================
EOF
