using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public enum Theme { Town, Field, Dungeon }

// The procedural poses a script's `anim` plays on a person (Art.PoseFor maps the names).
public enum Pose { None, Nod, Shake, Jump, Startle, Bow, Wave, Other, FadeIn, FadeOut, Dissolve, BlinkOut, Tremble }

// Deterministic hashing, so a cell's floor variant or an NPC's colors never change between frames or runs.
public static class Noise
{
	public static uint Hash(int a, int b = 0, int c = 0)
	{
		uint h = 2166136261u;
		foreach (int v in (ReadOnlySpan<int>)[a, b, c])
		{
			h ^= (uint)v;
			h *= 16777619u;
			h ^= h >> 13;
			h *= 0x5bd1e995u;
			h ^= h >> 15;
		}
		h ^= h >> 16;
		h *= 0x85ebca6bu;
		h ^= h >> 13;
		h *= 0xc2b2ae35u;
		h ^= h >> 16;
		return h;
	}

	public static uint Hash(string text)
	{
		uint h = 2166136261u;
		foreach (char ch in text)
		{
			h = (h ^ ch) * 16777619u;
		}
		return Hash((int)h);
	}
}

// A small xorshift generator for building pixels.
public struct Rng(uint seed)
{
	private uint _state = seed == 0 ? 0x9e3779b9u : seed;

	public uint Next()
	{
		_state ^= _state << 13;
		_state ^= _state >> 17;
		_state ^= _state << 5;
		return _state;
	}

	public int Range(int n) => (int)(Next() % (uint)n);
	public bool Chance(int percent) => Range(100) < percent;
}

// A square pixel buffer; colors are premultiplied, as SpriteBatch's default blending expects.
public sealed class Pixels
{
	public const int Size = 16;
	public Color[] Data { get; } = new Color[Size * Size];

	public Color this[int x, int y]
	{
		get => x is >= 0 and < Size && y is >= 0 and < Size ? Data[y * Size + x] : Color.Transparent;
		set
		{
			if (x is >= 0 and < Size && y is >= 0 and < Size)
			{
				Data[y * Size + x] = value;
			}
		}
	}

	public Pixels Fill(Color c)
	{
		Array.Fill(Data, c);
		return this;
	}

	public Pixels Rect(int x, int y, int w, int h, Color c)
	{
		for (int j = y; j < y + h; j++)
		{
			for (int i = x; i < x + w; i++)
			{
				this[i, j] = c;
			}
		}
		return this;
	}

	public Pixels MirrorX()
	{
		var p = new Pixels();
		for (int y = 0; y < Size; y++)
		{
			for (int x = 0; x < Size; x++)
			{
				p[Size - 1 - x, y] = this[x, y];
			}
		}
		return p;
	}

	// A quarter turn clockwise.
	public Pixels Rotate()
	{
		var p = new Pixels();
		for (int y = 0; y < Size; y++)
		{
			for (int x = 0; x < Size; x++)
			{
				p[Size - 1 - y, x] = this[x, y];
			}
		}
		return p;
	}

	public Pixels Shift(int dy)
	{
		var p = new Pixels();
		for (int y = 0; y < Size; y++)
		{
			for (int x = 0; x < Size; x++)
			{
				p[x, y] = this[x, y - dy];
			}
		}
		return p;
	}

	public static Pixels FromTemplate(string[] rows, Func<char, Color> palette)
	{
		if (rows.Length != Size || rows.Any(r => r.Length != Size))
		{
			throw new ArgumentException("a template is 16 rows of 16 characters");
		}
		var p = new Pixels();
		for (int y = 0; y < Size; y++)
		{
			for (int x = 0; x < Size; x++)
			{
				p[x, y] = rows[y][x] == '.' ? Color.Transparent : palette(rows[y][x]);
			}
		}
		return p;
	}

	// Scatters `count` pixels of `c` over the buffer.
	public Pixels Speckle(ref Rng rng, int count, Color c)
	{
		for (int i = 0; i < count; i++)
		{
			this[rng.Range(Size), rng.Range(Size)] = c;
		}
		return this;
	}
}

// A picture drawn with a person in a pose, offset from the person's own picture by (Dx, Dy) art pixels.
public readonly record struct MotionMark(int Image, int Dx, int Dy);

// Head: 0 bare, 1 cap, 2 hood, 3 long hair, 4 headband. Body: 0 coat, 1 apron, 2 belt. Hat colors the cap, hood
// and headband.
public sealed record ActorLook(Color Hair, Color Skin, Color Coat, Color Scarf, int Head = 0, int Body = 0, Color Hat = default);

// Every picture the sample draws, generated at startup from code and handed to the surface once, unless the asset
// directory's manifest names a file for it (Art.Files.cs). Actors and creatures are built on first use per look.
public sealed partial class Art
{
	public const int Scale = 3;
	public const int Tile = Pixels.Size * Scale;
	public const int WaterFrames = 4;

	// An area's tiles, how the renderer picks among them (one floor in DecorOneIn is a decoration, one wall top in
	// WallTopAltOneIn and one wall face in WallFaceAltOneIn is a variant, water steps a frame every WaterFrameMs) and
	// the objects standing on its cells.
	public sealed record TileSet
	{
		public required int[] Floor { get; init; }
		public required int[] Decor { get; init; }
		public required int[] WallTop { get; init; }
		public required int[] WallFace { get; init; }
		public required int[] Water { get; init; }
		public required Color Edge { get; init; }
		public required Color Bank { get; init; }
		public int DecorOneIn { get; init; } = 11;
		public int WallTopAltOneIn { get; init; } = 3;
		public int WallFaceAltOneIn { get; init; } = 5;
		public int WaterFrameMs { get; init; } = 220;
		public required int Door { get; init; }
		public required int ChestClosed { get; init; }
		public required int ChestOpen { get; init; }
		public required int Stall { get; init; }
		public required int Bed { get; init; }
		// A talk point that is not an actor.
		public required int Sign { get; init; }
		public required int Rune { get; init; }
		// North, east, south, west.
		public required int[] ExitArrow { get; init; }

		// By the picture they stand in for: frame runs over Images, looping in step on every cell.
		public IReadOnlyDictionary<int, Figure> Animated { get; init; } = new Dictionary<int, Figure>();

		// Recolors of every tile picture the cells show.
		public IReadOnlyList<PaletteTrack> Palettes { get; init; } = [];

		public int WaterFrame(int clockMs) => clockMs / WaterFrameMs % Water.Length;

		public int Frame(int image, int clockMs) => Animated.TryGetValue(image, out Figure? run) ? run.Images[run.Idle.FrameAt(clockMs, loop: true)] : image;

		// The first picture, or with a one in `oneIn` chance one of the others, from the cell's hash.
		public static int Variant(int[] images, uint hash, int oneIn) =>
			images.Length > 1 && hash % (uint)oneIn == 0 ? images[1 + (int)(hash / (uint)oneIn % (uint)(images.Length - 1))] : images[0];
	}

	private static readonly Color Outline = new(26, 20, 32);

	// The title screen's traveler.
	public static readonly ActorLook Hero = new(new Color(206, 122, 58), new Color(242, 204, 164), new Color(58, 142, 172), new Color(232, 92, 72));

	private readonly IDrawSurface _s;
	private readonly Dictionary<Theme, TileSet> _tiles = [];
	private readonly Dictionary<ActorLook, int[]> _actors = [];
	private readonly Dictionary<(uint, int), int> _creatures = [];
	private readonly MotionMark[] _nodMarks;
	private readonly MotionMark[] _shakeMarks;

	public Art(IDrawSurface surface, AssetManifest? assets = null)
	{
		_s = surface;
		Pixels arrow = Arrow();
		int[] exitArrow = [Add(arrow.Rotate().Rotate().Rotate()), Add(arrow), Add(arrow.Rotate()), Add(arrow.Rotate().Rotate())];
		int door = Add(BuildDoor());
		int chestClosed = Add(BuildChest(open: false));
		int chestOpen = Add(BuildChest(open: true));
		int stall = Add(BuildStall());
		int bed = Add(BuildBed());
		int rune = Add(BuildRune());
		Bubble = Add(BuildBubble());
		int noticeboard = Add(BuildNoticeboard());
		Alert = Add(BuildAlert());
		Shadow = Add(BuildShadow());
		Pointer = Add(BuildPointer());
		Pixels nod = BuildNodMark();
		_nodMarks = [new MotionMark(Add(nod), -3, -3), new MotionMark(Add(nod.MirrorX()), 3, -3)];
		Pixels shake = BuildShakeMark();
		_shakeMarks = [new MotionMark(Add(shake), -2, 0), new MotionMark(Add(shake.MirrorX()), 2, 0)];
		foreach (Theme theme in Enum.GetValues<Theme>())
		{
			_tiles[theme] = BuildTiles(theme, door, chestClosed, chestOpen, stall, bed, noticeboard, rune, exitArrow);
		}
		if (assets is not null)
		{
			_assetDir = assets.Directory;
			LoadFiles(assets);
		}
	}

	public static Theme ThemeOf(AreaKind kind) => kind switch
	{
		AreaKind.Town or AreaKind.Vessel => Theme.Town,
		AreaKind.Dungeon => Theme.Dungeon,
		_ => Theme.Field,
	};

	public TileSet Tiles(Theme theme) => _tiles[theme];

	public int Bubble { get => Sprite("bubble", field); private set; }
	public int Alert { get => Sprite("alert", field); private set; }
	public int Shadow { get => Sprite("shadow", field); private set; }
	// A downward pointer, for battle targets.
	public int Pointer { get => Sprite("pointer", field); private set; }

	public static (Color Edge, Color Bank) DefaultEdges(Theme theme) => theme switch
	{
		Theme.Field => (new Color(20, 52, 30), new Color(120, 98, 62)),
		Theme.Town => (new Color(40, 36, 70), new Color(110, 104, 98)),
		_ => (new Color(18, 16, 24), new Color(58, 54, 66)),
	};

	private int Add(Pixels p) => _s.CreateImage(Pixels.Size, Pixels.Size, p.Data);

	private static Color Shade(Color c, float f) => new((int)Math.Clamp(c.R * f, 0, 255), (int)Math.Clamp(c.G * f, 0, 255), (int)Math.Clamp(c.B * f, 0, 255));

	public static Color FromHsv(float h, float s, float v)
	{
		h = (h % 1f + 1f) % 1f * 6f;
		int i = (int)h;
		float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
		(float r, float g, float b) = i switch { 0 => (v, t, p), 1 => (q, v, p), 2 => (p, v, t), 3 => (p, q, v), 4 => (t, p, v), _ => (v, p, q) };
		return new Color(r, g, b);
	}

	// Every person in the game, party member or actor, looks the same wherever it is drawn: colors, headwear and
	// clothes all come from a hash of its id.
	public static ActorLook LookFor(string id)
	{
		uint h = Noise.Hash(id);
		Color[] hair = [new(52, 40, 34), new(120, 76, 44), new(216, 190, 120), new(150, 150, 160), new(170, 60, 50), new(40, 40, 60)];
		Color[] skin = [new(242, 204, 164), new(214, 164, 120), new(164, 112, 78), new(250, 220, 190)];
		float hue = (h & 0xff) / 255f;
		uint g = Noise.Hash((int)h, 1);
		return new ActorLook(hair[(h >> 8) % (uint)hair.Length], skin[(h >> 12) % (uint)skin.Length], FromHsv(hue, 0.5f, 0.72f), FromHsv(hue + 0.45f, 0.55f, 0.9f),
			Head: (int)(g % 5), Body: (int)((g >> 8) % 3), Hat: FromHsv(hue + 0.6f + (g >> 16 & 0x3f) / 255f, 0.6f, 0.62f));
	}

	// Actors: four facings (north, east, south, west) times stand, walk A, walk B.
	private static readonly string[] FrontRows =
	[
		"................",
		".....oooooo.....",
		"....ohhhhhho....",
		"...ohhhhhhhho...",
		"...ohhhhhhhho...",
		"...ohssssssho...",
		"...osossssoso...",
		"....osssssso....",
		"....oaaaaaao....",
		"...occcaaccco...",
		"..oscccaacccso..",
		"..osdccccccdso..",
		"...obbbbbbbbo...",
		"....obo..obo....",
		"...okko..okko...",
		"...oooo..oooo...",
	];

	private static readonly string[] BackRows =
	[
		"................",
		".....oooooo.....",
		"....ohhhhhho....",
		"...ohhhhhhhho...",
		"...ohhhhhhhho...",
		"...ohhhhhhhho...",
		"...ohhhhhhhho...",
		"....ohhhhhho....",
		"....oaaaaaao....",
		"...occcccccco...",
		"..osccccccccso..",
		"..osdccccccdso..",
		"...obbbbbbbbo...",
		"....obo..obo....",
		"...okko..okko...",
		"...oooo..oooo...",
	];

	private static readonly string[] SideRows =
	[
		"................",
		".....ooooo......",
		"....ohhhhho.....",
		"...ohhhhhhho....",
		"...ohhhhhhho....",
		"...ohhhsssso....",
		"...ohhhssoso....",
		"....ohsssso.....",
		"...aoaaaaao.....",
		"..aaoccccco.....",
		"....occcsco.....",
		"....odcccdo.....",
		"....obbbbbo.....",
		".....obbo.......",
		".....okkko......",
		".....ooooo......",
	];

	private static string[] Legs(string[] rows, params string[] last3) => [.. rows[..13], .. last3];

	private enum View { Back, Side, Front }

	// Puts headwear and clothes over a template. The side view faces east, so its face is on the right.
	private static string[] Dress(string[] rows, ActorLook look, View view)
	{
		char[][] g = rows.Select(r => r.ToCharArray()).ToArray();
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
		void Set(int x, int y, char c, string over)
		{
			if (over.Contains(g[y][x]))
			{
				g[y][x] = c;
			}
		}
		switch (look.Head)
		{
		case 1:
			// A cap over the crown, its brim reaching out past the head (forward only, in the side view).
			Swap(2, 'h', 'p');
			Swap(3, 'h', 'p');
			Swap(4, 'h', 'q');
			int left = Array.IndexOf(g[4], 'o'), right = Array.LastIndexOf(g[4], 'o');
			if (view != View.Back)
			{
				g[4][right] = 'q';
				Set(right + 1, 4, 'o', ".");
				if (view == View.Front)
				{
					g[4][left] = 'q';
					Set(left - 1, 4, 'o', ".");
				}
			}
			break;
		case 2:
			// A hood: the hair turns to cloth, down to the shoulders.
			for (int y = 1; y <= 7; y++)
			{
				Swap(y, 'h', 'p');
			}
			Swap(8, 'a', 'q');
			break;
		case 3:
			// Hair down past the shoulders, behind the arms.
			foreach (int y in new[] { 8, 9, 10 })
			{
				if (view == View.Side)
				{
					Set(3, y, 'h', ".a");
					Set(2, y, 'o', ".a");
				}
				else
				{
					Set(3, y, 'h', ".o");
					Set(12, y, 'h', ".o");
					Set(2, y, 'o', ".");
					Set(13, y, 'o', ".");
				}
			}
			break;
		case 4:
			Swap(4, 'h', 'p');
			break;
		}
		switch (look.Body)
		{
		case 1:
			// An apron over the front of the coat.
			if (view != View.Back)
			{
				int x0 = view == View.Front ? 5 : 7, x1 = view == View.Front ? 10 : 9;
				for (int y = 9; y <= 12; y++)
				{
					Swap(y, 'c', 'w', x0, x1);
					Swap(y, 'd', 'w', x0, x1);
					Swap(y, 'a', 'w', x0, x1);
				}
			}
			break;
		case 2:
			Swap(11, 'c', 'k');
			Swap(11, 'd', 'k');
			if (view == View.Front)
			{
				g[11][7] = g[11][8] = 'y';
			}
			break;
		}
		return g.Select(r => new string(r)).ToArray();
	}

	private static Func<char, Color> ActorPalette(ActorLook look) => c => c switch
	{
		'o' => Outline,
		'h' => look.Hair,
		'g' => Shade(look.Hair, 0.78f),
		's' => look.Skin,
		'c' => look.Coat,
		'd' => Shade(look.Coat, 0.68f),
		'a' => look.Scarf,
		'b' => new Color(60, 58, 84),
		'k' => new Color(74, 46, 32),
		'p' => look.Hat,
		'q' => Shade(look.Hat, 0.7f),
		'w' => new Color(236, 230, 214),
		'y' => new Color(232, 196, 92),
		_ => Color.Magenta,
	};

	public int[] Actor(ActorLook look)
	{
		if (_actors.TryGetValue(look, out int[]? frames))
		{
			return frames;
		}
		Func<char, Color> palette = ActorPalette(look);
		string[] backRows = BackRows;
		string[][] front =
		[
			FrontRows,
			Legs(FrontRows, "....obo..okko...", "...okko..oooo...", "...oooo........."),
			Legs(FrontRows, "...okko..obo....", "...oooo..okko...", ".........oooo..."),
		];
		string[][] back =
		[
			backRows,
			Legs(backRows, "....obo..okko...", "...okko..oooo...", "...oooo........."),
			Legs(backRows, "...okko..obo....", "...oooo..okko...", ".........oooo..."),
		];
		string[] stride = Legs(SideRows, "....obo.obo.....", "...okko.okko....", "...oooo.oooo....");
		string[][] side = [SideRows, stride, SideRows];
		frames = new int[12];
		for (int f = 0; f < 3; f++)
		{
			Pixels east = Pixels.FromTemplate(Dress(side[f], look, View.Side), palette);
			frames[0 * 3 + f] = Add(Pixels.FromTemplate(Dress(back[f], look, View.Back), palette));
			frames[1 * 3 + f] = Add(east);
			frames[2 * 3 + f] = Add(Pixels.FromTemplate(Dress(front[f], look, View.Front), palette));
			frames[3 * 3 + f] = Add(east.MirrorX());
		}
		_actors[look] = frames;
		return frames;
	}

	public int ActorFrame(ActorLook look, Direction facing, int frame) => Actor(look)[(facing == Direction.None ? 2 : (int)facing) * 3 + frame];

	// Script animation names the sample knows, and the pose each plays. Any other name plays Pose.Other.
	private static readonly Dictionary<string, Pose> PoseNames = new()
	{
		["blink_out"] = Pose.BlinkOut,
		["bow"] = Pose.Bow,
		["dissolve"] = Pose.Dissolve,
		["fade_in"] = Pose.FadeIn,
		["fade_out"] = Pose.FadeOut,
		["hop"] = Pose.Jump,
		["jump"] = Pose.Jump,
		["nod"] = Pose.Nod,
		["shake"] = Pose.Shake,
		["startle"] = Pose.Startle,
		["tremble"] = Pose.Tremble,
		["wave"] = Pose.Wave,
	};

	public static Pose PoseFor(string animation) => PoseNames.GetValueOrDefault(animation, Pose.Other);

	// One play of a pose; a looping animation repeats it.
	public static int PoseLengthMs(Pose pose) => pose switch
	{
		Pose.Nod => 640,
		Pose.Shake => 600,
		Pose.Jump => 480,
		Pose.Startle => 560,
		Pose.Bow => 900,
		Pose.Wave => 840,
		Pose.FadeIn => 400,
		Pose.FadeOut => 500,
		Pose.Dissolve => 700,
		Pose.BlinkOut => 640,
		Pose.Tremble => 600,
		_ => 520,
	};

	// The poses that change how the whole sprite shows rather than how the person stands, at `t` (0 to 1 through one
	// play): its opacity, a sideways shift in field pixels, and how far it has broken up into rising motes (0 to 1). The
	// ones that end out of sight leave the sprite undrawn once played, until the scene ends or another pose starts.
	public static (float Alpha, int Dx, float Scatter) SpriteLook(Pose pose, float t) => pose switch
	{
		Pose.FadeIn => (Math.Clamp(t, 0f, 1f), 0, 0f),
		Pose.FadeOut => (1f - Math.Clamp(t, 0f, 1f), 0, 0f),
		Pose.Dissolve => (1f - Math.Clamp(t, 0f, 1f), 0, Math.Clamp(t, 0f, 1f)),
		// On and off, faster and faster.
		Pose.BlinkOut => (t >= 1f || (int)(t * t * 14) % 2 == 1 ? 0f : 1f, 0, 0f),
		// Side to side, easing off.
		Pose.Tremble => (1f, (int)MathF.Round(MathF.Sin(t * MathF.PI * 16) * 3 * (1f - 0.5f * t)), 0f),
		_ => (1f, 0, 0f),
	};

	public static bool Vanishes(Pose pose) => pose is Pose.FadeOut or Pose.Dissolve or Pose.BlinkOut;

	// A person in a pose at `t` (0 to 1 through one play): the picture, a lift in screen pixels, whether a startled
	// mark shows above the head, and the motion marks drawn around the head. Poses keep the person's facing and only
	// move the head, arms or body.
	public (int Image, int Lift, bool Alert, MotionMark[] Marks) PoseFrame(ActorLook look, Direction facing, Pose pose, float t)
	{
		static int Hop(float t, float height) => (int)MathF.Round(MathF.Sin(MathF.PI * Math.Clamp(t, 0f, 1f)) * height);
		return pose switch
		{
			// Two dips of the head, each down halfway, all the way and back, with a stroke beside each side of the crown
			// the whole time, so a still frame between dips still reads as a nod.
			Pose.Nod => (PoseImage(look, facing, pose, NodPhases[(int)(t * 8) % 4]), 0, false, _nodMarks),
			// The head turns to one side and the other three times, with an arc on each side of it.
			Pose.Shake => (PoseImage(look, facing, pose, t >= 1f ? 0 : 1 + (int)(t * 6) % 2), 0, false, _shakeMarks),
			// One hop, standing straight.
			Pose.Jump => (ActorFrame(look, facing, 0), Hop(t, 15), false, []),
			// A quick hop with both arms up, and the mark above the head.
			Pose.Startle => (PoseImage(look, facing, pose, 1), Hop(t * 3.5f, 9), true, []),
			// Down, held, and up again.
			Pose.Bow => (PoseImage(look, facing, pose, t is > 0.15f and < 0.85f ? 2 : t is > 0.05f and < 0.95f ? 1 : 0), 0, false, []),
			// An arm up, the hand swinging.
			Pose.Wave => (PoseImage(look, facing, pose, 1 + (int)(t * 6) % 2), 0, false, []),
			// The sprite's own looks (SpriteLook) stand still.
			Pose.FadeIn or Pose.FadeOut or Pose.Dissolve or Pose.BlinkOut or Pose.Tremble => (ActorFrame(look, facing, 0), 0, false, []),
			// Anything else: two small bounces.
			_ => (ActorFrame(look, facing, 0), Hop(t * 2f % 1f, 5), false, []),
		};
	}

	private static readonly int[] NodPhases = [0, 1, 2, 1];

	// PoseFrame's pictures as frames for one play: each stretch's picture phase (0 the standing frame) and milliseconds.
	public static (int[] Phases, int[] Ms) PoseTimeline(Pose pose) => pose switch
	{
		Pose.Nod => ([0, 1, 2, 1, 0, 1, 2, 1], [80, 80, 80, 80, 80, 80, 80, 80]),
		Pose.Shake => ([1, 2, 1, 2, 1, 2], [100, 100, 100, 100, 100, 100]),
		Pose.Startle => ([1], [560]),
		Pose.Bow => ([0, 1, 2, 1, 0], [46, 90, 629, 90, 45]),
		Pose.Wave => ([1, 2, 1, 2, 1, 2], [140, 140, 140, 140, 140, 140]),
		_ => ([0], [PoseLengthMs(pose)]),
	};

	public int GeneratedPoseImage(string id, Direction facing, Pose pose, int phase) => PoseImage(LookFor(id), facing, pose, phase);

	private readonly Dictionary<(ActorLook, Direction, Pose, int), int> _poses = [];

	// Phase 0 is always the standing frame.
	private int PoseImage(ActorLook look, Direction facing, Pose pose, int phase)
	{
		facing = facing == Direction.None ? Direction.South : facing;
		if (phase == 0)
		{
			return ActorFrame(look, facing, 0);
		}
		if (_poses.TryGetValue((look, facing, pose, phase), out int image))
		{
			return image;
		}
		View view = facing switch { Direction.North => View.Back, Direction.South => View.Front, _ => View.Side };
		char[][] g = Dress(view switch { View.Back => BackRows, View.Front => FrontRows, _ => SideRows }, look, view).Select(r => r.ToCharArray()).ToArray();
		switch (pose)
		{
		case Pose.Nod:
			if (phase == 2)
			{
				// All the way down the head tips forward: from the front the hair comes down over the brow, from the
				// side the face leans a pixel ahead.
				if (view == View.Front)
				{
					int left = Array.IndexOf(g[5], 'o'), right = Array.LastIndexOf(g[5], 'o');
					for (int x = left + 1; x < right; x++)
					{
						g[5][x] = 'h';
					}
				}
				g = ShiftRows(g, 1, 7, view == View.Side ? 1 : 0, 2);
			}
			else
			{
				g = ShiftRows(g, 1, 7, 0, 1);
			}
			break;
		case Pose.Shake:
			// The head turns: from the front or the back it shows its profile to one side and then the other, from the
			// side it turns to face the viewer and back, and it swings a pixel with each turn.
			char[][] profile = Dress(SideRows, look, View.Side).Select(r => r.ToCharArray()).ToArray();
			char[][] head = view == View.Side
				? (phase == 1 ? Dress(FrontRows, look, View.Front).Select(r => r.ToCharArray()).ToArray() : profile)
				: phase == 1 ? profile.Select(r => r.Reverse().ToArray()).ToArray() : profile;
			for (int y = 1; y <= 7; y++)
			{
				g[y] = (char[])head[y].Clone();
			}
			g = ShiftRows(g, 1, 7, phase == 1 ? -1 : 1, 0);
			break;
		case Pose.Bow:
			if (view == View.Front && phase == 2)
			{
				// Bent right over, the face turns away and the top of the head shows.
				foreach (int y in new[] { 5, 6 })
				{
					int left = Array.IndexOf(g[y], 'o'), right = Array.LastIndexOf(g[y], 'o');
					for (int x = left + 1; x < right; x++)
					{
						g[y][x] = 'h';
					}
				}
			}
			g = ShiftRows(ShiftRows(g, 8, 11, 0, phase - 1), 1, 7, 0, phase);
			break;
		case Pose.Startle:
			if (view == View.Side)
			{
				RaiseArm(g, 12, 10, 8, false);
			}
			else
			{
				RaiseArm(g, 13, 10, 12, false);
				RaiseArm(g, 2, 10, 3, false);
			}
			break;
		case Pose.Wave:
			if (view == View.Side)
			{
				RaiseArm(g, 12, 10, 8, phase == 2);
			}
			else
			{
				RaiseArm(g, 13, 10, 12, phase == 2);
			}
			break;
		}
		Pixels p = Pixels.FromTemplate(g.Select(r => new string(r)).ToArray(), ActorPalette(look));
		image = Add(facing == Direction.West ? p.MirrorX() : p);
		_poses[(look, facing, pose, phase)] = image;
		return image;
	}

	// Rows y0 to y1 moved by (dx, dy): their old places cleared, the moved pixels drawn over whatever is there.
	private static char[][] ShiftRows(char[][] g, int y0, int y1, int dx, int dy)
	{
		char[][] o = g.Select(r => (char[])r.Clone()).ToArray();
		for (int y = y0; y <= y1; y++)
		{
			Array.Fill(o[y], '.');
		}
		for (int y = y0; y <= y1; y++)
		{
			for (int x = 0; x < Pixels.Size; x++)
			{
				int nx = x + dx, ny = y + dy;
				if (g[y][x] != '.' && nx is >= 0 and < Pixels.Size && ny is >= 0 and < Pixels.Size)
				{
					o[ny][nx] = g[y][x];
				}
			}
		}
		return o;
	}

	// An arm raised from the shoulder in column x, its hanging hand (column `hand`, from row `handY`) covered by the
	// coat; `tilt` swings the hand out one pixel.
	private static void RaiseArm(char[][] g, int x, int handY, int hand, bool tilt)
	{
		g[handY][hand] = 'd';
		g[handY + 1][hand] = g[handY + 1][hand] == 's' ? 'd' : g[handY + 1][hand];
		void Put(int px, int py, char c)
		{
			if (px is >= 0 and < Pixels.Size && py is >= 0 and < Pixels.Size)
			{
				g[py][px] = c;
			}
		}
		void Edge(int px, int py)
		{
			if (px is >= 0 and < Pixels.Size && py is >= 0 and < Pixels.Size && g[py][px] == '.')
			{
				g[py][px] = 'o';
			}
		}
		int side = x < Pixels.Size / 2 ? -1 : 1;
		for (int y = 5; y <= 9; y++)
		{
			Put(x, y, 'c');
			Edge(x - 1, y);
			Edge(x + 1, y);
		}
		int hx = tilt ? x + side : x;
		Put(hx, 3, 's');
		Put(hx, 4, 's');
		Edge(hx, 2);
		Edge(hx - 1, 3);
		Edge(hx + 1, 3);
		Edge(hx - 1, 4);
		Edge(hx + 1, 4);
	}

	private static readonly string[] CreatureRows =
	[
		"................",
		"................",
		"................",
		"......oooo......",
		"....oommmmoo....",
		"...ommmmmmmmo...",
		"..ommlmmmmmmmo..",
		"..omllmmmmmmmo..",
		".ommmwommwommmo.",
		".ommmoommoommmo.",
		".ommmmmmmmmmmmo.",
		".ommmmmmmmmmmmo.",
		"..oddmmmmmmddo..",
		"...oddddddddo...",
		"....oooooooo....",
		"................",
	];

	// A field or battle creature; its colors and horns come from `seed` (the encounter or enemy id), frame 0 or 1 bobs.
	public int Creature(uint seed, int frame)
	{
		if (_creatures.TryGetValue((seed, frame), out int image))
		{
			return image;
		}
		Color body = FromHsv((seed & 0xff) / 255f, 0.55f, 0.82f);
		Color Palette(char c) => c switch
		{
			'o' => Outline,
			'm' => body,
			'l' => Color.Lerp(body, Color.White, 0.55f),
			'd' => Shade(body, 0.62f),
			'w' => new Color(250, 248, 236),
			_ => Color.Magenta,
		};
		Pixels p = Pixels.FromTemplate(CreatureRows, Palette);
		if ((seed & 0x100) != 0)
		{
			Color horn = new(236, 226, 196);
			foreach ((int x, int y) in new[] { (4, 2), (4, 3), (11, 2), (11, 3) })
			{
				p[x, y] = horn;
				p[x, y - 1] = Outline;
			}
		}
		if ((seed & 0x200) != 0)
		{
			p.Rect(6, 11, 4, 1, Outline);
			p[7, 12] = new Color(250, 248, 236);
		}
		if (frame == 1)
		{
			p = p.Shift(1);
			p.Rect(1, 14, 14, 1, Outline);
			p.Rect(2, 13, 12, 1, Shade(body, 0.62f));
		}
		image = Add(p);
		_creatures[(seed, frame)] = image;
		return image;
	}

	private TileSet BuildTiles(Theme theme, int door, int chestClosed, int chestOpen, int stall, int bed, int sign, int rune, int[] exitArrow)
	{
		int seed = (int)theme * 1000;
		(Color edge, Color bank) = DefaultEdges(theme);
		return theme switch
		{
			Theme.Field => new TileSet
			{
				Floor = [.. Enumerable.Range(0, 4).Select(i => Add(Grass(seed + i)))],
				Decor = [Add(Flowers(seed + 10, new Color(250, 240, 230))), Add(Flowers(seed + 11, new Color(240, 130, 170))), Add(Pebble(Grass(seed + 12), seed + 12)), Add(Tuft(seed + 13))],
				WallTop = [Add(Canopy(seed + 20)), Add(Canopy(seed + 21))],
				WallFace = [Add(HedgeFace(seed + 30)), Add(HedgeFace(seed + 31))],
				Water = [.. Enumerable.Range(0, WaterFrames).Select(f => Add(Water(f, new Color(56, 120, 196))))],
				Edge = edge,
				Bank = bank,
				Door = door, ChestClosed = chestClosed, ChestOpen = chestOpen, Stall = stall, Bed = bed, Sign = sign, Rune = rune, ExitArrow = exitArrow,
			},
			Theme.Town => new TileSet
			{
				Floor = [.. Enumerable.Range(0, 4).Select(i => Add(Cobbles(seed + i)))],
				Decor = [Add(Crack(Cobbles(seed + 10), seed + 10)), Add(Moss(Cobbles(seed + 11), seed + 11)), Add(Grate(Cobbles(seed + 12))), Add(Pebble(Cobbles(seed + 13), seed + 13)),
					Add(Crack(Cobbles(seed + 14), seed + 14)), Add(Moss(Cobbles(seed + 15), seed + 15))],
				WallTop = [Add(Roof(seed + 20)), Add(Roof(seed + 21))],
				WallFace = [Add(Plaster(seed + 30, window: false)), Add(Plaster(seed + 31, window: true))],
				Water = [.. Enumerable.Range(0, WaterFrames).Select(f => Add(Water(f, new Color(48, 108, 180))))],
				Edge = edge,
				Bank = bank,
				Door = door, ChestClosed = chestClosed, ChestOpen = chestOpen, Stall = stall, Bed = bed, Sign = sign, Rune = rune, ExitArrow = exitArrow,
			},
			_ => new TileSet
			{
				Floor = [.. Enumerable.Range(0, 4).Select(i => Add(Flagstones(seed + i)))],
				Decor = [Add(Crack(Flagstones(seed + 10), seed + 10)), Add(Moss(Flagstones(seed + 11), seed + 11)), Add(Pebble(Flagstones(seed + 12), seed + 12)), Add(Pebble(Flagstones(seed + 13), seed + 14))],
				WallTop = [Add(Rock(seed + 20)), Add(Rock(seed + 21))],
				WallFace = [Add(Bricks(seed + 30)), Add(Bricks(seed + 31))],
				Water = [.. Enumerable.Range(0, WaterFrames).Select(f => Add(Water(f, new Color(36, 82, 142))))],
				Edge = edge,
				Bank = bank,
				Door = door, ChestClosed = chestClosed, ChestOpen = chestOpen, Stall = stall, Bed = bed, Sign = sign, Rune = rune, ExitArrow = exitArrow,
			},
		};
	}

	private static Pixels Grass(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 1));
		Color base_ = new(80, 152, 74);
		var p = new Pixels().Fill(base_);
		p.Speckle(ref rng, 26, new Color(66, 132, 62)).Speckle(ref rng, 14, new Color(100, 176, 88));
		for (int i = 0; i < 4; i++)
		{
			int x = rng.Range(15), y = 2 + rng.Range(13);
			p[x, y] = new Color(118, 192, 98);
			p[x, y - 1] = new Color(118, 192, 98);
			p[x + 1, y] = new Color(58, 118, 56);
		}
		return p;
	}

	private static Pixels Flowers(int seed, Color petal)
	{
		Pixels p = Grass(seed);
		var rng = new Rng(Noise.Hash(seed, 2));
		for (int i = 0; i < 3; i++)
		{
			int x = 2 + rng.Range(12), y = 2 + rng.Range(12);
			p[x - 1, y] = petal;
			p[x + 1, y] = petal;
			p[x, y - 1] = petal;
			p[x, y + 1] = petal;
			p[x, y] = new Color(250, 210, 70);
		}
		return p;
	}

	private static Pixels Tuft(int seed)
	{
		Pixels p = Grass(seed);
		Color dark = new(46, 104, 50), light = new(124, 196, 100);
		for (int x = 4; x < 12; x += 2)
		{
			int h = 3 + (x * 7 + seed) % 3;
			for (int y = 0; y < h; y++)
			{
				p[x + (y > 2 ? 0 : (x - 8) / 4), 12 - y] = y == h - 1 ? light : dark;
			}
		}
		return p;
	}

	private static Pixels Pebble(Pixels p, int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 3));
		int cx = 4 + rng.Range(7), cy = 5 + rng.Range(6);
		p.Rect(cx, cy, 4, 3, new Color(150, 146, 150));
		p.Rect(cx + 1, cy - 1, 2, 1, new Color(150, 146, 150));
		p.Rect(cx, cy + 3, 4, 1, new Color(70, 66, 74));
		p[cx + 1, cy] = new Color(204, 200, 204);
		return p;
	}

	private static Pixels Cobbles(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 4));
		var p = new Pixels().Fill(new Color(116, 108, 102));
		Color[] tones = [new(170, 160, 146), new(158, 150, 138), new(180, 172, 156), new(150, 140, 130)];
		for (int row = 0; row < 4; row++)
		{
			int offset = row % 2 == 0 ? 0 : 2 + rng.Range(2);
			for (int x = -offset; x < 16; x += 5 + rng.Range(2))
			{
				int w = 4 + rng.Range(1);
				Color c = tones[rng.Range(tones.Length)];
				p.Rect(x + 1, row * 4 + 1, w - 1, 2, c);
				p.Rect(x + 1, row * 4 + 3, w - 1, 1, Shade(c, 0.78f));
				p[x + 1, row * 4 + 1] = Color.Lerp(c, Color.White, 0.3f);
			}
		}
		return p;
	}

	private static Pixels Flagstones(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 5));
		var p = new Pixels().Fill(new Color(58, 54, 66));
		for (int sy = 0; sy < 2; sy++)
		{
			for (int sx = 0; sx < 2; sx++)
			{
				int tone = 80 + rng.Range(16);
				Color c = new(tone, tone - 4, tone + 10);
				p.Rect(sx * 8 + 1, sy * 8 + 1, 7, 7, c);
				p.Rect(sx * 8 + 1, sy * 8 + 1, 7, 1, Shade(c, 1.18f));
				p.Speckle(ref rng, 3, Shade(c, 0.86f));
			}
		}
		return p;
	}

	private static Pixels Crack(Pixels p, int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 6));
		int x = 3 + rng.Range(5), y = 3 + rng.Range(4);
		Color dark = new(40, 36, 44);
		for (int i = 0; i < 9; i++)
		{
			p[x, y] = dark;
			x += rng.Range(3) - (i % 2 == 0 ? 0 : 1);
			y += 1;
		}
		return p;
	}

	private static Pixels Moss(Pixels p, int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 7));
		int cx = 3 + rng.Range(8), cy = 3 + rng.Range(8);
		for (int i = 0; i < 18; i++)
		{
			p[cx + rng.Range(5) - 2, cy + rng.Range(5) - 2] = i % 3 == 0 ? new Color(110, 150, 84) : new Color(78, 118, 66);
		}
		return p;
	}

	private static Pixels Grate(Pixels p)
	{
		p.Rect(4, 4, 8, 8, new Color(46, 44, 50));
		for (int x = 5; x < 12; x += 2)
		{
			p.Rect(x, 5, 1, 6, new Color(20, 20, 26));
		}
		p.Rect(4, 4, 8, 1, new Color(120, 118, 126));
		return p;
	}

	private static Pixels Canopy(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 8));
		var p = new Pixels().Fill(new Color(38, 96, 52));
		for (int i = 0; i < 7; i++)
		{
			int cx = rng.Range(16), cy = rng.Range(16);
			for (int y = -2; y <= 2; y++)
			{
				for (int x = -2; x <= 2; x++)
				{
					if (x * x + y * y <= 5)
					{
						p[(cx + x + 16) % 16, (cy + y + 16) % 16] = y < 0 ? new Color(70, 140, 70) : new Color(54, 120, 62);
					}
				}
			}
		}
		p.Speckle(ref rng, 12, new Color(26, 72, 40));
		return p;
	}

	private static Pixels HedgeFace(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 9));
		var p = new Pixels().Fill(new Color(36, 88, 46));
		p.Rect(0, 0, 16, 5, new Color(54, 120, 62));
		p.Rect(0, 0, 16, 1, new Color(84, 156, 80));
		for (int x = 0; x < 16; x++)
		{
			int len = 2 + rng.Range(4);
			for (int y = 5; y < 5 + len; y++)
			{
				p[x, y] = (x + y) % 3 == 0 ? new Color(48, 108, 56) : p[x, y];
			}
		}
		p.Rect(0, 12, 16, 4, new Color(26, 62, 34));
		p.Rect(0, 15, 16, 1, new Color(16, 40, 22));
		p.Speckle(ref rng, 6, new Color(24, 60, 32));
		return p;
	}

	private static Pixels Roof(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 10));
		var p = new Pixels().Fill(new Color(86, 88, 142));
		for (int row = 0; row < 4; row++)
		{
			p.Rect(0, row * 4 + 3, 16, 1, new Color(56, 56, 100));
			int offset = row % 2 == 0 ? 0 : 3;
			for (int x = offset; x < 16; x += 6)
			{
				p.Rect(x, row * 4, 1, 3, new Color(64, 64, 112));
				p[x + 1, row * 4] = new Color(116, 120, 176);
			}
		}
		p.Speckle(ref rng, 5, new Color(100, 104, 160));
		return p;
	}

	private static Pixels Plaster(int seed, bool window)
	{
		var rng = new Rng(Noise.Hash(seed, 11));
		var p = new Pixels().Fill(new Color(226, 212, 182));
		p.Speckle(ref rng, 10, new Color(212, 196, 166));
		Color timber = new(112, 78, 54);
		p.Rect(0, 0, 16, 2, timber);
		p.Rect(0, 0, 1, 13, timber);
		p.Rect(0, 13, 16, 3, new Color(138, 128, 120));
		p.Rect(0, 15, 16, 1, new Color(92, 86, 82));
		if (window)
		{
			p.Rect(5, 4, 6, 6, timber);
			p.Rect(6, 5, 4, 4, new Color(56, 80, 124));
			p.Rect(6, 5, 2, 1, new Color(140, 176, 220));
			p.Rect(7, 5, 1, 4, timber);
			p.Rect(4, 10, 8, 1, timber);
		}
		return p;
	}

	private static Pixels Rock(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 12));
		var p = new Pixels().Fill(new Color(52, 48, 62));
		p.Speckle(ref rng, 22, new Color(66, 62, 78)).Speckle(ref rng, 16, new Color(40, 36, 48));
		for (int i = 0; i < 3; i++)
		{
			int x = rng.Range(14), y = rng.Range(14);
			p.Rect(x, y, 3, 2, new Color(72, 68, 84));
			p[x, y] = new Color(92, 88, 104);
		}
		return p;
	}

	private static Pixels Bricks(int seed)
	{
		var rng = new Rng(Noise.Hash(seed, 13));
		var p = new Pixels().Fill(new Color(62, 56, 70));
		for (int row = 0; row < 4; row++)
		{
			int offset = row % 2 == 0 ? 0 : 4;
			for (int x = -offset; x < 16; x += 8)
			{
				int tone = 96 + rng.Range(18);
				Color c = new(tone + 4, tone - 4, tone + 12);
				p.Rect(x + 1, row * 4, 7, 3, c);
				p.Rect(x + 1, row * 4, 7, 1, Shade(c, 1.15f));
			}
		}
		p.Rect(0, 0, 16, 1, new Color(140, 134, 150));
		p.Rect(0, 14, 16, 2, new Color(36, 32, 42));
		return p;
	}

	private static Pixels Water(int frame, Color deep)
	{
		var p = new Pixels().Fill(deep);
		Color light = Color.Lerp(deep, Color.White, 0.55f);
		Color mid = Color.Lerp(deep, Color.White, 0.22f);
		Color dark = Shade(deep, 0.82f);
		for (int y = 0; y < 16; y++)
		{
			float phase = y / 4 * 1.7f;
			for (int x = 0; x < 16; x++)
			{
				double v = Math.Sin((x + frame * 4) / 16.0 * Math.PI * 2 + phase);
				if (y % 4 == 1 && v > 0.8)
				{
					p[x, y] = light;
				}
				else if (y % 4 == 2 && v > 0.45)
				{
					p[x, y] = mid;
				}
				else if (y % 4 == 3 && v < -0.6)
				{
					p[x, y] = dark;
				}
			}
		}
		return p;
	}

	private static Pixels Arrow()
	{
		var p = new Pixels();
		Color body = new(150, 250, 176), edge = new(30, 96, 60);
		foreach (int start in new[] { 3, 8 })
		{
			for (int i = 0; i < 5; i++)
			{
				p.Rect(start + i - 1, 3 + i, 3, 1, edge);
				p.Rect(start + i - 1, 12 - i, 3, 1, edge);
				p.Rect(start + i, 3 + i, 1, 1, body);
				p.Rect(start + i, 12 - i, 1, 1, body);
			}
			p.Rect(start + 4, 7, 2, 2, body);
		}
		return p;
	}

	private static Pixels BuildDoor()
	{
		var p = new Pixels();
		p.Rect(2, 1, 12, 15, new Color(84, 78, 92));
		p.Rect(3, 2, 10, 14, Outline);
		p.Rect(4, 3, 8, 13, new Color(16, 12, 20));
		for (int i = 0; i < 3; i++)
		{
			p.Rect(4, 9 + i * 2, 8, 1, new Color(60, 54, 70));
		}
		return p;
	}

	private static Pixels BuildChest(bool open)
	{
		var p = new Pixels();
		Color wood = new(150, 92, 46), lid = new(180, 114, 58), light = new(214, 152, 82), metal = new(226, 190, 92);
		if (open)
		{
			p.Rect(2, 2, 12, 4, Outline);
			p.Rect(3, 3, 10, 2, lid);
			p.Rect(2, 6, 12, 3, Outline);
			p.Rect(3, 6, 10, 3, new Color(64, 36, 22));
			p[7, 7] = new Color(252, 236, 150);
		}
		else
		{
			p.Rect(2, 4, 12, 5, Outline);
			p.Rect(3, 5, 10, 3, lid);
			p.Rect(3, 5, 10, 1, light);
		}
		p.Rect(2, 8, 12, 7, Outline);
		p.Rect(3, 9, 10, 5, wood);
		p.Rect(3, 9, 10, 1, metal);
		p.Rect(4, 9, 1, 5, Shade(wood, 0.8f));
		p.Rect(11, 9, 1, 5, Shade(wood, 0.8f));
		if (!open)
		{
			p.Rect(7, 7, 2, 4, Outline);
			p.Rect(7, 8, 2, 2, metal);
		}
		p.Rect(3, 15, 10, 1, new Color(0, 0, 0) * 0.35f);
		return p;
	}

	private static Pixels BuildStall()
	{
		var p = new Pixels();
		Color a = new(226, 96, 84), b = new(246, 236, 220), post = new(120, 84, 56), counter = new(152, 104, 64);
		for (int x = 1; x < 15; x++)
		{
			p.Rect(x, 1, 1, 5, (x / 2) % 2 == 0 ? a : b);
			if (x % 2 == 1)
			{
				p[x, 6] = (x / 2) % 2 == 0 ? a : b;
			}
		}
		p.Rect(1, 0, 14, 1, Outline);
		p.Rect(2, 6, 1, 4, post);
		p.Rect(13, 6, 1, 4, post);
		p.Rect(1, 9, 14, 6, Outline);
		p.Rect(2, 10, 12, 4, counter);
		p.Rect(2, 10, 12, 1, new Color(192, 142, 92));
		p.Rect(5, 8, 2, 2, new Color(250, 214, 80));
		p.Rect(9, 8, 2, 2, new Color(250, 214, 80));
		p[5, 8] = new Color(255, 246, 190);
		p[9, 8] = new Color(255, 246, 190);
		return p;
	}

	private static Pixels BuildBed()
	{
		var p = new Pixels();
		p.Rect(2, 1, 12, 15, Outline);
		p.Rect(3, 2, 10, 13, new Color(122, 82, 52));
		p.Rect(4, 2, 8, 4, new Color(240, 240, 232));
		p.Rect(4, 5, 8, 1, new Color(200, 200, 196));
		p.Rect(3, 6, 10, 8, new Color(92, 112, 204));
		p.Rect(3, 6, 10, 2, new Color(146, 164, 232));
		p.Rect(3, 13, 10, 1, new Color(64, 78, 150));
		return p;
	}

	private static Pixels BuildRune()
	{
		var p = new Pixels();
		Color c = new Color(190, 226, 255) * 0.85f;
		for (int i = 0; i < 6; i++)
		{
			p[8 + i, 2 + i] = c;
			p[8 - i, 2 + i] = c;
			p[8 + i, 13 - i] = c;
			p[8 - i, 13 - i] = c;
		}
		p.Rect(7, 7, 2, 2, c);
		return p;
	}

	private static Pixels BuildBubble()
	{
		var p = new Pixels();
		p.Rect(2, 2, 12, 9, Outline);
		p.Rect(1, 3, 14, 7, Outline);
		p.Rect(2, 3, 12, 7, new Color(250, 250, 244));
		p.Rect(3, 2, 10, 1, new Color(250, 250, 244));
		p.Rect(3, 10, 10, 1, new Color(250, 250, 244));
		p.Rect(5, 11, 3, 1, Outline);
		p.Rect(6, 12, 2, 1, Outline);
		p[6, 11] = new Color(250, 250, 244);
		foreach (int x in new[] { 4, 7, 10 })
		{
			p.Rect(x, 6, 2, 2, new Color(60, 60, 80));
		}
		return p;
	}

	private static Pixels BuildNoticeboard()
	{
		string[] rows =
		[
			"................",
			"................",
			"..oooooooooooo..",
			".obbbbbbbbbbbbo.",
			".obwwwwwwwwwwbo.",
			".obwkkkwkkkwwbo.",
			".obwwwwwwwwwwbo.",
			".obwkkwkkkkwwbo.",
			".obddddddddddbo.",
			"..oooooddooooo..",
			"......oddo......",
			"......oddo......",
			"......oddo......",
			".....oddddo.....",
			"....oooooooo....",
			"................",
		];
		Color Palette(char c) => c switch
		{
			'o' => Outline,
			'b' => new Color(168, 116, 64),
			'd' => new Color(112, 74, 42),
			'w' => new Color(240, 230, 204),
			'k' => new Color(96, 84, 92),
			_ => Color.Magenta,
		};
		return Pixels.FromTemplate(rows, Palette);
	}

	private static Pixels BuildAlert()
	{
		var p = new Pixels();
		p.Rect(5, 1, 6, 14, Outline);
		p.Rect(6, 2, 4, 8, new Color(255, 214, 70));
		p.Rect(6, 11, 4, 3, new Color(255, 214, 70));
		return p;
	}

	// Light strokes with a dark edge, at (x, y) pairs.
	private static Pixels Strokes(params (int X, int Y)[] points)
	{
		var p = new Pixels();
		foreach ((int x, int y) in points)
		{
			for (int dy = -1; dy <= 1; dy++)
			{
				for (int dx = -1; dx <= 1; dx++)
				{
					p[x + dx, y + dy] = Outline;
				}
			}
		}
		foreach ((int x, int y) in points)
		{
			p[x, y] = new Color(250, 250, 244);
		}
		return p;
	}

	// The left half of a nod's marks, drawn 3 pixels left of and above the person: an upright stroke beside the
	// crown, where the head dips from.
	private static Pixels BuildNodMark() => Strokes((4, 3), (4, 4), (4, 5));

	// The left half of a shake's marks, drawn 2 pixels left of the person: an arc beside the head.
	private static Pixels BuildShakeMark() => Strokes((2, 3), (1, 4), (1, 5), (2, 6));

	private static Pixels BuildShadow()
	{
		var p = new Pixels();
		for (int y = 11; y < 16; y++)
		{
			float dy = (y - 13.5f) / 2.5f;
			for (int x = 0; x < 16; x++)
			{
				float dx = (x - 7.5f) / 6.5f;
				if (dx * dx + dy * dy <= 1f)
				{
					p[x, y] = Color.Black * 0.32f;
				}
			}
		}
		return p;
	}

	private static Pixels BuildPointer()
	{
		var p = new Pixels();
		for (int i = 0; i < 7; i++)
		{
			p.Rect(2 + i, 4 + i, 12 - 2 * i, 1, Outline);
			if (i < 6)
			{
				p.Rect(3 + i, 4 + i, 10 - 2 * i, 1, new Color(255, 220, 96));
			}
		}
		return p;
	}
}
