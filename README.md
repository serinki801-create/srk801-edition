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
`--with-swift` (host-Swift через swiftly, опционально, для `.ipa` не нужен) ·
`--uninstall` (полный откат; ядро/драйверы/GRUB не трогаются вообще).

## Сборка `.ipa` одной командой

```sh
cd ~/ios_projects/HelloHybrid && ./package_ipa.sh
# итог: packages/HelloHybrid_*.ipa
```

Цепочка: Theos (`~/theos`, clang + `iPhoneOS.sdk`) → Mach-O arm64 →
`ldid -S` (ad-hoc) → `Payload/App.app` → zip в `.ipa`.
Демо-проект — `UIViewController`/`UIView` на ObjC с Swift-фолбэком.
Установка: Filza/Sileo (джейл) либо AltStore/Sideloadly + Apple ID.

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
