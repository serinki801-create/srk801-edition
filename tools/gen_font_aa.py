#!/usr/bin/env python3
"""Генерация font_aa.h: 8x8 grayscale AA-глифы из DejaVu Sans (host-only)."""
import glob
from PIL import Image, ImageDraw, ImageFont

FONT_SIZE = 24
CELL = 8
SCALE = FONT_SIZE // CELL  # 3


def find_font():
    patterns = [
        "/usr/share/fonts/**/DejaVuSans.ttf",
        "/usr/share/fonts/**/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/**/NimbusSans-Regular.ttf",
        "/usr/share/fonts/**/*.ttf",
    ]
    for pat in patterns:
        hits = glob.glob(pat, recursive=True)
        if hits:
            return sorted(hits)[0]
    raise SystemExit("no TTF font found")


path = find_font()
print("font:", path)
font = ImageFont.truetype(path, FONT_SIZE)
asc, desc = font.getmetrics()
baseline = asc  # y базовой линии в коробке 24x24

glyphs = []
montage = Image.new("L", (CELL * 96, CELL), 0)

for c in range(0x20, 0x80):
    img = Image.new("L", (FONT_SIZE, FONT_SIZE), 0)
    d = ImageDraw.Draw(img)
    d.text((0, baseline), chr(c), font=font, fill=255, anchor="ls")
    small = img.resize((CELL, CELL), Image.BOX)
    px = list(small.getdata())
    glyphs.append(px)
    montage.paste(small, (CELL * (c - 0x20), 0))

# preview 8x для проверки
preview = montage.resize((CELL * 96 * 8, CELL * 8), Image.BOX)
preview.save("/tmp/font_aa_preview.png")

lines = []
lines.append("// font_aa.h - 8x8 grayscale AA-шрифт (сгенерировано из DejaVu Sans)")
lines.append("// Рендер 24px -> area-average 8x8. Индекс: c - 0x20 (96 глифов).")
lines.append("// Не редактировать. Пересборка: tools/gen_font_aa.py")
lines.append("#ifndef FONT_AA_H_INCLUDED")
lines.append("#define FONT_AA_H_INCLUDED")
lines.append("#include <stdint.h>")
lines.append("")
lines.append("#define FONT_AA_W 8")
lines.append("#define FONT_AA_H 8")
lines.append("#define FONT_AA_FIRST 0x20")
lines.append("#define FONT_AA_COUNT 96")
lines.append("")
lines.append("static const uint8_t font_aa[FONT_AA_COUNT][FONT_AA_H][FONT_AA_W] = {")
for c in range(0x20, 0x80):
    g = glyphs[c - 0x20]
    rows = []
    for r in range(CELL):
        rows.append("{" + ",".join(str(g[r * CELL + x]) for x in range(CELL)) + "}")
    lines.append("    /* 0x%02X */ {%s}," % (c, ",".join(rows)))
lines.append("};")
lines.append("")
lines.append("#endif // FONT_AA_H_INCLUDED")

with open("/home/abzal/hobby-os/font_aa.h", "w") as f:
    f.write("\n".join(lines) + "\n")

print("ok: /home/abzal/hobby-os/font_aa.h, preview /tmp/font_aa_preview.png")
