using Psiv.Session;

namespace Psiv.Sample;

public enum StepKind { Intro, Opening, Round, Forward, Retarget, Banner, Animate, Effect, Result, Back, Cancel, RoundEnd, Notice, Outcome }

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
			case BattleEventType.Message:
				Flush();
				steps.Add(c.Step(StepKind.Notice, e.Text, "notice " + e.Text, [e]));
				break;
			case BattleEventType.RoundStarted:
				steps.Add(c.Step(StepKind.Round, $"Round {e.Round}", $"round {e.Round}", [e]));
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
				steps.Add(c.Step(StepKind.Outcome, OutcomeText(e.Outcome), "outcome " + GameSession.NameOf(NameKind.Outcome, (int)e.Outcome), [e]));
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

	public static string OutcomeText(Outcome outcome) => outcome switch
	{
		Outcome.Victory => "Victory!",
		Outcome.Defeat => "Defeated...",
		Outcome.Escaped => "Got away.",
		Outcome.ScriptedEnd => "The battle ends.",
		_ => "",
	};

	private static IEnumerable<BattleStep> Turn(List<BattleEvent> events, Dictionary<int, RecordedCommand> commands, BattleContext c)
	{
		BattleEvent? head = events.FirstOrDefault(x => x.Type is BattleEventType.ActionStarted or BattleEventType.ActionCancelled or BattleEventType.ComboTriggered or BattleEventType.EscapeAttempted);
		int actor = head?.Actor ?? events[0].Actor;
		var t = new BattleTurn();
		// Retargets, then the action's name with what it cost, then its results: the order the events came in.
		List<BattleEvent> retargets = [], heads = [], results = [];
		foreach (BattleEvent e in events)
		{
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
			yield return c.Step(StepKind.Banner, "Escape", "banner Escape", heads, t);
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
		t.Look = c.Table.For(head.Command == CommandKind.Defend ? "" : head.Ability, shape);
		if (head.Command == CommandKind.Defend)
		{
			t.Targets = [actor];
		}
		// A combination names each participant with the ability it put in, from the round's locked commands, since the
		// event lists only the participants.
		string name = head.Type == BattleEventType.ComboTriggered
			? $"{c.Name(head.Ability)}: " + string.Join(" + ", head.Participants.Select(p =>
				c.Label(p) + (commands.TryGetValue(p, out RecordedCommand? part) && part.Id.Length > 0 ? $" ({c.Name(part.Id)})" : "")))
			: head.Command switch { CommandKind.Defend => "Defend", CommandKind.Attack => "Attack", _ => c.Name(head.Ability) };
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
		if (results.Count > 0 || head.Command == CommandKind.Defend)
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
		yield return c.Step(StepKind.Back, "", "back " + actors, [], t);
	}
}

// What building steps needs from the session and the data: names, log lines and cues for events, the presentation
// table and the action shapes.
public sealed class BattleContext(GameSession session, IReadOnlyList<string> labels, BattlePresentation table, GameData data, Func<BattleEvent, string?> cue)
{
	public BattlePresentation Table => table;
	public GameData Data => data;

	public string Label(int index) => index >= 0 && index < labels.Count ? labels[index] : "?";

	public string Name(string id) => session.DisplayName(id);

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
			if (BattleText.Describe(session, labels, e) is { } line)
			{
				step.Lines.Add(line);
			}
			if (cue(e) is { } sound)
			{
				step.Cues.Add(sound);
			}
		}
		return step;
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
			BattleEventType.Healed => $"{Label(e.Target)} +{e.Amount}",
			BattleEventType.Revived => $"{Label(e.Target)} revived +{e.Amount}",
			BattleEventType.StatusApplied => $"{Label(e.Target)} +{e.Status}",
			BattleEventType.StatusResisted when e.Status.Length == 0 => $"{Label(e.Target)} resists {stat}",
			BattleEventType.StatusResisted => $"{Label(e.Target)} {(e.Effectiveness == Effectiveness.Immune ? "immune to" : "resists")} {e.Status}",
			BattleEventType.StatusRemoved => $"{Label(e.Target)} -{e.Status}",
			BattleEventType.StatModified => $"{Label(e.Target)} {stat}{(e.Amount > 0 ? "+" : "-")}",
			BattleEventType.CombatantDefeated => $"{Label(e.Target)} falls",
			BattleEventType.ItemHadNoEffect => $"{Name(e.Ability)} no effect",
			BattleEventType.EscapeAttempted => e.Succeeded ? "the party escapes" : "no escape",
			_ => "",
		};
	}
}
