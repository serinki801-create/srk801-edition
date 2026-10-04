# srk801-edition — Mint-станция сборки iOS `.ipa` + хобби-ядро AI-OS

Linux Mint 22.3 превращается в рабочую станцию для сборки iOS-приложений
(джейлбрейк / AltStore / Sideloadly) с визуалом под macOS и лёгкой IDE.
Плюс бонусом — исходники хобби-ядра x86_64 (AI-OS) с композитным WM.

> App Store публикация тут невозможна в принципе (нужен Xcode на macOS).
> Этот проект закрывает честный потолок Linux: рабочий `.ipa` для
> джейла и сайдлоада.

## Быстрый старт (Linux Mint, обычным пользователем + sudo для apt)

```sh
bash mint_ios_setup.sh --all --yes
```

Флаги по частям: `--visual` (WhiteSur + Plank + picom) ·
`--toolchain` (Theos + SDK + подпись + демо `.ipa`) ·
`--ide` (Lite XL + Geany + VS Code таск) ·
`--with-swift=local` (тир A: проверка локального кросс-стека — тулчейн
Theos уже содержит swift-frontend; `swift_doctor` сверяет версию Swift
модулей SDK и тулчейна, при несовпадении отказывает с указанием версии) ·
`--with-swift=remote` (тир B, **гарантированный**: ставит `gh`, кэширует SDK
в `~/.cache/srk801-sdk/`, добавляет задачу `ipa:build(remote)` в tasks.json;
сборка уходит на macos-14 раннер и возвращается готовыми `.ipa`) ·
`--with-swift` (без значения = remote) · `--doctor` (только проверка) ·
`--uninstall` (полный откат: + кэш SDK, + сгенерированные `*-Swift.h`;
ядро/драйверы/GRUB не трогаются вообще).
`--uninstall` (полный откат; ядро/драйверы/GRUB не трогаются вообще).

## Сборка `.ipa` одной командой

```sh
cd ~/ios_projects/HelloHybrid && ./package_ipa.sh
# итог: packages/HelloHybrid_*.ipa
```

Цепочка: Theos (`~/theos`, clang + `iPhoneOS.sdk`) → Mach-O arm64 →
`ldid -S` (ad-hoc) → `Payload/App.app` → zip в `.ipa`.
Демо-проект — смешанный ObjC↔Swift в одном бинаре (`Counter`/`Greeter`, оба направления).
Установка: Filza/Sileo (джейл) либо AltStore/Sideloadly + Apple ID.
AltServer и Sideloadly под Linux не существуют — с этой машины доставить `.ipa`
на недевайл нельзя. Рабочий путь без Mac и без джейла: LiveContainer — лаунчер,
принимающий неподписанные `.ipa` внутрь контейнера (формулировка апстрима:
«Run iOS apps without actually installing them», лимит бесплатного аккаунта
3 app / 10 app ID здесь не действует — один app ID на контейнер; в свежих
релизах есть мультизапуск нескольких приложений одновременно; ограничения
зависят от версии — сверяйся с апстримом: https://github.com/LiveContainer/LiveContainer). Первичная установка LiveContainer
с Linux: SideServer-for-Linux (форк AltLinux) или SideStore Connect (prebuilt docker,
x86/arm64). `ideviceinstaller` из libimobiledevice не подходит: транспорт есть,
а установка падает, потому что у ad-hoc-подписи нет provisioning-профиля.

## Swift под iOS: тир A (локально) vs тир B (раннер)

- **Тир A — локальный кросс-тулчейн (работает здесь).** SDK содержит
  Swift-оверлеи (`UIKit`/`Foundation`/`SwiftUI` `.swiftinterface`,
  `libswift*.tbd`), модули собраны под Apple Swift 5.8 — и тулчейн Theos
  содержит ровно swift-frontend 5.8-RELEASE, принимающий
  `-target arm64-apple-ios` (с `-resource-dir` тулчейна, иначе конфликт
  модулей Dispatch). Проверка: `bash mint_ios_setup.sh --doctor`.
- **Тир B — удалённая компиляция (рекомендуемый).** Локального
  распространяемого Linux→iOS swift-frontend не существует как отдельный
  продукт; SDK с оверлеями не совпадают по версиям между релизами Xcode,
  поэтому гарантированный путь — собирать Swift на macOS-раннере тем же
  `package_ipa.sh` (`.github/workflows/swift-ios-build.yml`, запуск вручную):
  `bash mint_ios_setup.sh --remote-build [проект]` (таймаут 20 мин,
  артефакт `ipa` скачивается в `packages/`).

## Что компилируется / что нет (факты, не обещания)

| Штука | Статус |
|---|---|
| C / ObjC / C++ (.c, .m, .mm) в Mach-O arm64 | ✅ тир A, проверено `verify_ipa.sh` |
| Swift + UIKit/Foundation в Mach-O arm64 | ✅ тир A (оверлеи 5.8 + тулчейн 5.8), `__swift5_typeref` в бинаре |
| Смешанный ObjC↔Swift в одном бинаре | ✅ `Counter`/`Greeter` оба направления, проверено `nm` |
| SwiftUI | ❌ не поддерживается: в SDK нет `os/signpost.h` против его же `os.swiftmodule` |
| Asset catalogs (.xcassets) | ❌ нужен `actool` из Xcode; замена: плоские png + `UIImage(contentsOfFile:)`, системные иконки через `UIImage(systemName:)` |
| Storyboards / xib | ❌ нужен `ibtool`; замена: программный UIKit |
| SwiftUI-превью, Instruments, Metal debugger, симулятор | ❌ только macOS |
| Публикация в App Store | ❌ никогда |

## IDE

- **Geany 2.0** — F9 (`Build`) вызывает `./package_ipa.sh` из каталога
  проекта (настроено в `filedefs/filetypes.c`, `filetypes.objectivec`).
- **VS Code / Codium** — `HelloHybrid/.vscode/tasks.json`, Ctrl+Shift+B.
- **Lite XL 2.x** — portable, тема `xcode-dark`, плагин `ipa-build`
  (Ctrl+B, с `-- mod-version:3`, без баннера version mismatch).

## Визуал macOS

WhiteSur GTK + иконки + курсоры, Plank-док (тема `WhiteSur-dark`,
зум при наведении), picom (blur/тени/скругления, `backend="glx"`),
обои Sonoma. Панель XFCE сверху (меню слева, трей/часы справа),
окна — кнопки слева. Драйверы и ядро системы не затрагиваются.

## Хобби-ядро AI-OS (бонус, отдельная тема)

64-битное Long Mode ядро: Multiboot2, GDT/IDT, PIT 100 Гц, PMM+kheap,
VFS+initrd, ELF-лоадер, Ring 3 + `int 0x80`, framebuffer + WM в стиле
macOS, NX/XD + stack protector. Сборка: `make && make check`
(`build.sh` — те же шаги + GNUstep host-демо). Запуск: `make run`
(QEMU). Подробности: `PROJECT_SUMMARY.txt`, `MINT_SETUP_NOTES.txt`.

## Структура

| Путь | Что |
|---|---|
| `mint_ios_setup.sh` | главный сетап-скрипт Mint |
| `build.sh` | сборка ядра + проверки + host-демо |
| `kernel.c`, `boot.asm`, `*.c/*.h` | исходники AI-OS |
| `samples/` | C/C++/ObjC/Swift примеры |
| `tools/host_demo.m` | GNUstep + Blocks демо |
| `MINI_SETUP.txt` | статус мини-сетапа |
| `IPA_BUILD_GUIDE.md` | гайд по путям сборки `.ipa` |
