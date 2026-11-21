using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Frames of one picture set: idle and named animations are runs over Images, by index.
public sealed record Figure(int[] Images, FrameRun Idle, IReadOnlyDictionary<string, FrameRun> Animations);

// A window frame cut in nine: corners drawn at their size times Scale, edges and middle stretched.
public sealed record NineSliceImage(int[] Images, int Border, int Scale);

// The UI skin from the asset directory: any part left out keeps the generated look, drawn with rectangles.
public sealed record UiSkin(NineSliceImage? Frame, NineSliceImage? PanelFrame, IReadOnlyDictionary<string, NineSliceImage> CameraFrames, int? Cursor, int? BarTrack, int? BarFill);

// The pictures the asset directory's manifest names, cut into the same images the generated ones are, so every screen
// draws them the same way. Loaded once when the art is built; FromFile says which ids they replaced.
public sealed partial class Art
{
	private readonly Dictionary<string, (int W, int H, Color[] Pixels)> _files = [];
	private readonly Dictionary<string, TileSet> _areaTiles = [];
	private readonly Dictionary<string, (CharacterEntry Entry, int[] Images)> _people = [];
	private readonly Dictionary<string, int> _portraits = [];
	private readonly Dictionary<(string, bool), Figure> _figures = [];
	private readonly Dictionary<string, Figure> _backFigures = [];
	private readonly Dictionary<string, Figure> _fieldEnemies = [];
	private readonly Dictionary<string, int> _backdrops = [];
	private readonly Dictionary<string, Figure> _effects = [];
	private readonly Dictionary<string, int> _statusIcons = [];
	private readonly Dictionary<int, (int W, int H)> _sizes = [];
	private readonly Dictionary<string, string> _fromFile = [];
	private readonly string _assetDir = "";

	// Whether the picture for a key (`tileset:town`, `tileset:area:tidewater`, `character:fighter`, `portrait:guide`,
	// `figure:party:fighter`, `figure:back:fighter`, `figure:enemy:gel_mite`, `field_enemy:training`, `vehicle:reed_skimmer`, `backdrop:kind:town`, `effect:slash`,
	// `sprite:shadow`, `status_icon:sleep`, `ui:frame` ...) came from a file.
	public bool FromFile(string key) => _fromFile.ContainsKey(key);

	// The file a key's picture came from and a hash of its pixels as loaded, or null for a generated picture.
	public string? FileOf(string key)
	{
		if (!_fromFile.TryGetValue(key, out string? file))
		{
			return null;
		}
		uint h = 2166136261u;
		foreach (Color c in File(file).Pixels)
		{
			h = (h ^ c.PackedValue) * 16777619u;
		}
		return $"{file} {h:x8}";
	}

	public UiSkin? Skin { get; private set; }

	// The picture's size in pixels; generated pictures are 16x16.
	public (int W, int H) SizeOf(int image) => _sizes.GetValueOrDefault(image, (Pixels.Size, Pixels.Size));

	// Where a picture standing on a cell goes: bottom-centered on the cell, at the cell's scale for 16 pixels, so a
	// 16x16 picture fills the cell and a taller one rises above it.
	public Rectangle Stand(int image, int x, int y, int cell)
	{
		(int w, int h) = SizeOf(image);
		int dw = w * cell / Pixels.Size, dh = h * cell / Pixels.Size;
		return new Rectangle(x + (cell - dw) / 2, y + cell - dh, dw, dh);
	}

	private (int W, int H, Color[] Pixels) File(string relative)
	{
		string full = Path.GetFullPath(Path.Combine(_assetDir, relative));
		if (!_files.TryGetValue(full, out var file))
		{
			Color[] pixels = _s.ReadImage(full, out int w, out int h);
			_files[full] = file = (w, h, pixels);
		}
		return file;
	}

	private int Whole(string relative)
	{
		(int w, int h, Color[] pixels) = File(relative);
		int image = _s.CreateImage(w, h, pixels);
		_sizes[image] = (w, h);
		return image;
	}

	private int[] Cut(GridImage grid)
	{
		(int w, _, Color[] pixels) = File(grid.Path);
		var images = new int[grid.Cells];
		for (int cell = 0; cell < grid.Cells; cell++)
		{
			int cx = cell % grid.Columns * grid.FrameWidth, cy = cell / grid.Columns * grid.FrameHeight;
			var frame = new Color[grid.FrameWidth * grid.FrameHeight];
			for (int y = 0; y < grid.FrameHeight; y++)
			{
				Array.Copy(pixels, (cy + y) * w + cx, frame, y * grid.FrameWidth, grid.FrameWidth);
			}
			images[cell] = _s.CreateImage(grid.FrameWidth, grid.FrameHeight, frame);
			_sizes[images[cell]] = (grid.FrameWidth, grid.FrameHeight);
		}
		return images;
	}

	private NineSliceImage Slice(NineSlice slice)
	{
		(int w, int h, Color[] pixels) = File(slice.Path);
		int b = slice.Border;
		int[] xs = [0, b, w - b, w], ys = [0, b, h - b, h];
		var images = new int[9];
		for (int j = 0; j < 3; j++)
		{
			for (int i = 0; i < 3; i++)
			{
				int pw = xs[i + 1] - xs[i], ph = ys[j + 1] - ys[j];
				var part = new Color[pw * ph];
				for (int y = 0; y < ph; y++)
				{
					Array.Copy(pixels, (ys[j] + y) * w + xs[i], part, y * pw, pw);
				}
				images[j * 3 + i] = _s.CreateImage(pw, ph, part);
			}
		}
		return new NineSliceImage(images, b, slice.Scale);
	}

	private void LoadFiles(AssetManifest m)
	{
		// Kinds first, so an area's tileset takes the objects it leaves out from its kind's.
		foreach (TilesetEntry t in m.Tilesets.OrderBy(t => t.Kind is null))
		{
			int[] cells = Cut(t.Image);
			int Object(string name, int generated) => t.Objects.TryGetValue(name, out int cell) ? cells[cell] : generated;
			TileSet fallback = _tiles[t.Theme];
			var set = new TileSet
			{
				Floor = [.. t.Floor.Select(c => cells[c])],
				Decor = [.. t.Decor.Select(c => cells[c])],
				WallTop = [.. t.WallTop.Select(c => cells[c])],
				WallFace = [.. t.WallFace.Select(c => cells[c])],
				Water = [.. t.Water.Select(c => cells[c])],
				Edge = t.Edge,
				Bank = t.Bank,
				DecorOneIn = t.DecorOneIn,
				WallTopAltOneIn = t.WallTopAltOneIn,
				WallFaceAltOneIn = t.WallFaceAltOneIn,
				WaterFrameMs = t.WaterFrameMs,
				Door = Object("door", fallback.Door),
				ChestClosed = Object("chest_closed", fallback.ChestClosed),
				ChestOpen = Object("chest_open", fallback.ChestOpen),
				Stall = Object("stall", fallback.Stall),
				Bed = Object("bed", fallback.Bed),
				Sign = Object("sign", fallback.Sign),
				Rune = Object("rune", fallback.Rune),
				ExitArrow = [Object("exit_north", fallback.ExitArrow[0]), Object("exit_east", fallback.ExitArrow[1]), Object("exit_south", fallback.ExitArrow[2]),
					Object("exit_west", fallback.ExitArrow[3])],
			};
			if (t.Kind is { } kind)
			{
				_tiles[t.Theme] = set;
				_fromFile["tileset:" + kind] = t.Image.Path;
			}
			else
			{
				_areaTiles[t.Area!] = set;
				_fromFile["tileset:area:" + t.Area] = t.Image.Path;
			}
		}
		foreach (CharacterEntry c in m.Characters)
		{
			_people[c.Id] = (c, Cut(c.Image));
			_fromFile["character:" + c.Id] = c.Image.Path;
		}
		foreach ((string id, string file) in m.Portraits)
		{
			_portraits[id] = Whole(file);
			_fromFile["portrait:" + id] = file;
		}
		foreach (FigureEntry f in m.Figures)
		{
			var figure = new Figure(Cut(f.Image), f.Idle, f.Animations);
			if (f.Back)
			{
				_backFigures[f.Id] = figure;
			}
			else
			{
				_figures[(f.Id, f.Enemy)] = figure;
			}
			_fromFile[$"figure:{(f.Enemy ? "enemy" : f.Back ? "back" : "party")}:{f.Id}"] = f.Image.Path;
		}
		foreach (FieldEnemyEntry f in m.FieldEnemies)
		{
			_fieldEnemies[f.Encounter] = new Figure(Cut(f.Image), f.Run, new Dictionary<string, FrameRun>());
			_fromFile["field_enemy:" + f.Encounter] = f.Image.Path;
		}
		foreach (BackdropEntry b in m.Backdrops)
		{
			string key = b.Kind is not null ? "kind:" + b.Kind : b.Area is not null ? "area:" + b.Area : "encounter:" + b.Encounter;
			_backdrops[key] = Whole(b.Path);
			_fromFile["backdrop:" + key] = b.Path;
		}
		foreach (EffectEntry e in m.Effects)
		{
			_effects[e.Id] = new Figure(Cut(e.Image), e.Run, new Dictionary<string, FrameRun>());
			_fromFile["effect:" + e.Id] = e.Image.Path;
		}
		foreach ((string id, string file) in m.Sprites)
		{
			int image = Whole(file);
			switch (id)
			{
			case "shadow": Shadow = image; break;
			case "bubble": Bubble = image; break;
			case "alert": Alert = image; break;
			case "pier": break;
			default: Pointer = image; break;
			}
			_fromFile["sprite:" + id] = file;
		}
		foreach ((string id, string file) in m.StatusIcons)
		{
			_statusIcons[id] = Whole(file);
			_fromFile["status_icon:" + id] = file;
		}
		LoadVehicleFiles(m);
		if (m.Ui is { Enabled: true } ui)
		{
			int? Picture(string? file, string key)
			{
				if (file is null)
				{
					return null;
				}
				_fromFile["ui:" + key] = file;
				return Whole(file);
			}
			NineSliceImage? frame = ui.Frame is null ? null : Slice(ui.Frame);
			NineSliceImage? panel = ui.PanelFrames.FirstOrDefault(p => p.Camera is null) is { } any ? Slice(any) : null;
			Skin = new UiSkin(frame, panel, ui.PanelFrames.Where(p => p.Camera is not null).ToDictionary(p => p.Camera!, Slice),
				Picture(ui.Cursor, "cursor"), Picture(ui.BarTrack, "bar_track"), Picture(ui.BarFill, "bar_fill"));
			if (frame is not null)
			{
				_fromFile["ui:frame"] = ui.Frame!.Path;
			}
			foreach (NineSlice p in ui.PanelFrames)
			{
				_fromFile["ui:panel_frame" + (p.Camera is null ? "" : ":" + p.Camera)] = p.Path;
			}
		}
	}

	// The person's own sheet when the manifest gives one, else the generated look from its id.
	public int PersonFrame(string id, Direction facing, int frame) =>
		_people.TryGetValue(id, out var sheet) ? sheet.Images[sheet.Entry.Walk[facing == Direction.None ? Direction.South : facing][frame]] : ActorFrame(LookFor(id), facing, frame);

	// One play of the person's pose for a script animation: the sheet's own frames when it maps that animation; else the
	// generated pose, which on a person with a sheet keeps its standing frame and adds only the lift and marks.
	public int PoseLengthMs(string id, string animation) =>
		_people.TryGetValue(id, out var sheet) && sheet.Entry.Animations.TryGetValue(animation, out string? pose) ? sheet.Entry.Poses[pose].Ms.Sum() : PoseLengthMs(PoseFor(animation));

	public (int Image, int Lift, bool Alert, MotionMark[] Marks) PoseFrame(string id, Direction facing, string animation, int elapsedMs, bool loop)
	{
		Pose p = PoseFor(animation);
		int length = PoseLengthMs(p);
		float t = loop ? elapsedMs % length / (float)length : elapsedMs / (float)length;
		(int image, int lift, bool alert, MotionMark[] marks) = PoseFrame(LookFor(id), facing, p, t);
		if (_people.TryGetValue(id, out var sheet) && sheet.Entry.Animations.TryGetValue(animation, out string? name))
		{
			PoseEntry pose = sheet.Entry.Poses[name];
			int[] frames = pose.Frames.GetValueOrDefault(facing == Direction.None ? Direction.South : facing) ?? pose.Frames.GetValueOrDefault(Direction.South) ?? pose.Frames.Values.First();
			int frame = sheet.Images[new FrameRun(frames, pose.Ms).FrameAt(elapsedMs, loop)];
			return pose.Motion ? (frame, lift, alert, marks) : (frame, 0, false, []);
		}
		return (_people.ContainsKey(id) ? PersonFrame(id, facing, 0) : image, lift, alert, marks);
	}

	// A speaker's or member's portrait: the manifest's picture for the line's portrait id, else for the person, else the
	// front of the person's sheet or generated look.
	public int Portrait(string portrait, string person) =>
		_portraits.TryGetValue(portrait, out int image) || _portraits.TryGetValue(person, out image) ? image : PersonFrame(person, Direction.South, 0);

	// A visible enemy in the field, by its encounter; `offsetMs` keeps neighbors out of step.
	public int FieldEnemy(string encounter, int clockMs, int offset)
	{
		if (_fieldEnemies.TryGetValue(encounter, out Figure? f))
		{
			return f.Images[f.Idle.FrameAt(clockMs + offset * f.Idle.Ms[0], loop: true)];
		}
		return Creature(Noise.Hash(encounter), (clockMs / 280 + offset) % 2);
	}

	public TileSet Tiles(AreaKind kind, string area) => _areaTiles.GetValueOrDefault(area) ?? _tiles[ThemeOf(kind)];
}
