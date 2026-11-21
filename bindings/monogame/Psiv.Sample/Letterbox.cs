namespace Psiv.Sample;

// Where the sample's 1280x720 frame goes in a window of another size: centered with bars, scaled by a whole number
// when the window has room for it, so pixel art stays sharp, and shrunk to fit when the window is smaller.
public readonly record struct Letterbox(int X, int Y, int Width, int Height, float Scale)
{
	public const int BaseWidth = 1280;
	public const int BaseHeight = 720;

	public static Letterbox Fit(int windowWidth, int windowHeight)
	{
		float fit = Math.Min((float)windowWidth / BaseWidth, (float)windowHeight / BaseHeight);
		float scale = fit >= 1f ? MathF.Floor(fit) : fit;
		int w = (int)MathF.Round(BaseWidth * scale);
		int h = (int)MathF.Round(BaseHeight * scale);
		return new((windowWidth - w) / 2, (windowHeight - h) / 2, w, h, scale);
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
