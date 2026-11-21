using Psiv.Session;

namespace Psiv.Sample;

// A combatant as the stage shows it: who it is, where in the enemy formation it stands, the numbers and statuses the
// steps played so far have reached, and the steps that changed them, so bars, icons, the fall and a change of form
// animate while those steps run.
public sealed class ShownCombatant(Combatant c, int position)
{
	public string Id = c.Id;
	public string Label = c.Label;
	public readonly int Position = position;
	public int Hp = c.Hp;
	public int Tp = c.Tp;
	public int MaxHp = c.MaxHp;
	public int MaxTp = c.MaxTp;
	public bool Alive = c.Alive;
	public readonly List<string> Statuses = [.. c.Statuses.Where(s => s.Id != "defeated").Select(s => s.Id)];
	public int HpFrom = c.Hp;
	public int TpFrom = c.Tp;
	public BattleStep? BarStep;
	public BattleStep? AliveStep;
	// When in its step (clock units) the bar started running and the figure fell or rose.
	public long BarAt;
	public long AliveAt;
	// The step's release that felled it was an instant defeat.
	public bool FelledInstantly;
	// The step that brought this enemy in or changed its form, and the form it had before.
	public BattleStep? ChangeStep;
	public string PreviousId = "";
	public readonly Dictionary<string, (BattleStep Step, long At)> Appeared = [];
	public readonly List<(string Id, BattleStep Step, long At)> Leaving = [];
}

// The battle as the playback has shown it so far. It starts from the session's combatants and changes only by the
// events of each step as it starts, so when a round's playback ends it must match the session again. Enemies that join
// mid-battle come after the others, in a formation position of their own; positions outlive each round's reset.
public sealed class BattleShow
{
	private readonly Dictionary<int, int> _positions = [];
	private int _partyCount;
	private int _startingEnemies;

	public List<ShownCombatant> All { get; private set; } = [];
	// How many formation places the stage spreads the enemies over: the starting ones, and any a joining enemy took.
	public int Slots => Math.Max(_startingEnemies, _positions.Values.DefaultIfEmpty(-1).Max() + 1);

	// `formationSize` counts the formation's free places too; 0 takes one place per starting enemy.
	public void Start(IReadOnlyList<Combatant> combatants, int partyCount, int formationSize = 0)
	{
		_positions.Clear();
		_partyCount = partyCount;
		_startingEnemies = Math.Max(formationSize, combatants.Count - partyCount);
		for (int i = partyCount; i < combatants.Count; i++)
		{
			if (combatants[i].Position >= 0)
			{
				_positions[i] = combatants[i].Position;
			}
		}
		Reset(combatants);
	}

	public void Reset(IReadOnlyList<Combatant> combatants) => All = [.. combatants.Select((c, i) => new ShownCombatant(c, PositionOf(i)))];

	private int PositionOf(int index) => index < _partyCount ? -1 : _positions.TryGetValue(index, out int p) ? p : index - _partyCount;

	// `session` gives a joining or changed enemy's maximums and label, which the events do not carry; `startStatuses`
	// the statuses its form starts with.
	// `events` are the step's events a release lets out at `at` (clock units into the step).
	public void Apply(BattleStep step, IEnumerable<BattleEvent> events, long at, IReadOnlyList<Combatant> session, Func<string, IEnumerable<string>> startStatuses)
	{
		foreach (BattleEvent e in events)
		{
			ShownCombatant? target = e.Target >= 0 && e.Target < All.Count ? All[e.Target] : null;
			ShownCombatant? actor = e.Actor >= 0 && e.Actor < All.Count ? All[e.Actor] : null;
			switch (e.Type)
			{
			case BattleEventType.ResourceSpent when e.Resource == ResourceKind.Tp && actor is not null:
				Bar(actor, step, at);
				actor.Tp = Math.Max(0, actor.Tp - e.Amount);
				break;
			case BattleEventType.Healed when e.Resource == ResourceKind.Tp && target is not null:
				Bar(target, step, at);
				target.Tp = Math.Min(target.MaxTp, target.Tp + e.Amount);
				break;
			case BattleEventType.DamageDealt or BattleEventType.Healed when target is not null:
				Bar(target, step, at);
				target.Hp = e.Hp;
				break;
			case BattleEventType.Revived when target is not null:
				Bar(target, step, at);
				target.Hp = e.Hp;
				target.Alive = true;
				target.AliveStep = step;
				target.AliveAt = at;
				break;
			case BattleEventType.CombatantDefeated when target is not null:
				// An instant defeat fells without a hit, so the bar empties here.
				Bar(target, step, at);
				target.Hp = 0;
				target.Alive = false;
				target.AliveStep = step;
				target.AliveAt = at;
				target.FelledInstantly = step.Events.Any(m => m.Type == BattleEventType.Message && m.Message == "battle.instant_defeat" && m.Target == e.Target);
				break;
			case BattleEventType.EnemyLeft when target is not null:
				target.Hp = 0;
				target.Alive = false;
				target.AliveStep = step;
				target.AliveAt = at;
				break;
			case BattleEventType.EnemyJoined when e.Target == All.Count && e.Target < session.Count:
				_positions[e.Target] = e.Amount;
				All.Add(Changed(session[e.Target], e, step, "", startStatuses));
				break;
			case BattleEventType.EnemyTransformed when target is not null && e.Target < session.Count:
				All[e.Target] = Changed(session[e.Target], e, step, target.Id, startStatuses);
				break;
			case BattleEventType.StatusApplied when target is not null && e.Status != "defeated" && !target.Statuses.Contains(e.Status):
				target.Statuses.Add(e.Status);
				target.Appeared[e.Status] = (step, at);
				break;
			case BattleEventType.StatusRemoved when target is not null && target.Statuses.Remove(e.Status):
				target.Leaving.Add((e.Status, step, at));
				break;
			}
		}
	}

	// An enemy in a new form, or new to the battle: the form's numbers at full, its starting statuses.
	private ShownCombatant Changed(Combatant c, BattleEvent e, BattleStep step, string previous, Func<string, IEnumerable<string>> startStatuses)
	{
		string id = e.Ids.Count > 0 ? e.Ids[0] : c.Id;
		var shown = new ShownCombatant(c, PositionOf(e.Target)) { Id = id, Hp = e.Hp, HpFrom = e.Hp, Tp = c.MaxTp, TpFrom = c.MaxTp, Alive = true, ChangeStep = step, PreviousId = previous };
		shown.Statuses.Clear();
		shown.Statuses.AddRange(startStatuses(id));
		return shown;
	}

	// Brings every combatant to the session's state at once, as the step starts, with the bars, falls and statuses
	// animating over it: for a battle a developer command ended outside the events.
	public void Sync(BattleStep step, IReadOnlyList<Combatant> session)
	{
		for (int i = 0; i < Math.Min(All.Count, session.Count); i++)
		{
			ShownCombatant s = All[i];
			Combatant c = session[i];
			if (s.Hp != c.Hp || s.Tp != c.Tp)
			{
				Bar(s, step, 0);
				s.Hp = c.Hp;
				s.Tp = c.Tp;
			}
			if (s.Alive != c.Alive)
			{
				s.Alive = c.Alive;
				s.AliveStep = step;
				s.AliveAt = 0;
			}
			var statuses = c.Statuses.Where(x => x.Id != "defeated").Select(x => x.Id).ToList();
			foreach (string gone in s.Statuses.Except(statuses).ToList())
			{
				s.Statuses.Remove(gone);
				s.Leaving.Add((gone, step, 0));
			}
			foreach (string added in statuses.Except(s.Statuses).ToList())
			{
				s.Statuses.Add(added);
				s.Appeared[added] = (step, 0);
			}
		}
	}

	private static void Bar(ShownCombatant c, BattleStep step, long at)
	{
		if (c.BarStep != step || c.BarAt != at)
		{
			c.HpFrom = c.Hp;
			c.TpFrom = c.Tp;
			c.BarStep = step;
			c.BarAt = at;
		}
	}

	// What the shown battle has that the session does not, "" when they agree.
	public string Differences(IReadOnlyList<Combatant> session, IReadOnlyList<string> labels)
	{
		var differences = new List<string>();
		if (All.Count != session.Count)
		{
			differences.Add($"{All.Count} combatants shown, the session has {session.Count}");
		}
		for (int i = 0; i < Math.Min(All.Count, session.Count); i++)
		{
			ShownCombatant s = All[i];
			Combatant c = session[i];
			string statuses = string.Join(" ", c.Statuses.Where(x => x.Id != "defeated").Select(x => x.Id).Order(StringComparer.Ordinal));
			string shown = string.Join(" ", s.Statuses.Order(StringComparer.Ordinal));
			if (s.Id != c.Id || s.Hp != c.Hp || s.Tp != c.Tp || s.Alive != c.Alive || statuses != shown)
			{
				differences.Add($"{(i < labels.Count ? labels[i] : c.Label)} shows {s.Id} {s.Hp} HP {s.Tp} TP {(s.Alive ? "up" : "down")} [{shown}], the session has {c.Id} {c.Hp} HP {c.Tp} TP {(c.Alive ? "up" : "down")} [{statuses}]");
			}
		}
		return string.Join("; ", differences);
	}
}
