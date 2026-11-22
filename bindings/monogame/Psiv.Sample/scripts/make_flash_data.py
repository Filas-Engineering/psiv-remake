#!/usr/bin/env python3
"""Copies the placeholder data with a `flashing.json` the sample reads (docs/ui-layout.md, "Flashing"): a new profile's
flashing level, and optionally the `original` choice hidden, for scripts/flashing_notice.txt.

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
	flashing = {"default": level}
	if "--hide-original" in sys.argv[5:]:
		flashing["original"] = False
	(out / "game" / "data" / "flashing.json").write_text(json.dumps(flashing, indent="\t") + "\n")


if __name__ == "__main__":
	main()
