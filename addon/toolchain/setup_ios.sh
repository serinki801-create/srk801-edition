#!/usr/bin/env bash
# ============================================================================
# setup_ios.sh — установка/проверка iOS-тулчейна в Linux Mint (addon)
# ============================================================================
# Флаги:
#   --swift       скачать Swift-toolchain (kabiroberai v2.3.0, ~600 МБ) для
#                 сборки Swift-приложений в .ipa
#   --micropython предсобрать MicroPython для arm64-apple-ios (кэш)
#   --perf        настройки производительности (perf_tune.sh)
#   --check       только проверка (по умолчанию)
# Правило безопасности: только $HOME/$THEOS + apt (plank/picom не трогаем).
# ============================================================================
set -uo pipefail
THEOS="${THEOS:-$HOME/theos}"
TC="$THEOS/toolchain/linux/iphone"
ASSUME=""
DO_SWIFT=0; DO_MPY=0; DO_PERF=0; DO_CHECK=0
for a in "$@"; do
  case "$a" in
    --swift) DO_SWIFT=1 ;;
    --micropython) DO_MPY=1 ;;
    --perf) DO_PERF=1 ;;
    --check) DO_CHECK=1 ;;
    -y|--yes) ASSUME="-y" ;;
    -h|--help) sed -n 2,14p "$0"; exit 0 ;;
    *) echo "неизвестный флаг: $a" >&2; exit 1 ;;
  esac
done
[[ "$DO_SWIFT$DO_MPY$DO_PERF" == "000" ]] && DO_CHECK=1
log(){ echo "==> $*"; }

have(){ command -v "$1" >/dev/null 2>&1; }

# --- базовый тулчейн (clang + SDK + ldid) -----------------------------------
setup_base(){
  log "Theos..."
  if [[ ! -d "$THEOS/.git" ]]; then
    git clone --recursive https://github.com/theos/theos.git "$THEOS" || {
      echo "ERROR: git clone theos не удался"; exit 1; }
  fi
  log "toolchain (L1ghtmann iOSToolchain)..."
  if [[ ! -x "$TC/bin/clang" ]]; then
    ARCH="$(uname -m)"
    curl -sL "https://github.com/L1ghtmann/llvm-project/releases/latest/download/iOSToolchain-$ARCH.tar.xz" \
      | tar -xJ -C "$THEOS/toolchain/"
  fi
  [[ -x "$TC/bin/clang" ]] || { echo "ERROR: clang не установился"; exit 1; }
  log "iPhoneOS.sdk..."
  if ! find "$THEOS/sdks" -maxdepth 1 -name 'iPhoneOS*.sdk' -type d 2>/dev/null | grep -q .; then
    ( cd "$THEOS" && bin/install-sdk latest ) || {
      echo "WARN: install-sdk не прошёл — поставь SDK вручную в $THEOS/sdks/"; }
  fi
  log "ldid..."
  if [[ ! -x "$TC/bin/ldid" ]] && ! have ldid; then
    if have sudo && sudo -n true 2>/dev/null; then
      sudo apt-get install -y "$ASSUME" ldid 2>/dev/null || true
    fi
    if ! have ldid; then
      mkdir -p ~/src ~/ios-toolchain
      [[ -d ~/src/ldid ]] || git clone --depth=1 https://github.com/ProcursusTeam/ldid.git ~/src/ldid
      cmake -S ~/src/ldid -B ~/src/ldid/build -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$HOME/ios-toolchain"
      cmake --build ~/src/ldid/build -j"$(nproc)"
      cmake --install ~/src/ldid/build
      export PATH="$HOME/ios-toolchain/bin:$PATH"
    fi
  fi
}

# --- swift -------------------------------------------------------------------
setup_swift(){
  if [[ -x "$TC/bin/swiftc" ]]; then
    log "swiftc уже есть: $($TC/bin/swiftc --version 2>/dev/null | head -1)"
    return 0
  fi
  log "Swift-toolchain (kabiroberai v2.3.0, swift 5.8)..."
  # Сохраняем L1ghtmann-тулчейн от случайного замеса bin/
  if [[ -x "$TC/bin/clang" && ! -d "$TC.bak" ]]; then
    log "резерв текущего bin/ -> $TC.bak-bin"
    cp -a "$TC/bin" "$TC.bak-bin" 2>/dev/null || true
  fi
  ARCH="$(uname -m)"
  case "$ARCH" in
    x86_64)  URL="https://github.com/kabiroberai/swift-toolchain-linux/releases/download/v2.3.0/swift-5.8-ubuntu20.04.tar.xz" ;;
    aarch64) URL="https://github.com/kabiroberai/swift-toolchain-linux/releases/download/v2.3.0/swift-5.8-ubuntu20.04-aarch64.tar.xz" ;;
    *) echo "ERROR: swift-toolchain для $ARCH нет"; exit 1 ;;
  esac
  TMP="$(mktemp -d)"
  curl -sL "$URL" -o "$TMP/swift-tc.tar.xz" || { echo "ERROR: download"; exit 1; }
  echo "размер: $(du -h "$TMP/swift-tc.tar.xz" | cut -f1). Распаковка..."
  tar -xJf "$TMP/swift-tc.tar.xz" -C "$THEOS/toolchain/"
  rm -rf "$TMP"
  if [[ -x "$TC/bin/swiftc" ]]; then
    log "OK: $($TC/bin/swiftc --version 2>/dev/null | head -1)"
  else
    echo "ERROR: swiftc не на месте. Ручная проверка: ls $THEOS/toolchain"
    exit 1
  fi
}

# --- micropython -------------------------------------------------------------
setup_mpy(){
  log "MicroPython для arm64-apple-ios (кэш ~/.cache/ipabuild)..."
  python3 "$(dirname "$0")/ipabuild.py" micropython
}

# --- perf ---------------------------------------------------------------------
setup_perf(){
  log "Настройки производительности..."
  bash "$(dirname "$0")/../visual/perf_tune.sh"
}

# --- check --------------------------------------------------------------------
do_check(){
  log "Проверка тулчейна..."
  python3 "$(dirname "$0")/ipabuild.py" check
}

[[ "$DO_CHECK" == "1" ]] && { do_check; exit 0; }
setup_base
[[ "$DO_SWIFT" == "1" ]] && setup_swift
[[ "$DO_MPY" == "1" ]] && setup_mpy
[[ "$DO_PERF" == "1" ]] && setup_perf
echo "=============================="
echo "ГОТОВО. Проверка: bash $0 --check"
