using System.Globalization;
using System.Text.Json;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed record AssetProblem(string File, string Entry, string Reason)
{
	public override string ToString() => $"{File}: {Entry}: {Reason}";
}

// A PNG cut into a grid of equal frames, numbered left to right, top to bottom from 0.
// Scale: file pixels per pixel of the sample's 16-pixel grid (null when the entry gives none). Foot: the frame row, in file
// pixels from its top, that stands on the ground line (null: the bottom).
public sealed record GridImage(string Path, int FrameWidth, int FrameHeight)
{
	public int Columns { get; init; } = 1;
	public int Rows { get; init; } = 1;
	public float? Scale { get; init; }
	public int? Foot { get; init; }
	public int Cells => Columns * Rows;
}

// Frames of a grid played in order, each for its own milliseconds.
public sealed record FrameRun(int[] Frames, int[] Ms)
{
	public int LengthMs => Ms.Sum();

	public int FrameAt(int elapsedMs, bool loop)
	{
		int t = loop && LengthMs > 0 ? elapsedMs % LengthMs : elapsedMs;
		for (int i = 0; i < Frames.Length; i++)
		{
			if (t < Ms[i])
			{
				return Frames[i];
			}
			t -= Ms[i];
		}
		return Frames[^1];
	}
}

// Animated: frame runs that stand in for a tile frame wherever the renderer picks it.
public sealed record TilesetEntry(string? Kind, string? Area, Theme Theme, GridImage Image, int[] Floor, int[] Decor, int DecorOneIn, int[] WallTop, int WallTopAltOneIn,
	int[] WallFace, int WallFaceAltOneIn, int[] Water, int WaterFrameMs, Color Edge, Color Bank, Dictionary<string, int> Objects, Dictionary<int, FrameRun> Animated,
	List<PaletteTrack>? Palettes = null);

// A tilesets entry without a picture: palette tracks over the tiles its kind or area shows without it.
public sealed record TilesetRecolor(string? Kind, string? Area, Theme Theme, List<PaletteTrack> Palettes);

// A whole-area picture, Cell pixels a cell: Ground in place of the area's tiles, Above over everyone. A patch is drawn on
// the cells of its flag's changes while the flag is set.
public sealed record MapPatch(string Flag, string Path, (int X, int Y)[] Cells);

public sealed record MapEntry(string Area, string Ground, string? Above, int Cell, List<MapPatch> Patches);

// When a timed piece of an area's motion plays: its clock starts at the controller's zero (global) or when the area was
// entered (Entry), and it shows while Flag is set (when given) and UnlessFlag is not.
public sealed record MotionWhen(bool Entry, string? Flag, string? UnlessFlag);

// Exact-color replacement in steps: each step's pairs, each step lasting its Units (60000ths of a second, so
// milliseconds and 60 Hz ticks are both whole), looping. One step is a still recolor.
public sealed record PaletteTrack(IReadOnlyList<(Color From, Color To)>[] Steps, int[] Units, MotionWhen When)
{
	public int StepAt(long ms) => IndexAt(Units, ms);

	// The index of the duration a looping run of Units stands at after `ms` milliseconds.
	public static int IndexAt(int[] units, long ms)
	{
		if (units.Length == 1)
		{
			return 0;
		}
		long t = ms * 60 % units.Sum();
		for (int i = 0; i < units.Length; i++)
		{
			if (t < units[i])
			{
				return i;
			}
			t -= units[i];
		}
		return units.Length - 1;
	}
}

// A sheet's frames played on a rectangle of the area's picture (X, Y, W, H in the entry's picture pixels), on the ground
// or over the above picture; Units as a palette's.
public sealed record AreaRun(GridImage Image, int X, int Y, int W, int H, int[] Frames, int[] Units, bool Above, MotionWhen When)
{
	public int FrameAt(long ms) => Frames[PaletteTrack.IndexAt(Units, ms)];
}

// Rows First to Last (picture pixels) of a layer scrolling at their own factor and speed across the screen.
public sealed record LayerBand(int First, int Last, float Scroll, float Speed);

// A sine over the layer's lines (each row moved sideways) or columns (each moved up and down), in picture pixels, the wave
// travelling Speed pixels a second.
public sealed record LayerWave(bool Columns, float Amplitude, float Wavelength, float Speed);

// A picture behind the area (Front false) or in front of everything on it, moving Scroll times the camera and Speed
// picture pixels a second, repeated along the axes it repeats on; At moves it that many picture pixels.
public sealed record AreaLayer(string Image, int Width, int Height, bool Front, float ScrollX, float ScrollY, float SpeedX, float SpeedY, bool RepeatX, bool RepeatY,
	List<LayerBand> Bands, LayerWave? Wave, MotionWhen When, float AtX, float AtY);

// An area's motion, from its maps entry: Cell picture pixels a cell, as the entry's pictures.
public sealed record AreaMotion(int Cell, List<PaletteTrack> Palettes, List<AreaRun> Runs, List<AreaLayer> Layers);

// How an object looks in one state: a frame run of Image filling Width by Height cells, the anchor cell standing on the
// object's cell.
public sealed record ObjectState(GridImage Image, FrameRun Run, int Width, int Height, int AnchorX, int AnchorY);

// A picture of its own for one point (by id) or cell of an area: a person's standing frame (Character, Facing) or a
// state's frames; Open is its look once its chest is opened or OpenFlag is set.
public sealed record ObjectEntry(string Area, string? Point, (int X, int Y)? At, string? Character, Direction Facing, ObjectState? Closed, ObjectState? Open,
	string? OpenFlag)
{
	public string Key => $"{Area}:{Point ?? $"{At!.Value.X},{At!.Value.Y}"}";
}

// The generated wall shadows and water foam, on or off; the conveyor belts and the runes on tile step hooks' cells, drawn
// (generated, or the field's overlay pictures) or not (drawn into a whole-area picture).
public sealed record FieldLook(bool WallShadows, bool WaterFoam, bool Conveyors = true, bool TileHooks = true);

// Pictures drawn on conveyor cells in place of the generated belts: per direction a frame run looping at Ms a frame while
// the belt runs, its first frame while it stands still.
public sealed record ConveyorOverlay(GridImage Image, Dictionary<Direction, int[]> Facings, int Ms);

public sealed record OverlayEntry(string? Vehicle, string Path);

// Motion: whether the animation's generated motion (a hop, a startled mark, strokes beside the head) plays over the frames.
public sealed record PoseEntry(Dictionary<Direction, int[]> Frames, int[] Ms, bool Motion);

// How a sheet faces: Mirror draws a facing its walk lacks as another one flipped, Fallback draws any other facing it
// lacks, Start is how an actor stands until a script turns or moves it.
public sealed record SheetFacing(Dictionary<Direction, Direction> Mirror, Direction? Fallback, Direction? Start);

// Shadow: whether the generated ground shadow is drawn under the person (null: drawn).
public sealed record CharacterEntry(string Id, GridImage Image, Dictionary<Direction, int[]> Walk, Dictionary<string, PoseEntry> Poses, Dictionary<string, string> Animations,
	bool? Shadow = null, SheetFacing? Facing = null);

// Back: the party member or vehicle seen from behind, as the front battle layout draws it.
// Shadow: whether the generated ground shadow is drawn under it (null: battle_figures.shadows).
// Natural: whether it is drawn at its natural size (null: whenever battle_figures gives a reference height).
// Palette: the picture's colors in order, which a color transform's palette names by index. Entry: where it is in assets.json.
public sealed record FigureEntry(string Id, bool Enemy, GridImage Image, FrameRun Idle, Dictionary<string, FrameRun> Animations, bool Back, bool? Shadow = null,
	bool? Natural = null, IReadOnlyList<Color>? Palette = null, string Entry = "", string? PaletteFile = null);

public sealed record FieldEnemyEntry(string Encounter, GridImage Image, FrameRun Run);

// A vehicle in the field: per facing a frame run of `Ms` milliseconds a frame, looping (a bob).
public sealed record VehicleEntry(string Id, GridImage Image, Dictionary<Direction, int[]> Facings, int Ms);

// Id: the name backdrop maps give it; an entry with only an id serves the maps alone. Flag: the entry serves while the
// flag is set, in Area only when it names one.
public sealed record BackdropEntry(string? Kind, string? Area, string? Encounter, string? Terrain, string Path, string? Id = null, string? Flag = null)
{
	public string Key => Flag is not null ? "flag:" + Flag + (Area is null ? "" : "@" + Area) : Kind is not null ? "kind:" + Kind : Area is not null ? "area:" + Area
		: Encounter is not null ? "encounter:" + Encounter : Terrain is not null ? "terrain:" + Terrain : "id:" + Id;
}

// A backdrop per cell of an area, from a picture's colors (Image) or from named rectangles: each cell's backdrop id and
// the key the `backdrop` probe reports (`map:AREA:ID`, `region:AREA:NAME`), null where the map gives none.
public sealed record BackdropMap(string Area, string? Image, int Width, int Height, (string Backdrop, string Key)?[] Cells);

// A frame run drawn over the title's background at X, Y, Scale screen pixels a file pixel, moving ScrollX and ScrollY
// pixels a second and repeated across the screen along each axis it moves on.
public sealed record TitleLayer(GridImage Image, FrameRun Run, int X, int Y, int Scale, float ScrollX, float ScrollY);

// The title screen's own pictures: a background in place of the field scene, a logo in place of the title's text, layers
// over the background, and the traveler and the creature beside the title. ReferenceHeight: the logical screen height the
// logo, traveler and creature were drawn for, which draws them at a whole multiple of their pixels (null: as before).
public sealed record TitleArtEntry(string? Background, string? Logo, List<TitleLayer> Layers, (GridImage Image, FrameRun Run)? Traveler, (GridImage Image, FrameRun Run)? Creature,
	int? ReferenceHeight = null);

public sealed record EffectEntry(string Id, GridImage Image, FrameRun Run, IReadOnlyList<Color>? Palette = null, string? PaletteFile = null);

public sealed record NineSlice(string? Camera, string Path, int Border, int Scale);

public sealed record FontEntry(string File, float Scale);

// BattleFrame: the battle's windows, in place of Frame there. PageArrow: the picture the layouts' `page_arrow` field gives.
public sealed record UiEntry(bool Enabled, NineSlice? Frame, List<NineSlice> PanelFrames, string? Cursor, string? BarTrack, string? BarFill, Dictionary<string, FontEntry> Fonts,
	NineSlice? BattleFrame = null, string? PageArrow = null);

public sealed record AudioEntry(SoundKind Kind, string Id, string Path, int LoopStart, int LoopEnd, float Gain);

// A bitmap font for a text language, for every role (Role null) or for one, read from its glyph sheet and metrics file.
public sealed record BitmapFontEntry(string Language, TextRole? Role, string Image, string MetricsFile, float Scale, FontMetrics Metrics);

// The asset directory's manifest (assets.json): which files replace which generated placeholders. Every entry is
// optional; an id without one keeps its generated picture or sound. Parsing checks every field and every file it can
// without a graphics device (PNG and sound headers), and collects each problem with its file, entry and reason; an
// entry with a problem is left out, so its generated placeholder stands in.
public sealed class AssetManifest
{
	public const string FileName = "assets.json";
	public const int Version = 1;

	public static readonly string[] Facings = ["north", "east", "south", "west"];
	public static readonly string[] ObjectNames = ["door", "chest_closed", "chest_open", "stall", "bed", "sign", "rune", "exit_north", "exit_east", "exit_south", "exit_west"];
	public static readonly string[] SpriteNames = ["shadow", "bubble", "alert", "pointer", "pier", "platform"];

	// The fields each kind of entry takes, by its place in the file (`-` the top level, `[]` a list's entry, `{}` a map's
	// value): the check refuses any other, and `--capabilities` reports these.
	public static readonly Dictionary<string, string[]> Schema = new()
	{
		["frame_run"] = ["frames", "ms"],
		["-"] = ["version", "tilesets", "characters", "portraits", "figures", "field_enemies", "vehicles", "backdrops", "effects", "sprites", "status_icons", "ui", "audio", "battle_presentation", "battle_animation", "maps", "objects", "field", "vehicle_overlays", "battle_figures", "bitmap_fonts", "ui_layout", "backdrop_maps", "title_art", "command_icons", "pictures", "keepers"],
		["field_enemies[]"] = ["encounter", "image", "frame", "scale", "foot", "frames", "ms"],
		["vehicles[]"] = ["id", "image", "frame", "scale", "foot", "facings", "ms"],
		["backdrops[]"] = ["id", "kind", "area", "encounter", "terrain", "flag", "image"],
		["effects[]"] = ["id", "image", "frame", "scale", "frames", "ms", "palette"],
		["ui_layout"] = ["enabled"],
		["battle_figures"] = ["reference_height", "shadows"],
		["vehicle_overlays[]"] = ["vehicle", "image"],
		["portraits{}"] = ["image", "expressions"],
		["backdrop_maps[]"] = ["area", "image", "colors", "regions"],
		["backdrop_maps[].regions{}"] = ["rect", "backdrop"],
		["title_art"] = ["background", "logo", "layers", "traveler", "creature", "reference_height"],
		["title_art.traveler"] = ["image", "frame", "frames", "ms"],
		["title_art.layers[]"] = ["image", "frame", "frames", "ms", "at", "scale", "scroll"],
		["maps"] = ["enabled", "cell", "areas"],
		["maps.areas[]"] = ["area", "image", "above", "cell", "changes", "palettes", "animated", "layers"],
		["maps.areas[].animated[]"] = ["cells", "pixels", "image", "frames", "ms", "ticks", "layer", "clock", "flag", "unless_flag"],
		["maps.areas[].animated[].layer"] = ["ground", "above"],
		["maps.areas[].layers[]"] = ["image", "place", "scroll", "speed", "at", "repeat", "bands", "wave", "clock", "flag", "unless_flag"],
		["maps.areas[].layers[].bands[]"] = ["rows", "scroll", "speed"],
		["maps.areas[].layers[].place"] = ["back", "front"],
		["maps.areas[].layers[].repeat"] = ["both", "x", "y", "none"],
		["maps.areas[].layers[].wave"] = ["axis", "amplitude", "wavelength", "speed"],
		["maps.areas[].layers[].wave.axis"] = ["lines", "columns"],
		["motion.clock"] = ["global", "entry"],
		["palettes[]"] = ["colors", "steps", "ms", "ticks", "clock", "flag", "unless_flag"],
		["sprites{}"] = ["image", "palettes"],
		["objects[].state"] = ["image", "frame", "frames", "ms", "size", "anchor"],
		["objects[]"] = ["area", "point", "at", "image", "frame", "frames", "ms", "size", "anchor", "open", "open_flag", "character", "facing"],
		["field"] = ["wall_shadows", "water_foam", "conveyors", "tile_hooks", "conveyor_overlay", "tile_hook_overlay", "areas"],
		["field.areas{}"] = ["wall_shadows", "water_foam", "conveyors", "tile_hooks"],
		["field.conveyor_overlay"] = ["image", "frame", "facings", "ms"],
		["field.tile_hook_overlay"] = ["image", "frame", "frames", "ms"],
		["bitmap_fonts"] = ["enabled", "fonts"],
		["bitmap_fonts.fonts[]"] = ["language", "role", "image", "metrics", "scale"],
		["tilesets[]"] = ["kind", "area", "image", "tile", "scale", "floor", "decor", "decor_one_in", "wall_top", "wall_top_alt_one_in", "wall_face", "wall_face_alt_one_in", "water", "water_frame_ms", "edge", "bank", "objects", "animated", "palettes"],
		["tilesets[].animated[]"] = ["tile", "frames", "ms"],
		["characters[]"] = ["id", "image", "frame", "scale", "foot", "shadow", "walk", "poses", "animations", "facing"],
		["characters[].facing"] = ["mirror", "fallback", "start"],
		["characters[].poses{}"] = ["frames", "ms", "motion"],
		["figures[]"] = ["id", "side", "view", "image", "frame", "scale", "foot", "shadow", "natural", "idle", "animations", "palette"],
		["ui"] = ["enabled", "frame", "battle_frame", "panel_frames", "cursor", "bar_track", "bar_fill", "fonts", "page_arrow"],
		["ui.fonts{}"] = ["file", "scale"],
		["audio[]"] = ["kind", "id", "file", "loop_start", "loop_end", "gain"],
		["facings"] = Facings,
		["tilesets[].objects"] = ObjectNames,
		["nine_slice"] = ["image", "border", "scale"],
		["nine_slice_camera"] = ["camera", "image", "border", "scale"],
	};
	// The poses every figure has, its own frames or `idle`'s; a figure may name more (IsPoseName) for the presentation table.
	public static readonly string[] FigureAnimations = ["attack", "cast", "item", "defend", "hurt", "defeat"];
	// The terrain under the party, as a battle backdrop's scope: floor, water and soft ground.
	public static readonly string[] TerrainNames = ["floor", "water", "soft"];
	public static readonly string[] RoleNames = ["menu", "dialogue", "battle_numbers", "hint", "narration"];
	// What a member chose for the round, as the battle layouts' `command` field and the `command_icons` entry name it.
	public static readonly string[] CommandNames = ["attack", "technique", "skill", "combo", "item", "defend", "escape"];

	public static bool IsPoseName(string name) =>
		name.Length > 0 && char.IsAsciiLetterLower(name[0]) && name.All(c => char.IsAsciiLetterLower(c) || char.IsAsciiDigit(c) || c == '_') && name != "idle";

	public string Directory { get; }
	public List<TilesetEntry> Tilesets { get; } = [];
	public List<CharacterEntry> Characters { get; } = [];
	public Dictionary<string, string> Portraits { get; } = [];
	public List<FigureEntry> Figures { get; } = [];
	public List<FieldEnemyEntry> FieldEnemies { get; } = [];
	public List<VehicleEntry> Vehicles { get; } = [];
	public List<BackdropEntry> Backdrops { get; } = [];
	public List<BackdropMap> BackdropMaps { get; } = [];
	public TitleArtEntry? TitleArt { get; private set; }
	public List<EffectEntry> Effects { get; } = [];
	public Dictionary<string, string> Sprites { get; } = [];
	public Dictionary<string, List<PaletteTrack>> SpritePalettes { get; } = [];
	public Dictionary<string, string> StatusIcons { get; } = [];
	public Dictionary<string, string> CommandIcons { get; } = [];
	// Still pictures by name, which the dialogue's `picture` tags show.
	public Dictionary<string, string> Pictures { get; } = [];
	// A shop's or rest point's id to the portrait id its screen shows as the keeper.
	public Dictionary<string, string> Keepers { get; } = [];
	public UiEntry? Ui { get; private set; }
	public List<AudioEntry> Audio { get; } = [];
	public string? BattlePresentation { get; private set; }
	public string? BattleAnimation { get; private set; }
	// False when the manifest turns the whole-area pictures off, which also stops the maps/ file convention.
	public bool MapsEnabled { get; private set; } = true;
	public List<MapEntry> Maps { get; } = [];
	public List<TilesetRecolor> TilesetRecolors { get; } = [];
	// Palette tracks, frame runs and layers by area, from the maps entries.
	public Dictionary<string, AreaMotion> AreaMotions { get; } = [];
	public List<ObjectEntry> Objects { get; } = [];
	public FieldLook Field { get; private set; } = new(true, true);
	public Dictionary<string, FieldLook> FieldAreas { get; } = [];
	public ConveyorOverlay? ConveyorOverlay { get; private set; }
	// The picture drawn on each cell of an active tile step hook in place of the generated rune.
	public (GridImage Image, FrameRun Run)? TileHookOverlay { get; private set; }
	// Null without a bitmap_fonts section; false when the section turns the fonts off (nothing of it is used then).
	public bool? BitmapFontsEnabled { get; private set; }
	public List<BitmapFontEntry> BitmapFonts { get; } = [];
	public List<OverlayEntry> VehicleOverlays { get; } = [];
	// battle_figures: the logical screen height figures without a scale were drawn for (null: none), and whether figures
	// cast the generated ground shadow unless their entry says.
	public int? ReferenceHeight { get; private set; }
	public bool FigureShadows { get; private set; } = true;
	public List<AssetProblem> Problems { get; } = [];
	// Null without a ui_layout entry (the ui/ layout files load when there are any); false when the entry turns them off.
	public bool? UiLayoutEnabled { get; private set; }

	private AssetManifest(string directory) => Directory = directory;

	public string Full(string relative) => Path.GetFullPath(Path.Combine(Directory, relative));

	// Every file the manifest names, for change detection on reload.
	public IEnumerable<string> Files() =>
		Tilesets.Select(t => t.Image.Path).Concat(Characters.Select(c => c.Image.Path)).Concat(Portraits.Values).Concat(Figures.Select(f => f.Image.Path)).Concat(Figures.Select(f => f.PaletteFile).OfType<string>())
			.Concat(FieldEnemies.Select(f => f.Image.Path)).Concat(Vehicles.Select(v => v.Image.Path)).Concat(Backdrops.Select(b => b.Path)).Concat(Effects.Select(e => e.Image.Path)).Concat(Effects.Select(e => e.PaletteFile).OfType<string>()).Concat(Sprites.Values)
			.Concat(StatusIcons.Values).Concat(CommandIcons.Values).Concat(Pictures.Values).Concat(Audio.Select(a => a.Path))
			.Concat(Maps.SelectMany(a => new[] { a.Ground, a.Above }.OfType<string>().Concat(a.Patches.Select(p => p.Path))))
			.Concat(AreaMotions.Values.SelectMany(a => a.Runs.Select(r => r.Image.Path).Concat(a.Layers.Select(l => l.Image))))
			.Concat(Objects.SelectMany(o => new[] { o.Closed?.Image.Path, o.Open?.Image.Path }).OfType<string>()).Concat(VehicleOverlays.Select(o => o.Path))
			.Concat(new[] { ConveyorOverlay?.Image.Path, TileHookOverlay?.Image.Path }.OfType<string>())
			.Concat(BitmapFonts.SelectMany(f => new[] { f.Image, f.MetricsFile }))
			.Concat(BackdropMaps.Select(b => b.Image).OfType<string>())
			.Concat(TitleArt is not { } title ? [] : new[] { title.Background, title.Logo, title.Traveler?.Image.Path, title.Creature?.Image.Path }.OfType<string>()
				.Concat(title.Layers.Select(l => l.Image.Path)))
			.Concat(Ui is null ? [] : new[] { Ui.Frame?.Path, Ui.BattleFrame?.Path, Ui.Cursor, Ui.BarTrack, Ui.BarFill, Ui.PageArrow }.OfType<string>().Concat(Ui.PanelFrames.Select(p => p.Path)).Concat(Ui.Fonts.Values.Select(f => f.File)))
			.Concat(BattlePresentation is null ? [] : [BattlePresentation])
			.Concat(BattleAnimation is null ? [] : [BattleAnimation])
			.Concat(UiLayoutEnabled == false ? [] : UiLayoutSet.FilesIn(Directory))
			.Distinct();

	// Reads DIR/assets.json against the session's data, scaled tilesets against the field's `cell` (the layouts'
	// `field_cell`). A directory without a manifest is a problem too, since the caller named it.
	// `played`: cue and music ids something outside the data plays (the dialogue layout's tags), checked as the data's.
	public static AssetManifest Load(string directory, GameData data, int cell = Art.Tile, (IReadOnlySet<string> Cues, IReadOnlySet<string> Music)? played = null)
	{
		var m = new AssetManifest(Path.GetFullPath(directory));
		string path = Path.Combine(directory, FileName);
		if (!File.Exists(path))
		{
			m.Problems.Add(new AssetProblem(FileName, "-", $"no {FileName} in {m.Directory}"));
			return m;
		}
		try
		{
			using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path), new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip });
			new Reader(m, data, cell, played).Root(doc.RootElement);
		}
		catch (JsonException e)
		{
			m.Problems.Add(new AssetProblem(FileName, "-", "not valid JSON: " + e.Message));
		}
		return m;
	}

	// Walks the JSON, reporting unknown fields, wrong types, unknown ids and files that do not fit their entries.
	private sealed class Reader(AssetManifest m, GameData data, int cell, (IReadOnlySet<string> Cues, IReadOnlySet<string> Music)? played)
	{
		// Figures that ask for their natural size, checked once battle_figures is read.
		private readonly List<(string Entry, FigureEntry Figure)> _natural = [];

		private void Problem(string entry, string reason, string? file = null) => m.Problems.Add(new AssetProblem(file ?? FileName, entry, reason));

		private void Fields(JsonElement e, string entry, string[] known)
		{
			foreach (JsonProperty p in e.EnumerateObject().Where(p => !known.Contains(p.Name)))
			{
				Problem(entry, $"unknown field '{p.Name}'");
			}
		}

		private bool IsObject(JsonElement e, string entry)
		{
			if (e.ValueKind != JsonValueKind.Object)
			{
				Problem(entry, "must be an object");
				return false;
			}
			return true;
		}

		private string? Str(JsonElement e, string name, string entry, bool required = true)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				if (required)
				{
					Problem(entry, $"missing field '{name}'");
				}
				return null;
			}
			if (v.ValueKind != JsonValueKind.String || v.GetString()!.Length == 0)
			{
				Problem(entry, $"field '{name}' must be a non-empty string");
				return null;
			}
			return v.GetString();
		}

		private int Int(JsonElement e, string name, string entry, int fallback, int min, int max)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				return fallback;
			}
			if (v.ValueKind != JsonValueKind.Number || !v.TryGetInt32(out int n) || n < min || n > max)
			{
				Problem(entry, $"field '{name}' must be a whole number from {min} to {max}");
				return fallback;
			}
			return n;
		}

		private float Float(JsonElement e, string name, string entry, float fallback, float min, float max)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				return fallback;
			}
			if (v.ValueKind != JsonValueKind.Number || v.GetSingle() < min || v.GetSingle() > max)
			{
				Problem(entry, $"field '{name}' must be a number from {min.ToString(CultureInfo.InvariantCulture)} to {max.ToString(CultureInfo.InvariantCulture)}");
				return fallback;
			}
			return v.GetSingle();
		}

		private int[] Ints(JsonElement e, string name, string entry, bool required = false)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				if (required)
				{
					Problem(entry, $"missing field '{name}'");
				}
				return [];
			}
			if (v.ValueKind != JsonValueKind.Array || v.EnumerateArray().Any(x => x.ValueKind != JsonValueKind.Number || !x.TryGetInt32(out int n) || n < 0))
			{
				Problem(entry, $"field '{name}' must be a list of whole numbers from 0");
				return [];
			}
			return [.. v.EnumerateArray().Select(x => x.GetInt32())];
		}

		// "#rrggbb" or "#rrggbbaa", straight alpha.
		private Color ColorOf(JsonElement e, string name, string entry, Color fallback)
		{
			string? text = Str(e, name, entry, required: false);
			if (text is null)
			{
				return fallback;
			}
			if (text.Length is not (7 or 9) || text[0] != '#' || !uint.TryParse(text[1..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint v))
			{
				Problem(entry, $"field '{name}' must be a color as #rrggbb or #rrggbbaa");
				return fallback;
			}
			if (text.Length == 7)
			{
				v = (v << 8) | 0xff;
			}
			byte[] rgba = [.. Png.FromStraight([(byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v], 1).SelectMany(c => new[] { c.R, c.G, c.B, c.A })];
			return new Color(rgba[0], rgba[1], rgba[2], rgba[3]);
		}

		private bool Bool(JsonElement e, string name, string entry, bool fallback)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				return fallback;
			}
			if (v.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
			{
				Problem(entry, $"field '{name}' must be true or false");
				return fallback;
			}
			return v.ValueKind == JsonValueKind.True;
		}

		// [x, y], each from 0 and below the given bounds.
		private (int X, int Y)? Pair(JsonElement e, string name, string entry, int width, int height, string what)
		{
			int[] v = Ints(e, name, entry);
			if (!e.TryGetProperty(name, out _))
			{
				return null;
			}
			if (v.Length != 2 || v[0] >= width || v[1] >= height)
			{
				Problem(entry, $"field '{name}' must be [x, y] {what} ({width}x{height})");
				return null;
			}
			return (v[0], v[1]);
		}

		// A PNG that must exist and divide into frames of the given size; `scaled` entries take a `scale` and, when `footed`,
		// a `foot`.
		private GridImage? Grid(JsonElement e, string entry, int defaultFrame, bool scaled = false, bool footed = true)
		{
			float? scale = null;
			if (scaled && e.TryGetProperty("scale", out JsonElement sv))
			{
				if (sv.ValueKind == JsonValueKind.Number && sv.GetDouble() > 0 && sv.GetDouble() <= 64)
				{
					scale = (float)sv.GetDouble();
				}
				else
				{
					Problem(entry, "field 'scale' must be a number above 0, at most 64");
				}
			}
			string? file = Str(e, "image", entry);
			int[] frame = e.TryGetProperty("frame", out _) ? Ints(e, "frame", entry) : [defaultFrame, defaultFrame];
			if (e.TryGetProperty("tile", out _))
			{
				int tile = Int(e, "tile", entry, defaultFrame, 1, 1024);
				frame = [tile, tile];
			}
			else if (scale is float s && !e.TryGetProperty("frame", out _))
			{
				frame = [(int)MathF.Round(defaultFrame * s), (int)MathF.Round(defaultFrame * s)];
			}
			if (frame.Length != 2 || frame.Any(v => v == 0))
			{
				Problem(entry, "field 'frame' must be [width, height], each at least 1");
				return null;
			}
			if (file is null || Header(file, entry) is not { } header)
			{
				return null;
			}
			if (header.Width % frame[0] != 0 || header.Height % frame[1] != 0)
			{
				Problem(entry, $"{header.Width}x{header.Height} does not divide into {frame[0]}x{frame[1]} frames", file);
				return null;
			}
			int? foot = scaled && footed && e.TryGetProperty("foot", out _) ? Int(e, "foot", entry, frame[1], 1, frame[1]) : null;
			return new GridImage(file, frame[0], frame[1]) { Columns = header.Width / frame[0], Rows = header.Height / frame[1], Scale = scale, Foot = foot };
		}

		private Png.Header? Header(string file, string entry)
		{
			string full = m.Full(file);
			if (!File.Exists(full))
			{
				Problem(entry, "missing file", file);
				return null;
			}
			if (Png.ReadHeader(full, out string problem) is not { } header)
			{
				Problem(entry, problem, file);
				return null;
			}
			return header;
		}

		// An image used whole: it only has to be a readable PNG.
		// `palette`: the picture's colors in order, as a list of "#rrggbb" or a PNG strip whose top row holds them left to right.
		private (IReadOnlyList<Color>? Colors, string? File) Palette(JsonElement e, string entry)
		{
			if (!e.TryGetProperty("palette", out JsonElement p))
			{
				return (null, null);
			}
			if (p.ValueKind == JsonValueKind.String)
			{
				string file = p.GetString()!;
				if (Header(file, entry) is not { } header)
				{
					return (null, null);
				}
				if (header.Width > 256)
				{
					Problem(entry, $"field 'palette' must be a strip of at most 256 colors, not {header.Width}", file);
					return (null, null);
				}
				Color[] pixels = Png.Read(m.Full(file), out int width, out _);
				return ([.. pixels.Take(width)], file);
			}
			var colors = new List<Color>();
			if (p.ValueKind == JsonValueKind.Array && p.GetArrayLength() is >= 1 and <= 256)
			{
				foreach (JsonElement c in p.EnumerateArray())
				{
					string? text = c.ValueKind == JsonValueKind.String ? c.GetString() : null;
					if (text is not { Length: 7 } || text[0] != '#' || !uint.TryParse(text[1..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint v))
					{
						break;
					}
					colors.Add(new Color((int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255));
				}
				if (colors.Count == p.GetArrayLength())
				{
					return (colors, null);
				}
			}
			Problem(entry, "field 'palette' must be a list of 1 to 256 colors as #rrggbb, or a PNG strip of them");
			return (null, null);
		}

		private string? Picture(JsonElement e, string name, string entry, bool required = true)
		{
			string? file = Str(e, name, entry, required);
			return file is not null && Header(file, entry) is not null ? file : null;
		}

		private int[] Cells(int[] cells, GridImage? image, string entry, string what)
		{
			foreach (int bad in cells.Where(c => image is not null && c >= image.Cells).Take(1))
			{
				Problem(entry, $"{what} names frame {bad}, but the image has {image!.Cells} frames of {image.FrameWidth}x{image.FrameHeight}", image.Path);
			}
			return cells;
		}

		// `inline` when the frames and durations sit in the entry itself, which checks its own fields.
		private FrameRun? Run(JsonElement e, string entry, GridImage? image, int defaultMs, bool inline = false)
		{
			if (!IsObject(e, entry))
			{
				return null;
			}
			if (!inline)
			{
				Fields(e, entry, Schema["frame_run"]);
			}
			int[] frames = Cells(Ints(e, "frames", entry, required: true), image, entry, "'frames'");
			int[] ms = e.TryGetProperty("ms", out JsonElement msValue) && msValue.ValueKind == JsonValueKind.Number
				? [.. frames.Select(_ => Int(e, "ms", entry, defaultMs, 1, 60000))]
				: e.TryGetProperty("ms", out _) ? Ints(e, "ms", entry) : [.. frames.Select(_ => defaultMs)];
			if (frames.Length == 0 || ms.Length != frames.Length || ms.Any(v => v == 0))
			{
				Problem(entry, "needs at least one frame and one duration from 1 ms for each frame ('ms' a number or a list as long as 'frames')");
				return null;
			}
			return new FrameRun(frames, ms);
		}

		private Dictionary<Direction, int[]> Facing(JsonElement e, string entry, GridImage? image, string what)
		{
			var result = new Dictionary<Direction, int[]>();
			if (!IsObject(e, entry))
			{
				return result;
			}
			Fields(e, entry, Schema["facings"]);
			for (int i = 0; i < Facings.Length; i++)
			{
				if (e.TryGetProperty(Facings[i], out _))
				{
					result[(Direction)i] = Cells(Ints(e, Facings[i], entry), image, entry, $"{what} '{Facings[i]}'");
				}
			}
			return result;
		}

		private IEnumerable<(JsonElement Item, string Entry)> List(JsonElement root, string name)
		{
			if (!root.TryGetProperty(name, out JsonElement list))
			{
				yield break;
			}
			if (list.ValueKind != JsonValueKind.Array)
			{
				Problem(name, "must be a list");
				yield break;
			}
			int i = 0;
			foreach (JsonElement item in list.EnumerateArray())
			{
				string entry = $"{name}[{i++}]";
				if (IsObject(item, entry))
				{
					yield return (item, entry);
				}
			}
		}

		private void Known(string? id, IReadOnlySet<string> ids, string entry, string what)
		{
			if (id is not null && !ids.Contains(id))
			{
				Problem(entry, $"unknown {what} '{id}': not in the game data");
			}
		}

		// The scope fields of a tileset or backdrop: exactly one of the area kind, the area id or (backdrops) the encounter, the
		// terrain under the party or a flag, which may add an area; a backdrop with an id may have none.
		private (string? Kind, string? Area, string? Encounter, string? Terrain, string? Flag) Scope(JsonElement e, string entry, bool backdrop)
		{
			string? kind = Str(e, "kind", entry, required: false);
			string? area = Str(e, "area", entry, required: false);
			string? enc = backdrop ? Str(e, "encounter", entry, required: false) : null;
			string? terrain = backdrop ? Str(e, "terrain", entry, required: false) : null;
			string? flag = backdrop ? Str(e, "flag", entry, required: false) : null;
			int scopes = new[] { kind, area, enc, terrain }.Count(v => v is not null);
			if (flag is not null ? kind is not null || enc is not null || terrain is not null : scopes != 1 && !(backdrop && scopes == 0 && e.TryGetProperty("id", out _)))
			{
				Problem(entry, backdrop ? "needs exactly one of 'kind', 'area', 'encounter', 'terrain' and 'flag' (a flag may add 'area'), or only an 'id' for the backdrop maps"
					: "needs exactly one of 'kind' and 'area'");
			}
			Known(flag, data.Flags, entry, "flag");
			if (kind is not null && kind is not ("town" or "field" or "dungeon"))
			{
				Problem(entry, $"unknown area kind '{kind}': town, field or dungeon");
			}
			if (terrain is not null && !TerrainNames.Contains(terrain))
			{
				Problem(entry, $"unknown terrain '{terrain}': {string.Join(", ", TerrainNames)}");
			}
			Known(area, data.Areas, entry, "area");
			Known(enc, data.Encounters, entry, "encounter");
			return (kind, area, enc, terrain, flag);
		}

		public void Root(JsonElement root)
		{
			if (!IsObject(root, "-"))
			{
				return;
			}
			Fields(root, "-", Schema["-"]);
			if (Int(root, "version", "-", 0, 0, int.MaxValue) != Version)
			{
				Problem("-", $"field 'version' must be {Version}");
			}
			foreach ((JsonElement e, string entry) in List(root, "tilesets"))
			{
				Tileset(e, entry);
			}
			foreach ((JsonElement e, string entry) in List(root, "characters"))
			{
				Character(e, entry);
			}
			Portraits(root);
			foreach ((JsonElement e, string entry) in List(root, "figures"))
			{
				Figure(e, entry);
			}
			foreach ((JsonElement e, string entry) in List(root, "field_enemies"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, Schema["field_enemies[]"]);
				string? encounter = Str(e, "encounter", entry);
				Known(encounter, data.Encounters, entry, "encounter");
				GridImage? image = Grid(e, entry, Pixels.Size, scaled: true);
				if (Run(e, entry, image, 280, inline: true) is { } run && encounter is not null && image is not null && m.Problems.Count == before)
				{
					m.FieldEnemies.Add(new FieldEnemyEntry(encounter, image, run));
				}
			}
			foreach ((JsonElement e, string entry) in List(root, "vehicles"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, Schema["vehicles[]"]);
				string? id = Str(e, "id", entry);
				Known(id, data.FieldVehicles, entry, "vehicle");
				GridImage? image = Grid(e, entry, Pixels.Size, scaled: true);
				Dictionary<Direction, int[]> facings = e.TryGetProperty("facings", out JsonElement f) ? Facing(f, entry + ".facings", image, "facings") : [];
				if (facings.Count != 4 || facings.Values.Any(v => v.Length == 0))
				{
					Problem(entry, "'facings' needs north, east, south and west, each at least one frame");
				}
				int ms = Int(e, "ms", entry, 240, 1, 60000);
				if (id is not null && image is not null && m.Problems.Count == before)
				{
					m.Vehicles.Add(new VehicleEntry(id, image, facings, ms));
				}
			}
			foreach ((JsonElement e, string entry) in List(root, "backdrops"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, Schema["backdrops[]"]);
				(string? kind, string? area, string? encounter, string? terrain, string? flag) = Scope(e, entry, backdrop: true);
				string? id = Str(e, "id", entry, required: false);
				if (Picture(e, "image", entry) is { } file && m.Problems.Count == before)
				{
					m.Backdrops.Add(new BackdropEntry(kind, area, encounter, terrain, file, id, flag));
				}
			}
			foreach ((JsonElement e, string entry) in List(root, "backdrop_maps"))
			{
				BackdropMapEntry(e, entry);
			}
			foreach ((JsonElement e, string entry) in List(root, "effects"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, Schema["effects[]"]);
				string? id = Str(e, "id", entry);
				GridImage? image = Grid(e, entry, Art.EffectSize, scaled: true, footed: false);
				(IReadOnlyList<Color>? palette, string? paletteFile) = Palette(e, entry);
				if (Run(e, entry, image, 60, inline: true) is { } run && id is not null && image is not null && m.Problems.Count == before)
				{
					m.Effects.Add(new EffectEntry(id, image, run, palette, paletteFile));
				}
			}
			Sprites(root);
			Named(root, "status_icons", (id, entry) => Known(id, data.Statuses, entry, "status"), m.StatusIcons);
			Named(root, "command_icons", (id, entry) =>
			{
				if (!CommandNames.Contains(id))
				{
					Problem(entry, $"unknown command '{id}': {string.Join(", ", CommandNames)}");
				}
			}, m.CommandIcons);
			Named(root, "pictures", (id, entry) =>
			{
				if (!id.All(c => char.IsAsciiLetterOrDigit(c) || c == '_'))
				{
					Problem(entry, "a picture's name is letters, digits and '_'");
				}
			}, m.Pictures);
			if (root.TryGetProperty("keepers", out JsonElement keepers) && IsObject(keepers, "keepers"))
			{
				foreach (JsonProperty k in keepers.EnumerateObject())
				{
					string entry = "keepers." + k.Name;
					int before = m.Problems.Count;
					Known(k.Name, data.Keepers, entry, "shop or rest point");
					string? portrait = k.Value.ValueKind == JsonValueKind.String ? k.Value.GetString() : null;
					if (portrait is not { Length: > 0 })
					{
						Problem(entry, "must be a portrait id");
					}
					else if (!data.Portraits.Contains(portrait) && !data.Characters.Contains(portrait) && !data.Actors.Contains(portrait))
					{
						Problem(entry, $"unknown portrait '{portrait}': not a portrait, character or actor in the game data");
					}
					if (m.Problems.Count == before)
					{
						m.Keepers[k.Name] = portrait!;
					}
				}
			}
			if (root.TryGetProperty("ui", out JsonElement ui))
			{
				Ui(ui);
			}
			foreach ((JsonElement e, string entry) in List(root, "audio"))
			{
				Audio(e, entry);
			}
			if (root.TryGetProperty("ui_layout", out JsonElement ul) && IsObject(ul, "ui_layout"))
			{
				Fields(ul, "ui_layout", Schema["ui_layout"]);
				m.UiLayoutEnabled = Bool(ul, "enabled", "ui_layout", true);
			}
			if (root.TryGetProperty("battle_figures", out JsonElement bf) && IsObject(bf, "battle_figures"))
			{
				Fields(bf, "battle_figures", Schema["battle_figures"]);
				int reference = Int(bf, "reference_height", "battle_figures", 0, 1, 720);
				m.ReferenceHeight = reference > 0 ? reference : null;
				m.FigureShadows = Bool(bf, "shadows", "battle_figures", true);
			}
			foreach ((string entry, FigureEntry f) in _natural.Where(n => m.ReferenceHeight is null))
			{
				Problem(entry, "'natural': true needs battle_figures' reference_height, the screen height the figure was drawn for");
				m.Figures.Remove(f);
			}
			if (root.TryGetProperty("bitmap_fonts", out JsonElement fonts))
			{
				BitmapFonts(fonts);
			}
			Maps(root);
			if (root.TryGetProperty("title_art", out JsonElement title))
			{
				TitleArt(title);
			}
			foreach ((JsonElement e, string entry) in List(root, "objects"))
			{
				Object(e, entry);
			}
			if (root.TryGetProperty("field", out JsonElement field))
			{
				Field(field);
			}
			foreach ((JsonElement e, string entry) in List(root, "vehicle_overlays"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, Schema["vehicle_overlays[]"]);
				string? vehicle = Str(e, "vehicle", entry, required: false);
				if (vehicle is not null && !data.Vehicles.Contains(vehicle))
				{
					Problem(entry, $"unknown vehicle '{vehicle}': not a vehicle in the game data");
				}
				if (Picture(e, "image", entry) is { } file && m.Problems.Count == before)
				{
					m.VehicleOverlays.Add(new OverlayEntry(vehicle, file));
				}
			}
			void Once<T>(IEnumerable<T> items, Func<T, string> key, string list)
			{
				foreach (string twice in items.GroupBy(key).Where(g => g.Count() > 1).Select(g => g.Key))
				{
					Problem(list, $"{twice} is given more than once");
				}
			}
			Once(m.Tilesets, t => t.Kind is null ? "area " + t.Area : "kind " + t.Kind, "tilesets");
			Once(m.Characters, c => c.Id, "characters");
			Once(m.Figures, f => (f.Enemy ? "enemy " : f.Back ? "party member from behind " : "party member ") + f.Id, "figures");
			Once(m.FieldEnemies, f => f.Encounter, "field_enemies");
			Once(m.Vehicles, v => v.Id, "vehicles");
			Once(m.Backdrops.Where(b => b.Key != "id:" + b.Id), b => b.Key.Replace(':', ' '), "backdrops");
			Once(m.Backdrops.Where(b => b.Id is not null), b => "id " + b.Id, "backdrops");
			Once(m.BackdropMaps, b => b.Area, "backdrop_maps");
			Once(m.Effects, e => e.Id, "effects");
			Once(m.Audio, a => $"{a.Kind.ToString().ToLowerInvariant()} {a.Id}", "audio");
			Once(m.Objects, o => o.Key, "objects");
			Once(m.VehicleOverlays, o => o.Vehicle ?? "the overlay for every vehicle", "vehicle_overlays");
			if (root.TryGetProperty("battle_animation", out _) && Str(root, "battle_animation", "-") is { } animation)
			{
				if (!File.Exists(m.Full(animation)))
				{
					Problem("battle_animation", "missing file", animation);
				}
				else
				{
					m.BattleAnimation = animation;
				}
			}
			if (root.TryGetProperty("battle_presentation", out _) && Str(root, "battle_presentation", "-") is { } table)
			{
				if (!File.Exists(m.Full(table)))
				{
					Problem("battle_presentation", "missing file", table);
				}
				else
				{
					m.BattlePresentation = table;
				}
			}
		}

		// An object of id to an image file used whole.
		private void Named(JsonElement root, string name, Action<string, string> check, Dictionary<string, string> into)
		{
			if (!root.TryGetProperty(name, out JsonElement map))
			{
				return;
			}
			if (!IsObject(map, name))
			{
				return;
			}
			foreach (JsonProperty p in map.EnumerateObject())
			{
				string entry = $"{name}.{p.Name}";
				int before = m.Problems.Count;
				check(p.Name, entry);
				if (p.Value.ValueKind != JsonValueKind.String)
				{
					Problem(entry, "must be an image file name");
				}
				else if (Header(p.Value.GetString()!, entry) is not null && m.Problems.Count == before)
				{
					into[p.Name] = p.Value.GetString()!;
				}
			}
		}

		// Sprites by name: a file, or an object of its `image` and `palettes`.
		private void Sprites(JsonElement root)
		{
			if (!root.TryGetProperty("sprites", out JsonElement map) || !IsObject(map, "sprites"))
			{
				return;
			}
			foreach (JsonProperty p in map.EnumerateObject())
			{
				string entry = $"sprites.{p.Name}";
				int before = m.Problems.Count;
				if (!SpriteNames.Contains(p.Name))
				{
					Problem(entry, $"unknown sprite '{p.Name}': {string.Join(", ", SpriteNames)}");
				}
				string? file = null;
				List<PaletteTrack> palettes = [];
				if (p.Value.ValueKind == JsonValueKind.Object)
				{
					Fields(p.Value, entry, Schema["sprites{}"]);
					file = Str(p.Value, "image", entry);
					palettes = Palettes(p.Value, entry);
				}
				else if (p.Value.ValueKind == JsonValueKind.String)
				{
					file = p.Value.GetString()!;
				}
				else
				{
					Problem(entry, "must be an image file name, or {\"image\": FILE, \"palettes\": [...]}");
				}
				if (file is not null && Header(file, entry) is not null && m.Problems.Count == before)
				{
					m.Sprites[p.Name] = file;
					if (palettes.Count > 0)
					{
						m.SpritePalettes[p.Name] = palettes;
					}
				}
			}
		}

		// Portraits by id: a file, or an object of a default `image` and named `expressions` (shown by
		// `{portrait:ID:EXPRESSION}`), each kept as `ID:EXPRESSION`.
		private void Portraits(JsonElement root)
		{
			if (!root.TryGetProperty("portraits", out JsonElement map) || !IsObject(map, "portraits"))
			{
				return;
			}
			foreach (JsonProperty p in map.EnumerateObject())
			{
				string entry = "portraits." + p.Name;
				int before = m.Problems.Count;
				if (!data.Portraits.Contains(p.Name) && !data.Characters.Contains(p.Name) && !data.Actors.Contains(p.Name))
				{
					Problem(entry, $"unknown portrait '{p.Name}': not a portrait, character or actor in the game data");
				}
				var files = new Dictionary<string, string>();
				if (p.Value.ValueKind == JsonValueKind.String)
				{
					if (Header(p.Value.GetString()!, entry) is not null)
					{
						files[p.Name] = p.Value.GetString()!;
					}
				}
				else if (p.Value.ValueKind == JsonValueKind.Object)
				{
					Fields(p.Value, entry, Schema["portraits{}"]);
					if (Picture(p.Value, "image", entry, required: false) is { } image)
					{
						files[p.Name] = image;
					}
					if (p.Value.TryGetProperty("expressions", out JsonElement expressions) && IsObject(expressions, entry + ".expressions"))
					{
						foreach (JsonProperty x in expressions.EnumerateObject())
						{
							string xEntry = $"{entry}.expressions.{x.Name}";
							if (x.Name.Length == 0 || !x.Name.All(c => char.IsAsciiLetterOrDigit(c) || c == '_'))
							{
								Problem(xEntry, "an expression's name is letters, digits and '_'");
							}
							else if (x.Value.ValueKind != JsonValueKind.String)
							{
								Problem(xEntry, "must be an image file name");
							}
							else if (Header(x.Value.GetString()!, xEntry) is not null)
							{
								files[$"{p.Name}:{x.Name}"] = x.Value.GetString()!;
							}
						}
					}
					if (!p.Value.TryGetProperty("image", out _) && !p.Value.TryGetProperty("expressions", out _))
					{
						Problem(entry, "needs 'image', 'expressions' or both");
					}
				}
				else
				{
					Problem(entry, "must be an image file name, or an object with 'image' and 'expressions'");
				}
				if (m.Problems.Count == before)
				{
					foreach ((string id, string file) in files)
					{
						m.Portraits[id] = file;
					}
				}
			}
		}

		// A backdrop per cell of an area: a picture of one pixel a cell whose colors `colors` maps to backdrop ids
		// (transparent pixels give none), or named rectangles of cells, inside the area and not overlapping.
		private void BackdropMapEntry(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, Schema["backdrop_maps[]"]);
			string? area = Str(e, "area", entry);
			Known(area, data.Areas, entry, "area");
			AreaMap? map = area is null ? null : data.Maps.GetValueOrDefault(area);
			HashSet<string> ids = [.. m.Backdrops.Select(b => b.Id).OfType<string>()];
			string? file = Str(e, "image", entry, required: false);
			if (e.TryGetProperty("image", out _) == e.TryGetProperty("regions", out _))
			{
				Problem(entry, "needs exactly one of 'image' (with 'colors') and 'regions'");
			}
			string? Backdrop(JsonElement value, string where)
			{
				if (value.ValueKind == JsonValueKind.String && ids.Contains(value.GetString()!))
				{
					return value.GetString();
				}
				Problem(where, $"names no backdrop: {value} is not the 'id' of a backdrop entry");
				return null;
			}
			var cells = new (string Backdrop, string Key)?[map is null ? 0 : map.Width * map.Height];
			// Each color the entry names, with its backdrop (null when that names none).
			var colors = new Dictionary<Color, string?>();
			if (e.TryGetProperty("colors", out JsonElement cs) && IsObject(cs, entry + ".colors"))
			{
				foreach (JsonProperty c in cs.EnumerateObject())
				{
					string cEntry = $"{entry}.colors.{c.Name}";
					if (c.Name.Length != 7 || c.Name[0] != '#' || !uint.TryParse(c.Name[1..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint rgb))
					{
						Problem(cEntry, "a color is #rrggbb");
						continue;
					}
					colors[new Color((int)(rgb >> 16) & 255, (int)(rgb >> 8) & 255, (int)rgb & 255)] = Backdrop(c.Value, cEntry);
				}
			}
			else if (file is not null)
			{
				Problem(entry, "an 'image' needs 'colors': each of its colors to the id of a backdrop");
			}
			if (file is not null && map is not null && Header(file, entry) is { } header)
			{
				if (header.Width != map.Width || header.Height != map.Height)
				{
					Problem(entry, $"{header.Width}x{header.Height} is not the area's {map.Width}x{map.Height} cells at one pixel a cell", file);
				}
				else
				{
					Color[] pixels = Png.Read(m.Full(file), out _, out _);
					var unnamed = new SortedSet<string>(StringComparer.Ordinal);
					int partial = 0;
					for (int i = 0; i < pixels.Length; i++)
					{
						Color c = pixels[i];
						if (c.A is > 0 and < 255)
						{
							partial++;
						}
						else if (c.A == 255 && !colors.TryGetValue(c, out string? id))
						{
							unnamed.Add($"#{c.R:x2}{c.G:x2}{c.B:x2}");
						}
						else if (c.A == 255 && colors[c] is { } named)
						{
							cells[i] = (named, $"map:{area}:{named}");
						}
					}
					if (unnamed.Count > 0)
					{
						Problem(entry, $"colors 'colors' does not name: {string.Join(", ", unnamed.Take(8))}{(unnamed.Count > 8 ? $" and {unnamed.Count - 8} more" : "")}", file);
					}
					if (partial > 0)
					{
						Problem(entry, $"{partial} pixels are neither transparent nor opaque", file);
					}
				}
			}
			var regions = new List<(string Name, Rectangle Cells)>();
			if (e.TryGetProperty("regions", out JsonElement rs) && IsObject(rs, entry + ".regions"))
			{
				foreach (JsonProperty r in rs.EnumerateObject())
				{
					string rEntry = $"{entry}.regions.{r.Name}";
					if (!IsObject(r.Value, rEntry))
					{
						continue;
					}
					Fields(r.Value, rEntry, Schema["backdrop_maps[].regions{}"]);
					int[] rect = Ints(r.Value, "rect", rEntry, required: true);
					string? id = r.Value.TryGetProperty("backdrop", out JsonElement b) ? Backdrop(b, rEntry) : Str(r.Value, "backdrop", rEntry);
					if (rect.Length != 4 || rect[2] == 0 || rect[3] == 0)
					{
						Problem(rEntry, "field 'rect' must be [x, y, width, height] in cells, the width and height at least 1");
						continue;
					}
					var region = new Rectangle(rect[0], rect[1], rect[2], rect[3]);
					if (map is not null && (region.Right > map.Width || region.Bottom > map.Height))
					{
						Problem(rEntry, $"[{string.Join(", ", rect)}] reaches past the area's {map.Width}x{map.Height} cells");
						continue;
					}
					foreach ((string other, _) in regions.Where(o => o.Cells.Intersects(region)))
					{
						Problem(rEntry, $"overlaps region '{other}'");
					}
					regions.Add((r.Name, region));
					for (int y = region.Top; id is not null && map is not null && y < region.Bottom; y++)
					{
						for (int x = region.Left; x < region.Right; x++)
						{
							cells[y * map.Width + x] = (id, $"region:{area}:{r.Name}");
						}
					}
				}
			}
			if (area is not null && map is not null && m.Problems.Count == before)
			{
				m.BackdropMaps.Add(new BackdropMap(area, file, map.Width, map.Height, cells));
			}
		}

		private void TitleArt(JsonElement e)
		{
			if (!IsObject(e, "title_art"))
			{
				return;
			}
			int before = m.Problems.Count;
			Fields(e, "title_art", Schema["title_art"]);
			(float X, float Y) Signed(JsonElement owner, string name, string entry)
			{
				if (!owner.TryGetProperty(name, out JsonElement v))
				{
					return (0, 0);
				}
				if (v.ValueKind != JsonValueKind.Array || v.GetArrayLength() != 2 || v.EnumerateArray().Any(n => n.ValueKind != JsonValueKind.Number || MathF.Abs(n.GetSingle()) > 4096))
				{
					Problem(entry, $"field '{name}' must be [x, y], each a number from -4096 to 4096");
					return (0, 0);
				}
				return (v[0].GetSingle(), v[1].GetSingle());
			}
			(GridImage, FrameRun)? Sprite(string name, int defaultMs)
			{
				string entry = "title_art." + name;
				if (!e.TryGetProperty(name, out JsonElement f) || !IsObject(f, entry))
				{
					return null;
				}
				Fields(f, entry, Schema["title_art.traveler"]);
				GridImage? image = Grid(f, entry, Pixels.Size);
				return Run(f, entry, image, defaultMs, inline: true) is { } run && image is not null ? (image, run) : null;
			}
			var layers = new List<TitleLayer>();
			foreach ((JsonElement l, string listEntry) in List(e, "layers"))
			{
				string entry = "title_art." + listEntry;
				Fields(l, entry, Schema["title_art.layers[]"]);
				GridImage? image = Grid(l, entry, Pixels.Size);
				FrameRun? run = Run(l, entry, image, 200, inline: true);
				(float x, float y) = Signed(l, "at", entry);
				(float dx, float dy) = Signed(l, "scroll", entry);
				int scale = Int(l, "scale", entry, 1, 1, 16);
				if (image is not null && run is not null)
				{
					layers.Add(new TitleLayer(image, run, (int)MathF.Round(x), (int)MathF.Round(y), scale, dx, dy));
				}
			}
			var title = new TitleArtEntry(Picture(e, "background", "title_art", required: false), Picture(e, "logo", "title_art", required: false), layers,
				Sprite("traveler", 260), Sprite("creature", 400), e.TryGetProperty("reference_height", out _) ? Int(e, "reference_height", "title_art", 0, 1, 4320) : null);
			if (m.Problems.Count == before)
			{
				m.TitleArt = title;
			}
		}

		// A whole-area picture must cover the area's grid cell for cell.
		private bool MapPicture(string file, string entry, AreaMap map, int cell)
		{
			if (Header(file, entry) is not { } header)
			{
				return false;
			}
			if (header.Width != map.Width * cell || header.Height != map.Height * cell)
			{
				Problem(entry, $"{header.Width}x{header.Height} is not the area's {map.Width}x{map.Height} cells at {cell} pixels a cell ({map.Width * cell}x{map.Height * cell})", file);
				return false;
			}
			return true;
		}

		private void AddMap(string area, string entry, int cell, string? ground, string? above, IEnumerable<(string Flag, string File)> patches)
		{
			int before = m.Problems.Count;
			AreaMap map = data.Maps[area];
			if (ground is null)
			{
				Problem(entry, $"an above picture or a change's picture needs the ground picture (maps/{area}.png or 'image')");
			}
			else
			{
				MapPicture(ground, entry, map, cell);
			}
			if (above is not null)
			{
				MapPicture(above, entry, map, cell);
			}
			var list = new List<MapPatch>();
			foreach ((string flag, string file) in patches)
			{
				(int X, int Y)[] cells = [.. map.Changes.Where(c => c.Flag == flag).SelectMany(c => c.Cells).Distinct()];
				if (cells.Length == 0)
				{
					Problem(entry, $"unknown change flag '{flag}': not a flag of the area's changes ({(map.Changes.Count == 0 ? "it has none" : string.Join(", ", map.Changes.Select(c => c.Flag).Distinct()))})");
				}
				else if (MapPicture(file, entry, map, cell))
				{
					list.Add(new MapPatch(flag, file, cells));
				}
			}
			if (ground is not null && m.Problems.Count == before && m.MapsEnabled)
			{
				m.Maps.Add(new MapEntry(area, ground, above, cell, list));
			}
		}

		// The whole-area pictures: explicit entries, then for every other area the files maps/AREA.png, maps/AREA_above.png
		// and maps/AREA@FLAG.png when they exist. Any other picture in maps/ that no entry names is a problem, since it
		// is most likely a misspelled area.
		private void Maps(JsonElement root)
		{
			int cell = Pixels.Size;
			var named = new HashSet<string>();
			var explicitAreas = new HashSet<string>();
			if (root.TryGetProperty("maps", out JsonElement maps) && IsObject(maps, "maps"))
			{
				Fields(maps, "maps", Schema["maps"]);
				m.MapsEnabled = Bool(maps, "enabled", "maps", true);
				cell = Int(maps, "cell", "maps", Pixels.Size, 1, 256);
				foreach ((JsonElement e, string entry) in List(maps, "areas"))
				{
					string full = "maps." + entry;
					Fields(e, full, Schema["maps.areas[]"]);
					string? area = Str(e, "area", full);
					Known(area, data.Areas, full, "area");
					string? ground = Str(e, "image", full, required: false);
					string? above = Str(e, "above", full, required: false);
					var patches = new List<(string, string)>();
					if (e.TryGetProperty("changes", out JsonElement changes) && IsObject(changes, full + ".changes"))
					{
						foreach (JsonProperty p in changes.EnumerateObject())
						{
							if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString()!.Length == 0)
							{
								Problem($"{full}.changes.{p.Name}", "must be an image file name");
								continue;
							}
							patches.Add((p.Name, p.Value.GetString()!));
						}
					}
					named.UnionWith(new[] { ground, above }.OfType<string>().Concat(patches.Select(p => p.Item2)).Select(f => m.Full(f)));
					if (area is not null && data.Maps.ContainsKey(area))
					{
						explicitAreas.Add(area);
						int areaCell = Int(e, "cell", full, cell, 1, 256);
						bool moving = new[] { "palettes", "animated", "layers" }.Any(f => e.TryGetProperty(f, out _));
						if (ground is not null || above is not null || patches.Count > 0 || !moving)
						{
							AddMap(area, full, areaCell, ground, above, patches);
						}
						if (moving)
						{
							Motion(e, full, area, areaCell);
						}
					}
				}
			}
			string dir = m.Full("maps");
			if (!m.MapsEnabled || !System.IO.Directory.Exists(dir))
			{
				return;
			}
			var seen = new HashSet<string>(named);
			foreach (string area in data.Areas.Order(StringComparer.Ordinal).Where(a => !explicitAreas.Contains(a)))
			{
				string File(string name) => $"maps/{name}.png";
				string? Existing(string name) => System.IO.File.Exists(m.Full(File(name))) ? File(name) : null;
				string? ground = Existing(area), above = Existing(area + "_above");
				var patches = data.Maps[area].Changes.Select(c => c.Flag).Distinct().Select(f => (f, Existing($"{area}@{f}"))).Where(p => p.Item2 is not null)
					.Select(p => (p.f, p.Item2!)).ToList();
				seen.UnionWith(new[] { ground, above }.OfType<string>().Concat(patches.Select(p => p.Item2)).Select(f => m.Full(f)));
				if (ground is not null || above is not null || patches.Count > 0)
				{
					AddMap(area, "maps." + area, cell, ground, above, patches);
				}
			}
			foreach (string stray in System.IO.Directory.EnumerateFiles(dir, "*.png").Select(Path.GetFullPath).Where(f => !seen.Contains(f)).Order(StringComparer.Ordinal))
			{
				Problem("maps", "names no area: maps/AREA.png, maps/AREA_above.png or maps/AREA@FLAG.png, with an area of the game data and a flag of its changes",
					Path.GetRelativePath(m.Directory, stray).Replace('\\', '/'));
			}
		}

		// "#rrggbb": a palette matches opaque pixels by their color alone.
		private Color? Rgb(JsonElement v, string entry, string what)
		{
			string? text = v.ValueKind == JsonValueKind.String ? v.GetString() : null;
			if (text is not { Length: 7 } || text[0] != '#' || !uint.TryParse(text[1..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint c))
			{
				Problem(entry, $"{what} must be a color as #rrggbb");
				return null;
			}
			return new Color((byte)(c >> 16), (byte)(c >> 8), (byte)c, (byte)255);
		}

		// Numbers from min to max, exactly `count` of them.
		private float[]? Floats(JsonElement e, string name, string entry, int count, float min, float max)
		{
			if (!e.TryGetProperty(name, out JsonElement v))
			{
				return null;
			}
			if (v.ValueKind != JsonValueKind.Array || v.GetArrayLength() != count || v.EnumerateArray().Any(x => x.ValueKind != JsonValueKind.Number || x.GetSingle() < min || x.GetSingle() > max))
			{
				string F(float x) => x.ToString(CultureInfo.InvariantCulture);
				Problem(entry, $"field '{name}' must be {(count == 2 ? "[x, y]" : $"{count} numbers")}, each from {F(min)} to {F(max)}");
				return null;
			}
			return [.. v.EnumerateArray().Select(x => x.GetSingle())];
		}

		// One of a field's named values (`table` a Schema key), or the fallback when the field is left out.
		private string? OneOf(JsonElement e, string name, string entry, string table, string fallback)
		{
			string? value = Str(e, name, entry, required: false);
			if (value is null)
			{
				return e.TryGetProperty(name, out _) ? null : fallback;
			}
			if (!Schema[table].Contains(value))
			{
				Problem(entry, $"field '{name}' must be one of {string.Join(", ", Schema[table])}, not '{value}'");
				return null;
			}
			return value;
		}

		// Durations in 60000ths of a second from `ms` or `ticks` (60 Hz frames): a number for each of `count`, or a list as
		// long. Left out, every one lasts the same (unused: a single step or frame).
		private int[]? Units(JsonElement e, string entry, int count)
		{
			bool ms = e.TryGetProperty("ms", out JsonElement msValue), ticks = e.TryGetProperty("ticks", out JsonElement tickValue);
			if (ms && ticks)
			{
				Problem(entry, "takes 'ms' or 'ticks', not both");
				return null;
			}
			if (!ms && !ticks)
			{
				if (count > 1)
				{
					Problem(entry, "needs 'ms' or 'ticks' for more than one step or frame");
					return null;
				}
				return [.. Enumerable.Repeat(1, count)];
			}
			(string name, JsonElement v, int max, int unit) = ms ? ("ms", msValue, 60000, 60) : ("ticks", tickValue, 3600, 1000);
			if (v.ValueKind == JsonValueKind.Number)
			{
				int n = Int(e, name, entry, 0, 1, max);
				return n == 0 ? null : [.. Enumerable.Repeat(n * unit, count)];
			}
			int[] list = Ints(e, name, entry);
			if (list.Length != count || list.Any(x => x is < 1 || x > max))
			{
				Problem(entry, $"field '{name}' must be a number from 1 to {max} or a list of {count} of them, one for each step or frame");
				return null;
			}
			return [.. list.Select(x => x * unit)];
		}

		private MotionWhen? When(JsonElement e, string entry)
		{
			string? clock = OneOf(e, "clock", entry, "motion.clock", "global");
			string? flag = Str(e, "flag", entry, required: false), unless = Str(e, "unless_flag", entry, required: false);
			Known(flag, data.Flags, entry, "flag");
			Known(unless, data.Flags, entry, "flag");
			return clock is null ? null : new MotionWhen(clock == "entry", flag, unless);
		}

		// Palette tracks: named source colors and steps of replacements for them.
		private List<PaletteTrack> Palettes(JsonElement e, string entry)
		{
			var tracks = new List<PaletteTrack>();
			foreach ((JsonElement p, string trackEntry) in List(e, "palettes").Select(x => (x.Item, $"{entry}.{x.Entry}")))
			{
				int before = m.Problems.Count;
				Fields(p, trackEntry, Schema["palettes[]"]);
				var colors = new Dictionary<string, Color>();
				if (!p.TryGetProperty("colors", out JsonElement named) || named.ValueKind != JsonValueKind.Object || !named.EnumerateObject().Any())
				{
					Problem(trackEntry, "'colors' must name at least one source color: {\"NAME\": \"#rrggbb\"}");
				}
				else
				{
					foreach (JsonProperty c in named.EnumerateObject())
					{
						if (Rgb(c.Value, trackEntry, $"color '{c.Name}'") is Color color)
						{
							if (colors.FirstOrDefault(o => o.Value == color) is { Key: { } same })
							{
								Problem(trackEntry, $"colors '{same}' and '{c.Name}' are the same color");
							}
							colors[c.Name] = color;
						}
					}
				}
				var steps = new List<IReadOnlyList<(Color, Color)>>();
				if (!p.TryGetProperty("steps", out JsonElement list) || list.ValueKind != JsonValueKind.Array || list.GetArrayLength() == 0)
				{
					Problem(trackEntry, "'steps' must be a list of at least one step, each {\"NAME\": \"#rrggbb\"}");
				}
				else
				{
					int i = 0;
					foreach (JsonElement step in list.EnumerateArray())
					{
						string stepEntry = $"{trackEntry}.steps[{i++}]";
						var pairs = new List<(Color, Color)>();
						if (IsObject(step, stepEntry))
						{
							foreach (JsonProperty c in step.EnumerateObject())
							{
								if (!colors.TryGetValue(c.Name, out Color from) && named.ValueKind == JsonValueKind.Object && !named.TryGetProperty(c.Name, out _))
								{
									Problem(stepEntry, $"names no color '{c.Name}' of 'colors'");
								}
								else if (Rgb(c.Value, stepEntry, $"color '{c.Name}'") is Color to)
								{
									pairs.Add((from, to));
								}
							}
						}
						steps.Add(pairs);
					}
				}
				int[]? units = Units(p, trackEntry, Math.Max(1, steps.Count));
				MotionWhen? when = When(p, trackEntry);
				if (m.Problems.Count == before && units is not null && when is not null)
				{
					tracks.Add(new PaletteTrack([.. steps], units, when));
				}
			}
			return tracks;
		}

		// An area's palette tracks, frame runs and layers; any problem in them drops all three.
		private void Motion(JsonElement e, string entry, string area, int cell)
		{
			int before = m.Problems.Count;
			AreaMap map = data.Maps[area];
			int pw = map.Width * cell, ph = map.Height * cell;
			List<PaletteTrack> palettes = Palettes(e, entry);
			var runs = new List<AreaRun>();
			foreach ((JsonElement r, string runEntry) in List(e, "animated").Select(x => (x.Item, $"{entry}.{x.Entry}")))
			{
				Fields(r, runEntry, Schema["maps.areas[].animated[]"]);
				bool byCells = r.TryGetProperty("cells", out _), byPixels = r.TryGetProperty("pixels", out _);
				int[] rect = byCells == byPixels ? [] : Ints(r, byCells ? "cells" : "pixels", runEntry);
				if (byCells == byPixels)
				{
					Problem(runEntry, "needs exactly one of 'cells' and 'pixels': the rectangle [x, y, width, height] the frames are drawn on");
					continue;
				}
				int k = byCells ? cell : 1;
				if (rect.Length != 4 || rect[2] == 0 || rect[3] == 0 || (rect[0] + rect[2]) * k > pw || (rect[1] + rect[3]) * k > ph)
				{
					Problem(runEntry, $"field '{(byCells ? "cells" : "pixels")}' must be [x, y, width, height], width and height at least 1, inside the area's " +
						(byCells ? $"{map.Width}x{map.Height} cells" : $"{pw}x{ph} picture pixels at {cell} a cell"));
					continue;
				}
				(int x, int y, int w, int h) = (rect[0] * k, rect[1] * k, rect[2] * k, rect[3] * k);
				string? file = Str(r, "image", runEntry);
				GridImage? image = null;
				if (file is not null && Header(file, runEntry) is { } header)
				{
					if (header.Width % w != 0 || header.Height % h != 0)
					{
						Problem(runEntry, $"{header.Width}x{header.Height} does not divide into frames the rectangle's size ({w}x{h} picture pixels)", file);
					}
					else
					{
						image = new GridImage(file, w, h) { Columns = header.Width / w, Rows = header.Height / h };
					}
				}
				int[] frames = Cells(Ints(r, "frames", runEntry, required: true), image, runEntry, "'frames'");
				if (r.TryGetProperty("frames", out _) && frames.Length == 0)
				{
					Problem(runEntry, "'frames' needs at least one frame");
				}
				int[]? units = Units(r, runEntry, Math.Max(1, frames.Length));
				string? layer = OneOf(r, "layer", runEntry, "maps.areas[].animated[].layer", "ground");
				if (When(r, runEntry) is { } when && image is not null && units is not null && layer is not null && frames.Length > 0)
				{
					runs.Add(new AreaRun(image, x, y, w, h, frames, units, layer == "above", when));
				}
			}
			var layers = new List<AreaLayer>();
			foreach ((JsonElement l, string layerEntry) in List(e, "layers").Select(x => (x.Item, $"{entry}.{x.Entry}")))
			{
				Fields(l, layerEntry, Schema["maps.areas[].layers[]"]);
				string? file = Str(l, "image", layerEntry);
				Png.Header? header = file is null ? null : Header(file, layerEntry);
				string? place = OneOf(l, "place", layerEntry, "maps.areas[].layers[].place", "back");
				string? repeat = OneOf(l, "repeat", layerEntry, "maps.areas[].layers[].repeat", "both");
				float[] scroll = Floats(l, "scroll", layerEntry, 2, -16, 16) ?? [1, 1];
				float[] speed = Floats(l, "speed", layerEntry, 2, -10000, 10000) ?? [0, 0];
				float[] at = Floats(l, "at", layerEntry, 2, -65536, 65536) ?? [0, 0];
				var bands = new List<LayerBand>();
				foreach ((JsonElement b, string bandEntry) in List(l, "bands").Select(x => (x.Item, $"{layerEntry}.{x.Entry}")))
				{
					Fields(b, bandEntry, Schema["maps.areas[].layers[].bands[]"]);
					int[] rows = Ints(b, "rows", bandEntry, required: true);
					if (header is { } hd && (rows.Length != 2 || rows[0] > rows[1] || rows[1] >= hd.Height))
					{
						Problem(bandEntry, $"field 'rows' must be [first, last], rows of the picture (0 to {hd.Height - 1}), first at most last");
						continue;
					}
					if (rows.Length == 2 && bands.FirstOrDefault(o => o.First <= rows[1] && rows[0] <= o.Last) is { } other)
					{
						Problem(bandEntry, $"rows {rows[0]} to {rows[1]} overlap the band of rows {other.First} to {other.Last}");
					}
					if (rows.Length == 2)
					{
						bands.Add(new LayerBand(rows[0], rows[1], Float(b, "scroll", bandEntry, scroll[0], -16, 16), Float(b, "speed", bandEntry, speed[0], -10000, 10000)));
					}
				}
				LayerWave? wave = null;
				if (l.TryGetProperty("wave", out JsonElement wv) && IsObject(wv, layerEntry + ".wave"))
				{
					string waveEntry = layerEntry + ".wave";
					Fields(wv, waveEntry, Schema["maps.areas[].layers[].wave"]);
					string? axis = OneOf(wv, "axis", waveEntry, "maps.areas[].layers[].wave.axis", "lines");
					wave = new LayerWave(axis == "columns", Float(wv, "amplitude", waveEntry, 2, 0, 1024), Float(wv, "wavelength", waveEntry, 32, 1, 4096),
						Float(wv, "speed", waveEntry, 0, -10000, 10000));
					if (wave.Columns && l.TryGetProperty("bands", out _))
					{
						Problem(layerEntry, "a wave over columns does not go with 'bands', which move rows");
					}
				}
				if (When(l, layerEntry) is { } when && header is { } size && place is not null && repeat is not null)
				{
					layers.Add(new AreaLayer(file!, size.Width, size.Height, place == "front", scroll[0], scroll[1], speed[0], speed[1], repeat is "both" or "x",
						repeat is "both" or "y", bands, wave, when, at[0], at[1]));
				}
			}
			if (m.Problems.Count == before && m.MapsEnabled)
			{
				m.AreaMotions[area] = new AreaMotion(cell, palettes, runs, layers);
			}
		}

		// The look of an object in one state; `inherit` gives the picture, size and anchor a state leaves out.
		private ObjectState? State(JsonElement e, string entry, bool inline, ObjectState? inherit)
		{
			if (!inline)
			{
				Fields(e, entry, Schema["objects[].state"]);
			}
			GridImage? image = inherit is not null && !e.TryGetProperty("image", out _) ? inherit.Image : Grid(e, entry, Pixels.Size);
			FrameRun? run = Run(e, entry, image, 400, inline: true);
			int[] size = e.TryGetProperty("size", out _) ? Ints(e, "size", entry) : inherit is null ? [1, 1] : [inherit.Width, inherit.Height];
			if (size.Length != 2 || size.Any(v => v is < 1 or > 16))
			{
				Problem(entry, "field 'size' must be [width, height] in cells, each from 1 to 16");
				return null;
			}
			(int X, int Y) anchor = e.TryGetProperty("anchor", out _)
				? Pair(e, "anchor", entry, size[0], size[1], "inside the object's size") ?? (0, 0)
				: inherit is not null && inherit.Width == size[0] && inherit.Height == size[1] ? (inherit.AnchorX, inherit.AnchorY) : ((size[0] - 1) / 2, size[1] - 1);
			return run is null || image is null ? null : new ObjectState(image, run, size[0], size[1], anchor.X, anchor.Y);
		}

		private void Object(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, Schema["objects[]"]);
			string? area = Str(e, "area", entry);
			Known(area, data.Areas, entry, "area");
			AreaMap? map = area is null ? null : data.Maps.GetValueOrDefault(area);
			string? point = Str(e, "point", entry, required: false);
			(int X, int Y)? at = map is null ? null : Pair(e, "at", entry, map.Width, map.Height, "on the area's map");
			if ((point is null) == !e.TryGetProperty("at", out _))
			{
				Problem(entry, "needs exactly one of 'point' and 'at'");
			}
			if (point is not null && map is not null && !map.Points.Contains(point))
			{
				Problem(entry, $"unknown point '{point}': not a hook, chest, shop, rest point, port or helm of {area}");
			}
			else if (point is not null && data.Actors.Contains(point))
			{
				Problem(entry, $"point '{point}' is where the actor stands: give the actor a 'characters' entry instead");
			}
			string? character = Str(e, "character", entry, required: false);
			string? openFlag = Str(e, "open_flag", entry, required: false);
			Known(openFlag, data.Flags, entry, "flag");
			if ((character is null) == !e.TryGetProperty("image", out _))
			{
				Problem(entry, "needs exactly one of 'image' and 'character'");
			}
			if (character is not null)
			{
				if (!data.Characters.Contains(character) && !data.Actors.Contains(character))
				{
					Problem(entry, $"unknown character '{character}': not a party member or actor in the game data");
				}
				foreach (string field in new[] { "frame", "frames", "ms", "size", "anchor", "open", "open_flag" }.Where(f => e.TryGetProperty(f, out _)))
				{
					Problem(entry, $"field '{field}' goes with 'image', not 'character'");
				}
				string facing = Str(e, "facing", entry, required: false) ?? "south";
				int index = Array.IndexOf(Facings, facing);
				if (index < 0)
				{
					Problem(entry, $"unknown facing '{facing}': {string.Join(", ", Facings)}");
				}
				if (area is not null && m.Problems.Count == before)
				{
					m.Objects.Add(new ObjectEntry(area, point, at, character, (Direction)index, null, null, null));
				}
				return;
			}
			if (e.TryGetProperty("facing", out _))
			{
				Problem(entry, "field 'facing' goes with 'character': a sheet's frames already face one way");
			}
			ObjectState? closed = State(e, entry, inline: true, null);
			ObjectState? open = e.TryGetProperty("open", out JsonElement o) && IsObject(o, entry + ".open") ? State(o, entry + ".open", inline: false, closed) : null;
			bool chest = point is not null && map is not null && map.Chests.Contains(point);
			if (e.TryGetProperty("open", out _) && !chest && openFlag is null)
			{
				Problem(entry, "'open' needs a chest point or an 'open_flag'");
			}
			if (area is not null && closed is not null && m.Problems.Count == before)
			{
				m.Objects.Add(new ObjectEntry(area, point, at, null, Direction.South, closed, open, openFlag));
			}
		}

		private void Field(JsonElement e)
		{
			if (!IsObject(e, "field"))
			{
				return;
			}
			Fields(e, "field", Schema["field"]);
			m.Field = new FieldLook(Bool(e, "wall_shadows", "field", true), Bool(e, "water_foam", "field", true), Bool(e, "conveyors", "field", true),
				Bool(e, "tile_hooks", "field", true));
			if (e.TryGetProperty("areas", out JsonElement areas) && IsObject(areas, "field.areas"))
			{
				foreach (JsonProperty p in areas.EnumerateObject())
				{
					string entry = "field.areas." + p.Name;
					Known(p.Name, data.Areas, entry, "area");
					if (IsObject(p.Value, entry))
					{
						Fields(p.Value, entry, Schema["field.areas{}"]);
						m.FieldAreas[p.Name] = new FieldLook(Bool(p.Value, "wall_shadows", entry, m.Field.WallShadows), Bool(p.Value, "water_foam", entry, m.Field.WaterFoam),
							Bool(p.Value, "conveyors", entry, m.Field.Conveyors), Bool(p.Value, "tile_hooks", entry, m.Field.TileHooks));
					}
				}
			}
			if (e.TryGetProperty("conveyor_overlay", out JsonElement belt) && IsObject(belt, "field.conveyor_overlay"))
			{
				const string entry = "field.conveyor_overlay";
				int before = m.Problems.Count;
				Fields(belt, entry, Schema["field.conveyor_overlay"]);
				GridImage? image = Grid(belt, entry, Pixels.Size);
				Dictionary<Direction, int[]> facings = belt.TryGetProperty("facings", out JsonElement f) ? Facing(f, entry + ".facings", image, "facings") : [];
				if (facings.Count != 4 || facings.Values.Any(v => v.Length == 0))
				{
					Problem(entry, "'facings' needs north, east, south and west, each at least one frame");
				}
				int ms = Int(belt, "ms", entry, 120, 1, 60000);
				if (image is not null && m.Problems.Count == before)
				{
					m.ConveyorOverlay = new ConveyorOverlay(image, facings, ms);
				}
			}
			if (e.TryGetProperty("tile_hook_overlay", out JsonElement pad) && IsObject(pad, "field.tile_hook_overlay"))
			{
				const string entry = "field.tile_hook_overlay";
				int before = m.Problems.Count;
				Fields(pad, entry, Schema["field.tile_hook_overlay"]);
				GridImage? image = Grid(pad, entry, Pixels.Size);
				if (Run(pad, entry, image, 300, inline: true) is { } run && image is not null && m.Problems.Count == before)
				{
					m.TileHookOverlay = (image, run);
				}
			}
		}

		// The bitmap fonts: per language, for every role or one, a glyph sheet and its metrics. A font that lacks glyphs the
		// language's text tables use is a problem but still loads, those glyphs drawing in the fallback font.
		private void BitmapFonts(JsonElement e)
		{
			if (!IsObject(e, "bitmap_fonts"))
			{
				return;
			}
			Fields(e, "bitmap_fonts", Schema["bitmap_fonts"]);
			m.BitmapFontsEnabled = Bool(e, "enabled", "bitmap_fonts", true);
			foreach ((JsonElement f, string listEntry) in List(e, "fonts"))
			{
				string entry = "bitmap_fonts." + listEntry;
				int before = m.Problems.Count;
				Fields(f, entry, Schema["bitmap_fonts.fonts[]"]);
				string? language = Str(f, "language", entry);
				Known(language, data.Languages, entry, "language");
				string? roleName = Str(f, "role", entry, required: false);
				int roleIndex = roleName is null ? -1 : Array.IndexOf(RoleNames, roleName);
				if (roleName is not null && roleIndex < 0)
				{
					Problem(entry, $"unknown role '{roleName}': {string.Join(", ", RoleNames)}");
				}
				TextRole? role = roleIndex < 0 ? null : (TextRole)roleIndex;
				float scale = 1f;
				if (f.TryGetProperty("scale", out JsonElement sv))
				{
					if (sv.ValueKind == JsonValueKind.Number && sv.GetDouble() > 0 && sv.GetDouble() <= 64)
					{
						scale = (float)sv.GetDouble();
					}
					else
					{
						Problem(entry, "field 'scale' must be a number above 0, at most 64");
					}
				}
				string? image = Str(f, "image", entry);
				string? metricsFile = Str(f, "metrics", entry);
				Png.Header? header = image is null ? null : Header(image, entry);
				if (metricsFile is not null && !File.Exists(m.Full(metricsFile)))
				{
					Problem(entry, "missing file", metricsFile);
					continue;
				}
				if (header is not { } h || metricsFile is null || language is null)
				{
					continue;
				}
				FontMetrics? metrics = FontMetrics.Read(m.Full(metricsFile), h.Width, h.Height, reason => Problem(entry, reason, metricsFile));
				if (metrics is null || m.Problems.Count != before)
				{
					continue;
				}
				if (m.BitmapFonts.Any(b => b.Language == language && b.Role == role))
				{
					Problem(entry, $"a second font for {language}{(role is null ? "" : " " + roleName)}");
					continue;
				}
				m.BitmapFonts.Add(new BitmapFontEntry(language, role, image!, metricsFile, scale, metrics));
				char[] missing = [.. data.TextGlyphs(language, role).Where(c => !metrics.Glyphs.ContainsKey(c))];
				if (missing.Length > 0)
				{
					const int Listed = 60;
					string Shown(char c) => char.IsWhiteSpace(c) || char.IsControl(c) ? $"U+{(int)c:X4}" : c.ToString();
					Problem(entry, $"lacks {missing.Length} glyph{(missing.Length == 1 ? "" : "s")} the {language} text tables use{(role is null ? "" : $" for {roleName}")}: " +
						string.Concat(missing.Take(Listed).Select(Shown)) + (missing.Length > Listed ? $" and {missing.Length - Listed} more" : ""), metricsFile);
				}
			}
		}

		private void Tileset(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, Schema["tilesets[]"]);
			(string? kind, string? area, _, _, _) = Scope(e, entry, backdrop: false);
			Theme theme = kind switch { "town" => Theme.Town, "dungeon" => Theme.Dungeon, "field" => Theme.Field, _ => Art.ThemeOf(area is null ? AreaKind.Field : data.AreaKinds.GetValueOrDefault(area)) };
			if (!e.TryGetProperty("image", out _) && e.TryGetProperty("palettes", out _))
			{
				foreach (string field in Schema["tilesets[]"].Where(f => f is not ("kind" or "area" or "palettes") && e.TryGetProperty(f, out _)))
				{
					Problem(entry, $"field '{field}' needs 'image': an entry without one recolors the tiles its {(kind is null ? "area" : "kind")} shows with 'palettes' only");
				}
				List<PaletteTrack> recolor = Palettes(e, entry);
				if ((kind ?? area) is not null && m.Problems.Count == before)
				{
					m.TilesetRecolors.Add(new TilesetRecolor(kind, area, theme, recolor));
				}
				return;
			}
			GridImage? image = Grid(e, entry, Pixels.Size, scaled: true, footed: false);
			// Tiles lie edge to edge, so a scaled tile must come out exactly a cell wide at its snapped factor: the cell's
			// factor (the field's cell over 16) over the scale.
			float factor = cell / (float)Pixels.Size;
			if (image is { Scale: float scale } && image.FrameWidth * Art.Snap(factor / scale) is var drawn && MathF.Abs(drawn - cell) > 0.01f)
			{
				string F(float x) => x.ToString(CultureInfo.InvariantCulture);
				Problem(entry, $"a {image.FrameWidth}-pixel tile at scale {F(scale)} draws {F(drawn)} screen pixels a cell, not the field's {cell}: a tile is 16 x scale pixels, " +
					$"with {F(factor)} / scale a whole number or one over a whole number ({cell / 2} at {F(factor / 2)}, {cell} at {F(factor)}, {cell * 2} at {F(factor * 2)})");
			}
			int[] Group(string name, int least)
			{
				int[] cells = Cells(Ints(e, name, entry, required: true), image, entry, $"'{name}'");
				if (e.TryGetProperty(name, out _) && cells.Length < least)
				{
					Problem(entry, $"'{name}' needs at least {least} frame{(least == 1 ? "" : "s")}");
				}
				return cells;
			}
			int[] floor = Group("floor", 1), decor = Group("decor", 1), wallTop = Group("wall_top", 1), wallFace = Group("wall_face", 1), water = Group("water", 1);
			var objects = new Dictionary<string, int>();
			if (e.TryGetProperty("objects", out JsonElement o) && IsObject(o, entry + ".objects"))
			{
				Fields(o, entry + ".objects", Schema["tilesets[].objects"]);
				foreach (string name in ObjectNames.Where(n => o.TryGetProperty(n, out _)))
				{
					int cell = Int(o, name, entry + ".objects", -1, 0, int.MaxValue);
					if (cell >= 0)
					{
						objects[name] = Cells([cell], image, entry, $"object '{name}'")[0];
					}
				}
			}
			var animated = new Dictionary<int, FrameRun>();
			int[] stills = [.. floor, .. decor, .. wallTop, .. wallFace];
			foreach ((JsonElement a, string animEntry) in List(e, "animated").Select(x => (x.Item, $"{entry}.{x.Entry}")))
			{
				Fields(a, animEntry, Schema["tilesets[].animated[]"]);
				int tile = Int(a, "tile", animEntry, -1, 0, int.MaxValue);
				if (tile < 0)
				{
					Problem(animEntry, "missing field 'tile': the frame the animation stands in for");
				}
				else if (!stills.Contains(tile))
				{
					Problem(animEntry, $"tile {tile} is not one of the tileset's floor, decor, wall_top or wall_face frames (water animates by itself)");
				}
				else if (animated.ContainsKey(tile))
				{
					Problem(animEntry, $"tile {tile} is animated more than once");
				}
				if (Run(a, animEntry, image, 200, inline: true) is { } run && tile >= 0)
				{
					animated[tile] = run;
				}
			}
			List<PaletteTrack> palettes = Palettes(e, entry);
			(Color edge, Color bank) = Art.DefaultEdges(theme);
			if (image is not null && (kind ?? area) is not null && m.Problems.Count == before)
			{
				m.Tilesets.Add(new TilesetEntry(kind, area, theme, image, floor, decor, Int(e, "decor_one_in", entry, 11, 1, 1000), wallTop, Int(e, "wall_top_alt_one_in", entry, 3, 1, 1000),
					wallFace, Int(e, "wall_face_alt_one_in", entry, 5, 1, 1000), water, Int(e, "water_frame_ms", entry, 220, 1, 60000), ColorOf(e, "edge", entry, edge),
					ColorOf(e, "bank", entry, bank), objects, animated, palettes));
			}
		}

		private void Character(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, Schema["characters[]"]);
			string? id = Str(e, "id", entry);
			if (id is not null && !data.Characters.Contains(id) && !data.Actors.Contains(id))
			{
				Problem(entry, $"unknown character '{id}': not a party member or actor in the game data");
			}
			GridImage? image = Grid(e, entry, Pixels.Size, scaled: true);
			Dictionary<Direction, int[]> walk = e.TryGetProperty("walk", out JsonElement w) ? Facing(w, entry + ".walk", image, "walk") : [];
			SheetFacing? facing = null;
			if (e.TryGetProperty("facing", out JsonElement fa) && IsObject(fa, entry + ".facing"))
			{
				string faEntry = entry + ".facing";
				Fields(fa, faEntry, Schema["characters[].facing"]);
				Direction? Named(string? name, string field)
				{
					int index = name is null ? -1 : Array.IndexOf(Facings, name);
					if (name is not null && index < 0)
					{
						Problem(faEntry, $"unknown facing '{name}' in '{field}': {string.Join(", ", Facings)}");
					}
					return index < 0 ? null : (Direction)index;
				}
				var mirror = new Dictionary<Direction, Direction>();
				if (fa.TryGetProperty("mirror", out JsonElement mi) && IsObject(mi, faEntry + ".mirror"))
				{
					foreach (JsonProperty mp in mi.EnumerateObject())
					{
						if (Named(mp.Name, "mirror") is not { } to || Named(mp.Value.ValueKind == JsonValueKind.String ? mp.Value.GetString() : mp.Value.ToString(), "mirror") is not { } from)
						{
							continue;
						}
						if (walk.ContainsKey(to))
						{
							Problem(faEntry, $"'mirror' draws {mp.Name}, which 'walk' gives already");
						}
						else if (!walk.ContainsKey(from))
						{
							Problem(faEntry, $"'mirror' draws {mp.Name} from {Facings[(int)from]}, which 'walk' does not give");
						}
						else
						{
							mirror[to] = from;
						}
					}
				}
				Direction? fallback = Named(Str(fa, "fallback", faEntry, required: false), "fallback");
				if (fallback is { } back && !walk.ContainsKey(back))
				{
					Problem(faEntry, $"'fallback' {Facings[(int)back]} is not a facing 'walk' gives");
				}
				Direction? start = Named(Str(fa, "start", faEntry, required: false), "start");
				if (start is not null && id is not null && !data.Actors.Contains(id))
				{
					Problem(faEntry, $"'start' is for script actors: {id} is a party member, who faces where the party goes");
				}
				facing = new SheetFacing(mirror, fallback, start);
			}
			bool lacking = Enum.GetValues<Direction>().Any(d => d != Direction.None && !walk.ContainsKey(d) && facing?.Mirror.ContainsKey(d) != true);
			if (walk.Count == 0 || walk.Values.Any(v => v.Length != 3) || lacking && facing?.Fallback is null)
			{
				Problem(entry, facing is null ? "'walk' needs north, east, south and west, each three frames: standing, walk A, walk B"
					: "'walk' needs three frames (standing, walk A, walk B) for each facing it gives, and every facing it lacks needs 'facing.mirror' or 'facing.fallback'");
			}
			var poses = new Dictionary<string, PoseEntry>();
			if (e.TryGetProperty("poses", out JsonElement p) && IsObject(p, entry + ".poses"))
			{
				foreach (JsonProperty pose in p.EnumerateObject())
				{
					string poseEntry = $"{entry}.poses.{pose.Name}";
					if (!IsObject(pose.Value, poseEntry))
					{
						continue;
					}
					Fields(pose.Value, poseEntry, Schema["characters[].poses{}"]);
					Dictionary<Direction, int[]> frames = pose.Value.TryGetProperty("frames", out JsonElement f) ? Facing(f, poseEntry + ".frames", image, "frames") : [];
					int[] ms = Ints(pose.Value, "ms", poseEntry, required: true);
					if (frames.Count == 0 || frames.Values.Any(v => v.Length != ms.Length) || ms.Length == 0 || ms.Any(v => v == 0))
					{
						Problem(poseEntry, "needs 'frames' for at least one facing and 'ms', a duration from 1 ms for each frame, as many as every facing's frames");
						continue;
					}
					bool motion = !pose.Value.TryGetProperty("motion", out JsonElement mo) || mo.ValueKind != JsonValueKind.False;
					if (pose.Value.TryGetProperty("motion", out mo) && mo.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
					{
						Problem(poseEntry, "field 'motion' must be true or false");
					}
					poses[pose.Name] = new PoseEntry(frames, ms, motion);
				}
			}
			var animations = new Dictionary<string, string>();
			if (e.TryGetProperty("animations", out JsonElement a) && IsObject(a, entry + ".animations"))
			{
				foreach (JsonProperty anim in a.EnumerateObject())
				{
					string animEntry = $"{entry}.animations.{anim.Name}";
					if (!data.Animations.Contains(anim.Name))
					{
						Problem(animEntry, $"unknown animation '{anim.Name}': not in game.json's presentation animations");
					}
					if (anim.Value.ValueKind != JsonValueKind.String || !poses.ContainsKey(anim.Value.GetString()!))
					{
						Problem(animEntry, $"unknown pose '{(anim.Value.ValueKind == JsonValueKind.String ? anim.Value.GetString() : anim.Value.ToString())}': not one of this character's poses");
						continue;
					}
					animations[anim.Name] = anim.Value.GetString()!;
				}
			}
			foreach (string unused in poses.Keys.Where(k => !animations.ContainsValue(k) && !data.Animations.Contains(k)))
			{
				Problem($"{entry}.poses.{unused}", "no animation plays this pose: name it after one of game.json's presentation animations, or map one to it in 'animations'");
			}
			if (id is not null && image is not null && m.Problems.Count == before)
			{
				m.Characters.Add(new CharacterEntry(id, image, walk, poses, animations, e.TryGetProperty("shadow", out _) ? Bool(e, "shadow", entry, true) : null, facing));
			}
		}

		private void Figure(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, Schema["figures[]"]);
			string? id = Str(e, "id", entry);
			string? side = Str(e, "side", entry);
			string view = Str(e, "view", entry, required: false) ?? "side";
			if (side is not null && side is not ("party" or "enemy"))
			{
				Problem(entry, $"unknown side '{side}': party or enemy");
			}
			if (view is not ("side" or "back"))
			{
				Problem(entry, $"unknown view '{view}': side or back");
			}
			else if (view == "back" && side == "enemy")
			{
				Problem(entry, "view 'back' is for party members and vehicles only");
			}
			if (id is not null && side == "party" && !data.Characters.Contains(id) && !data.Vehicles.Contains(id))
			{
				Problem(entry, $"unknown party member '{id}': not a character or vehicle in the game data");
			}
			if (side == "enemy")
			{
				Known(id, data.Enemies, entry, "enemy");
			}
			GridImage? image = Grid(e, entry, Pixels.Size, scaled: true);
			FrameRun? idle = e.TryGetProperty("idle", out JsonElement i) ? Run(i, entry + ".idle", image, 400) : null;
			if (idle is null && !e.TryGetProperty("idle", out _))
			{
				Problem(entry, "missing field 'idle'");
			}
			var animations = new Dictionary<string, FrameRun>();
			if (e.TryGetProperty("animations", out JsonElement a) && IsObject(a, entry + ".animations"))
			{
				foreach (JsonProperty anim in a.EnumerateObject())
				{
					string animEntry = $"{entry}.animations.{anim.Name}";
					if (!IsPoseName(anim.Name))
					{
						Problem(animEntry, $"a pose's name is lower-case letters, digits and '_', starting with a letter, and not 'idle': '{anim.Name}'");
					}
					else if (Run(anim.Value, animEntry, image, 120) is { } run)
					{
						animations[anim.Name] = run;
					}
				}
			}
			bool? natural = e.TryGetProperty("natural", out _) ? Bool(e, "natural", entry, false) : null;
			if (natural == true && image?.Scale is not null)
			{
				Problem(entry, "'natural': true and 'scale' do not go together: the scale sets the size");
			}
			(IReadOnlyList<Color>? palette, string? paletteFile) = Palette(e, entry);
			if (id is not null && image is not null && idle is not null && side is "party" or "enemy" && m.Problems.Count == before)
			{
				var figure = new FigureEntry(id, side == "enemy", image, idle, animations, view == "back", e.TryGetProperty("shadow", out _) ? Bool(e, "shadow", entry, true) : null,
					natural, palette, entry, paletteFile);
				m.Figures.Add(figure);
				if (natural == true)
				{
					_natural.Add((entry, figure));
				}
			}
		}

		private NineSlice? Slice(JsonElement e, string entry, bool camera)
		{
			if (!IsObject(e, entry))
			{
				return null;
			}
			Fields(e, entry, camera ? Schema["nine_slice_camera"] : Schema["nine_slice"]);
			string? cam = camera ? Str(e, "camera", entry, required: false) : null;
			Known(cam, data.Cameras, entry, "camera");
			string? file = Str(e, "image", entry);
			int border = Int(e, "border", entry, 4, 1, 256);
			int scale = Int(e, "scale", entry, 1, 1, 16);
			if (file is null || Header(file, entry) is not { } header)
			{
				return null;
			}
			if (header.Width <= 2 * border || header.Height <= 2 * border)
			{
				Problem(entry, $"{header.Width}x{header.Height} is too small for a border of {border}", file);
				return null;
			}
			return new NineSlice(cam, file, border, scale);
		}

		private void Ui(JsonElement e)
		{
			if (!IsObject(e, "ui"))
			{
				return;
			}
			Fields(e, "ui", Schema["ui"]);
			bool enabled = !e.TryGetProperty("enabled", out JsonElement en) || en.ValueKind != JsonValueKind.False;
			if (e.TryGetProperty("enabled", out en) && en.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
			{
				Problem("ui", "field 'enabled' must be true or false");
			}
			NineSlice? frame = e.TryGetProperty("frame", out JsonElement f) ? Slice(f, "ui.frame", camera: false) : null;
			NineSlice? battleFrame = e.TryGetProperty("battle_frame", out JsonElement bf) ? Slice(bf, "ui.battle_frame", camera: false) : null;
			var panels = new List<NineSlice>();
			foreach ((JsonElement p, string entry) in List(e, "panel_frames"))
			{
				if (Slice(p, "ui." + entry, camera: true) is { } slice)
				{
					panels.Add(slice);
				}
			}
			var fonts = new Dictionary<string, FontEntry>();
			if (e.TryGetProperty("fonts", out JsonElement fs) && IsObject(fs, "ui.fonts"))
			{
				foreach (JsonProperty font in fs.EnumerateObject())
				{
					string entry = $"ui.fonts.{font.Name}";
					Known(font.Name, data.Languages, entry, "language");
					if (!IsObject(font.Value, entry))
					{
						continue;
					}
					Fields(font.Value, entry, Schema["ui.fonts{}"]);
					string? file = Str(font.Value, "file", entry);
					if (file is not null && !File.Exists(m.Full(file)))
					{
						Problem(entry, "missing file", file);
						continue;
					}
					if (file is not null)
					{
						fonts[font.Name] = new FontEntry(file, Float(font.Value, "scale", entry, 1f, 0.5f, 3f));
					}
				}
			}
			m.Ui = new UiEntry(enabled, frame, panels, Picture(e, "cursor", "ui", required: false), Picture(e, "bar_track", "ui", required: false),
				Picture(e, "bar_fill", "ui", required: false), fonts, battleFrame, Picture(e, "page_arrow", "ui", required: false));
		}

		private void Audio(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, Schema["audio[]"]);
			string? kindName = Str(e, "kind", entry);
			string? id = Str(e, "id", entry);
			string? file = Str(e, "file", entry);
			SoundKind? kind = kindName switch { "music" => SoundKind.Music, "ambient" => SoundKind.Ambient, "cue" => SoundKind.Cue, _ => null };
			if (kindName is not null && kind is null)
			{
				Problem(entry, $"unknown kind '{kindName}': music, ambient or cue");
			}
			HashSet<string>? ids = kind switch { SoundKind.Music => data.Music, SoundKind.Ambient => data.Ambient, SoundKind.Cue => data.Cues, _ => null };
			IReadOnlySet<string>? tagged = kind switch { SoundKind.Music => played?.Music, SoundKind.Cue => played?.Cues, _ => null };
			if (id is not null && ids is not null && !ids.Contains(id) && tagged?.Contains(id) != true)
			{
				Problem(entry, $"unknown {kindName} '{id}': not an id the game or the sample plays as {kindName}");
			}
			float gain = Float(e, "gain", entry, 1f, 0f, 4f);
			int loopStart = Int(e, "loop_start", entry, 0, 0, int.MaxValue);
			int loopEnd = Int(e, "loop_end", entry, -1, 1, int.MaxValue);
			if (file is null || kind is null || id is null)
			{
				return;
			}
			if (!file.EndsWith(".wav", StringComparison.OrdinalIgnoreCase) && !file.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase))
			{
				Problem(entry, "not a .wav or .ogg file", file);
				return;
			}
			if (!File.Exists(m.Full(file)))
			{
				Problem(entry, "missing file", file);
				return;
			}
			if (SoundFile.ReadFrames(m.Full(file), out string problem) is not { } frames)
			{
				Problem(entry, problem, file);
				return;
			}
			if (kind == SoundKind.Cue && (e.TryGetProperty("loop_start", out _) || e.TryGetProperty("loop_end", out _)))
			{
				Problem(entry, "a cue plays once and takes no loop points");
			}
			if (loopEnd < 0)
			{
				loopEnd = (int)Math.Min(frames, int.MaxValue);
			}
			if (loopEnd > frames || loopStart >= loopEnd)
			{
				Problem(entry, $"bad loop points {loopStart} to {loopEnd}: the file has {frames} frames, and the start must come before the end", file);
				return;
			}
			if (m.Problems.Count == before)
			{
				m.Audio.Add(new AudioEntry(kind.Value, id, file, loopStart, loopEnd, gain));
			}
		}
	}
}
