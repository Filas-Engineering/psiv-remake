namespace Psiv.Session;

// Enum values match psiv_session.h, which matches the C++ libraries.

public enum PsivStatus { Ok, Refused, InvalidArgument, NotFound, DataError, InternalError }
public enum Mode { Title, Field, Event, Battle, Menu, Shop }
public enum MenuKind { None, Field, Inn, SavePrompt, NameEntry }
public enum Direction { None = -1, North, East, South, West }
public enum NoticeKind { Line, Choice, Action, Notice, Warning, Autosave }
public enum AreaKind { Town, Field, Dungeon }
public enum EncounterMode { Visible, Random }
public enum EnemyState { Idle, Chasing, Returning }
public enum PointKind { Talk, Step, Chest, Shop, Rest }
public enum JobState { Available, Accepted, Completed }
public enum TextSpeed { Slow, Normal, Fast, Instant }
public enum BattleSpeed { X1, X1_5, X2, X3 }
public enum RewardMultiplier { X1, X1_5, X2 }
public enum BattleSource { Visible, Random, Event }
public enum Side { Party, Enemy }
public enum CommandKind { None, Attack, Ability, Item, Defend, Escape }
public enum AbilityKind { Attack, Technique, Skill, Combo }
public enum Targeting { OneEnemy, EnemyGroup, AllEnemies, OneAlly, AllAllies, Self, OneFallenAlly, RandomEnemy }
public enum EquipSlot { Weapon, Head, Body, Arms, Accessory }
public enum Outcome { Ongoing, Victory, Defeat, Escaped, ScriptedEnd }

public enum BattleEventType
{
	BattleStarted, RoundStarted, CommandsLocked, OrderResolved, Retargeted, ActionStarted, ActionCancelled, ComboTriggered,
	ComboDiscovered, ResourceSpent, ItemHadNoEffect, Hit, Missed, Critical, DamageDealt, Healed, Revived, StatusApplied,
	StatusResisted, StatusRemoved, StatModified, CombatantDefeated, EscapeAttempted, Message, RoundEnded, BattleEnded,
}

public enum CancelReason { None, ActorUnable, Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget }
public enum Effectiveness { Normal, Weak, Resisted, Immune }
public enum StatusEnd { Cured, Duration, Recovered, WokeOnDamage, Defeated }
public enum ResourceKind { None, Tp, SkillUse, Item }
public enum Element { None, Heat, Chill, Shock, Gale }
public enum Stat { Strength, Mental, Agility, Dexterity, Defense }
public enum Opening { Normal, PartyFirst, EnemyFirst }
public enum AutoStop { None, Cancelled, MemberFell, CannotPay, BattleEnded }
public enum MacroStepStatus { Applied, Retargeted, FellBack, Skipped }

public enum MacroIssue
{
	None, NotInParty, ActorDefeated, CaughtOffGuard, CannotAct, ForcedByStatus, UnknownAbility, Silenced, NotEnoughTp,
	NoUsesLeft, NoItem, EscapeNotAllowed, TargetGone, NoTarget,
}

public enum ActionType
{
	Move, Face, Animate, ShowActor, HideActor, PanelShow, PanelHide, PanelClear, CameraCut, CameraMove, FadeIn, FadeOut,
	Shake, MusicPlay, MusicStop, MusicFade, AmbientPlay, AmbientStop, Sound, Warp,
}

public enum ActionPhase { Begin, Cancel }
public enum Presentation { Instant, Wait }
public enum WalkStatus { Walking, Arrived, LeftArea, Stopped, NoPath, TooLong }

[Flags]
public enum CellFlags : byte { Floor = 0, Wall = 1, Water = 2, TerrainMask = 3, Walkable = 16 }

// The kinds psiv_name_of knows, for the stable names of enum values.
public enum NameKind
{
	Mode, MenuKind, Direction, NoticeKind, AreaKind, EncounterMode, EnemyState, PointKind, JobState, TextSpeed, BattleSpeed,
	RewardMultiplier, BattleSource, Side, CommandKind, AbilityKind, Targeting, EquipSlot, Outcome, EventType, CancelReason,
	Effectiveness, StatusEnd, ResourceKind, Element, Stat, Opening, AutoStop, MacroStepStatus, MacroIssue, ActionType,
}

public sealed record PsivError(PsivStatus Status, string Key, IReadOnlyList<string> Params, string Text)
{
	public override string ToString() => $"{Status}: {Text} ({Key})";
}

public sealed class PsivException(PsivError error) : Exception(error.Text)
{
	public PsivError Error { get; } = error;
}

// What a game operation returns. A refusal is ordinary play (wrong mode, not enough money); anything worse
// (bad arguments, internal errors) is thrown as PsivException instead.
public readonly record struct PsivResult(PsivError? Error)
{
	public static readonly PsivResult Success = new(null);
	public bool Ok => Error is null;

	public void ThrowIfFailed()
	{
		if (Error is not null)
		{
			throw new PsivException(Error);
		}
	}
}

public sealed record SessionOptions
{
	public required string DataDirectory { get; init; }
	// Core's battle data; null uses game.json's core_data, relative to DataDirectory.
	public string? CoreDataDirectory { get; init; }
	public string? SaveDirectory { get; init; }
	public ulong Seed { get; init; } = 1;
	public EncounterMode? EncounterMode { get; init; }
	public Presentation Presentation { get; init; } = Presentation.Instant;
	public bool WallClock { get; init; }
}

public sealed record GameInfo(string Title, string GameBuild, int RulesVersion, int TickMs, int RoundMs, int ManualSlots, int AutosaveSlots, int ItemCap, int MoneyCap, IReadOnlyList<string> Languages);

public sealed record Profile(TextSpeed TextSpeed, bool AutoAdvance, bool ShowShippedNames, string TextLanguage, int TextSizePercent, BattleSpeed BattleSpeed,
	bool AmbientTalk, bool ReducedFlash, int MusicVolume, int AmbientVolume, int EffectsVolume);

public sealed record Settings(string Difficulty, RewardMultiplier RewardMultiplier, string AutoMacro);

public sealed record SaveSummary(string Slot, string Area, long PlayTimeMs, IReadOnlyList<string> Party, string Difficulty, long WriteTime, ulong Sequence, bool Autosave, bool Damaged, string Error);

public sealed record WorldInfo(Mode Mode, MenuKind MenuKind, int Money, long PlayTimeMs, int AutosaveCount, bool GameOver, bool CanRetry, bool PlayerControl);

public sealed record FieldInfo(string AreaId, string AreaName, AreaKind AreaKind, EncounterMode EncounterMode, int Width, int Height, int StepMs,
	int LeaderX, int LeaderY, Direction Facing, bool PlayerControl, int GraceMs);

public sealed record FieldEnemy(int Id, string Encounter, string Zone, int X, int Y, Direction Facing, EnemyState State);

public sealed record Marker(string Name, int X, int Y);

public sealed record FieldExit(int X, int Y, string ToArea, string ToMarker);

public sealed record FieldPoint(PointKind Kind, string Id, int X, int Y, string Event, bool Active, int Cost);

// A snapshot of everything a field renderer draws, read in one go after update.
public sealed record FieldView(FieldInfo Info, IReadOnlyList<CellFlags> Grid, IReadOnlyList<FieldEnemy> Enemies, IReadOnlyList<Marker> Markers,
	IReadOnlyList<FieldExit> Exits, IReadOnlyList<FieldPoint> Points)
{
	public CellFlags Cell(int x, int y) => x < 0 || y < 0 || x >= Info.Width || y >= Info.Height ? CellFlags.Wall : Grid[y * Info.Width + x];
}

public sealed record Line(string SpeakerId, string SpeakerName, string TextId, string Text, string Portrait, string Display);

public sealed record Choice(string Prompt, bool YesNo, IReadOnlyList<string> Options);

public sealed record PresentationAction(ulong Handle, ActionPhase Phase, ActionType Type, string Subject, string Target, string Slot, bool HasPosition,
	double X, double Y, double Z, double Seconds, double Speed, double Strength, bool Loop, bool Instant, string Description);

public sealed record Notice(NoticeKind Kind, string Key, IReadOnlyList<string> Params, string Text);

public sealed record ActiveStatus(string Id, string Name, int Remaining);

public sealed record Combatant(int Index, string Id, string Name, string Label, Side Side, string Group, int Hp, int MaxHp, int Tp, int MaxTp, bool Alive,
	bool Defending, IReadOnlyList<ActiveStatus> Statuses, CommandKind CommandKind, string CommandId, int CommandTarget);

public sealed record BattleInfo(bool Live, string Encounter, BattleSource Source, int Round, int PartyCount, int CombatantCount, Outcome Outcome,
	bool EscapeAllowed, bool AutoActive, bool PartyActsNextRound, bool GameOver);

public sealed record BattleOption(string Id, string Name, CommandKind Command, AbilityKind? AbilityKind, Targeting Targeting, int Cost, int Uses, CancelReason Unusable)
{
	public bool Usable => Unusable == CancelReason.None;
}

public sealed record MacroStepResult(string Member, int Actor, MacroStepStatus Status, MacroIssue Issue, CommandKind CommandKind, string CommandId, int CommandTarget);

public sealed record RecordedCommand(int Actor, CommandKind Kind, string Id, int Target);

public sealed record BattleEvent(
	BattleEventType Type, int Round, int Actor, int Target, int PreviousTarget, string Ability, string Combo, string Status, string Message,
	IReadOnlyList<string> Params, string Text, int Amount, int Hp, int Duration, CommandKind Command, Element Element, Effectiveness Effectiveness,
	Stat Stat, CancelReason Cancel, StatusEnd StatusEnd, Outcome Outcome, ResourceKind Resource, Opening Opening, bool Succeeded, bool GameOver,
	int Experience, int Currency, IReadOnlyDictionary<string, int> Items, string Macro, IReadOnlyList<int> Participants, IReadOnlyList<int> Order,
	IReadOnlyList<string> Ids, IReadOnlyList<string> Groups, int PartyCount, bool EscapeAllowed, bool DefeatHasBranch, bool VehicleBattle,
	IReadOnlyList<RecordedCommand> Commands);

public sealed record Member(string Id, string Name, bool InLineup, int Level, int Experience, int Hp, int MaxHp, int Tp, int MaxTp, int Strength, int Mental,
	int Agility, int Dexterity, int Defense, IReadOnlyList<ActiveStatus> Statuses, IReadOnlyList<string> Equipment, IReadOnlyList<string> EquipmentNames);

public sealed record AbilityInfo(string Id, string Name, AbilityKind Kind, Targeting Targeting, int Cost, bool Field);

public sealed record InventoryEntry(string Id, string Name, int Count, bool IsEquipment, bool Field, int TargetingOrSlot, int Price)
{
	public Targeting Targeting => (Targeting)TargetingOrSlot;
	public EquipSlot Slot => (EquipSlot)TargetingOrSlot;
}

public sealed record ShopInfo(string Id, int SellPercent, IReadOnlyList<InventoryEntry> Stock);

public sealed record InnInfo(string Id, int Cost);

public sealed record Job(string Id, string Title, JobState State);

public sealed record QuestLog(string Objective, IReadOnlyList<Job> Jobs);

public sealed record CodexEntry(string Id, string Title, string Text);

public sealed record GlossaryEntry(string Id, string Restored, string Shipped);

// One data load problem: where, and a text key with its parameters.
public sealed record DataProblem(string File, int Line, string Entry, string Key, IReadOnlyList<string> Params, string Text);

public sealed class PsivDataException(PsivError error, IReadOnlyList<DataProblem> problems) : Exception(error.Text)
{
	public PsivError Error { get; } = error;
	public IReadOnlyList<DataProblem> Problems { get; } = problems;
}

public sealed record LevelUp(string Member, string Name, int Level, IReadOnlyList<string> Learned);

// What an ended battle paid out; zero and empty unless the party won or reached a scripted end.
public sealed record BattleResult(string Encounter, BattleSource Source, Outcome Outcome, bool GameOver, int Experience, int Money,
	IReadOnlyDictionary<string, int> Drops, IReadOnlyList<LevelUp> LevelUps);

public sealed record AutoRound(IReadOnlyList<Notice> Notices, IReadOnlyList<BattleEvent> Events);

// Refusal is why auto-battle with the auto macro did not start, or null.
public sealed record AutoBattle(PsivError? Refusal, IReadOnlyList<AutoRound> Rounds, bool Ended);

public sealed record Replay(IReadOnlyList<BattleEvent> Events, string EventText);

public sealed record PacingArea(string Area, int Visits, int Battles, int Steps, long FieldMs, long BattleMs, long EventMs, int Experience, int Money,
	IReadOnlyList<long> GapMs, IReadOnlyList<int> GapSteps);

public sealed record PacingBoss(string Flag, string Encounter, string Area, int AverageLevelX100, long DungeonMs);
