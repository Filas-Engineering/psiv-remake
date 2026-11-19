namespace Psiv.Session;

// One line of plain text per battle event, as the terminal front end phrases them. `labels` are the
// combatant labels (Combatant.Label), by combatant index.
public static class BattleText
{
	public static string? Describe(GameSession session, IReadOnlyList<string> labels, BattleEvent e)
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
			BattleEventType.ItemHadNoEffect => $"  {session.DisplayName(e.Ability)} has no effect and is kept.",
			BattleEventType.Missed => $"  {Name(e.Target)} evades.",
			BattleEventType.Critical => "  Critical hit!",
			BattleEventType.DamageDealt => $"  {Name(e.Target)} takes {e.Amount} damage.",
			BattleEventType.Healed => $"  {Name(e.Target)} recovers {e.Amount}.",
			BattleEventType.Revived => $"  {Name(e.Target)} is revived.",
			BattleEventType.StatusApplied or BattleEventType.StatusResisted or BattleEventType.StatusRemoved => "  " + e.Text,
			BattleEventType.StatusTicked => $"  {Name(e.Target)} suffers {e.Amount} from {session.DisplayName(e.Status)}.",
			BattleEventType.StatModified => $"  {Name(e.Target)}'s {Stat()} {(e.Amount > 0 ? "rises." : "falls.")}",
			BattleEventType.StatModifierExpired => $"  {Name(e.Target)}'s {Stat()} returns to normal.",
			BattleEventType.CombatantDefeated => $"  {Name(e.Target)} falls.",
			BattleEventType.EscapeAttempted => e.Succeeded ? "  The party escapes!" : "  No escape!",
			BattleEventType.BattleEnded => "Outcome: " + GameSession.NameOf(NameKind.Outcome, (int)e.Outcome),
			_ => null,
		};
	}
}
