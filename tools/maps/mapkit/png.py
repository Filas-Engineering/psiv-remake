"""A minimal PNG writer, so the cell tileset can carry the image file Tiled needs."""

import struct
import zlib


def _chunk(kind, data):
	return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def encode(width, height, pixel):
	"""PNG bytes for an RGB image; pixel(x, y) returns (r, g, b)."""
	raw = b"".join(b"\x00" + bytes(c for x in range(width) for c in pixel(x, y)) for y in range(height))
	header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
	return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b"")


# Floor, wall, water, soft ground, barrier and counter, in tile id order.
COLORS = [(214, 196, 150), (84, 78, 92), (70, 120, 180), (150, 112, 70), (150, 60, 50), (170, 120, 60)]


def cell_tiles(tile_size=16):
	"""The cell tileset image: one flat tile per kind with a darker one-pixel border."""

	def pixel(x, y):
		r, g, b = COLORS[x // tile_size]
		edge = x % tile_size in (0, tile_size - 1) or y in (0, tile_size - 1)
		return (r * 3 // 4, g * 3 // 4, b * 3 // 4) if edge else (r, g, b)

	return encode(tile_size * len(COLORS), tile_size, pixel)
