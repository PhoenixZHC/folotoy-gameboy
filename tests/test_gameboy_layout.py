import csv
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LayoutTests(unittest.TestCase):
    def test_single_game_app_and_no_recovery_partition(self):
        with (ROOT / "partitions.csv").open(newline="", encoding="utf-8") as file:
            rows = [row for row in csv.reader(file)
                    if row and not row[0].lstrip().startswith("#")]
        actual = [(r[0].strip(), r[1].strip(), r[2].strip(),
                   int(r[3].strip(), 0), int(r[4].strip(), 0)) for r in rows]
        self.assertEqual(actual, [
            ("nvs", "data", "nvs", 0x9000, 0x6000),
            ("phy_init", "data", "phy", 0xf000, 0x1000),
            ("game_app", "app", "factory", 0x10000, 0x300000),
            ("roms", "data", "0x40", 0x310000, 0x400000),
            ("saves", "data", "spiffs", 0x710000, 0x0f0000),
        ])
        self.assertEqual(actual[-1][3] + actual[-1][4], 0x800000)


if __name__ == "__main__":
    unittest.main()
