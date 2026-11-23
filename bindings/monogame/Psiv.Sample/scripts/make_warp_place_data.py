#!/usr/bin/env python3
"""Copies make_arrival_data.py's copy for warp_place.txt: a scene that places the leader right after a warp.

* the event `arrival_warp_place` warps to `arrival_field`'s `north_spot` (6,4), places the fighter on 2,2 facing east in
  the same step, walks it to 8,2 and says `arrival.talk`, so the field must show the place before the walk starts.

Usage: make_warp_place_data.py ARRIVAL_DATA OUT
"""

import shutil
import sys
from pathlib import Path

EVENT = """
event arrival_warp_place unskippable
	lock player
	warp arrival_field north_spot
	place fighter 2 0 2 facing east
	move fighter to 8 0 2
	say guide @arrival.talk
	unlock player
end
"""


def main():
	source, out = Path(sys.argv[1]), Path(sys.argv[2])
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(source, out)
	events = out / "game/data/events/arrival.ev"
	events.write_text(events.read_text() + EVENT)


if __name__ == "__main__":
	main()
