using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Frames of one picture set: idle and named animations are runs over Images, by index.
// Shadow: whether the battle draws the generated ground shadow under it.
public sealed record Figure(int[] Images, FrameRun Idle, IReadOnlyDictionary<string, FrameRun> Animations, bool Shadow = true);

// A window frame cut in nine: corners drawn at their size times Scale, edges and middle stretched.
public sealed record NineSliceImage(int[] Images, int Border, int Scale);

// A whole-area picture loaded: the ground and above images, pixels a cell, and per change flag the patch's cells cut out.
public sealed record AreaPicture(int Ground, int? Above, int Cell, int Width, int Height, IReadOnlyList<(string Flag, IReadOnlyDictionary<(int X, int Y), int> Cells)> Patches);

// An object loaded: its entry and the images each state's frame run indexes (none for a person's standing frame).
public sealed record LoadedObject(ObjectEntry Entry, int[] Closed, int[] Open);

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
	private readonly Dictionary<string, AreaPicture> _areaPictures = [];
	private readonly Dictionary<string, List<LoadedObject>> _objects = [];
	private readonly Dictionary<string, int> _vehicleOverlays = [];
	private FieldLook _fieldLook = new(true, true);
	private readonly Dictionary<string, FieldLook> _fieldLooks = [];
	private readonly Dictionary<int, (int W, int H)> _sizes = [];
	// How a picture with a scale, a natural size or a foot row stands: file pixels per grid pixel (0: the generated
	// pictures' factor, -1: the natural multiple) and the row its feet stand on.
	private readonly Dictionary<int, (float Scale, int Foot)> _fits = [];
	// Logical pixels per file pixel for figures drawn at their natural size: the logical screen height (720 by default) over battle_figures'
	// reference height, rounded down, at least 1.
	private int _naturalMultiple;
	private bool _figureShadows = true;
	// Effect frames with a scale: file pixels per grid pixel.
	private readonly Dictionary<int, float> _effectScales = [];
	private readonly Dictionary<(string Language, TextRole? Role), BitmapFont> _bitmapFonts = [];
	private Figure? _tileHookOverlay;
	private (Dictionary<Direction, int[]> Facings, int Ms, int[] Images)? _conveyorOverlay;
	private readonly Dictionary<string, string> _fromFile = [];
	private readonly string _assetDir = "";

	// Whether the picture for a key (`tileset:town`, `tileset:area:tidewater`, `character:fighter`, `portrait:guide`,
	// `figure:party:fighter`, `figure:back:fighter`, `figure:enemy:gel_mite`, `field_enemy:training`, `vehicle:reed_skimmer`, `backdrop:kind:town`, `effect:slash`,
	// `sprite:shadow`, `status_icon:sleep`, `ui:frame`, `map:tidewater`, `map:tidewater:above`, `map:AREA@FLAG`, `object:AREA:POINT`,
	// `object:AREA:X,Y`, `vehicle_overlay[:ID]` ...) came from a file.
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
	// 16x16 picture fills the cell and a taller one rises above it. A picture with a scale is drawn at the cell's factor
	// over its scale, snapped to a whole number of screen pixels per file pixel (or a whole fraction below 1); one at its
	// natural size at the natural multiple; and one with a foot row stands on that row instead of its bottom.
	public Rectangle Stand(int image, int x, int y, int cell)
	{
		(int w, int h) = SizeOf(image);
		if (!_fits.TryGetValue(image, out var fit))
		{
			int dw = w * cell / Pixels.Size, dh = h * cell / Pixels.Size;
			return new Rectangle(x + (cell - dw) / 2, y + cell - dh, dw, dh);
		}
		float p = fit.Scale > 0 ? Snap(cell / (Pixels.Size * fit.Scale)) : fit.Scale < 0 ? _naturalMultiple : cell / (float)Pixels.Size;
		int fw = (int)MathF.Round(w * p), fh = (int)MathF.Round(h * p), below = (int)MathF.Round((h - fit.Foot) * p);
		return new Rectangle(x + (cell - fw) / 2, y + cell - fh + below, fw, fh);
	}

	// An effect frame's size drawn at `factor` screen pixels a grid pixel: at the factor over its scale, snapped, when its
	// sheet gives one.
	public (int W, int H) DrawnEffectSize(int image, int factor)
	{
		(int w, int h) = SizeOf(image);
		float p = _effectScales.TryGetValue(image, out float scale) ? Snap(factor / scale) : factor;
		return ((int)MathF.Round(w * p), (int)MathF.Round(h * p));
	}

	// The bitmap font for a text language and role: the role's own, else the language's font for every role. The hint
	// without its own font draws in the menu's.
	public BitmapFont? BitmapFontFor(string language, TextRole role) =>
		_bitmapFonts.GetValueOrDefault((language, role)) ?? (role == TextRole.Hint ? BitmapFontFor(language, TextRole.Menu) : _bitmapFonts.GetValueOrDefault((language, null)));

	// The picture on a cell of an active tile step hook, from the field's overlay, or null for the generated rune.
	public int? TileHookOverlay(int clockMs) => _tileHookOverlay is { } o ? o.Images[o.Idle.FrameAt(clockMs, loop: true)] : null;

	// The picture on a conveyor cell from the field's overlay (its direction's run while it runs, the first frame while it
	// stands), or null for the generated belt.
	public int? ConveyorOverlay(Direction direction, bool running, int clockMs)
	{
		if (_conveyorOverlay is not { } o)
		{
			return null;
		}
		int[] frames = o.Facings[direction];
		return o.Images[running ? frames[clockMs / o.Ms % frames.Length] : frames[0]];
	}

	// The nearest whole factor, halves up, or below 1 the nearest whole fraction 1/n, so every file pixel covers the same
	// number of screen pixels.
	public static float Snap(float raw) => raw >= 1f ? MathF.Floor(raw + 0.5f) : 1f / Math.Max(1f, MathF.Floor(1f / raw + 0.5f));

	// Whether the field draws the ground shadow under a person: not when its sheet turns it off.
	public bool PersonShadow(string id) => !_people.TryGetValue(id, out var sheet) || sheet.Entry.Shadow != false;

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

	// `natural`: a battle figure, which stands at the natural multiple when battle_figures gives a reference height and
	// the entry no scale.
	private int[] Cut(GridImage grid, bool natural = false)
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
			float? fit = grid.Scale ?? (natural && _naturalMultiple > 0 ? -1f : grid.Foot is not null ? 0f : null);
			if (fit is float f)
			{
				_fits[images[cell]] = (f, grid.Foot ?? grid.FrameHeight);
			}
		}
		return images;
	}

	private readonly Dictionary<string, NineSliceImage?> _layoutSlices = [];
	private readonly Dictionary<string, int> _layoutPictures = [];

	// A frame or cursor picture a layout's styles name, from the asset directory; null or -1 when it cannot be read.
	public NineSliceImage? LayoutSlice(string file, int border, int scale)
	{
		string key = $"{file}#{border}#{scale}";
		if (!_layoutSlices.TryGetValue(key, out NineSliceImage? slice))
		{
			string full = Path.GetFullPath(Path.Combine(_assetDir, file));
			slice = _assetDir.Length > 0 && System.IO.File.Exists(full) && Png.ReadHeader(full, out _) is { } h && h.Width > 2 * border && h.Height > 2 * border
				? Slice(new NineSlice(null, file, border, scale)) : null;
			_layoutSlices[key] = slice;
		}
		return slice;
	}

	public int LayoutPicture(string file)
	{
		if (!_layoutPictures.TryGetValue(file, out int image))
		{
			string full = Path.GetFullPath(Path.Combine(_assetDir, file));
			image = _assetDir.Length > 0 && System.IO.File.Exists(full) && Png.ReadHeader(full, out _) is not null ? Whole(file) : -1;
			_layoutPictures[file] = image;
		}
		return image;
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
		_naturalMultiple = m.ReferenceHeight is int reference ? Math.Max(1, _s.Height / reference) : 0;
		_figureShadows = m.FigureShadows;
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
				Animated = t.Animated.ToDictionary(a => cells[a.Key], a => new Figure(cells, a.Value, new Dictionary<string, FrameRun>())),
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
			var figure = new Figure(Cut(f.Image, natural: f.Natural ?? true), f.Idle, f.Animations, f.Shadow ?? m.FigureShadows);
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
			_backdrops[b.Key] = Whole(b.Path);
			_fromFile["backdrop:" + b.Key] = b.Path;
		}
		foreach (EffectEntry e in m.Effects)
		{
			int[] frames = Cut(e.Image);
			if (e.Image.Scale is float scale)
			{
				foreach (int frame in frames)
				{
					_fits.Remove(frame);
					_effectScales[frame] = scale;
				}
			}
			_effects[e.Id] = new Figure(frames, e.Run, new Dictionary<string, FrameRun>());
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
		foreach (MapEntry a in m.Maps)
		{
			var patches = new List<(string, IReadOnlyDictionary<(int X, int Y), int>)>();
			foreach (MapPatch p in a.Patches)
			{
				(int w, _, Color[] pixels) = File(p.Path);
				var cells = new Dictionary<(int X, int Y), int>();
				foreach ((int x, int y) in p.Cells)
				{
					var part = new Color[a.Cell * a.Cell];
					for (int row = 0; row < a.Cell; row++)
					{
						Array.Copy(pixels, (y * a.Cell + row) * w + x * a.Cell, part, row * a.Cell, a.Cell);
					}
					cells[(x, y)] = _s.CreateImage(a.Cell, a.Cell, part);
				}
				patches.Add((p.Flag, cells));
				_fromFile[$"map:{a.Area}@{p.Flag}"] = p.Path;
			}
			(int gw, int gh, _) = File(a.Ground);
			_areaPictures[a.Area] = new AreaPicture(Whole(a.Ground), a.Above is null ? null : Whole(a.Above), a.Cell, gw / a.Cell, gh / a.Cell, patches);
			_fromFile["map:" + a.Area] = a.Ground;
			if (a.Above is not null)
			{
				_fromFile[$"map:{a.Area}:above"] = a.Above;
			}
		}
		foreach (ObjectEntry o in m.Objects)
		{
			if (!_objects.TryGetValue(o.Area, out List<LoadedObject>? list))
			{
				_objects[o.Area] = list = [];
			}
			int[] closed = o.Closed is null ? [] : Cut(o.Closed.Image);
			list.Add(new LoadedObject(o, closed, o.Open is null ? [] : o.Open.Image == o.Closed!.Image ? closed : Cut(o.Open.Image)));
			if (o.Closed is not null)
			{
				_fromFile["object:" + o.Key] = o.Closed.Image.Path;
			}
		}
		foreach (OverlayEntry o in m.VehicleOverlays)
		{
			_vehicleOverlays[o.Vehicle ?? ""] = Whole(o.Path);
			_fromFile["vehicle_overlay" + (o.Vehicle is null ? "" : ":" + o.Vehicle)] = o.Path;
		}
		if (m.TileHookOverlay is { } pad)
		{
			_tileHookOverlay = new Figure(Cut(pad.Image), pad.Run, new Dictionary<string, FrameRun>());
			_fromFile["tile_hook_overlay"] = pad.Image.Path;
		}
		if (m.ConveyorOverlay is { } belt)
		{
			_conveyorOverlay = (belt.Facings, belt.Ms, Cut(belt.Image));
			_fromFile["conveyor_overlay"] = belt.Image.Path;
		}
		if (m.BitmapFontsEnabled == true)
		{
			foreach (BitmapFontEntry f in m.BitmapFonts)
			{
				(int w, _, Color[] pixels) = File(f.Image);
				var glyphs = new Dictionary<char, int>();
				foreach ((char c, GlyphMetrics g) in f.Metrics.Glyphs)
				{
					var part = new Color[g.Width * g.Height];
					for (int y = 0; y < g.Height; y++)
					{
						Array.Copy(pixels, (g.Y + y) * w + g.X, part, y * g.Width, g.Width);
					}
					glyphs[c] = _s.CreateImage(g.Width, g.Height, part);
				}
				_bitmapFonts[(f.Language, f.Role)] = new BitmapFont(f.Image, f.Metrics, f.Scale, glyphs);
				_fromFile[$"bitmap_font:{f.Language}{(f.Role is { } role ? ":" + AssetManifest.RoleNames[(int)role] : "")}"] = f.Image;
			}
		}
		_fieldLook = m.Field;
		foreach ((string area, FieldLook look) in m.FieldAreas)
		{
			_fieldLooks[area] = look;
		}
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

	public AreaPicture? AreaPictureOf(string area) => _areaPictures.GetValueOrDefault(area);

	// The objects of an area with a picture of their own, at points and at cells.
	public IReadOnlyList<LoadedObject> ObjectsIn(string area) => _objects.GetValueOrDefault(area) ?? [];

	public FieldLook FieldLookOf(string area) => _fieldLooks.GetValueOrDefault(area) ?? _fieldLook;

	// The overlay over a vehicle battle's stage: the vehicle's own, else the one for every vehicle.
	public int? VehicleOverlay(string vehicle) => _vehicleOverlays.TryGetValue(vehicle, out int image) || _vehicleOverlays.TryGetValue("", out image) ? image : null;

	public TileSet Tiles(AreaKind kind, string area) => _areaTiles.GetValueOrDefault(area) ?? _tiles[ThemeOf(kind)];
}
