using FontStashSharp;

using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace Psiv.Sample;

// Everything the sample draws goes through this: flat rectangles, pictures and text. Pictures are pixel
// arrays the sample generates itself (Art.cs), handed over once and drawn scaled by handle. Replace it to
// put real rendering behind the same screens, or replace the screens and keep only the session calls.
public interface IDrawSurface
{
	int Width { get; }
	int Height { get; }
	void Fill(int x, int y, int w, int h, Color color);
	void Text(string text, int x, int y, Color color, int size = Style.Body);
	int Measure(string text, int size = Style.Body);
	// Takes width * height pixels, row-major; returns the handle Image draws.
	int CreateImage(int width, int height, Color[] pixels);
	void Image(int image, int x, int y, int w, int h, Color tint);
}

public static class Style
{
	public const int Small = 16;
	public const int Body = 20;
	public const int Title = 28;
	public const int LineHeight = 26;

	public static readonly Color Background = new(14, 16, 26);
	public static readonly Color Panel = new(22, 28, 52);
	public static readonly Color PanelTop = new(34, 44, 78);
	public static readonly Color PanelEdge = new(196, 206, 232);
	public static readonly Color PanelShadow = new(6, 8, 16);
	public static readonly Color Row = new(64, 84, 140);
	public static readonly Color Text = new(236, 236, 226);
	public static readonly Color Dim = new(146, 152, 172);
	public static readonly Color Accent = new(252, 214, 96);
	public static readonly Color Bad = new(236, 96, 84);
	public static readonly Color Good = new(112, 214, 124);
	public static readonly Color Tp = new(96, 168, 248);
	public static readonly Color Track = new(10, 12, 22);

	// A window: drop shadow, a light border with clipped corners, a dark body with a lighter top band.
	public static void Box(IDrawSurface s, int x, int y, int w, int h, float opacity = 1f)
	{
		s.Fill(x + 4, y + 4, w, h, PanelShadow * (0.6f * opacity));
		s.Fill(x + 2, y, w - 4, h, PanelEdge * opacity);
		s.Fill(x, y + 2, w, h - 4, PanelEdge * opacity);
		s.Fill(x + 3, y + 3, w - 6, h - 6, PanelShadow * opacity);
		s.Fill(x + 4, y + 4, w - 8, h - 8, Panel * opacity);
		s.Fill(x + 4, y + 4, w - 8, Math.Min(24, (h - 8) / 3), PanelTop * opacity);
	}

	// A box with a title tab sitting on its top edge.
	public static void Window(IDrawSurface s, int x, int y, int w, int h, string title, float opacity = 1f)
	{
		Box(s, x, y, w, h, opacity);
		if (title.Length > 0)
		{
			Plate(s, title, x + 16, y - 14, Accent);
		}
	}

	// A small framed label, for speaker names and window titles.
	public static void Plate(IDrawSurface s, string text, int x, int y, Color color, int size = Small)
	{
		int w = s.Measure(text, size) + 24;
		int h = (s is ScaledSurface scaled ? (int)MathF.Round(size * scaled.Scale) : size) + 12;
		s.Fill(x + 1, y, w - 2, h, PanelEdge);
		s.Fill(x, y + 1, w, h - 2, PanelEdge);
		s.Fill(x + 2, y + 2, w - 4, h - 4, PanelTop);
		s.Text(text, x + 12, y + 5, color, size);
	}

	public static void Bar(IDrawSurface s, int x, int y, int w, int h, int value, int max, Color color)
	{
		s.Fill(x - 1, y - 1, w + 2, h + 2, PanelShadow);
		s.Fill(x, y, w, h, Track);
		if (max > 0 && value > 0)
		{
			int filled = Math.Max(1, w * Math.Min(value, max) / max);
			s.Fill(x, y, filled, h, color);
			s.Fill(x, y, filled, Math.Max(1, h / 3), Color.Lerp(color, Color.White, 0.45f));
		}
	}

	// A right-pointing cursor, `size` pixels tall, its tip at (x + size / 2, y + size / 2).
	public static void Pointer(IDrawSurface s, int x, int y, Color color, int size = 12)
	{
		for (int i = 0; i < size; i++)
		{
			int len = Math.Max(1, size / 2 - Math.Abs(i - size / 2));
			s.Fill(x, y + i, len, 1, color);
		}
	}

	// One menu row: a highlight bar and pointer under the selected row.
	// `height` is the highlight's, for text drawn larger than `size` through a ScaledSurface.
	public static void ListRow(IDrawSurface s, string text, int x, int y, int width, bool selected, bool enabled = true, int size = Body, int height = 0)
	{
		if (selected)
		{
			height = height > 0 ? height : size + 8;
			s.Fill(x - 6, y - 3, width, height, Row);
			Pointer(s, x - 4, y + (height - 8) / 2 - 6, Accent);
		}
		s.Text(text, x + 14, y, !enabled ? Dim : selected ? Accent : Text, size);
	}

	public static void Frame(IDrawSurface s, int x, int y, int w, int h, Color c, int t = 2)
	{
		s.Fill(x, y, w, t, c);
		s.Fill(x, y + h - t, w, t, c);
		s.Fill(x, y, t, h, c);
		s.Fill(x + w - t, y, t, h, c);
	}

	// Pills for a row of tabs, the selected one underlined.
	public static void Tabs(IDrawSurface s, int x, int y, IReadOnlyList<string> tabs, int selected)
	{
		for (int i = 0; i < tabs.Count; i++)
		{
			int w = s.Measure(tabs[i]) + 28;
			if (i == selected)
			{
				s.Fill(x, y, w, 32, Row);
				s.Fill(x, y + 30, w, 2, Accent);
			}
			s.Text(tabs[i], x + 14, y + 5, i == selected ? Accent : Dim);
			x += w + 8;
		}
	}

	// The text cut to fit `width`, with an ellipsis when cut.
	public static string Fit(IDrawSurface s, string text, int width, int size = Body)
	{
		if (s.Measure(text, size) <= width)
		{
			return text;
		}
		while (text.Length > 0 && s.Measure(text + "...", size) > width)
		{
			text = text[..^1];
		}
		return text + "...";
	}

	public static List<string> Wrap(IDrawSurface s, string text, int width, int size = Body)
	{
		var lines = new List<string>();
		foreach (string paragraph in text.Split('\n'))
		{
			string line = "";
			foreach (string word in paragraph.Split(' '))
			{
				string candidate = line.Length == 0 ? word : line + " " + word;
				if (line.Length > 0 && s.Measure(candidate, size) > width)
				{
					lines.Add(line);
					line = word;
				}
				else
				{
					line = candidate;
				}
			}
			lines.Add(line);
		}
		return lines;
	}
}

// Another surface with every text size scaled, for the profile's text size; the layout scales its own spacing.
public sealed class ScaledSurface(IDrawSurface inner, float scale) : IDrawSurface
{
	private int Size(int size) => (int)MathF.Round(size * scale);

	public IDrawSurface Inner => inner;
	public float Scale => scale;

	public int Width => inner.Width;
	public int Height => inner.Height;
	public void Fill(int x, int y, int w, int h, Color color) => inner.Fill(x, y, w, h, color);
	public void Text(string text, int x, int y, Color color, int size = Style.Body) => inner.Text(text, x, y, color, Size(size));
	public int Measure(string text, int size = Style.Body) => inner.Measure(text, Size(size));
	public int CreateImage(int width, int height, Color[] pixels) => inner.CreateImage(width, height, pixels);
	public void Image(int image, int x, int y, int w, int h, Color tint) => inner.Image(image, x, y, w, h, tint);
}

public sealed class MonoGameSurface(GraphicsDevice device, SpriteBatch batch, FontSystem fonts) : IDrawSurface, IDisposable
{
	private readonly Texture2D _pixel = CreatePixel(device);
	private readonly List<Texture2D> _images = [];

	private static Texture2D CreatePixel(GraphicsDevice device)
	{
		var pixel = new Texture2D(device, 1, 1);
		pixel.SetData([Color.White]);
		return pixel;
	}

	public int Width => device.Viewport.Width;
	public int Height => device.Viewport.Height;

	public void Fill(int x, int y, int w, int h, Color color) => batch.Draw(_pixel, new Rectangle(x, y, w, h), color);

	public void Text(string text, int x, int y, Color color, int size = Style.Body) => fonts.GetFont(size).DrawText(batch, text, new Vector2(x, y), color);

	public int Measure(string text, int size = Style.Body) => (int)fonts.GetFont(size).MeasureString(text).X;

	public int CreateImage(int width, int height, Color[] pixels)
	{
		var texture = new Texture2D(device, width, height);
		texture.SetData(pixels);
		_images.Add(texture);
		return _images.Count - 1;
	}

	public void Image(int image, int x, int y, int w, int h, Color tint) => batch.Draw(_images[image], new Rectangle(x, y, w, h), tint);

	public void Dispose()
	{
		foreach (Texture2D texture in _images)
		{
			texture.Dispose();
		}
		_images.Clear();
		_pixel.Dispose();
	}
}

// Headless: draws nothing, keeps the last frame's text so a script can print it, and counts calls.
public sealed class RecordingSurface : IDrawSurface
{
	public int Width => 1280;
	public int Height => 720;
	public int Rects { get; private set; }
	public int Texts { get; private set; }
	public int Images { get; private set; }
	private int _created;
	public List<string> FrameText { get; } = [];

	public void BeginFrame() => FrameText.Clear();

	public void Fill(int x, int y, int w, int h, Color color) => Rects++;

	public void Text(string text, int x, int y, Color color, int size = Style.Body)
	{
		Texts++;
		FrameText.Add(text);
	}

	public int Measure(string text, int size = Style.Body) => text.Length * size * 11 / 20;

	public int CreateImage(int width, int height, Color[] pixels)
	{
		if (pixels.Length != width * height)
		{
			throw new ArgumentException($"{pixels.Length} pixels for a {width}x{height} image");
		}
		return _created++;
	}

	public void Image(int image, int x, int y, int w, int h, Color tint)
	{
		if (image < 0 || image >= _created)
		{
			throw new ArgumentOutOfRangeException(nameof(image));
		}
		Images++;
	}
}

public static class Fonts
{
	// The first file that exists is the main face; every later one that exists is a fallback for glyphs it
	// lacks (the placeholder Japanese tables, say).
	public static IEnumerable<string> Candidates()
	{
		if (Environment.GetEnvironmentVariable("PSIV_FONT") is { Length: > 0 } custom)
		{
			yield return custom;
		}
		string windows = Environment.GetFolderPath(Environment.SpecialFolder.Fonts);
		string[] all =
		[
			"/System/Library/Fonts/Supplemental/Arial.ttf",
			"/System/Library/Fonts/Supplemental/Verdana.ttf",
			"/System/Library/Fonts/Geneva.ttf",
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/TTF/DejaVuSans.ttf",
			"/usr/share/fonts/dejavu/DejaVuSans.ttf",
			"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
			"/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
			"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
			"/usr/share/fonts/noto/NotoSans-Regular.ttf",
			Path.Combine(windows, "segoeui.ttf"),
			Path.Combine(windows, "arial.ttf"),
			Path.Combine(windows, "tahoma.ttf"),
			"/System/Library/Fonts/Hiragino Sans GB.ttc",
			"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
			"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
			Path.Combine(windows, "msgothic.ttc"),
		];
		foreach (string path in all)
		{
			yield return path;
		}
	}

	public static FontSystem Load(string? explicitPath)
	{
		var system = new FontSystem();
		int loaded = 0;
		IEnumerable<string> paths = explicitPath is null ? Candidates() : Candidates().Prepend(explicitPath);
		foreach (string path in paths.Where(File.Exists).Distinct())
		{
			try
			{
				system.AddFont(File.ReadAllBytes(path));
				loaded++;
			}
			catch (Exception)
			{
				// A face the rasterizer cannot read is skipped; the next candidate stands in.
			}
		}
		if (loaded == 0)
		{
			throw new FileNotFoundException("No system font found. Pass --font PATH or set PSIV_FONT to a .ttf file.");
		}
		return system;
	}
}
