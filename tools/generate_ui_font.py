"""Generate the flash-resident BMP font used by the game shell (Pillow, fonttools)."""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets/fonts/NotoSansSC-wght.ttf"
OUT = ROOT / "main/ui_font.c"
TEXT = (ROOT / "main/game_main.c").read_text(encoding="utf-8")


def main() -> None:
    # Include the font's BMP glyphs so user-uploaded Chinese ROM names remain readable.
    source = TTFont(str(FONT))
    codepoints = set().union(*(set(table.cmap) for table in source["cmap"].tables
                               if table.isUnicode()))
    source.close()
    codepoints = {cp for cp in codepoints if 0x80 <= cp <= 0xFFFF}
    codepoints.update(ord(char) for char in TEXT if "\u4e00" <= char <= "\u9fff")
    chars = [chr(cp) for cp in sorted(codepoints)]

    def make_rows(size: int, weight: int) -> list[str]:
        font = ImageFont.truetype(str(FONT), size)
        font.set_variation_by_axes([weight])
        rows = []
        for char in chars:
            image = Image.new("L", (size, size), 0)
            ImageDraw.Draw(image).text((0, 0), char, font=font, fill=255, anchor="lt")
            bits = []
            for y in range(size):
                row = 0
                for x in range(size):
                    if image.getpixel((x, y)) >= 96:
                        row |= 1 << (15 - x)
                bits.append(row)
            if not any(bits):
                if char in TEXT:
                    raise ValueError(f"missing UI glyph: U+{ord(char):04X} at {size}px")
                continue
            rows.append(f"    {{0x{ord(char):04X}, {{{', '.join(f'0x{r:04X}' for r in bits)}}}}},")
        return rows

    rows = make_rows(16, 700)
    OUT.write_text(
        '#include "ui_font.h"\n'
        '// Generated from Noto Sans SC Bold 700 under SIL OFL 1.1.\n'
        'static const ui_glyph_t s_glyphs[] = {\n' + '\n'.join(rows) + '\n};\n'
        'const uint16_t *ui_font_rows(uint32_t codepoint) {\n'
        '    size_t lo = 0, hi = sizeof(s_glyphs) / sizeof(s_glyphs[0]);\n'
        '    while (lo < hi) {\n'
        '        size_t mid = lo + (hi - lo) / 2;\n'
        '        if (s_glyphs[mid].codepoint < codepoint) lo = mid + 1;\n'
        '        else hi = mid;\n'
        '    }\n'
        '    return lo < sizeof(s_glyphs) / sizeof(s_glyphs[0]) &&\n'
        '           s_glyphs[lo].codepoint == codepoint ? s_glyphs[lo].rows : NULL;\n'
        '}\n', encoding="utf-8")
    print(f"Generated {len(rows)} glyphs in {OUT}")


if __name__ == "__main__":
    main()
