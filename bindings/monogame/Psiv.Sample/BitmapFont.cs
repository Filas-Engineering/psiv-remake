using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework;

namespace Psiv.Sample;

// Which text a bitmap font entry may be given for: the dialogue box's lines and speaker plate, the floating numbers and
// words over battle targets, and everything else.
public enum TextRole { Menu, Dialogue, BattleNumbers }

// Where a glyph's rectangle sits in the sheet, where it is drawn from the pen position and the line's top (Offset), and
// how far it moves the pen, all in file pixels.
public sealed record GlyphMetrics(int X, int Y, int Width, int Height, int OffsetX, int OffsetY, int Advance);

// A bitmap font's metrics file as read: Size is the text size the sheet draws at one screen pixel a file pixel.
public sealed record FontMetrics(int Size, int LineHeight, int Baseline, int Space, IReadOnlyDictionary<char, GlyphMetrics> Glyphs,
	IReadOnlyDictionary<(char, char), int> Kerning)
{
	public const string Fields = "size, line_height, baseline, space, advance, cell, rows, glyphs, kerning";

	// Reads and checks the metrics against the glyph sheet's size; every problem goes to `problem` with the field it is in.
	public static FontMetrics? Read(string path, int imageWidth, int imageHeight, Action<string> problem)
	{
		JsonElement root;
		try
		{
			using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path), new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip });
			root = doc.RootElement.Clone();
		}
		catch (JsonException e)
		{
			problem("not valid JSON: " + e.Message);
			return null;
		}
		if (root.ValueKind != JsonValueKind.Object)
		{
			problem("must be an object");
			return null;
		}
		int problems = 0;
		void Problem(string reason)
		{
			problems++;
			problem(reason);
		}
		foreach (JsonProperty p in root.EnumerateObject().Where(p => !Fields.Split(", ").Contains(p.Name)))
		{
			Problem($"unknown field '{p.Name}'");
		}
		int Int(JsonElement e, string name, string where, int fallback, int min, int max)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				return fallback;
			}
			if (v.ValueKind != JsonValueKind.Number || !v.TryGetInt32(out int n) || n < min || n > max)
			{
				Problem($"{where}'{name}' must be a whole number from {min} to {max}");
				return fallback;
			}
			return n;
		}
		int[]? Ints(JsonElement e, string name, string where, int count, int min, int max)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				return null;
			}
			if (v.ValueKind != JsonValueKind.Array || v.GetArrayLength() != count
				|| v.EnumerateArray().Any(x => x.ValueKind != JsonValueKind.Number || !x.TryGetInt32(out int n) || n < min || n > max))
			{
				Problem($"{where}'{name}' must be a list of {count} whole numbers from {min} to {max}");
				return null;
			}
			return [.. v.EnumerateArray().Select(x => x.GetInt32())];
		}
		if (!root.TryGetProperty("line_height", out _))
		{
			Problem("missing field 'line_height'");
		}
		int lineHeight = Int(root, "line_height", "", 1, 1, 256);
		int baseline = Int(root, "baseline", "", lineHeight, 0, lineHeight);
		int size = Int(root, "size", "", lineHeight, 1, 256);
		int[]? cell = Ints(root, "cell", "", 2, 1, 256);
		int advance = Int(root, "advance", "", cell?[0] ?? -1, 0, 1024);
		int space = Int(root, "space", "", advance >= 0 ? advance : Math.Max(1, lineHeight / 3), 0, 1024);
		var glyphs = new Dictionary<char, GlyphMetrics>();
		bool Inside(int x, int y, int w, int h) => x + w <= imageWidth && y + h <= imageHeight;
		if (root.TryGetProperty("rows", out JsonElement rows))
		{
			if (cell is null)
			{
				Problem("'rows' needs 'cell', the [width, height] of each glyph's cell in the sheet");
			}
			else if (rows.ValueKind != JsonValueKind.Array || rows.EnumerateArray().Any(r => r.ValueKind != JsonValueKind.String))
			{
				Problem("'rows' must be a list of strings, one per row of cells");
			}
			else
			{
				int y = 0;
				foreach (JsonElement row in rows.EnumerateArray())
				{
					string chars = row.GetString()!;
					if (!Inside(0, y * cell[1], chars.Length * cell[0], cell[1]))
					{
						Problem($"rows[{y}]: {chars.Length} cells of {cell[0]}x{cell[1]} at row {y} do not fit the {imageWidth}x{imageHeight} sheet");
					}
					for (int x = 0; x < chars.Length; x++)
					{
						char c = chars[x];
						if (c == ' ')
						{
							continue;
						}
						if (!glyphs.TryAdd(c, new GlyphMetrics(x * cell[0], y * cell[1], cell[0], cell[1], 0, 0, advance >= 0 ? advance : cell[0])))
						{
							Problem($"rows[{y}]: '{c}' is in the rows more than once");
						}
					}
					y++;
				}
			}
		}
		else if (cell is not null)
		{
			Problem("'cell' needs 'rows', the characters of each row of cells");
		}
		if (root.TryGetProperty("glyphs", out JsonElement map))
		{
			if (map.ValueKind != JsonValueKind.Object)
			{
				Problem("'glyphs' must be an object of one character to its 'rect', 'advance' and 'offset'");
			}
			else
			{
				foreach (JsonProperty g in map.EnumerateObject())
				{
					string where = $"glyphs.{g.Name}: ";
					if (g.Name.Length != 1 || char.IsSurrogate(g.Name[0]))
					{
						Problem($"{where}a glyph's key is one character");
						continue;
					}
					if (g.Value.ValueKind != JsonValueKind.Object)
					{
						Problem($"{where}must be an object with 'rect', 'advance' and 'offset'");
						continue;
					}
					foreach (JsonProperty p in g.Value.EnumerateObject().Where(p => p.Name is not ("rect" or "advance" or "offset")))
					{
						Problem($"{where}unknown field '{p.Name}'");
					}
					char c = g.Name[0];
					glyphs.TryGetValue(c, out GlyphMetrics? grid);
					int[]? rect = Ints(g.Value, "rect", where, 4, 0, 65535);
					if (rect is null && grid is null)
					{
						Problem($"{where}needs 'rect' [x, y, width, height]: it is in no row of cells");
						continue;
					}
					if (rect is not null && (rect[2] == 0 || rect[3] == 0 || !Inside(rect[0], rect[1], rect[2], rect[3])))
					{
						Problem($"{where}rect {string.Join(", ", rect)} is empty or reaches past the {imageWidth}x{imageHeight} sheet");
						continue;
					}
					int[] offset = Ints(g.Value, "offset", where, 2, -256, 256) ?? [grid?.OffsetX ?? 0, grid?.OffsetY ?? 0];
					(int x, int y, int w, int h) = rect is null ? (grid!.X, grid.Y, grid.Width, grid.Height) : (rect[0], rect[1], rect[2], rect[3]);
					int own = Int(g.Value, "advance", where, rect is null ? grid!.Advance : advance >= 0 ? advance : w, 0, 1024);
					glyphs[c] = new GlyphMetrics(x, y, w, h, offset[0], offset[1], own);
				}
			}
		}
		if (glyphs.Count == 0)
		{
			Problem("no glyphs: give 'cell' and 'rows', or 'glyphs' with a 'rect' each");
		}
		var kerning = new Dictionary<(char, char), int>();
		if (root.TryGetProperty("kerning", out JsonElement pairs))
		{
			if (pairs.ValueKind != JsonValueKind.Object)
			{
				Problem("'kerning' must be an object of two characters to the pixels the second moves");
			}
			else
			{
				foreach (JsonProperty pair in pairs.EnumerateObject())
				{
					if (pair.Name.Length != 2 || !glyphs.ContainsKey(pair.Name[0]) || !glyphs.ContainsKey(pair.Name[1]))
					{
						Problem($"kerning.{pair.Name}: a pair is two characters of the font's glyphs");
					}
					else if (pair.Value.ValueKind != JsonValueKind.Number || !pair.Value.TryGetInt32(out int k) || k < -64 || k > 64)
					{
						Problem($"kerning.{pair.Name}: must be a whole number of pixels from -64 to 64");
					}
					else
					{
						kerning[(pair.Name[0], pair.Name[1])] = k;
					}
				}
			}
		}
		return problems > 0 ? null : new FontMetrics(size, lineHeight, baseline, space, glyphs, kerning);
	}
}

// A bitmap font loaded: each glyph cut out of the sheet as its own image, drawn at Factor(size) screen pixels a file
// pixel. Characters it has no glyph for are drawn in the fallback font, the UI skin's for the language or the system's.
public sealed class BitmapFont(string key, FontMetrics metrics, float scale, IReadOnlyDictionary<char, int> images)
{
	// The glyph sheet's path, which probes and the recording surface's digest name the font by.
	public string Key => key;
	public FontMetrics Metrics => metrics;

	public bool Has(char c) => images.ContainsKey(c);

	// Screen pixels a file pixel at a text size: the size times the entry's scale over the metrics' size, snapped as
	// figures are, so every file pixel covers the same whole number of screen pixels.
	public float Factor(int size) => Art.Snap(size * scale / metrics.Size);

	public int LineStep(int size) => Math.Max(1, (int)MathF.Round(metrics.LineHeight * Factor(size)));

	// Lays out one line: `glyph` gets each glyph's image and screen rectangle from the pen at x 0 and the line's top at y
	// 0, `fallback` each run of characters without a glyph with its x and the y that puts the fallback font's baseline
	// near the font's. Returns the width, which `measure` (the fallback font's) gives for the runs.
	public int Layout(string text, int size, Func<string, int> measure, Action<int, int, int, int, int>? glyph = null, Action<string, int, int>? fallback = null)
	{
		float k = Factor(size);
		float pen = 0;
		int start = -1;
		char previous = '\0';
		int fallbackY = (int)MathF.Round(metrics.Baseline * k) - (int)MathF.Round(size * 0.8f);
		void Flush(int end)
		{
			if (start < 0)
			{
				return;
			}
			string run = text[start..end];
			fallback?.Invoke(run, (int)MathF.Round(pen), fallbackY);
			pen += measure(run);
			start = -1;
		}
		for (int i = 0; i < text.Length; i++)
		{
			char c = text[i];
			if (metrics.Glyphs.TryGetValue(c, out GlyphMetrics? g))
			{
				Flush(i);
				if (metrics.Kerning.TryGetValue((previous, c), out int kern))
				{
					pen += kern * k;
				}
				glyph?.Invoke(images[c], (int)MathF.Round(pen + g.OffsetX * k), (int)MathF.Round(g.OffsetY * k), (int)MathF.Round(g.Width * k), (int)MathF.Round(g.Height * k));
				pen += g.Advance * k;
				previous = c;
			}
			else if (c == ' ')
			{
				Flush(i);
				pen += metrics.Space * k;
				previous = c;
			}
			else
			{
				if (start < 0)
				{
					start = i;
				}
				previous = '\0';
			}
		}
		Flush(text.Length);
		return (int)MathF.Round(pen);
	}

	// The characters of `text` other than spaces and line breaks that the font has no glyph for, each once, in order.
	public string Missing(string text) => new([.. text.Where(c => c is not (' ' or '\n' or '\r' or '\t') && !Has(c)).Distinct()]);

	public override string ToString() => $"{key}@{scale.ToString(CultureInfo.InvariantCulture)}";
}

// The placeholder bitmap font the export writes for each language: printable ASCII drawn from a 5 by 9 pixel table, and
// every other character the language's text tables use as a pattern of strokes from its code point (in a 9 by 9 box
// for East Asian wide characters), so each is told apart and takes its own width, though only ASCII reads as letters.
public static class GeneratedFont
{
	private const int Cell = 10, Columns = 32, Advance = 6, WideAdvance = 10;
	private const string Digits = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

	// Printable ASCII from the space, nine rows each from the top: a base-32 digit per row, the left pixel its highest of five
	// bits. Rows 0 to 6 stand on the baseline, 7 and 8 hang below it.
	private static readonly string[] Ascii =
	[
		"000000000", "444440400", "AA0000000", "AAVAVAA00", "4FKE5U400", "OP248J300", "CIK8LID00", "440000000",
		"248884200", "842224800", "04LEL4000", "044V44000", "000004480", "000V00000", "00000CC00", "11248GG00",
		"EHJLPHE00", "4C4444E00", "EH1248V00", "U11E11U00", "26AIV2200", "VGU11HE00", "68GUHHE00", "V12488800",
		"EHHEHHE00", "EHHF12C00", "0CC0CC000", "0CC0CC480", "248G84200", "00V0V0000", "842124800", "EH1240400",
		"EHNLNGE00", "EHHVHHH00", "UHHUHHU00", "EHGGGHE00", "SIHHHIS00", "VGGUGGV00", "VGGUGGG00", "EHGNHHF00",
		"HHHVHHH00", "E44444E00", "72222IC00", "HIKOKIH00", "GGGGGGV00", "HRLLHHH00", "HHPLJHH00", "EHHHHHE00",
		"UHHUGGG00", "EHHHLID00", "UHHUKIH00", "FGGE11U00", "V44444400", "HHHHHHE00", "HHHHHA400", "HHHLLLA00",
		"HHA4AHH00", "HHA444400", "V1248GV00", "E88888E00", "GG8421100", "E22222E00", "4AH000000", "0000000V0",
		"840000000", "00E1FHF00", "GGUHHHU00", "00EGGHE00", "11FHHHF00", "00EHVGE00", "698S88800", "00FHHF1E0",
		"GGMPHHH00", "40C444E00", "206222IC0", "GGIKOKI00", "C44444E00", "00QLLHH00", "00MPHHH00", "00EHHHE00",
		"00UHHUGG0", "00FHHF110", "00MPGGG00", "00FGE1U00", "88S889600", "00HHHJD00", "00HHHA400", "00HHLLA00",
		"00HA4AH00", "00HHHF1E0", "00V248V00", "244844200", "444444400", "844244800", "008L20000",
	];

	private static readonly string[] KerningPairs = ["AV", "VA", "AT", "TA", "LT", "To", "Ta", "Te", "Yo", "WA", "AW"];

	public static bool Wide(char c) => c is >= '\u1100' and <= '\u115F' or >= '\u2E80' and <= '\uA4CF' or >= '\uAC00' and <= '\uD7A3' or >= '\uF900' and <= '\uFAFF'
		or >= '\uFE30' and <= '\uFE4F' or >= '\uFF00' and <= '\uFF60' or >= '\uFFE0' and <= '\uFFE6';

	// The glyph sheet, white on transparent, and its metrics file's content, for printable ASCII and `chars`.
	public static (int Width, int Height, Color[] Pixels, JsonObject Metrics) Build(IEnumerable<char> chars)
	{
		char[] list = [.. Enumerable.Range(0x21, 0x5E).Select(c => (char)c).Concat(chars).Where(c => !char.IsWhiteSpace(c) || c == '\u3000').Distinct().Order()];
		int rows = (list.Length + Columns - 1) / Columns, width = Columns * Cell, height = rows * Cell;
		var pixels = new Color[width * height];
		for (int i = 0; i < list.Length; i++)
		{
			Draw(pixels, width, i % Columns * Cell, i / Columns * Cell, list[i]);
		}
		var metrics = new JsonObject
		{
			["size"] = Cell, ["line_height"] = Cell, ["baseline"] = 8, ["space"] = 4, ["advance"] = Advance,
			["cell"] = new JsonArray(Cell, Cell),
			["rows"] = new JsonArray([.. list.Chunk(Columns).Select(r => (JsonNode?)new string(r))]),
			["glyphs"] = new JsonObject(list.Where(Wide).Select(c => KeyValuePair.Create(c.ToString(), (JsonNode?)new JsonObject { ["advance"] = WideAdvance }))),
			["kerning"] = new JsonObject(KerningPairs.Select(p => KeyValuePair.Create(p, (JsonNode?)(-1)))),
		};
		return (width, height, pixels, metrics);
	}

	private static void Draw(Color[] pixels, int stride, int x0, int y0, char c)
	{
		void Dot(int x, int y) => pixels[(y0 + y) * stride + x0 + x] = Color.White;
		if (c is >= '!' and <= '~')
		{
			string rows = Ascii[c - ' '];
			for (int y = 0; y < 9; y++)
			{
				int bits = Digits.IndexOf(rows[y]);
				for (int x = 0; x < 5; x++)
				{
					if ((bits >> (4 - x) & 1) != 0)
					{
						Dot(x, y + 1);
					}
				}
			}
			return;
		}
		if (c == '\u2026')
		{
			Dot(0, 7);
			Dot(2, 7);
			Dot(4, 7);
			return;
		}
		uint h = Noise.Hash(c, 31, 7);
		int size = Wide(c) ? 9 : 5, top = Wide(c) ? 0 : 1;
		// Two strokes across and two down, each at a place and of a length from the hash, so neighbors differ.
		for (int k = 0; k < 2; k++)
		{
			int row = top + 1 + (int)(h >> (k * 6) & 7) % (size - 1), from = (int)(h >> (k * 6 + 3) & 3) % 3;
			for (int x = from; x < size - (int)(h >> (k * 4 + 12) & 1); x++)
			{
				Dot(x, row);
			}
			int column = 1 + (int)(h >> (k * 5 + 16) & 7) % (size - 2), start = (int)(h >> (k * 3 + 26) & 1) * 2;
			for (int y = top + start; y < top + size + (Wide(c) ? 0 : 2); y++)
			{
				Dot(column, y);
			}
		}
	}
}
