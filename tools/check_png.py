#!/usr/bin/env python3
"""Fails unless the file is a readable 8-bit RGB or RGBA PNG with at least MIN_COLORS colors.

Usage: check_png.py FILE.png [MIN_COLORS]   (default 16)

Standard library only: the IDAT chunks are inflated with zlib and every scanline is unfiltered, so a picture that is
one color, or a few, is told apart from a drawn frame.
"""

import struct
import sys
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def chunks(data):
	pos = len(SIGNATURE)
	while pos + 8 <= len(data):
		length, kind = struct.unpack(">I4s", data[pos:pos + 8])
		yield kind, data[pos + 8:pos + 8 + length]
		pos += 12 + length


def paeth(a, b, c):
	p = a + b - c
	pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
	if pa <= pb and pa <= pc:
		return a
	return b if pb <= pc else c


def rows(width, height, channels, raw):
	stride = width * channels
	previous = bytearray(stride)
	pos = 0
	for _ in range(height):
		kind = raw[pos]
		line = bytearray(raw[pos + 1:pos + 1 + stride])
		pos += 1 + stride
		for i in range(stride):
			left = line[i - channels] if i >= channels else 0
			up = previous[i]
			upLeft = previous[i - channels] if i >= channels else 0
			if kind == 1:
				line[i] = (line[i] + left) & 0xFF
			elif kind == 2:
				line[i] = (line[i] + up) & 0xFF
			elif kind == 3:
				line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
			elif kind == 4:
				line[i] = (line[i] + paeth(left, up, upLeft)) & 0xFF
			elif kind != 0:
				raise ValueError("unknown filter type %d" % kind)
		yield bytes(line)
		previous = line


def main():
	if len(sys.argv) not in (2, 3):
		print(__doc__.strip().splitlines()[2], file=sys.stderr)
		return 2
	path = sys.argv[1]
	minColors = int(sys.argv[2]) if len(sys.argv) == 3 else 16
	with open(path, "rb") as f:
		data = f.read()
	if not data.startswith(SIGNATURE):
		print("%s: not a PNG (%d bytes)" % (path, len(data)))
		return 1
	header = None
	idat = b""
	for kind, body in chunks(data):
		if kind == b"IHDR":
			header = struct.unpack(">IIBBBBB", body)
		elif kind == b"IDAT":
			idat += body
	if header is None:
		print("%s: no IHDR chunk" % path)
		return 1
	width, height, depth, color, _, _, interlace = header
	if depth != 8 or color not in (2, 6) or interlace != 0:
		print("%s: unsupported PNG (bit depth %d, color type %d, interlace %d)" % (path, depth, color, interlace))
		return 1
	channels = 3 if color == 2 else 4
	raw = zlib.decompress(idat)
	if len(raw) != height * (1 + width * channels):
		print("%s: image data is %d bytes, expected %d" % (path, len(raw), height * (1 + width * channels)))
		return 1
	colors = set()
	distinctRows = set()
	for line in rows(width, height, channels, raw):
		distinctRows.add(line)
		if len(colors) < minColors:
			colors.update(line[i:i + 3] for i in range(0, len(line), channels))
	if len(colors) < minColors:
		print("%s: %dx%d with only %d colors and %d distinct rows" % (path, width, height, len(colors), len(distinctRows)))
		return 1
	print("%s: %dx%d, %d distinct rows of %d, at least %d colors" % (path, width, height, len(distinctRows), height, len(colors)))
	return 0


if __name__ == "__main__":
	sys.exit(main())
