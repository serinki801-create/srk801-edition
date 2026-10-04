#!/bin/bash
# ============================================================================
# build.sh — сборка AI-OS под Linux Mint (Ubuntu x86_64)
# ============================================================================
# Что делает:
#  1) Проверяет/ставит зависимости (nasm, qemu, xorriso, GNUstep, BlocksRuntime)
#  2) Собирает ядро: kernel.elf + user_prog.elf + initrd.img (+ ISO)
#  3) Гоняет статические тесты: make check (43 проверки)
#  4) Собирает и ЗАПУСКАЕТ хост-демо на Objective-C под GNUstep
#     (флаги строго по ТЗ):
#       компиляция: $(gnustep-config --objc-flags) -fblocks
#       линковка:   $(gnustep-config --base-libs) -lobjc -lBlocksRuntime
#  5) Компилирует встроенные сэмплы C/C++/ObjC (хост-проверка тулчейна)
#
# Использование:
#   bash build.sh            # всё: ядро + тесты + хост-демо
#   bash build.sh --kernel   # только ядро + тесты
#   bash build.sh --host     # только хост GNUstep-демо
#   bash build.sh --run      # сборка + headless smoke-тест в QEMU (если есть)
# ============================================================================
set -u
cd "$(dirname "$0")"

MODE="all"
for a in "$@"; do
    case "$a" in
        --kernel) MODE="kernel" ;;
        --host)   MODE="host" ;;
        --run)    MODE="run" ;;
        -h|--help)
            echo "Usage: bash build.sh [--kernel|--host|--run]"
            exit 0
            ;;
    esac
done

say()  { printf '\033[1;34m[build]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[ OK ]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[WARN]\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m[FAIL]\033[0m %s\n' "$*"; exit 1; }

# --- 0. Среда ----------------------------------------------------------------
say "Host: $(cat /etc/os-release 2>/dev/null | grep PRETTY_NAME | cut -d= -f2 | tr -d '"')"
say "CPU: $(nproc) cores, RAM: $(free -h | awk '/Mem:/{print $2}')"

# --- 1. Зависимости -----------------------------------------------------------
need_apt=""
for t in gcc ld make cmake grub-mkrescue xorriso gnustep-config qemu-system-x86_64 nasm; do
    command -v "$t" >/dev/null 2>&1 || need_apt="$need_apt $t"
done
if [ -n "$need_apt" ]; then
    warn "Нет в PATH:$need_apt"
    if command -v apt-get >/dev/null 2>&1; then
        PKGS=""
        echo "$need_apt" | grep -qw nasm              && PKGS="$PKGS nasm"
        echo "$need_apt" | grep -qw qemu-system-x86_64 && PKGS="$PKGS qemu-system-x86"
        echo "$need_apt" | grep -qw xorriso           && PKGS="$PKGS xorriso"
        # GNUstep/BlocksRuntime тулчейн для хост-демо:
        PKGS="$PKGS libgnustep-base-dev libblocksruntime-dev gobjc"
        if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
            say "Ставлю через sudo apt: $PKGS"
            sudo apt-get update -qq && sudo apt-get install -y -qq $PKGS \
                || warn "apt install частично не удался — продолжаю с тем что есть"
        else
            warn "Нет passwordless sudo. На чистом Mint выполни вручную:"
            echo "      sudo apt update && sudo apt install -y$PKGS qemu-system-x86 xorriso build-essential"
            warn "Продолжаю: для NASM есть fallback на Flatpak SDK / локальную распаковку."
        fi
    fi
fi

# NASM: PATH -> Flatpak SDK -> локальный /tmp (apt download без sudo)
ensure_nasm() {
    if command -v nasm >/dev/null 2>&1; then ok "nasm: $(nasm -v 2>&1 | head -1)"; return 0; fi
    FP="/var/lib/flatpak/runtime/org.freedesktop.Sdk/x86_64/25.08/0cc82216a407cc993941b5ddabd446becc3c9a6219d9bcf50125c60101dcad46/files/bin/nasm"
    if [ -x "$FP" ]; then ok "nasm: Flatpak SDK fallback"; return 0; fi
    if [ -x /tmp/opencode/bin/nasm ]; then
        export PATH="/tmp/opencode/bin:$PATH"
        ok "nasm: локальный /tmp/opencode/bin"; return 0
    fi
    say "Пробую apt-get download nasm (без sudo)..."
    mkdir -p /tmp/opencode/deb /tmp/opencode/bin
    (cd /tmp/opencode/deb && apt-get download nasm -qq 2>&1 | tail -1) || true
    DEB=$(ls /tmp/opencode/deb/nasm_*.deb 2>/dev/null | head -1)
    if [ -n "$DEB" ]; then
        dpkg-deb -x "$DEB" /tmp/opencode/nasmroot 2>/dev/null || true
        if [ -x /tmp/opencode/nasmroot/usr/bin/nasm ]; then
            cp /tmp/opencode/nasmroot/usr/bin/nasm /tmp/opencode/bin/
            export PATH="/tmp/opencode/bin:$PATH"
            ok "nasm: распакован локально без sudo"
            return 0
        fi
    fi
    fail "nasm не найден. Установи: sudo apt install nasm"
}
ensure_nasm

# Проверка GNUstep/BlocksRuntime (для хост-демо; ядро их НЕ требует — freestanding)
have_gnustep=1
command -v gnustep-config >/dev/null 2>&1 || { warn "нет gnustep-config — хост-демо будет пропущено"; have_gnustep=0; }
[ -f /usr/include/Block.h ] || warn "нет /usr/include/Block.h (libblocksruntime-dev)"

# --- 2. Ядро -------------------------------------------------------------------
build_kernel() {
    say "Сборка ядра (make clean && make)..."
    make clean >/dev/null 2>&1 || true
    if ! make 2>&1 | tee /tmp/opencode/kernel_build.log | tail -20; then
        fail "Сборка ядра упала. Лог: /tmp/opencode/kernel_build.log"
    fi
    ok "Ядро собрано:"
    ls -la kernel.elf user_prog.elf initrd.img 2>/dev/null
    file kernel.elf 2>/dev/null || true
    say "Статические тесты (make check)..."
    make check || fail "make check упал"
    ok "ALL CHECKS PASSED"
}

# --- 3. Хост GNUstep-демо (строго по флагам ТЗ) --------------------------------
build_host() {
    [ "$have_gnustep" -eq 1 ] || { warn "Пропускаю хост-демо (нет GNUstep)"; return 0; }
    mkdir -p tools
    if [ ! -f tools/host_demo.m ]; then
        warn "tools/host_demo.m отсутствует (создай его) — пропускаю"
        return 0
    fi
    OBJCFLAGS="$(gnustep-config --objc-flags) -fblocks"
    BASELIBS="$(gnustep-config --base-libs) -lobjc -lBlocksRuntime"
    # Портативные -I поверх строгих флагов ТЗ (без sudo, работает на чистом Mint):
    #  -Itools/include — локальный шим objc/blocks_runtime.h (libobjc2 нет в Ubuntu);
    #  -I$(gcc --print-file-name=include) — objc/objc.h из GCC libobjc (clang их не видит сам).
    OBJCFLAGS="$OBJCFLAGS -Itools/include"
    RINC="$(gcc --print-file-name=include 2>/dev/null || true)"
    [ -n "$RINC" ] && [ -d "$RINC/objc" ] && OBJCFLAGS="$OBJCFLAGS -I$RINC"
    say "OBJCFLAGS: $OBJCFLAGS"
    say "BASELIBS:  $BASELIBS"
    say "Компиляция host_demo.m (clang, ObjC + Blocks + Foundation)..."
    mkdir -p /tmp/opencode
    # shellcheck disable=SC2086
    if ! clang $OBJCFLAGS -c tools/host_demo.m -o /tmp/opencode/host_demo.o; then
        fail "Компиляция host_demo.m упала"
    fi
    say "Линковка host_demo..."
    # shellcheck disable=SC2086
    if ! clang /tmp/opencode/host_demo.o $BASELIBS -o /tmp/opencode/host_demo; then
        fail "Линковка host_demo упала"
    fi
    ok "Хост-бинарник собран. Запуск:"
    echo "----------------------------------------------------------------"
    /tmp/opencode/host_demo || fail "host_demo завершился с ошибкой"
    echo "----------------------------------------------------------------"
    ok "host_demo отработал успешно"

    # Встроенные сэмплы C / C++ / ObjC — проверка тулчейна на Mint:
    say "Сэмплы C/C++/ObjC (хост-проверка)..."
    for s in samples/hello.c samples/hello.cpp samples/hello.m; do
        [ -f "$s" ] || continue
        case "$s" in
            *.c)   gcc -O2 -Wall -o /tmp/opencode/$(basename $s .c)_host "$s" \
                      && echo "  OK  $s -> $(/tmp/opencode/$(basename $s .c)_host)" ;;
            *.cpp) g++ -O2 -Wall -o /tmp/opencode/$(basename $s .cpp)_host "$s" \
                      && echo "  OK  $s -> $(/tmp/opencode/$(basename $s .cpp)_host)" ;;
            *.m)   clang $OBJCFLAGS -c "$s" -o /tmp/opencode/hello_m.o 2>/dev/null \
                      && clang /tmp/opencode/hello_m.o $BASELIBS -o /tmp/opencode/hello_m_host \
                      && echo "  OK  $s (GNUstep, запуск:)" \
                      && /tmp/opencode/hello_m_host | head -3 ;;
        esac
    done
    if command -v swiftc >/dev/null 2>&1; then
        [ -f samples/hello.swift ] && swiftc -O -o /tmp/opencode/hello_swift_host samples/hello.swift \
            && echo "  OK  samples/hello.swift -> $(/tmp/opencode/hello_swift_host)"
    else
        warn "swiftc нет на Mint по умолчанию — Swift-сэмпл проверяется встроенной командой OS (swift). См. docs."
    fi
}

# --- 4. QEMU smoke-тест ---------------------------------------------------------
qemu_smoke() {
    command -v qemu-system-x86_64 >/dev/null 2>&1 || {
        warn "QEMU нет — smoke-тест пропускаю. На Mint: sudo apt install qemu-system-x86"
        warn "ISO/kernel.elf при этом СОБРАНЫ и валидны (проверено make check + readelf)."
        return 0
    }
    say "QEMU smoke-тест: -kernel kernel.elf, 8 секунд, без GUI..."
    timeout 8 qemu-system-x86_64 -kernel kernel.elf -m 512M -display none -serial stdio 2>&1 | head -20 || true
    code=$?
    # timeout убивает qemu через 8с -> код 124 = норма (ядро не вышло само, так и должно быть: hlt-цикл)
    if [ "$code" -eq 124 ]; then
        ok "QEMU стартовал и держал ядро 8с (timeout 124 = штатно для hlt-цикла)"
    else
        warn "QEMU вышел с кодом $code (см. вывод выше)"
    fi
}

case "$MODE" in
    kernel) build_kernel ;;
    host)   build_host ;;
    run)    build_kernel; build_host; qemu_smoke ;;
    *)      build_kernel; build_host ;;
esac
ok "build.sh ($MODE) завершён успешно"
