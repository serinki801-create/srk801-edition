#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ipabuild.py — единый сборщик .ipa для iOS из Linux Mint.
Языки: C, C++, Objective-C, Swift, Python (MicroPython).

Использование:
  ipabuild.py check                      # проверка тулчейна
  ipabuild.py new <Name> --lang c        # создать проект из шаблона
  ipabuild.py build <dir> [--lang L] [--sign adhoc|p12]
  ipabuild.py verify <file.ipa>          # проверка структуры/подписи
  ipabuild.py micropython                # кросс-сборка MicroPython (кэш)

Тулчейн (порядок поиска):
  $IOS_TC            -> корень toolchain (bin/clang, bin/swiftc, bin/ldid)
  $THEOS/toolchain   -> по умолчанию
  $IOS_SDK           -> путь к iPhoneOS.sdk
  $THEOS/sdks/*.sdk  -> по умолчанию
Подпись: ldid -S (ad-hoc, ставится через AltStore/Sideloadly)
или ldid -k cert.p12 -U pass (CERT_P12 / CERT_PASS в окружении).
"""

import argparse
import glob
import os
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

MIN_IOS = "14.0"
HOME = os.path.expanduser("~")
TC_CANDIDATES = [
    os.environ.get("IOS_TC", ""),
    os.path.join(os.environ.get("THEOS", os.path.join(HOME, "theos")), "toolchain", "linux", "iphone"),
    "/usr/local/ios-tc",
    "/home/abzal/theos/toolchain/linux/iphone",
]
SDK_CANDIDATES = [
    os.environ.get("IOS_SDK", ""),
]
IPABUILD_CACHE = os.path.join(HOME, ".cache", "ipabuild")


def log(msg):
    print("[ipabuild] %s" % msg, flush=True)


def die(msg, code=1):
    print("[ipabuild] ERROR: %s" % msg, file=sys.stderr)
    sys.exit(code)


def run(cmd, **kw):
    """Запуск команды с выводом в консоль. Возвращает exit code."""
    log("+ " + " ".join(cmd))
    return subprocess.call(cmd, **kw)


def run_out(cmd, **kw):
    quiet = kw.pop("quiet_ok", False)
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if p.returncode != 0 and not quiet:
        sys.stderr.write(p.stdout + "\n" + p.stderr + "\n")
    return p


def host_env():
    """Окружение для HOST-сборок (mpy-cross и т.п.): из PATH убираем
    каталоги iOS-тулчейна, иначе его `as`/`ld` перехватывают вызовы
    системного gcc (gcc -> as --64 -> clang-11: unsupported option)."""
    env = os.environ.copy()
    keep = ["/usr/local/sbin", "/usr/local/bin", "/usr/sbin", "/usr/bin",
            "/sbin", "/bin"]
    env["PATH"] = os.pathsep.join(keep)
    return env


# ---------------------------------------------------------------- toolchain
def find_toolchain():
    for c in TC_CANDIDATES:
        if c and os.path.exists(os.path.join(c, "bin", "clang")):
            return c
    return None


def find_sdk():
    for c in SDK_CANDIDATES:
        if c and os.path.isdir(c):
            return c
    theos = os.environ.get("THEOS", os.path.join(HOME, "theos"))
    for c in sorted(glob.glob(os.path.join(theos, "sdks", "iPhoneOS*.sdk"))):
        return c
    for c in sorted(glob.glob("/usr/local/iPhoneOS*.sdk")):
        return c
    return None


class TC:
    def __init__(self):
        self.root = find_toolchain()
        self.sdk = find_sdk()
        if self.root:
            b = os.path.join(self.root, "bin")
            self.clang = os.path.join(b, "clang")
            self.clangxx = os.path.join(b, "clang++")
            self.swiftc = os.path.join(b, "swiftc")
            self.ldid = os.path.join(b, "ldid")
        else:
            self.clang = self.clangxx = self.swiftc = self.ldid = None
        if not os.path.exists(self.ldid):
            p = shutil.which("ldid")
            self.ldid = p
        self.swift = os.path.exists(self.swiftc) if self.swiftc else False

    def ok(self, need_swift=False):
        return bool(self.clang and self.sdk and self.ldid and
                    (self.swift or not need_swift))


def base_cflags(tc):
    return [
        "-target", "arm64-apple-ios" + MIN_IOS,
        "-miphoneos-version-min=" + MIN_IOS,
        "--sysroot", tc.sdk,
        "-isysroot", tc.sdk,
        "-O2",
        "-fobjc-arc",
        "-Wno-deprecated-declarations",
    ]


def base_ldflags(tc):
    return [
        "-L" + tc.sdk + "/usr/lib",
        "-L" + tc.sdk + "/usr/lib/swift",
        "-F" + tc.sdk + "/System/Library/Frameworks",
        "-framework", "UIKit",
        "-framework", "Foundation",
        "-framework", "CoreGraphics",
        "-Xlinker", "-rpath", "-Xlinker", "/usr/lib/swift",
    ]


# ---------------------------------------------------------------- micropython
def patch_mpthreadport(src):
    """Локальный патч: в APPLE-секции mpthreadport.c используется
    pthread_mach_thread_np(), но <pthread.h> подключается только при
    MICROPY_PY_THREAD. Без него кросс-сборка под apple-ios падает."""
    p = os.path.join(src, "ports", "unix", "mpthreadport.c")
    with open(p) as f:
        s = f.read()
    marker = "#if defined(__APPLE__)\n#include <pthread.h>\n#include <mach/mach_error.h>"
    if marker in s:
        return
    old = "#if defined(__APPLE__)\n#include <mach/mach_error.h>"
    if old in s:
        s = s.replace(old, marker, 1)
        with open(p, "w") as f:
            f.write(s)
        log("применён локальный патч mpthreadport.c (+pthread.h)")
    else:
        log("WARN: патч mpthreadport.c не применился (структура файла изменилась)")


def build_micropython(tc):
    """Кросс-компилирует MicroPython (unix-порт) для arm64-apple-ios. Кэш в ~/.cache/ipabuild."""
    cache = IPABUILD_CACHE
    out_dir = os.path.join(cache, "micropython-arm64-ios")
    binpath = os.path.join(out_dir, "python")
    if os.path.exists(binpath):
        log("MicroPython уже собран: %s" % binpath)
        return binpath

    src = os.path.join(cache, "micropython")
    if not os.path.isdir(src):
        log("Клонирую MicroPython...")
        os.makedirs(cache, exist_ok=True)
        if run(["git", "clone", "--depth", "1",
                "https://github.com/micropython/micropython.git", src]) != 0:
            die("git clone micropython не удался")
    patch_mpthreadport(src)

    cc = tc.clang
    # ВАЖНО: только CFLAGS_EXTRA/LDFLAGS_EXTRA. Передача CFLAGS/LDFLAGS через
    # командную строку make override'ит += из Makefile портов и сольёт все
    # include-пути (-I$(TOP) и т.д.) -> 'py/obj.h' not found.
    cflags = ("-target arm64-apple-ios" + MIN_IOS +
              " -miphoneos-version-min=" + MIN_IOS +
              " --sysroot " + tc.sdk + " -isysroot " + tc.sdk + " -O2")
    lflags = ("--sysroot " + tc.sdk + " -target arm64-apple-ios" + MIN_IOS +
              " -L" + tc.sdk + "/usr/lib")
    log("Собираю mpy-cross (host)...")
    henv = host_env()
    if run(["make", "-C", os.path.join(src, "mpy-cross"), "clean"],
           stdout=subprocess.DEVNULL, env=henv) != 0:
        pass
    if run(["make", "-C", os.path.join(src, "mpy-cross"), "-j4"],
           env=henv) != 0:
        die("сборка mpy-cross не удалась")
    log("Собираю MicroPython (unix-порт, минимальный) для arm64-apple-ios...")
    # VARIANT=minimal: не тянет подмодули libffi/mbedtls/berkeley-db
    up = os.path.join(src, "ports", "unix")
    # make clean чистит только default-вариант (build-standard);
    # старыми .o из другого запуска собьём линку — чистим каталог жёстко.
    if run(["make", "-C", up, "clean"], stdout=subprocess.DEVNULL) != 0:
        pass
    for d in ("build-minimal", "build-standard"):
        shutil.rmtree(os.path.join(up, d), ignore_errors=True)
    # LDFLAGS_ARCH: Makefile выбирает синтаксис gcc по ОС-хосту (Linux),
    # а линкуем Darwin ld64 — нужен -dead_strip, а не -Wl,-Map=...
    # MICROPY_PY_THREAD=1: вариант minimal имеет THREAD=0, но тогда
    # APPLE-секция main.c (realtime-опция) не компилируется — включаем
    # потоки (pthread есть в iOS libSystem). Переменная на командной
    # строке make перекрывает и их -DMICROPY_PY_THREAD=0, и выбор файлов.
    if run(["make", "-C", up,
            "VARIANT=minimal", "CC=" + cc,
            "CFLAGS_EXTRA=" + cflags, "LDFLAGS_EXTRA=" + lflags,
            "LDFLAGS_ARCH=-Wl,-dead_strip",
            "MICROPY_PY_THREAD=1",
            "-j4"]) != 0:
        die("сборка MicroPython не удалась")

    built = os.path.join(src, "ports", "unix", "build-minimal", "micropython")
    if not os.path.exists(built):
        die("бинарник mpy не найден: " + built)
    os.makedirs(out_dir, exist_ok=True)
    shutil.copy2(built, binpath)
    os.chmod(binpath, 0o755)
    log("MicroPython готов: %s" % binpath)
    return binpath


# ---------------------------------------------------------------- verify
MH_MAGIC_64 = b"\xCF\xFA\xED\xFE"
CPU_ARCH_ABI64 = 0x01000000
CPU_TYPE_ARM = 0x0000000C
# arm64 = CPU_TYPE_ARM | CPU_ARCH_ABI64 (0x0100000C) — историческое
# кодирование, которое используют iOS-линковщики (cctools/ld64-форки).


def macho_info(path):
    with open(path, "rb") as f:
        head = f.read(8)
    if not head.startswith(MH_MAGIC_64):
        return None
    import struct
    cputype = struct.unpack("<I", head[4:8])[0]
    if (cputype & CPU_ARCH_ABI64) and (cputype & 0xFF) == CPU_TYPE_ARM:
        return "arm64"
    return hex(cputype)


def verify_ipa(path):
    ok = True
    log("Проверяю .ipa: %s" % path)
    if not os.path.exists(path):
        die("файл не найден: " + path)
    tc = TC()
    ldid = tc.ldid or "ldid"
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        apps = sorted({m.group(1) for n in names
                       if (m := re.match(r"^Payload/([^/]+\.app)/", n))})
        if not apps:
            print("  FAIL: нет Payload/<App>.app/")
            return False
        app = "Payload/" + apps[0] + "/"
        plist_path = app + "Info.plist"
        if plist_path not in names:
            print("  FAIL: нет Info.plist")
            ok = False
        else:
            with z.open(plist_path) as f:
                pl = plistlib.load(f)
            bid = pl.get("CFBundleIdentifier", "?")
            exe = pl.get("CFBundleExecutable", "?")
            print("  OK: Info.plist bundle_id=%s exec=%s" % (bid, exe))
            exe_path = app + exe
            if exe_path not in names:
                print("  FAIL: исполняемый файл %s не в бандле" % exe)
                ok = False
            else:
                tmp = os.path.join(tempfile.mkdtemp(prefix="ipacheck"), exe)
                with z.open(exe_path) as src, open(tmp, "wb") as dst:
                    shutil.copyfileobj(src, dst)
                arch = macho_info(tmp)
                if arch != "arm64":
                    print("  FAIL: бинарник не arm64 Mach-O (получено: %s)" % arch)
                    ok = False
                else:
                    print("  OK: CFBundleExecutable — Mach-O 64-bit arm64")
                # подпись всех Mach-O в бандле
                tmpdir = tempfile.mkdtemp(prefix="ipacheck_")
                for n in names:
                    if n.endswith("/"):
                        continue
                    t = os.path.join(tmpdir, os.path.basename(n))
                    with z.open(n) as src, open(t, "wb") as dst:
                        shutil.copyfileobj(src, dst)
                    if macho_info(t):
                        p = run_out([ldid, "-d", t], quiet_ok=True)
                        if p.returncode == 0:
                            print("  OK: подпись: %s" % os.path.basename(n))
                        else:
                            print("  WARN: %s не подписан (ldid -d не прошёл)" %
                                  os.path.basename(n))
                shutil.rmtree(tmpdir, ignore_errors=True)
    print("ИТОГ: %s" % ("PASS" if ok else "FAIL"))
    return ok


# ---------------------------------------------------------------- build
def detect_lang(d):
    if not os.path.isdir(d):
        return None
    # явный маркер (приоритетный источник)
    marker = os.path.join(d, "ipabuild.txt")
    if os.path.exists(marker):
        try:
            for line in open(marker):
                if line.startswith("lang="):
                    return line.split("=", 1)[1].strip()
        except OSError:
            pass
    exts = set()
    for root, _, files in os.walk(d):
        if any(x in root for x in (".theos", "packages", ".git", "build")):
            continue
        for f in files:
            exts.add(os.path.splitext(f)[1].lower())
    if ".swift" in exts:
        if ".m" in exts or ".mm" in exts:
            return "hybrid"   # ObjC-оболочка + Swift-логика
        return "swift"
    if ".mm" in exts or ".cpp" in exts or ".cc" in exts:
        return "cpp"
    has_m = ".m" in exts
    has_c = ".c" in exts
    if has_m and has_c:
        return "c"          # glue.m + user.c
    if has_m:
        return "objc"
    if has_c:
        return "c"
    if os.path.exists(os.path.join(d, "main.py")):
        return "python"
    return None


def user_files(d, pattern):
    out = []
    for root, dirs, files in os.walk(d):
        dirs[:] = [x for x in dirs if x not in
                   (".theos", "packages", ".git", ".cache", "build")]
        for f in files:
            if f.endswith(pattern):
                out.append(os.path.join(root, f))
    return out


def make_info_plist(d, name, bid, version):
    tpl = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "templates", "Info.plist.tpl")
    with open(tpl) as f:
        s = f.read()
    s = (s.replace("__BUNDLE_ID__", bid)
          .replace("__EXEC__", name)
          .replace("__NAME__", name)
          .replace("__VERSION__", version))
    out = os.path.join(d, "Info.plist")
    with open(out, "w") as f:
        f.write(s)
    return out


def swift_base_cmd(tc):
    """Проверенный набор флагов для swiftc (theos-стиль):
    -sdk <sdk>, -resource-dir <toolchain>/lib/swift, -Xcc -isysroot <sdk>.
    `--sdk` в swiftc 5.8 не существует — только `-sdk`."""
    return [tc.swiftc,
            "-target", "arm64-apple-ios" + MIN_IOS,
            "-sdk", tc.sdk,
            "-resource-dir", os.path.join(tc.root, "lib", "swift"),
            "-Xcc", "-isysroot", "-Xcc", tc.sdk,
            "-swift-version", "5", "-O"]


def compile_swift_objects(tc, d, swift_files, name):
    """Swift -> .o + эммит ObjC-заголовка <name>-Swift.h.
    Возвращает (список .o, путь к заголовку)."""
    out = []
    hdr = os.path.join(d, "build", "%s-Swift.h" % name)
    for f in swift_files:
        bname = os.path.splitext(os.path.basename(f))[0] + ".o"
        o = os.path.join(d, "build", bname)
        c = swift_base_cmd(tc) + [
            "-I" + d, "-parse-as-library",
            "-emit-objc-header-path", hdr,
            "-c", f, "-o", o]
        if run(c) != 0:
            die("компиляция Swift не удалась: " + f)
        out.append(o)
    return out, hdr


def swift_runtime_ldflags(tc):
    """Линковка Swift: автолинкованные swift_* ищи в тулчейне;
    libswiftCore (5.8) не входит в toolchain — резолвится рантаймом
    устройства через -undefined dynamic_lookup (iOS >= 12.2 везёт
    Swift-рантайм в /usr/lib/swift)."""
    fl = ["-L" + os.path.join(tc.root, "lib", "swift-5.0", "iphoneos"),
          "-L" + os.path.join(tc.root, "lib", "swift", "iphoneos"),
          "-L" + tc.sdk + "/usr/lib/swift",
          "-Xlinker", "-rpath", "-Xlinker", "/usr/lib/swift",
          "-Xlinker", "-rpath", "-Xlinker", "/usr/lib/libswift/stable",
          "-Xlinker", "-undefined", "-Xlinker", "dynamic_lookup"]
    return fl


def build_app(tc, d, lang, name):
    """Компилирует исполняемый файл приложения. Возвращает путь."""
    app = os.path.join(d, "build", name)
    os.makedirs(os.path.join(d, "build"), exist_ok=True)
    swift_files = user_files(d, ".swift")
    c_files = user_files(d, ".c")
    m_files = user_files(d, ".m")
    mm_files = user_files(d, ".mm")

    # гибридный режим: .m/.mm + .swift -> swift .o + clang-линковка
    if swift_files and (m_files or mm_files):
        if not tc.swift:
            die("swiftc не найден. Установи: bash %s/toolchain/setup_ios.sh --swift"
                % os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        objs, hdr = compile_swift_objects(tc, d, swift_files, name)
        cmd = [tc.clang] + base_cflags(tc)
        if mm_files:
            cmd += ["-x", "objective-c++"]
        cmd += m_files + mm_files + c_files + objs
        cmd += ["-I" + os.path.join(d, "build")]
        cmd += swift_runtime_ldflags(tc)
        cmd += base_ldflags(tc) + ["-o", app]
        if run(cmd) != 0:
            die("линковка гибрида (ObjC+Swift) не удалась")
        log("Гибридная сборка ObjC+Swift: %s" % app)
        return app

    if lang in ("c", "cpp"):
        glue = "glue.m" if lang == "c" else "glue.mm"
        if not os.path.exists(os.path.join(d, glue)):
            die("нет %s в проекте (проверь шаблон)" % glue)
        driver = tc.clang if lang == "c" else tc.clangxx
        cmd = [driver] + base_cflags(tc)
        if lang == "cpp":
            cmd += ["-x", "objective-c++"]
        cmd += [os.path.join(d, glue)] + user_files(d, ".c" if lang == "c" else ".cpp")
        cmd += base_ldflags(tc) + ["-o", app]
        if run(cmd) != 0:
            die("компиляция не удалась")

    elif lang == "objc":
        files = user_files(d, ".m")
        if not files:
            die("нет .m файлов")
        cmd = [tc.clang] + base_cflags(tc) + files + base_ldflags(tc) + ["-o", app]
        if run(cmd) != 0:
            die("компиляция не удалась")

    elif lang == "swift":
        if not tc.swift:
            die("swiftc не найден в тулчейне. Скачай Swift-toolchain: "
                "bash addon/toolchain/setup_ios.sh --swift")
        files = user_files(d, ".swift")
        if not files:
            die("нет .swift файлов")
        cmd = swift_base_cmd(tc) + ["-parse-as-library",
                                    "-L" + tc.sdk + "/usr/lib",
                                    "-F" + tc.sdk + "/System/Library/Frameworks",
                                    "-framework", "UIKit",
                                    "-framework", "Foundation",
                                    "-Xlinker", "-undefined",
                                    "-Xlinker", "dynamic_lookup",
                                    "-emit-executable", "-o", app] + files
        if run(cmd) != 0:
            die("сборка Swift не удалась")

    elif lang == "python":
        if not os.path.exists(os.path.join(d, "launcher.m")):
            die("нет launcher.m в python-проекте")
        files = [os.path.join(d, "launcher.m")]
        cmd = [tc.clang] + base_cflags(tc) + files + base_ldflags(tc) + ["-o", app]
        if run(cmd) != 0:
            die("компиляция launcher.m не удалась")
        mpy = build_micropython(tc)
        shutil.copy2(mpy, os.path.join(d, "build", "python"))

    else:
        die("неизвестный язык: " + str(lang))
    log("Исполняемый файл: %s" % app)
    return app


def sign_all(bundled_app_dir, tc, mode, p12, p12pass):
    """Подписывает все Mach-O внутри .app."""
    for root, _, files in os.walk(bundled_app_dir):
        for f in files:
            p = os.path.join(root, f)
            if macho_info(p):
                cmd = [tc.ldid]
                if mode == "p12" and p12:
                    cmd += ["-k", p12, "-U", p12pass]
                else:
                    cmd += ["-S"]
                cmd.append(p)
                if run(cmd) != 0:
                    die("подпись не удалась: " + p)
                log("подписано: " + os.path.relpath(p, bundled_app_dir))


def build(d, lang=None, sign="adhoc"):
    d = os.path.abspath(d)
    name = os.path.basename(d.rstrip("/"))
    tc = TC()
    need_swift = (lang or detect_lang(d)) in ("swift", "hybrid")
    if not tc.ok(need_swift):
        die("тулчейн не готов: clang=%s sdk=%s ldid=%s swift=%s\n"
            "Установи: bash %s/toolchain/setup_ios.sh" %
            (tc.clang, tc.sdk, tc.ldid, tc.swift,
             os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    lang = lang or detect_lang(d)
    if not lang:
        die("не определён язык проекта (в папке нет .c/.cpp/.m/.swift/main.py)")
    log("проект: %s, язык: %s" % (d, lang))

    p12 = os.environ.get("CERT_P12", "")
    p12pass = os.environ.get("CERT_PASS", "dumb")

    bid = "com.abzal." + re.sub(r"[^a-zA-Z0-9]", "", name).lower() or "com.abzal.app"
    plist = make_info_plist(d, name, bid, "1.0.0")

    app = build_app(tc, d, lang, name)

    payload_root = os.path.join(d, "Payload")
    appdir = os.path.join(payload_root, name + ".app")
    if os.path.exists(payload_root):
        shutil.rmtree(payload_root)
    os.makedirs(appdir)
    shutil.copy2(app, os.path.join(appdir, name))
    shutil.copy2(plist, os.path.join(appdir, "Info.plist"))
    if lang == "python":
        shutil.copy2(os.path.join(d, "build", "python"),
                     os.path.join(appdir, "python"))
        for f in user_files(d, ".py"):
            shutil.copy2(f, os.path.join(appdir, os.path.basename(f)))

    sign_all(appdir, tc, sign, p12, p12pass)

    stamp = __import__("time").strftime("%Y%m%d-%H%M%S")
    outdir = os.path.join(d, "packages")
    os.makedirs(outdir, exist_ok=True)
    ipa = os.path.join(outdir, "%s_%s.ipa" % (name, stamp))
    cwd = os.getcwd()
    os.chdir(d)
    try:
        with zipfile.ZipFile(ipa, "w", zipfile.ZIP_DEFLATED) as z:
            for root, _, files in os.walk("Payload"):
                for f in files:
                    p = os.path.join(root, f)
                    z.write(p, os.path.relpath(p, "."))
    finally:
        os.chdir(cwd)
    log("ГОТОВО: %s" % ipa)
    ok = verify_ipa(ipa)
    return ipa if ok else None


# ---------------------------------------------------------------- new
LANG_TEMPLATES = {
    "c": "c", "cpp": "cpp", "objc": "objc", "swift": "swift", "python": "python",
}


def new(name, lang):
    lang = lang.lower()
    if lang not in LANG_TEMPLATES:
        die("язык: " + ", ".join(LANG_TEMPLATES))
    tpl_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "templates", lang)
    dst = os.path.join(HOME, "ios_projects", name)
    if os.path.exists(dst):
        die("каталог уже существует: " + dst)
    shutil.copytree(tpl_dir, dst)
    marker = os.path.join(dst, "ipabuild.txt")
    with open(marker, "w") as f:
        f.write("lang=%s\nname=%s\n" % (lang, name))
    log("проект создан: %s (язык: %s)" % (dst, lang))
    log("сборка: ipabuild.py build %s" % dst)
    return dst


# ---------------------------------------------------------------- check
def check():
    tc = TC()
    print("Тулчейн:")
    print("  корень  : %s" % (tc.root or "НЕ НАЙДЕН"))
    print("  clang   : %s" % (tc.clang or "-"))
    if tc.clang:
        p = run_out([tc.clang, "--version"], quiet_ok=True)
        if p.returncode == 0:
            print("            " + p.stdout.splitlines()[0])
    print("  swiftc  : %s" % (tc.swiftc if tc.swift else "НЕТ (для Swift: setup_ios.sh --swift)"))
    print("  ldid    : %s" % (tc.ldid or "-"))
    print("  SDK     : %s" % (tc.sdk or "НЕ НАЙДЕН"))

    ok = True
    if tc.ok():
        # быстрый smoke: C -> arm64
        with tempfile.TemporaryDirectory() as t:
            src = os.path.join(t, "h.c")
            with open(src, "w") as f:
                f.write("int main(){return 42;}\n")
            out = os.path.join(t, "h")
            p = run_out([tc.clang, "-target", "arm64-apple-ios" + MIN_IOS,
                         "--sysroot", tc.sdk, "-isysroot", tc.sdk,
                         src, "-o", out], quiet_ok=True)
            if p.returncode == 0 and macho_info(out) == "arm64":
                print("  smoke-test: C -> Mach-O arm64  OK")
            else:
                print("  smoke-test: FAIL\n" + p.stderr)
                ok = False
    else:
        print("\nТулчейн НЕ готов. Установи: bash addon/toolchain/setup_ios.sh")
    return ok


def main():
    ap = argparse.ArgumentParser(description="Сборка .ipa для iOS из Linux")
    sub = ap.add_subparsers(dest="cmd")

    sub.add_parser("check")

    p_new = sub.add_parser("new")
    p_new.add_argument("name")
    p_new.add_argument("--lang", default="c",
                       choices=sorted(LANG_TEMPLATES))

    p_build = sub.add_parser("build")
    p_build.add_argument("dir")
    p_build.add_argument("--lang", choices=sorted(LANG_TEMPLATES))
    p_build.add_argument("--sign", default="adhoc", choices=["adhoc", "p12"])

    p_verify = sub.add_parser("verify")
    p_verify.add_argument("ipa")

    sub.add_parser("micropython")

    a = ap.parse_args()
    if a.cmd == "check" or a.cmd is None:
        sys.exit(0 if check() else 1)
    elif a.cmd == "new":
        new(a.name, a.lang)
    elif a.cmd == "build":
        r = build(a.dir, lang=a.lang, sign=a.sign)
        sys.exit(0 if r else 1)
    elif a.cmd == "verify":
        sys.exit(0 if verify_ipa(a.ipa) else 1)
    elif a.cmd == "micropython":
        tc = TC()
        if not tc.ok():
            die("тулчейн не готов")
        build_micropython(tc)


if __name__ == "__main__":
    main()
