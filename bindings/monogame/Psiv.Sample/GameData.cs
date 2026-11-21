using System.Text.Json;
using System.Text.RegularExpressions;

using Psiv.Session;

namespace Psiv.Sample;

// An ability's or item's shape, for presentations that fall back on kind and element: its kind ("attack",
// "technique", "skill", "combo" or "item"), the kind of its first effect ("damage", "heal", "revive", "status",
// "cure", "stat" or "other") and its element ("none" for physical and non-damage effects).
public sealed record ActionShape(string Kind, string Effect, string Element);

// The ids the session's data defines, for checking an asset manifest and for battle presentations. The session lists
// actors, cameras and languages; the C interface lists no areas, characters, enemies, encounters, statuses, abilities,
// items, audio ids or script animations, so those are read from the files the session loaded (game.json, the area
// files, the event scripts and core's battle data).
public sealed partial class GameData
{
	public HashSet<string> Areas { get; } = [];
	public Dictionary<string, AreaKind> AreaKinds { get; } = [];
	public HashSet<string> Actors { get; } = [];
	public HashSet<string> Characters { get; } = [];
	public HashSet<string> Vehicles { get; } = [];
	// The vehicles game.json gives field rules, which the field draws.
	public HashSet<string> FieldVehicles { get; } = [];
	public HashSet<string> Enemies { get; } = [];
	public HashSet<string> Encounters { get; } = [];
	public HashSet<string> Statuses { get; } = [];
	public HashSet<string> Cameras { get; } = [];
	public HashSet<string> Portraits { get; } = [];
	public HashSet<string> Animations { get; } = [];
	public HashSet<string> Languages { get; } = [];
	// Every audio id the game can ask for by kind: the data's tracks, loops and sounds, the tracks the sample derives
	// where the data names none, and the sample's own cues.
	public HashSet<string> Music { get; } = [];
	public HashSet<string> Ambient { get; } = [];
	public HashSet<string> Cues { get; } = [];
	public Dictionary<string, ActionShape> Actions { get; } = [];

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
			d.Music.Add($"{FieldRenderer.KindName(kind).ToLowerInvariant()}_{id}");
		}
		d.Music.UnionWith(["title", "battle_town", "battle_field", "battle_dungeon", "battle_scripted"]);
		d.FieldVehicles.UnionWith(Array(root, "vehicles").Select(v => v.GetProperty("id").GetString()!));
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
			d.Animations.UnionWith(Array(presentation, "animations").Select(a => a.GetString()!));
			d.Portraits.UnionWith(Array(presentation, "portraits").Select(a => a.GetString()!));
		}
		string core = Path.GetFullPath(Path.Combine(dataDir, root.GetProperty("core_data").GetString()!));
		using JsonDocument party = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "party.json")));
		d.Characters.UnionWith(Array(party.RootElement, "characters").Select(c => c.GetProperty("id").GetString()!));
		d.Vehicles.UnionWith(Array(party.RootElement, "vehicles").Select(c => c.GetProperty("id").GetString()!));
		using JsonDocument enemies = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "enemies.json")));
		d.Enemies.UnionWith(Array(enemies.RootElement, "enemies").Select(c => c.GetProperty("id").GetString()!));
		d.Encounters.UnionWith(Array(enemies.RootElement, "encounters").Select(c => c.GetProperty("id").GetString()!));
		using JsonDocument statuses = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "statuses.json")));
		d.Statuses.UnionWith(Array(statuses.RootElement, "statuses").Select(c => c.GetProperty("id").GetString()!));
		using JsonDocument abilities = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "abilities.json")));
		foreach (JsonElement a in Array(abilities.RootElement, "abilities"))
		{
			d.Actions[a.GetProperty("id").GetString()!] = Shape(a.GetProperty("kind").GetString()!, a);
		}
		using JsonDocument items = JsonDocument.Parse(File.ReadAllText(Path.Combine(core, "items.json")));
		foreach (JsonElement i in Array(items.RootElement, "items"))
		{
			d.Actions[i.GetProperty("id").GetString()!] = Shape("item", i);
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
			"heal" => "heal",
			"revive" => "revive",
			"apply_status" => "status",
			"cure_status" => "cure",
			"modify_stat" => "stat",
			_ => "other",
		};
		string element = first.ValueKind == JsonValueKind.Object && first.TryGetProperty("element", out JsonElement el) ? el.GetString()! : "none";
		return new ActionShape(kind, effect, element);
	}

	// The shape of an action no data file defines (a bare basic attack): a physical hit.
	public ActionShape ShapeOf(string id, CommandKind command) =>
		Actions.GetValueOrDefault(id) ?? new ActionShape(command == CommandKind.Item ? "item" : "attack", "damage", "none");
}
