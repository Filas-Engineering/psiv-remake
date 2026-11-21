using Psiv.Session;

namespace Psiv.Sample;

// Join, Merge and Transform show the enemy formation changing; Sync brings the stage to the session's state when a
// developer command ended the battle outside the events.
public enum StepKind { Intro, Opening, Round, Forward, Retarget, Banner, Animate, Effect, Result, Back, Cancel, RoundEnd, Notice, Outcome, Join, Merge, Transform, Sync }

// One action as the stage shows it: who moves (every participant of a combination), whom it reaches, and its look.
public sealed class BattleTurn
{
	public List<int> Actors = [];
	public List<int> Targets = [];
	public ActionLook? Look;
	// The action's name, shown over the turn's steps.
	public string Name = "";
}

// One staged step of a round's playback. The events it presents change what the stage shows when it starts; its log
// lines and cues come out then too.
public sealed class BattleStep
{
	public StepKind Kind;
	public int LengthMs;
	public string Text = "";
	public string Description = "";
	public BattleTurn? Turn;
	public int From = -1;
	public int To = -1;
	// A change of the enemy formation's look, from the presentation table.
	public ActionLook? Change;
	public List<BattleEvent> Events = [];
	public List<string> Lines = [];
	public List<string> Cues = [];
}

// Plays steps on the controller's clock: the current step runs for its length, scaled by the battle speed, and the next
// starts as it ends. Confirm finishes the current step; fast-forward starts and finishes every one left. It only shows
// what the session already resolved.
public sealed class BattleTimeline
{
	private readonly Queue<BattleStep> _pending = new();
	private float _elapsedMs;

	public BattleStep? Current { get; private set; }
	public bool Done => Current is null && _pending.Count == 0;
	// How far through the current step, 0 to 1, and its milliseconds at 1x so far.
	public float Progress => Current is null || Current.LengthMs == 0 ? 1f : Math.Clamp(_elapsedMs / Current.LengthMs, 0f, 1f);
	public int ElapsedMs => (int)_elapsedMs;

	public void Add(IEnumerable<BattleStep> steps)
	{
		foreach (BattleStep step in steps)
		{
			_pending.Enqueue(step);
		}
	}

	public void Clear()
	{
		_pending.Clear();
		Current = null;
		_elapsedMs = 0;
	}

	// The steps that started, in order. `scaledMs` is the frame time times the battle speed.
	public List<BattleStep> Advance(float scaledMs)
	{
		var started = new List<BattleStep>();
		if (Current is null && !Next(started))
		{
			return started;
		}
		_elapsedMs += scaledMs;
		while (Current is not null && _elapsedMs >= Current.LengthMs)
		{
			_elapsedMs -= Current.LengthMs;
			Current = null;
			Next(started);
		}
		return started;
	}

	private bool Next(List<BattleStep> started)
	{
		if (!_pending.TryDequeue(out BattleStep? step))
		{
			_elapsedMs = 0;
			return false;
		}
		Current = step;
		started.Add(step);
		return true;
	}

	public List<BattleStep> Hurry()
	{
		var started = new List<BattleStep>();
		Current = null;
		_elapsedMs = 0;
		Next(started);
		return started;
	}

	public List<BattleStep> SkipAll()
	{
		var started = new List<BattleStep>(_pending);
		_pending.Clear();
		Current = null;
		_elapsedMs = 0;
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
				steps.Add(c.Step(StepKind.Opening, e.Text, "opening " + e.Message["battle.opening.".Length..], [e]));
				break;
			case BattleEventType.Message when e.Message.StartsWith("battle.instant_defeat", StringComparison.Ordinal):
				turn.Add(e);
				break;
			case BattleEventType.Message:
				Flush();
				steps.Add(c.Step(StepKind.Notice, e.Text, "notice " + e.Text, [e]));
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
				steps.Add(c.Step(StepKind.Outcome, c.OutcomeText(e.Outcome), "outcome " + GameSession.NameOf(NameKind.Outcome, (int)e.Outcome), [e]));
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
			yield return c.Step(StepKind.Cancel, text, $"cancel {c.Label(actor)}: {text.TrimEnd('.')}", events, t, down ? 0 : -1);
			yield break;
		}
		if (head.Type == BattleEventType.EscapeAttempted)
		{
			t.Actors = [actor];
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
			BattleStep effect = c.Step(StepKind.Effect, "", $"effect {t.Look.Effect} on {string.Join(", ", t.Targets.Select(c.Label))}", [], t, t.Look.EffectMs);
			if (t.Look.Cue.Length > 0)
			{
				effect.Cues.Add(t.Look.Cue);
			}
			yield return effect;
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
			if (look.Cue.Length > 0)
			{
				step.Cues.Add(look.Cue);
			}
			yield return step;
		}
	}
}

// What building steps needs from the session and the data: names, log lines and cues for events, the presentation
// table, the action shapes and the text tables. `labels` grows and changes with the enemies that join or change form
// as the steps are built, so each step's lines name them as they were then.
public sealed class BattleContext(GameSession session, List<string> labels, BattlePresentation table, GameData data, Func<BattleEvent, string?> cue,
	Func<string, object[], string> text)
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
			}
			if (e.Type == BattleEventType.EnemyTransformed && e.Target < labels.Count)
			{
				labels[e.Target] = FinalLabel(e.Target);
			}
			if (cue(e) is { } sound)
			{
				step.Cues.Add(sound);
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
			BattleEventType.Message when e.Effectiveness == Effectiveness.Immune => $"{Label(e.Target)} immune to felling",
			BattleEventType.Message => $"{Label(e.Target)} resists felling",
			_ => "",
		};
	}
}
