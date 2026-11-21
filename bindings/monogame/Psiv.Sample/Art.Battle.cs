using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The battle's generated pictures: backdrops, figures for both sides, effects and status icons, each replaced by the
// manifest's file when it names one.
public sealed partial class Art
{
	public const int EffectSize = 32;
	public const int BackdropWidth = 320;
	public const int BackdropHeight = 105;
	private const int Horizon = 60;
	private const int EffectFrames = 6;
	private const int EffectFrameMs = 60;

	private readonly Dictionary<Theme, int> _generatedBackdrops = [];
	private readonly Dictionary<string, int> _generatedIcons = [];

	// The encounter's backdrop, else the area's, else the area kind's.
	public int Backdrop(AreaKind kind, string area, string encounter)
	{
		if (_backdrops.TryGetValue("encounter:" + encounter, out int image) || _backdrops.TryGetValue("area:" + area, out image) ||
			_backdrops.TryGetValue("kind:" + FieldRenderer.KindName(kind).ToLowerInvariant(), out image))
		{
			return image;
		}
		Theme theme = ThemeOf(kind);
		if (!_generatedBackdrops.TryGetValue(theme, out image))
		{
			image = _s.CreateImage(BackdropWidth, BackdropHeight, BackdropPixels(theme));
			_sizes[image] = (BackdropWidth, BackdropHeight);
			_generatedBackdrops[theme] = image;
		}
		return image;
	}

	// Sky shading into a horizon, a far silhouette (rooftops with lit windows, hills, or a cave's hanging rock and
	// ridges), and the area kind's floor tiles as the ground.
	public static Color[] BackdropPixels(Theme theme)
	{
		var canvas = new CanvasSurface(BackdropWidth, BackdropHeight);
		(Color top, Color bottom, Color far) = theme switch
		{
			Theme.Town => (new Color(48, 54, 112), new Color(226, 160, 136), new Color(60, 54, 96)),
			Theme.Dungeon => (new Color(12, 10, 18), new Color(54, 46, 68), new Color(30, 26, 40)),
			_ => (new Color(92, 156, 222), new Color(204, 230, 242), new Color(70, 128, 90)),
		};
		for (int y = 0; y < Horizon; y++)
		{
			canvas.Fill(0, y, BackdropWidth, 1, Color.Lerp(top, bottom, y / (Horizon - 1f)));
		}
		for (int x = 0; x < BackdropWidth; x += 2)
		{
			switch (theme)
			{
			case Theme.Town:
				int roof = 12 + (int)(Noise.Hash(x / 18, 3) % 5) * 5;
				canvas.Fill(x, Horizon - roof, 2, roof, far);
				if (x % 18 == 6 && Noise.Hash(x, 4) % 3 != 0)
				{
					canvas.Fill(x, Horizon - roof + 4, 3, 3, new Color(250, 214, 120));
				}
				break;
			case Theme.Dungeon:
				int span = (int)(Noise.Hash(x / 12, 5) % 22) + 5;
				canvas.Fill(x, 0, 2, span * (6 - Math.Abs(x % 12 - 6)) / 6, far);
				int ridge = 6 + (int)(4 * MathF.Sin(x / 15f)) + (int)(Noise.Hash(x / 4, 9) % 3);
				canvas.Fill(x, Horizon - ridge, 2, ridge, far);
				break;
			default:
				int hill = 11 + (int)(6 * MathF.Sin(x / 27f) + 3 * MathF.Sin(x / 10f + 1.3f));
				canvas.Fill(x, Horizon - hill, 2, hill, far);
				break;
			}
		}
		Pixels[] floor = theme switch
		{
			Theme.Town => [.. Enumerable.Range(0, 4).Select(Cobbles)],
			Theme.Dungeon => [.. Enumerable.Range(0, 4).Select(i => Flagstones(2000 + i))],
			_ => [.. Enumerable.Range(0, 4).Select(i => Grass(1000 + i))],
		};
		int[] tiles = [.. floor.Select(p => canvas.CreateImage(Pixels.Size, Pixels.Size, p.Data))];
		for (int y = Horizon; y < BackdropHeight; y += Pixels.Size)
		{
			for (int x = 0; x < BackdropWidth; x += Pixels.Size)
			{
				canvas.Image(tiles[Noise.Hash(x, y) % (uint)tiles.Length], x, y, Pixels.Size, Pixels.Size, Color.White);
			}
		}
		canvas.Fill(0, Horizon, BackdropWidth, 4, Color.Black * 0.3f);
		return canvas.Pixels;
	}

	// A battle figure: the manifest's, else for the party the person's look facing west (standing, a stride for an
	// attack, arms up to cast or use an item, lying down when fallen) and for an enemy its creature, bobbing.
	public Figure BattleFigure(string id, bool enemy)
	{
		if (_figures.TryGetValue((id, enemy), out Figure? figure))
		{
			return figure;
		}
		if (enemy)
		{
			uint seed = Noise.Hash(id);
			figure = new Figure([Creature(seed, 0), Creature(seed, 1)], new FrameRun([0, 1], [400, 400]), new Dictionary<string, FrameRun>());
		}
		else
		{
			ActorLook look = LookFor(id);
			int lying = Add(Lying(look));
			figure = new Figure([ActorFrame(look, Direction.West, 0), ActorFrame(look, Direction.West, 1), PoseImage(look, Direction.West, Pose.Startle, 1), lying],
				new FrameRun([0], [400]), new Dictionary<string, FrameRun>
				{
					["attack"] = new([1, 0], [140, 140]),
					["cast"] = new([2], [280]),
					["item"] = new([2], [280]),
					["defeat"] = new([3], [400]),
				});
		}
		_figures[(id, enemy)] = figure;
		return figure;
	}

	// The person seen from the front, turned on its side and lying on the ground.
	private static Pixels Lying(ActorLook look)
	{
		Pixels p = Pixels.FromTemplate(Dress(FrontRows, look, View.Front), ActorPalette(look)).Rotate();
		int lowest = Enumerable.Range(0, Pixels.Size).Where(y => Enumerable.Range(0, Pixels.Size).Any(x => p[x, y].A > 0)).DefaultIfEmpty(Pixels.Size - 1).Max();
		return p.Shift(Pixels.Size - 1 - lowest);
	}

	// An effect by id: the manifest's, else a generated one, else none.
	public Figure? Effect(string id)
	{
		if (_effects.TryGetValue(id, out Figure? effect))
		{
			return effect;
		}
		if (!BattlePresentation.GeneratedEffects.Contains(id))
		{
			return null;
		}
		effect = new Figure([.. Enumerable.Range(0, EffectFrames).Select(f => _s.CreateImage(EffectSize, EffectSize, EffectPixels(id, f)))],
			new FrameRun([.. Enumerable.Range(0, EffectFrames)], [.. Enumerable.Repeat(EffectFrameMs, EffectFrames)]), new Dictionary<string, FrameRun>());
		foreach (int image in effect.Images)
		{
			_sizes[image] = (EffectSize, EffectSize);
		}
		_effects[id] = effect;
		return effect;
	}

	public static Color ElementColor(string element) => element switch
	{
		"heat" => new Color(255, 140, 60),
		"chill" => new Color(120, 210, 255),
		"shock" => new Color(255, 230, 80),
		"gale" => new Color(130, 230, 140),
		_ => new Color(240, 240, 240),
	};

	// One frame of a generated effect: a style (slash, burst, nova, ring, motes or shield) in a color from its element
	// or purpose, growing over the frames and fading over the last ones.
	public static Color[] EffectPixels(string id, int frame)
	{
		string style = id.Split('_')[0];
		string rest = id.Contains('_') ? id[(id.IndexOf('_') + 1)..] : "none";
		Color color = rest switch
		{
			"status" => new Color(190, 120, 255),
			"stat" => new Color(255, 196, 90),
			"cure" => new Color(240, 250, 255),
			"heal" => new Color(120, 240, 150),
			"revive" => new Color(255, 226, 120),
			"item" => new Color(150, 210, 255),
			_ when style == "shield" => new Color(120, 180, 255),
			_ => ElementColor(rest),
		};
		var pixels = new Color[EffectSize * EffectSize];
		float t = (frame + 1) / (float)EffectFrames;
		float fade = frame < EffectFrames - 2 ? 1f : (EffectFrames - frame) / 3f;
		Color core = Color.Lerp(color, Color.White, 0.6f) * fade, edge = color * fade;
		void Dot(int x, int y, Color c)
		{
			if (x is >= 0 and < EffectSize && y is >= 0 and < EffectSize)
			{
				pixels[y * EffectSize + x] = c;
			}
		}
		const float mid = (EffectSize - 1) / 2f;
		var rng = new Rng(Noise.Hash(id));
		switch (style)
		{
		case "slash":
			// Three parallel strokes from the top right to the bottom left, drawn further along each frame.
			for (int k = -1; k <= 1; k++)
			{
				int length = (int)(EffectSize * 1.2f * Math.Min(1f, t * 1.6f));
				for (int i = 0; i < length; i++)
				{
					int x = EffectSize - 3 - i + k * 6, y = 2 + i + Math.Abs(k) * 2;
					Dot(x, y, core);
					Dot(x + 1, y, edge);
					Dot(x, y + 1, edge);
				}
			}
			break;
		case "burst":
		case "nova":
			// Spikes out from the middle, longer each frame; a nova adds a ring around them.
			int spikes = style == "nova" ? 12 : 8;
			for (int s = 0; s < spikes; s++)
			{
				float angle = s * MathF.Tau / spikes + rng.Range(10) * 0.02f;
				float reach = mid * t * (s % 2 == 0 ? 1f : 0.7f);
				for (float r = reach * 0.3f; r < reach; r += 0.5f)
				{
					int x = (int)MathF.Round(mid + MathF.Cos(angle) * r), y = (int)MathF.Round(mid + MathF.Sin(angle) * r);
					Dot(x, y, r < reach * 0.7f ? core : edge);
				}
			}
			if (style == "nova")
			{
				Ring(Dot, mid, mid * t, edge);
			}
			Dot((int)mid, (int)mid, core);
			break;
		case "ring":
			Ring(Dot, mid, 3 + (mid - 3) * t, edge);
			Ring(Dot, mid, 1 + (mid - 5) * t, core);
			break;
		case "motes":
			// Specks rising from the bottom, each at its own pace.
			for (int i = 0; i < 14; i++)
			{
				int x = 3 + rng.Range(EffectSize - 6);
				int start = EffectSize - 2 - rng.Range(8);
				int y = start - (int)(t * (12 + rng.Range(14)));
				Dot(x, y, core);
				Dot(x, y + 1, edge);
				Dot(x - 1, y + 1, edge * 0.6f);
				Dot(x + 1, y + 1, edge * 0.6f);
			}
			break;
		default:
			// A shield: an upright oval that firms up and fades.
			for (float a = 0; a < MathF.Tau; a += 0.04f)
			{
				int x = (int)MathF.Round(mid + MathF.Cos(a) * 10), y = (int)MathF.Round(mid + MathF.Sin(a) * 14);
				Dot(x, y, frame % 2 == 0 ? core : edge);
			}
			break;
		}
		return pixels;
	}

	private static void Ring(Action<int, int, Color> dot, float mid, float radius, Color c)
	{
		for (float a = 0; a < MathF.Tau; a += 0.03f)
		{
			dot((int)MathF.Round(mid + MathF.Cos(a) * radius), (int)MathF.Round(mid + MathF.Sin(a) * radius), c);
		}
	}

	// A status's icon: the manifest's, else a disc in a color from the status id with one of a few marks on it.
	public int StatusIcon(string id)
	{
		if (_statusIcons.TryGetValue(id, out int image) || _generatedIcons.TryGetValue(id, out image))
		{
			return image;
		}
		image = Add(StatusIconPixels(id));
		_generatedIcons[id] = image;
		return image;
	}

	public static Pixels StatusIconPixels(string id)
	{
		uint h = Noise.Hash(id);
		Color body = FromHsv((h & 0xff) / 255f, 0.6f, 0.85f), mark = new(250, 250, 244);
		var p = new Pixels();
		for (int y = 0; y < Pixels.Size; y++)
		{
			for (int x = 0; x < Pixels.Size; x++)
			{
				float dx = x - 7.5f, dy = y - 7.5f;
				float d = dx * dx + dy * dy;
				if (d <= 56f)
				{
					p[x, y] = d > 42f ? Outline : body;
				}
			}
		}
		switch ((h >> 8) % 4)
		{
		case 0:
			p.Rect(5, 7, 6, 2, mark);
			break;
		case 1:
			p.Rect(7, 4, 2, 8, mark);
			p.Rect(4, 7, 8, 2, mark);
			break;
		case 2:
			p.Rect(5, 5, 6, 1, mark).Rect(6, 7, 4, 1, mark).Rect(7, 9, 2, 1, mark);
			break;
		default:
			p.Rect(6, 6, 4, 4, mark);
			break;
		}
		return p;
	}
}
