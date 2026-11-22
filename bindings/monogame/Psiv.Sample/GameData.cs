using System.Text.Json;
using System.Text.RegularExpressions;

using Psiv.Session;

namespace Psiv.Sample;

// An ability's or item's shape, for presentations that fall back on kind and element: its kind ("attack",
// "technique", "skill", "combo" or "item"), the kind of its first effect ("damage", "heal", "revive", "status",
// "cure", "stat" or "other") and its element ("none" for physical and non-damage effects).
public sealed record ActionShape(string Kind, string Effect, string Element);

// A step hook that fires on every cell of one map character (the C interface lists it as no point): its cells as the
// map writes them, and the flags that gate it.
public sealed record TileHook(string Id, char Tile, (int X, int Y)[] Cells, string RequireFlag, string BlockFlag);

// An area's map as its file writes it, before any change: each cell's terrain (CellFlags' terrain and barrier bits),
// its changes by flag in data order, the ids of its points (hooks, chests, shops, rests, ports, helms) and of its chests,
// its tile step hooks, and the interact hooks solid only while their flags hold.
public sealed record AreaMap(int Width, int Height, CellFlags[] Written, List<(string Flag, (int X, int Y)[] Cells)> Changes, HashSet<string> Points,
	HashSet<string> Chests, List<TileHook> TileHooks, HashSet<string> SolidWhileActive)
{
	public const CellFlags TerrainBits = CellFlags.TerrainMask | CellFlags.Barrier;

	public static CellFlags Terrain(char tile) => tile switch
	{
		// A counter (R109) is never walked: it draws as a wall.
		'#' or '=' => CellFlags.Wall,
		'~' => CellFlags.Water,
		':' => CellFlags.Soft,
		'%' => CellFlags.Wall | CellFlags.Barrier,
		_ => CellFlags.Floor,
	};
}

// The ids the session's data defines, for checking an asset manifest and for battle presentations. The session lists
// actors, cameras and languages; the C interface lists no areas, characters, enemies, encounters, statuses, abilities,
// items, audio ids or script animations, so those are read from the files the session loaded (game.json, the area
// files, the event scripts and core's battle data).
public sealed partial class GameData
{
	public HashSet<string> Areas { get; } = [];
	public Dictionary<string, AreaKind> AreaKinds { get; } = [];
	public Dictionary<string, AreaMap> Maps { get; } = [];
	public HashSet<string> Flags { get; } = [];
	public HashSet<string> Actors { get; } = [];
	public HashSet<string> Characters { get; } = [];
	public HashSet<string> Vehicles { get; } = [];
	// The vehicles game.json gives field rules, which the field draws.
	public HashSet<string> FieldVehicles { get; } = [];
	// What each of those crosses (`crosses`, and `barrier` when it `breaks` barriers), and each area's conveyor cells
	// with their time per cell: where the real-time field input expects the leader's next step to go, and how fast.
	public Dictionary<string, HashSet<string>> VehicleCrosses { get; } = [];
	public Dictionary<string, List<Belt>> Belts { get; } = [];
	public HashSet<string> Enemies { get; } = [];
	public HashSet<string> Encounters { get; } = [];
	public HashSet<string> Statuses { get; } = [];
	// The notice keys battles can show with this data (GameSession.BattleKeys), for the animation set's `step:notice.KEY`.
	public HashSet<string> BattleNotices { get; } = [];
	public HashSet<string> Cameras { get; } = [];
	public HashSet<string> Portraits { get; } = [];
	public HashSet<string> Animations { get; } = [];
	// game.json's `interact` (R77): true under "nearby", when the prompt and interaction follow the session's target query.
	public bool InteractNearby { get; private set; }
	// game.json's `run_speed_percent` (R81), 0 when it gives none: the real-time walk then runs through the session.
	public int RunSpeedPercent { get; private set; }
	// Each wandering or patrolling actor's speed against the area's step time (game.json actors' `wander.speed_percent` or
	// `patrol.speed_percent`, default 50).
	public Dictionary<string, int> WanderPercent { get; } = [];
	// Each talk point's `actor_at` cell by actor: where the actor stands apart from its hook's cell.
	public Dictionary<string, (int X, int Y)> ActorAt { get; } = [];
	// The first encounter listing each enemy, whose field figure an actor in that enemy's figure wears.
	public Dictionary<string, string> EnemyEncounters { get; } = [];
	// Each vehicle's skills (core's party.json `abilities`), which its macro steps may name.
	public Dictionary<string, List<string>> VehicleSkills { get; } = [];
	public HashSet<string> Languages { get; } = [];
	// Every audio id the game can ask for by kind: the data's tracks, loops and sounds, the tracks the sample derives
	// where the data names none, and the sample's own cues.
	public HashSet<string> Music { get; } = [];
	public HashSet<string> Ambient { get; } = [];
	public HashSet<string> Cues { get; } = [];
	public Dictionary<string, ActionShape> Actions { get; } = [];
	// Each ability's targeting, each equipment piece's battle use (an ability), and the statuses each enemy starts with,
	// which a summoned or changed enemy shows from its first step.
	public Dictionary<string, Targeting> AbilityTargeting { get; } = [];
	public Dictionary<string, string> PieceUses { get; } = [];
	// Weapons by id: their category, and whether they take both hands.
	public Dictionary<string, string> EquipmentCategories { get; } = [];
	// Every equipment piece of any slot, by id: its category.
	public Dictionary<string, string> AllEquipment { get; } = [];
	public HashSet<string> TwoHanded { get; } = [];
	// Combination recipes by id: the ability they make and the abilities that make it.
	public Dictionary<string, (string Result, List<string> Abilities)> Recipes { get; } = [];
	public Dictionary<string, List<string>> EnemyStatuses { get; } = [];
	// Where each event a hook starts begins: the hook's area and cell, for the event previewer.
	public Dictionary<string, (string Area, int X, int Y)> EventStarts { get; } = [];
	// The shops' and rest points' ids, which the assets' keepers give a portrait.
	public HashSet<string> Keepers { get; } = [];
	// Per language the text tables' lines: each key and text, and whether it came from the dialogue's string table.
	public Dictionary<string, List<(string Key, string Text, bool Dialogue)>> TextTables { get; } = [];

	[GeneratedRegex(@"^\s*(music play|ambient play|sound)\s+([A-Za-z0-9_]+)", RegexOptions.Multiline)]
	private static partial Regex ScriptAudio();

	public static GameData Load(GameSession session, string dataDir)
	{
		var d = new GameData();
		d.Actors.UnionWith(session.ActorIds());
		d.Cameras.UnionWith(session.Cameras().Select(c => c.Id));
		d.BattleNotices.UnionWith(session.BattleKeys().Notices);
		d.Languages.UnionWith(session.GameInfo().Languages);
		d.Cues.UnionWith(Synth.SampleCues);
		using JsonDocument game = JsonDocument.Parse(File.ReadAllText(Path.Combine(dataDir, "game.json")));
		JsonElement root = game.RootElement;
		d.Each(Array(root, "areas"), "game.json areas", area => d.Guard(Text(area) ?? "an area", () => d.LoadArea(dataDir, Text(area)!)));
		d.Each(Array(root, "languages"), "game.json languages", language =>
		{
			var lines = new List<(string, string, bool)>();
			if (language.TryGetProperty("text", out JsonElement textFile))
			{
				using JsonDocument text = JsonDocument.Parse(File.ReadAllText(Path.Combine(dataDir, textFile.GetString()!)));
				lines.AddRange(text.RootElement.GetProperty("text").EnumerateObject().Select(t => (t.Name, t.Value.ValueKind == JsonValueKind.String ? t.Value.GetString()! : "", false)));
			}
			if (language.TryGetProperty("strings", out JsonElement stringsFile))
			{
				foreach (string line in File.ReadLines(Path.Combine(dataDir, stringsFile.GetString()!)))
				{
					int eq = line.IndexOf('=');
					if (!line.TrimStart().StartsWith('#') && eq > 0)
					{
						lines.Add((line[..eq].Trim(), line[(eq + 1)..].Trim(), true));
					}
				}
			}
			d.TextTables[language.GetProperty("code").GetString()!] = lines;
		});
		d.Flags.UnionWith(Array(root, "flags").Select(IdOf).OfType<string>());
		d.InteractNearby = root.TryGetProperty("interact", out JsonElement interact) && Text(interact) == "nearby";
		d.RunSpeedPercent = root.TryGetProperty("run_speed_percent", out JsonElement run) ? Whole(run) ?? 0 : 0;
		// An actor is a name or an object; `wander` is an object with an optional `speed_percent`, or any other value for the
		// default pace.
		d.Each(Array(root, "actors").Where(a => a.ValueKind == JsonValueKind.Object && (a.TryGetProperty("wander", out _) || a.TryGetProperty("patrol", out _))), "game.json actors", actor =>
		{
			JsonElement wander = actor.TryGetProperty("wander", out JsonElement w) ? w : actor.GetProperty("patrol");
			if (wander.ValueKind is JsonValueKind.False or JsonValueKind.Null)
			{
				return;
			}
			int? speed = wander.ValueKind == JsonValueKind.Object && wander.TryGetProperty("speed_percent", out JsonElement s) ? Whole(s) : null;
			d.WanderPercent[actor.GetProperty("id").GetString()!] = Math.Max(1, speed ?? 50);
		});
		d.Music.UnionWith(["title", "battle_town", "battle_field", "battle_dungeon", "battle_scripted"]);
		d.Each(Array(root, "vehicles"), "game.json vehicles", v =>
		{
			string id = v.GetProperty("id").GetString()!;
			d.FieldVehicles.Add(id);
			d.VehicleCrosses[id] = [.. Array(v, "crosses").Concat(Array(v, "breaks")).Select(Text).OfType<string>()];
		});
		var scripted = new Dictionary<string, string>();
		d.Each(Array(root, "events"), "game.json events", script =>
		{
			foreach (Match m in ScriptAudio().Matches(File.ReadAllText(Path.Combine(dataDir, script.GetString()!))))
			{
				scripted.TryAdd(m.Groups[2].Value, m.Groups[1].Value);
			}
		});
		if (root.TryGetProperty("presentation", out JsonElement presentation) && presentation.ValueKind == JsonValueKind.Object)
		{
			// An audio entry is an id, an object with its `id`, or a list of either.
			foreach (string audio in Array(presentation, "audio").SelectMany(a => a.ValueKind == JsonValueKind.Array ? a.EnumerateArray() : Enumerable.Repeat(a, 1))
				.Select(IdOf).OfType<string>())
			{
				// An id is a loop or a sound by how the scripts use it; every other id is a track.
				(scripted.GetValueOrDefault(audio) switch { "ambient play" => d.Ambient, "sound" => d.Cues, _ => d.Music }).Add(audio);
			}
			// An entry is a name, or an object with its `id` and `seconds`.
			d.Animations.UnionWith(Array(presentation, "animations").Select(IdOf).OfType<string>());
			d.Portraits.UnionWith(Array(presentation, "portraits").Select(IdOf).OfType<string>());
		}
		string core = Path.GetFullPath(Path.Combine(dataDir, root.TryGetProperty("core_data", out JsonElement coreData) ? Text(coreData) ?? "" : ""));
		d.Guard("party.json", () =>
		{
			using JsonDocument party = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "party.json")));
			d.Characters.UnionWith(Array(party.RootElement, "characters").Select(IdOf).OfType<string>());
			d.Vehicles.UnionWith(Array(party.RootElement, "vehicles").Select(IdOf).OfType<string>());
			d.Each(Array(party.RootElement, "vehicles"), "party.json vehicles", v =>
				d.VehicleSkills[v.GetProperty("id").GetString()!] = [.. Array(v, "abilities").Select(IdOf).OfType<string>()]);
		});
		d.Guard("enemies.json", () =>
		{
			using JsonDocument enemies = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "enemies.json")));
			d.Each(Array(enemies.RootElement, "enemies"), "enemies.json enemies", e =>
			{
				string id = e.GetProperty("id").GetString()!;
				d.Enemies.Add(id);
				d.EnemyStatuses[id] = [.. Array(e, "statuses").Select(IdOf).OfType<string>()];
			});
			d.Each(Array(enemies.RootElement, "encounters"), "enemies.json encounters", encounter =>
			{
				string id = encounter.GetProperty("id").GetString()!;
				d.Encounters.Add(id);
				foreach (JsonElement e in Array(encounter, "enemies"))
				{
					if ((e.ValueKind == JsonValueKind.Object && e.TryGetProperty("enemy", out JsonElement enemy) ? Text(enemy) : Text(e)) is { } name)
					{
						d.EnemyEncounters.TryAdd(name, id);
					}
				}
			});
		});
		d.Guard("statuses.json", () =>
		{
			using JsonDocument statuses = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "statuses.json")));
			d.Statuses.UnionWith(Array(statuses.RootElement, "statuses").Select(IdOf).OfType<string>());
		});
		d.Guard("abilities.json", () =>
		{
			using JsonDocument abilities = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "abilities.json")));
			d.Each(Array(abilities.RootElement, "abilities"), "abilities.json abilities", a =>
			{
				d.Actions[a.GetProperty("id").GetString()!] = Shape(a.GetProperty("kind").GetString()!, a);
				if (a.TryGetProperty("target", out JsonElement targeting) && Text(targeting) is { } named && GameSession.ParseName(NameKind.Targeting, named) is { } t)
				{
					d.AbilityTargeting[a.GetProperty("id").GetString()!] = (Targeting)t;
				}
			});
		});
		d.Guard("items.json", () =>
		{
			using JsonDocument items = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "items.json")));
			d.Each(Array(items.RootElement, "items"), "items.json items", i => d.Actions[i.GetProperty("id").GetString()!] = Shape("item", i));
		});
		d.Guard("equipment.json", () =>
		{
			using JsonDocument equipment = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "equipment.json")));
			d.Each(Array(equipment.RootElement, "equipment"), "equipment.json equipment", piece =>
			{
				string id = piece.GetProperty("id").GetString()!;
				string? category = piece.TryGetProperty("category", out JsonElement c) ? Text(c) : null;
				string? slot = piece.TryGetProperty("slot", out JsonElement sl) ? Text(sl) : null;
				d.AllEquipment[id] = category ?? slot ?? "";
				if (piece.TryGetProperty("use", out JsonElement use) && Text(use) is { } ability)
				{
					d.PieceUses[id] = ability;
				}
				if (slot == "weapon")
				{
					d.EquipmentCategories[id] = category ?? "weapon";
					if (piece.TryGetProperty("hands", out JsonElement hands) && Whole(hands) == 2)
					{
						d.TwoHanded.Add(id);
					}
				}
			});
		});
		if (File.Exists(Path.Combine(core, "combos.json")))
		{
			d.Guard("combos.json", () =>
			{
				using JsonDocument combos = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "combos.json")));
				d.Each(Array(combos.RootElement, "combos"), "combos.json combos", combo =>
					d.Recipes[combo.GetProperty("id").GetString()!] = (combo.GetProperty("result").GetString()!,
						[.. Array(combo, "abilities").Select(a => a.ValueKind == JsonValueKind.Object && a.TryGetProperty("ability", out JsonElement ab) ? Text(ab) : Text(a)).OfType<string>()]));
			});
		}
		return d;
	}

	private void LoadArea(string dataDir, string file)
	{
		using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(dataDir, file)));
		JsonElement area = doc.RootElement;
		string id = area.GetProperty("id").GetString()!;
		AreaKind kind = Enum.Parse<AreaKind>(area.GetProperty("kind").GetString()!, ignoreCase: true);
		Areas.Add(id);
		AreaKinds[id] = kind;
		Each(Array(area, "hooks"), $"{file} hooks", hook =>
		{
			if (hook.TryGetProperty("actor_at", out JsonElement actorAt) && Cell(actorAt) is { } stand)
			{
				ActorAt[hook.GetProperty("id").GetString()!] = stand;
			}
			if (hook.TryGetProperty("event", out JsonElement ev) && Text(ev) is { } started && hook.TryGetProperty("at", out JsonElement at) && Cell(at) is { } cell)
			{
				EventStarts.TryAdd(started, (id, cell.X, cell.Y));
			}
		});
		Music.Add($"{FieldRenderer.KindName(kind).ToLowerInvariant()}_{id}");
		string[] rows = [.. Array(area, "map").Select(Text).OfType<string>()];
		int width = rows.Length == 0 ? 0 : rows[0].Length;
		var changes = new List<(string, (int X, int Y)[])>();
		Each(Array(area, "changes"), $"{file} changes", c => changes.Add((c.GetProperty("flag").GetString()!, Cells(c))));
		var points = new HashSet<string>(new[] { "hooks", "chests", "shops", "rests", "ports", "helms" }.SelectMany(list => Array(area, list)).Select(IdOf).OfType<string>());
		Keepers.UnionWith(new[] { "shops", "rests" }.SelectMany(list => Array(area, list)).Select(IdOf).OfType<string>());
		static string Flag(JsonElement hook, string name) => hook.TryGetProperty(name, out JsonElement f) ? Text(f) ?? "" : "";
		var tileHooks = new List<TileHook>();
		Each(Array(area, "hooks").Where(h => h.TryGetProperty("tile", out _)), $"{file} hooks", h =>
		{
			char tile = h.GetProperty("tile").GetString()![0];
			(int, int)[] cells = [.. rows.SelectMany((r, y) => r.Select((c, x) => (c, x, y))).Where(t => t.c == tile).Select(t => (t.x, t.y))];
			tileHooks.Add(new TileHook(h.GetProperty("id").GetString()!, tile, cells, Flag(h, "require_flag"), Flag(h, "block_flag")));
		});
		var whileActive = new HashSet<string>(Array(area, "hooks")
			.Where(h => h.TryGetProperty("solid", out JsonElement solid) && Text(solid) == "while_active").Select(IdOf).OfType<string>());
		// A step time may have a fraction; the glide takes whole milliseconds. Belts may share cells, each gated by its flags.
		var belts = new List<Belt>();
		Each(Array(area, "conveyors"), $"{file} conveyors", c =>
		{
			double ms = c.TryGetProperty("step_ms", out JsonElement step) && step.ValueKind == JsonValueKind.Number ? step.GetDouble() : 250;
			belts.Add(new Belt([.. Cells(c)], Math.Max(1, (int)Math.Round(ms)), Flag(c, "require_flag"), Flag(c, "block_flag")));
		});
		Belts[id] = belts;
		Maps[id] = new AreaMap(width, rows.Length, [.. rows.SelectMany(r => r.PadRight(width).Select(AreaMap.Terrain))], changes, points,
			[.. Array(area, "chests").Select(IdOf).OfType<string>()], tileHooks, whileActive);
	}

	// What could not be read from the data files, as `where: why`; the rest loads without it.
	public List<string> Problems { get; } = [];

	private void Guard(string where, Action read)
	{
		try
		{
			read();
		}
		catch (Exception e) when (e is InvalidOperationException or KeyNotFoundException or FormatException or IndexOutOfRangeException or ArgumentException
			or JsonException or IOException or OverflowException or NullReferenceException)
		{
			Problems.Add($"{where}: {e.Message}");
		}
	}

	private void Each(IEnumerable<JsonElement> items, string where, Action<JsonElement> read)
	{
		int n = 0;
		foreach (JsonElement item in items)
		{
			Guard($"{where}[{n++}]", () => read(item));
		}
	}

	private static string? Text(JsonElement e) => e.ValueKind == JsonValueKind.String ? e.GetString() : null;

	// A name, or an object's `id`.
	private static string? IdOf(JsonElement e) => e.ValueKind == JsonValueKind.Object ? e.TryGetProperty("id", out JsonElement id) ? Text(id) : null : Text(e);

	// A number, rounded to a whole one; null for anything else.
	private static int? Whole(JsonElement e) => e.ValueKind == JsonValueKind.Number && e.TryGetDouble(out double v) && Math.Abs(v) < int.MaxValue ? (int)Math.Round(v) : null;

	private static (int X, int Y)? Cell(JsonElement e) =>
		e.ValueKind == JsonValueKind.Array && e.GetArrayLength() >= 2 && Whole(e[0]) is int x && Whole(e[1]) is int y ? (x, y) : null;

	private static (int X, int Y)[] Cells(JsonElement e) => [.. Array(e, "cells").Select(Cell).OfType<(int X, int Y)>()];

	// A belt's cells, time per cell and the flags that gate it.
	public sealed record Belt(HashSet<(int X, int Y)> Cells, int Ms, string RequireFlag, string BlockFlag)
	{
		public bool Active(Func<string, bool> flag) => (RequireFlag.Length == 0 || flag(RequireFlag)) && (BlockFlag.Length == 0 || !flag(BlockFlag));
	}

	// The time per cell of the belt running on a cell of an area: the first whose flags hold, null for none.
	public int? BeltMsAt(string area, int x, int y, Func<string, bool> flag) =>
		Belts.GetValueOrDefault(area)?.FirstOrDefault(b => b.Cells.Contains((x, y)) && b.Active(flag))?.Ms;

	private static IEnumerable<JsonElement> Array(JsonElement e, string name) =>
		e.TryGetProperty(name, out JsonElement list) && list.ValueKind == JsonValueKind.Array ? list.EnumerateArray() : [];

	private static ActionShape Shape(string kind, JsonElement def)
	{
		JsonElement first = Array(def, "effects").FirstOrDefault();
		string type = first.ValueKind == JsonValueKind.Object ? first.GetProperty("type").GetString()! : "";
		string effect = type switch
		{
			"physical_damage" or "elemental_damage" => "damage",
			"heal" or "restore_tp" => "heal",
			"revive" => "revive",
			"apply_status" => "status",
			"cure_status" => "cure",
			"modify_stat" => "stat",
			_ => "other",
		};
		string element = first.ValueKind == JsonValueKind.Object && first.TryGetProperty("element", out JsonElement el) ? el.GetString()! : "none";
		return new ActionShape(kind, effect, element);
	}

	[GeneratedRegex(@"\{[^{}]*\}")]
	private static partial Regex Placeholder();

	// The characters a bitmap font for a language and role must have to draw that language's text tables: for the
	// dialogue role the string tables' lines and the dialogue box's own words; for the battle numbers the digits and the
	// words that float over targets; for the hint the dialogue box's next and skip words and the key and button names;
	// otherwise every line. Placeholders such as {0} are left out, spaces and line breaks too.
	public SortedSet<char> TextGlyphs(string language, TextRole? role)
	{
		IEnumerable<(string Key, string Text, bool Dialogue)> lines = TextTables.GetValueOrDefault(language) ?? [];
		lines = role switch
		{
			TextRole.Dialogue or TextRole.Narration => lines.Where(l => l.Dialogue || l.Key.StartsWith("ui.dialogue.", StringComparison.Ordinal)),
			TextRole.BattleNumbers => lines.Where(l => l.Key.StartsWith("ui.battle.float.", StringComparison.Ordinal) || l.Key.StartsWith("name.status.", StringComparison.Ordinal))
				.Append(("", "0123456789+", false)),
			TextRole.Hint => lines.Where(l => l.Key is "ui.dialogue.next" or "ui.dialogue.skip" || l.Key.StartsWith("ui.key.", StringComparison.Ordinal)
				|| l.Key.StartsWith("ui.button.", StringComparison.Ordinal)),
			_ => lines,
		};
		return [.. lines.SelectMany(l => Placeholder().Replace(l.Text, "")).Where(c => !char.IsWhiteSpace(c) || c == '\u3000')];
	}

	// The shape of an action no data file defines (a bare basic attack): a physical hit.
	public ActionShape ShapeOf(string id, CommandKind command) =>
		Actions.GetValueOrDefault(id) ?? new ActionShape(command == CommandKind.Item ? "item" : "attack", "damage", "none");
}
