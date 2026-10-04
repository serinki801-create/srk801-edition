#!/usr/bin/env python3
"""Статические тесты Phase 5: multiboot, символы защиты/WM, user ELF, TAR."""
import os
import struct
import subprocess
import sys
import tarfile

FAIL = []


def check(name, cond):
    print(('PASS' if cond else 'FAIL'), '-', name)
    if not cond:
        FAIL.append(name)


def nm_syms(path):
    out = subprocess.run(['nm', path], capture_output=True, text=True)
    return out.stdout


def main():
    kd = open('kernel.elf', 'rb').read()

    # 1. Multiboot2 header.
    off = 0x1000
    magic, arch, hlen, chk = struct.unpack_from('<IIII', kd, off)
    check('MB2 magic/arch/checksum', magic == 0xE85250D6 and arch == 0
          and (magic + arch + hlen + chk) & 0xFFFFFFFF == 0)
    check('MB2 len==80, aligned, in first 8K',
          hlen == 80 and hlen % 8 == 0 and off + hlen <= 8192)
    p = off + 16
    tags = []
    ok = True
    while True:
        t, _, s = struct.unpack_from('<HHI', kd, p)
        tags.append((t, s))
        if t == 0:
            break
        p += (s + 7) & ~7
        if p > off + hlen:
            ok = False
            break
    check('MB2 tag walk', ok and tags == [(1, 24), (5, 20), (6, 8), (0, 8)])

    # 2. Символы защиты/Phase5 в ядре.
    syms = nm_syms('kernel.elf')
    for s in ['security_init', 'security_set_exec',
              'security_check_user_ptr', 'security_set_user_stack',
              '__stack_chk_guard', '__stack_chk_fail',
              'idt_set_entry_dpl', 'gdt_set_tss_rsp0',
              'wm_render', 'desktop_run', 'desktop_init',
              'mouse_init', 'mouse_tick',
              'text_start', 'text_end', 'kernel_end']:
        check(f'kernel symbol {s}', s in syms)

    # 3. Индикаторы кода (исходники): user-селекторы, NX, MSR, DPL=3.
    src = {f: open(f).read() for f in
           ['gdt.c', 'security.c', 'syscall.c', 'user.asm', 'idt.c']}
    check('GDT user code DPL=3 (0xFA)', '0xFA' in src['gdt.c'])
    check('GDT user data DPL=3 (0xF2)', '0xF2' in src['gdt.c'])
    check('GDT TSS selector 0x28', '0x28' in src['gdt.c'])
    check('NX bit (1ULL<<63 / bit 63)', '63' in src['security.c'])
    check('EFER.NXE enable', 'NXE' in src['security.c'])
    check('syscall DPL=3 gate', ', 3)' in src['syscall.c'])
    check('SYSCALL MSRs (STAR/LSTAR/FMASK)', 'STAR' in src['syscall.c']
          and 'LSTAR' in src['syscall.c'] and 'FMASK' in src['syscall.c'])
    check('user.asm iretq frame 0x1B/0x23',
          '0x1B' in src['user.asm'] and '0x23' in src['user.asm'])
    mk_lines = [l for l in open('Makefile').read().splitlines()
                if l.strip() and not l.strip().startswith('#')]
    mk_code = '\n'.join(mk_lines)
    check('stack protector flag in Makefile',
          '-fstack-protector-strong' in mk_code
          and '-fno-stack-protector' not in mk_code)

    # 4. user_prog.elf.
    ud = open('user_prog.elf', 'rb').read()
    check('user ELF magic/class', ud[:4] == b'\x7fELF' and ud[4] == 2)
    etype, mach = struct.unpack_from('<HH', ud, 16)
    entry, phoff = struct.unpack_from('<QQ', ud, 24)
    phnum = struct.unpack_from('<H', ud, 56)[0]
    check('user ET_EXEC x86-64', etype == 2 and mach == 62)
    check('user entry in [0x400000,512M)',
          0x400000 <= entry < 0x20000000)
    loads_ok = True
    for i in range(phnum):
        pt, _, foff, va, _, fsz, msz, _ = struct.unpack_from(
            '<IIQQQQQQ', ud, phoff + i * 56)
        if pt == 1 and msz:
            if not (0x400000 <= va and va + msz < 0x20000000
                    and foff + fsz <= len(ud)):
                loads_ok = False
    check('user PT_LOAD in user area', loads_ok)
    r = subprocess.run(['readelf', '-d', 'user_prog.elf'],
                       capture_output=True, text=True)
    dyn_out = (r.stdout + r.stderr).lower()
    check('user static (no dynamic section)',
          'нет динамического' in dyn_out or 'no dynamic section' in dyn_out)

    # 5. initrd TAR.
    names = []
    with tarfile.open('initrd.img', 'r') as t:
        for m in t.getmembers():
            n = m.name
            while n.startswith('./'):
                n = n[2:]
            if m.isfile() and '/' not in n and n:
                names.append(n)
    check('initrd has hello.txt', 'hello.txt' in names)
    check('initrd has readme.txt', 'readme.txt' in names)
    check('initrd has user_prog.elf', 'user_prog.elf' in names)

    # 6. Хостовые скрипты валидны.
    for _sh in ['arch_macos_build.sh', 'setup_host.sh',
                'run_xcode_container.sh', 'setup_macos_ui.sh',
                'setup_ios_toolchain.sh', 'full_macos_setup.sh']:
        r = subprocess.run(['bash', '-n', _sh])
        check(f'{_sh} syntax (bash -n)', r.returncode == 0)
    check('IPA_BUILD_GUIDE.md exists',
          os.path.exists('IPA_BUILD_GUIDE.md'))
    check('FULL_INSTRUCTIONS.txt exists',
          os.path.exists('FULL_INSTRUCTIONS.txt'))

    # 7. Linux Mint порт (Makefile NASM-autodetect, без хардкода Flatpak).
    mk_full = open('Makefile').read()
    check('Makefile NASM autodetect (no hardcoded AS path)',
          'FLATPAK_NASM' in mk_full and 'command -v nasm' in mk_full)
    check('Makefile keeps stack protector',
          '-fstack-protector-strong' in mk_full)
    check('minicc.c in kernel build', 'minicc.c' in mk_full)

    # 8. Phase 6: glass/anim/lowfx + встроенный компилятор в ядре.
    for s in ['fb_fill_rect_alpha', 'fb_glass_rect', 'fb_fill_round_rect',
              'fb_draw_string_transp', 'wm_tick', 'wm_set_lowfx', 'wm_lowfx',
              'mini_compile_run', 'mini_build_verbose']:
        check(f'kernel symbol {s}', s in syms)

    # 9. Хост-тулчейн Mint: build.sh + GNUstep-демо + шим + сэмплы.
    for _sh in ['build.sh']:
        r = subprocess.run(['bash', '-n', _sh])
        check(f'{_sh} syntax (bash -n)', r.returncode == 0)
    for _f in ['tools/host_demo.m', 'tools/include/objc/blocks_runtime.h',
               'samples/hello.c', 'samples/hello.cpp',
               'samples/hello.m', 'samples/hello.swift']:
        check(f'{_f} exists', os.path.exists(_f))

    print()
    if FAIL:
        print(f'{len(FAIL)} FAILURES')
        return 1
    print('ALL CHECKS PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
