using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace Psiv.Sample;

// What a battle animation file is checked against: the game data, the effects pictures can play (generated and the
// manifest's, with their frame counts), the styles' color names and the asset directory for backdrop pictures.
public sealed record AnimCheckContext(GameData Data, Dictionary<string, int> EffectFrames, HashSet<string> ColorNames, string? Directory, HashSet<string> BackdropKeys);

// The battle animation scripts (docs/battle-animation.md): the built-in set embedded in the sample, with an asset
// directory's battle_animation.json (and the files it includes) layered over it entry by entry. Entries stay JSON and
// are read when a round's steps are built (BattleAnimationRun) and drawn (BattleAnimationEval).
public sealed partial class BattleAnimation
{
	public const string FileName = "battle_animation.json";
	public const int Version = 1;
	// A sanity limit, not a design limit: a script longer than ten minutes at 60 Hz is a problem.
	public const int MaxFrames = 36000;

	public static readonly string[] Sections = ["version", "include", "settings", "select", "scripts", "fragments", "effects", "figures", "numbers", "transitions", "reduced_flash",
		"weapon_classes", "placements"];
	public static readonly string[] TurnPhases = ["forward", "banner", "animate", "effect", "result", "back"];
	public static readonly string[] BattlePhases = ["cover", "hold", "reveal"];
	public static readonly string[] TrackTypes = ["backdrop", "call", "color", "flash", "move", "name_window", "pose", "react", "release", "shake", "sound", "sprite", "state", "transition"];
	public static readonly string[] Eases = ["linear", "in", "out", "in_out", "smooth", "sine", "step"];
	public static readonly string[] Conditions = ["hit", "miss", "critical", "weak", "resisted", "immune", "defeat", "multi", "single", "party_actor", "enemy_actor", "vehicle", "combo",
		"escaped", "layout:front", "layout:side", "result_step", "transition_playing"];
	public static readonly string[] NumberKinds = ["damage", "critical", "weak", "resisted", "immune", "miss", "heal", "tp", "revive", "status", "status_resisted", "status_ends", "stat_up",
		"stat_down", "stat_normal", "no_effect", "felled", "felling_resisted"];
	public static readonly string[] DefeatKinds = ["fade", "blink", "bands", "slide", "sink", "flash_fade", "frames"];
	public static readonly string[] TransitionKinds = ["flash", "fade", "wipe", "iris", "tiles", "spiral", "split", "sequence"];
	public static readonly string[] Layers = ["under_figures", "over_figures", "effects", "over_labels", "top"];
	public static readonly string[] SubjectNames = ["actor", "user", "targets", "hit_targets", "fallen_targets", "party", "enemies", "all", "party_others", "enemies_others", "allies", "changed", "previous_form", "from", "to", "self"];
	public static readonly string[] Anchors = ["center", "feet", "head", "top", "left", "right", "auto", "place"];
	public static readonly string[] CancelReasons = ["actor_unable", "silenced", "not_enough_tp", "no_uses_left", "no_item", "no_target"];
	public static readonly string[] NoticeKeys = ["game.level_up", "game.saved", "game.learned", "battle.opening.normal"];
	public static readonly string[] Outcomes = ["victory", "defeat", "escaped", "scripted_end"];
	public static readonly string[] ReactionKeys = ["damage", "critical", "miss", "heal", "revive", "status", "status_resisted", "status_ends", "stat", "defeat", "instant_defeat",
		"felling_resisted", "no_effect", "tp"];
	public static readonly string[] EventCues = ["hit", "miss", "critical", "heal", "status", "enemy_down"];
	public static readonly string[] Changes = ["join", "merge", "transform", "split", "split_piece", "leave", "self_destruct", "remove_beside"];

	public int RateMilli { get; private set; } = BattleClock.DefaultRateMilli;
	public bool LogRelease { get; private set; }
	// At most Count screen-wide flashes may start in any Frames frames of the controller's clock; null turns it off.
	public (int Count, int Frames)? FlashLimit { get; private set; } = (3, 60);
	// Event cues a release plays that the whole set turns off: every one (`true`) or the listed ones.
	public HashSet<string> EventCuesOff { get; } = new(StringComparer.Ordinal);
	public Dictionary<string, string> Select { get; } = new(StringComparer.Ordinal);
	public Dictionary<string, JsonObject> Scripts { get; } = new(StringComparer.Ordinal);
	public Dictionary<string, JsonObject> Fragments { get; } = new(StringComparer.Ordinal);
	public Dictionary<string, JsonObject> Effects { get; } = new(StringComparer.Ordinal);
	public Dictionary<string, JsonObject> Transitions { get; } = new(StringComparer.Ordinal);
	public Dictionary<string, JsonObject> Numbers { get; } = new(StringComparer.Ordinal);
	// By scope (party, enemy, vehicle, party:ID, enemy:ID, vehicle:ID), then state name.
	public Dictionary<string, Dictionary<string, JsonObject>> States { get; } = new(StringComparer.Ordinal);
	// By `equipment:ID` or `category:NAME`: a weapon animation class.
	public Dictionary<string, string> WeaponClasses { get; } = new(StringComparer.Ordinal);
	// By `count:N` (1 to 5, 5 meaning five or more) or `encounter:ID`: the enemies' places, [x, foot] per formation place.
	public Dictionary<string, JsonNode> Placements { get; } = new(StringComparer.Ordinal);
	// By track type: keep, drop or damp; `damp` is the factor.
	public Dictionary<string, string> ReducedPolicy { get; } = new(StringComparer.Ordinal);
	public float Damp { get; private set; } = 0.25f;
	public bool FromFile { get; private set; }
	public int FileScripts { get; private set; }
	public int FileKeys { get; private set; }
	public List<string> Files { get; } = [];

	private static JsonObject? _builtinRoot;

	private static JsonObject BuiltinRoot()
	{
		if (_builtinRoot is null)
		{
			using Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("battle/animation.json")
				?? throw new InvalidOperationException("the built-in battle animation set is not embedded");
			_builtinRoot = (JsonObject)JsonNode.Parse(stream, documentOptions: new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip })!;
		}
		return _builtinRoot;
	}

	public static BattleAnimation Builtin()
	{
		var a = new BattleAnimation();
		a.Take(BuiltinRoot(), null, null, builtin: true);
		return a;
	}

	public static bool IsId(string id) => id.Length is >= 1 and <= 64 && id.All(c => c is (>= 'a' and <= 'z') or (>= '0' and <= '9') or '_' or '.');

	private static JsonNode? Parse(string path, string display, Action<string, string, string> problem)
	{
		try
		{
			return JsonNode.Parse(File.ReadAllText(path), documentOptions: new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip });
		}
		catch (JsonException e)
		{
			problem(display, "-", "not valid JSON: " + e.Message);
			return null;
		}
	}

	// The asset directory's file over the built-in set. A problem leaves its entry out, so the built-in one stays.
	public static BattleAnimation Load(string directory, string file, AnimCheckContext ctx, List<AssetProblem> problems)
	{
		BattleAnimation a = Builtin();
		a.FromFile = true;
		void Problem(string display, string entry, string reason) => problems.Add(new AssetProblem(display, entry, reason));
		string path = Path.GetFullPath(Path.Combine(directory, file));
		a.Files.Add(path);
		if (Parse(path, file, Problem) is not JsonObject root)
		{
			if (File.Exists(path))
			{
				Problem(file, "-", "must be an object");
			}
			return a;
		}
		var checker = new Checker(ctx, a, Problem);
		var files = new List<(JsonObject Root, string Display)> { (root, file) };
		foreach ((string name, JsonNode? _) in root)
		{
			if (!Sections.Contains(name))
			{
				Problem(file, "-", $"unknown field '{name}'");
			}
		}
		if (root["version"] is not JsonValue version || !version.TryGetValue(out int v) || v != Version)
		{
			Problem(file, "version", "must be 1");
		}
		if (root["include"] is JsonNode include)
		{
			if (include is not JsonArray list)
			{
				Problem(file, "include", "must be a list of files in the asset directory");
			}
			else
			{
				for (int i = 0; i < list.Count; i++)
				{
					string? name = list[i] is JsonValue s && s.TryGetValue(out string? text) ? text : null;
					string full = name is null ? "" : Path.GetFullPath(Path.Combine(directory, name));
					if (name is null || !File.Exists(full))
					{
						Problem(file, $"include[{i}]", "missing file");
						continue;
					}
					a.Files.Add(full);
					if (Parse(full, name, Problem) is not JsonObject inc)
					{
						continue;
					}
					foreach ((string field, JsonNode? _) in inc)
					{
						if (field is "version" or "include" || !root.ContainsKey(field) && !Sections.Contains(field))
						{
							Problem(name, "-", $"unknown field '{field}'");
						}
					}
					files.Add((inc, name));
				}
			}
		}
		// Ids first, so references between files resolve; an id defined twice keeps the first.
		var defined = new Dictionary<string, string>(StringComparer.Ordinal);
		foreach ((JsonObject r, string display) in files)
		{
			foreach (string section in new[] { "scripts", "fragments", "effects", "transitions" })
			{
				if (r[section] is JsonObject entries)
				{
					foreach ((string id, JsonNode? _) in entries)
					{
						if (!defined.TryAdd(section + "." + id, display))
						{
							Problem(display, $"{section}.{id}", $"defined twice: first in {defined[section + "." + id]}");
						}
					}
				}
			}
		}
		checker.Declare(files);
		foreach ((JsonObject r, string display) in files)
		{
			checker.Display = display;
			JsonObject clean = checker.Clean(r);
			a.Take(clean, display, defined, builtin: false);
		}
		return a;
	}

	// Takes a checked root's entries over what is there.
	private void Take(JsonObject root, string? display, Dictionary<string, string>? defined, bool builtin)
	{
		if (root["settings"] is JsonObject settings)
		{
			if (settings["rate"] is JsonValue rate && rate.TryGetValue(out double hz))
			{
				RateMilli = (int)Math.Round(hz * 1000);
			}
			if (settings["log_timing"] is JsonValue timing && timing.TryGetValue(out string? t))
			{
				LogRelease = t == "release";
			}
			if (settings["flash_limit"] is JsonNode limit)
			{
				FlashLimit = limit is JsonObject l ? ((int)(BattleAnimationRun.Num(l["count"]) ?? 3), (int)(BattleAnimationRun.Num(l["frames"]) ?? 60)) : null;
			}
			if (settings["event_cues_off"] is JsonNode off)
			{
				EventCuesOff.Clear();
				EventCuesOff.UnionWith(CuesOff(off));
			}
		}
		void Section(string name, Dictionary<string, JsonObject> into, bool count = false)
		{
			if (root[name] is not JsonObject entries)
			{
				return;
			}
			foreach ((string id, JsonNode? node) in entries)
			{
				if (node is JsonObject o && (defined is null || name == "numbers" || defined.GetValueOrDefault(name + "." + id) == display))
				{
					into[id] = (JsonObject)o.DeepClone();
					if (count && !builtin)
					{
						FileScripts++;
					}
				}
			}
		}
		Section("scripts", Scripts, count: true);
		Section("fragments", Fragments);
		Section("effects", Effects);
		Section("transitions", Transitions);
		Section("numbers", Numbers);
		if (root["select"] is JsonObject select)
		{
			foreach ((string key, JsonNode? node) in select)
			{
				if (node is JsonValue v && v.TryGetValue(out string? id))
				{
					Select[key] = id;
					if (!builtin)
					{
						FileKeys++;
					}
				}
			}
		}
		if (root["figures"] is JsonObject figures)
		{
			foreach ((string scope, JsonNode? node) in figures)
			{
				if (node is JsonObject f && f["states"] is JsonObject states)
				{
					Dictionary<string, JsonObject> into = States.TryGetValue(scope, out var existing) ? existing : States[scope] = new(StringComparer.Ordinal);
					foreach ((string name, JsonNode? state) in states)
					{
						if (state is JsonObject s)
						{
							into[name] = (JsonObject)s.DeepClone();
						}
					}
				}
			}
		}
		if (root["weapon_classes"] is JsonObject classes)
		{
			foreach ((string key, JsonNode? node) in classes)
			{
				if (node is JsonValue v && v.TryGetValue(out string? c))
				{
					WeaponClasses[key] = c;
				}
			}
		}
		if (root["placements"] is JsonObject placements)
		{
			foreach ((string key, JsonNode? node) in placements)
			{
				if (node is not null)
				{
					Placements[key] = node.DeepClone();
				}
			}
		}
		if (root["reduced_flash"] is JsonObject reduced)
		{
			foreach ((string key, JsonNode? node) in reduced)
			{
				if (key == "damp" && node is JsonValue d && d.TryGetValue(out double damp))
				{
					Damp = (float)damp;
				}
				else if (node is JsonValue p && p.TryGetValue(out string? policy))
				{
					ReducedPolicy[key] = policy;
				}
			}
		}
	}

	// An `event_cues_off` value: true for every event cue, or the list of them.
	public static IEnumerable<string> CuesOff(JsonNode? n) => BattleAnimationRun.Bool(n) == true ? EventCues
		: n is JsonArray a ? a.Select(BattleAnimationRun.Str).OfType<string>() : [];

	// The whole set as one file, for the placeholder export: every key, script, fragment, effect, state, style and
	// transition, so the export plays as the built-in set does.
	public void Write(string path)
	{
		JsonObject root = (JsonObject)BuiltinRoot().DeepClone();
		File.WriteAllText(path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true, IndentCharacter = '\t', IndentSize = 1 }) + "\n");
	}

	// The script a key names, or null.
	public string? Find(string key) => Select.TryGetValue(key, out string? id) && Scripts.ContainsKey(id) ? id : null;

	// The figure state for a figure's scopes (most specific first), its variant before its base name.
	public JsonObject? State(IEnumerable<string> scopes, string name)
	{
		List<string> names = name.Contains(':') ? [name, name[..name.IndexOf(':')]] : [name];
		foreach (string n in names)
		{
			foreach (string scope in scopes)
			{
				if (States.TryGetValue(scope, out var states) && states.TryGetValue(n, out JsonObject? state))
				{
					return state;
				}
			}
		}
		return null;
	}

	public string WeaponClass(string equipment, string category) =>
		WeaponClasses.GetValueOrDefault("equipment:" + equipment) ?? WeaponClasses.GetValueOrDefault("category:" + category) ?? category;

	// Why a script cannot play as the dialogue's `fx` tag, or null: an fx script is a step script (one `step` phase, or
	// none) of screen-level tracks only: flash, color on the stage or the frame, shake and sound.
	public string? FxProblem(string id)
	{
		if (!Scripts.TryGetValue(id, out JsonObject? script))
		{
			return $"no fx script '{id}' in the battle animation set";
		}
		for (int depth = 0; script is not null && depth < 9; depth++)
		{
			if (script["phases"] is JsonObject phases && phases.Any(p => p.Key != "step"))
			{
				return $"fx script '{id}' has phases other than step: an fx script is a step script";
			}
			foreach (JsonObject t in (script["tracks"] as JsonArray ?? []).OfType<JsonObject>())
			{
				string type = BattleAnimationRun.Str(t["type"]) ?? "";
				if (type is not ("flash" or "color" or "shake" or "sound") || type == "color" && BattleAnimationRun.Str(t["on"]) is not ("stage" or "frame"))
				{
					return $"fx script '{id}' has a {type} track{(type == "color" ? " on a figure or the backdrop" : "")}: an fx script has only flash, color on stage or frame, shake and sound tracks";
				}
			}
			script = BattleAnimationRun.Str(script["extends"]) is { } parent ? Scripts.GetValueOrDefault(parent) : null;
		}
		return null;
	}

	// The summary `--check-assets` prints.
	public string Summary() => FromFile ? $"{FileScripts} battle animation scripts ({FileKeys} keys)" : "the built-in battle animations";

	[GeneratedRegex(@"^-?\d+ms$")]
	private static partial Regex MsPattern();

	[GeneratedRegex(@"^-?\d+(\.\d+)?%$")]
	private static partial Regex PercentPattern();

	[GeneratedRegex(@"^-?\d+(\.\d+)?([fhwm]|px)$")]
	private static partial Regex DistPattern();
}
