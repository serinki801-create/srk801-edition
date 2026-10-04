# AGENTS.md — инструкция для CLI ИИ-агентов (сборка iOS `.ipa`)

> Этот файл — для автоматических агентов (opencode, aider, goose и т.п.).
> Человеку читать `README.md` и `MINI_SETUP.txt`.

## Цель одной командой

```sh
bash agent_build.sh [каталог_проекта]   # default: ~/ios_projects/HelloHybrid
```

- exit 0 + `BUILD PASS: <путь к .ipa>` = успех;
- exit 1 + `FAIL:<причина>` = неуспех (строка пригодна для парсинга).

Скрипт **неинтерактивен**: ни вопросов, ни sudo, ни сети не требует
(сеть нужна только при первой установке тулчейна, см. ниже).

## Карта проекта

| Путь | Назначение |
|---|---|
| `mint_ios_setup.sh` | установка всего (флаги `--visual/--toolchain/--ide/--all/--uninstall`); требует sudo+сеть, запускается человеком один раз |
| `agent_build.sh` | **точка входа для агентов**: проверка окружения → сборка → верификация `.ipa` |
| `~/ios_projects/HelloHybrid/` | эталонный демо-проект (ObjC + Swift-фолбэк); `Makefile` (Theos), `package_ipa.sh`, `packages/*.ipa` |
| `~/ios_projects/NativeApp/` | второй демо-проект, та же схема сборки |
| `~/theos` | Theos; тулчейн `toolchain/linux/iphone/bin/{clang,swiftc,ldid...}`, SDK в `sdks/*.sdk` |

## Конвейер сборки (что происходит внутри)

1. `make package FINALPACKAGE=1` через Theos → Mach-O **arm64**,
   staged `.app` в `.theos/_/Applications/`.
2. `Payload/<App>.app/` + `Info.plist` → подпись `ldid -S` (ad-hoc).
3. `zip -qry packages/<App>_<stamp>.ipa Payload` → готовый `.ipa`
   для джейлбрейка / AltStore / Sideloadly / LiveContainer.

## Правила для агента

1. **Всегда экспортируй окружение** перед любым `make`:
   `export THEOS="$HOME/theos"` (+ toolchain `bin` в `PATH`).
   Без этого — классическая ошибка `Makefile:25: /application.mk: Нет такого файла`.
2. Не запускай GUI-редакторы для проверки (Geany/Lite XL открывают окна
   в живой сессии пользователя). Проверяй файлами и кодами возврата.
3. Не ставь пакеты через apt и не трогай драйверы/ядро/GRUB/picом —
   это вне скоупа; при отсутствии зависимости верни `FAIL:<что именно>`.
4. Swift собирается только при наличии `swiftc` в тулчейне Theos;
   иначе `Makefile` автоматически использует `USE_SWIFT=0` (ObjC-фолбэк) —
   это **норма**, а не ошибка.
5. App Store публикация **невозможна** на Linux (нужен Xcode на macOS) —
   не обещай её пользователю; потолок — ad-hoc `.ipa`.

## Верификация результата (чеклист `agent_build.sh`)

- [ ] `unzip -t` — архив цел;
- [ ] есть `Payload/*.app/Info.plist` и исполняемый файл;
- [ ] `file` говорит `Mach-O 64-bit arm64 executable`;
- [ ] `ldid -e` читает entitlements (ad-hoc подпись на месте).

## Типовые FAIL и лечение

| FAIL | Причина → действие |
|---|---|
| `no THEOS at ...` | тулчейн не установлен → скажи человеку `bash mint_ios_setup.sh --toolchain` |
| `no theos clang toolchain` | битый/неполный тулчейн → переустановка через тот же флаг |
| `no SDK in .../sdks` | нет `iPhoneOS*.sdk` → `$THEOS/bin/install-sdk latest` (нужна сеть) |
| `no ldid in PATH` | нет подписи → `~/ios-toolchain/bin` в `PATH` или собрать ldid (см. `mint_ios_setup.sh`) |
| `package_ipa.sh failed` | смотри `/tmp/agent_build_pkg.log`, чини `Makefile`/исходники проекта |

## Минимальный пример для нового проекта

```sh
cp -r ~/ios_projects/HelloHybrid ~/ios_projects/MyApp
cd ~/ios_projects/MyApp
sed -i 's/HelloHybrid/MyApp/g' Makefile control Info.plist main.m package_ipa.sh
bash /home/abzal/hobby-os/agent_build.sh ~/ios_projects/MyApp
# → BUILD PASS: .../MyApp/packages/MyApp_*.ipa
```
