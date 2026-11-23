using Microsoft.Xna.Framework;

namespace Psiv.Sample;

// An image element's `src`: the part of a picture a layout scrolls or crops over time, in the picture's own pixels.
public static class UiImages
{
	// The part (sx, sy, sw, sh) drawn into the rectangle, each source pixel as w / sw by h / sh of it. Without `repeat` what
	// falls outside the picture is left out (the rectangle shrinks with it); with it the picture wraps on both axes.
	public static void DrawPart(IDrawSurface s, int image, int sx, int sy, int sw, int sh, bool repeat, int x, int y, int w, int h, Color tint)
	{
		(int iw, int ih) = s.ImageSize(image);
		if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0 || iw <= 0 || ih <= 0)
		{
			return;
		}
		if (!repeat)
		{
			int x0 = Math.Max(sx, 0), x1 = Math.Min(sx + sw, iw), y0 = Math.Max(sy, 0), y1 = Math.Min(sy + sh, ih);
			if (x0 < x1 && y0 < y1)
			{
				int dx0 = x + (x0 - sx) * w / sw, dx1 = x + (x1 - sx) * w / sw, dy0 = y + (y0 - sy) * h / sh, dy1 = y + (y1 - sy) * h / sh;
				s.ImagePart(image, x0, y0, x1 - x0, y1 - y0, dx0, dy0, dx1 - dx0, dy1 - dy0, tint);
			}
			return;
		}
		for (int v = sy; v < sy + sh;)
		{
			int tv = Mod(v, ih), rows = Math.Min(ih - tv, sy + sh - v);
			int dy0 = y + (v - sy) * h / sh, dy1 = y + (v + rows - sy) * h / sh;
			for (int u = sx; u < sx + sw;)
			{
				int tu = Mod(u, iw), columns = Math.Min(iw - tu, sx + sw - u);
				int dx0 = x + (u - sx) * w / sw, dx1 = x + (u + columns - sx) * w / sw;
				if (dx1 > dx0 && dy1 > dy0)
				{
					s.ImagePart(image, tu, tv, columns, rows, dx0, dy0, dx1 - dx0, dy1 - dy0, tint);
				}
				u += columns;
			}
			v += rows;
		}
	}

	private static int Mod(int a, int n) => (a % n + n) % n;
}
