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
public sealed record GridImage(string Path, int FrameWidth, int FrameHeight)
{
	public int Columns { get; init; } = 1;
	public int Rows { get; init; } = 1;
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

public sealed record TilesetEntry(string? Kind, string? Area, Theme Theme, GridImage Image, int[] Floor, int[] Decor, int DecorOneIn, int[] WallTop, int WallTopAltOneIn,
	int[] WallFace, int WallFaceAltOneIn, int[] Water, int WaterFrameMs, Color Edge, Color Bank, Dictionary<string, int> Objects);

// Motion: whether the animation's generated motion (a hop, a startled mark, strokes beside the head) plays over the frames.
public sealed record PoseEntry(Dictionary<Direction, int[]> Frames, int[] Ms, bool Motion);

public sealed record CharacterEntry(string Id, GridImage Image, Dictionary<Direction, int[]> Walk, Dictionary<string, PoseEntry> Poses, Dictionary<string, string> Animations);

public sealed record FigureEntry(string Id, bool Enemy, GridImage Image, FrameRun Idle, Dictionary<string, FrameRun> Animations);

public sealed record FieldEnemyEntry(string Encounter, GridImage Image, FrameRun Run);

// A vehicle in the field: per facing a frame run of `Ms` milliseconds a frame, looping (a bob).
public sealed record VehicleEntry(string Id, GridImage Image, Dictionary<Direction, int[]> Facings, int Ms);

public sealed record BackdropEntry(string? Kind, string? Area, string? Encounter, string Path);

public sealed record EffectEntry(string Id, GridImage Image, FrameRun Run);

public sealed record NineSlice(string? Camera, string Path, int Border, int Scale);

public sealed record FontEntry(string File, float Scale);

public sealed record UiEntry(bool Enabled, NineSlice? Frame, List<NineSlice> PanelFrames, string? Cursor, string? BarTrack, string? BarFill, Dictionary<string, FontEntry> Fonts);

public sealed record AudioEntry(SoundKind Kind, string Id, string Path, int LoopStart, int LoopEnd, float Gain);

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
	public static readonly string[] SpriteNames = ["shadow", "bubble", "alert", "pointer", "pier"];
	public static readonly string[] FigureAnimations = ["attack", "cast", "item", "defend", "hurt", "defeat"];

	public string Directory { get; }
	public List<TilesetEntry> Tilesets { get; } = [];
	public List<CharacterEntry> Characters { get; } = [];
	public Dictionary<string, string> Portraits { get; } = [];
	public List<FigureEntry> Figures { get; } = [];
	public List<FieldEnemyEntry> FieldEnemies { get; } = [];
	public List<VehicleEntry> Vehicles { get; } = [];
	public List<BackdropEntry> Backdrops { get; } = [];
	public List<EffectEntry> Effects { get; } = [];
	public Dictionary<string, string> Sprites { get; } = [];
	public Dictionary<string, string> StatusIcons { get; } = [];
	public UiEntry? Ui { get; private set; }
	public List<AudioEntry> Audio { get; } = [];
	public string? BattlePresentation { get; private set; }
	public List<AssetProblem> Problems { get; } = [];

	private AssetManifest(string directory) => Directory = directory;

	public string Full(string relative) => Path.GetFullPath(Path.Combine(Directory, relative));

	// Every file the manifest names, for change detection on reload.
	public IEnumerable<string> Files() =>
		Tilesets.Select(t => t.Image.Path).Concat(Characters.Select(c => c.Image.Path)).Concat(Portraits.Values).Concat(Figures.Select(f => f.Image.Path))
			.Concat(FieldEnemies.Select(f => f.Image.Path)).Concat(Vehicles.Select(v => v.Image.Path)).Concat(Backdrops.Select(b => b.Path)).Concat(Effects.Select(e => e.Image.Path)).Concat(Sprites.Values)
			.Concat(StatusIcons.Values).Concat(Audio.Select(a => a.Path))
			.Concat(Ui is null ? [] : new[] { Ui.Frame?.Path, Ui.Cursor, Ui.BarTrack, Ui.BarFill }.OfType<string>().Concat(Ui.PanelFrames.Select(p => p.Path)).Concat(Ui.Fonts.Values.Select(f => f.File)))
			.Concat(BattlePresentation is null ? [] : [BattlePresentation])
			.Distinct();

	// Reads DIR/assets.json against the session's data. A directory without a manifest is a problem too, since the
	// caller named it.
	public static AssetManifest Load(string directory, GameData data)
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
			new Reader(m, data).Root(doc.RootElement);
		}
		catch (JsonException e)
		{
			m.Problems.Add(new AssetProblem(FileName, "-", "not valid JSON: " + e.Message));
		}
		return m;
	}

	// Walks the JSON, reporting unknown fields, wrong types, unknown ids and files that do not fit their entries.
	private sealed class Reader(AssetManifest m, GameData data)
	{
		private void Problem(string entry, string reason, string? file = null) => m.Problems.Add(new AssetProblem(file ?? FileName, entry, reason));

		private void Fields(JsonElement e, string entry, params string[] known)
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

		// A PNG that must exist and divide into frames of the given size.
		private GridImage? Grid(JsonElement e, string entry, int defaultFrame)
		{
			string? file = Str(e, "image", entry);
			int[] frame = e.TryGetProperty("frame", out _) ? Ints(e, "frame", entry) : [defaultFrame, defaultFrame];
			if (e.TryGetProperty("tile", out _))
			{
				int tile = Int(e, "tile", entry, defaultFrame, 1, 1024);
				frame = [tile, tile];
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
			return new GridImage(file, frame[0], frame[1]) { Columns = header.Width / frame[0], Rows = header.Height / frame[1] };
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
				Fields(e, entry, "frames", "ms");
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
			Fields(e, entry, Facings);
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

		// The scope fields of a tileset or backdrop: exactly one of the area kind, the area id or (backdrops) the encounter.
		private (string? Kind, string? Area, string? Encounter) Scope(JsonElement e, string entry, bool encounter)
		{
			string? kind = Str(e, "kind", entry, required: false);
			string? area = Str(e, "area", entry, required: false);
			string? enc = encounter ? Str(e, "encounter", entry, required: false) : null;
			if (new[] { kind, area, enc }.Count(v => v is not null) != 1)
			{
				Problem(entry, encounter ? "needs exactly one of 'kind', 'area' and 'encounter'" : "needs exactly one of 'kind' and 'area'");
			}
			if (kind is not null && kind is not ("town" or "field" or "dungeon"))
			{
				Problem(entry, $"unknown area kind '{kind}': town, field or dungeon");
			}
			Known(area, data.Areas, entry, "area");
			Known(enc, data.Encounters, entry, "encounter");
			return (kind, area, enc);
		}

		public void Root(JsonElement root)
		{
			if (!IsObject(root, "-"))
			{
				return;
			}
			Fields(root, "-", "version", "tilesets", "characters", "portraits", "figures", "field_enemies", "vehicles", "backdrops", "effects", "sprites", "status_icons", "ui",
				"audio", "battle_presentation");
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
			Named(root, "portraits", (id, entry) =>
			{
				if (!data.Portraits.Contains(id) && !data.Characters.Contains(id) && !data.Actors.Contains(id))
				{
					Problem(entry, $"unknown portrait '{id}': not a portrait, character or actor in the game data");
				}
			}, m.Portraits);
			foreach ((JsonElement e, string entry) in List(root, "figures"))
			{
				Figure(e, entry);
			}
			foreach ((JsonElement e, string entry) in List(root, "field_enemies"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, "encounter", "image", "frame", "frames", "ms");
				string? encounter = Str(e, "encounter", entry);
				Known(encounter, data.Encounters, entry, "encounter");
				GridImage? image = Grid(e, entry, Pixels.Size);
				if (Run(e, entry, image, 280, inline: true) is { } run && encounter is not null && image is not null && m.Problems.Count == before)
				{
					m.FieldEnemies.Add(new FieldEnemyEntry(encounter, image, run));
				}
			}
			foreach ((JsonElement e, string entry) in List(root, "vehicles"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, "id", "image", "frame", "facings", "ms");
				string? id = Str(e, "id", entry);
				Known(id, data.FieldVehicles, entry, "vehicle");
				GridImage? image = Grid(e, entry, Pixels.Size);
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
				Fields(e, entry, "kind", "area", "encounter", "image");
				(string? kind, string? area, string? encounter) = Scope(e, entry, encounter: true);
				if (Picture(e, "image", entry) is { } file && m.Problems.Count == before)
				{
					m.Backdrops.Add(new BackdropEntry(kind, area, encounter, file));
				}
			}
			foreach ((JsonElement e, string entry) in List(root, "effects"))
			{
				int before = m.Problems.Count;
				Fields(e, entry, "id", "image", "frame", "frames", "ms");
				string? id = Str(e, "id", entry);
				GridImage? image = Grid(e, entry, Art.EffectSize);
				if (Run(e, entry, image, 60, inline: true) is { } run && id is not null && image is not null && m.Problems.Count == before)
				{
					m.Effects.Add(new EffectEntry(id, image, run));
				}
			}
			Named(root, "sprites", (id, entry) =>
			{
				if (!SpriteNames.Contains(id))
				{
					Problem(entry, $"unknown sprite '{id}': {string.Join(", ", SpriteNames)}");
				}
			}, m.Sprites);
			Named(root, "status_icons", (id, entry) => Known(id, data.Statuses, entry, "status"), m.StatusIcons);
			if (root.TryGetProperty("ui", out JsonElement ui))
			{
				Ui(ui);
			}
			foreach ((JsonElement e, string entry) in List(root, "audio"))
			{
				Audio(e, entry);
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
			Once(m.Figures, f => (f.Enemy ? "enemy " : "party member ") + f.Id, "figures");
			Once(m.FieldEnemies, f => f.Encounter, "field_enemies");
			Once(m.Vehicles, v => v.Id, "vehicles");
			Once(m.Backdrops, b => b.Kind is not null ? "kind " + b.Kind : b.Area is not null ? "area " + b.Area : "encounter " + b.Encounter, "backdrops");
			Once(m.Effects, e => e.Id, "effects");
			Once(m.Audio, a => $"{a.Kind.ToString().ToLowerInvariant()} {a.Id}", "audio");
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

		private void Tileset(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, "kind", "area", "image", "tile", "floor", "decor", "decor_one_in", "wall_top", "wall_top_alt_one_in", "wall_face", "wall_face_alt_one_in",
				"water", "water_frame_ms", "edge", "bank", "objects");
			(string? kind, string? area, _) = Scope(e, entry, encounter: false);
			GridImage? image = Grid(e, entry, Pixels.Size);
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
				Fields(o, entry + ".objects", ObjectNames);
				foreach (string name in ObjectNames.Where(n => o.TryGetProperty(n, out _)))
				{
					int cell = Int(o, name, entry + ".objects", -1, 0, int.MaxValue);
					if (cell >= 0)
					{
						objects[name] = Cells([cell], image, entry, $"object '{name}'")[0];
					}
				}
			}
			Theme theme = kind switch { "town" => Theme.Town, "dungeon" => Theme.Dungeon, "field" => Theme.Field, _ => Art.ThemeOf(area is null ? AreaKind.Field : data.AreaKinds.GetValueOrDefault(area)) };
			(Color edge, Color bank) = Art.DefaultEdges(theme);
			if (image is not null && (kind ?? area) is not null && m.Problems.Count == before)
			{
				m.Tilesets.Add(new TilesetEntry(kind, area, theme, image, floor, decor, Int(e, "decor_one_in", entry, 11, 1, 1000), wallTop, Int(e, "wall_top_alt_one_in", entry, 3, 1, 1000),
					wallFace, Int(e, "wall_face_alt_one_in", entry, 5, 1, 1000), water, Int(e, "water_frame_ms", entry, 220, 1, 60000), ColorOf(e, "edge", entry, edge),
					ColorOf(e, "bank", entry, bank), objects));
			}
		}

		private void Character(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, "id", "image", "frame", "walk", "poses", "animations");
			string? id = Str(e, "id", entry);
			if (id is not null && !data.Characters.Contains(id) && !data.Actors.Contains(id))
			{
				Problem(entry, $"unknown character '{id}': not a party member or actor in the game data");
			}
			GridImage? image = Grid(e, entry, Pixels.Size);
			Dictionary<Direction, int[]> walk = e.TryGetProperty("walk", out JsonElement w) ? Facing(w, entry + ".walk", image, "walk") : [];
			if (walk.Count != 4 || walk.Values.Any(v => v.Length != 3))
			{
				Problem(entry, "'walk' needs north, east, south and west, each three frames: standing, walk A, walk B");
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
					Fields(pose.Value, poseEntry, "frames", "ms", "motion");
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
			foreach (string unused in poses.Keys.Where(k => !animations.ContainsValue(k)))
			{
				Problem($"{entry}.poses.{unused}", "no animation plays this pose: map script animation names to it in 'animations'");
			}
			if (id is not null && image is not null && m.Problems.Count == before)
			{
				m.Characters.Add(new CharacterEntry(id, image, walk, poses, animations));
			}
		}

		private void Figure(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, "id", "side", "image", "frame", "idle", "animations");
			string? id = Str(e, "id", entry);
			string? side = Str(e, "side", entry);
			if (side is not null && side is not ("party" or "enemy"))
			{
				Problem(entry, $"unknown side '{side}': party or enemy");
			}
			if (id is not null && side == "party" && !data.Characters.Contains(id) && !data.Vehicles.Contains(id))
			{
				Problem(entry, $"unknown party member '{id}': not a character or vehicle in the game data");
			}
			if (side == "enemy")
			{
				Known(id, data.Enemies, entry, "enemy");
			}
			GridImage? image = Grid(e, entry, Pixels.Size);
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
					if (!FigureAnimations.Contains(anim.Name))
					{
						Problem(animEntry, $"unknown animation '{anim.Name}': {string.Join(", ", FigureAnimations)}");
					}
					else if (Run(anim.Value, animEntry, image, 120) is { } run)
					{
						animations[anim.Name] = run;
					}
				}
			}
			if (id is not null && image is not null && idle is not null && side is "party" or "enemy" && m.Problems.Count == before)
			{
				m.Figures.Add(new FigureEntry(id, side == "enemy", image, idle, animations));
			}
		}

		private NineSlice? Slice(JsonElement e, string entry, bool camera)
		{
			if (!IsObject(e, entry))
			{
				return null;
			}
			Fields(e, entry, camera ? ["camera", "image", "border", "scale"] : ["image", "border", "scale"]);
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
			Fields(e, "ui", "enabled", "frame", "panel_frames", "cursor", "bar_track", "bar_fill", "fonts");
			bool enabled = !e.TryGetProperty("enabled", out JsonElement en) || en.ValueKind != JsonValueKind.False;
			if (e.TryGetProperty("enabled", out en) && en.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
			{
				Problem("ui", "field 'enabled' must be true or false");
			}
			NineSlice? frame = e.TryGetProperty("frame", out JsonElement f) ? Slice(f, "ui.frame", camera: false) : null;
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
					Fields(font.Value, entry, "file", "scale");
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
				Picture(e, "bar_fill", "ui", required: false), fonts);
		}

		private void Audio(JsonElement e, string entry)
		{
			int before = m.Problems.Count;
			Fields(e, entry, "kind", "id", "file", "loop_start", "loop_end", "gain");
			string? kindName = Str(e, "kind", entry);
			string? id = Str(e, "id", entry);
			string? file = Str(e, "file", entry);
			SoundKind? kind = kindName switch { "music" => SoundKind.Music, "ambient" => SoundKind.Ambient, "cue" => SoundKind.Cue, _ => null };
			if (kindName is not null && kind is null)
			{
				Problem(entry, $"unknown kind '{kindName}': music, ambient or cue");
			}
			HashSet<string>? ids = kind switch { SoundKind.Music => data.Music, SoundKind.Ambient => data.Ambient, SoundKind.Cue => data.Cues, _ => null };
			if (id is not null && ids is not null && !ids.Contains(id))
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
