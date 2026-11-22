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
	// Until Unclip, nothing is drawn outside this rectangle; panels draw the scene through one.
	void Clip(int x, int y, int w, int h);
	void Unclip();
	// A PNG from the asset directory as premultiplied pixels, decoded by the device where there is one.
	Color[] ReadImage(string path, out int width, out int height);
	// Drops every image, so a reloaded set of assets starts from handle 0.
	void ClearImages();
	// Text in this font file, every size scaled, from now on; null goes back to the system font.
	void UseFont(string? file, float scale);
	// Text in this bitmap font from now on, its missing glyphs in the font UseFont set; null goes back to that font alone.
	void UseBitmapFont(BitmapFont? font);
	// A picture mirrored left to right; a tint with alpha 0 draws it additively (the pixels are premultiplied).
	void ImageEx(int image, int x, int y, int w, int h, Color tint, bool flipX) => Image(image, x, y, w, h, tint);
	// A picture made from another's pixels (width, height, premultiplied), made once per key; a surface that keeps no
	// pixels returns the picture itself.
	int Derive(int image, string key, Func<Color[], int, int, Color[]> make) => image;
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

	// The asset directory's UI skin while one is loaded and enabled; null draws the generated look with rectangles.
	public static UiSkin? Skin { get; set; }

	// The off half of the layout's cursor blink (timings.cursor_blink_ms), set each frame.
	public static bool PointerHidden { get; set; }

	// Corners at their size times the slice's scale, edges and middle stretched between them.
	public static void NineSlice(IDrawSurface s, NineSliceImage n, int x, int y, int w, int h, Color tint)
	{
		int b = Math.Min(n.Border * n.Scale, Math.Min(w, h) / 2);
		int[] xs = [x, x + b, x + w - b, x + w], ys = [y, y + b, y + h - b, y + h];
		for (int j = 0; j < 3; j++)
		{
			for (int i = 0; i < 3; i++)
			{
				s.Image(n.Images[j * 3 + i], xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], tint);
			}
		}
	}

	// A window: drop shadow, a light border with clipped corners, a dark body with a lighter top band.
	public static void Box(IDrawSurface s, int x, int y, int w, int h, float opacity = 1f)
	{
		if (Skin?.Frame is { } frame)
		{
			NineSlice(s, frame, x, y, w, h, Color.White * opacity);
			return;
		}
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
		if (Skin is { BarTrack: int track, BarFill: int fill })
		{
			s.Image(track, x, y, w, h, Color.White);
			if (max > 0 && value > 0)
			{
				s.Image(fill, x, y, Math.Max(1, w * Math.Min(value, max) / max), h, color);
			}
			return;
		}
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
		if (PointerHidden)
		{
			return;
		}
		if (Skin?.Cursor is int cursor)
		{
			s.Image(cursor, x, y, size, size, Color.White);
			return;
		}
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

	public static List<string> Wrap(IDrawSurface s, string text, int width, int size = Body) =>
		[.. WrapSpans(s, text, width, size).Select(span => text.Substring(span.Start, span.Length))];

	// The lines as spans of the text: broken at spaces, which no line keeps, and inside a word wider than the line (text
	// without spaces, such as Japanese) between characters, keeping closing punctuation on the line it closes.
	public static List<(int Start, int Length)> WrapSpans(IDrawSurface s, string text, int width, int size = Body)
	{
		var lines = new List<(int, int)>();
		int at = 0;
		foreach (string paragraph in text.Split('\n'))
		{
			int lineStart = at, lineEnd = at;
			int wordStart = at;
			foreach (string word in paragraph.Split(' '))
			{
				if (lineEnd == lineStart)
				{
					lineStart = wordStart;
				}
				else if (s.Measure(text[lineStart..(wordStart + word.Length)], size) > width)
				{
					lines.Add((lineStart, lineEnd - lineStart));
					lineStart = wordStart;
				}
				// A word wider than the line on its own is cut where it stops fitting.
				while (wordStart + word.Length > lineStart && s.Measure(text[lineStart..(wordStart + word.Length)], size) > width)
				{
					int cut = lineStart + 1;
					while (cut < wordStart + word.Length && s.Measure(text[lineStart..(cut + 1)], size) <= width)
					{
						cut++;
					}
					while (cut < wordStart + word.Length && Closing.Contains(text[cut]))
					{
						cut++;
					}
					if (cut >= wordStart + word.Length)
					{
						break;
					}
					lines.Add((lineStart, cut - lineStart));
					lineStart = cut;
				}
				lineEnd = wordStart + word.Length;
				wordStart = lineEnd + 1;
			}
			lines.Add((lineStart, lineEnd - lineStart));
			at += paragraph.Length + 1;
		}
		return lines;
	}

	private const string Closing = "、。，．・：；？！ー）」』】〉》〕｝,.!?:;)]}";
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
	public void Clip(int x, int y, int w, int h) => inner.Clip(x, y, w, h);
	public void Unclip() => inner.Unclip();
	public Color[] ReadImage(string path, out int width, out int height) => inner.ReadImage(path, out width, out height);
	public void ClearImages() => inner.ClearImages();
	public void UseFont(string? file, float scale) => inner.UseFont(file, scale);
	public void UseBitmapFont(BitmapFont? font) => inner.UseBitmapFont(font);
	public void ImageEx(int image, int x, int y, int w, int h, Color tint, bool flipX) => inner.ImageEx(image, x, y, w, h, tint, flipX);
	public int Derive(int image, string key, Func<Color[], int, int, Color[]> make) => inner.Derive(image, key, make);
}

// Another surface seen as a frame `zoom` times its size: every position and size drawn is multiplied by it, so drawing
// laid out for a large frame lands smaller on a small one (the developer tools on a small logical resolution). Text sizes
// too, unless `text` is false: a bitmap font already draws at its own scale.
public sealed class ZoomSurface(IDrawSurface inner, float zoom, bool text = true) : IDrawSurface
{
	private int Z(int v) => (int)MathF.Round(v * zoom);

	private int T(int size) => text ? Math.Max(1, Z(size)) : size;

	public IDrawSurface Inner => inner;
	public float Zoom => zoom;

	public int Width => (int)MathF.Round(inner.Width / zoom);
	public int Height => (int)MathF.Round(inner.Height / zoom);
	public void Fill(int x, int y, int w, int h, Color color) => inner.Fill(Z(x), Z(y), Math.Max(1, Z(w)), Math.Max(1, Z(h)), color);
	public void Text(string text, int x, int y, Color color, int size = Style.Body) => inner.Text(text, Z(x), Z(y), color, T(size));
	public int Measure(string text, int size = Style.Body) => (int)MathF.Round(inner.Measure(text, T(size)) / zoom);
	public int CreateImage(int width, int height, Color[] pixels) => inner.CreateImage(width, height, pixels);
	public void Image(int image, int x, int y, int w, int h, Color tint) => inner.Image(image, Z(x), Z(y), Z(w), Z(h), tint);
	public void Clip(int x, int y, int w, int h) => inner.Clip(Z(x), Z(y), Z(w), Z(h));
	public void Unclip() => inner.Unclip();
	public Color[] ReadImage(string path, out int width, out int height) => inner.ReadImage(path, out width, out height);
	public void ClearImages() => inner.ClearImages();
	public void UseFont(string? file, float scale) => inner.UseFont(file, scale);
	public void UseBitmapFont(BitmapFont? font) => inner.UseBitmapFont(font);
	public void ImageEx(int image, int x, int y, int w, int h, Color tint, bool flipX) => inner.ImageEx(image, Z(x), Z(y), Z(w), Z(h), tint, flipX);
	public int Derive(int image, string key, Func<Color[], int, int, Color[]> make) => inner.Derive(image, key, make);
}

public sealed class MonoGameSurface(GraphicsDevice device, SpriteBatch batch, FontSystem fonts) : IDrawSurface, IDisposable
{
	private static readonly RasterizerState Scissor = new() { CullMode = CullMode.None, ScissorTestEnable = true };
	private readonly Texture2D _pixel = CreatePixel(device);
	private readonly List<Texture2D> _images = [];
	private readonly Dictionary<string, FontSystem> _fontFiles = [];
	private readonly FontSystem _system = fonts;
	private FontSystem _font = fonts;
	private float _fontScale = 1f;
	private BitmapFont? _bitmap;

	private static Texture2D CreatePixel(GraphicsDevice device)
	{
		var pixel = new Texture2D(device, 1, 1);
		pixel.SetData([Color.White]);
		return pixel;
	}

	public int Width => device.Viewport.Width;
	public int Height => device.Viewport.Height;

	public void Fill(int x, int y, int w, int h, Color color) => batch.Draw(_pixel, new Rectangle(x, y, w, h), color);

	public void Text(string text, int x, int y, Color color, int size = Style.Body)
	{
		if (_bitmap is null)
		{
			_font.GetFont(FontSize(size)).DrawText(batch, text, new Vector2(x, y), color);
			return;
		}
		_bitmap.Layout(text, size, run => FallbackWidth(run, size), (image, gx, gy, w, h) => Image(image, x + gx, y + gy, w, h, color),
			(run, fx, fy) => _font.GetFont(FontSize(size)).DrawText(batch, run, new Vector2(x + fx, y + fy), color));
	}

	public int Measure(string text, int size = Style.Body) => _bitmap is null ? FallbackWidth(text, size) : _bitmap.Layout(text, size, run => FallbackWidth(run, size));

	private int FallbackWidth(string text, int size) => (int)_font.GetFont(FontSize(size)).MeasureString(text).X;

	public void UseBitmapFont(BitmapFont? font) => _bitmap = font;

	private int FontSize(int size) => Math.Max(1, (int)MathF.Round(size * _fontScale));

	// The file first, the system faces after it for the glyphs it lacks.
	public void UseFont(string? file, float scale)
	{
		_fontScale = file is null ? 1f : scale;
		if (file is null)
		{
			_font = _system;
			return;
		}
		if (!_fontFiles.TryGetValue(file, out FontSystem? system))
		{
			system = new FontSystem();
			system.AddFont(File.ReadAllBytes(file));
			foreach (string fallback in Fonts.Candidates().Where(File.Exists).Distinct())
			{
				try
				{
					system.AddFont(File.ReadAllBytes(fallback));
				}
				catch (Exception)
				{
					// As in Fonts.Load: a face the rasterizer cannot read is left out.
				}
			}
			_fontFiles[file] = system;
		}
		_font = system;
	}

	// Through Texture2D.FromStream, premultiplied the way Png.Read does it so a picture is the same with and without a device.
	public Color[] ReadImage(string path, out int width, out int height)
	{
		using FileStream file = File.OpenRead(path);
		using Texture2D texture = Texture2D.FromStream(device, file, rgba =>
		{
			for (int i = 0; i < rgba.Length; i += 4)
			{
				int a = rgba[i + 3];
				rgba[i] = (byte)((rgba[i] * a + 127) / 255);
				rgba[i + 1] = (byte)((rgba[i + 1] * a + 127) / 255);
				rgba[i + 2] = (byte)((rgba[i + 2] * a + 127) / 255);
			}
		});
		width = texture.Width;
		height = texture.Height;
		var pixels = new Color[width * height];
		texture.GetData(pixels);
		return pixels;
	}

	public void ClearImages()
	{
		foreach (Texture2D texture in _images)
		{
			texture.Dispose();
		}
		_images.Clear();
		_derived.Clear();
	}

	public int CreateImage(int width, int height, Color[] pixels)
	{
		var texture = new Texture2D(device, width, height);
		texture.SetData(pixels);
		_images.Add(texture);
		return _images.Count - 1;
	}

	public void Image(int image, int x, int y, int w, int h, Color tint) => batch.Draw(_images[image], new Rectangle(x, y, w, h), tint);

	public void ImageEx(int image, int x, int y, int w, int h, Color tint, bool flipX) =>
		batch.Draw(_images[image], new Rectangle(x, y, w, h), null, tint, 0f, Vector2.Zero, flipX ? SpriteEffects.FlipHorizontally : SpriteEffects.None, 0f);

	private readonly Dictionary<(int, string), int> _derived = [];

	public int Derive(int image, string key, Func<Color[], int, int, Color[]> make)
	{
		if (!_derived.TryGetValue((image, key), out int made))
		{
			Texture2D source = _images[image];
			var pixels = new Color[source.Width * source.Height];
			source.GetData(pixels);
			made = _derived[(image, key)] = CreateImage(source.Width, source.Height, make(pixels, source.Width, source.Height));
		}
		return made;
	}

	// The scissor rectangle applies to a whole batch, so a clip ends the batch drawn so far and starts another.
	public void Clip(int x, int y, int w, int h)
	{
		batch.End();
		device.ScissorRectangle = Rectangle.Intersect(new Rectangle(x, y, w, h), device.Viewport.Bounds);
		batch.Begin(samplerState: SamplerState.PointClamp, rasterizerState: Scissor);
	}

	public void Unclip()
	{
		batch.End();
		batch.Begin(samplerState: SamplerState.PointClamp);
	}

	public void Dispose()
	{
		ClearImages();
		foreach (FontSystem system in _fontFiles.Values)
		{
			system.Dispose();
		}
		_pixel.Dispose();
	}
}

// Headless: draws nothing, keeps the last frame's text so a script can print it, and counts calls. Each frame's draws
// also fold into a digest, with every image known by a hash of its pixels rather than its handle, so two runs that draw
// the same pictures in the same places give the same digests whether the pictures came from code or from files.
public sealed class RecordingSurface : IDrawSurface
{
	// The layout's logical resolution, set by the headless runner each frame.
	public int Width { get; set; } = 1280;
	public int Height { get; set; } = 720;
	public int Rects { get; private set; }
	public int Texts { get; private set; }
	public int Images { get; private set; }
	private readonly List<ulong> _contents = [];
	private readonly List<(int W, int H, Color[] Pixels)> _pixels = [];
	private readonly Dictionary<(int, string), int> _derived = [];
	private bool _clipped;
	private float _fontScale = 1f;
	private ulong _digest;
	public List<string> FrameText { get; } = [];
	public List<ulong> Digests { get; } = [];

	public void BeginFrame()
	{
		FrameText.Clear();
		_ops.Clear();
		_digest = 14695981039346656037ul;
	}

	// The frame's fills and pictures in order, each with the clip it was drawn under, for PixelAt. Text is left out.
	private readonly List<(int Image, Rectangle Rect, Color Color, bool FlipX, Rectangle? Clip)> _ops = [];
	private Rectangle? _clip;

	// The color the last frame left at a pixel of the logical frame, as the window would draw it without its text:
	// every fill and picture over it blended in order, pictures sampled nearest-neighbor and tinted. `#rrggbb`.
	// A picture's pixel, as made.
	public Color ImagePixel(int image, int x, int y) => _pixels[image].Pixels[y * _pixels[image].W + x];

	public string PixelAt(int x, int y)
	{
		Color d = Color.Transparent;
		foreach ((int image, Rectangle rect, Color color, bool flipX, Rectangle? clip) in _ops)
		{
			if (!rect.Contains(x, y) || clip is { } c && !c.Contains(x, y))
			{
				continue;
			}
			Color src = color;
			if (image >= 0)
			{
				(int iw, int ih, Color[] pixels) = _pixels[image];
				int u = (x - rect.X) * iw / rect.Width, v = (y - rect.Y) * ih / rect.Height;
				Color p = pixels[v * iw + (flipX ? iw - 1 - u : u)];
				src = new Color(p.R * color.R / 255, p.G * color.G / 255, p.B * color.B / 255, p.A * color.A / 255);
			}
			int keep = 255 - src.A;
			d = new Color(src.R + d.R * keep / 255, src.G + d.G * keep / 255, src.B + d.B * keep / 255, src.A + d.A * keep / 255);
		}
		return $"#{d.R:x2}{d.G:x2}{d.B:x2}";
	}

	public void EndFrame() => Digests.Add(_digest);

	private void Mix(params long[] values)
	{
		foreach (long v in values)
		{
			_digest = (_digest ^ (ulong)v) * 1099511628211ul;
		}
	}

	private void Mix(string text)
	{
		foreach (char c in text)
		{
			Mix(c);
		}
	}

	public void Fill(int x, int y, int w, int h, Color color)
	{
		_ops.Add((-1, new Rectangle(x, y, w, h), color, false, _clip));
		Rects++;
		Mix(1, x, y, w, h, color.PackedValue);
	}

	public void Text(string text, int x, int y, Color color, int size = Style.Body)
	{
		Texts++;
		FrameText.Add(text);
		Mix(2, x, y, color.PackedValue, size);
		Mix(text);
		// A bitmap font's glyphs are pictures: each folds in as an image, its fallback runs as their position and text.
		_bitmap?.Layout(text, size, run => FallbackWidth(run, size), (image, gx, gy, w, h) => Image(image, x + gx, y + gy, w, h, color), (run, fx, fy) =>
		{
			Mix(8, x + fx, y + fy);
			Mix(run);
		});
	}

	public int Measure(string text, int size = Style.Body) => _bitmap is null ? FallbackWidth(text, size) : _bitmap.Layout(text, size, run => FallbackWidth(run, size));

	private int FallbackWidth(string text, int size) => text.Length * (int)MathF.Round(size * _fontScale) * 11 / 20;

	public int CreateImage(int width, int height, Color[] pixels)
	{
		if (pixels.Length != width * height)
		{
			throw new ArgumentException($"{pixels.Length} pixels for a {width}x{height} image");
		}
		ulong h = 14695981039346656037ul;
		foreach (uint v in pixels.Select(p => p.PackedValue).Prepend((uint)height).Prepend((uint)width))
		{
			h = (h ^ v) * 1099511628211ul;
		}
		_contents.Add(h);
		_pixels.Add((width, height, pixels));
		return _contents.Count - 1;
	}

	public void ImageEx(int image, int x, int y, int w, int h, Color tint, bool flipX)
	{
		if (image < 0 || image >= _contents.Count)
		{
			throw new ArgumentOutOfRangeException(nameof(image));
		}
		Images++;
		_ops.Add((image, new Rectangle(x, y, w, h), tint, flipX, _clip));
		Mix(9, (long)_contents[image], x, y, w, h, tint.PackedValue, flipX ? 1 : 0);
	}

	public int Derive(int image, string key, Func<Color[], int, int, Color[]> make)
	{
		if (!_derived.TryGetValue((image, key), out int made))
		{
			(int w, int h, Color[] pixels) = _pixels[image];
			made = _derived[(image, key)] = CreateImage(w, h, make(pixels, w, h));
		}
		return made;
	}

	public void Image(int image, int x, int y, int w, int h, Color tint)
	{
		if (image < 0 || image >= _contents.Count)
		{
			throw new ArgumentOutOfRangeException(nameof(image));
		}
		Images++;
		_ops.Add((image, new Rectangle(x, y, w, h), tint, false, _clip));
		Mix(3, (long)_contents[image], x, y, w, h, tint.PackedValue);
	}

	public void Clip(int x, int y, int w, int h)
	{
		if (_clipped || w <= 0 || h <= 0)
		{
			throw new InvalidOperationException($"a clip of {w}x{h} inside another clip, or an empty one");
		}
		_clipped = true;
		_clip = new Rectangle(x, y, w, h);
		Mix(4, x, y, w, h);
	}

	public void Unclip()
	{
		if (!_clipped)
		{
			throw new InvalidOperationException("an unclip without a clip");
		}
		_clipped = false;
		_clip = null;
		Mix(5);
	}

	public Color[] ReadImage(string path, out int width, out int height) => Png.Read(path, out width, out height);

	public void ClearImages()
	{
		_contents.Clear();
		_pixels.Clear();
		_derived.Clear();
	}

	public void UseFont(string? file, float scale)
	{
		_fontScale = file is null ? 1f : scale;
		Mix(6, (long)(_fontScale * 1000));
		Mix(file ?? "");
	}

	private BitmapFont? _bitmap;

	public void UseBitmapFont(BitmapFont? font)
	{
		if (font == _bitmap)
		{
			return;
		}
		_bitmap = font;
		Mix(7);
		Mix(font?.ToString() ?? "");
	}
}

// Draws into a pixel buffer, for pictures the sample builds from rectangles and other pictures (the placeholder export's
// window frame, cursor and bars, and the battle backdrops). Text is left out.
public sealed class CanvasSurface(int width, int height) : IDrawSurface
{
	private readonly List<(int W, int H, Color[] Pixels)> _images = [];
	private Rectangle _clip = new(0, 0, width, height);

	public int Width => width;
	public int Height => height;
	public Color[] Pixels { get; } = new Color[width * height];

	private void Blend(int x, int y, Color c)
	{
		if (!_clip.Contains(x, y) || c.A == 0)
		{
			return;
		}
		ref Color d = ref Pixels[y * width + x];
		int keep = 255 - c.A;
		d = new Color(c.R + d.R * keep / 255, c.G + d.G * keep / 255, c.B + d.B * keep / 255, c.A + d.A * keep / 255);
	}

	public void Fill(int x, int y, int w, int h, Color color)
	{
		for (int j = Math.Max(y, 0); j < Math.Min(y + h, height); j++)
		{
			for (int i = Math.Max(x, 0); i < Math.Min(x + w, width); i++)
			{
				Blend(i, j, color);
			}
		}
	}

	public void Text(string text, int x, int y, Color color, int size = Style.Body)
	{
	}

	public int Measure(string text, int size = Style.Body) => 0;

	public int CreateImage(int w, int h, Color[] pixels)
	{
		_images.Add((w, h, pixels));
		return _images.Count - 1;
	}

	// Nearest neighbor, tinted as SpriteBatch tints premultiplied pixels.
	public void Image(int image, int x, int y, int w, int h, Color tint)
	{
		(int iw, int ih, Color[] src) = _images[image];
		for (int j = Math.Max(y, 0); j < Math.Min(y + h, height); j++)
		{
			for (int i = Math.Max(x, 0); i < Math.Min(x + w, width); i++)
			{
				Color c = src[(j - y) * ih / h * iw + (i - x) * iw / w];
				Blend(i, j, new Color(c.R * tint.R / 255, c.G * tint.G / 255, c.B * tint.B / 255, c.A * tint.A / 255));
			}
		}
	}

	public void Clip(int x, int y, int w, int h) => _clip = Rectangle.Intersect(new Rectangle(x, y, w, h), new Rectangle(0, 0, width, height));

	public void Unclip() => _clip = new Rectangle(0, 0, width, height);

	public (int W, int H, Color[] Pixels) ImageData(int image) => _images[image];

	public Color[] ReadImage(string path, out int w, out int h) => Png.Read(path, out w, out h);

	public void ClearImages() => _images.Clear();

	public void UseFont(string? file, float scale)
	{
	}

	public void UseBitmapFont(BitmapFont? font)
	{
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
			// The Hiragino collection below draws no Japanese glyphs through FontStashSharp on macOS; Arial Unicode does.
			"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
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
