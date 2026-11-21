using System.Text.Json;
using System.Text.Json.Nodes;

namespace Psiv.Sample;

// How one action looks in battle: the user's figure animation, the effect drawn on the targets, a cue played with the
// effect, the two steps' lengths at 1x battle speed, how far the screen shakes and whether it flashes.
public sealed record ActionLook(string Animation, string Effect, string Cue, int AnimateMs, int EffectMs, int Shake, bool Flash);

// How the battle stage is arranged: the party seen from behind along the bottom facing the enemies, or the two sides
// facing each other across the screen.
public enum BattleLayout { Front, Side }

// The battle presentation table: the stage's layout (per encounter or area too), step lengths, a look per ability or
// item id, and a look for each way the enemy formation changes. An id the table does not name takes the fallback for its
// kind, effect and element (`technique.damage.shock`, then `technique.damage`, then `technique`), so every action looks
// reasonable with nothing authored; a change takes its entry for the enemy it brings (`transform.ENEMY`), else the
// change's own. The built-in defaults cover every fallback key and change; the asset directory's table
// (battle_presentation.json) overrides any field of any entry.
public sealed class BattlePresentation
{
	public const string FileName = "battle_presentation.json";

	public static readonly string[] Steps = ["intro", "opening", "round", "forward", "retarget", "banner", "animate", "effect", "result", "back", "cancel", "roundend", "notice", "outcome"];
	public static readonly string[] Animations = ["none", "attack", "cast", "item", "defend"];
	public static readonly string[] Kinds = ["attack", "technique", "skill", "combo", "item", "defend"];
	public static readonly string[] Effects = ["damage", "heal", "revive", "status", "cure", "stat", "other"];
	public static readonly string[] Elements = ["none", "heat", "chill", "shock", "gale", "holy"];
	// An enemy joining (a summon or a replacement), two merging into one, one taking its next form, and the mark of an
	// instant defeat landing.
	public static readonly string[] Changes = ["join", "merge", "transform", "instant_defeat"];

	private static readonly Dictionary<string, int> DefaultTimings = new()
	{
		["intro"] = 900, ["opening"] = 1000, ["round"] = 480, ["forward"] = 180, ["retarget"] = 320, ["banner"] = 420, ["animate"] = 280, ["effect"] = 360,
		["result"] = 560, ["back"] = 160, ["cancel"] = 640, ["roundend"] = 440, ["notice"] = 700, ["outcome"] = 900,
	};

	public Dictionary<string, int> Timings { get; } = new(DefaultTimings);
	public Dictionary<string, ActionLook> Fallbacks { get; } = [];
	public Dictionary<string, ActionLook> Actions { get; } = [];
	// By change, and by `CHANGE.ENEMY` for the enemy it brings or marks.
	public Dictionary<string, ActionLook> ChangeLooks { get; } = [];
	public BattleLayout Layout { get; private set; } = BattleLayout.Front;
	// By `encounter:ID` and `area:ID`, over Layout.
	public Dictionary<string, BattleLayout> Layouts { get; } = [];

	public int Ms(string step) => Timings[step];

	// The generated effects Art draws, by id: a style and an element.
	public static IEnumerable<string> GeneratedEffects =>
		new[] { "slash", "burst", "nova" }.SelectMany(style => Elements.Select(e => e == "none" ? style : $"{style}_{e}"))
			.Concat(["ring_status", "ring_stat", "ring_cure", "motes_heal", "motes_revive", "motes_item", "shield", "motes_join", "ring_merge", "nova_transform",
				"mark_defeat"]);

	public BattleLayout LayoutFor(string encounter, string area) =>
		Layouts.TryGetValue("encounter:" + encounter, out BattleLayout layout) || Layouts.TryGetValue("area:" + area, out layout) ? layout : Layout;

	public static string NameOf(BattleLayout layout) => layout.ToString().ToLowerInvariant();

	public static BattleLayout? ParseLayout(string? name) => name switch { "front" => BattleLayout.Front, "side" => BattleLayout.Side, _ => null };

	public static BattlePresentation Defaults()
	{
		var p = new BattlePresentation();
		foreach (string kind in Kinds)
		{
			foreach (string effect in Effects.Prepend(""))
			{
				foreach (string element in effect == "damage" ? Elements.Prepend("") : [""])
				{
					string key = string.Join('.', new[] { kind, effect, element }.Where(k => k.Length > 0));
					p.Fallbacks[key] = DefaultLook(kind, effect.Length == 0 ? "damage" : effect, element.Length == 0 ? "none" : element);
				}
			}
		}
		// The step's length is the effect's; the other fields are an action's.
		p.ChangeLooks["join"] = new ActionLook("none", "motes_join", "", 0, 720, 0, false);
		p.ChangeLooks["merge"] = new ActionLook("none", "ring_merge", "", 0, 900, 2, false);
		p.ChangeLooks["transform"] = new ActionLook("none", "nova_transform", "", 0, 960, 5, true);
		p.ChangeLooks["instant_defeat"] = new ActionLook("none", "mark_defeat", "", 0, 0, 0, false);
		return p;
	}

	// The look for a change: the enemy's own entry, else the change's.
	public ActionLook ChangeFor(string change, string enemy) => ChangeLooks.GetValueOrDefault($"{change}.{enemy}") ?? ChangeLooks[change];

	private static ActionLook DefaultLook(string kind, string effect, string element)
	{
		string suffix = element == "none" ? "" : "_" + element;
		string animation = kind switch { "attack" => "attack", "item" => "item", "defend" => "defend", "skill" when effect == "damage" => "attack", _ => "cast" };
		string fx = (kind, effect) switch
		{
			("defend", _) => "shield",
			("attack", _) => "slash" + suffix,
			("combo", "damage") => "nova" + suffix,
			(_, "damage") => "burst" + suffix,
			(_, "heal") => "motes_heal",
			(_, "revive") => "motes_revive",
			(_, "status") => "ring_status",
			(_, "cure") => "ring_cure",
			("item", _) => "motes_item",
			_ => "ring_stat",
		};
		// A combination is the battle's big moment: it flashes, and one that deals damage shakes the screen.
		bool combo = kind == "combo";
		int shake = combo && effect == "damage" ? 6 : kind == "skill" && effect == "damage" ? 3 : 0;
		return new ActionLook(animation, fx, "", combo ? 420 : 280, combo ? 520 : 360, shake, combo);
	}

	// The look for an action: its own entry, else the most specific fallback.
	public ActionLook For(string id, ActionShape shape)
	{
		if (Actions.TryGetValue(id, out ActionLook? own))
		{
			return own;
		}
		return Fallbacks.GetValueOrDefault($"{shape.Kind}.{shape.Effect}.{shape.Element}") ?? Fallbacks.GetValueOrDefault($"{shape.Kind}.{shape.Effect}")
			?? Fallbacks.GetValueOrDefault(shape.Kind) ?? Fallbacks["attack"];
	}

	public static BattlePresentation Load(string path, string display, GameData data, IEnumerable<string> fileEffects, List<AssetProblem> problems)
	{
		BattlePresentation p = Defaults();
		var effects = new HashSet<string>(GeneratedEffects.Concat(fileEffects).Append("none"));
		void Problem(string entry, string reason) => problems.Add(new AssetProblem(display, entry, reason));
		JsonObject? root;
		try
		{
			root = JsonNode.Parse(File.ReadAllText(path), documentOptions: new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip }) as JsonObject;
		}
		catch (JsonException e)
		{
			Problem("-", "not valid JSON: " + e.Message);
			return p;
		}
		if (root is null)
		{
			Problem("-", "must be an object");
			return p;
		}
		foreach ((string name, JsonNode? node) in root)
		{
			if (name is not ("timings" or "fallbacks" or "actions" or "changes" or "layout" or "layouts"))
			{
				Problem("-", $"unknown field '{name}'");
			}
			else if (name == "layout")
			{
				if (ParseLayout(node is JsonValue v && v.TryGetValue(out string? text) ? text : null) is { } layout)
				{
					p.Layout = layout;
				}
				else
				{
					Problem(name, "must be front or side");
				}
			}
			else if (node is not JsonObject)
			{
				Problem(name, "must be an object");
			}
		}
		if (root["layouts"] is JsonObject layouts)
		{
			foreach ((string key, JsonNode? value) in layouts)
			{
				string[] parts = key.Split(':', 2);
				bool known = parts is ["encounter", string encounter] ? data.Encounters.Contains(encounter) : parts is ["area", string area] && data.Areas.Contains(area);
				if (!known)
				{
					Problem($"layouts.{key}", "unknown key: encounter:ID or area:ID, with an encounter or area of the game data");
				}
				else if (ParseLayout(value is JsonValue v && v.TryGetValue(out string? text) ? text : null) is { } layout)
				{
					p.Layouts[key] = layout;
				}
				else
				{
					Problem($"layouts.{key}", "must be front or side");
				}
			}
		}
		if (root["timings"] is JsonObject timings)
		{
			foreach ((string step, JsonNode? value) in timings)
			{
				if (!Steps.Contains(step))
				{
					Problem($"timings.{step}", $"unknown step '{step}': {string.Join(", ", Steps)}");
				}
				else if (Number(value, 0, 10000) is { } ms)
				{
					p.Timings[step] = ms;
				}
				else
				{
					Problem($"timings.{step}", "must be milliseconds from 0 to 10000");
				}
			}
		}
		if (root["fallbacks"] is JsonObject fallbacks)
		{
			foreach ((string key, JsonNode? value) in fallbacks)
			{
				if (!p.Fallbacks.TryGetValue(key, out ActionLook? look))
				{
					Problem($"fallbacks.{key}", "unknown fallback: KIND, KIND.EFFECT or KIND.damage.ELEMENT, with the kinds, effects and elements docs/assets.md lists");
					continue;
				}
				p.Fallbacks[key] = Merge(look, value, $"fallbacks.{key}", effects, Problem);
			}
		}
		if (root["actions"] is JsonObject actions)
		{
			foreach ((string id, JsonNode? value) in actions)
			{
				if (!data.Actions.TryGetValue(id, out ActionShape? shape))
				{
					Problem($"actions.{id}", $"unknown ability or item '{id}': not in the game data");
					continue;
				}
				p.Actions[id] = Merge(p.For(id, shape), value, $"actions.{id}", effects, Problem);
			}
		}
		if (root["changes"] is JsonObject changes)
		{
			foreach ((string key, JsonNode? value) in changes)
			{
				string[] parts = key.Split('.', 2);
				if (!Changes.Contains(parts[0]) || (parts.Length == 2 && !data.Enemies.Contains(parts[1])))
				{
					Problem($"changes.{key}", $"unknown change: {string.Join(", ", Changes)}, each alone or with .ENEMY for an enemy of the game data");
					continue;
				}
				p.ChangeLooks[key] = Merge(p.ChangeFor(parts[0], parts.Length == 2 ? parts[1] : ""), value, $"changes.{key}", effects, Problem);
			}
		}
		return p;
	}

	private static int? Number(JsonNode? node, int min, int max) =>
		node is JsonValue v && v.TryGetValue(out int n) && n >= min && n <= max ? n : null;

	// The fields an entry gives, over `look`.
	private static ActionLook Merge(ActionLook look, JsonNode? node, string entry, HashSet<string> effects, Action<string, string> problem)
	{
		if (node is not JsonObject o)
		{
			problem(entry, "must be an object");
			return look;
		}
		foreach ((string field, JsonNode? value) in o)
		{
			string text = value is JsonValue v && v.TryGetValue(out string? s) ? s : "";
			switch (field)
			{
			case "animation" when Animations.Contains(text):
				look = look with { Animation = text };
				break;
			case "animation":
				problem(entry, $"unknown animation '{text}': {string.Join(", ", Animations)}");
				break;
			case "effect" when effects.Contains(text):
				look = look with { Effect = text };
				break;
			case "effect":
				problem(entry, $"unknown effect '{text}': not a generated effect, an effect in assets.json or none");
				break;
			case "cue":
				look = look with { Cue = text };
				break;
			case "animate_ms" when Number(value, 0, 10000) is { } ms:
				look = look with { AnimateMs = ms };
				break;
			case "effect_ms" when Number(value, 0, 10000) is { } ms:
				look = look with { EffectMs = ms };
				break;
			case "shake" when Number(value, 0, 32) is { } px:
				look = look with { Shake = px };
				break;
			case "flash" when value is JsonValue f && f.TryGetValue(out bool flash):
				look = look with { Flash = flash };
				break;
			case "animate_ms" or "effect_ms" or "shake" or "flash":
				problem(entry, $"bad value for '{field}': milliseconds from 0 to 10000, a shake from 0 to 32 pixels, flash true or false");
				break;
			default:
				problem(entry, $"unknown field '{field}'");
				break;
			}
		}
		return look;
	}

	// The whole table as JSON, for the placeholder export: every step length and fallback, and the actions given.
	public void Write(string path)
	{
		static JsonObject Look(ActionLook l) => new()
		{
			["animation"] = l.Animation, ["effect"] = l.Effect, ["cue"] = l.Cue, ["animate_ms"] = l.AnimateMs, ["effect_ms"] = l.EffectMs, ["shake"] = l.Shake, ["flash"] = l.Flash,
		};
		var root = new JsonObject
		{
			["layout"] = NameOf(Layout),
			["layouts"] = new JsonObject(Layouts.OrderBy(l => l.Key, StringComparer.Ordinal).Select(l => KeyValuePair.Create(l.Key, (JsonNode?)NameOf(l.Value)))),
			["timings"] = new JsonObject(Timings.Select(t => KeyValuePair.Create(t.Key, (JsonNode?)t.Value))),
			["fallbacks"] = new JsonObject(Fallbacks.OrderBy(f => f.Key, StringComparer.Ordinal).Select(f => KeyValuePair.Create(f.Key, (JsonNode?)Look(f.Value)))),
			["actions"] = new JsonObject(Actions.OrderBy(a => a.Key, StringComparer.Ordinal).Select(a => KeyValuePair.Create(a.Key, (JsonNode?)Look(a.Value)))),
			["changes"] = new JsonObject(ChangeLooks.OrderBy(c => c.Key, StringComparer.Ordinal).Select(c => KeyValuePair.Create(c.Key, (JsonNode?)Look(c.Value)))),
		};
		File.WriteAllText(path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true, IndentCharacter = '\t', IndentSize = 1 }) + "\n");
	}
}
