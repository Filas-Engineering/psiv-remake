#!/usr/bin/env python3
"""Copies make_arrival_data.py's copy for arrival_hold.txt: an arrival event that speaks, then warps the party onward.

* the load hook's event `arrival_land` (Saltgrass's `return` lands on `arrival_field`, which plays `town_canal_market`)
  says `arrival.talk`, warps to Tidewater's `inn_front` (`town_tide_bells`) and says `arrival.stacked` there, so the
  landing area's music and both lines meet the map load hold (300 ms); `arrival_spare`, run by nothing, keeps the copy's
  other two lines in use and a warp into the field for the data check.

Usage: make_arrival_hold_data.py ARRIVAL_DATA OUT
"""

import re
import shutil
import sys
from pathlib import Path

EVENT = """event arrival_land unskippable
	lock player
	set arrival.warped
	say guide @arrival.talk
	warp tidewater inn_front
	say guide @arrival.stacked
	unlock player
end

event arrival_spare unskippable
	warp arrival_field north_spot
	say guide @arrival.up
	say guide @arrival.wall
end
"""


def main():
	source, out = Path(sys.argv[1]), Path(sys.argv[2])
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(source, out)
	events = out / "game/data/events/arrival.ev"
	events.write_text(re.sub(r"event arrival_land unskippable\n.*?\nend\n", EVENT, events.read_text(), count=1, flags=re.S))


if __name__ == "__main__":
	main()
