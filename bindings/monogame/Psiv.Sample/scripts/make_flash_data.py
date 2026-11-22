#!/usr/bin/env python3
"""Copies the placeholder data with game.json's `presentation.flash_default` (docs/ui-layout.md, "Flashing"): a new
profile's flashing level, and optionally `presentation.flash_original` false hiding the `original` choice, for
scripts/flashing_notice.txt.

Usage: make_flash_data.py GAME_DATA CORE_DATA OUT LEVEL [--hide-original]
"""

import json
import shutil
import sys
from pathlib import Path


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	level = sys.argv[4]
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	game_file = out / "game" / "data" / "game.json"
	config = json.loads(game_file.read_text())
	config["presentation"]["flash_default"] = level
	if "--hide-original" in sys.argv[5:]:
		config["presentation"]["flash_original"] = False
	game_file.write_text(json.dumps(config, indent="\t", ensure_ascii=False) + "\n")


if __name__ == "__main__":
	main()
