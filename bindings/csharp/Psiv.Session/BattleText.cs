namespace Psiv.Session;

// One line of plain text per battle event, as the terminal front end phrases them. `labels` are the
// combatant labels (Combatant.Label), by combatant index.
public static class BattleText
{
	// An item that changed nothing was used up when its action spends it after the no-effect event at `at`.
	public static bool UsedUp(IReadOnlyList<BattleEvent> events, int at)
	{
		for (int j = at + 1; j < events.Count; j++)
		{
			BattleEvent next = events[j];
			if (next.Type is BattleEventType.ActionStarted or BattleEventType.RoundStarted or BattleEventType.BattleEnded)
			{
				break;
			}
			if (next.Type == BattleEventType.ResourceSpent && next.Resource == ResourceKind.Item && next.Ability == events[at].Ability)
			{
				return true;
			}
		}
		return false;
	}

	public static string? Describe(GameSession session, IReadOnlyList<string> labels, BattleEvent e, bool usedUp = false)
	{
		string Name(int index) => index >= 0 && index < labels.Count ? labels[index] : "?";
		string Stat() => GameSession.NameOf(NameKind.Stat, (int)e.Stat);
		return e.Type switch
		{
			BattleEventType.RoundStarted => $"-- round {e.Round} --",
			BattleEventType.Retargeted => $"{Name(e.Actor)} turns from {Name(e.PreviousTarget)} to {Name(e.Target)}.",
			BattleEventType.ActionStarted when e.Command == CommandKind.Defend => $"{Name(e.Actor)} defends.",
			BattleEventType.ActionStarted when e.Command == CommandKind.Escape => $"{Name(e.Actor)} tries to escape.",
			BattleEventType.ActionStarted => $"{Name(e.Actor)} uses {session.DisplayName(e.Ability)}{(e.Target >= 0 ? " on " + Name(e.Target) : "")}.",
			BattleEventType.ActionCancelled or BattleEventType.Message => e.Text,
			BattleEventType.ComboTriggered => $"Combo: {session.DisplayName(e.Ability)}.",
			BattleEventType.ComboDiscovered => "  A new combination joins the combo log.",
			BattleEventType.ItemHadNoEffect => $"  {session.DisplayName(e.Ability)} has no effect and is {(usedUp ? "used up" : "kept")}.",
			BattleEventType.Missed => $"  {Name(e.Target)} evades.",
			BattleEventType.Critical => "  Critical hit!",
			BattleEventType.DamageDealt => $"  {Name(e.Target)} takes {e.Amount} damage.",
			BattleEventType.Healed => $"  {Name(e.Target)} recovers {e.Amount}.",
			BattleEventType.Revived => $"  {Name(e.Target)} is revived.",
			BattleEventType.StatusApplied or BattleEventType.StatusResisted or BattleEventType.StatusRemoved => "  " + e.Text,
			BattleEventType.StatModified => $"  {Name(e.Target)}'s {Stat()} {(e.Amount > 0 ? "rises." : "falls.")}",
			BattleEventType.CombatantDefeated => $"  {Name(e.Target)} falls.",
			BattleEventType.EscapeAttempted => e.Succeeded ? "  The party escapes!" : "  No escape!",
			BattleEventType.BattleEnded => "Outcome: " + GameSession.NameOf(NameKind.Outcome, (int)e.Outcome),
			_ => null,
		};
	}
}
