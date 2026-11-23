using System.Buffers.Binary;
using System.IO.Compression;

using Microsoft.Xna.Framework;

namespace Psiv.Sample;

// What the asset pipeline needs of PNG files without a graphics device: the size and format from the header, a reader
// (StbImageSharp, the decoder MonoGame's Texture2D.FromStream uses) and a writer for the placeholder export. Pixels in
// the sample are premultiplied, as SpriteBatch blends them; files hold straight alpha.
public static class Png
{
	private static readonly byte[] Signature = [137, 80, 78, 71, 13, 10, 26, 10];

	public readonly record struct Header(int Width, int Height, int BitDepth, int ColorType, bool Interlaced);

	// The IHDR chunk, or a reason the file is not a PNG the sample reads.
	public static Header? ReadHeader(string path, out string problem)
	{
		problem = "";
		byte[] head = new byte[33];
		try
		{
			using FileStream file = File.OpenRead(path);
			if (file.ReadAtLeast(head, head.Length, throwOnEndOfStream: false) < head.Length)
			{
				problem = "not a PNG file (too short)";
				return null;
			}
		}
		catch (Exception e) when (e is IOException or UnauthorizedAccessException)
		{
			problem = "cannot be read: " + e.Message;
			return null;
		}
		if (!head.AsSpan(0, 8).SequenceEqual(Signature) || head[12] != 'I' || head[13] != 'H' || head[14] != 'D' || head[15] != 'R')
		{
			problem = "not a PNG file";
			return null;
		}
		var header = new Header(BinaryPrimitives.ReadInt32BigEndian(head.AsSpan(16)), BinaryPrimitives.ReadInt32BigEndian(head.AsSpan(20)), head[24], head[25], head[28] != 0);
		// StbImageSharp reads 16-bit and interlaced files too, but 8-bit non-interlaced is what every tool writes by default
		// and what the export writes, so anything else is reported rather than relied on.
		if (header.BitDepth != 8 && !(header.ColorType == 3 && header.BitDepth is 1 or 2 or 4))
		{
			problem = $"{header.BitDepth}-bit channels; use 8-bit PNG";
			return null;
		}
		if (header.Interlaced)
		{
			problem = "interlaced; save it without interlacing";
			return null;
		}
		return header;
	}

	// Decoded to premultiplied pixels, row-major.
	public static Color[] Read(string path, out int width, out int height)
	{
		using FileStream file = File.OpenRead(path);
		StbImageSharp.ImageResult image = StbImageSharp.ImageResult.FromStream(file, StbImageSharp.ColorComponents.RedGreenBlueAlpha);
		width = image.Width;
		height = image.Height;
		return FromStraight(image.Data, width * height);
	}

	// An indexed PNG's (color type 3, 1 to 8 bits, not interlaced) palette index per pixel, row-major; null for any other.
	public static byte[]? ReadIndexes(string path, out int width, out int height)
	{
		width = height = 0;
		byte[] file = File.ReadAllBytes(path);
		if (file.Length < 33 || !file.AsSpan(0, 8).SequenceEqual(Signature))
		{
			return null;
		}
		int depth = 0;
		using var idat = new MemoryStream();
		for (int at = 8; at + 8 <= file.Length;)
		{
			int length = BinaryPrimitives.ReadInt32BigEndian(file.AsSpan(at));
			string type = System.Text.Encoding.ASCII.GetString(file, at + 4, 4);
			if (length < 0 || at + 12 + length > file.Length)
			{
				return null;
			}
			if (type == "IHDR")
			{
				width = BinaryPrimitives.ReadInt32BigEndian(file.AsSpan(at + 8));
				height = BinaryPrimitives.ReadInt32BigEndian(file.AsSpan(at + 12));
				depth = file[at + 16];
				if (file[at + 17] != 3 || file[at + 20] != 0 || depth is not (1 or 2 or 4 or 8))
				{
					return null;
				}
			}
			else if (type == "IDAT")
			{
				idat.Write(file, at + 8, length);
			}
			at += 12 + length;
		}
		if (depth == 0)
		{
			return null;
		}
		idat.Position = 0;
		using var inflated = new MemoryStream();
		using (var z = new ZLibStream(idat, CompressionMode.Decompress))
		{
			z.CopyTo(inflated);
		}
		byte[] raw = inflated.ToArray();
		int stride = (width * depth + 7) / 8;
		if (raw.Length < height * (stride + 1))
		{
			return null;
		}
		var rows = new byte[height * stride];
		for (int y = 0; y < height; y++)
		{
			int filter = raw[y * (stride + 1)];
			for (int x = 0; x < stride; x++)
			{
				int v = raw[y * (stride + 1) + 1 + x];
				int a = x > 0 ? rows[y * stride + x - 1] : 0, b = y > 0 ? rows[(y - 1) * stride + x] : 0, c = x > 0 && y > 0 ? rows[(y - 1) * stride + x - 1] : 0;
				int p = a + b - c, pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
				rows[y * stride + x] = (byte)(v + filter switch
				{
					1 => a,
					2 => b,
					3 => (a + b) / 2,
					4 => pa <= pb && pa <= pc ? a : pb <= pc ? b : c,
					_ => 0,
				});
			}
		}
		var indexes = new byte[width * height];
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				int bit = x * depth;
				indexes[y * width + x] = (byte)((rows[y * stride + bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1));
			}
		}
		return indexes;
	}

	public static Color[] FromStraight(ReadOnlySpan<byte> rgba, int count)
	{
		var pixels = new Color[count];
		for (int i = 0; i < count; i++)
		{
			int a = rgba[i * 4 + 3];
			pixels[i] = new Color((rgba[i * 4] * a + 127) / 255, (rgba[i * 4 + 1] * a + 127) / 255, (rgba[i * 4 + 2] * a + 127) / 255, a);
		}
		return pixels;
	}

	// Premultiplied pixels to an 8-bit RGBA PNG with straight alpha.
	public static void Write(string path, int width, int height, Color[] pixels)
	{
		byte[] raw = new byte[(width * 4 + 1) * height];
		for (int y = 0; y < height; y++)
		{
			int row = y * (width * 4 + 1);
			for (int x = 0; x < width; x++)
			{
				Color c = pixels[y * width + x];
				int o = row + 1 + x * 4;
				if (c.A > 0)
				{
					raw[o] = (byte)Math.Min(255, (c.R * 255 + c.A / 2) / c.A);
					raw[o + 1] = (byte)Math.Min(255, (c.G * 255 + c.A / 2) / c.A);
					raw[o + 2] = (byte)Math.Min(255, (c.B * 255 + c.A / 2) / c.A);
					raw[o + 3] = c.A;
				}
			}
		}
		using var compressed = new MemoryStream();
		using (var zlib = new ZLibStream(compressed, CompressionLevel.Optimal, leaveOpen: true))
		{
			zlib.Write(raw);
		}
		using FileStream file = File.Create(path);
		file.Write(Signature);
		byte[] ihdr = new byte[13];
		BinaryPrimitives.WriteInt32BigEndian(ihdr, width);
		BinaryPrimitives.WriteInt32BigEndian(ihdr.AsSpan(4), height);
		ihdr[8] = 8;
		ihdr[9] = 6;
		Chunk(file, "IHDR", ihdr);
		Chunk(file, "IDAT", compressed.ToArray());
		Chunk(file, "IEND", []);
	}

	private static void Chunk(Stream s, string type, byte[] data)
	{
		Span<byte> word = stackalloc byte[4];
		BinaryPrimitives.WriteInt32BigEndian(word, data.Length);
		s.Write(word);
		byte[] typed = [.. type.Select(c => (byte)c), .. data];
		s.Write(typed);
		BinaryPrimitives.WriteUInt32BigEndian(word, Crc(typed));
		s.Write(word);
	}

	private static readonly uint[] CrcTable = Enumerable.Range(0, 256).Select(n =>
	{
		uint c = (uint)n;
		for (int k = 0; k < 8; k++)
		{
			c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		}
		return c;
	}).ToArray();

	private static uint Crc(byte[] data)
	{
		uint c = 0xffffffffu;
		foreach (byte b in data)
		{
			c = CrcTable[(c ^ b) & 0xff] ^ (c >> 8);
		}
		return c ^ 0xffffffffu;
	}
}
