namespace Psiv.Session;

// Enum values match psiv_session.h, which matches the C++ libraries.

public enum PsivStatus { Ok, Refused, InvalidArgument, NotFound, DataError, InternalError }
public enum Mode { Title, Field, Event, Battle, Menu, Shop }
// Port: a port's destinations (TravelDestinations(TravelKind.Port)).
public enum MenuKind { None, Field, Inn, SavePrompt, NameEntry, Port, Credits }
public enum Direction { None = -1, North, East, South, West }
// Ambient: a party talk line that came up by itself on arrival; it does not stop play.
public enum NoticeKind { Line, Choice, Action, Notice, Warning, Autosave, Ambient }
public enum AreaKind { Town, Field, Dungeon, Vessel }
public enum EncounterMode { Visible, Random }
public enum EnemyState { Idle, Chasing, Returning }
public enum PointKind { Talk, Step, Chest, Shop, Rest, Port, Helm, Gangway, Conveyor }
// What Interact acts on. Talk is an interact hook that is no actor's (a sign); Actor an actor's talk point, or the actor
// where it stands under game.json's `interact: "nearby"`.
public enum InteractKind { Vehicle, Port, Helm, Talk, Actor, Chest, Shop, Rest }
// From a port to another open port, back to a visited town of the region, out of a dungeon.
public enum TravelKind { Port, Town, DungeonExit }
public enum JobState { Available, Accepted, Completed, Missed }
public enum TextSpeed { Slow, Normal, Fast, Instant }
public enum BattleSpeed { X1, X1_5, X2, X3 }
public enum RewardMultiplier { X1, X1_5, X2 }
public enum BattleSource { Visible, Random, Event }
public enum Side { Party, Enemy }
public enum CommandKind { None, Attack, Ability, Item, Defend, Escape }
public enum AbilityKind { Attack, Technique, Skill, Combo }
public enum Targeting { OneEnemy, EnemyGroup, AllEnemies, OneAlly, AllAllies, Self, OneFallenAlly, RandomEnemy }
// Weapon is also the right hand; LeftHand is only a place to wear a weapon or a shield.
public enum EquipSlot { Weapon, Head, Body, Arms, Accessory, Shield, LeftHand }

public enum ShopKind { General, Weapon, Armor, Tool, Gift }
public enum Outcome { Ongoing, Victory, Defeat, Escaped, ScriptedEnd }

public enum BattleEventType
{
	BattleStarted, RoundStarted, CommandsLocked, OrderResolved, Retargeted, ActionStarted, ActionCancelled, ComboTriggered,
	ComboDiscovered, ResourceSpent, ItemHadNoEffect, Hit, Missed, Critical, DamageDealt, Healed, Revived, StatusApplied,
	StatusResisted, StatusRemoved, StatModified, CombatantDefeated, EscapeAttempted, Message, RoundEnded, BattleEnded, EnemyJoined,
	EnemyLeft, EnemyTransformed,
}

public enum CancelReason { None, ActorUnable, Silenced, NotEnoughTp, NoUsesLeft, NoItem, NoTarget }
public enum Effectiveness { Normal, Weak, Resisted, Immune }
public enum StatusEnd { Cured, Duration, Recovered, WokeOnDamage, Defeated }
public enum ResourceKind { None, Tp, SkillUse, Item }
public enum Element { None, Heat, Chill, Shock, Gale, Holy }
public enum CreatureKind { Living, Spirit, Machine }
public enum Stat { Strength, Mental, Agility, Dexterity, Defense, MagicDefense }
public enum Opening { Normal, PartyFirst, EnemyFirst }
public enum AutoStop { None, Cancelled, MemberFell, CannotPay, BattleEnded }
public enum MacroStepStatus { Applied, Retargeted, FellBack, Skipped, Automatic }
// What ApplyMacro does with a step that cannot run as written: BasicAttack is core's default; Automatic gives the member core's
// automatic choice for the round instead (MacroStepStatus.Automatic).
public enum MacroFallback { BasicAttack, Automatic }
public enum MacroTargetKind { Default, Self, Member, Enemy, All }

public enum MacroIssue
{
	None, NotInParty, ActorDefeated, CaughtOffGuard, CannotAct, ForcedByStatus, UnknownAbility, Silenced, NotEnoughTp,
	NoUsesLeft, NoItem, EscapeNotAllowed, TargetGone, NoTarget,
}

public enum ActionType
{
	Move, Face, Animate, ShowActor, HideActor, PanelShow, PanelHide, PanelClear, CameraCut, CameraMove, FadeIn, FadeOut,
	Shake, MusicPlay, MusicStop, MusicFade, AmbientPlay, AmbientStop, Sound, Warp, Place,
}

public enum ActionPhase { Begin, Cancel }
// How a hidden actor left: a plain hide, or the standard animation that hid it.
public enum ActorVanish { None, Faded, Dissolved, BlinkedOut }
// A script's remove: gone until the party next enters an area, or for good.
public enum ActorRemoval { None, UntilReload, ForGood }
// What an animation leaves behind once it has played.
public enum AnimationEnd { None, Shown, Hidden }
public enum Presentation { Instant, Wait }
public enum WalkStatus { Walking, Arrived, LeftArea, Stopped, NoPath, TooLong }
public enum DescriptionKind { Item, Equipment, Ability, Status, Enemy, Difficulty }
// The bag has no order of its own; a sort only lists it.
public enum ItemSort { Id, Kind, Name, Count }
public enum CameraTarget { None, Marker, Follow, Leader }

[Flags]
// Soft ground is crossed by a vehicle only; a barrier is a wall (Wall | Barrier) a vehicle can break through.
public enum CellFlags : byte { Floor = 0, Wall = 1, Water = 2, Soft = 3, TerrainMask = 3, Barrier = 8, Walkable = 16 }

// The kinds psiv_name_of knows, for the stable names of enum values.
public enum NameKind
{
	Mode, MenuKind, Direction, NoticeKind, AreaKind, EncounterMode, EnemyState, PointKind, JobState, TextSpeed, BattleSpeed,
	RewardMultiplier, BattleSource, Side, CommandKind, AbilityKind, Targeting, EquipSlot, Outcome, EventType, CancelReason,
	Effectiveness, StatusEnd, ResourceKind, Element, Stat, Opening, AutoStop, MacroStepStatus, MacroIssue, ActionType, MacroTargetKind,
	DescriptionKind, ItemSort, CameraTarget, TravelKind, DevList, CreatureKind, ShopKind, ActorVanish, ActorRemoval, AnimationEnd,
	InteractKind,
}

// The id lists of DevIds: what the developer commands take. Markers are "area:marker".
public enum DevList { Areas, Markers, Flags, Items, Equipment, Abilities, Enemies, Encounters, Events, Vehicles, Characters }

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

// TitleMusic, VictoryMusic and DefeatMusic are game.json's tracks, "" when the data names none.
public sealed record GameInfo(string Title, string GameBuild, int RulesVersion, int TickMs, int RoundMs, int ManualSlots, int AutosaveSlots, int ItemCap, int MoneyCap,
	IReadOnlyList<string> Languages, string TitleMusic, string VictoryMusic, string DefeatMusic, bool MagicDefense = false, int AffinityDivisor = 100);

// An element the data knows. Element values past Holy are the ones rules.json declares, numbered by Index.
public sealed record ElementInfo(Element Index, string Id, string Name);

// An affinity on the rules' divisor scale (GameInfo.AffinityDivisor is normal damage).
public sealed record Affinity(Element Element, string Id, string Name, int Percent);

public sealed record Profile(TextSpeed TextSpeed, bool AutoAdvance, bool ShowShippedNames, string TextLanguage, int TextSizePercent, BattleSpeed BattleSpeed,
	bool AmbientTalk, bool ReducedFlash, int MusicVolume, int AmbientVolume, int EffectsVolume, bool AlwaysRun = false);

public sealed record Settings(string Difficulty, RewardMultiplier RewardMultiplier, string AutoMacro);

public sealed record Difficulty(string Id, string Name, string Description, int RewardPercent);

// Every preset in id order; CanChange says SetSettings would accept a change now (a game in progress, outside battle).
public sealed record Difficulties(IReadOnlyList<Difficulty> Presets, bool CanChange);

// Dev: developer tools marked the saved game.
public sealed record SaveSummary(string Slot, string Area, long PlayTimeMs, IReadOnlyList<string> Party, string Difficulty, long WriteTime, ulong Sequence, bool Autosave, bool Damaged, string Error,
	bool Dev);

// Dev: developer tools marked the game. Encounters: false while DevEncounters has turned battles off for this session.
public sealed record WorldInfo(Mode Mode, MenuKind MenuKind, int Money, long PlayTimeMs, int AutosaveCount, bool GameOver, bool CanRetry, bool PlayerControl,
	bool Dev, bool Encounters);

// The running event and the source line its interpreter is at.
public sealed record EventPosition(string Event, string File, int Line);
public enum CreditsEntryKind { Heading, Line }
public sealed record CreditsEntry(CreditsEntryKind Kind, string Key, string Text, int Gap, int PauseMs);
// The credits playing (MenuKind.Credits): Shown entries have come up after ElapsedMs of play time; CloseMenu() says they
// finished.
public sealed record CreditsInfo(string Id, string Music, int Shown, long ElapsedMs, long TotalMs, ValueList<CreditsEntry> Entries);

// An encounter zone of the party's area: the rectangle it covers (corners included), its table and its visible spawns.
public sealed record Zone(string Id, int FromX, int FromY, int ToX, int ToY, IReadOnlyList<(string Encounter, int Weight)> Table,
	IReadOnlyList<(int X, int Y)> Spawns);

// Music is the area's track (a script's, else game.json's music.areas by flag, else the area's own), "" when the data names none. Aboard is the vehicle the party rides, "" on foot; LeaderStepMs
// the leader's time per cell now, StepMs on foot, and RunStepMs the same while the run input is held (Update's running).
// ShipPort, in a vessel, is the port the party walked aboard from. StepProgressMs: held input spent toward the next cell.
// ConveyorStepMs and ConveyorProgressMs: the belt under the party, 0 off one. StepMask: where CanStep allows a step, 1 north,
// 2 east, 4 south, 8 west.
public sealed record FieldInfo(string AreaId, string AreaName, AreaKind AreaKind, EncounterMode EncounterMode, int Width, int Height, int StepMs,
	int LeaderX, int LeaderY, Direction Facing, bool PlayerControl, int GraceMs, string Music, string Region, string Aboard, int LeaderStepMs, string ShipPort = "", int RunStepMs = 0,
	int StepProgressMs = 0, int ConveyorStepMs = 0, int ConveyorProgressMs = 0, int StepMask = 0);

// Actor: the reserved name scripts address it by ("enemy.<zone>.<n>"); an action with that subject moves or turns this enemy.
public sealed record FieldEnemy(int Id, string Encounter, string Zone, int X, int Y, Direction Facing, EnemyState State, string Actor = "");

public sealed record Marker(string Name, int X, int Y);

// Open: its flags let the party through. ClosedTile: the map character it is drawn as while closed ('#' unless the data
// names another, a door drawn as floor); FieldRows and the grid show it then.
public sealed record FieldExit(int X, int Y, string ToArea, string ToMarker, bool Open = true, char ClosedTile = '#');

// What Interact would act on now: the cell acted on, where the leader faces to use it (Interact turns it there), Ready
// false when Interact would refuse, Across when used across a counter, Keeper a shop's or rest point's keeper ("" for none).
public sealed record InteractTarget(InteractKind Kind, string Id, int X, int Y, Direction Facing, bool Ready, bool Across, string Keeper = "");

// Active for a shop or a port: open now.
// Direction: for a conveyor cell, where it carries the party. Keeper: a shop's or rest point's keeper, "" for none.
public sealed record FieldPoint(PointKind Kind, string Id, int X, int Y, string Event, bool Active, int Cost, ShopKind ShopKind = ShopKind.General, Direction Direction = Direction.North,
	string Keeper = "");

// One of the game's actors in the party's area. Scripted: a script has moved, turned, shown or hidden it; an
// untouched actor stands at the talk point named after it.
// Solid: it blocks walking on its cell now. Animation: the one-shot animation playing, with its milliseconds left and in
// all; Visible already holds its end state. SpriteEnemy or Look: its figure (a script's `look`, else game.json's), "" for the
// host's own. Wanders: it walks about by itself now (game.json `wander`).
public sealed record FieldActor(string Id, string Name, int X, int Y, Direction Facing, bool Visible, bool Scripted, bool Solid = false,
	ActorVanish Vanish = ActorVanish.None, string Animation = "", int AnimationMs = 0, int AnimationTotalMs = 0, ActorRemoval Removal = ActorRemoval.None,
	string SpriteEnemy = "", string Look = "", bool Wanders = false);

public sealed record RouteCell(int X, int Y);

// A list that equals another with the same items in the same order, so records holding one compare by value.
public sealed class ValueList<T>(IEnumerable<T> items) : IReadOnlyList<T>, IEquatable<ValueList<T>>
{
	private readonly T[] _items = items.ToArray();

	public T this[int index] => _items[index];
	public int Count => _items.Length;
	public IEnumerator<T> GetEnumerator() => ((IEnumerable<T>)_items).GetEnumerator();
	System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => _items.GetEnumerator();
	public bool Equals(ValueList<T>? other) => other is not null && _items.SequenceEqual(other._items);
	public override bool Equals(object? obj) => Equals(obj as ValueList<T>);
	public override int GetHashCode() => _items.Aggregate(_items.Length, (hash, item) => HashCode.Combine(hash, item));
	public override string ToString() => "[" + string.Join(", ", _items) + "]";
}

// An animation scripts may name, with how long one play lasts and what it leaves behind.
public sealed record AnimationInfo(string Id, double Seconds, bool Standard, AnimationEnd End);

// One of the party's vehicles in its area; Aboard: the party rides it, so it stands on the leader's cell.
public sealed record FieldVehicle(string Id, string Name, int X, int Y, Direction Facing, bool Aboard);

// A place a travel can go now: Id is what Travel takes (a port id, else an area id), X and Y where the party lands.
public sealed record TravelDestination(string Id, string Area, string AreaName, string Region, int X, int Y);

// A snapshot of everything a field renderer draws, read in one go after update.
public sealed record FieldView(FieldInfo Info, IReadOnlyList<CellFlags> Grid, IReadOnlyList<FieldEnemy> Enemies, IReadOnlyList<Marker> Markers,
	IReadOnlyList<FieldExit> Exits, IReadOnlyList<FieldPoint> Points, IReadOnlyList<FieldActor> Actors, IReadOnlyList<FieldVehicle> Vehicles)
{
	public CellFlags Cell(int x, int y) => x < 0 || y < 0 || x >= Info.Width || y >= Info.Height ? CellFlags.Wall : Grid[y * Info.Width + x];
}

public sealed record Line(string SpeakerId, string SpeakerName, string TextId, string Text, string Portrait, string Display);

public sealed record Choice(string Prompt, bool YesNo, IReadOnlyList<string> Options);

// Duration is how long the animation lasts by the data (game.json's presentation durations, else Seconds), 0 for none.
public sealed record PresentationAction(ulong Handle, ActionPhase Phase, ActionType Type, string Subject, string Target, string Slot, bool HasPosition,
	double X, double Y, double Z, double Seconds, double Speed, double Strength, bool Loop, bool Instant, string Description, double Duration,
	IReadOnlyList<string>? Waypoints = null, bool Pathfind = false, string Facing = "", IReadOnlyList<RouteCell>? Route = null);

// A camera scripts may name and what game.json says it looks at: Area and Marker for a Marker target, Follow for a Follow
// target; Zoom is 1 when the data gives none.
public sealed record Camera(string Id, CameraTarget Target, string Area, string Marker, string Follow, double Zoom);

public sealed record Notice(NoticeKind Kind, string Key, IReadOnlyList<string> Params, string Text);

public sealed record ActiveStatus(string Id, string Name, int Remaining);

public sealed record Combatant(int Index, string Id, string Name, string Label, Side Side, string Group, int Hp, int MaxHp, int Tp, int MaxTp, bool Alive,
	bool Defending, IReadOnlyList<ActiveStatus> Statuses, CommandKind CommandKind, string CommandId, int CommandTarget, bool Flying = false, int Position = -1);

// Music is the encounter's track from game.json, else for a random battle aboard the vehicle's, "" when the data names none. Vehicle is a vehicle battle's vehicle, the
// combatant after the members riding it, "" otherwise.
public sealed record BattleInfo(bool Live, string Encounter, BattleSource Source, int Round, int PartyCount, int CombatantCount, Outcome Outcome,
	bool EscapeAllowed, bool AutoActive, bool PartyActsNextRound, bool GameOver, string Music, string Vehicle, int FormationSize = 0);

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

// Uses and MaxUses: a skill's uses left and after a rest; -1 for other kinds. Travel: the kind Travel takes a travelling
// technique with, null for others.
public sealed record AbilityInfo(string Id, string Name, AbilityKind Kind, Targeting Targeting, int Cost, bool Field, int Uses, int MaxUses, TravelKind? Travel);

// MagicDefense counts only when GameInfo.MagicDefense is set.
public sealed record StatBonus(int MaxHp, int MaxTp, int Strength, int Mental, int Agility, int Dexterity, int Defense, int Critical, int Evasion, int MagicDefense = 0);

public sealed record StatusResist(string Id, string Name, int Percent);

// What a status screen shows beyond Member. EquipmentBonus is indexed by EquipSlot; Affinities and AffinityList hold each
// element the member takes at other than the normal rate, the list in the data's element order with names.
public sealed record MemberDetail(string Id, int ExperienceToNext, int Critical, int Evasion, Element AttackElement, IReadOnlyList<StatBonus> EquipmentBonus,
	IReadOnlyDictionary<Element, int> Affinities, IReadOnlyList<StatusResist> StatusResists, int MagicDefense = 0, IReadOnlyList<Affinity>? AffinityList = null);

// A member's battle numbers, built by the code that sets up a battle combatant.
public sealed record CombatView(StatBonus Stats, string BasicAttack, string BasicAttackName, Element AttackElement, IReadOnlyList<string> Abilities,
	IReadOnlyList<string> AbilityNames, IReadOnlyList<StatusResist> StatusResists);

// An equipment piece compared on a member before it goes on. After equals Before when CanWear is false; Refusal then
// says why. Wearers are the lineup members who can wear it.
public sealed record EquipPreview(string Member, string Piece, string PieceName, EquipSlot Slot, string Category, StatBonus Bonus, Element Element,
	string Attack, string AttackName, IReadOnlyList<string> Granted, IReadOnlyList<string> GrantedNames, IReadOnlyList<StatusResist> PieceResists,
	string Replaces, string ReplacesName, bool CanWear, PsivError? Refusal, IReadOnlyList<string> Wearers, IReadOnlyList<string> WearerNames,
	CombatView Before, CombatView After, EquipSlot Position = EquipSlot.Weapon, string AlsoReplaces = "", string AlsoReplacesName = "", int Hands = 1);

// One step of a saved macro. TargetMember is set for a Member target, TargetEnemy (else -1) for an Enemy target; Issue says
// why the step cannot run as written now. Piece: an Item step whose Id is an equipment piece with a battle use.
public sealed record MacroStep(string Member, string MemberName, CommandKind CommandKind, string Id, string IdName, MacroTargetKind TargetKind,
	string TargetMember, string TargetMemberName, int TargetEnemy, MacroIssue Issue, bool Piece = false);

public sealed record Macro(string Name, IReadOnlyList<MacroStep> Steps);

public sealed record ComboEntry(string Recipe, string Result, string ResultName, long PlayTimeMs, IReadOnlyList<string> Participants,
	IReadOnlyList<string> ParticipantNames, IReadOnlyList<string> Abilities, IReadOnlyList<string> AbilityNames);

// Total counts every recipe, discovered or not.
public sealed record ComboLog(IReadOnlyList<ComboEntry> Entries, int Total);

// Travel: the kind Travel takes a travelling item with, null for others. CallsVehicle: the vehicle an item calls, "" for
// others. Hands: 2 for a two-handed weapon, 1 for other pieces, 0 for items.
public sealed record InventoryEntry(string Id, string Name, int Count, bool IsEquipment, bool Field, int TargetingOrSlot, int Price, TravelKind? Travel,
	string CallsVehicle = "", int Hands = 0)
{
	public Targeting Targeting => (Targeting)TargetingOrSlot;
	public EquipSlot Slot => (EquipSlot)TargetingOrSlot;
}

public sealed record ShopInfo(string Id, int SellPercent, IReadOnlyList<InventoryEntry> Stock, ShopKind Kind = ShopKind.General);

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
