#!/usr/bin/env python3
"""Writes OUT as SAVE would have been written under save schema 15: the header without its `money` and `member` lines,
which schema 16 added, the header chunk's length and the checksum made to match.

Usage: make_old_save.py SAVE OUT
"""

import sys
from pathlib import Path


def fnv(data):
	h = 1469598103934665603
	for b in data:
		h ^= b
		h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
	return h


def main():
	lines = Path(sys.argv[1]).read_bytes().decode("utf-8").split("\n")
	if not lines[0].startswith("PSR-SAVE "):
		sys.exit(f"{sys.argv[1]}: not a save")
	lines[0] = "PSR-SAVE 15"
	header = next(i for i, line in enumerate(lines) if line.startswith("chunk header "))
	end = next(i for i in range(header + 1, len(lines)) if lines[i].startswith("chunk "))
	payload = [line for line in lines[header + 1:end] if not line.startswith(("money ", "member "))]
	payload = ["schema 15" if line.startswith("schema ") else line for line in payload]
	size = sum(len(line.encode("utf-8")) + 1 for line in payload) - 1
	kind = lines[header].split(" ")
	lines[header] = " ".join(kind[:3] + [str(size)])
	lines = lines[:header + 1] + payload + lines[end:]
	trailer = next(i for i in range(len(lines) - 1, -1, -1) if lines[i].startswith("end "))
	body = ("\n".join(lines[:trailer]) + "\n").encode("utf-8")
	Path(sys.argv[2]).write_bytes(body + f"end {fnv(body):016x}\n".encode("utf-8"))


if __name__ == "__main__":
	main()
