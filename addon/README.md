# mini-ios — дополнение Linux Mint: macOS-визуал + лёгкая IDE + .ipa-конвейер

Дополнение (addon) для Linux Mint (Cinnamon и XFCE). НЕ замена ОС — всё живёт
в `$HOME`, системные файлы не трогаются (кроdept apt при наличии sudo).

## Что даёт

| Направление | Реализация |
|---|---|
| Визуал macOS (AppKit/CoreAnimation-стиль) | WhiteSur GTK/иконки/курсоры, верхняя панель без списка окон, кнопки окон слева, Plank-док (zoom 130%), macOS-обои, Muffin/xfwm4-эффекты + `perf_tune.sh` (fast/balance/pretty) |
| Лёгкая IDE | **Geany 2.0** (~30–40 МБ ОЗУ), подсветка C/ObjC(.m)/C++(.mm)/Swift(.swift), **F9 = сборка .ipa** через `package_ipa.sh` в VTE-терминале |
| Компиляция .ipa | `ipabuild.py` + `package_ipa.sh`: **C, C++, Objective-C, Swift, гибрид ObjC+Swift, Python (MicroPython)** → Mach-O arm64 → `Payload/App.app` → ad-hoc (`ldid -S`) или p12 → `.ipa` |
| Установка на iPhone | AltStore / Sideloadly: открывается наш `.ipa`, AltStore переподписывает своим Apple ID (free — 7 дней, до 3 приложений) |

## Быстрый старт

```bash
# 1) тулчейн (Theos + SDK + ldid + swiftc) — уже на этой машине:
bash addon/toolchain/setup_ios.sh --check

# 2) новый проект и сборка .ipa:
cd ~/ios_projects && python3 ~/hobby-os/addon/toolchain/ipabuild.py new MyApp --lang objc
./package_ipa.sh ~/ios_projects/MyApp
# или в Geany: открыть файл проекта -> F9

# 3) визуал:
#    Cinnamon:  bash addon/visual/macos_setup.sh
#    XFCE/это:  уже применено (gtk-3.0/settings.ini, xfwm4, панель, plank)
# 4) производительность (0 лагов):
#    bash addon/visual/perf_tune.sh balance   # или fast / pretty
```

## Тулчейн (текущая конфигурация)

```
$THEOS = ~/theos
~/theos/toolchain/linux/iphone/   clang 13 (apple-fork) + swiftc 5.8 + ldid + otool + vtool
~/theos/toolchain/linux/iphone-llvm11.bak/  резерв старого L1ghtmann-тулчейна
~/theos/toolchain/linux/host/     host-часть Swift-тулчейна (обязательно!)
~/theos/sdks/iPhoneOS16.5.sdk
~/ios_projects/package_ipa.sh     универсальный сборщик
~/.local/bin/{ldid,otool,vtool}   обёртки (PATH не загрязняется)
```

Окружение зафиксировано в `~/.bashrc` и `~/.profile` (`THEOS`, `IOS_TC`).

### Важно по Swift
- swiftc 5.8 кросс-компилирует в `arm64-apple-ios14.0` с флагами
  `-sdk <sdk> -resource-dir $IOS_TC/lib/swift -Xcc -isysroot -Xcc <sdk>`.
- Rантайм Swift 5.8 в toolchain отсутствует — исполняемые файлы линкуются
  с `-undefined dynamic_lookup`: символы резолвятся рантаймом устройства
  (iOS ≥ 12.2 везёт Swift-рантайм в `/usr/lib/swift`). Для iPhone
  12+/iOS 14+ это норма для сайдлоад-приложений.
- Гибрид ObjC+Swift: `swiftc -c -emit-objc-header` → `clang`-линковка
  с `.o` + сгенерированным `<App>-Swift.h`.

## Формат .ipa и проверка

```
App_Timestamp.ipa
└─ Payload/App.app/
   ├─ Info.plist        (bundle id com.abzal.<name>, arm64, min iOS 14)
   ├─ App               (Mach-O 64-bit arm64, подпись ad-hoc: Identity=adhoc)
   └─ [python, main.py] (для python-проектов; python тоже подписан)
```

`ipabuild.py verify <ipa>` проверяет: структуру, Info.plist, Mach-O arm64,
подпись каждого бинарника (`ldid -d`). Результат: `PASS/FAIL`.

## Установленные демон-проекты (все PASS)

| Проект | Язык | .ipa |
|---|---|---|
| `~/ios_projects/NativeApp` | ObjC + Swift (гибрид) | `packages/NativeApp_*.ipa` |
| `~/ios_projects/HelloC` | C (glue.m + user.c) | `packages/HelloC_*.ipa` |
| `~/ios_projects/HelloCpp` | C++ (glue.mm + user.cpp) | `packages/HelloCpp_*.ipa` |
| `~/ios_projects/HelloObjC` | Objective-C | `packages/HelloObjC_*.ipa` |
| `~/ios_projects/HelloSwift` | Swift | `packages/HelloSwift_*.ipa` |
| `~/ios_projects/HelloPy` | Python (MicroPython) | `packages/HelloPy_*.ipa` |

## Как ставить .ipa через AltStore

1. iPhone: установить AltStore с Mac (через Finder/Catalyst) или с Windows.
2. AltStore → `+` → выбрать наш `.ipa` → Install (AltStore переподпишет
   твоим Apple ID; free-аккаунт: 7 дней, до 3 приложений).
3. Обновлять раз в 7 дней (AltStore делает это сам по Wi-Fi рядом с
   компьютером, если запущен AltStore Desktop).
4. Для подписи своим сертификатом (p12):
   `CERT_P12=~/certs/dev.p12 CERT_PASS='pass' ./package_ipa.sh ~/ios_projects/MyApp`

## Производительность («ноль лагов»)

- `perf_tune.sh balance` (по умолчанию): композитор с минимальным набором,
  plank-зум включён.
- `perf_tune.sh fast`: композитор xfwm4 выключен, plank-зум выключен —
  для слабой видеокарты/виртуальной машины.
- Geany ~30–40 МБ ОЗУ (в отличие от VS Code ~500 МБ); сборка не поднимает
  GUI — один процесс `make`/`clang`.
- PATH не загрязняется toolchain-бинарниками (осознанно): системный gcc
  не подхватывает `as`/`ld` из iOS-тулчейна.

## География файлов

```
addon/
├── README.md               этот файл
├── visual/
│   ├── macos_setup.sh      Cinnamon: Muffin/панель/WhiteSur/Plank (по SPEC)
│   └── perf_tune.sh        fast|balance|pretty
└── toolchain/
    ├── ipabuild.py         C/C++/ObjC/Swift/hybrid/Python -> .ipa
    ├── setup_ios.sh        check / --swift / --micropython / --perf
    └── templates/          c/ cpp/ objc/ swift/ python/ + Info.plist.tpl
~/ios_projects/package_ipa.sh   универсальный вход (F9 в Geany)
~/.local/geany/               Geany 2.0 (dpkg -x, без sudo)
~/.config/geany/geany.conf    F9 -> package_ipa.sh (VTE)
```

## Ограничения / честные рамки

- **Swift**: только через swiftc 5.8 toolchain (Theos-путь). SwiftPM/
  xcodebuild не используются; многофайловые проекты — по одному `.o` на файл.
- **Python**: MicroPython (быстрый, компактный). Полноценный CPython для
  iOS — отдельная тяжёлая кросс-сборка (по необходимости).
- **Подпись**: ad-hoc — для AltStore/Sideloadly (они переподписывают).
  Для App Store (нереально без аккаунта разработчика) не подходит.
- Слот `Payload/App.app` требует уникальный `CFBundleIdentifier` на устройство
  (у нас `com.abzal.<имя>` — уникальность гарантирована).
