using Psiv.Session;

namespace Psiv.Sample;

// A combatant as the stage shows it: the numbers and statuses the steps played so far have reached, and the steps that
// changed them, so bars, icons and the fall animate while those steps run.
public sealed class ShownCombatant(Combatant c)
{
	public int Hp = c.Hp;
	public int Tp = c.Tp;
	public readonly int MaxHp = c.MaxHp;
	public readonly int MaxTp = c.MaxTp;
	public bool Alive = c.Alive;
	public readonly List<string> Statuses = [.. c.Statuses.Where(s => s.Id != "defeated").Select(s => s.Id)];
	public int HpFrom = c.Hp;
	public int TpFrom = c.Tp;
	public BattleStep? BarStep;
	public BattleStep? AliveStep;
	public readonly Dictionary<string, BattleStep> Appeared = [];
	public readonly List<(string Id, BattleStep Step)> Leaving = [];
}

// The battle as the playback has shown it so far. It starts from the session's combatants and changes only by the
// events of each step as it starts, so when a round's playback ends it must match the session again.
public sealed class BattleShow
{
	public List<ShownCombatant> All { get; private set; } = [];

	public void Reset(IReadOnlyList<Combatant> combatants) => All = [.. combatants.Select(c => new ShownCombatant(c))];

	public void Apply(BattleStep step)
	{
		foreach (BattleEvent e in step.Events)
		{
			ShownCombatant? target = e.Target >= 0 && e.Target < All.Count ? All[e.Target] : null;
			ShownCombatant? actor = e.Actor >= 0 && e.Actor < All.Count ? All[e.Actor] : null;
			switch (e.Type)
			{
			case BattleEventType.ResourceSpent when e.Resource == ResourceKind.Tp && actor is not null:
				Bar(actor, step);
				actor.Tp = Math.Max(0, actor.Tp - e.Amount);
				break;
			case BattleEventType.DamageDealt or BattleEventType.Healed when target is not null:
				Bar(target, step);
				target.Hp = e.Hp;
				break;
			case BattleEventType.Revived when target is not null:
				Bar(target, step);
				target.Hp = e.Hp;
				target.Alive = true;
				target.AliveStep = step;
				break;
			case BattleEventType.CombatantDefeated when target is not null:
				target.Alive = false;
				target.AliveStep = step;
				break;
			case BattleEventType.StatusApplied when target is not null && e.Status != "defeated" && !target.Statuses.Contains(e.Status):
				target.Statuses.Add(e.Status);
				target.Appeared[e.Status] = step;
				break;
			case BattleEventType.StatusRemoved when target is not null && target.Statuses.Remove(e.Status):
				target.Leaving.Add((e.Status, step));
				break;
			}
		}
	}

	private static void Bar(ShownCombatant c, BattleStep step)
	{
		if (c.BarStep != step)
		{
			c.HpFrom = c.Hp;
			c.TpFrom = c.Tp;
			c.BarStep = step;
		}
	}

	// What the shown battle has that the session does not, "" when they agree.
	public string Differences(IReadOnlyList<Combatant> session, IReadOnlyList<string> labels)
	{
		var differences = new List<string>();
		for (int i = 0; i < Math.Min(All.Count, session.Count); i++)
		{
			ShownCombatant s = All[i];
			Combatant c = session[i];
			string statuses = string.Join(" ", c.Statuses.Where(x => x.Id != "defeated").Select(x => x.Id).Order(StringComparer.Ordinal));
			string shown = string.Join(" ", s.Statuses.Order(StringComparer.Ordinal));
			if (s.Hp != c.Hp || s.Tp != c.Tp || s.Alive != c.Alive || statuses != shown)
			{
				differences.Add($"{labels[i]} shows {s.Hp} HP {s.Tp} TP {(s.Alive ? "up" : "down")} [{shown}], the session has {c.Hp} HP {c.Tp} TP {(c.Alive ? "up" : "down")} [{statuses}]");
			}
		}
		return string.Join("; ", differences);
	}
}
