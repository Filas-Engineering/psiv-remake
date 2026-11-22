#!/usr/bin/env python3
"""Writes the asset directory scripts/library_macros.txt plays: a copy of the test assets with layout files built from
the export's built-in ones and the macro choices docs/ui-layout.md describes:

* menu_macros: the Options action (`options` 1), where a macro's `on_trouble`, `recover_who`, `unnamed` and `policy`
  are stepped;
* battle_front: each step of the highlighted macro marked before Confirm (`macro_check` 1), a failed step losing the
  member's turn (`macro_failed` "lose_turn"), the menus opening on Macro and the macro played last after its round
  (`macro_flow` "remember"), and the Auto row asking the trouble mode and the policy (`auto_choose` 1);
* extras: the settings rows `auto_trouble` and `auto_policy` beside the options and controls;
* alt/battle_front_broken.json and alt/menu_macros_broken.json: a bad `macro_failed` word, a bad `macro_flow` word and a
  non-number `options`, which the Makefile checks on a copy.

Usage: make_library_assets.py EXPORT TEST OUT

EXPORT is the placeholder export (its ui/ files are the built-in layouts), TEST the test assets (make_test_assets.py).
"""

import json
import shutil
import sys
from pathlib import Path


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def main():
	export, test, out = (Path(a) for a in sys.argv[1:4])
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(test, out)
	ui = out / "ui"
	built_in = export / "ui"

	macros = load(built_in / "menu_macros.json")
	save(out / "alt/menu_macros_broken.json", dict(macros, params={"options": "yes"}))
	save(ui / "menu_macros.json", dict(macros, params={"options": 1}))

	front = load(built_in / "battle_front.json")
	broken = json.loads(json.dumps(front))
	broken["params"].update({"macro_failed": "skip", "macro_flow": "keep"})
	save(out / "alt/battle_front_broken.json", broken)
	front["params"].update({"macro_check": 1, "macro_failed": "lose_turn", "macro_flow": "remember", "auto_choose": 1})
	save(ui / "battle_front.json", front)

	extras = load(built_in / "extras.json")
	extras["entries"] = [{"page": "options"}, {"page": "controls"}, {"page": "auto_trouble"}, {"page": "auto_policy"}]
	save(ui / "extras.json", extras)


if __name__ == "__main__":
	main()
