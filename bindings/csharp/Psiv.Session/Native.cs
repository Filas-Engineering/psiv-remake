using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Psiv.Session.Interop;

// Mirrors of the structs in bindings/c/psiv_session.h, field for field. Every pointer is an IntPtr into
// session-owned memory that the next MUTATES call frees, so GameSession copies out of these at once.
// NativeLayoutTests checks each size against psiv_sizeof.

[StructLayout(LayoutKind.Sequential)]
public struct PsivErrorNative { public int Status; public IntPtr Key; public int ParamCount; public IntPtr Params; public IntPtr Text; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivSessionOptions
{
	public uint StructSize;
	public IntPtr DataDir;
	public IntPtr SaveDir;
	public ulong Seed;
	public int EncounterMode;
	public int Presentation;
	public int WallClock;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivGameInfo
{
	public IntPtr Title;
	public IntPtr GameBuild;
	public int RulesVersion, TickMs, RoundMs, ManualSlots, AutosaveSlots, ItemCap, MoneyCap, LanguageCount;
	public IntPtr Languages;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivProfile
{
	public int TextSpeed, AutoAdvance, ShowShippedNames;
	public IntPtr TextLanguage;
	public int TextSizePercent, BattleSpeed, AmbientTalk, ReducedFlash, MusicVolume, AmbientVolume, EffectsVolume;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivSettings { public IntPtr Difficulty; public int RewardMultiplier; public IntPtr AutoMacro; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivSaveSummary
{
	public IntPtr Slot, Area;
	public long PlayTimeMs;
	public int PartyCount;
	public IntPtr Party, Difficulty;
	public long WriteTime;
	public ulong Sequence;
	public int Autosave, Damaged;
	public IntPtr Error;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivWorldInfo
{
	public int Mode, MenuKind, Money;
	public long PlayTimeMs;
	public int AutosaveCount, GameOver, CanRetry, PlayerControl;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivFieldInfo
{
	public IntPtr AreaId, AreaName;
	public int AreaKind, EncounterMode, Width, Height, StepMs, LeaderX, LeaderY, Facing, PlayerControl, GraceMs;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivEnemy { public int Id; public IntPtr Encounter, Zone; public int X, Y, Facing, State; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivMarker { public IntPtr Name; public int X, Y; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivExit { public int X, Y; public IntPtr ToArea, ToMarker; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivPoint { public int Kind; public IntPtr Id; public int X, Y; public IntPtr Event; public int Active, Cost; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivLine { public IntPtr SpeakerId, SpeakerName, Text, Portrait, Display; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivChoice { public IntPtr Prompt; public int YesNo, OptionCount; public IntPtr Options; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivAction
{
	public ulong Handle;
	public int Phase, Type;
	public IntPtr Subject, Target, Slot;
	public int HasPosition;
	public double X, Y, Z, Seconds, Speed, Strength;
	public int Loop, Instant;
	public IntPtr Description;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivNotice { public int Kind; public IntPtr Key; public int ParamCount; public IntPtr Params; public IntPtr Text; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivActiveStatus { public IntPtr Id, Name; public int Remaining; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivCombatant
{
	public int Index;
	public IntPtr Id, Name, Label;
	public int Side;
	public IntPtr Group;
	public int Hp, MaxHp, Tp, MaxTp, Alive, Defending, StatusCount;
	public IntPtr Statuses;
	public int CommandKind;
	public IntPtr CommandId;
	public int CommandTarget;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivBattleInfo
{
	public int Live;
	public IntPtr Encounter;
	public int Source, Round, PartyCount, CombatantCount, Outcome, EscapeAllowed, AutoActive, PartyActsNextRound, GameOver;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivBattleOption { public IntPtr Id, Name; public int Command, AbilityKind, Targeting, Cost, Uses, Unusable; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivMacroStepResult { public IntPtr Member; public int Actor, Status, Issue, CommandKind; public IntPtr CommandId; public int CommandTarget; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivItemCount { public IntPtr Id; public int Count; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivRecordedCommand { public int Actor, Kind; public IntPtr Id; public int Target; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivBattleEvent
{
	public int Type, Round, Actor, Target, PreviousTarget;
	public IntPtr Ability, Combo, Status, Message;
	public int ParamCount;
	public IntPtr Params, Text;
	public int Amount, Hp, Duration, Command, Element, Effectiveness, Stat, Cancel, StatusEnd, Outcome, Resource, Opening, Succeeded, GameOver, Experience, Currency, ItemCount;
	public IntPtr Items, Macro;
	public int ParticipantCount;
	public IntPtr Participants;
	public int OrderCount;
	public IntPtr Order;
	public int IdCount;
	public IntPtr Ids, Groups;
	public int PartyCount, EscapeAllowed, DefeatHasBranch, VehicleBattle, CommandCount;
	public IntPtr Commands;
}

[InlineArray(5)]
public struct PsivSlotPointers { private IntPtr _first; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivMember
{
	public IntPtr Id, Name;
	public int InLineup, Level, Experience, Hp, MaxHp, Tp, MaxTp, Strength, Mental, Agility, Dexterity, Defense, StatusCount;
	public IntPtr Statuses;
	public PsivSlotPointers Equipment;
	public PsivSlotPointers EquipmentNames;
}

[StructLayout(LayoutKind.Sequential)]
public struct PsivAbilityInfo { public IntPtr Id, Name; public int Kind, Targeting, Cost, Field; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivInventoryEntry { public IntPtr Id, Name; public int Count, Equipment, Field, TargetingOrSlot, Price; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivShopInfo { public IntPtr Id; public int SellPercent, StockCount; public IntPtr Stock; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivInnInfo { public IntPtr Id; public int Cost; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivJob { public IntPtr Id, Title; public int State; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivQuestLog { public IntPtr Objective; public int JobCount; public IntPtr Jobs; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivCodexEntry { public IntPtr Id, Title, Text; }

[StructLayout(LayoutKind.Sequential)]
public struct PsivGlossaryEntry { public IntPtr Id, Restored, Shipped; }

public static unsafe partial class Native
{
	public const string Library = "psiv_session";
	public const uint AbiVersion = 1;

	[LibraryImport(Library, EntryPoint = "psiv_abi_version")] public static partial uint AbiVersionOf();
	[LibraryImport(Library, EntryPoint = "psiv_version")] public static partial IntPtr Version();
	[LibraryImport(Library, EntryPoint = "psiv_sizeof", StringMarshalling = StringMarshalling.Utf8)] public static partial uint SizeOf(string name);
	[LibraryImport(Library, EntryPoint = "psiv_name_of")] public static partial IntPtr NameOf(int kind, int value);
	[LibraryImport(Library, EntryPoint = "psiv_last_error")] public static partial int LastError(SessionHandle session, out PsivErrorNative error);
	[LibraryImport(Library, EntryPoint = "psiv_last_error")] public static partial int LastErrorRaw(IntPtr session, out PsivErrorNative error);

	[LibraryImport(Library, EntryPoint = "psiv_session_options_init")] public static partial void SessionOptionsInit(ref PsivSessionOptions options);
	[LibraryImport(Library, EntryPoint = "psiv_session_create")] public static partial int SessionCreate(in PsivSessionOptions options, out IntPtr session);
	[LibraryImport(Library, EntryPoint = "psiv_session_destroy")] public static partial void SessionDestroy(IntPtr session);
	[LibraryImport(Library, EntryPoint = "psiv_game_info_get")] public static partial int GameInfoGet(SessionHandle s, out PsivGameInfo info);

	[LibraryImport(Library, EntryPoint = "psiv_profile_get")] public static partial int ProfileGet(SessionHandle s, out PsivProfile profile);
	[LibraryImport(Library, EntryPoint = "psiv_profile_set")] public static partial int ProfileSet(SessionHandle s, in PsivProfile profile);
	[LibraryImport(Library, EntryPoint = "psiv_profile_set_value", StringMarshalling = StringMarshalling.Utf8)] public static partial int ProfileSetValue(SessionHandle s, string key, string value);
	[LibraryImport(Library, EntryPoint = "psiv_settings_get")] public static partial int SettingsGet(SessionHandle s, out PsivSettings settings);
	[LibraryImport(Library, EntryPoint = "psiv_settings_set", StringMarshalling = StringMarshalling.Utf8)] public static partial int SettingsSet(SessionHandle s, string difficulty, int reward);
	[LibraryImport(Library, EntryPoint = "psiv_settings_set_auto_macro", StringMarshalling = StringMarshalling.Utf8)] public static partial int SettingsSetAutoMacro(SessionHandle s, string? macro);
	[LibraryImport(Library, EntryPoint = "psiv_macros")] public static partial int Macros(SessionHandle s, out IntPtr names, out int count);

	[LibraryImport(Library, EntryPoint = "psiv_new_game")] public static partial int NewGame(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_continue_game")] public static partial int ContinueGame(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_load", StringMarshalling = StringMarshalling.Utf8)] public static partial int Load(SessionHandle s, string slot);
	[LibraryImport(Library, EntryPoint = "psiv_retry_battle")] public static partial int RetryBattle(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_quit_to_title")] public static partial int QuitToTitle(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_save_manual", StringMarshalling = StringMarshalling.Utf8)] public static partial int SaveManual(SessionHandle s, string slot);
	[LibraryImport(Library, EntryPoint = "psiv_saves")] public static partial int Saves(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_mode_get")] public static partial int ModeGet(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_menu_kind_get")] public static partial int MenuKindGet(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_world_info_get")] public static partial int WorldInfoGet(SessionHandle s, out PsivWorldInfo info);
	[LibraryImport(Library, EntryPoint = "psiv_flag", StringMarshalling = StringMarshalling.Utf8)] public static partial int Flag(SessionHandle s, string name);

	[LibraryImport(Library, EntryPoint = "psiv_update")] public static partial int Update(SessionHandle s, int dtMs, int direction);
	[LibraryImport(Library, EntryPoint = "psiv_walk")] public static partial int Walk(SessionHandle s, int direction);
	[LibraryImport(Library, EntryPoint = "psiv_face")] public static partial int Face(SessionHandle s, int direction);
	[LibraryImport(Library, EntryPoint = "psiv_interact")] public static partial int Interact(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_party_talk")] public static partial int PartyTalk(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_field_info_get")] public static partial int FieldInfoGet(SessionHandle s, out PsivFieldInfo info);
	[LibraryImport(Library, EntryPoint = "psiv_field_grid")] public static partial int FieldGrid(SessionHandle s, out IntPtr cells, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_field_rows")] public static partial int FieldRows(SessionHandle s, out IntPtr rows, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_field_enemies")] public static partial int FieldEnemies(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_field_markers")] public static partial int FieldMarkers(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_field_exits")] public static partial int FieldExits(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_field_points")] public static partial int FieldPoints(SessionHandle s, out IntPtr list, out int count);

	[LibraryImport(Library, EntryPoint = "psiv_line_pending")] public static partial int LinePending(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_line_get")] public static partial int LineGet(SessionHandle s, out PsivLine line);
	[LibraryImport(Library, EntryPoint = "psiv_line_confirm")] public static partial int LineConfirm(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_choice_pending")] public static partial int ChoicePending(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_choice_get")] public static partial int ChoiceGet(SessionHandle s, out PsivChoice choice);
	[LibraryImport(Library, EntryPoint = "psiv_choose")] public static partial int Choose(SessionHandle s, int index);
	[LibraryImport(Library, EntryPoint = "psiv_can_skip")] public static partial int CanSkip(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_skip")] public static partial int Skip(SessionHandle s);

	[LibraryImport(Library, EntryPoint = "psiv_actions_poll")] public static partial int ActionsPoll(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_action_finish")] public static partial int ActionFinish(SessionHandle s, ulong handle);
	[LibraryImport(Library, EntryPoint = "psiv_actions_set_callback")] public static partial int ActionsSetCallback(SessionHandle s, delegate* unmanaged[Cdecl]<IntPtr, PsivAction*, void> callback, IntPtr user);
	[LibraryImport(Library, EntryPoint = "psiv_notices_drain")] public static partial int NoticesDrain(SessionHandle s, out IntPtr list, out int count);

	[LibraryImport(Library, EntryPoint = "psiv_battle_info_get")] public static partial int BattleInfoGet(SessionHandle s, out PsivBattleInfo info);
	[LibraryImport(Library, EntryPoint = "psiv_battle_combatants")] public static partial int BattleCombatants(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_battle_options")] public static partial int BattleOptions(SessionHandle s, int actor, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_battle_set_command", StringMarshalling = StringMarshalling.Utf8)] public static partial int BattleSetCommand(SessionHandle s, int actor, int kind, string? id, int target);
	[LibraryImport(Library, EntryPoint = "psiv_battle_apply_macro", StringMarshalling = StringMarshalling.Utf8)] public static partial int BattleApplyMacro(SessionHandle s, string macro, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_battle_start_auto")] public static partial int BattleStartAuto(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_battle_cancel_auto")] public static partial int BattleCancelAuto(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_battle_fill_from_auto")] public static partial int BattleFillFromAuto(SessionHandle s, out int filled);
	[LibraryImport(Library, EntryPoint = "psiv_battle_fill_from_policy")] public static partial int BattleFillFromPolicy(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_battle_resolve_round")] public static partial int BattleResolveRound(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_battle_last_events")] public static partial int BattleLastEvents(SessionHandle s, out IntPtr list, out int count);

	[LibraryImport(Library, EntryPoint = "psiv_party")] public static partial int Party(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_member_abilities", StringMarshalling = StringMarshalling.Utf8)] public static partial int MemberAbilities(SessionHandle s, string member, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_inventory")] public static partial int Inventory(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_shop_get")] public static partial int ShopGet(SessionHandle s, out PsivShopInfo info);
	[LibraryImport(Library, EntryPoint = "psiv_inn_get")] public static partial int InnGet(SessionHandle s, out PsivInnInfo info);
	[LibraryImport(Library, EntryPoint = "psiv_open_menu")] public static partial int OpenMenu(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_close_menu")] public static partial int CloseMenu(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_buy", StringMarshalling = StringMarshalling.Utf8)] public static partial int Buy(SessionHandle s, string id, int count);
	[LibraryImport(Library, EntryPoint = "psiv_sell", StringMarshalling = StringMarshalling.Utf8)] public static partial int Sell(SessionHandle s, string id, int count);
	[LibraryImport(Library, EntryPoint = "psiv_rest")] public static partial int Rest(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_use_item", StringMarshalling = StringMarshalling.Utf8)] public static partial int UseItem(SessionHandle s, string item, string? member);
	[LibraryImport(Library, EntryPoint = "psiv_use_ability", StringMarshalling = StringMarshalling.Utf8)] public static partial int UseAbility(SessionHandle s, string user, string ability, string? member);
	[LibraryImport(Library, EntryPoint = "psiv_equip", StringMarshalling = StringMarshalling.Utf8)] public static partial int Equip(SessionHandle s, string member, string equipment);
	[LibraryImport(Library, EntryPoint = "psiv_unequip", StringMarshalling = StringMarshalling.Utf8)] public static partial int Unequip(SessionHandle s, string member, int slot);
	[LibraryImport(Library, EntryPoint = "psiv_name_entry_member")] public static partial IntPtr NameEntryMember(SessionHandle s);
	[LibraryImport(Library, EntryPoint = "psiv_enter_name", StringMarshalling = StringMarshalling.Utf8)] public static partial int EnterName(SessionHandle s, string name);

	[LibraryImport(Library, EntryPoint = "psiv_quest_log_get")] public static partial int QuestLogGet(SessionHandle s, out PsivQuestLog log);
	[LibraryImport(Library, EntryPoint = "psiv_encyclopedia")] public static partial int Encyclopedia(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_glossary")] public static partial int Glossary(SessionHandle s, out IntPtr list, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_text_log")] public static partial int TextLog(SessionHandle s, out IntPtr pages, out int count);
	[LibraryImport(Library, EntryPoint = "psiv_pacing_table")] public static partial int PacingTable(SessionHandle s, out IntPtr text);
	[LibraryImport(Library, EntryPoint = "psiv_render", StringMarshalling = StringMarshalling.Utf8)] public static partial int Render(SessionHandle s, string key, IntPtr* parameters, int count, out IntPtr text);
	[LibraryImport(Library, EntryPoint = "psiv_format_text", StringMarshalling = StringMarshalling.Utf8)] public static partial int FormatText(SessionHandle s, string key, IntPtr* parameters, int count, out IntPtr text);
	[LibraryImport(Library, EntryPoint = "psiv_display_name", StringMarshalling = StringMarshalling.Utf8)] public static partial int DisplayName(SessionHandle s, string id, out IntPtr text);
	[LibraryImport(Library, EntryPoint = "psiv_string_text", StringMarshalling = StringMarshalling.Utf8)] public static partial int StringText(SessionHandle s, string id, out IntPtr text);

	public static string Str(IntPtr p) => p == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(p) ?? "";

	public static string[] Strs(IntPtr p, int count)
	{
		var result = new string[count];
		for (int i = 0; i < count; i++)
		{
			result[i] = Str(((IntPtr*)p)[i]);
		}
		return result;
	}

	public static ReadOnlySpan<T> Span<T>(IntPtr p, int count) where T : unmanaged =>
		p == IntPtr.Zero || count <= 0 ? ReadOnlySpan<T>.Empty : new ReadOnlySpan<T>((void*)p, count);
}
