#!/usr/bin/env python3
"""Extract the name tables from a Phantasy Star IV cartridge dump.

Usage: python3 -I tools/extract_names.py <dump> <output.tsv>

Names are stored uncompressed: one byte per character, letters as ASCII
minus 0x40, each name ended by 0xFE or 0xFF. Output is a tab-separated file
and must stay inside source-material/, which git ignores.
"""
import sys

SEPARATORS = {0x00: " ", 0x31: "-", 0x3C: " "}
TERMINATORS = (0xFE, 0xFF)
MIN_TABLE_NAMES = 8
MAX_NAME_BYTES = 16


def decode(raw):
	out = []
	for byte in raw:
		if 1 <= byte <= 26:
			out.append(chr(byte + 0x40))
		elif byte in SEPARATORS:
			out.append(SEPARATORS[byte])
		else:
			return None
	text = "".join(out)
	if not any(ch.isalpha() for ch in text):
		return None
	return text


def find_tables(rom):
	tables = []
	pos = 0
	size = len(rom)
	while pos < size:
		names = []
		cursor = pos
		while cursor < size:
			end = cursor
			while end < size and end - cursor <= MAX_NAME_BYTES and rom[end] not in TERMINATORS:
				end += 1
			if end >= size or rom[end] not in TERMINATORS or end - cursor < 2:
				break
			text = decode(rom[cursor:end])
			if text is None:
				break
			names.append((cursor, rom[end], text))
			cursor = end + 1
		if len(names) >= MIN_TABLE_NAMES:
			tables.append(names)
			pos = cursor
		else:
			pos += 1
	return tables


def main():
	dump_path, out_path = sys.argv[1], sys.argv[2]
	with open(dump_path, "rb") as handle:
		rom = handle.read()
	tables = find_tables(rom)
	with open(out_path, "w") as out:
		out.write("table\toffset\tterminator\tlength\tname\n")
		for index, names in enumerate(tables):
			for offset, terminator, text in names:
				out.write(f"{index}\t{offset:06X}\t{terminator:02X}\t{len(text)}\t{text}\n")
	for index, names in enumerate(tables):
		longest = max(len(text) for _, _, text in names)
		print(f"table {index}: {names[0][0]:06X}  {len(names)} names  longest {longest}")


if __name__ == "__main__":
	main()
