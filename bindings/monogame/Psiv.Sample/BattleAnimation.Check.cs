using System.Globalization;
using System.Text.Json.Nodes;

namespace Psiv.Sample;

public sealed partial class BattleAnimation
{
	// Checks a file's entries and keeps the ones without a problem. Each check returns the reason, or null.
	// The fields each track type, number style, figure state, motion and color track takes, as the checker reads them.
	public static Dictionary<string, IEnumerable<string>> FieldTables(AnimCheckContext ctx) => new Checker(ctx, Builtin(), (_, _, _) => { }).Tables();

	private sealed class Checker(AnimCheckContext ctx, BattleAnimation set, Action<string, string, string> problem)
	{
		private delegate string? Check(JsonNode? node);

		public Dictionary<string, IEnumerable<string>> Tables()
		{
			var tables = TrackTypes.ToDictionary(t => "track." + t, t => (IEnumerable<string>)TrackFields(t).Keys);
			tables["numbers"] = NumberFields().Keys;
			tables["states"] = StateFields().Keys;
			tables["move"] = MoveFields().Keys;
			tables["color"] = ColorTrackFields().Keys;
			tables["settings"] = SettingsFields().Keys;
			tables["-"] = Sections;
			return tables;
		}

		public string Display = "";
		private readonly HashSet<string> _scripts = new(set.Scripts.Keys, StringComparer.Ordinal);
		private readonly HashSet<string> _fragments = new(set.Fragments.Keys, StringComparer.Ordinal);
		private readonly HashSet<string> _effects = new(set.Effects.Keys, StringComparer.Ordinal);
		private readonly HashSet<string> _transitions = new(set.Transitions.Keys, StringComparer.Ordinal);
		private readonly Dictionary<string, JsonObject> _allScripts = new(set.Scripts, StringComparer.Ordinal);
		private readonly Dictionary<string, JsonObject> _allFragments = new(set.Fragments, StringComparer.Ordinal);
		private readonly HashSet<string> _classes = new(StringComparer.Ordinal);
		private readonly HashSet<string> _fromFiles = new(StringComparer.Ordinal);
		private bool _problems;

		private void Problem(string entry, string reason)
		{
			_problems = true;
			problem(Display, entry, reason);
		}

		public void Declare(List<(JsonObject Root, string Display)> files)
		{
			foreach ((JsonObject r, _) in files)
			{
				void Ids(string section, HashSet<string> into)
				{
					if (r[section] is JsonObject o)
					{
						into.UnionWith(o.Select(p => p.Key));
					}
				}
				Ids("scripts", _scripts);
				Ids("fragments", _fragments);
				Ids("effects", _effects);
				Ids("transitions", _transitions);
				if (r["scripts"] is JsonObject scripts)
				{
					foreach ((string id, JsonNode? s) in scripts)
					{
						if (s is JsonObject o && _fromFiles.Add(id))
						{
							_allScripts[id] = o;
						}
					}
				}
				if (r["fragments"] is JsonObject fragments)
				{
					foreach ((string id, JsonNode? f) in fragments)
					{
						if (f is JsonObject o)
						{
							_allFragments[id] = o;
						}
					}
				}
				if (r["weapon_classes"] is JsonObject classes)
				{
					_classes.UnionWith(classes.Where(c => c.Key.Split(':', 2) is ["equipment", var piece] ? ctx.Data.AllEquipment.ContainsKey(piece)
						: c.Key.Split(':', 2) is ["category", var cat] && ctx.Data.AllEquipment.ContainsValue(cat))
						.Select(c => c.Value is JsonValue v && v.TryGetValue(out string? s) ? s : "").Where(s => s.Length > 0));
				}
			}
			_classes.UnionWith(ctx.Data.EquipmentCategories.Values);
		}

		// The root with only the entries that passed.
		public JsonObject Clean(JsonObject root)
		{
			var clean = new JsonObject();
			if (root["settings"] is JsonNode settings)
			{
				var s = new JsonObject();
				Fields(settings, "settings", SettingsFields(), s);
				clean["settings"] = s;
			}
			Entries(root, "scripts", clean, CheckScript);
			Entries(root, "fragments", clean, CheckFragment);
			Entries(root, "effects", clean, (id, o) => Fields(o, "effects." + id, EffectFields(requireSheet: true), null) && Required(o, "effects." + id, "sheet"));
			Entries(root, "transitions", clean, CheckTransition);
			Entries(root, "numbers", clean, (id, o) => NumberKinds.Contains(id) ? Fields(o, "numbers." + id, NumberFields(), null)
				: Fail($"numbers.{id}", $"unknown result kind '{id}': {string.Join(", ", NumberKinds)}"));
			if (root["select"] is JsonNode select)
			{
				if (select is not JsonObject keys)
				{
					Problem("select", "must be an object of keys to script ids");
				}
				else
				{
					var into = new JsonObject();
					foreach ((string key, JsonNode? value) in keys)
					{
						string entry = "select." + key;
						string? id = value is JsonValue v && v.TryGetValue(out string? text) ? text : null;
						if (KeyProblem(key) is { } reason)
						{
							Problem(entry, reason);
						}
						else if (id is null)
						{
							Problem(entry, "must be a script id");
						}
						else if (!_scripts.Contains(id))
						{
							Problem(entry, $"unknown script '{id}'");
						}
						else
						{
							into[key] = id;
						}
					}
					clean["select"] = into;
				}
			}
			if (root["figures"] is JsonNode figures)
			{
				var into = new JsonObject();
				if (figures is not JsonObject scopes)
				{
					Problem("figures", "must be an object of scopes");
				}
				else
				{
					foreach ((string scope, JsonNode? node) in scopes)
					{
						string entry = "figures." + scope;
						if (ScopeProblem(scope) is { } reason)
						{
							Problem(entry, reason);
							continue;
						}
						if (node is not JsonObject f || f["states"] is not JsonObject states || f.Count != 1)
						{
							Problem(entry, "must be {\"states\": {...}}");
							continue;
						}
						var kept = new JsonObject();
						foreach ((string name, JsonNode? state) in states)
						{
							_problems = false;
							if (state is not JsonObject so)
							{
								Problem($"{entry}.states.{name}", "must be an object");
								continue;
							}
							if (!AssetManifest.IsPoseName(name.Split(':')[0]) && name.Split(':')[0] != "idle")
							{
								Problem($"{entry}.states.{name}", "a state name is lower-case letters, digits and '_', with an optional :VARIANT");
								continue;
							}
							Fields(so, $"{entry}.states.{name}", StateFields(), null);
							if (!_problems)
							{
								kept[name] = so.DeepClone();
							}
						}
						into[scope] = new JsonObject { ["states"] = kept };
					}
				}
				clean["figures"] = into;
			}
			if (root["weapon_classes"] is JsonNode classes)
			{
				var into = new JsonObject();
				if (classes is not JsonObject o)
				{
					Problem("weapon_classes", "must be an object of equipment:ID or category:NAME to a class name");
				}
				else
				{
					foreach ((string key, JsonNode? value) in o)
					{
						string[] parts = key.Split(':', 2);
						bool known = parts is ["equipment", var piece] ? ctx.Data.AllEquipment.ContainsKey(piece) : parts is ["category", var cat] && ctx.Data.AllEquipment.ContainsValue(cat);
						if (!known)
						{
							Problem($"weapon_classes.{key}", "unknown key: equipment:ID or category:NAME, with an equipment piece or category of the game data");
						}
						else if (value is not JsonValue v || !v.TryGetValue(out string? c) || !IsId(c))
						{
							Problem($"weapon_classes.{key}", "must be a class name: lower-case letters, digits, '_' and '.'");
						}
						else
						{
							into[key] = c;
						}
					}
				}
				clean["weapon_classes"] = into;
			}
			if (root["placements"] is JsonNode placements)
			{
				var into = new JsonObject();
				if (placements is not JsonObject o)
				{
					Problem("placements", "must be an object of count:N or encounter:ID to places");
				}
				else
				{
					foreach ((string key, JsonNode? value) in o)
					{
						string[] parts = key.Split(':', 2);
						bool known = parts is ["count", var n] ? n is "1" or "2" or "3" or "4" or "5" : parts is ["encounter", var enc] && ctx.Data.Encounters.Contains(enc);
						string? reason = known ? L(PlaceList)(value) : "unknown key: count:1 to count:5 (5 is five or more enemies) or encounter:ID";
						if (reason is not null)
						{
							Problem($"placements.{key}", reason);
						}
						else
						{
							into[key] = value!.DeepClone();
						}
					}
				}
				clean["placements"] = into;
			}
			if (root["reduced_flash"] is JsonNode reduced)
			{
				var into = new JsonObject();
				var fields = new Dictionary<string, Check> { ["damp"] = Num(0, 1) };
				foreach (string type in TrackTypes)
				{
					fields[type] = Enum("keep", "drop", "damp");
				}
				Fields(reduced, "reduced_flash", fields, into);
				clean["reduced_flash"] = into;
			}
			return clean;
		}

		private bool Fail(string entry, string reason)
		{
			Problem(entry, reason);
			return false;
		}

		private static bool Required(JsonObject o, string entry, string field) => o.ContainsKey(field);

		private void Entries(JsonObject root, string section, JsonObject clean, Func<string, JsonObject, bool> check)
		{
			if (root[section] is not JsonNode node)
			{
				return;
			}
			var into = new JsonObject();
			if (node is not JsonObject entries)
			{
				Problem(section, "must be an object of ids");
				clean[section] = into;
				return;
			}
			foreach ((string id, JsonNode? value) in entries)
			{
				_problems = false;
				string entry = $"{section}.{id}";
				if (!IsId(id))
				{
					Problem(entry, "an id is lower-case letters, digits, '_' and '.', 1 to 64 characters");
					continue;
				}
				if (value is not JsonObject o)
				{
					Problem(entry, "must be an object");
					continue;
				}
				bool ok = check(id, o);
				if (ok && !_problems)
				{
					into[id] = o.DeepClone();
				}
			}
			clean[section] = into;
		}

		// Checks the fields of an object against `fields`; unknown fields are problems. Copies the good ones into `into`.
		private bool Fields(JsonNode? node, string entry, Dictionary<string, Check> fields, JsonObject? into)
		{
			if (node is not JsonObject o)
			{
				Problem(entry, "must be an object");
				return false;
			}
			bool ok = true;
			foreach ((string name, JsonNode? value) in o)
			{
				if (!fields.TryGetValue(name, out Check? check))
				{
					Problem(entry, $"unknown field '{name}'");
					ok = false;
					continue;
				}
				if (check(value) is { } reason)
				{
					Problem($"{entry}.{name}", reason);
					ok = false;
					continue;
				}
				if (into is not null)
				{
					into[name] = value?.DeepClone();
				}
			}
			return ok;
		}

		// A value field may hold one value per stage layout.
		private static Check L(Check c) => n => n is JsonObject o && o.Count == 2 && o.ContainsKey("front") && o.ContainsKey("side") ? c(o["front"]) ?? c(o["side"]) : c(n);

		private static string? Text(JsonNode? n) => n is JsonValue v && v.TryGetValue(out string? s) ? s : null;

		private static double? Number(JsonNode? n) => n is JsonValue v && v.TryGetValue(out double d) ? d : null;

		private static bool IsParam(string? s) => s is not null && s.StartsWith("$param.", StringComparison.Ordinal) && s.Length > 7;

		private static Check Num(double min, double max) => L(n => Number(n) is double d && d >= min && d <= max || IsParam(Text(n)) ? null : $"must be a number from {Fmt(min)} to {Fmt(max)}");

		private static Check Int(int min, int max) => L(n => n is JsonValue v && v.TryGetValue(out int i) && i >= min && i <= max || IsParam(Text(n)) ? null : $"must be a whole number from {min} to {max}");

		private static Check Bool() => L(n => n is JsonValue v && v.TryGetValue(out bool _) || IsParam(Text(n)) ? null : "must be true or false");

		private static Check Enum(params string[] values) => L(n => Text(n) is { } s && (values.Contains(s) || IsParam(s)) ? null : $"must be one of {string.Join(", ", values)}");

		private static Check Name() => L(n => Text(n) is { Length: > 0 } s && (IsId(s) || s.StartsWith('$')) ? null : "must be a name");

		private static Check Any() => _ => null;

		private static string Fmt(double d) => d.ToString(CultureInfo.InvariantCulture);

		private static string? TimeProblem(JsonNode? n, bool relative)
		{
			const string Reason = "must be frames from 0 to 36000, \"Nms\", \"N%\", or a $timing, $look, $param, $step, $phase or $rest reference";
			if (n is JsonValue v && v.TryGetValue(out int frames))
			{
				return frames is >= 0 and <= MaxFrames ? null : Reason;
			}
			string? s = Text(n);
			if (s is null)
			{
				return Reason;
			}
			if (MsPattern().IsMatch(s))
			{
				return int.TryParse(s[..^2], CultureInfo.InvariantCulture, out int ms) && ms >= 0 && ms <= MaxFrames * 1000 / 60 ? null : Reason;
			}
			if (PercentPattern().IsMatch(s))
			{
				return relative && double.Parse(s[..^1], CultureInfo.InvariantCulture) is >= 0 and <= 100 ? null : Reason;
			}
			if (s.StartsWith("$timing.", StringComparison.Ordinal))
			{
				return BattlePresentation.Steps.Contains(s[8..]) ? null : $"unknown step '{s[8..]}': {string.Join(", ", BattlePresentation.Steps)}";
			}
			return s is "$look.animate_ms" or "$look.effect_ms" or "$step" || IsParam(s) || relative && s is "$phase" or "$rest" ? null : Reason;
		}

		private static Check Time() => L(n => TimeProblem(n, relative: true));

		// A start: a time from the script's start, or a phase, mark or `end` with an offset. Names are checked per script.
		private static Check At() => L(n =>
		{
			if (TimeProblem(n, relative: false) is null)
			{
				return null;
			}
			string? s = Text(n);
			if (s is null)
			{
				return "must be a time, or a phase or mark name with an optional +N or -N";
			}
			int sign = s.IndexOfAny(['+', '-']);
			string offset = sign < 0 ? "" : s[(sign + 1)..];
			return IsId(sign < 0 ? s : s[..sign]) && (offset.Length == 0 || TimeProblem(offset.EndsWith("ms", StringComparison.Ordinal) ? offset : (JsonNode?)(int.TryParse(offset, out int f) ? f : -1), false) is null)
				? null : "must be a time, or a phase or mark name with an optional +N or -N";
		});

		private static string? DistProblem(JsonNode? n) => Number(n) is double d ? (d is >= -4000 and <= 4000 ? null : "must be pixels from -4000 to 4000")
			: Text(n) is { } s && (DistPattern().IsMatch(s) || IsParam(s)) ? null : "must be pixels, \"Npx\" (never scaled), \"Nm\" (scaled by motion_percent), \"Nf\" (forward units), \"Nh\" or \"Nw\" (of the figure's height or width)";

		private static Check Dist() => L(DistProblem);

		private static string? CuesOffProblem(JsonNode? n) => n is JsonValue v && v.TryGetValue(out bool _) || n is JsonArray a && a.All(x => Text(x) is { } c && EventCues.Contains(c))
			? null : $"must be true, false or a list of event cues: {string.Join(", ", EventCues)}";

		// Logical pixels never scaled: a number or "Npx".
		private static Check Px(double min, double max) => L(n => (Number(n) ?? (Text(n) is { } s && s.EndsWith("px", StringComparison.Ordinal)
			&& double.TryParse(s[..^2], CultureInfo.InvariantCulture, out double v) ? v : null)) is double d && d >= min && d <= max || IsParam(Text(n)) ? null
			: $"must be logical pixels from {Fmt(min)} to {Fmt(max)}, a number or \"Npx\"");

		private static Check DistPair() => L(n => n is JsonArray a && a.Count == 2 ? DistProblem(a[0]) ?? DistProblem(a[1]) : "must be [dx, dy]");

		private static string? PlaceList(JsonNode? n) => n is JsonArray a && a.Count is >= 1 and <= 8 && a.All(p => p is JsonArray xy && xy.Count == 2 && xy.All(v => Number(v) is not null))
			? null : "must be a list of up to 8 places, each [x, foot] in logical pixels";

		private static string? SubjectProblem(string s)
		{
			if (SubjectNames.Contains(s))
			{
				return null;
			}
			if (s.Split(':', 3) is [var kind and ("beside" or "adjacent"), var side, var of])
			{
				return side is not ("left" or "right") ? $"{kind}:SIDE:SUBJECT takes left or right"
					: of.StartsWith("beside:", StringComparison.Ordinal) || of.StartsWith("adjacent:", StringComparison.Ordinal) ? $"{kind}: the subject beside which is not itself beside another"
					: SubjectProblem(of);
			}
			string[] parts = s.Split(':');
			if (parts.Length == 2 && int.TryParse(parts[1], CultureInfo.InvariantCulture, out int n))
			{
				return parts[0] switch
				{
					"actor" => n is >= 0 and <= 3 ? null : "actor:N takes 0 to 3",
					"target" or "place" => n is >= 0 and <= 7 ? null : $"{parts[0]}:N takes 0 to 7",
					"slot" => n is >= 0 and <= 4 ? null : "slot:N takes 0 to 4",
					_ => $"unknown subject '{s}'",
				};
			}
			return $"unknown subject '{s}': {string.Join(", ", SubjectNames)}, actor:N, target:N, place:N, slot:N, beside:SIDE:SUBJECT or adjacent:SIDE:SUBJECT";
		}

		private static Check Subject() => n => Text(n) is { } s ? SubjectProblem(s) : "must be a subject";

		public static string? PointProblem(JsonNode? n)
		{
			if (n is JsonObject o)
			{
				return o.Any(p => p.Key is not ("point" or "offset")) ? "a point object has only 'point' and 'offset'"
					: PointProblem(o["point"]) ?? (o["offset"] is null ? null : DistPair()(o["offset"]));
			}
			string? s = Text(n);
			if (s is null)
			{
				return "must be a point";
			}
			if (s is "stage:center" or "enemies:center" or "party:center" or "row:front" or "row:back")
			{
				return null;
			}
			if (s.StartsWith("screen:", StringComparison.Ordinal))
			{
				string[] xy = s[7..].Split(',');
				return xy.Length == 2 && xy.All(c => double.TryParse(c.TrimEnd('%'), CultureInfo.InvariantCulture, out _)) ? null : "screen:X,Y in pixels or percent";
			}
			int last = s.LastIndexOf(':');
			if (last > 0 && Anchors.Contains(s[(last + 1)..]))
			{
				return SubjectProblem(s[..last]);
			}
			return SubjectProblem(s);
		}

		private static Check Point() => L(PointProblem);

		private string? ColorProblem(JsonNode? n)
		{
			string? s = Text(n);
			return s is not null && (UiLayoutColor(s) || ctx.ColorNames.Contains(s) || s == "$look.element_color" || IsParam(s)) ? null
				: $"unknown color '{s ?? n?.ToJsonString()}': a styles.json color name or #rrggbb";
		}

		private static bool UiLayoutColor(string s) => s.StartsWith('#') && (s.Length == 7 || s.Length == 9) && s[1..].All(Uri.IsHexDigit);

		private Check Color() => L(ColorProblem);

		private string? CtProblem(JsonNode? n)
		{
			if (n is not JsonObject o)
			{
				return "must be a color transform: {multiply, add, alpha, tint, amount, palette}";
			}
			foreach ((string name, JsonNode? v) in o)
			{
				string? reason = name switch
				{
					"multiply" => v is JsonArray rgb ? rgb.Count == 3 && rgb.All(c => Number(c) is >= 0 and <= 1) ? null : "must be a color or [r, g, b] from 0 to 1" : ColorProblem(v),
					"add" or "tint" => ColorProblem(v),
					"alpha" or "amount" => Num(0, 1)(v),
					"palette" => v is JsonArray map && map.Count is >= 1 and <= 64 && map.All(p => p is JsonArray pair && pair.Count == 2 && pair.All(c => Text(c) is { } h && UiLayoutColor(h)))
						? null : "must be a list of up to 64 [\"#rrggbb\", \"#rrggbb\"] pairs: source color, replacement",
					_ => $"unknown field '{name}'",
				};
				if (reason is not null)
				{
					return $"{name}: {reason}";
				}
			}
			return null;
		}

		private Check Ct() => L(CtProblem);

		private static Check Ease() => Enum(Eases);

		private string? ConditionProblem(JsonNode? n)
		{
			IEnumerable<JsonNode?> list = n is JsonArray a ? (IEnumerable<JsonNode?>)a : [n];
			foreach (JsonNode? c in list)
			{
				string? s = Text(c)?.TrimStart('!');
				if (s is null || !(Conditions.Contains(s) || s.StartsWith("element:", StringComparison.Ordinal) && BattlePresentation.Elements.Contains(s[8..])
					|| s is "$look.flash" || IsParam(s)))
				{
					return $"unknown condition '{s}': {string.Join(", ", Conditions)}, element:ID, $look.flash or a $param";
				}
			}
			return null;
		}

		private Dictionary<string, Check> Common() => new()
		{
			["type"] = Any(), ["id"] = Name(), ["at"] = At(), ["length"] = Time(), ["if"] = ConditionProblem, ["layout"] = Enum("front", "side"),
			["stagger"] = L(n => Text(n) == "sweep" ? null : TimeProblem(n, false)), ["order"] = Enum("event", "left_to_right", "right_to_left", "near_first", "far_first"),
			["reduced"] = n => n is JsonObject ? null : Enum("keep", "drop", "damp")(n), ["flash"] = Bool(),
		};

		private Dictionary<string, Check> EffectFields(bool requireSheet)
		{
			var f = new Dictionary<string, Check>
			{
				["frames"] = L(n => n is JsonArray a && a.Count is >= 1 and <= 256 && a.All(x => x is JsonValue v && v.TryGetValue(out int i) && i >= -1) ? null : "must be a list of frame indexes, -1 drawing nothing"),
				["durations"] = L(n => n is JsonArray a ? a.Count is >= 1 and <= 256 && a.All(x => TimeProblem(x, false) is null) ? null : "must be a time or a list of times" : TimeProblem(n, false)),
				["loops"] = Int(-1, 1000), ["pingpong"] = Bool(), ["hold_last"] = Time(), ["anchor"] = Enum(Anchors), ["origin"] = L(n => n is JsonArray a && a.Count == 2 && a.All(x => Number(x) is >= 0 and <= 1) ? null : "must be [x, y] fractions of the frame"),
				["offset"] = DistPair(), ["scale"] = L(n => Number(n) is > 0 and <= 64 || Text(n) is "layout" or "large" || Text(n) is { } s && s.StartsWith('x') && double.TryParse(s[1..], CultureInfo.InvariantCulture, out double x) && x is > 0 and <= 16 ? null : "must be layout, large, a factor or \"xN\""),
				["blend"] = Enum("alpha", "additive"), ["color"] = L(n => n is JsonObject o && (o.ContainsKey("from") || o.ContainsKey("keys")) ? null : CtProblem(n)),
				["directions"] = L(n => n is JsonObject o && o.All(p => p.Key is "left" or "right" or "up" or "down" && p.Value is JsonArray) ? null : "must be frame lists by left, right, up and down"),
				["flip"] = Enum("auto", "none", "x"), ["travel"] = TravelProblem, ["impact"] = Name(), ["fade_out"] = Time(),
				["path"] = L(n => n is JsonArray a && a.Count is >= 1 and <= 64 && a.All(k => k is JsonArray key && key.Count == 3 && TimeProblem(key[0], false) is null && DistProblem(key[1]) is null && DistProblem(key[2]) is null)
					? null : "must be a list of [frame, dx, dy] keys"),
			};
			if (requireSheet)
			{
				f["sheet"] = n => Text(n) is { } s && ctx.EffectFrames.ContainsKey(s) ? null : $"unknown effect sheet '{Text(n)}': an effect in assets.json or a generated effect";
			}
			return f;
		}

		private string? TravelProblem(JsonNode? n)
		{
			if (n is not JsonObject o)
			{
				return "must be an object";
			}
			foreach ((string name, JsonNode? v) in o)
			{
				string? reason = name switch
				{
					"from" or "to" => PointProblem(v),
					"frames" => TimeProblem(v, false),
					"path" => Enum("line", "arc", "wave")(v),
					"height" => DistProblem(v),
					"waves" => Int(1, 16)(v),
					"ease" => Ease()(v),
					"trail" => v is JsonObject t && t.All(p => p.Key is "count" or "gap" or "alpha") ? null : "must be {count, gap, alpha}",
					_ => $"unknown field '{name}'",
				};
				if (reason is not null)
				{
					return $"{name}: {reason}";
				}
			}
			return o.ContainsKey("frames") ? null : "needs frames";
		}

		private Dictionary<string, Check> SettingsFields() => new()
		{
			["rate"] = Num(1, 240), ["log_timing"] = Enum("step", "release"), ["event_cues_off"] = CuesOffProblem,
			["flash_limit"] = n => n is JsonValue v && v.TryGetValue(out bool b) && !b || n is JsonObject o && o.All(p => p.Key is "count" or "frames")
				&& Int(1, 60)(o["count"] ?? 3) is null && Int(1, 3600)(o["frames"] ?? 60) is null ? null : "must be false or {count: 1 to 60, frames: 1 to 3600}",
		};

		private Dictionary<string, Check> MoveFields() => new()
		{
			["kind"] = Enum("forward", "home", "to", "arc", "hop", "offset", "enter", "exit", "sine", "shake"), ["distance"] = Dist(), ["height"] = Dist(), ["stop_short"] = Dist(),
			["to"] = Point(), ["reach"] = Num(0, 1), ["by"] = DistPair(), ["space"] = Enum("screen", "forward"), ["ease"] = Ease(), ["length"] = Time(), ["speed"] = Num(0.01, 100),
			["axis"] = Enum("both", "x", "y", "forward"), ["amplitude"] = Dist(), ["period"] = Time(), ["phase"] = Time(), ["frequency"] = Num(0, 1000), ["decay"] = Enum("linear", "none", "out"),
		};

		private Dictionary<string, Check> ColorTrackFields() => new()
		{
			["mode"] = Enum("set", "flash", "tint", "fade", "cycle", "keys"), ["to"] = Ct(), ["colors"] = L(n => n is JsonArray a && a.Count is >= 1 and <= 32 ? a.Select(CtProblem).FirstOrDefault(r => r is not null) : "must be a list of color transforms"),
			["period"] = Time(), ["in"] = Time(), ["hold"] = Time(), ["out"] = Time(), ["ease"] = Ease(), ["smooth"] = Bool(), ["length"] = Time(),
			["keys"] = L(n => n is JsonArray a && a.Count is >= 1 and <= 64 && a.All(k => k is JsonArray key && key.Count == 2 && TimeProblem(key[0], true) is null && CtProblem(key[1]) is null)
				? null : "must be a list of [time, color transform] keys"),
		};

		private Dictionary<string, Check> StateFields() => new()
		{
			["pose"] = Name(), ["loop"] = Bool(), ["length"] = Time(), ["kind"] = Enum(DefeatKinds), ["bands"] = Int(2, 32), ["period"] = Time(), ["by"] = DistPair(), ["delay"] = Time(),
			["flash"] = Time(), ["ease"] = Ease(), ["previous"] = Enum("crossfade"), ["outlast"] = Time(),
			["motion"] = n => Fields(n, "motion", MoveFields(), null) ? null : "has a problem",
			["color"] = n => n is JsonObject o && o.ContainsKey("mode") ? Fields(n, "color", ColorTrackFields(), null) ? null : "has a problem" : CtProblem(n),
			["opacity"] = n => n is JsonObject o && o.All(p => p.Key is "from" or "to" or "speed" or "ease") ? null : "must be {from, to, speed, ease}",
		};

		private Dictionary<string, Check> NumberFields() => new()
		{
			["color"] = Color(), ["size"] = L(n => Text(n) == "layout" ? null : Int(6, 200)(n)), ["motion"] = Enum("rise", "pop", "bounce", "float", "none", "window"),
			["rise"] = L(n => Text(n) == "layout" ? null : Px(-4000, 4000)(n)), ["rise_time"] = Time(), ["pop"] = Num(0.1, 8), ["pop_time"] = Time(), ["bounces"] = Int(0, 8),
			["life"] = Time(), ["fade"] = Time(), ["delay"] = Time(), ["stack"] = Px(0, 200), ["from"] = Enum("top"), ["row"] = Px(0, 4000), ["open"] = Time(), ["pad"] = Px(0, 64),
			["shadow"] = n => n is JsonValue v && v.TryGetValue(out bool b) && !b || n is JsonArray a && a.Count == 3 && Number(a[0]) is not null && Number(a[1]) is not null && ColorProblem(a[2]) is null
				? null : "must be false or [dx, dy, color]",
		};

		private Dictionary<string, Check> TrackFields(string type)
		{
			Dictionary<string, Check> f = Common();
			void Add(Dictionary<string, Check> more)
			{
				foreach ((string k, Check c) in more)
				{
					f[k] = c;
				}
			}
			switch (type)
			{
			case "pose":
				Add(new() { ["on"] = Subject(), ["pose"] = Name(), ["frame"] = Int(0, 4096), ["loop"] = Bool(), ["place_of"] = Subject(), ["offset"] = DistPair() });
				break;
			case "state":
				Add(new() { ["on"] = Subject(), ["state"] = Name() });
				break;
			case "move":
				Add(MoveFields());
				f["on"] = Subject();
				break;
			case "sprite":
			case "react":
				Add(EffectFields(requireSheet: false));
				Add(new()
				{
					["effect"] = Name(), ["anchor"] = Enum(Anchors), ["offsets"] = L(n => n is JsonArray a && a.Count is >= 1 and <= 64 ? a.Select(x => DistPair()(x)).FirstOrDefault(r => r is not null) : "must be a list of [dx, dy]"),
					["jitter"] = DistPair(), ["layer"] = Enum(Layers), ["column_y"] = Int(0, 4000),
					["spawn"] = n => Text(n) is "all_targets" or "columns" ? null : PointProblem(n),
				});
				if (type == "react")
				{
					Add(new() { ["targets"] = Subject(), ["pose"] = Name(), ["state"] = Name() });
					f.Remove("spawn");
				}
				break;
			case "flash":
				Add(new() { ["color"] = Color(), ["amount"] = Num(0, 1), ["area"] = Enum("stage", "frame") });
				break;
			case "shake":
				Add(new()
				{
					["amplitude"] = L(n => Text(n) == "$look.shake" ? null : Px(0, 32)(n)), ["decay"] = Enum("linear", "none", "out"), ["axis"] = Enum("both", "x", "y"), ["pattern"] = Enum("clock", "script"),
					["offsets"] = n => n is JsonObject o && o.Count > 0 && o.All(p => p.Key is "backdrop" or "figures" or "screen" && p.Value is JsonArray list && list.Count is >= 1 and <= 600
						&& list.All(xy => xy is JsonArray pair && pair.Count == 2 && pair.All(v => Number(v) is >= -64 and <= 64))) ? null : "must be per-frame [dx, dy] lists for backdrop, figures and screen",
				});
				break;
			case "color":
				Add(ColorTrackFields());
				f["on"] = n => Text(n) is "backdrop" or "stage" or "frame" ? null : Subject()(n);
				break;
			case "backdrop":
				Add(new()
				{
					["effect"] = Enum("scroll", "wave", "offset", "swap", "restore", "darken", "tint"), ["speed"] = L(n => n is JsonArray a && a.Count == 2 && a.All(v => Number(v) is >= -64 and <= 64) ? null : "must be [x, y] pixels a frame"),
					["amplitude"] = Num(0, 64), ["wavelength"] = Num(2, 4000), ["period"] = Time(), ["band"] = Int(2, 32), ["by"] = DistPair(), ["image"] = n => Text(n) is { } file && ctx.Directory is not null
						&& File.Exists(Path.Combine(ctx.Directory, file)) && file.EndsWith(".png", StringComparison.OrdinalIgnoreCase) ? null : "missing file: a PNG in the asset directory",
					["key"] = n => Text(n) is { } k && ctx.BackdropKeys.Contains(k) ? null : "unknown backdrop key: kind:field, area:ID, encounter:ID, terrain:water, generated ...",
					["to"] = Ct(), ["loop"] = Bool(), ["persist"] = Bool(), ["ease"] = Ease(),
				});
				break;
			case "sound":
				Add(new() { ["cue"] = L(n => Text(n) is { Length: > 0 } ? null : "must be a cue id"), ["per"] = Enum("once", "each", "release"), ["spawn"] = Subject() });
				break;
			case "release":
				Add(new() { ["targets"] = Subject(), ["unit"] = Enum("all", "target", "hit") });
				f["at"] = n => n is JsonArray a ? a.Count is >= 1 and <= 64 ? a.Select(x => At()(x)).FirstOrDefault(r => r is not null) : "must be a start or a list of starts" : At()(n);
				break;
			case "call":
				Add(new()
				{
					["fragment"] = n => Text(n) is { } id && _fragments.Contains(id) ? null : $"unknown fragment '{Text(n)}'",
					["bind"] = n => n is JsonObject o && o.All(p => SubjectProblem(p.Key) is null && Text(p.Value) is { } s && SubjectProblem(s) is null) ? null : "must map subjects to subjects",
					["params"] = n => n is JsonObject ? null : "must be an object of parameter values",
				});
				break;
			case "transition":
				Add(new() { ["transition"] = n => Text(n) is { } id && _transitions.Contains(id) ? null : $"unknown transition '{Text(n)}'" });
				break;
			case "name_window":
				Add(new() { ["fade"] = Time() });
				break;
			}
			return f;
		}

		private static string KindOf(JsonObject script, Dictionary<string, JsonObject> all, int depth = 0)
		{
			if (script["phases"] is JsonObject phases && phases.Count > 0)
			{
				string first = phases.First().Key;
				return TurnPhases.Contains(first) ? "turn" : first == "step" ? "step" : first == "change" ? "change" : BattlePhases.Contains(first) ? "battle" : "?";
			}
			if (script["extends"] is JsonValue e && e.TryGetValue(out string? parent) && all.TryGetValue(parent, out JsonObject? p) && depth < 8)
			{
				return KindOf(p, all, depth + 1);
			}
			return script.ContainsKey("length") ? "reaction" : "turn";
		}

		private static string[] PhasesOf(string kind) => kind switch
		{
			"turn" => TurnPhases,
			"step" => ["step"],
			"change" => ["change"],
			"battle" => BattlePhases,
			_ => [],
		};

		private bool CheckScript(string id, JsonObject s)
		{
			string entry = "scripts." + id;
			Fields(s, entry, new()
			{
				["extends"] = n => Text(n) is { } p && _scripts.Contains(p) ? null : $"unknown script '{Text(n)}'",
				["params"] = n => n is JsonObject o && o.All(p => IsId(p.Key) && p.Value is JsonValue) ? null : "must be an object of names to strings, numbers or booleans",
				["phases"] = n => n is JsonObject ? null : "must be an object of phase lengths",
				["marks"] = n => n is JsonObject ? null : "must be an object of names to starts",
				["length"] = Time(), ["tracks"] = n => n is JsonArray ? null : "must be a list of tracks",
				["remove"] = n => n is JsonArray a && a.All(x => Text(x) is not null) ? null : "must be a list of track ids",
				["event_cues_off"] = CuesOffProblem,
			}, null);
			string kind = KindOf(s, _allScripts);
			// The chain: a cycle or more than 8 parents is a problem.
			var seen = new List<string> { id };
			JsonObject cur = s;
			while (cur["extends"] is JsonValue ev && ev.TryGetValue(out string? parent))
			{
				if (seen.Contains(parent))
				{
					Problem(entry + ".extends", $"'{seen[^1]}' extends '{parent}': a cycle");
					break;
				}
				if (seen.Count > 8)
				{
					Problem(entry + ".extends", "extends nests deeper than 8");
					break;
				}
				seen.Add(parent);
				if (!_allScripts.TryGetValue(parent, out JsonObject? p))
				{
					break;
				}
				if (KindOf(p, _allScripts) != kind)
				{
					Problem(entry + ".extends", $"'{parent}' is a {KindOf(p, _allScripts)} script; this one is a {kind} script");
				}
				cur = p;
			}
			string[] phases = PhasesOf(kind);
			if (kind == "?")
			{
				Problem(entry + ".phases", "unknown phases: a turn has forward, banner, animate, effect, result and back; a step script step; a change script change; a battle script cover, hold and reveal");
			}
			long known = 0;
			bool allKnown = true;
			var starts = new Dictionary<string, long>();
			if (s["phases"] is JsonObject ph)
			{
				foreach ((string name, JsonNode? len) in ph)
				{
					if (!phases.Contains(name))
					{
						Problem($"{entry}.phases.{name}", $"unknown phase '{name}': {string.Join(", ", phases)}");
					}
					else if (TimeProblem(len, relative: false) is not null)
					{
						Problem($"{entry}.phases.{name}", "must be frames from 0 to 36000, \"Nms\", or a $timing, $look, $step or $param reference");
					}
				}
				foreach (string name in phases)
				{
					starts[name] = known;
					if (ph[name] is JsonValue v && v.TryGetValue(out int f))
					{
						known += f;
					}
					else if (ph[name] is JsonValue m && m.TryGetValue(out string? ms) && MsPattern().IsMatch(ms))
					{
						known += int.Parse(ms[..^2], CultureInfo.InvariantCulture) * 60 / 1000;
					}
					else
					{
						allKnown = false;
					}
				}
				if (allKnown && known > MaxFrames)
				{
					Problem(entry, $"lasts {known} frames; a script lasts at most {MaxFrames}");
				}
			}
			var marks = new HashSet<string>(phases.Append("end"));
			JsonObject? c = s;
			for (int depth = 0; c is not null && depth < 9; depth++)
			{
				if (c["marks"] is JsonObject mk)
				{
					marks.UnionWith(mk.Select(m => m.Key));
				}
				c = c["extends"] is JsonValue ex && ex.TryGetValue(out string? p) && _allScripts.TryGetValue(p, out JsonObject? po) ? po : null;
			}
			if (s["marks"] is JsonObject ownMarks)
			{
				foreach ((string name, JsonNode? at) in ownMarks)
				{
					if (phases.Contains(name) || name == "end" || !IsId(name))
					{
						Problem($"{entry}.marks.{name}", "a mark's name must not be a phase name or end");
					}
					else if (At()(at) is { } reason)
					{
						Problem($"{entry}.marks.{name}", reason);
					}
				}
			}
			if (s["tracks"] is JsonArray tracks)
			{
				CheckTracks(tracks, $"{entry}.tracks", marks, kind, allKnown && s["phases"] is JsonObject ? starts : null, s["phases"] as JsonObject);
			}
			return true;
		}

		private bool CheckFragment(string id, JsonObject f)
		{
			string entry = "fragments." + id;
			Fields(f, entry, new() { ["params"] = n => n is JsonObject ? null : "must be an object", ["tracks"] = n => n is JsonArray ? null : "must be a list of tracks" }, null);
			// Calls nest at most 8 deep.
			int Depth(JsonObject frag, int d, HashSet<string> path)
			{
				int deepest = d;
				foreach (JsonObject t in (frag["tracks"] as JsonArray ?? []).OfType<JsonObject>().Where(t => Text(t["type"]) == "call"))
				{
					string? callee = Text(t["fragment"]);
					if (callee is null || !_allFragments.TryGetValue(callee, out JsonObject? next))
					{
						continue;
					}
					if (!path.Add(callee))
					{
						return 99;
					}
					deepest = Math.Max(deepest, Depth(next, d + 1, path));
					path.Remove(callee);
				}
				return deepest;
			}
			if (Depth(f, 1, [id]) > 8)
			{
				Problem(entry, "calls nest deeper than 8, or call themselves");
			}
			if (f["tracks"] is JsonArray tracks)
			{
				CheckTracks(tracks, entry + ".tracks", ["end"], "fragment", null, null);
			}
			return true;
		}

		private void CheckTracks(JsonArray tracks, string entry, HashSet<string> marks, string kind, Dictionary<string, long>? starts, JsonObject? phases)
		{
			var ids = new HashSet<string>();
			for (int i = 0; i < tracks.Count; i++)
			{
				string te = $"{entry}[{i}]";
				if (tracks[i] is not JsonObject t)
				{
					Problem(te, "must be an object");
					continue;
				}
				string? type = Text(t["type"]);
				if (type is null || !TrackTypes.Contains(type))
				{
					Problem(te + ".type", $"unknown track type '{type}': {string.Join(", ", TrackTypes)}");
					continue;
				}
				Fields(t, te, TrackFields(type), null);
				if (t["reduced"] is JsonObject replacement)
				{
					CheckTracks([replacement.DeepClone()], te + ".reduced", marks, kind, starts, phases);
				}
				if (Text(t["id"]) is { } tid && !ids.Add(tid))
				{
					Problem(te + ".id", $"id '{tid}' used twice in the script");
				}
				foreach (JsonNode? at in t["at"] is JsonArray list ? (IEnumerable<JsonNode?>)list : [t["at"]])
				{
					string? s = Text(at);
					if (s is not null && TimeProblem(at, false) is not null)
					{
						int sign = s.IndexOfAny(['+', '-']);
						string name = sign < 0 ? s : s[..sign];
						if (!marks.Contains(name))
						{
							Problem(te + ".at", $"unknown phase or mark '{name}': {string.Join(", ", marks)}");
						}
					}
				}
				if (type is "sprite" or "react" && Text(t["effect"]) is { } effect && effect != "$look.effect" && effect != "none" && !_effects.Contains(effect) && !ctx.EffectFrames.ContainsKey(effect))
				{
					Problem(te + ".effect", $"unknown effect '{effect}': not an effect animation, an effect in assets.json, a generated effect or $look.effect");
				}
				if (type == "color" && Text(t["on"]) is "stage" or "frame" && (t["to"] is JsonObject to && to.ContainsKey("multiply") || t["colors"] is JsonArray cs && cs.OfType<JsonObject>().Any(c => c.ContainsKey("multiply"))))
				{
					Problem(te, $"'multiply' cannot be used on '{Text(t["on"])}': use tint and amount");
				}
				if (kind != "battle" && type == "transition")
				{
					Problem(te, "a transition track plays only in a battle script (cover, hold, reveal)");
				}
				// A release before the result phase, or after it ends, as far as numbers tell.
				if (type == "release" && kind == "turn" && starts is not null && phases is not null)
				{
					long result = starts["result"];
					long resultEnd = result + (phases["result"] is JsonValue rv && rv.TryGetValue(out int rl) ? rl : 0);
					foreach (JsonNode? at in t["at"] is JsonArray list2 ? (IEnumerable<JsonNode?>)list2 : [t["at"] ?? "result"])
					{
						long? frame = at is JsonValue fv && fv.TryGetValue(out int f) ? f : Text(at) is { } s2 && s2.Split('+') is [var n, var off] && starts.TryGetValue(n, out long b)
							&& int.TryParse(off, out int o) ? b + o : Text(at) is { } s3 && starts.TryGetValue(s3, out long b2) ? b2 : null;
						if (frame < result)
						{
							Problem(te, $"release at frame {frame} is before the result phase (frame {result}); it is released at frame {result}");
						}
						else if (frame >= resultEnd && phases.ContainsKey("result"))
						{
							Problem(te, $"release at frame {frame} is after the result phase ends (frame {resultEnd}); its events would be forced then");
						}
					}
				}
			}
		}

		private bool CheckTransition(string id, JsonObject t)
		{
			string entry = "transitions." + id;
			string? kind = Text(t["kind"]);
			if (kind is null || !TransitionKinds.Contains(kind))
			{
				return Fail(entry + ".kind", $"unknown transition kind '{kind}': {string.Join(", ", TransitionKinds)}");
			}
			Fields(t, entry, new()
			{
				["kind"] = Any(), ["color"] = Color(), ["count"] = Int(1, 3), ["on"] = Time(), ["off"] = Time(), ["ease"] = Ease(),
				["from"] = Enum("left", "right", "top", "bottom", "top_left", "top_right", "bottom_left", "bottom_right", "center", "edge"), ["edge"] = Int(0, 512),
				["shape"] = Enum("circle", "diamond", "square"), ["center"] = Point(), ["tile"] = Int(8, 160), ["grow"] = Time(),
				["order"] = Enum("rows", "columns", "checker", "diagonal", "random", "spiral", "spiral_in"), ["turn"] = Enum("cw", "ccw"),
				["steps"] = Int(2, 32), ["step_frames"] = Time(), ["cover_step_frames"] = Time(), ["reveal_step_frames"] = Time(), ["speed"] = Num(1, 4000), ["frames"] = Time(),
				["axis"] = Enum("horizontal", "vertical"),
				["phases"] = n => kind != "sequence" ? "is for a sequence transition"
					: n is JsonArray a && a.Count is >= 1 and <= 8 && a.All(p => p is JsonObject o && o.ContainsKey("color") && o.All(f => f.Key switch
					{
						"color" => ColorProblem(f.Value) is null,
						"steps" => Int(1, 32)(f.Value) is null,
						"step_frames" or "hold" => TimeProblem(f.Value, false) is null,
						_ => false,
					})) ? null : "must be 1 to 8 phases, each {color, steps (1 to 32), step_frames, hold}",
			}, null);
			if (kind == "sequence" && t["phases"] is null)
			{
				return Fail(entry + ".phases", "a sequence transition needs its phases");
			}
			return true;
		}

		// A select key, checked against the data.
		private string? KeyProblem(string key)
		{
			string[] parts = key.Split('/');
			string head = parts[0];
			string? Qualifiers(IEnumerable<string> rest, params string[] allowed)
			{
				foreach (string q in rest)
				{
					string[] kv = q.Split(':', 2);
					string? reason = kv[0] switch
					{
						_ when !allowed.Contains(kv[0]) => $"unknown qualifier '{q}': {string.Join(", ", allowed.Select(a => a + ":..."))}",
						"encounter" => ctx.Data.Encounters.Contains(kv.ElementAtOrDefault(1) ?? "") ? null : $"unknown encounter '{kv.ElementAtOrDefault(1)}': not in the game data",
						"area" => ctx.Data.Areas.Contains(kv.ElementAtOrDefault(1) ?? "") ? null : $"unknown area '{kv.ElementAtOrDefault(1)}': not in the game data",
						"count" => kv.ElementAtOrDefault(1) is "1" or "2" or "3" or "4" or "5" ? null : "count:N takes 1 to 5 (5 is five or more enemies)",
						"character" => ctx.Data.Characters.Contains(kv.ElementAtOrDefault(1) ?? "") ? null : $"unknown character '{kv.ElementAtOrDefault(1)}': not in the game data",
						"weapon" => _classes.Contains(kv.ElementAtOrDefault(1) ?? "") ? null : $"unknown weapon class '{kv.ElementAtOrDefault(1)}': {string.Join(", ", _classes.Order(StringComparer.Ordinal))}",
						"terrain" => kv.ElementAtOrDefault(1) is "floor" or "water" or "soft" ? null : "terrain: floor, water or soft",
						"kind" => kv.ElementAtOrDefault(1) is "town" or "field" or "dungeon" ? null : "kind: town, field or dungeon",
						"opening" => kv.ElementAtOrDefault(1) is "enemy_first" or "party_first" ? null : "opening: enemy_first or party_first",
						"vehicle" when kv.Length == 2 => ctx.Data.Vehicles.Contains(kv[1]) ? null : $"unknown vehicle '{kv[1]}': not in the game data",
						"one" or "both" or "boss" or "vehicle" => kv.Length == 1 ? null : $"'{kv[0]}' takes no value",
						_ => null,
					};
					if (reason is not null)
					{
						return reason;
					}
				}
				return null;
			}
			string[] hv = head.Split(':', 2);
			string value = hv.ElementAtOrDefault(1) ?? "";
			string[] turnQualifiers = ["count", "encounter"];
			switch (hv[0])
			{
			case "default" when hv.Length == 1:
				return Qualifiers(parts[1..], turnQualifiers);
			case "turn" when value == "escape":
				return Qualifiers(parts[1..], turnQualifiers);
			case "item" when ctx.Data.PieceUses.ContainsKey(value) || ctx.Data.EquipmentCategories.ContainsKey(value):
				return Qualifiers(parts[1..], "character", "weapon", "count", "encounter");
			case "ability" or "item":
				return ctx.Data.Actions.ContainsKey(value) ? Qualifiers(parts[1..], "character", "weapon", "vehicle", "count", "encounter") : $"unknown ability or item '{value}': not in the game data";
			case "combo":
				return ctx.Data.Recipes.ContainsKey(value) ? Qualifiers(parts[1..], turnQualifiers) : $"unknown combination recipe '{value}': not in combos.json";
			case "weapon":
				return _classes.Contains(value) ? Qualifiers(parts[1..], "one", "both", "character", "count", "encounter")
					: $"unknown weapon class '{value}': {string.Join(", ", _classes.Order(StringComparer.Ordinal))}";
			case "shape":
				string[] shape = value.Split('.');
				return shape.Length is >= 1 and <= 3 && BattlePresentation.Kinds.Contains(shape[0]) && (shape.Length < 2 || BattlePresentation.Effects.Contains(shape[1]))
					&& (shape.Length < 3 || BattlePresentation.Elements.Contains(shape[2])) ? Qualifiers(parts[1..], "character", "vehicle", "count", "encounter") : $"unknown shape '{value}': KIND, KIND.EFFECT or KIND.EFFECT.ELEMENT";
			case "enemy":
				if (!ctx.Data.Enemies.Contains(value))
				{
					return $"unknown enemy '{value}': not in the game data";
				}
				if (parts.Length < 2 || !(parts[1] is "attack" or "defend" || ctx.Data.Actions.ContainsKey(parts[1])))
				{
					return "enemy:ID/ACTION, ACTION attack, defend or an ability id";
				}
				return Qualifiers(parts[2..], turnQualifiers);
			case "encounter":
				if (!ctx.Data.Encounters.Contains(value))
				{
					return $"unknown encounter '{value}': not in the game data";
				}
				return parts.Length >= 2 ? KeyProblem(string.Join('/', parts[1..])) : "encounter:ID/enemy:ID/ACTION or encounter:ID/change:...";
			case "step":
				string step = value.Split('.')[0];
				if (step is not ("intro" or "opening" or "round" or "retarget" or "notice" or "roundend" or "outcome" or "sync" or "cancel" or "recover"))
				{
					return $"unknown step '{value}': intro, opening, round, retarget, notice, roundend, outcome, sync, cancel, recover";
				}
				string sub = value.Contains('.') ? value[(value.IndexOf('.') + 1)..] : "";
				string? subProblem = (step, sub) switch
				{
					(_, "") => null,
					("opening", _) => sub is "normal" or "party_first" or "enemy_first" ? null : "opening: normal, party_first or enemy_first",
					("outcome", _) => Outcomes.Contains(sub) ? null : $"outcome: {string.Join(", ", Outcomes)}",
					("cancel", _) => CancelReasons.Contains(sub) ? null : $"cancel: {string.Join(", ", CancelReasons)}",
					("notice", _) => IsId(sub) ? null : $"unknown notice key '{sub}'",
					_ => $"step:{step} takes no .KEY",
				};
				return subProblem ?? Qualifiers(parts[1..], "encounter", "area");
			case "change":
				string change = value.Split('.')[0];
				string enemy = value.Contains('.') ? value[(value.IndexOf('.') + 1)..] : "";
				return !Changes.Contains(change) ? $"unknown change '{change}': {string.Join(", ", Changes)}"
					: enemy.Length > 0 && !ctx.Data.Enemies.Contains(enemy) ? $"unknown enemy '{enemy}': not in the game data" : parts.Length > 1 ? "change keys take no qualifier" : null;
			case "react":
				string ev = value.Split('.')[0];
				string rest = value.Contains('.') ? value[(value.IndexOf('.') + 1)..] : "";
				return !ReactionKeys.Contains(ev) ? $"unknown reaction '{ev}': {string.Join(", ", ReactionKeys)}"
					: rest.Length > 0 && !ctx.Data.Enemies.Contains(rest) && !(ev == "status" && ctx.Data.Statuses.Contains(rest)) ? $"unknown enemy or status '{rest}': not in the game data" : null;
			case "battle":
				return value is "transition_in" or "backdrop" or "transition_out"
					? Qualifiers(parts[1..], "encounter", "area", "terrain", "kind", "boss", "opening", "vehicle") : "battle:transition_in, battle:backdrop or battle:transition_out";
			case "status":
				return !ctx.Data.Statuses.Contains(value) ? $"unknown status '{value}': not in the game data" : parts.Length == 2 && parts[1] is "idle" or "tick" ? null : "status:ID/idle or status:ID/tick";
			}
			return $"unknown key '{key}': default, turn:escape, ability:, item:, combo:, weapon:, shape:, enemy:, encounter:, step:, change:, react:, battle: or status:";
		}

		private string? ScopeProblem(string scope)
		{
			string[] parts = scope.Split(':', 2);
			return parts[0] switch
			{
				"party" or "enemy" or "vehicle" when parts.Length == 1 => null,
				"party" => ctx.Data.Characters.Contains(parts[1]) ? null : $"unknown character '{parts[1]}': not in the game data",
				"enemy" => ctx.Data.Enemies.Contains(parts[1]) ? null : $"unknown enemy '{parts[1]}': not in the game data",
				"vehicle" => ctx.Data.Vehicles.Contains(parts[1]) ? null : $"unknown vehicle '{parts[1]}': not in the game data",
				_ => $"unknown scope '{scope}': party, enemy, vehicle, or one of them with :ID",
			};
		}
	}
}
