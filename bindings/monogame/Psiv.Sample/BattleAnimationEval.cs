using System.Globalization;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework;

namespace Psiv.Sample;

// A color transform as drawn: the multiply as the picture's tint, an added color drawn as the picture's silhouette on
// top, the opacity, and an exact-match recolor of opaque source pixels.
public readonly record struct ColorTransform(Color Multiply, Color Add, float Alpha, IReadOnlyList<(Color From, Color To)>? Palette)
{
	public static readonly ColorTransform None = new(Color.White, Color.Transparent, 1f, null);

	public bool HasAdd => Add.R > 0 || Add.G > 0 || Add.B > 0;
}

// The pure parts of drawing a script: easing, color transforms and effect animation frames. Everything is computed from
// script time and the inputs, so any frame can be drawn without playing up to it.
public static class BattleAnimationEval
{
	public static float Ease(string? ease, float t)
	{
		t = Math.Clamp(t, 0f, 1f);
		return ease switch
		{
			"linear" or null => t,
			"in" => t * t,
			"out" => 1f - (1f - t) * (1f - t),
			"in_out" => t < 0.5f ? 2f * t * t : 1f - 2f * (1f - t) * (1f - t),
			"smooth" => t * t * (3f - 2f * t),
			"sine" => MathF.Sin(MathF.PI * t),
			"step" => t >= 1f ? 1f : 0f,
			_ => t,
		};
	}

	public static Color ParseColor(string s, Func<string, Color?> named) =>
		s.StartsWith('#') ? UiLayoutColor(s) : named(s) ?? Color.White;

	private static Color UiLayoutColor(string s)
	{
		uint v = uint.Parse(s[1..], NumberStyles.HexNumber, CultureInfo.InvariantCulture);
		return s.Length == 7 ? new Color((int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255) : new Color((int)(v >> 24) & 255, (int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255);
	}

	// A CT object; `amountScale` scales a tint's amount (an eased `set`). `tint` and `amount` tint the multiply toward
	// the color (Color.Lerp from white), as the table's glow and hurt colors did.
	public static ColorTransform Ct(JsonNode? node, Func<string, Color> color, float amountScale = 1f)
	{
		if (node is not JsonObject o)
		{
			return ColorTransform.None;
		}
		Color mul = Color.White;
		if (o["multiply"] is JsonArray rgb && rgb.Count == 3)
		{
			mul = new Color((float)(BattleAnimationRun.Num(rgb[0]) ?? 1), (float)(BattleAnimationRun.Num(rgb[1]) ?? 1), (float)(BattleAnimationRun.Num(rgb[2]) ?? 1));
		}
		else if (BattleAnimationRun.Str(o["multiply"]) is { } m)
		{
			mul = color(m);
		}
		if (BattleAnimationRun.Str(o["tint"]) is { } tint)
		{
			float amount = (float)(BattleAnimationRun.Num(o["amount"]) ?? 1);
			mul = Color.Lerp(Color.White, color(tint), amount * amountScale);
		}
		Color add = BattleAnimationRun.Str(o["add"]) is { } a ? color(a) : Color.Transparent;
		if (amountScale != 1f && o["tint"] is null)
		{
			add = Color.Lerp(Color.Transparent, add, amountScale);
		}
		float alpha = (float)(BattleAnimationRun.Num(o["alpha"]) ?? 1);
		List<(Color, Color)>? palette = o["palette"] is JsonArray map
			? [.. map.OfType<JsonArray>().Select(p => (UiLayoutColor(BattleAnimationRun.Str(p[0])!), UiLayoutColor(BattleAnimationRun.Str(p[1])!)))]
			: null;
		return new ColorTransform(mul, add, alpha, palette);
	}

	public static ColorTransform Lerp(ColorTransform a, ColorTransform b, float t) =>
		new(Color.Lerp(a.Multiply, b.Multiply, t), Color.Lerp(a.Add, b.Add, t), a.Alpha + (b.Alpha - a.Alpha) * t, t < 1f ? a.Palette : b.Palette);

	// A color track's transform at progress t over its length; `local` and `rate` for the absolute times of tint and cycle.
	public static ColorTransform? ColorAt(JsonObject def, float t, long local, long length, AnimRun run, Func<string, Color> color)
	{
		string mode = BattleAnimationRun.Str(def["mode"]) ?? "set";
		string? ease = BattleAnimationRun.Str(def["ease"]);
		ColorTransform to = Ct(BattleAnimationRun.Pick(def["to"], run.In.Front), color);
		switch (mode)
		{
		case "set":
			if (ease is null)
			{
				return to;
			}
			return Ct(BattleAnimationRun.Pick(def["to"], run.In.Front), color, Ease(ease, t));
		case "flash":
			return Lerp(to, ColorTransform.None, Ease(ease ?? "linear", t));
		case "tint":
		{
			long inLen = BattleAnimationRun.Time(def["in"], run), hold = BattleAnimationRun.Time(def["hold"], run), outLen = BattleAnimationRun.Time(def["out"], run);
			if (local < inLen)
			{
				return Lerp(ColorTransform.None, to, Ease(ease, BattleClock.Ratio(local, inLen)));
			}
			if (local < inLen + hold)
			{
				return to;
			}
			return local < inLen + hold + outLen ? Lerp(to, ColorTransform.None, Ease(ease, BattleClock.Ratio(local - inLen - hold, outLen))) : null;
		}
		case "fade":
		{
			long inLen = BattleAnimationRun.Time(def["in"], run);
			return local < inLen ? Lerp(ColorTransform.None, to, Ease(ease, BattleClock.Ratio(local, inLen))) : to;
		}
		case "cycle":
		{
			var colors = (def["colors"] as JsonArray ?? []).Select(c => Ct(c, color)).ToList();
			if (colors.Count == 0)
			{
				return null;
			}
			long period = Math.Max(1, BattleAnimationRun.Time(def["period"], run, BattleClock.Frame));
			long k = local / period;
			ColorTransform a = colors[(int)(k % colors.Count)];
			if (BattleAnimationRun.Bool(def["smooth"]) == true)
			{
				return Lerp(a, colors[(int)((k + 1) % colors.Count)], BattleClock.Ratio(local % period, period));
			}
			return a;
		}
		case "keys":
		{
			var keys = (def["keys"] as JsonArray ?? []).OfType<JsonArray>()
				.Select(k => (At: BattleAnimationRun.Time(k[0], run, length, length, out _) ?? 0, Ct: Ct(k[1], color))).OrderBy(k => k.At).ToList();
			if (keys.Count == 0)
			{
				return null;
			}
			if (local <= keys[0].At)
			{
				return keys[0].Ct;
			}
			for (int i = 1; i < keys.Count; i++)
			{
				if (local < keys[i].At)
				{
					float u = BattleClock.Ratio(local - keys[i - 1].At, keys[i].At - keys[i - 1].At);
					return ease == "step" ? keys[i - 1].Ct : Lerp(keys[i - 1].Ct, keys[i].Ct, Ease(ease, u));
				}
			}
			return keys[^1].Ct;
		}
		}
		return null;
	}

	// The frame of an effect animation at `local` clock units into an instance, -1 for none (a frame of -1 draws
	// nothing), and whether the instance is over.
	public static int Frame(int[] frames, long[] durations, int loops, bool pingpong, long holdLast, long local, out bool over)
	{
		over = false;
		if (frames.Length == 0)
		{
			return -1;
		}
		int[] seq = pingpong && frames.Length > 1 ? [.. frames, .. frames.Reverse().Skip(1).SkipLast(1)] : frames;
		long[] lens = pingpong && durations.Length > 1 ? [.. durations, .. durations.Reverse().Skip(1).SkipLast(1)] : durations;
		long once = lens.Sum();
		if (once <= 0)
		{
			return seq[^1];
		}
		long plays = loops < 0 ? long.MaxValue : loops + 1;
		long whole = local / once;
		if (whole >= plays)
		{
			over = local >= once * plays + holdLast;
			return seq[^1];
		}
		long at = local % once;
		for (int i = 0; i < seq.Length; i++)
		{
			if (at < lens[i])
			{
				return seq[i];
			}
			at -= lens[i];
		}
		return seq[^1];
	}

	// A distance: a number or "Nm" in pixels scaled by the layout's motion, "Npx" in logical pixels never scaled, forward
	// units, or a share of the figure's height or width.
	public static float Dist(JsonNode? n, float forwardUnit, Rectangle figure, float motion)
	{
		if (BattleAnimationRun.Num(n) is double px)
		{
			return (float)px * motion;
		}
		string? s = BattleAnimationRun.Str(n);
		if (s is not null && s.EndsWith("px", StringComparison.Ordinal))
		{
			return double.TryParse(s[..^2], CultureInfo.InvariantCulture, out double logical) ? (float)logical : 0;
		}
		if (s is null || s.Length < 2 || !double.TryParse(s[..^1], CultureInfo.InvariantCulture, out double v))
		{
			return 0;
		}
		return s[^1] switch
		{
			'm' => (float)v * motion,
			'f' => (float)v * forwardUnit,
			'h' => (float)v * figure.Height,
			'w' => (float)v * figure.Width,
			_ => 0,
		};
	}

	// A length in logical pixels that is never scaled: a number or "Npx".
	public static float? Px(JsonNode? n)
	{
		if (BattleAnimationRun.Num(n) is double px)
		{
			return (float)px;
		}
		return BattleAnimationRun.Str(n) is { } s && s.EndsWith("px", StringComparison.Ordinal) && double.TryParse(s[..^2], CultureInfo.InvariantCulture, out double v) ? (float)v : null;
	}

	public static float Hash(string a, int b, int c)
	{
		uint h = Noise.Hash(a) ^ (uint)(b * 73856093) ^ (uint)(c * 19349663);
		h = (h ^ (h >> 13)) * 1274126177u;
		return (h & 0xffff) / 65535f;
	}
}
