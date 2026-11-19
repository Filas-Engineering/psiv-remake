using FontStashSharp;

using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace Psiv.Sample;

// Everything the sample draws goes through this: flat rectangles and text. Replace it to put real
// rendering behind the same screens, or replace the screens and keep only the session calls.
public interface IDrawSurface
{
	int Width { get; }
	int Height { get; }
	void Fill(int x, int y, int w, int h, Color color);
	void Text(string text, int x, int y, Color color, int size = Style.Body);
	int Measure(string text, int size = Style.Body);
}

public static class Style
{
	public const int Small = 16;
	public const int Body = 20;
	public const int Title = 28;
	public const int LineHeight = 26;

	public static readonly Color Background = new(16, 18, 24);
	public static readonly Color Panel = new(30, 34, 46);
	public static readonly Color PanelEdge = new(90, 100, 130);
	public static readonly Color Text = new(230, 230, 220);
	public static readonly Color Dim = new(140, 140, 150);
	public static readonly Color Accent = new(250, 210, 90);
	public static readonly Color Bad = new(230, 90, 80);
	public static readonly Color Good = new(110, 210, 120);

	public static readonly Color Wall = new(60, 60, 70);
	public static readonly Color Floor = new(150, 130, 100);
	public static readonly Color Water = new(50, 90, 170);
	public static readonly Color Exit = new(80, 200, 110);
	public static readonly Color Talk = new(210, 90, 200);
	public static readonly Color Step = new(190, 170, 220);
	public static readonly Color Chest = new(230, 190, 40);
	public static readonly Color ChestOpen = new(120, 100, 50);
	public static readonly Color Shop = new(60, 200, 220);
	public static readonly Color Rest = new(140, 100, 230);
	public static readonly Color Enemy = new(220, 60, 50);
	public static readonly Color EnemyChasing = new(255, 140, 30);
	public static readonly Color Leader = new(255, 255, 255);

	public static void Box(IDrawSurface s, int x, int y, int w, int h)
	{
		s.Fill(x, y, w, h, PanelEdge);
		s.Fill(x + 2, y + 2, w - 4, h - 4, Panel);
	}

	public static void Bar(IDrawSurface s, int x, int y, int w, int h, int value, int max, Color color)
	{
		s.Fill(x, y, w, h, Wall);
		if (max > 0 && value > 0)
		{
			s.Fill(x, y, Math.Max(1, w * Math.Min(value, max) / max), h, color);
		}
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

public sealed class MonoGameSurface(GraphicsDevice device, SpriteBatch batch, FontSystem fonts) : IDrawSurface
{
	private readonly Texture2D _pixel = CreatePixel(device);

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
}

// Headless: draws nothing, keeps the last frame's text so a script can print it, and counts calls.
public sealed class RecordingSurface : IDrawSurface
{
	public int Width => 1280;
	public int Height => 720;
	public int Rects { get; private set; }
	public int Texts { get; private set; }
	public List<string> FrameText { get; } = [];

	public void BeginFrame() => FrameText.Clear();

	public void Fill(int x, int y, int w, int h, Color color) => Rects++;

	public void Text(string text, int x, int y, Color color, int size = Style.Body)
	{
		Texts++;
		FrameText.Add(text);
	}

	public int Measure(string text, int size = Style.Body) => text.Length * size * 11 / 20;
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
