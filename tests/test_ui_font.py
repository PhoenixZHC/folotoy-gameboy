"""Every fixed Chinese shell character must have a generated bitmap."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ui = (ROOT / "main/game_main.c").read_text(encoding="utf-8")
font = (ROOT / "main/ui_font.c").read_text(encoding="utf-8")
used = {ord(char) for char in ui if "\u4e00" <= char <= "\u9fff"}
used.update(ord(char) for char in "口袋妖怪蓝精灵宝可梦")
covered = {int(value, 16) for value in re.findall(r"\{0x([0-9A-F]{4}), \{", font)}
missing = used - covered
assert not missing, f"Missing UI glyphs: {[f'U+{value:04X}' for value in sorted(missing)]}"
