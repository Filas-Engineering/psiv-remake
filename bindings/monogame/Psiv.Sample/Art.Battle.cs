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
	private readonly Dictionary<string, int> _generatedCommandIcons = [];

	// The encounter's backdrop, else the first whose flag is set, else the area's backdrop map's at the party's cell, else
	// the terrain's under the party, else the area's, else the area kind's, with the key it came by ("generated" for none).
	public (int Image, string Key) Backdrop(AreaKind kind, string area, string encounter, string terrain, (int X, int Y) cell, Func<string, bool> flagSet)
	{
		if (_backdrops.TryGetValue("encounter:" + encounter, out int byEncounter))
		{
			return (byEncounter, "encounter:" + encounter);
		}
		foreach ((string flag, string? only, string key) in _flagBackdrops)
		{
			if ((only is null || only == area) && flagSet(flag))
			{
				return (_backdrops[key], key);
			}
		}
		if (_backdropMaps.TryGetValue(area, out BackdropMap? map) && cell.X >= 0 && cell.Y >= 0 && cell.X < map.Width && cell.Y < map.Height
			&& map.Cells[cell.Y * map.Width + cell.X] is { } mapped)
		{
			return (_namedBackdrops[mapped.Backdrop], mapped.Key);
		}
		foreach (string key in new[] { "terrain:" + terrain, "area:" + area, "kind:" + FieldRenderer.KindName(kind).ToLowerInvariant() })
		{
			if (_backdrops.TryGetValue(key, out int found))
			{
				return (found, key);
			}
		}
		Theme theme = ThemeOf(kind);
		if (!_generatedBackdrops.TryGetValue(theme, out int image))
		{
			image = _s.CreateImage(BackdropWidth, BackdropHeight, BackdropPixels(theme));
			_sizes[image] = (BackdropWidth, BackdropHeight);
			_generatedBackdrops[theme] = image;
		}
		return (image, "generated");
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
			figure = new Figure([Creature(seed, 0), Creature(seed, 1)], new FrameRun([0, 1], [400, 400]), new Dictionary<string, FrameRun>(), _figureShadows);
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
				}, _figureShadows);
		}
		_figures[(id, enemy)] = figure;
		return figure;
	}

	// The back poses, in the order of a generated back figure's frames.
	private enum BackPose { Idle, Attack, Cast, Item, Defend, Hurt, Defeat }

	// A party member seen from behind for the front layout: the manifest's back figure, else the person's look as head and
	// shoulders (standing, an arm raised to strike, both arms up to cast, a hand up with an item, braced, knocked aside
	// when hurt, slumped when fallen).
	public Figure BackFigure(string id)
	{
		if (_backFigures.TryGetValue(id, out Figure? figure))
		{
			return figure;
		}
		ActorLook look = LookFor(id);
		figure = new Figure([.. Enum.GetValues<BackPose>().Select(pose => Add(BackPixels(look, pose)))], new FrameRun([0], [400]), new Dictionary<string, FrameRun>
		{
			["attack"] = new([1, 0], [140, 140]),
			["cast"] = new([2], [280]),
			["item"] = new([3], [280]),
			["defend"] = new([4], [400]),
			["hurt"] = new([5], [300]),
			["defeat"] = new([6], [400]),
		}, _figureShadows);
		_backFigures[id] = figure;
		return figure;
	}

	// A vehicle seen from behind: the manifest's back figure for its id, else its north frames bobbing.
	public Figure VehicleBackFigure(string id)
	{
		if (!_backFigures.TryGetValue(id, out Figure? figure))
		{
			figure = new Figure([GeneratedVehicle(id, Direction.North, 0), GeneratedVehicle(id, Direction.North, 1)], new FrameRun([0, 1], [VehicleFrameMs, VehicleFrameMs]),
				new Dictionary<string, FrameRun> { ["attack"] = new([1, 0], [140, 140]) }, _figureShadows);
			_backFigures[id] = figure;
		}
		return figure;
	}

	private static readonly string[] BackBustRows =
	[
		"................",
		"................",
		"................",
		".....oooooo.....",
		"....ohhhhhho....",
		"...ohhhhhhhho...",
		"...ohhhhhhhho...",
		"...ohhhhhhhho...",
		"...ohhhhhhhho...",
		"....ohhhhhho....",
		"...ooaaaaaaoo...",
		".oocccaaaacccoo.",
		"odccccccccccccdo",
		"odccccccccccccdo",
		"odccccccccccccdo",
		"odccccccccccccdo",
	];

	private static Pixels BackPixels(ActorLook look, BackPose pose)
	{
		char[][] g = BackBustRows.Select(r => r.ToCharArray()).ToArray();
		void Swap(int y, char from, char to, int x0 = 0, int x1 = Pixels.Size - 1)
		{
			for (int x = x0; x <= x1; x++)
			{
				if (g[y][x] == from)
				{
					g[y][x] = to;
				}
			}
		}
		switch (look.Head)
		{
		case 1:
			Swap(4, 'h', 'p');
			Swap(5, 'h', 'p');
			Swap(6, 'h', 'q');
			break;
		case 2:
			for (int y = 4; y <= 9; y++)
			{
				Swap(y, 'h', 'p');
			}
			Swap(10, 'a', 'q');
			break;
		case 3:
			// Hair down the back, over the scarf and the coat.
			for (int y = 10; y <= 13; y++)
			{
				Swap(y, 'a', 'h', 5, 10);
				Swap(y, 'c', 'h', 5, 10);
			}
			break;
		case 4:
			Swap(6, 'h', 'p');
			break;
		}
		// The hair's lower rows in shade, the ears at its sides and the nape under it, so the back of the head has shape;
		// a hood or long hair covers ears and nape.
		Swap(8, 'h', 'g', 5, 10);
		Swap(9, 'h', 'g');
		if (look.Head is not (2 or 3))
		{
			g[6][2] = g[7][2] = g[6][13] = g[7][13] = 's';
			g[6][1] = g[7][1] = g[6][14] = g[7][14] = 'o';
			g[5][2] = g[5][13] = g[8][2] = g[8][13] = 'o';
			Swap(9, 'g', 's', 6, 9);
		}
		switch (look.Body)
		{
		case 1:
			// The apron's strings across the back.
			Swap(14, 'c', 'w', 2, 13);
			break;
		case 2:
			Swap(15, 'c', 'k');
			Swap(15, 'd', 'k');
			break;
		}
		switch (pose)
		{
		case BackPose.Attack:
			ArmUp(g, 13, 0, null);
			break;
		case BackPose.Cast:
			ArmUp(g, 13, 1, null);
			ArmUp(g, 1, 1, null);
			break;
		case BackPose.Item:
			ArmUp(g, 13, 4, 'w');
			break;
		case BackPose.Defend:
			g = ShiftRows(g, 3, 9, 0, 1);
			break;
		case BackPose.Hurt:
			g = ShiftRows(g, 3, 9, 1, 0);
			break;
		case BackPose.Defeat:
			g = ShiftRows(g, 3, 9, 0, 3);
			break;
		}
		return Pixels.FromTemplate(g.Select(r => new string(r)).ToArray(), ActorPalette(look));
	}

	// An arm two columns wide (from column x) raised from the shoulder, its hand at row `top`; `held` puts a thing of that
	// color in the hand.
	private static void ArmUp(char[][] g, int x, int top, char? held)
	{
		void Edge(int px, int py)
		{
			if (px is >= 0 and < Pixels.Size && py is >= 0 and < Pixels.Size && g[py][px] == '.')
			{
				g[py][px] = 'o';
			}
		}
		int outer = x < Pixels.Size / 2 ? x : x + 1;
		for (int y = top; y <= 11; y++)
		{
			char c = y < top + 2 ? 's' : 'c';
			g[y][x] = c;
			g[y][x + 1] = y >= top + 2 && x + 1 == outer ? 'd' : c;
			if (y >= top + 2 && x == outer)
			{
				g[y][x] = 'd';
			}
			Edge(x - 1, y);
			Edge(x + 2, y);
		}
		Edge(x, top - 1);
		Edge(x + 1, top - 1);
		if (held is { } thing && top >= 3)
		{
			g[top - 2][x] = g[top - 2][x + 1] = g[top - 1][x] = g[top - 1][x + 1] = thing;
			Edge(x, top - 3);
			Edge(x + 1, top - 3);
			Edge(x - 1, top - 2);
			Edge(x + 2, top - 2);
			Edge(x - 1, top - 1);
			Edge(x + 2, top - 1);
		}
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
		"holy" => new Color(255, 246, 196),
		_ => new Color(240, 240, 240),
	};

	// One frame of a generated effect: a style (slash, burst, nova, ring, motes, mark or shield) in a color from its element
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
			"join" => new Color(170, 140, 255),
			"merge" => new Color(110, 230, 200),
			"transform" => new Color(255, 120, 200),
			"defeat" => new Color(255, 70, 70),
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
		case "mark":
			// Two crossed strokes inside a ring, stamped at full size at once and pulsing.
			Ring(Dot, mid, mid - 2, edge);
			for (int i = 6; i < EffectSize - 6; i++)
			{
				for (int w = -1; w <= 1; w++)
				{
					Dot(i + w, i, frame % 2 == 0 ? core : edge);
					Dot(EffectSize - 1 - i + w, i, frame % 2 == 0 ? core : edge);
				}
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

	// The picture of a command a member chose (AssetManifest.CommandNames): the manifest's, else a square in the command's
	// color with its mark: a blade, a spark, a ring, two discs, a flask, a shield or an arrow.
	public int CommandIcon(string command)
	{
		if (_commandIcons.TryGetValue(command, out int image) || _generatedCommandIcons.TryGetValue(command, out image))
		{
			return image;
		}
		image = Add(CommandIconPixels(command));
		_generatedCommandIcons[command] = image;
		return image;
	}

	public static Pixels CommandIconPixels(string command)
	{
		int kind = Array.IndexOf(AssetManifest.CommandNames, command);
		Color body = FromHsv(kind / 7f, 0.55f, 0.6f), mark = new(250, 250, 244);
		var p = new Pixels().Rect(1, 1, 14, 14, Outline).Rect(2, 2, 12, 12, body);
		switch (command)
		{
		case "attack":
			for (int i = 4; i <= 11; i++)
			{
				p[i, 15 - i] = mark;
				p[i + 1, 15 - i] = mark;
			}
			p.Rect(4, 9, 3, 1, mark).Rect(6, 9, 1, 3, mark);
			break;
		case "technique":
			p.Rect(7, 4, 2, 8, mark).Rect(4, 7, 8, 2, mark).Rect(5, 5, 1, 1, mark).Rect(10, 5, 1, 1, mark).Rect(5, 10, 1, 1, mark).Rect(10, 10, 1, 1, mark);
			break;
		case "skill":
			p.Rect(5, 4, 6, 1, mark).Rect(5, 11, 6, 1, mark).Rect(4, 5, 1, 6, mark).Rect(11, 5, 1, 6, mark);
			break;
		case "combo":
			p.Rect(4, 5, 4, 4, mark).Rect(8, 7, 4, 4, mark);
			break;
		case "item":
			p.Rect(7, 3, 2, 3, mark).Rect(5, 6, 6, 6, mark);
			break;
		case "defend":
			p.Rect(4, 4, 8, 5, mark).Rect(5, 9, 6, 1, mark).Rect(6, 10, 4, 1, mark).Rect(7, 11, 2, 1, mark);
			break;
		default:
			p.Rect(4, 7, 7, 2, mark).Rect(9, 5, 1, 6, mark).Rect(10, 6, 1, 4, mark).Rect(11, 7, 1, 2, mark);
			break;
		}
		return p;
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
