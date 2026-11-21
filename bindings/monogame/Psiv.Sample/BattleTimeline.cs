using Psiv.Session;

namespace Psiv.Sample;

// Join, Merge and Transform show the enemy formation changing; Sync brings the stage to the session's state when a
// developer command ended the battle outside the events.
public enum StepKind { Intro, Opening, Round, Forward, Retarget, Banner, Animate, Effect, Result, Back, Cancel, RoundEnd, Notice, Outcome, Join, Merge, Transform, Sync, Transition }

// One action as the stage shows it: who moves (every participant of a combination), whom it reaches, and its look.
public sealed class BattleTurn
{
	public List<int> Actors = [];
	public List<int> Targets = [];
	public ActionLook? Look;
	// The action's name, shown over the turn's steps.
	public string Name = "";
	// What chooses its animation script: the command, the ability or item, the acting enemy's id, the combination's
	// recipe, and the action's shape.
	public CommandKind Command;
	public string Ability = "";
	public string EnemyActor = "";
	public string Recipe = "";
	public bool Combo;
	public bool Escape;
	public bool Escaped;
	public ActionShape? Shape;
}

// Results a script releases together: their frame within the step (clock units from its start) and, once released,
// whether the step's end forced them.
public sealed class ReleaseUnit
{
	public List<BattleEvent> Events = [];
	public int Target = -1;
	public long At;
	public bool Released;
	public bool Forced;
}

// A reaction script playing on one figure from a release, until its step ends.
public sealed class Reaction
{
	public required AnimRun Run;
	public required string Key;
	public int Figure;
	public long Start;
}

// One staged step of a round's playback. The events it presents change what the stage shows when it starts; its log
// lines and cues come out then too.
public sealed class BattleStep
{
	public StepKind Kind;
	public int LengthMs;
	// In clock units (BattleClock); -1 until the timeline takes the step, which then derives it from LengthMs.
	public long Length = -1;
	public string Text = "";
	public string Description = "";
	public BattleTurn? Turn;
	public int From = -1;
	public int To = -1;
	// A change of the enemy formation's look, from the presentation table.
	public ActionLook? Change;
	public List<BattleEvent> Events = [];
	public List<string> Lines = [];
	// Per line, the index of the event it tells of in Events, -1 for the step's own.
	public List<int> LineEvents = [];
	public List<string> Cues = [];
	// The key's detail for step scripts: the opening, notice key, outcome or cancel reason.
	public string Sub = "";
	// The script playing this step and the script time the step starts at; for a retarget or change step inside a turn,
	// the turn's script held at its time.
	public AnimRun? Run;
	public long At;
	public AnimRun? Held;
	public long HeldAt;
	public List<ReleaseUnit> Units = [];
	public List<Reaction> Reactions = [];
	public List<string> Lookup = [];
}

// Plays steps on the controller's clock: the current step runs for its length, scaled by the battle speed, and the next
// starts as it ends. Confirm finishes the current step; fast-forward starts and finishes every one left. It only shows
// what the session already resolved.
public sealed class BattleTimeline
{
	private readonly Queue<BattleStep> _pending = new();
	private long _carry;

	public BattleStep? Current { get; private set; }
	public bool Done => Current is null && _pending.Count == 0;
	// Script frames a second, in thousandths (60000 is 60 Hz); a frame is a million clock units.
	public int RateMilli { get; set; } = BattleClock.DefaultRateMilli;
	// How far through the current step, 0 to 1, its milliseconds at 1x so far, and its clock units so far.
	public float Progress => Current is null || Current.Length == 0 ? 1f : Math.Clamp(BattleClock.Ratio(Elapsed, Current.Length), 0f, 1f);
	public int ElapsedMs => (int)(Elapsed / RateMilli);
	public long Elapsed { get; private set; }
	public IEnumerable<BattleStep> Pending => _pending;

	public void Add(IEnumerable<BattleStep> steps)
	{
		foreach (BattleStep step in steps)
		{
			if (step.Length < 0)
			{
				step.Length = (long)step.LengthMs * RateMilli;
			}
			_pending.Enqueue(step);
		}
	}

	public void Clear()
	{
		_pending.Clear();
		Current = null;
		Elapsed = 0;
		_carry = 0;
	}

	// The steps that started, in order. `speedPercent` is the battle speed (100, 150, 200, 300): the clock moves by the
	// frame's milliseconds times the speed in whole clock units, the remainder carried, so it never rounds.
	public List<BattleStep> Advance(int dtMs, int speedPercent)
	{
		var started = new List<BattleStep>();
		if (Current is null && !Next(started))
		{
			return started;
		}
		long units = (long)dtMs * speedPercent * RateMilli + _carry;
		Elapsed += units / 100;
		_carry = units % 100;
		while (Current is not null && Elapsed >= Current.Length)
		{
			Elapsed -= Current.Length;
			Current = null;
			Next(started);
		}
		return started;
	}

	private bool Next(List<BattleStep> started)
	{
		if (!_pending.TryDequeue(out BattleStep? step))
		{
			Elapsed = 0;
			return false;
		}
		Current = step;
		started.Add(step);
		return true;
	}

	// The previewer's seek: the current step at `elapsed` clock units.
	public void Seek(long elapsed)
	{
		Elapsed = elapsed;
		_carry = 0;
	}

	public List<BattleStep> Hurry()
	{
		var started = new List<BattleStep>();
		Current = null;
		Elapsed = 0;
		Next(started);
		return started;
	}

	public List<BattleStep> SkipAll()
	{
		var started = new List<BattleStep>(_pending);
		_pending.Clear();
		Current = null;
		Elapsed = 0;
		return started;
	}

	// A round's events as steps: banners for the opening and the round, then each turn (an action, a combination, an
	// escape or a cancelled action) as the actor stepping forward, the action's name, its animation, the effect on the
	// targets, the results, and the step back; then what ended with the round, and the outcome.
	public static List<BattleStep> Build(IReadOnlyList<BattleEvent> events, BattleContext c)
	{
		var steps = new List<BattleStep>();
		var commands = new Dictionary<int, RecordedCommand>();
		var roundEnd = new List<BattleEvent>();
		var turn = new List<BattleEvent>();
		void Flush()
		{
			if (turn.Count > 0)
			{
				steps.AddRange(Turn(turn, commands, c));
				turn = [];
			}
		}
		foreach (BattleEvent e in events)
		{
			switch (e.Type)
			{
			case BattleEventType.BattleStarted or BattleEventType.OrderResolved:
				break;
			case BattleEventType.CommandsLocked:
				foreach (RecordedCommand command in e.Commands)
				{
					commands[command.Actor] = command;
				}
				break;
			case BattleEventType.Message when e.Message.StartsWith("battle.opening.", StringComparison.Ordinal):
				BattleStep opening = c.Step(StepKind.Opening, e.Text, "opening " + e.Message["battle.opening.".Length..], [e]);
				opening.Sub = e.Message["battle.opening.".Length..];
				steps.Add(opening);
				break;
			case BattleEventType.Message when e.Message.StartsWith("battle.instant_defeat", StringComparison.Ordinal):
				turn.Add(e);
				break;
			case BattleEventType.Message:
				Flush();
				BattleStep notice = c.Step(StepKind.Notice, e.Text, "notice " + e.Text, [e]);
				notice.Sub = e.Message;
				steps.Add(notice);
				break;
			case BattleEventType.RoundStarted:
				steps.Add(c.Step(StepKind.Round, c.T("ui.battle.round", e.Round), $"round {e.Round}", [e]));
				break;
			case BattleEventType.StatusRemoved when e.StatusEnd is StatusEnd.Recovered or StatusEnd.Duration:
			case BattleEventType.StatModified when e.Actor < 0:
				Flush();
				roundEnd.Add(e);
				break;
			case BattleEventType.RoundEnded:
				Flush();
				if (roundEnd.Count > 0)
				{
					steps.Add(c.Step(StepKind.RoundEnd, "", "roundend " + string.Join(", ", roundEnd.Select(c.Detail).Where(d => d.Length > 0)), roundEnd));
				}
				break;
			case BattleEventType.BattleEnded:
				Flush();
				BattleStep outcome = c.Step(StepKind.Outcome, c.OutcomeText(e.Outcome), "outcome " + GameSession.NameOf(NameKind.Outcome, (int)e.Outcome), [e]);
				outcome.Sub = GameSession.NameOf(NameKind.Outcome, (int)e.Outcome);
				steps.Add(outcome);
				break;
			case BattleEventType.Revived when e.Actor < 0:
				// A fallen machine member standing up after the battle.
				Flush();
				steps.Add(c.Step(StepKind.Result, "", "recover " + c.Detail(e), [e], new BattleTurn { Targets = [e.Target] }));
				break;
			case BattleEventType.Retargeted or BattleEventType.ActionStarted or BattleEventType.ActionCancelled or BattleEventType.ComboTriggered
				or BattleEventType.EscapeAttempted:
				// A turn's head; a Retargeted or ResourceSpent before it from the same actor belongs to it.
				bool headed = turn.Any(x => x.Type is BattleEventType.ActionStarted or BattleEventType.ActionCancelled or BattleEventType.ComboTriggered or BattleEventType.EscapeAttempted);
				if (headed || (turn.Count > 0 && turn[0].Actor != e.Actor))
				{
					Flush();
				}
				turn.Add(e);
				break;
			case BattleEventType.ResourceSpent:
				// A cost belongs to the turn of its actor or of the combination it takes part in; one paid before a turn's
				// head (a silenced technique) starts that turn.
				if (turn.Count > 0 && turn[0].Actor != e.Actor && !turn.Any(x => x.Participants.Contains(e.Actor)))
				{
					Flush();
				}
				turn.Add(e);
				break;
			default:
				turn.Add(e);
				break;
			}
		}
		Flush();
		return steps;
	}

	private static IEnumerable<BattleStep> Turn(List<BattleEvent> events, Dictionary<int, RecordedCommand> commands, BattleContext c)
	{
		BattleEvent? head = events.FirstOrDefault(x => x.Type is BattleEventType.ActionStarted or BattleEventType.ActionCancelled or BattleEventType.ComboTriggered or BattleEventType.EscapeAttempted);
		int actor = head?.Actor ?? events[0].Actor;
		var t = new BattleTurn();
		// Retargets, then the action's name with what it cost, then its results: the order the events came in.
		List<BattleEvent> retargets = [], heads = [], results = [], changes = [];
		foreach (BattleEvent e in events)
		{
			if (e.Type is BattleEventType.EnemyJoined or BattleEventType.EnemyLeft or BattleEventType.EnemyTransformed)
			{
				changes.Add(e);
				continue;
			}
			bool result = e.Type is not (BattleEventType.Retargeted or BattleEventType.ActionStarted or BattleEventType.ComboTriggered or BattleEventType.ComboDiscovered
				or BattleEventType.ResourceSpent);
			(e.Type == BattleEventType.Retargeted ? retargets : result || results.Count > 0 ? results : heads).Add(e);
		}
		if (head is null || head.Type == BattleEventType.ActionCancelled)
		{
			// A cancelled action shows its reason; one cancelled because the actor is down takes no time.
			bool down = head is { Cancel: CancelReason.ActorUnable } && head.Status == "defeated";
			string text = head is null ? "" : head.Text;
			BattleStep cancel = c.Step(StepKind.Cancel, text, $"cancel {c.Label(actor)}: {text.TrimEnd('.')}", events, t, down ? 0 : -1);
			cancel.Sub = head is null ? "" : GameSession.NameOf(NameKind.CancelReason, (int)head.Cancel);
			yield return cancel;
			yield break;
		}
		if (head.Type == BattleEventType.EscapeAttempted)
		{
			t.Actors = [actor];
			t.Escape = true;
			t.Escaped = head.Succeeded;
			yield return c.Step(StepKind.Forward, "", "forward " + c.Label(actor), [], t);
			string escape = c.T("ui.battle.command.escape");
			yield return c.Step(StepKind.Banner, escape, "banner " + escape, heads, t);
			yield return c.Step(StepKind.Result, "", "result " + (head.Succeeded ? "escaped" : "no escape"), results, t);
			if (!head.Succeeded)
			{
				yield return c.Step(StepKind.Back, "", "back " + c.Label(actor), [], t);
			}
			yield break;
		}
		t.Actors = head.Type == BattleEventType.ComboTriggered ? [.. head.Participants] : [actor];
		foreach (BattleEvent r in results.Where(r => r.Target >= 0 && r.Type is not (BattleEventType.ResourceSpent or BattleEventType.ItemHadNoEffect)))
		{
			if (!t.Targets.Contains(r.Target))
			{
				t.Targets.Add(r.Target);
			}
		}
		if (t.Targets.Count == 0 && head.Target >= 0)
		{
			t.Targets.Add(head.Target);
		}
		string element = events.FirstOrDefault(x => x.Type == BattleEventType.DamageDealt) is { } hit ? GameSession.NameOf(NameKind.Element, (int)hit.Element) : "";
		ActionShape shape = head.Command == CommandKind.Defend ? new ActionShape("defend", "damage", "none") : c.Data.ShapeOf(head.Ability, head.Command);
		if (element.Length > 0 && shape.Effect == "damage")
		{
			shape = shape with { Element = element };
		}
		t.Look = c.Table.For(head.Command == CommandKind.Defend ? "" : head.Ability, shape, head.Type == BattleEventType.ComboTriggered ? "" : c.EnemyId(actor),
			head.Command switch { CommandKind.Defend => "defend", CommandKind.Attack => "attack", _ => head.Ability });
		if (head.Command == CommandKind.Defend)
		{
			t.Targets = [actor];
		}
		// A combination names each participant with the ability it put in, from the round's locked commands, since the
		// event lists only the participants.
		string name = head.Type == BattleEventType.ComboTriggered
			? c.T("ui.battle.combo_name", c.Name(head.Ability), string.Join(c.T("ui.battle.combo_join"), head.Participants.Select(p =>
				commands.TryGetValue(p, out RecordedCommand? part) && part.Id.Length > 0 ? c.T("ui.battle.combo_part", c.Label(p), c.Name(part.Id)) : c.Label(p))))
			: head.Command switch { CommandKind.Defend => c.T("ui.battle.command.defend"), CommandKind.Attack => c.T("ui.battle.command.attack"), _ => c.Name(head.Ability) };
		t.Name = name;
		t.Command = head.Command;
		t.Ability = head.Ability;
		t.Shape = shape;
		t.Combo = head.Type == BattleEventType.ComboTriggered;
		t.EnemyActor = t.Combo ? "" : c.EnemyId(actor);
		if (t.Combo)
		{
			var parts = head.Participants.Select(p => commands.TryGetValue(p, out RecordedCommand? part) ? part.Id : "").Order(StringComparer.Ordinal).ToList();
			t.Recipe = c.Data.Recipes.Where(r => r.Value.Result == head.Ability).OrderByDescending(r => r.Value.Abilities.Order(StringComparer.Ordinal).SequenceEqual(parts))
				.Select(r => r.Key).FirstOrDefault() ?? "";
		}
		string actors = string.Join("+", t.Actors.Select(c.Label));
		yield return c.Step(StepKind.Forward, "", "forward " + actors, [], t);
		foreach (BattleEvent r in retargets)
		{
			BattleStep step = c.Step(StepKind.Retarget, "", $"retarget {c.Label(r.Actor)}: {c.Label(r.PreviousTarget)}>{c.Label(r.Target)}", [r], t);
			step.From = r.PreviousTarget;
			step.To = r.Target;
			yield return step;
		}
		yield return c.Step(StepKind.Banner, name, "banner " + name, heads, t);
		yield return c.Step(StepKind.Animate, "", $"animate {actors} {t.Look.Animation}", [], t, t.Look.AnimateMs);
		if (results.Count > 0 || changes.Count > 0 || head.Command == CommandKind.Defend)
		{
			yield return c.Step(StepKind.Effect, "", $"effect {t.Look.Effect} on {string.Join(", ", t.Targets.Select(c.Label))}", [], t, t.Look.EffectMs);
		}
		if (results.Count > 0)
		{
			yield return c.Step(StepKind.Result, "", "result " + string.Join(", ", results.Select(c.Detail).Where(d => d.Length > 0)), results, t);
		}
		foreach (BattleStep change in Changes(changes, c, t))
		{
			yield return change;
		}
		yield return c.Step(StepKind.Back, "", "back " + actors, [], t);
	}

	// The formation's changes after a turn's results, each a step of its own: an enemy that leaves into another and that
	// one's new form are one merge.
	private static IEnumerable<BattleStep> Changes(List<BattleEvent> changes, BattleContext c, BattleTurn t)
	{
		for (int k = 0; k < changes.Count; k++)
		{
			BattleEvent e = changes[k];
			string id = e.Ids.Count > 0 ? e.Ids[0] : "";
			BattleStep step;
			ActionLook look;
			if (e.Type == BattleEventType.EnemyLeft && k + 1 < changes.Count && changes[k + 1] is { Type: BattleEventType.EnemyTransformed } merged && merged.Target == e.Actor)
			{
				string into = merged.Ids.Count > 0 ? merged.Ids[0] : "";
				string text = c.T("ui.battle.merge_banner", c.Label(e.Target), c.Label(e.Actor), c.Name(into));
				look = c.Table.ChangeFor("merge", into);
				step = c.Step(StepKind.Merge, text, $"merge {c.Label(e.Target)}>{c.Label(e.Actor)}: {c.Name(into)}", [e, merged], t, look.EffectMs);
				(step.From, step.To, step.Change) = (e.Target, e.Actor, look);
				k++;
			}
			else if (e.Type == BattleEventType.EnemyTransformed)
			{
				string text = c.T("ui.battle.transform_banner", c.Label(e.Target), c.Name(id));
				look = c.Table.ChangeFor("transform", id);
				step = c.Step(StepKind.Transform, text, $"transform {c.Label(e.Target)}: {c.Name(id)}", [e], t, look.EffectMs);
				(step.To, step.Change) = (e.Target, look);
			}
			else if (e.Type == BattleEventType.EnemyJoined)
			{
				string label = c.FinalLabel(e.Target);
				look = c.Table.ChangeFor("join", id);
				step = c.Step(StepKind.Join, c.T("ui.battle.join_banner", label), $"join {label} at {e.Amount}", [e], t, look.EffectMs);
				(step.To, step.Change) = (e.Target, look);
			}
			else
			{
				look = c.Table.ChangeFor("merge", "");
				step = c.Step(StepKind.Merge, "", $"leave {c.Label(e.Target)}", [e], t, look.EffectMs);
				(step.From, step.Change) = (e.Target, look);
			}
			yield return step;
		}
	}
}

// What building steps needs from the session and the data: names, log lines and cues for events, the presentation
// table, the action shapes and the text tables. `labels` grows and changes with the enemies that join or change form
// as the steps are built, so each step's lines name them as they were then.
public sealed class BattleContext(GameSession session, List<string> labels, BattlePresentation table, GameData data, Func<string, object[], string> text)
{
	private readonly IReadOnlyList<Combatant> _final = session.Combatants();

	public BattlePresentation Table => table;
	public GameData Data => data;

	public string T(string key, params object[] parameters) => text(key, parameters);

	public string Label(int index) => index >= 0 && index < labels.Count ? labels[index] : "?";

	// The label the session gives a combatant after the round.
	public string FinalLabel(int index) => index >= 0 && index < _final.Count ? _final[index].Label : "?";

	// The enemy id of a combatant, "" for a party member.
	public string EnemyId(int index) => index >= 0 && index < _final.Count && _final[index].Side == Side.Enemy ? _final[index].Id : "";

	public string Name(string id) => session.DisplayName(id);

	public string OutcomeText(Outcome outcome) => outcome == Outcome.Ongoing ? "" : T("ui.battle.outcome." + GameSession.NameOf(NameKind.Outcome, (int)outcome));

	// `lengthMs` -1 takes the step kind's length from the table.
	public BattleStep Step(StepKind kind, string text, string description, IEnumerable<BattleEvent> events, BattleTurn? turn = null, int lengthMs = -1)
	{
		var step = new BattleStep
		{
			Kind = kind,
			Text = text,
			Description = description,
			Turn = turn,
			LengthMs = lengthMs >= 0 ? lengthMs : table.Ms(kind.ToString().ToLowerInvariant()),
			Events = [.. events],
		};
		foreach (BattleEvent e in step.Events)
		{
			if (e.Type == BattleEventType.EnemyJoined && e.Target == labels.Count)
			{
				labels.Add(FinalLabel(e.Target));
			}
			if (Describe(e) is { } line)
			{
				step.Lines.Add(line);
				step.LineEvents.Add(step.Events.IndexOf(e));
			}
			if (e.Type == BattleEventType.EnemyTransformed && e.Target < labels.Count)
			{
				labels[e.Target] = FinalLabel(e.Target);
			}
		}
		return step;
	}

	// One log line per event, as the terminal front end phrases them, in the text language.
	private string? Describe(BattleEvent e)
	{
		string Stat() => T("ui.battle.stat." + GameSession.NameOf(NameKind.Stat, (int)e.Stat));
		return e.Type switch
		{
			BattleEventType.RoundStarted => T("ui.battle.log.round", e.Round),
			BattleEventType.Retargeted => T("ui.battle.log.retarget", Label(e.Actor), Label(e.PreviousTarget), Label(e.Target)),
			BattleEventType.ActionStarted when e.Command == CommandKind.Defend => T("ui.battle.log.defends", Label(e.Actor)),
			BattleEventType.ActionStarted when e.Command == CommandKind.Escape => T("ui.battle.log.escape_try", Label(e.Actor)),
			BattleEventType.ActionStarted when e.Target >= 0 => T("ui.battle.log.uses_on", Label(e.Actor), Name(e.Ability), Label(e.Target)),
			BattleEventType.ActionStarted => T("ui.battle.log.uses", Label(e.Actor), Name(e.Ability)),
			BattleEventType.Message when e.Message is "battle.instant_defeat" or "battle.instant_defeat_immune" or "battle.instant_defeat_resisted"
				=> T("ui." + e.Message.Insert("battle.".Length, "log."), Label(e.Target)),
			BattleEventType.Message when e.Message == "battle.self_destruct" => T("ui.battle.log.self_destruct", Label(e.Actor)),
			BattleEventType.ActionCancelled or BattleEventType.Message => e.Text,
			BattleEventType.ComboTriggered => T("ui.battle.log.combo", Name(e.Ability)),
			BattleEventType.ComboDiscovered => T("ui.battle.log.combo_found"),
			BattleEventType.ItemHadNoEffect => T("ui.battle.log.no_effect", Name(e.Ability)),
			BattleEventType.Missed => T("ui.battle.log.evades", Label(e.Target)),
			BattleEventType.Critical => T("ui.battle.log.critical"),
			BattleEventType.DamageDealt => T("ui.battle.log.damage", Label(e.Target), e.Amount),
			BattleEventType.Healed when e.Resource == ResourceKind.Tp => T("ui.battle.log.recovers_tp", Label(e.Target), e.Amount),
			BattleEventType.Healed => T("ui.battle.log.recovers", Label(e.Target), e.Amount),
			BattleEventType.Revived when e.Message.Length > 0 => "  " + e.Text,
			BattleEventType.Revived => T("ui.battle.log.revived", Label(e.Target)),
			BattleEventType.StatusApplied or BattleEventType.StatusResisted or BattleEventType.StatusRemoved => "  " + e.Text,
			BattleEventType.StatModified => T(e.Amount > 0 ? "ui.battle.log.stat_rises" : e.Amount < 0 ? "ui.battle.log.stat_falls" : "ui.battle.log.stat_normal", Label(e.Target), Stat()),
			BattleEventType.CombatantDefeated => T("ui.battle.log.falls", Label(e.Target)),
			BattleEventType.EscapeAttempted => T(e.Succeeded ? "ui.battle.log.escaped" : "ui.battle.log.no_escape"),
			BattleEventType.EnemyJoined => T("ui.battle.log.joined", Label(e.Target)),
			BattleEventType.EnemyLeft when e.Actor < 0 => T("ui.battle.log.gone", Label(e.Target)),
			BattleEventType.EnemyLeft => T("ui.battle.log.left", Label(e.Target), Label(e.Actor)),
			BattleEventType.EnemyTransformed => T("ui.battle.log.transformed", Label(e.Target), Name(e.Ids.Count > 0 ? e.Ids[0] : "")),
			BattleEventType.BattleEnded => T("ui.battle.log.outcome." + GameSession.NameOf(NameKind.Outcome, (int)e.Outcome)),
			_ => null,
		};
	}

	// One result as a test reads it: the target's label, then what happened to it.
	public string Detail(BattleEvent e)
	{
		string tags = (e.Effectiveness switch { Effectiveness.Weak => " weak", Effectiveness.Resisted => " resisted", Effectiveness.Immune => " immune", _ => "" });
		string stat = GameSession.NameOf(NameKind.Stat, (int)e.Stat);
		return e.Type switch
		{
			BattleEventType.Missed => $"{Label(e.Target)} miss",
			BattleEventType.Critical => $"{Label(e.Target)} critical",
			BattleEventType.DamageDealt => $"{Label(e.Target)} -{e.Amount}{tags}",
			BattleEventType.Healed when e.Resource == ResourceKind.Tp => $"{Label(e.Target)} +{e.Amount} TP",
			BattleEventType.Healed => $"{Label(e.Target)} +{e.Amount}",
			BattleEventType.Revived => $"{Label(e.Target)} revived +{e.Amount}",
			BattleEventType.StatusApplied => $"{Label(e.Target)} +{e.Status}",
			BattleEventType.StatusResisted when e.Status.Length == 0 => $"{Label(e.Target)} resists {stat}",
			BattleEventType.StatusResisted => $"{Label(e.Target)} {(e.Effectiveness == Effectiveness.Immune ? "immune to" : "resists")} {e.Status}",
			BattleEventType.StatusRemoved => $"{Label(e.Target)} -{e.Status}",
			BattleEventType.StatModified => $"{Label(e.Target)} {stat}{(e.Amount > 0 ? "+" : e.Amount < 0 ? "-" : "=")}",
			BattleEventType.CombatantDefeated => $"{Label(e.Target)} falls",
			BattleEventType.ItemHadNoEffect => $"{Name(e.Ability)} no effect",
			BattleEventType.EscapeAttempted => e.Succeeded ? "the party escapes" : "no escape",
			BattleEventType.Message when e.Message == "battle.instant_defeat" => $"{Label(e.Target)} felled",
			BattleEventType.Message when e.Message == "battle.self_destruct" => $"{Label(e.Actor)} bursts",
			BattleEventType.Message when e.Message == "battle.removed_beside" => $"{Label(e.Target)} swept away",
			BattleEventType.Message when e.Effectiveness == Effectiveness.Immune => $"{Label(e.Target)} immune to felling",
			BattleEventType.Message => $"{Label(e.Target)} resists felling",
			_ => "",
		};
	}
}

// The battle's clock: a script frame is a million units, so a millisecond at R thousandths of a hertz is R units and
// every millisecond and frame length is whole.
public static class BattleClock
{
	public const int DefaultRateMilli = 60000;
	public const long Frame = 1_000_000;

	// a / b as the float of the reduced fraction, so two lengths in different units that stand in the same ratio give
	// the same float (a step of 180 ms at 16 ms frames reads as 16f / 180f did).
	public static float Ratio(long a, long b)
	{
		if (b == 0)
		{
			return 1f;
		}
		long g = Gcd(Math.Abs(a), Math.Abs(b));
		if (g > 1)
		{
			a /= g;
			b /= g;
		}
		return Math.Abs(a) < (1 << 24) && Math.Abs(b) < (1 << 24) ? (float)a / b : (float)((double)a / b);
	}

	private static long Gcd(long a, long b)
	{
		while (b != 0)
		{
			(a, b) = (b, a % b);
		}
		return a == 0 ? 1 : a;
	}
}
