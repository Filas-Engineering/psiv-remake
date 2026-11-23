using System.Globalization;
using System.Text.Json.Nodes;

using Psiv.Session;

namespace Psiv.Sample;

// What a script reads when it is instantiated and drawn: the turn, its look, its events, and the figures a change or a
// reaction names.
public sealed class AnimInputs
{
	public BattleTurn? Turn;
	public ActionLook? Look;
	public IReadOnlyList<BattleEvent> Events = [];
	public IReadOnlyDictionary<string, int> Timings = new Dictionary<string, int>();
	public long StepLength;
	public int Changed = -1;
	public int From = -1;
	public int To = -1;
	public int Self = -1;
	public bool Combo;
	public bool Vehicle;
	public bool Front;
	public int PartyCount;
	// The profile's reduced flash when the script was instantiated: tracks with a replacement `reduced` object take it.
	public bool Reduced;

	public IEnumerable<int> Targets => Turn?.Targets ?? [];
	public IEnumerable<int> Actors => Turn?.Actors ?? [];
}

// One track of an instantiated script: its definition with parameters and calls filled in, its start and length in
// clock units, and for lengths given as a share of a phase, the phase and the share, so its progress is the same float
// as the phase's (a "25%" flash reads p / 0.25f as the table's p * 4f did).
public sealed class AnimTrack
{
	public required JsonObject Def;
	public required string Type;
	public string? Id;
	public int Index;
	public long Start;
	public long Length;
	public long PhaseStart;
	public long PhaseLength;
	public float Share = float.NaN;
	// The track names its length (false: a sprite plays its animation's length).
	public bool HasLength = true;
	// A release track's starts, one per unit, the last repeating with the stagger.
	public List<long> Ats = [];

	public string Label => Id ?? Index.ToString(CultureInfo.InvariantCulture);

	public long End => Start + Length;

	// Progress 0 to 1 at script time `now` (may run past 1).
	public float T(long now)
	{
		if (!float.IsNaN(Share) && Start == PhaseStart)
		{
			return BattleClock.Ratio(now - PhaseStart, PhaseLength) / Share;
		}
		return Length <= 0 ? 1f : BattleClock.Ratio(now - Start, Length);
	}

	public bool Active(long now) => now >= Start && (Length > 0 ? now < End : now == Start);
}

public sealed class AnimRun
{
	public required string ScriptId;
	public required string Key;
	public required string Kind;
	public required AnimInputs In;
	public int RateMilli;
	public List<(string Name, long Start, long Length, bool Present)> Phases = [];
	public Dictionary<string, long> Marks = new(StringComparer.Ordinal);
	public List<AnimTrack> Tracks = [];
	public long Total;
	// A looping script repeats this length.
	public long Loop;
	public List<string> Problems = [];
	// The release event cues this script turns off (its own `event_cues_off` or a parent's); null leaves it to the set.
	public HashSet<string>? CuesOff;
	// The kinds of turn step that draw only the members acting (its own `solo` or a parent's); null leaves it to the set.
	public HashSet<string>? Solo;
	// The sound instances (track index, instance) this run has played: steps that share a frame (a phase of length 0)
	// each reach it, and a sound plays once per action, or per figure under `per: each`.
	public HashSet<(int Track, int Instance)> SoundsPlayed = [];

	public (string Name, long Start, long Length) PhaseAt(long t)
	{
		(string Name, long Start, long Length, bool) last = Phases.LastOrDefault(p => p.Present && p.Start <= t);
		return last.Name is null ? ("-", 0, Total) : (last.Name, last.Start, last.Length);
	}

	public long PhaseStart(string name) => Phases.FirstOrDefault(p => p.Name == name).Start;

	public bool Has(string phase) => Phases.Any(p => p.Name == phase && p.Present);
}

public static class BattleAnimationRun
{
	// A per-layout value: the stage's layout picks one.
	public static JsonNode? Pick(JsonNode? n, bool front) =>
		n is JsonObject o && o.Count == 2 && o.ContainsKey("front") && o.ContainsKey("side") ? o[front ? "front" : "side"] : n;

	public static string? Str(JsonNode? n) => n is JsonValue v && v.TryGetValue(out string? s) ? s : null;

	public static double? Num(JsonNode? n) => n is JsonValue v && v.TryGetValue(out double d) ? d : null;

	public static bool? Bool(JsonNode? n) => n is JsonValue v && v.TryGetValue(out bool b) ? b : null;

	// A TIME in clock units; `phaseLength` and `rest` answer "$phase" and "$rest", and `share` takes a "N%" (null
	// leaves it to the caller). Null when the value is not a time.
	public static long? Time(JsonNode? n, AnimRun run, long phaseLength, long rest, out float share)
	{
		share = float.NaN;
		n = Pick(n, run.In.Front);
		if (n is JsonValue v && v.TryGetValue(out int frames))
		{
			return frames * BattleClock.Frame;
		}
		string? s = Str(n);
		if (s is null)
		{
			return null;
		}
		if (s.EndsWith("ms", StringComparison.Ordinal) && int.TryParse(s[..^2], CultureInfo.InvariantCulture, out int ms))
		{
			return (long)ms * run.RateMilli;
		}
		if (s.EndsWith('%') && double.TryParse(s[..^1], CultureInfo.InvariantCulture, out double pct))
		{
			share = (float)(pct / 100);
			return (long)Math.Round(phaseLength * pct / 100);
		}
		return s switch
		{
			"$phase" => phaseLength,
			"$rest" => rest,
			"$step" => run.In.StepLength,
			"$look.animate_ms" => (long)(run.In.Look?.AnimateMs ?? 0) * run.RateMilli,
			"$look.effect_ms" => (long)(run.In.Look?.EffectMs ?? 0) * run.RateMilli,
			_ when s.StartsWith("$timing.", StringComparison.Ordinal) => (long)run.In.Timings.GetValueOrDefault(s[8..]) * run.RateMilli,
			_ => null,
		};
	}

	public static long Time(JsonNode? n, AnimRun run, long fallback = 0) => Time(n, run, 0, 0, out _) ?? fallback;

	// Builds a script for one use. `phases` gives each phase of the kind, in order: its default length, and whether it is
	// present (a turn leaves phases out as it leaves steps out). Null when the id names nothing.
	public static AnimRun Instantiate(BattleAnimation set, string scriptId, string key, string kind, AnimInputs inputs, IReadOnlyList<(string Name, long Default, bool Present)> phases,
		long reactionLength = 0)
	{
		var run = new AnimRun { ScriptId = scriptId, Key = key, Kind = kind, In = inputs, RateMilli = set.RateMilli };
		var walked = new List<JsonObject>();
		for (string? cur = scriptId; cur is not null && set.Scripts.TryGetValue(cur, out JsonObject? s) && walked.Count < 9 && !walked.Contains(s); cur = Str(s["extends"]))
		{
			walked.Add(s);
			if (s["event_cues_off"] is JsonNode off)
			{
				run.CuesOff = [.. BattleAnimation.CuesOff(off)];
				break;
			}
		}
		string? from = scriptId;
		for (int depth = 0; depth < 9 && from is not null && set.Scripts.TryGetValue(from, out JsonObject? s); depth++, from = Str(s["extends"]))
		{
			if (s["solo"] is JsonNode solo)
			{
				run.Solo = [.. BattleAnimation.SoloOf(solo)];
				break;
			}
		}
		(JsonObject phasesDef, JsonObject marksDef, JsonObject paramsDef, JsonNode? lengthDef, List<JsonObject> tracks) = Flatten(set, scriptId);
		long at = 0;
		foreach ((string name, long def, bool present) in phases)
		{
			long len = present ? Time(phasesDef[name], run, def, 0, out _) ?? def : 0;
			run.Phases.Add((name, at, len, present));
			at += len;
		}
		run.Total = at;
		if (phases.Count == 0)
		{
			run.Total = lengthDef is null ? reactionLength : Time(lengthDef, run, reactionLength, reactionLength, out _) ?? reactionLength;
			run.Loop = run.Total;
		}
		foreach ((string name, JsonNode? m) in marksDef)
		{
			if (Start(m, run, 0, out _) is long t)
			{
				run.Marks[name] = t;
			}
		}
		Expand(set, run, tracks, paramsDef, new Dictionary<string, string>(), 0, null, 0);
		return run;
	}

	private static (JsonObject Phases, JsonObject Marks, JsonObject Params, JsonNode? Length, List<JsonObject> Tracks) Flatten(BattleAnimation set, string id)
	{
		var chain = new List<JsonObject>();
		for (string? cur = id; cur is not null && set.Scripts.TryGetValue(cur, out JsonObject? s) && chain.Count < 9 && !chain.Contains(s); cur = Str(s["extends"]))
		{
			chain.Insert(0, s);
		}
		JsonObject phases = [], marks = [], parameters = [];
		JsonNode? length = null;
		var tracks = new List<JsonObject>();
		foreach (JsonObject s in chain)
		{
			foreach ((string k, JsonNode? v) in s["phases"] as JsonObject ?? [])
			{
				phases[k] = v?.DeepClone();
			}
			foreach ((string k, JsonNode? v) in s["marks"] as JsonObject ?? [])
			{
				marks[k] = v?.DeepClone();
			}
			foreach ((string k, JsonNode? v) in s["params"] as JsonObject ?? [])
			{
				parameters[k] = v?.DeepClone();
			}
			length = s["length"]?.DeepClone() ?? length;
			foreach (JsonObject t in (s["tracks"] as JsonArray ?? []).OfType<JsonObject>())
			{
				string? tid = Str(t["id"]);
				int existing = tid is null ? -1 : tracks.FindIndex(x => Str(x["id"]) == tid);
				if (existing >= 0)
				{
					tracks[existing] = (JsonObject)t.DeepClone();
				}
				else
				{
					tracks.Add((JsonObject)t.DeepClone());
				}
			}
			foreach (string? r in (s["remove"] as JsonArray ?? []).Select(Str))
			{
				tracks.RemoveAll(x => Str(x["id"]) == r);
			}
		}
		return (phases, marks, parameters, length, tracks);
	}

	// A start: a time from the script's (or the calling track's) start, or a phase or mark with an offset. `phase` is the
	// phase it names or falls in; null when it names a phase the run left out.
	private static long? Start(JsonNode? n, AnimRun run, long baseAt, out string? phase)
	{
		phase = null;
		n = Pick(n, run.In.Front);
		if (n is null)
		{
			return baseAt;
		}
		string? s = Str(n);
		if (s is null || s.StartsWith('$') || s.EndsWith("ms", StringComparison.Ordinal))
		{
			return Time(n, run, 0, 0, out _) is long t ? baseAt + t : null;
		}
		int sign = s.IndexOfAny(['+', '-'], 1);
		string name = sign < 0 ? s : s[..sign];
		long offset = 0;
		if (sign >= 0)
		{
			string off = s[(sign + 1)..];
			offset = off.EndsWith("ms", StringComparison.Ordinal) ? long.Parse(off[..^2], CultureInfo.InvariantCulture) * run.RateMilli
				: long.Parse(off, CultureInfo.InvariantCulture) * BattleClock.Frame;
			if (s[sign] == '-')
			{
				offset = -offset;
			}
		}
		if (name == "end")
		{
			return run.Total + offset;
		}
		foreach ((string pn, long ps, long _, bool present) in run.Phases)
		{
			if (pn == name)
			{
				phase = pn;
				return present ? ps + offset : null;
			}
		}
		return run.Marks.TryGetValue(name, out long m) ? m + offset : null;
	}

	private static readonly Dictionary<string, string> DefaultLengths = new()
	{
		["pose"] = "$phase", ["state"] = "$phase", ["move"] = "$phase", ["flash"] = "25%", ["shake"] = "$phase", ["color"] = "$phase", ["name_window"] = "$phase",
		["backdrop"] = "end", ["transition"] = "end", ["sound"] = "0", ["release"] = "0", ["react"] = "$rest",
	};

	private static void Expand(BattleAnimation set, AnimRun run, List<JsonObject> tracks, JsonObject parameters, Dictionary<string, string> bind, long baseAt, string? basePhase, int depth)
	{
		foreach (JsonObject raw in tracks)
		{
			JsonObject t = (JsonObject)Substitute(raw, parameters, bind)!;
			if (run.In.Reduced && t["reduced"] is JsonObject replacement)
			{
				var swapped = (JsonObject)replacement.DeepClone();
				if (!swapped.ContainsKey("at") && t["at"] is JsonNode inherited)
				{
					swapped["at"] = inherited.DeepClone();
				}
				t = swapped;
			}
			string type = Str(t["type"]) ?? "";
			if (run.In.Reduced && type == "sprite" && (Str(t["reduced"]) ?? set.ReducedPolicy.GetValueOrDefault("sprite")) == "drop")
			{
				continue;
			}
			long? start;
			string? phase;
			List<long> ats = [];
			if (type == "release")
			{
				foreach (JsonNode? at in t["at"] is JsonArray list ? (IEnumerable<JsonNode?>)list : [t["at"] ?? JsonValue.Create("result")])
				{
					if (Start(at, run, baseAt, out _) is long r)
					{
						ats.Add(r);
					}
				}
				start = ats.Count > 0 ? ats[0] : null;
				phase = null;
			}
			else
			{
				start = Start(t["at"], run, baseAt, out phase);
			}
			if (start is not long s)
			{
				continue;
			}
			(string Name, long PStart, long PLength) p = phase is not null ? run.Phases.Where(x => x.Name == phase).Select(x => (x.Name, x.Start, x.Length)).First()
				: run.Phases.Count == 0 || depth > 0 && basePhase is null ? ("-", 0L, run.Total) : run.PhaseAt(s);
			if (depth > 0 && phase is null && basePhase is not null)
			{
				p = run.Phases.Where(x => x.Name == basePhase).Select(x => (x.Name, x.Start, x.Length)).First();
			}
			if (type == "call")
			{
				if (depth < 8 && Str(t["fragment"]) is { } fid && set.Fragments.TryGetValue(fid, out JsonObject? frag))
				{
					var inner = (JsonObject)(frag["params"]?.DeepClone() ?? new JsonObject());
					foreach ((string k, JsonNode? v) in t["params"] as JsonObject ?? [])
					{
						inner[k] = v?.DeepClone();
					}
					var innerBind = new Dictionary<string, string>(bind);
					foreach ((string k, JsonNode? v) in t["bind"] as JsonObject ?? [])
					{
						innerBind[k] = Str(v) ?? k;
					}
					Expand(set, run, [.. (frag["tracks"] as JsonArray ?? []).OfType<JsonObject>()], inner, innerBind, s, p.Name == "-" ? null : p.Name, depth + 1);
				}
				continue;
			}
			long rest = Math.Max(0, p.PStart + p.PLength - s);
			if (run.Phases.Count == 0)
			{
				rest = Math.Max(0, run.Total - s);
			}
			JsonNode? lengthNode = t["length"] ?? (DefaultLengths.TryGetValue(type, out string? d) ? JsonValue.Create(d) : null);
			// A tint lasts its in, hold and out; a fade holds to the script's end.
			if (type == "color" && t["length"] is null && Str(t["mode"]) is "tint" or "fade")
			{
				lengthNode = Str(t["mode"]) == "fade" ? JsonValue.Create("end")
					: JsonValue.Create((int)((Time(t["in"], run) + Time(t["hold"], run) + Time(t["out"], run)) / BattleClock.Frame));
			}
			var track = new AnimTrack { Def = t, Type = type, Id = Str(t["id"]), Index = run.Tracks.Count, Start = s, PhaseStart = p.PStart, PhaseLength = p.PLength, Ats = ats };
			if (Str(lengthNode) == "end")
			{
				track.Length = Math.Max(0, run.Total - s);
			}
			else if (Str(lengthNode) == "0")
			{
				track.Length = 0;
			}
			else if (lengthNode is null)
			{
				track.HasLength = false;
				track.Length = Math.Max(0, run.Total - s);
			}
			else
			{
				track.Length = Time(lengthNode, run, p.PLength, rest, out float share) ?? rest;
				track.Share = share;
			}
			if (run.Total > 0 && track.End > run.Total)
			{
				track.Length = Math.Max(0, run.Total - s);
			}
			run.Tracks.Add(track);
		}
	}

	// Parameters filled in and subjects renamed, on a copy.
	private static JsonNode? Substitute(JsonNode? n, JsonObject parameters, Dictionary<string, string> bind)
	{
		switch (n)
		{
		case JsonObject o:
			var copy = new JsonObject();
			foreach ((string k, JsonNode? v) in o)
			{
				copy[k] = k is "on" or "spawn" or "targets" or "place_of" && Str(v) is { } subj ? Rename(subj, bind) : Substitute(v, parameters, bind);
			}
			return copy;
		case JsonArray a:
			return new JsonArray([.. a.Select(x => Substitute(x, parameters, bind))]);
		case JsonValue v when Str(v) is { } s && s.StartsWith("$param.", StringComparison.Ordinal):
			return parameters[s[7..]]?.DeepClone() ?? v.DeepClone();
		case JsonValue v when Str(v) is { } point && bind.Count > 0 && point.Contains(':'):
			return Rename(point, bind);
		default:
			return n?.DeepClone();
		}
	}

	private static string Rename(string subject, Dictionary<string, string> bind)
	{
		foreach ((string from, string to) in bind)
		{
			if (subject == from)
			{
				return to;
			}
			if (subject.StartsWith(from + ":", StringComparison.Ordinal) && !char.IsDigit(subject[from.Length + 1]))
			{
				return to + subject[from.Length..];
			}
		}
		return subject;
	}
}
