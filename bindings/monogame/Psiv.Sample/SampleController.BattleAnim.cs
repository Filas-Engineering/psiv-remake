using System.Globalization;
using System.Text.Json.Nodes;

using Psiv.Session;

namespace Psiv.Sample;

// Battle animation scripts on the round's steps (docs/battle-animation.md): choosing each turn's, step's, change's and
// reaction's script, releasing results on their frames, playing script sounds, and the animation log and probes.
public sealed partial class SampleController
{
	private BattleAnimation? _defaultAnimation;
	private readonly List<string> _animLog = [];
	private int _animLogMark;
	// The figures as the last frame drew them, for the `figure.LABEL` probe, and their bodies for stagger orders.
	private readonly Dictionary<string, string> _probeFigureLooks = [];
	private Dictionary<int, Microsoft.Xna.Framework.Rectangle> _lastBodies = [];
	// The battle's looping backdrop script and the clock it started on.
	private AnimRun? _backdropRun;
	private int _backdropStartMs;
	// A swapped backdrop that stays for the rest of the battle.
	private int? _persistBackdrop;

	private BattleAnimation Anim => _assets?.Animation ?? (_defaultAnimation ??= BattleAnimation.Builtin());

	private static bool IsTurnPhase(BattleStep s) => s.Kind is StepKind.Forward or StepKind.Banner or StepKind.Animate or StepKind.Effect or StepKind.Result or StepKind.Back
		&& s.Turn is { Actors.Count: > 0 };

	private static string PhaseOf(StepKind kind) => kind.ToString().ToLowerInvariant();

	private AnimInputs Inputs(BattleTurn? turn, ActionLook? look, IReadOnlyList<BattleEvent> events, long stepLength)
	{
		BattleInfo? info = StageInfo();
		return new AnimInputs
		{
			Turn = turn, Look = look, Events = events, Timings = PresentationTable.Timings, StepLength = stepLength, Combo = turn?.Combo == true, Vehicle = (info?.Vehicle ?? "").Length > 0,
			Front = StageLayout() == BattleLayout.Front, PartyCount = info?.PartyCount ?? StageCombatants().Count(c => c.Side == Side.Party),
			Reduced = _s.Profile().ReducedFlash,
		};
	}

	// The first key of `keys` with a script; `trace` gets each key tried with the script it found or `-`.
	private (string Id, string Key) Choose(IEnumerable<string> keys, List<string> trace, string fallback)
	{
		BattleAnimation set = Anim;
		foreach (string key in keys)
		{
			string? id = set.Find(key);
			trace.Add($"{key} {id ?? "-"}");
			if (id is not null)
			{
				return (id, key);
			}
		}
		return (fallback, "-");
	}

	private (string, string)? Forced(string kind) => _forcedScript is { } id && ScriptKind(id) == kind ? (id, "preview") : null;

	private string EnemyCountKey() => Math.Clamp(_show.All.Count(c => c.Position >= 0 && c.Alive), 1, 5).ToString(CultureInfo.InvariantCulture);

	// A member's weapon animation class and whether both hands attack, from its weapon and left hand.
	private (string Class, string Hands)? WeaponOf(int index)
	{
		IReadOnlyList<Combatant> all = StageCombatants();
		if (index < 0 || index >= all.Count || all[index].Side != Side.Party)
		{
			return null;
		}
		Member? m = _s.Party().FirstOrDefault(x => x.Id == all[index].Id);
		if (m is null)
		{
			return null;
		}
		string right = m.Equipment.ElementAtOrDefault((int)EquipSlot.Weapon) ?? "", left = m.Equipment.ElementAtOrDefault((int)EquipSlot.LeftHand) ?? "";
		string main = _data.EquipmentCategories.ContainsKey(right) ? right : left;
		if (!_data.EquipmentCategories.TryGetValue(main, out string? category))
		{
			return null;
		}
		bool both = _data.EquipmentCategories.ContainsKey(right) && _data.EquipmentCategories.ContainsKey(left) || _data.TwoHanded.Contains(main);
		return (Anim.WeaponClass(main, category), both ? "both" : "one");
	}

	private List<string> TurnKeys(BattleTurn t)
	{
		var keys = new List<string>();
		string encounter = StageInfo()?.Encounter ?? "";
		string count = EnemyCountKey();
		void Q(string k)
		{
			if (encounter.Length > 0)
			{
				keys.Add(k + "/encounter:" + encounter);
			}
			keys.Add(k + "/count:" + count);
			keys.Add(k);
		}
		if (t.Escape)
		{
			Q("turn:escape");
			return keys;
		}
		IReadOnlyList<Combatant> all = StageCombatants();
		string character = t.Actors.Count > 0 && t.Actors[0] < all.Count && all[t.Actors[0]].Side == Side.Party ? all[t.Actors[0]].Id : "";
		(string Class, string Hands)? weapon = t.Combo ? null : WeaponOf(t.Actors.FirstOrDefault(-1));
		string vehicle = StageInfo()?.Vehicle ?? "";
		bool aboard = vehicle.Length > 0 && character == vehicle;
		if (t.EnemyActor.Length > 0)
		{
			string action = t.Command switch { CommandKind.Attack => "attack", CommandKind.Defend => "defend", _ => t.Ability };
			keys.Add($"encounter:{encounter}/enemy:{t.EnemyActor}/{action}");
			Q($"enemy:{t.EnemyActor}/{action}");
		}
		if (t.Combo && t.Recipe.Length > 0)
		{
			Q("combo:" + t.Recipe);
		}
		if (t.Ability.Length > 0 && t.Command != CommandKind.Defend)
		{
			string kind = _data.Actions.GetValueOrDefault(t.Ability)?.Kind == "item" || t.Command == CommandKind.Item ? "item" : "ability";
			if (character.Length > 0)
			{
				Q($"{kind}:{t.Ability}/character:{character}");
			}
			if (weapon is { } w)
			{
				Q($"{kind}:{t.Ability}/weapon:{w.Class}");
			}
			if (aboard)
			{
				Q($"{kind}:{t.Ability}/vehicle:{vehicle}");
			}
			Q($"{kind}:{t.Ability}");
		}
		if (t.Command == CommandKind.Attack && weapon is { } arm && character.Length > 0)
		{
			Q($"weapon:{arm.Class}/{arm.Hands}/character:{character}");
			Q($"weapon:{arm.Class}/character:{character}");
			Q($"weapon:{arm.Class}/{arm.Hands}");
			Q($"weapon:{arm.Class}");
		}
		if (t.Shape is { } shape)
		{
			// A member's defend and a vehicle's actions take their actor as a qualifier on the shape keys too.
			string actor = aboard ? "vehicle:" + vehicle : t.Command == CommandKind.Defend && character.Length > 0 ? "character:" + character : "";
			if (actor.Length > 0)
			{
				Q($"shape:{shape.Kind}/{actor}");
			}
			Q($"shape:{shape.Kind}.{shape.Effect}.{shape.Element}");
			Q($"shape:{shape.Kind}.{shape.Effect}");
			Q($"shape:{shape.Kind}");
		}
		Q("default");
		return keys;
	}

	private IEnumerable<string> StepKeys(string kind, string sub)
	{
		string encounter = StageInfo()?.Encounter ?? "", area = _s.FieldInfo()?.AreaId ?? "";
		foreach (string k in sub.Length > 0 ? new[] { $"step:{kind}.{sub}", $"step:{kind}" } : [$"step:{kind}"])
		{
			if (encounter.Length > 0)
			{
				yield return k + "/encounter:" + encounter;
			}
			if (area.Length > 0)
			{
				yield return k + "/area:" + area;
			}
			yield return k;
		}
	}

	private IEnumerable<string> ChangeKeys(BattleStep step)
	{
		string change = step.Kind switch { StepKind.Join => "join", StepKind.Transform => "transform", _ => step.To >= 0 ? "merge" : "leave" };
		string enemy = step.To >= 0 && step.To < _battle.Labels.Count ? EnemyIdAfter(step) : step.From >= 0 ? _show.All.ElementAtOrDefault(step.From)?.Id ?? "" : "";
		string encounter = StageInfo()?.Encounter ?? "";
		List<string> chain = change switch { "leave" => ["leave", "merge"], _ => [change] };
		foreach (string c in chain)
		{
			if (enemy.Length > 0)
			{
				yield return $"encounter:{encounter}/change:{c}.{enemy}";
				yield return $"change:{c}.{enemy}";
			}
			yield return "change:" + c;
		}
	}

	private static string EnemyIdAfter(BattleStep step) =>
		step.Events.LastOrDefault(e => e.Type is BattleEventType.EnemyJoined or BattleEventType.EnemyTransformed)?.Ids.FirstOrDefault() ?? "";

	private static List<(string, long, bool)> OnePhase(string name, long length) => [(name, length, true)];

	// A battle-level moment's keys: by encounter, area, terrain, area kind and vehicle, then bare.
	private IEnumerable<string> BattleKeys(string moment)
	{
		BattleInfo? info = StageInfo();
		FieldInfo? field = _s.FieldInfo();
		string b = "battle:" + moment;
		if (info?.Encounter is { Length: > 0 } encounter)
		{
			yield return $"{b}/encounter:{encounter}";
		}
		if (field?.AreaId is { Length: > 0 } area)
		{
			yield return $"{b}/area:{area}";
		}
		if (_battleTerrain.Length > 0)
		{
			yield return $"{b}/terrain:{_battleTerrain}";
		}
		if (field is not null)
		{
			yield return $"{b}/kind:{FieldRenderer.KindName(field.AreaKind).ToLowerInvariant()}";
		}
		if ((info?.Vehicle ?? "").Length > 0)
		{
			yield return $"{b}/vehicle";
		}
		yield return b;
	}

	// A battle-level script: a transition with cover, hold and reveal, or a looping one with a length.
	private AnimRun BattleRun(string moment)
	{
		var trace = new List<string>();
		(string id, string key) = Choose(BattleKeys(moment), trace, "battle.none");
		AnimInputs inputs = Inputs(null, null, [], 0);
		bool looping = Anim.Scripts.GetValueOrDefault(id)?["length"] is not null;
		return BattleAnimationRun.Instantiate(Anim, id, key, "battle", inputs, looping ? [] : [.. BattleAnimation.BattlePhases.Select(p => (p, 0L, true))]);
	}

	// Each step's script, length and release frames, when the round's steps are built.
	private void AttachScripts(List<BattleStep> steps)
	{
		_timeline.RateMilli = Anim.RateMilli;
		int rate = Anim.RateMilli;
		if (_flashVerdicts.Count > 256)
		{
			_flashVerdicts.Clear();
		}
		for (int i = 0; i < steps.Count;)
		{
			BattleStep step = steps[i];
			if (step.Kind == StepKind.Transition)
			{
				i++;
				continue;
			}
			if (IsTurnPhase(step))
			{
				int j = i;
				while (j < steps.Count && steps[j].Turn == step.Turn && (IsTurnPhase(steps[j]) || steps[j].Kind is StepKind.Retarget or StepKind.Join or StepKind.Merge or StepKind.Transform))
				{
					j++;
				}
				AttachTurn(steps.GetRange(i, j - i), rate);
				i = j;
				continue;
			}
			if (step.Kind is StepKind.Join or StepKind.Merge or StepKind.Transform)
			{
				AttachChange(step, rate);
			}
			else
			{
				AttachStep(step, step.Kind == StepKind.Result ? "recover" : PhaseOf(step.Kind), rate);
			}
			i++;
		}
	}

	private void AttachStep(BattleStep step, string kind, int rate)
	{
		var trace = new List<string>();
		(string id, string key) = Forced("step") ?? Choose(StepKeys(kind, step.Sub), trace, "step.banner");
		long length = (long)step.LengthMs * rate;
		AnimRun run = BattleAnimationRun.Instantiate(Anim, id, key, "step", Inputs(step.Turn, step.Turn?.Look, step.Events, length), OnePhase("step", length));
		step.Run = run;
		step.At = 0;
		step.Length = run.Total;
		step.Lookup = trace;
		step.Units = Units(step, run);
	}

	private void AttachChange(BattleStep step, int rate)
	{
		var trace = new List<string>();
		(string id, string key) = Forced("change") ?? Choose(ChangeKeys(step), trace, "change.merge");
		long length = (long)step.LengthMs * rate;
		AnimInputs inputs = Inputs(step.Turn, step.Change, step.Events, length);
		inputs.Changed = step.To;
		inputs.From = step.From;
		inputs.To = step.To;
		AnimRun run = BattleAnimationRun.Instantiate(Anim, id, key, "change", inputs, OnePhase("change", length));
		step.Run = run;
		step.At = 0;
		step.Length = run.Total;
		step.Lookup = trace;
		step.Units = Units(step, run);
	}

	private void AttachTurn(List<BattleStep> group, int rate)
	{
		BattleTurn turn = group[0].Turn!;
		BattleStep? result = group.FirstOrDefault(s => s.Kind == StepKind.Result);
		var trace = new List<string>();
		(string id, string key) = Forced("turn") ?? Choose(TurnKeys(turn), trace, "turn.default");
		var phases = BattleAnimation.TurnPhases.Select(name => group.FirstOrDefault(s => IsTurnPhase(s) && PhaseOf(s.Kind) == name) is { } s
			? (name, (long)s.LengthMs * rate, true) : (name, 0L, false)).ToList();
		AnimInputs inputs = Inputs(turn, turn.Look, result?.Events ?? [], 0);
		AnimRun run = BattleAnimationRun.Instantiate(Anim, id, key, "turn", inputs, phases);
		foreach (BattleStep s in group)
		{
			s.Lookup = trace;
			if (IsTurnPhase(s))
			{
				(string Name, long Start, long Length, bool Present) phase = run.Phases.First(p => p.Name == PhaseOf(s.Kind));
				s.Run = run;
				s.At = phase.Start;
				s.Length = phase.Length;
				s.Units = Units(s, run);
				continue;
			}
			if (s.Kind == StepKind.Retarget)
			{
				AttachStep(s, "retarget", rate);
				s.HeldAt = run.Has("banner") ? run.PhaseStart("banner") : run.PhaseStart("forward") + run.Phases[0].Length;
			}
			else
			{
				AttachChange(s, rate);
				s.HeldAt = run.PhaseStart("back");
			}
			s.Held = run;
		}
	}

	private static int TargetOf(BattleEvent e) => e.Type is BattleEventType.ItemHadNoEffect or BattleEventType.ResourceSpent ? e.Actor : e.Target >= 0 ? e.Target : e.Actor;

	private static bool MainResult(BattleEvent e) => e.Type is BattleEventType.DamageDealt or BattleEventType.Healed or BattleEventType.Revived or BattleEventType.StatusApplied
		or BattleEventType.StatusResisted or BattleEventType.StatModified or BattleEventType.Missed or BattleEventType.ItemHadNoEffect
		|| e.Type == BattleEventType.StatusRemoved && e.StatusEnd != StatusEnd.Defeated || e.Type == BattleEventType.Message && e.Message.StartsWith("battle.instant_defeat", StringComparison.Ordinal);

	// The step's events in release units: each release track takes the events of its targets no earlier one took, unit by
	// unit; the rest are released as the step starts. A target's units keep event order.
	private List<ReleaseUnit> Units(BattleStep step, AnimRun run)
	{
		var units = new List<ReleaseUnit>();
		var taken = new bool[step.Events.Count];
		bool releasing = step.Kind is StepKind.Result or StepKind.Join or StepKind.Merge or StepKind.Transform;
		foreach (AnimTrack t in releasing ? run.Tracks.Where(t => t.Type == "release") : [])
		{
			if (!Holds(run, t.Def))
			{
				continue;
			}
			var who = new HashSet<int>(Subject(run, BattleAnimationRun.Str(t.Def["targets"]) ?? "targets"));
			string unit = BattleAnimationRun.Str(BattleAnimationRun.Pick(t.Def["unit"], run.In.Front)) ?? "target";
			var groups = new List<ReleaseUnit>();
			ReleaseUnit? current = null;
			for (int k = 0; k < step.Events.Count; k++)
			{
				BattleEvent e = step.Events[k];
				int target = TargetOf(e);
				if (taken[k] || !who.Contains(target))
				{
					continue;
				}
				taken[k] = true;
				ReleaseUnit? into = unit switch
				{
					"all" => groups.FirstOrDefault(),
					"target" => groups.FirstOrDefault(g => g.Target == target),
					_ => current is not null && current.Target == target && (!MainResult(e) || current.Events.All(x => !MainResult(x))) ? current : null,
				};
				if (into is null)
				{
					into = new ReleaseUnit { Target = target };
					groups.Add(into);
				}
				into.Events.Add(e);
				current = into;
			}
			string order = BattleAnimationRun.Str(BattleAnimationRun.Pick(t.Def["order"], run.In.Front)) ?? "event";
			groups = [.. Ordered(groups, g => g.Target, order, run)];
			long stagger = BattleAnimationRun.Time(t.Def["stagger"], run);
			for (int k = 0; k < groups.Count; k++)
			{
				long at = k < t.Ats.Count ? t.Ats[k] : t.Ats[^1] + (k - t.Ats.Count + 1) * stagger;
				if (t.Ats.Count == 1)
				{
					at = t.Ats[0] + k * stagger;
				}
				groups[k].At = Math.Max(0, at - step.At);
			}
			units.AddRange(groups);
		}
		var rest = new ReleaseUnit { Target = -1 };
		for (int k = 0; k < step.Events.Count; k++)
		{
			if (!taken[k])
			{
				rest.Events.Add(step.Events[k]);
			}
		}
		if (rest.Events.Count > 0 || units.Count == 0)
		{
			units.Insert(0, rest);
		}
		// A unit is never released before an earlier unit of the same target.
		var byFirst = units.Where(u => u.Events.Count > 0).OrderBy(u => step.Events.IndexOf(u.Events[0])).ToList();
		var latest = new Dictionary<int, long>();
		foreach (ReleaseUnit u in byFirst)
		{
			foreach (int target in u.Events.Select(TargetOf).Distinct())
			{
				if (latest.TryGetValue(target, out long before) && u.At < before)
				{
					u.At = before;
				}
			}
			foreach (int target in u.Events.Select(TargetOf).Distinct())
			{
				latest[target] = Math.Max(latest.GetValueOrDefault(target), u.At);
			}
		}
		return [.. units.OrderBy(u => u.At)];
	}

	// Figures in a stagger order; `near_first` measures from the user.
	private IEnumerable<T> Ordered<T>(IEnumerable<T> items, Func<T, int> figure, string order, AnimRun run)
	{
		float X(int i) => _lastBodies.TryGetValue(i, out var r) ? r.Center.X : i;
		float Near(int i)
		{
			int user = run.In.Actors.FirstOrDefault(-1);
			return _lastBodies.TryGetValue(i, out var r) && _lastBodies.TryGetValue(user, out var u) ? MathF.Abs(r.Center.X - u.Center.X) + MathF.Abs(r.Center.Y - u.Center.Y) : i;
		}
		return order switch
		{
			"left_to_right" => items.OrderBy(x => X(figure(x))),
			"right_to_left" => items.OrderByDescending(x => X(figure(x))),
			"near_first" => items.OrderBy(x => Near(figure(x))),
			"far_first" => items.OrderByDescending(x => Near(figure(x))),
			_ => items,
		};
	}

	// The figures a subject names in a script.
	private List<int> Subject(AnimRun run, string name)
	{
		IReadOnlyList<Combatant> all = StageCombatants();
		// `beside:SIDE:SUBJECT` and `adjacent:SIDE:SUBJECT`: the figure of the same side standing next to the subject's first
		// figure, by formation place (marching slot for the party), `left` the lower place; `beside` passes over empty
		// places, `adjacent` takes only the place next to it.
		if (name.Split(':', 3) is [var kind and ("beside" or "adjacent"), var side, var of])
		{
			if (Subject(run, of) is not [int at, ..] || at >= all.Count)
			{
				return [];
			}
			int Place(int i) => all[i].Side == Psiv.Session.Side.Party ? i : _show.All[i].Position;
			int step = side == "left" ? -1 : 1;
			var standing = Enumerable.Range(0, Math.Min(all.Count, _show.All.Count)).Where(i => i != at && all[i].Side == all[at].Side && _show.All[i].Alive && Place(i) >= 0)
				.ToDictionary(Place, i => i);
			for (int p = Place(at) + step, limit = kind == "adjacent" ? 1 : 8; Math.Abs(p - Place(at)) <= limit && p >= 0 && p <= 7; p += step)
			{
				if (standing.TryGetValue(p, out int found))
				{
					return [found];
				}
			}
			return [];
		}
		AnimInputs a = run.In;
		List<int> actors = [.. a.Actors];
		string[] parts = name.Split(':');
		int n = parts.Length == 2 && int.TryParse(parts[1], CultureInfo.InvariantCulture, out int v) ? v : -1;
		IEnumerable<int> Side(Side side) => Enumerable.Range(0, Math.Min(all.Count, _show.All.Count)).Where(i => all[i].Side == side && (_show.All[i].Alive || side == Psiv.Session.Side.Party));
		IEnumerable<int> result = parts[0] switch
		{
			"actor" when n >= 0 => n < actors.Count ? [actors[n]] : [],
			"actor" => actors,
			"user" => actors.Take(1),
			"targets" => a.Targets,
			"target" => a.Targets.Skip(n).Take(1),
			"hit_targets" => a.Targets.Where(t => a.Events.Any(e => e.Type == BattleEventType.DamageDealt && e.Target == t)),
			"fallen_targets" => a.Targets.Where(t => a.Events.Any(e => e.Type == BattleEventType.CombatantDefeated && e.Target == t)),
			"party" => Side(Psiv.Session.Side.Party),
			"enemies" => Side(Psiv.Session.Side.Enemy),
			"all" => Side(Psiv.Session.Side.Party).Concat(Side(Psiv.Session.Side.Enemy)),
			"party_others" => Side(Psiv.Session.Side.Party).Where(i => !actors.Contains(i)),
			"enemies_others" => Side(Psiv.Session.Side.Enemy).Where(i => !actors.Contains(i)),
			"allies" => actors.Count == 0 || actors[0] >= all.Count ? [] : Side(all[actors[0]].Side).Where(i => !actors.Contains(i)),
			"changed" or "previous_form" => [a.Changed],
			"from" => [a.From],
			"to" => [a.To],
			"self" => [a.Self],
			"place" => Enumerable.Range(0, _show.All.Count).Where(i => _show.All[i].Position == n && _show.All[i].Alive).Take(1),
			"slot" => n < a.PartyCount ? [n] : [],
			_ => [],
		};
		return [.. result.Where(i => i >= 0)];
	}

	// Whether a track's `if` holds for the run.
	private bool Holds(AnimRun run, JsonObject def)
	{
		if (def["layout"] is JsonNode layout && BattleAnimationRun.Str(layout) != (run.In.Front ? "front" : "side"))
		{
			return false;
		}
		IEnumerable<JsonNode?> conditions = def["if"] is JsonArray list ? (IEnumerable<JsonNode?>)list : def["if"] is JsonNode one ? [one] : [];
		foreach (JsonNode? c in conditions)
		{
			if (c is JsonValue bv && bv.TryGetValue(out bool literal))
			{
				if (!literal)
				{
					return false;
				}
				continue;
			}
			string s = BattleAnimationRun.Str(c) ?? "";
			bool not = s.StartsWith('!');
			if (Condition(run, s.TrimStart('!')) == not)
			{
				return false;
			}
		}
		return true;
	}

	private bool Condition(AnimRun run, string name)
	{
		IReadOnlyList<BattleEvent> ev = run.In.Events;
		bool Damage(Effectiveness e) => ev.Any(x => x.Type == BattleEventType.DamageDealt && x.Effectiveness == e);
		return name switch
		{
			"hit" => ev.Any(e => e.Type == BattleEventType.DamageDealt),
			"miss" => ev.Any(e => e.Type == BattleEventType.Missed),
			"critical" => ev.Any(e => e.Type == BattleEventType.Critical),
			"weak" => Damage(Effectiveness.Weak),
			"resisted" => Damage(Effectiveness.Resisted),
			"immune" => Damage(Effectiveness.Immune),
			"defeat" => ev.Any(e => e.Type == BattleEventType.CombatantDefeated),
			"multi" => run.In.Targets.Count() > 1,
			"single" => run.In.Targets.Count() <= 1,
			"party_actor" => run.In.Actors.FirstOrDefault(-1) is >= 0 and var a && a < run.In.PartyCount,
			"enemy_actor" => run.In.Actors.FirstOrDefault(-1) >= run.In.PartyCount,
			"vehicle" => run.In.Vehicle,
			"combo" => run.In.Combo,
			"escaped" => run.In.Turn?.Escaped == true,
			"layout:front" => run.In.Front,
			"layout:side" => !run.In.Front,
			"result_step" => _battle.Phase == BattlePhase.Ended,
			"transition_playing" => TransitionPlaying,
			"$look.flash" => run.In.Look?.Flash == true,
			_ when name.StartsWith("element:", StringComparison.Ordinal) => ev.Any(e => e.Type == BattleEventType.DamageDealt && GameSession.NameOf(NameKind.Element, (int)e.Element) == name[8..]),
			_ => false,
		};
	}

	private static long Frames(long units) => units / BattleClock.Frame;

	// The number style a floating result takes.
	private static string NumberKind(BattleEvent e, BattleEvent? previous) => e.Type switch
	{
		BattleEventType.DamageDealt when previous is { Type: BattleEventType.Critical } => "critical",
		BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Immune => "immune",
		BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Weak => "weak",
		BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Resisted => "resisted",
		BattleEventType.DamageDealt => "damage",
		BattleEventType.Missed => "miss",
		BattleEventType.Healed when e.Resource == ResourceKind.Tp => "tp",
		BattleEventType.Healed => "heal",
		BattleEventType.Revived => "revive",
		BattleEventType.StatusApplied => "status",
		BattleEventType.StatusResisted => "status_resisted",
		BattleEventType.StatusRemoved => "status_ends",
		BattleEventType.StatModified => e.Amount > 0 ? "stat_up" : e.Amount < 0 ? "stat_down" : "stat_normal",
		BattleEventType.ItemHadNoEffect => "no_effect",
		BattleEventType.Message when e.Message == "battle.instant_defeat" => "felled",
		_ => "felling_resisted",
	};

	private void AnimLog(string entry) => _animLog.Add(entry);

	// A step starts: its log lines (all, or with release log timing only its own), its cues, then what it releases and
	// its script's sounds at its first frame.
	private void PlayStep(BattleStep step, bool quiet)
	{
		PresentedSteps.Add(step.Description);
		_log("present: " + step.Description);
		bool byRelease = Anim.LogRelease;
		for (int i = 0; i < step.Lines.Count; i++)
		{
			if (!byRelease || step.LineEvents.ElementAtOrDefault(i) < 0)
			{
				_battle.Log.Add(step.Lines[i]);
				_log("battle: " + step.Lines[i]);
			}
		}
		if (!quiet)
		{
			foreach (string cue in step.Cues)
			{
				_audio.PlayCue(cue);
			}
		}
		if (step.Run is { } run)
		{
			if (step.At == 0 && (run.Kind != "turn" || run.Phases.First(p => p.Present).Name == PhaseOf(step.Kind)))
			{
				AnimLog($"start {run.ScriptId} ({run.Key}) @0");
			}
			if (run.Kind == "turn")
			{
				AnimLog($"phase {PhaseOf(step.Kind)} @{Frames(step.At)}");
			}
		}
		if (step.Kind == StepKind.Sync)
		{
			_show.Sync(step, StageCombatants());
			return;
		}
		if (step.Kind == StepKind.Round)
		{
			StatusTicks(step);
		}
		ReleaseDue(step, 0, quiet);
		if (!quiet)
		{
			Sounds(step, 0, 0, inclusive: true);
		}
	}

	// The step went from `from` to `to` clock units by playing.
	private void PlayOn(BattleStep step, long from, long to, bool quiet)
	{
		if (to <= from)
		{
			return;
		}
		ReleaseDue(step, to, quiet);
		if (!quiet)
		{
			Sounds(step, from, to, inclusive: false);
		}
	}

	// The step ended by playing to its end: what it still holds is released, forced when its frame lies past the end.
	private void FinishStep(BattleStep step, long from, bool quiet)
	{
		if (!quiet && step.Length > from)
		{
			Sounds(step, from, step.Length - 1, inclusive: false);
		}
		ReleaseDue(step, step.Length - 1, quiet);
		foreach (ReleaseUnit u in step.Units.Where(u => !u.Released))
		{
			u.Forced = true;
			Release(step, u, quiet, step.Length);
		}
		if (step.Run is { } run && step.At + step.Length >= run.Total && run.Kind is "turn" or "step" or "change")
		{
			AnimLog($"end @{Frames(run.Total)}");
		}
	}

	// Confirm or fast-forward cuts the step: everything left is released now, the event cues of a hurry once each.
	private void CutStep(BattleStep step, long at, bool quiet, string how)
	{
		long scriptAt = step.At + at;
		AnimLog($"{how} @{Frames(scriptAt)}>{Frames(step.At + step.Length)}");
		var played = new HashSet<string>();
		foreach (ReleaseUnit u in step.Units.Where(u => !u.Released))
		{
			Release(step, u, quiet: true, at);
			if (!quiet)
			{
				foreach (string cue in u.Events.Select(e => BattleCue(e, StageInfo()?.PartyCount ?? 0)).OfType<string>().Where(c => EventCueOn(step, c)))
				{
					if (played.Add(cue))
					{
						_audio.PlayCue(cue);
					}
				}
			}
		}
	}

	// A release's event cue plays unless the step's script, else the set's settings, turns it off.
	private bool EventCueOn(BattleStep step, string cue) => !(step.Run?.CuesOff ?? Anim.EventCuesOff).Contains(cue);

	private void ReleaseDue(BattleStep step, long now, bool quiet)
	{
		foreach (ReleaseUnit u in step.Units.Where(u => !u.Released && u.At <= now))
		{
			Release(step, u, quiet, u.At);
		}
	}

	private void Release(BattleStep step, ReleaseUnit unit, bool quiet, long at)
	{
		unit.Released = true;
		long shownAt = Math.Min(at, unit.At);
		if (unit.Forced)
		{
			shownAt = Math.Max(0, step.Length - 1);
		}
		if (Anim.LogRelease)
		{
			for (int i = 0; i < step.Lines.Count; i++)
			{
				int ev = step.LineEvents.ElementAtOrDefault(i);
				if (ev >= 0 && unit.Events.Contains(step.Events[ev]))
				{
					_battle.Log.Add(step.Lines[i]);
					_log("battle: " + step.Lines[i]);
				}
			}
		}
		if (!quiet)
		{
			foreach (string cue in unit.Events.Select(e => BattleCue(e, StageInfo()?.PartyCount ?? 0)).OfType<string>().Where(c => EventCueOn(step, c)))
			{
				_audio.PlayCue(cue);
			}
		}
		if (step.Kind != StepKind.Sync)
		{
			_show.Apply(step, unit.Events, shownAt, StageCombatants(), id => _data.EnemyStatuses.GetValueOrDefault(id) ?? []);
		}
		BattleContext c = Context();
		foreach (BattleEvent e in unit.Events.Where(Shown))
		{
			string detail = Detail(c, e);
			AnimLog(unit.Forced ? $"forced {detail} @{Frames(step.At + step.Length)}" : $"release {detail} @{Frames(step.At + shownAt)}");
		}
		StartReactions(step, unit, shownAt);
		if (!quiet && step.Run is { } run)
		{
			foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "sound" && BattleAnimationRun.Str(t.Def["per"]) == "release" && Holds(run, t.Def)))
			{
				PlaySound(run, t, step.At + shownAt);
			}
		}
	}

	private static IEnumerable<string> ReactionKeys(BattleEvent e, int partyCount)
	{
		string? ev = e.Type switch
		{
			BattleEventType.DamageDealt => "damage",
			BattleEventType.Critical => "critical",
			BattleEventType.Missed => "miss",
			BattleEventType.Healed when e.Resource == ResourceKind.Tp => "tp",
			BattleEventType.Healed => "heal",
			BattleEventType.Revived => "revive",
			BattleEventType.StatusApplied => "status",
			BattleEventType.StatusResisted => "status_resisted",
			BattleEventType.StatusRemoved => "status_ends",
			BattleEventType.StatModified => "stat",
			BattleEventType.CombatantDefeated => "defeat",
			BattleEventType.ItemHadNoEffect => "no_effect",
			BattleEventType.Message when e.Message == "battle.instant_defeat" => "instant_defeat",
			BattleEventType.Message when e.Message.StartsWith("battle.instant_defeat", StringComparison.Ordinal) => "felling_resisted",
			_ => null,
		};
		if (ev is null)
		{
			yield break;
		}
		if (e.Type == BattleEventType.StatusApplied)
		{
			yield return $"react:status.{e.Status}";
		}
		yield return "react:" + ev;
	}

	// Reactions start at a release, on the figure each event lands on; a reaction lasts to its step's end.
	private void StartReactions(BattleStep step, ReleaseUnit unit, long at)
	{
		BattleAnimation set = Anim;
		int partyCount = StageInfo()?.PartyCount ?? 0;
		foreach (BattleEvent e in unit.Events)
		{
			int figure = TargetOf(e);
			if (figure < 0)
			{
				continue;
			}
			string enemy = figure >= partyCount && figure < _show.All.Count ? _show.All[figure].Id : "";
			foreach (string key in ReactionKeys(e, partyCount))
			{
				string? id = key == "react:damage" && _forcedReaction is not null ? _forcedReaction : (enemy.Length > 0 ? set.Find($"{key}.{enemy}") : null) ?? set.Find(key);
				if (id is null)
				{
					continue;
				}
				ActionLook? look = e.Type == BattleEventType.Message && e.Message == "battle.instant_defeat" ? PresentationTable.ChangeFor("instant_defeat", enemy) : step.Turn?.Look;
				AnimInputs inputs = Inputs(step.Turn, look, [e], step.Length);
				inputs.Self = figure;
				AnimRun run = BattleAnimationRun.Instantiate(set, id, key, "reaction", inputs, [], Math.Max(0, step.Length - at));
				step.Reactions.Add(new Reaction { Run = run, Key = key, Figure = figure, Start = at });
				break;
			}
		}
		// The turn script's react tracks: a pose, state or effect on each released target from its release frame.
		if (step.Run is not { } turn)
		{
			return;
		}
		foreach (AnimTrack t in turn.Tracks.Where(t => t.Type == "react" && Holds(turn, t.Def)))
		{
			var who = Subject(turn, BattleAnimationRun.Str(t.Def["targets"]) ?? "targets");
			foreach (int figure in unit.Events.Select(TargetOf).Distinct().Where(who.Contains))
			{
				AnimInputs inputs = Inputs(step.Turn, step.Turn?.Look, unit.Events, step.Length);
				inputs.Self = figure;
				long length = Math.Max(0, step.Length - at);
				var run = new AnimRun { ScriptId = "react", Key = "react", Kind = "reaction", In = inputs, RateMilli = turn.RateMilli, Total = length };
				foreach (string part in new[] { "pose", "state", "effect" }.Where(p => t.Def.ContainsKey(p)))
				{
					var def = (JsonObject)t.Def.DeepClone();
					def["type"] = part == "effect" ? "sprite" : part;
					def["on"] = "self";
					def["spawn"] = "self";
					long len = t.Def["length"] is null ? length : Math.Min(length, BattleAnimationRun.Time(t.Def["length"], run, length));
					run.Tracks.Add(new AnimTrack { Def = def, Type = part == "effect" ? "sprite" : part, Id = t.Id, Index = run.Tracks.Count, Start = 0, Length = len, PhaseLength = length, HasLength = t.Def["length"] is not null });
				}
				step.Reactions.Add(new Reaction { Run = run, Key = "react", Figure = figure, Start = at });
			}
		}
	}

	// Script sounds whose frame the step reached between `from` and `to` (step clock units).
	private void Sounds(BattleStep step, long from, long to, bool inclusive)
	{
		if (step.Run is { } run)
		{
			foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "sound" && BattleAnimationRun.Str(t.Def["per"]) != "release"))
			{
				SoundsOf(run, t, step.At + from, step.At + to, inclusive);
			}
		}
		foreach (Reaction r in step.Reactions)
		{
			foreach (AnimTrack t in r.Run.Tracks.Where(t => t.Type == "sound"))
			{
				SoundsOf(r.Run, t, from - r.Start, to - r.Start, inclusive);
			}
		}
	}

	private void SoundsOf(AnimRun run, AnimTrack t, long from, long to, bool inclusive)
	{
		if (!Holds(run, t.Def))
		{
			return;
		}
		int count = 1;
		long stagger = 0;
		if (BattleAnimationRun.Str(t.Def["per"]) == "each")
		{
			count = Subject(run, BattleAnimationRun.Str(t.Def["spawn"]) ?? "targets").Count;
			stagger = BattleAnimationRun.Time(BattleAnimationRun.Pick(t.Def["stagger"], run.In.Front), run);
		}
		for (int k = 0; k < count; k++)
		{
			long at = t.Start + k * stagger;
			if (inclusive ? at >= from && at <= to : at > from && at <= to)
			{
				PlaySound(run, t, at);
			}
		}
	}

	private void PlaySound(AnimRun run, AnimTrack t, long at)
	{
		string cue = BattleAnimationRun.Str(BattleAnimationRun.Pick(t.Def["cue"], run.In.Front)) ?? "";
		if (cue == "$look.cue")
		{
			cue = run.In.Look?.Cue ?? "";
		}
		if (cue.Length > 0)
		{
			_audio.PlayCue(cue);
			AnimLog($"cue {cue} @{Frames(at)}");
		}
	}

	// The playback's update: the timeline moves (or is cut), and every step it passes gets its sounds and releases.
	private void PlaybackTimeline(int dtMs, bool fast, bool hurry)
	{
		BattleStep? before = _timeline.Current;
		long from = _timeline.Elapsed;
		if (before is not null && (fast || hurry))
		{
			CutStep(before, from, fast, fast ? "skip" : "hurry");
		}
		List<BattleStep> started = fast ? _timeline.SkipAll() : hurry ? _timeline.Hurry() : _timeline.Advance(dtMs, SpeedPercent());
		if (before is not null && !fast && !hurry)
		{
			if (_timeline.Current == before && started.Count == 0)
			{
				PlayOn(before, from, _timeline.Elapsed, quiet: false);
			}
			else
			{
				FinishStep(before, from, quiet: false);
			}
		}
		foreach (BattleStep step in started)
		{
			PlayStep(step, quiet: fast);
			if (fast)
			{
				CutStep(step, 0, quiet: true, "skip");
			}
			else if (step != _timeline.Current)
			{
				FinishStep(step, 0, quiet: false);
			}
			else
			{
				PlayOn(step, 0, _timeline.Elapsed, quiet: false);
			}
		}
	}

	// The animation probes `expect state` reads.
	private string AnimState(string key)
	{
		BattleStep? step = _battle.Phase == BattlePhase.Playback ? _timeline.Current : null;
		AnimRun? run = step?.Run;
		long now = step is null ? 0 : step.At + _timeline.Elapsed;
		return key switch
		{
			"anim" => run is null ? "-" : $"{run.ScriptId} {run.Key} frame {Frames(now)}/{Frames(run.Total)} {(run.Kind == "turn" ? PhaseOf(step!.Kind) : run.Kind == "step" ? "step" : run.Kind)}",
			"animkey" => step is null ? "-" : string.Join(" | ", step.Lookup),
			"animframe" => run is null ? "-" : Frames(now).ToString(CultureInfo.InvariantCulture),
			"animtracks" => run is null ? "-" : string.Join(", ", run.Tracks.Where(t => t.Active(now) && t.Type != "release").Select(t => $"{t.Label} {t.Type} {Frames(now - t.Start)}/{Frames(t.Length)}")
				.Concat(step!.Reactions.SelectMany(r => r.Run.Tracks.Where(t => t.Active(_timeline.Elapsed - r.Start)).Select(t => $"{r.Key}:{t.Label} {t.Type}"))).DefaultIfEmpty("none")),
			"released" => step is null ? "-" : Released(step),
			// Labels hold spaces, which a script's words cannot: `figure.Gel_Mite_A`.
			_ when key.StartsWith("figure.", StringComparison.Ordinal) => _probeFigureLooks.GetValueOrDefault(key[7..].Replace('_', ' '), "-"),
			"transition" => _probeTransition,
			"backdropfx" => _probeBackdropFx,
			_ => "?",
		};
	}

	// A result the release log and probe name: a main result, or a change of the formation.
	private static bool Shown(BattleEvent e) => MainResult(e) || e.Type is BattleEventType.EnemyJoined or BattleEventType.EnemyLeft or BattleEventType.EnemyTransformed;

	private string Released(BattleStep step)
	{
		int total = step.Units.Sum(u => u.Events.Count(Shown));
		var released = step.Units.Where(u => u.Released).ToList();
		int forced = released.Where(u => u.Forced).Sum(u => u.Events.Count(Shown));
		BattleContext c = Context();
		IEnumerable<string> each = released.SelectMany(u => u.Events.Where(Shown).Select(e => $"{Detail(c, e)} @{Frames(step.At + (u.Forced ? step.Length : Math.Min(u.At, step.Length)))}"));
		return $"{released.Sum(u => u.Events.Count(Shown))}/{total}" + (forced > 0 ? $" forced {forced}" : "") + (released.Count > 0 ? " " + string.Join(", ", each) : "");
	}

	private static string Detail(BattleContext c, BattleEvent e) => e.Type switch
	{
		BattleEventType.EnemyJoined => $"{c.FinalLabel(e.Target)} joins",
		BattleEventType.EnemyLeft => $"{c.Label(e.Target)} leaves",
		BattleEventType.EnemyTransformed => $"{c.Label(e.Target)} becomes {c.Name(e.Ids.FirstOrDefault() ?? "")}",
		_ => c.Detail(e),
	};

	public void MarkAnimLog() => _animLogMark = _animLog.Count;

	public IReadOnlyList<string> AnimLogSinceMark => _animLog.Skip(_animLogMark).ToList();
}
