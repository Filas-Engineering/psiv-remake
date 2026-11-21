namespace Psiv.Sample;

// Where the sample's logical frame (1280x720 unless the layout's styles say otherwise) goes in a window of another size,
// centered with bars inside the safe area. `fit`, the default, scales by a whole number when the window has room for it,
// so pixel art stays sharp, and shrinks to fit when the window is smaller; `integer` only ever scales by a whole number,
// at least 1, cropping a window too small for the frame; `stretch` scales by whatever keeps the frame's shape.
public readonly record struct Letterbox(int X, int Y, int Width, int Height, float Scale)
{
	public const int BaseWidth = 1280;
	public const int BaseHeight = 720;

	public static Letterbox Fit(int windowWidth, int windowHeight) => Fit(windowWidth, windowHeight, BaseWidth, BaseHeight, null);

	public static Letterbox Fit(int windowWidth, int windowHeight, int baseWidth, int baseHeight, UiScaling? scaling)
	{
		int left = 0, top = 0, areaW = windowWidth, areaH = windowHeight;
		if (scaling is not null)
		{
			left = (int)(windowWidth * scaling.SafeLeft / 100f);
			top = (int)(windowHeight * scaling.SafeTop / 100f);
			areaW = Math.Max(1, windowWidth - left - (int)(windowWidth * scaling.SafeRight / 100f));
			areaH = Math.Max(1, windowHeight - top - (int)(windowHeight * scaling.SafeBottom / 100f));
		}
		float fit = Math.Min((float)areaW / baseWidth, (float)areaH / baseHeight);
		float scale = scaling?.Mode switch
		{
			"integer" => Math.Max(1f, MathF.Floor(fit)),
			"stretch" => fit,
			_ => fit >= 1f ? MathF.Floor(fit) : fit,
		};
		int w = (int)MathF.Round(baseWidth * scale);
		int h = (int)MathF.Round(baseHeight * scale);
		return new(left + (areaW - w) / 2, top + (areaH - h) / 2, w, h, scale);
	}

	// A window pixel in the frame's own pixels, or null on the bars.
	public (int X, int Y)? ToBase(int windowX, int windowY)
	{
		if (Scale <= 0 || windowX < X || windowY < Y || windowX >= X + Width || windowY >= Y + Height)
		{
			return null;
		}
		return ((int)((windowX - X) / Scale), (int)((windowY - Y) / Scale));
	}
}
