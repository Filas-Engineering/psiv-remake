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
	public Dictionary<string, Dictionary<(int X, int Y), int>> BeltMs { get; } = [];
	public HashSet<string> Enemies { get; } = [];
	public HashSet<string> Encounters { get; } = [];
	public HashSet<string> Statuses { get; } = [];
	public HashSet<string> Cameras { get; } = [];
	public HashSet<string> Portraits { get; } = [];
	public HashSet<string> Animations { get; } = [];
	// game.json's `interact` (R77): true under "nearby", when the prompt and interaction follow the session's target query.
	public bool InteractNearby { get; private set; }
	// game.json's `run_speed_percent` (R81), 0 when it gives none: the real-time walk then runs through the session.
	public int RunSpeedPercent { get; private set; }
	// Each wandering actor's speed against the area's step time (game.json actors' `wander.speed_percent`, default 50).
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
		d.Languages.UnionWith(session.GameInfo().Languages);
		d.Cues.UnionWith(Synth.SampleCues);
		using JsonDocument game = JsonDocument.Parse(File.ReadAllText(Path.Combine(dataDir, "game.json")));
		JsonElement root = game.RootElement;
		foreach (JsonElement area in root.GetProperty("areas").EnumerateArray())
		{
			using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(dataDir, area.GetString()!)));
			string id = doc.RootElement.GetProperty("id").GetString()!;
			AreaKind kind = Enum.Parse<AreaKind>(doc.RootElement.GetProperty("kind").GetString()!, ignoreCase: true);
			d.Areas.Add(id);
			d.AreaKinds[id] = kind;
			foreach (JsonElement hook in Array(doc.RootElement, "hooks"))
			{
				if (hook.TryGetProperty("actor_at", out JsonElement actorAt) && actorAt.ValueKind == JsonValueKind.Array)
				{
					d.ActorAt[hook.GetProperty("id").GetString()!] = (actorAt[0].GetInt32(), actorAt[1].GetInt32());
				}
				if (hook.TryGetProperty("event", out JsonElement ev) && hook.TryGetProperty("at", out JsonElement at) && at.ValueKind == JsonValueKind.Array)
				{
					d.EventStarts.TryAdd(ev.GetString()!, (id, at[0].GetInt32(), at[1].GetInt32()));
				}
			}
			d.Music.Add($"{FieldRenderer.KindName(kind).ToLowerInvariant()}_{id}");
			string[] rows = [.. Array(doc.RootElement, "map").Select(r => r.GetString()!)];
			int width = rows.Length == 0 ? 0 : rows[0].Length;
			var changes = Array(doc.RootElement, "changes")
				.Select(c => (c.GetProperty("flag").GetString()!, Array(c, "cells").Select(at => (at[0].GetInt32(), at[1].GetInt32())).ToArray())).ToList();
			var points = new HashSet<string>(new[] { "hooks", "chests", "shops", "rests", "ports", "helms" }
				.SelectMany(list => Array(doc.RootElement, list)).Select(p => p.GetProperty("id").GetString()!));
			d.Keepers.UnionWith(new[] { "shops", "rests" }.SelectMany(list => Array(doc.RootElement, list)).Select(p => p.GetProperty("id").GetString()!));
			string Flag(JsonElement hook, string name) => hook.TryGetProperty(name, out JsonElement f) ? f.GetString()! : "";
			var tileHooks = Array(doc.RootElement, "hooks").Where(h => h.TryGetProperty("tile", out _)).Select(h =>
			{
				char tile = h.GetProperty("tile").GetString()![0];
				(int, int)[] cells = [.. rows.SelectMany((r, y) => r.Select((c, x) => (c, x, y))).Where(t => t.c == tile).Select(t => (t.x, t.y))];
				return new TileHook(h.GetProperty("id").GetString()!, tile, cells, Flag(h, "require_flag"), Flag(h, "block_flag"));
			}).ToList();
			var whileActive = new HashSet<string>(Array(doc.RootElement, "hooks")
				.Where(h => h.TryGetProperty("solid", out JsonElement solid) && solid.GetString() == "while_active").Select(h => h.GetProperty("id").GetString()!));
			d.BeltMs[id] = Array(doc.RootElement, "conveyors")
				.SelectMany(c => Array(c, "cells").Select(at => ((at[0].GetInt32(), at[1].GetInt32()), c.TryGetProperty("step_ms", out JsonElement ms) ? ms.GetInt32() : 250)))
				.ToDictionary(x => x.Item1, x => x.Item2);
			d.Maps[id] = new AreaMap(width, rows.Length, [.. rows.SelectMany(r => r.PadRight(width).Select(AreaMap.Terrain))], changes, points,
				[.. Array(doc.RootElement, "chests").Select(c => c.GetProperty("id").GetString()!)], tileHooks, whileActive);
		}
		foreach (JsonElement language in Array(root, "languages"))
		{
			var lines = new List<(string, string, bool)>();
			if (language.TryGetProperty("text", out JsonElement textFile))
			{
				using JsonDocument text = JsonDocument.Parse(File.ReadAllText(Path.Combine(dataDir, textFile.GetString()!)));
				lines.AddRange(text.RootElement.GetProperty("text").EnumerateObject().Select(t => (t.Name, t.Value.GetString() ?? "", false)));
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
		}
		d.Flags.UnionWith(Array(root, "flags").Select(f => f.GetString()!));
		d.InteractNearby = root.TryGetProperty("interact", out JsonElement interact) && interact.GetString() == "nearby";
		d.RunSpeedPercent = root.TryGetProperty("run_speed_percent", out JsonElement run) && run.ValueKind == JsonValueKind.Number ? run.GetInt32() : 0;
		foreach (JsonElement actor in Array(root, "actors").Where(a => a.ValueKind == JsonValueKind.Object && a.TryGetProperty("wander", out _)))
		{
			JsonElement wander = actor.GetProperty("wander");
			d.WanderPercent[actor.GetProperty("id").GetString()!] = wander.TryGetProperty("speed_percent", out JsonElement speed) ? speed.GetInt32() : 50;
		}
		d.Music.UnionWith(["title", "battle_town", "battle_field", "battle_dungeon", "battle_scripted"]);
		d.FieldVehicles.UnionWith(Array(root, "vehicles").Select(v => v.GetProperty("id").GetString()!));
		foreach (JsonElement v in Array(root, "vehicles"))
		{
			d.VehicleCrosses[v.GetProperty("id").GetString()!] = [.. Array(v, "crosses").Concat(Array(v, "breaks")).Select(t => t.GetString()!)];
		}
		var scripted = new Dictionary<string, string>();
		foreach (JsonElement script in root.GetProperty("events").EnumerateArray())
		{
			foreach (Match m in ScriptAudio().Matches(File.ReadAllText(Path.Combine(dataDir, script.GetString()!))))
			{
				scripted.TryAdd(m.Groups[2].Value, m.Groups[1].Value);
			}
		}
		if (root.GetProperty("presentation") is var presentation)
		{
			foreach (JsonElement id in Array(presentation, "audio"))
			{
				// An id is a loop or a sound by how the scripts use it; every other id is a track.
				string audio = id.GetString()!;
				(scripted.GetValueOrDefault(audio) switch { "ambient play" => d.Ambient, "sound" => d.Cues, _ => d.Music }).Add(audio);
			}
			// An entry is a name, or an object with its `id` and `seconds`.
			d.Animations.UnionWith(Array(presentation, "animations").Select(a => a.ValueKind == JsonValueKind.Object ? a.GetProperty("id").GetString()! : a.GetString()!));
			d.Portraits.UnionWith(Array(presentation, "portraits").Select(a => a.GetString()!));
		}
		string core = Path.GetFullPath(Path.Combine(dataDir, root.GetProperty("core_data").GetString()!));
		using JsonDocument party = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "party.json")));
		d.Characters.UnionWith(Array(party.RootElement, "characters").Select(c => c.GetProperty("id").GetString()!));
		d.Vehicles.UnionWith(Array(party.RootElement, "vehicles").Select(c => c.GetProperty("id").GetString()!));
		foreach (JsonElement v in Array(party.RootElement, "vehicles"))
		{
			d.VehicleSkills[v.GetProperty("id").GetString()!] = [.. Array(v, "abilities").Select(a => a.GetString()!)];
		}
		using JsonDocument enemies = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "enemies.json")));
		foreach (JsonElement e in Array(enemies.RootElement, "enemies"))
		{
			string id = e.GetProperty("id").GetString()!;
			d.Enemies.Add(id);
			d.EnemyStatuses[id] = [.. Array(e, "statuses").Select(x => x.GetString()!)];
		}
		foreach (JsonElement encounter in Array(enemies.RootElement, "encounters"))
		{
			string id = encounter.GetProperty("id").GetString()!;
			d.Encounters.Add(id);
			foreach (JsonElement e in Array(encounter, "enemies").Where(e => e.ValueKind is JsonValueKind.String or JsonValueKind.Object))
			{
				d.EnemyEncounters.TryAdd(e.ValueKind == JsonValueKind.String ? e.GetString()! : e.GetProperty("enemy").GetString()!, id);
			}
		}
		using JsonDocument statuses = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "statuses.json")));
		d.Statuses.UnionWith(Array(statuses.RootElement, "statuses").Select(c => c.GetProperty("id").GetString()!));
		using JsonDocument abilities = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "abilities.json")));
		foreach (JsonElement a in Array(abilities.RootElement, "abilities"))
		{
			d.Actions[a.GetProperty("id").GetString()!] = Shape(a.GetProperty("kind").GetString()!, a);
			if (a.TryGetProperty("target", out JsonElement targeting) && GameSession.ParseName(NameKind.Targeting, targeting.GetString()!) is { } t)
			{
				d.AbilityTargeting[a.GetProperty("id").GetString()!] = (Targeting)t;
			}
		}
		using JsonDocument items = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "items.json")));
		foreach (JsonElement i in Array(items.RootElement, "items"))
		{
			d.Actions[i.GetProperty("id").GetString()!] = Shape("item", i);
		}
		using JsonDocument equipment = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "equipment.json")));
		foreach (JsonElement piece in Array(equipment.RootElement, "equipment"))
		{
			d.AllEquipment[piece.GetProperty("id").GetString()!] = piece.TryGetProperty("category", out JsonElement category) ? category.GetString()! : piece.TryGetProperty("slot", out JsonElement slot) ? slot.GetString()! : "";
		}
		foreach (JsonElement piece in Array(equipment.RootElement, "equipment").Where(x => x.TryGetProperty("use", out _)))
		{
			d.PieceUses[piece.GetProperty("id").GetString()!] = piece.GetProperty("use").GetString()!;
		}
		foreach (JsonElement piece in Array(equipment.RootElement, "equipment").Where(x => x.TryGetProperty("slot", out JsonElement slot) && slot.GetString() == "weapon"))
		{
			string id = piece.GetProperty("id").GetString()!;
			d.EquipmentCategories[id] = piece.TryGetProperty("category", out JsonElement category) ? category.GetString()! : "weapon";
			if (piece.TryGetProperty("hands", out JsonElement hands) && hands.ValueKind == JsonValueKind.Number && hands.GetInt32() == 2)
			{
				d.TwoHanded.Add(id);
			}
		}
		if (File.Exists(Path.Combine(core, "combos.json")))
		{
			using JsonDocument combos = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "combos.json")));
			foreach (JsonElement combo in Array(combos.RootElement, "combos"))
			{
				d.Recipes[combo.GetProperty("id").GetString()!] = (combo.GetProperty("result").GetString()!,
					[.. Array(combo, "abilities").Select(a => a.ValueKind == JsonValueKind.String ? a.GetString()! : a.GetProperty("ability").GetString()!)]);
			}
		}
		return d;
	}

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
