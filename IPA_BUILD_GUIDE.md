# Сборка .ipa на Arch Linux: два способа

- **Способ A — Xcode в Docker-OSX** (ниже, разделы 0–8): настоящий Xcode,
  настоящая подпись, App Store. Нужны KVM + 8 ГБ RAM + Apple ID.
- **Способ B — нативно через Theos** (раздел 9): без VM и Docker, прямо на
  Arch. ObjC (+Swift при Swift-тулчейне) → ad-hoc/real подпись через
  `rcodesign` → `.ipa`. Для джейлбрейка, AltStore/Sideloadly и тестов —
  идеально; в App Store такой .ipa не примут (туда только Xcode-archive).

## IDE для написания и сборки .ipa

| IDE | Ссылка | Для чего |
|---|---|---|
| **Xcode** | https://developer.apple.com/xcode/ | Способ A. Официальная IDE Apple (C, ObjC, Swift, C++). Архив → .ipa, App Store. |
| **Lite XL** | https://github.com/lite-xl/lite-xl | Способ B. Лёгкая IDE (C/Lua, ~20 МБ), ставится `mint_ios_setup.sh --ide`. Ctrl+B → `make package`. |
| **VS Code** | https://code.visualstudio.com/ | Альтернатива для Theos-проектов (расширения C/C++, Swift, Objective-C). |

> Примечание: настоящий App Store `.ipa` собирается **только через Xcode** (macOS или Docker-OSX). Theos/Lite XL/VS Code дают `ad-hoc`/`development` `.ipa` для AltStore/Sideloadly и джейлбрейка.

---

# Способ A. Xcode в Docker-OSX (Arch Linux → macOS Monterey)

Цель: из консоли Arch получать готовый `.ipa` headless-командой,
без сидения в GUI:
`bash run_xcode_container.sh build ~/ios_projects/MyApp MyApp Release TEAMID12345`

> Честно: GUI понадобится дважды — установка macOS и установка Xcode.
> Дальше всё (archive → export → .ipa) идёт по SSH из консоли Arch.

## 0. Что нужно заранее

- Arch Linux после `bash setup_host.sh` (KVM, docker, libvirt).
- Железо: Intel с VT-x (4–5 поколение ок), **8+ ГБ RAM**, **50+ ГБ свободно**
  на SSD (образ macOS ~20 ГБ + Xcode ~15 ГБ + проект/архивы).
- Apple ID. Для установки Xcode из App Store хватает обычного Apple ID.
- Для подписи `.ipa`:
  - `development` / локальные тесты — хватит бесплатного Apple ID;
  - `app-store` / `ad-hoc` — нужен платный **Apple Developer Program**
    ($99/год) и Team ID (смотри на developer.apple.com → Membership).
- ВАЖНО про версии: **Monterey тянет Xcode максимум 14.x**
  (SDK — максимум iOS 16). Нужен SDK новее → пересоздай контейнер
  с `SHORTNAME=ventura` (Xcode 15) или `sonoma` (Xcode 16), но это
  заметно тяжелее для Intel 4–5 поколения. Для старта — Monterey.

## 1. Первый запуск и установка macOS (GUI нужен один раз)

```bash
bash run_xcode_container.sh start --gui
```

Откроется окно установщика macOS. Далее как в официальном гайде:

1. Выбери язык → **Disk Utility** → сотри САМЫЙ БОЛЬШОЙ диск
   (около 200 ГБ по умолчанию; маленькие диски не трогай).
2. Закрой Disk Utility → **Reinstall macOS** → жди
   (будет несколько перезагрузок, это нормально).
3. Создай пользователя macOS (запомни имя и пароль, например `user`).
4. Войди в систему.

## 2. Настройка гостя (внутри macOS, один раз)

1. Войди в **Apple ID**: Системные настройки → Apple ID
   (понадобится код 2FA с твоего iPhone/другого устройства).
2. Включи **Remote Login** (это SSH для headless-сборок):
   Системные настройки → Основные → Общий доступ → Remote Login → вкл.
3. Поставь **Xcode**: App Store → найди Xcode → Install
   (долго, ~15 ГБ; на время установки GUI не закрывай).
4. Открой Xcode один раз, прими лицензию, дай ему доставить компоненты.
5. (Опционально, ускоряет всё): `xcode-select --install` для Command Line Tools.

## 3. SSH без пароля (на хосте Arch, один раз)

```bash
# MAC_SSH_USER — пользователь, созданный в п.1 (по умолчанию user)
ssh-copy-id -p 50922 user@127.0.0.1
bash run_xcode_container.sh ssh 'sw_vers && xcodebuild -version'
```

Должны показаться версия macOS и версия Xcode. Теперь GUI можно закрыть:
`bash run_xcode_container.sh stop`, дальше — только headless (`start` без `--gui`).

## 4. Проект и подпись

1. Положи проект в `~/ios_projects/MyApp/` (там должен быть
   `MyApp.xcodeproj` или `MyApp.xcworkspace`).
2. Открой проект в Xcode (один раз, можно по SSH с X11 или в GUI):
   Target → **Signing & Capabilities** → выбери Team → включи
   **Automatically manage signing**, поправь Bundle Identifier
   (формат `com.твоёимя.app`, уникален).
3. Узнай точное имя scheme: Product → Scheme → Manage Schemes
   (обычно совпадает с именем проекта).
4. Сгенерируй шаблон экспорта (Team ID подставится автоматически):
```bash
bash run_xcode_container.sh export-template TEAMID12345
# файл: ~/ios_projects/ExportOptions.plist
# method по умолчанию app-store; для тестовых девайсов поменяй на ad-hoc
```

## 5. Headless-сборка .ipa (основная команда)

```bash
bash run_xcode_container.sh build ~/ios_projects/MyApp MyApp Release TEAMID12345
```

Что происходит внутри:

1. `sync`: `rsync` проекта в гостя (`~/ios_projects/`).
2. По SSH: `xcodebuild -scheme MyApp -configuration Release \
   -archivePath ~/ios_projects/build/MyApp-<дата>.xcarchive archive`
   (первый раз 10–60 минут — Swift/зависимости компилируются с нуля).
3. По SSH: `xcodebuild -exportArchive -archivePath ... \
   -exportPath ~/ios_projects/build/ipa-<дата> \
   -exportOptionsPlist ~/ios_projects/build/ExportOptions.plist`
4. `.ipa` забирается назад в `~/ios_projects/build/ipa-<дата>/`.

Методы экспорта (`method` в ExportOptions.plist):

| method | Куда годится |
|---|---|
| `app-store` | Загрузка в App Store Connect (нужен paid Developer) |
| `ad-hoc` | Установка на зарегистрированные тестовые девайсы (UDID) |
| `enterprise` | Внутреннее корпоративное распространение |
| `development` | Локальная отладка на своём девайсе |

## 6. Про подпись подробнее (если automatic signing упёрся)

- **Automatic (рекомендуется):** Xcode сам создаёт CertificateSigningRequest,
  забирает development/distribution-сертификаты и provisioning profiles
  по твоему Apple ID. В 95% случаев этого достаточно.
- **Manual:** сертификаты лежат в Keychain Access гостя, profiles — в
  `~/Library/MobileDevice/Provisioning Profiles/`. Тогда в ExportOptions.plist
  добавь `signingStyle: manual`, `signingCertificate`, `provisioningProfiles`
  (словарь bundle-id → имя профиля). Нужно, только если automatic не завёлся.

## 7. Типовые проблемы

- `ERROR: нет /dev/kvm` → `bash setup_host.sh`, проверь VT-x в BIOS.
- Контейнер жрёт всё → ресурсы уже ограничены (80% RAM, все ядра);
  уменьшить: `RAM_GB`/`CORES` правятся переменными? Нет — они считаются
  автоматически; для ручного лимита останови контейнер и перезапусти
  с `-e RAM=8 -e SMP=4`, поменяв значения прямо в скрипте.
- `xcodebuild: command not found` по SSH → Xcode не доставлен до конца:
  открой его в GUI один раз и прими лицензию.
- Ошибка подписи `No signing certificate` → зайди в Xcode → Settings →
  Accounts → твой Apple ID → Manage Certificates → «+» нужный тип.
- App Store требует SDK новее, чем даёт Xcode 14 → см. блок про версии
  в разделе 0 (Ventura/Sonoma вместо Monterey).
- iCloud/iMessage внутри гостя ругаются → серийник: в скрипте уже стоят
  `GENERATE_UNIQUE=true` + `MASTER_PLIST_URL`; проверить: в госте
  `ioreg -l | grep IOPlatformSerialNumber`.
- Место кончилось → архивы `.xcarchive` жиреют; чисти
  `~/ios_projects/build/` и DerivedData гостя
  (`~/Library/Developer/Xcode/DerivedData`).
- По EULA Apple виртуализация macOS разрешена только на оборудовании
  Apple — учитывай это для коммерческих сборок.

## 8. Альтернатива вообще без виртуалки

Нет железа/терпения — облачная сборка: **EAS Build (Expo)** или
**Codemagic**. Заливаешь код, `.ipa` собирают на их Маках. Для Cordova/
Capacitor/React Native это часто быстрее, чем свой Monterey в Docker.

---

# Способ B. Нативно через Theos (без VM, без Docker)

## 9. Установка тулчейна одной командой

```bash
bash setup_ios_toolchain.sh            # полный тулчейн + Swift toolchain
bash setup_ios_toolchain.sh --no-swift # облегчённый вариант (только ObjC)
```

Скрипт делает всё сам (факты сверены с официальными доками Theos,
cctools-port и apple-codesign, ссылки — в шапке скрипта):

1. Зависимости pacman: `base-devel clang llvm lld make perl git fakeroot
   zip unzip xz openssl openssh rsync cmake ninja pkg-config ncurses
   libbsd libxml2 curl autoconf automake libtool rust`.
2. Host-Swift через AUR (`paru` + `swift-bin`; заметь: пакета
   `swift-language` **нет в extra, только в AUR** — скрипт это учитывает).
   Best-effort: не встал — идём дальше, iOS-Swift от него не зависит.
3. Шим `update-alternatives`: официальный `install-theos` зовёт его
   безусловно (Debian-наследие), а в Arch такой команды нет — без шима
   installer упал бы с exit 10.
4. `export THEOS=$HOME/theos` + PATH в `~/.bashrc`.
5. Официальный `install-theos` (НЕ от root — он так требует):
   тулчейн `$THEOS/toolchain/linux/iphone/bin/clang` + свежие
   `iPhoneOS*.sdk` через `$THEOS/bin/install-sdk latest`.
6. Проверка Apple-линковщика; **только если его нет** — сборка
   `cctools-port` (`--target=arm-apple-darwin`, именно `arm`, иначе
   `config.sub` падает; `-arch arm64` отдаётся уже clang).
7. `cargo install apple-codesign` → `rcodesign` (ad-hoc из коробки,
   настоящая подпись через `--p12-file`).
8. Генерация `~/ios_projects/HelloHybrid/` (см. раздел 10).
9. Smoke test с ретраями (Swift → чистый ObjC → ручная упаковка).

## 10. Структура трибридного проекта

`~/ios_projects/HelloHybrid/`:

- `Makefile` — Theos, таргет `iphone:clang:latest:14.0`, `ARCHS = arm64`,
  `HelloHybrid_FILES = main.m`, `HelloHybrid_SWIFT_FILES = SwiftLogic.swift`
  (только если в тулчейне есть `swiftc`, иначе авто-fallback),
  `HelloHybrid_FRAMEWORKS = UIKit Foundation CoreGraphics`,
  `THEOS_PACKAGE_SCHEME = rootless`.
- `main.m` — `UIApplicationMain` + `AppDelegate` + `UIWindow`. Через
  `#import <HelloHybrid-Swift.h>` зовёт Swift при `HAVE_SWIFT=1`,
  иначе ObjC-fallback (код без ARC/MRR-специфики — компилируется везде).
- `SwiftLogic.swift` — `@objcMembers public class SwiftLogic` (`public`
  обязателен для видимости из ObjC), `greeting()/accentColor()/answer()`.
  Swift runtime встроен в iOS ≥ 12.2, deployment 14.0 — ок.
- `HelloHybrid-Bridging-Header.h` — имя строго такое, Theos подхватывает сам.
- `Info.plist`, `control` (для theos-пакета), `package_ipa.sh` (fallback).

## 11. Сборка финального пакета

```bash
cd ~/ios_projects/HelloHybrid
make package FINALPACKAGE=1
```

Готовый файл: `packages/*.ipa` (свежий — первый в `ls -t`).
Если theos-пакета нет (например, упёрся в подпись) — скрипт сам запускает
`./package_ipa.sh`: ищет staged `.app`, собирает `Payload/`, подписывает
(`CERT_P12` + `CERT_PASS` → настоящая подпись, иначе ad-hoc `rcodesign`),
пакует `releases/HelloHybrid_1.0_<дата>.ipa` и печатает путь.

Настоящая подпись своим сертификатом (Apple Developer, CSR через openssl,
сертификат из developer portal → экспорт в .p12):

```bash
CERT_P12=~/certs/dev.p12 CERT_PASS='...' ./package_ipa.sh
```

## 12. Куда такой .ipa ставится

- **Jailbreak-девайс:** `scp` + `dpkg -i` / Filza — ставится любой,
  включая ad-hoc.
- **Обычный девайс:** AltStore / Sideloadly + бесплатный Apple ID
  (переподпишут своим сертификатом на 7 дней).
- **App Store / TestFlight:** НЕТ — туда только Xcode-archive
  (см. Способ A). Нативный .ipa не содержит нужных entitlements/Profile
  для стора.
