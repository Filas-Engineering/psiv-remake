using System.Globalization;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private static float SpeedOf(BattleSpeed speed) => speed switch
	{
		BattleSpeed.X1_5 => 1.5f,
		BattleSpeed.X2 => 2f,
		BattleSpeed.X3 => 3f,
		_ => 1f,
	};

	// The battle speed as a factor: the game's own level's percent (presentation.options), else the built-in enum's.
	private float BattleSpeedFactor()
	{
		OptionLevels levels = _s.OptionLevels(OptionSetting.BattleSpeed);
		return levels.Declared ? levels.Levels[levels.Current].Value / 100f : SpeedOf(_s.Profile().BattleSpeed);
	}

	private int SpeedPercent() => (int)MathF.Round(BattleSpeedFactor() * 100);

	private enum BattlePhase { Command, Option, Target, Macro, Playback, Ended, Top, AutoChoice }

	// The phases with a menu to choose from.
	private static bool Choosing(BattlePhase phase) =>
		phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro or BattlePhase.Top or BattlePhase.AutoChoice;

	// The battle layout's `macro_flow` "remember": after a macro's round the command menu opens on Macro, and the macro list
	// on the macro played last; "reset" (the default) opens both at their first row.
	private bool MacroFlowRemember => Ui.ParamText(BattleScreen, "macro_flow", UiData(), "reset") == "remember";
	private string _lastMacro = "";
	private string _probeMacroLines = "-";
	private bool _macroRound;

	// The battle layout's `macro_failed`: what a macro picked from the menu does with a step that cannot run (`automatic`,
	// `attack` or `lose_turn`); without it the Macros page's choice (basic attack or automatic).
	private MacroFallback MenuMacroFallback => Ui.ParamText(BattleScreen, "macro_failed", UiData(), "") switch
	{
		"automatic" => MacroFallback.Automatic,
		"attack" => MacroFallback.Attack,
		"lose_turn" => MacroFallback.LoseTurn,
		_ => BattleMacroFallback,
	};

	// The Auto row's choice with the battle layout's `auto_choose`: the trouble mode, then the policy.
	private int _autoChoiceStep;
	private MacroTrouble _autoChoiceTrouble;
	private static readonly MacroTrouble[] AutoTroubles = [MacroTrouble.Continue, MacroTrouble.Recover, MacroTrouble.Stop];
	private static readonly MacroPolicy[] AutoPolicies = [MacroPolicy.Default, MacroPolicy.Power, MacroPolicy.Expected];

	// The top-level step's rows, before the members' commands when the battle layout's `command_top` is on; its
	// `command_rows.top` can leave some out.
	private string[] TopMenu => [.. Layout.Screen(BattleScreen).TopCommands ?? [.. UiFields.TopCommands]];

	// The battle layout's command navigation: `command_axis` "row" makes Left and Right walk the command rows (Up and Down
	// then do nothing), and `command_top` puts the top-level step (fight, macro, run) before the members' commands each round.
	private string BattleScreen => BattleScreenOf(StageLayout());

	// The stage layout's file, or in a vehicle battle its `_vehicle` file when the layouts have one (else the stage
	// layout's own, so a set without one draws as before).
	private string BattleScreenOf(BattleLayout layout)
	{
		string screen = layout == BattleLayout.Front ? "battle_front" : "battle_side";
		return _s.BattleInfo() is { Vehicle.Length: > 0 } && Layout.Screens.ContainsKey(screen + "_vehicle") ? screen + "_vehicle" : screen;
	}

	private bool LayoutSwitch(string name) => Ui.Param(BattleScreen, name, UiData(), 0) != 0;

	// Whether Confirm or Back leaves the result screen: always but after a game over whose layout's `game_over_confirm` is 0.
	private bool ResultConfirms => _s.BattleInfo()?.GameOver != true || Ui.Param(BattleScreen, "game_over_confirm", UiData(), 1) != 0;

	private bool CommandRowAxis => Ui.ParamText(BattleScreen, "command_axis", UiData(), "column") == "row";

	private sealed class BattleUi
	{
		public bool Active;
		public BattlePhase Phase;
		public int Actor;
		public string MenuChoice = "";
		// The command row MenuChoice came from.
		public int MenuIndex = -1;
		// The macro list was opened from the top-level step, not by a member.
		public bool FromTop;
		// The result window's page, when the battle layout's `result_page_lines` pages it.
		public int ResultPage;
		public List<BattleOption> Options = [];
		// The worn pieces among the options, cast without being used up.
		public HashSet<string> Worn = [];
		public BattleOption? Option;
		public List<int> Targets = [];
		// Allies among the targets the option cannot reach (AlliesOutOfReach), greyed out before the pick.
		public HashSet<int> Refused = [];
		public List<string> Labels = [];
		public readonly List<string> Log = [];
		public bool Auto;
		public bool LogShown;
		// Frame time on the result screen, for a game over's return to the title.
		public int EndedMs;
		// The battle file's `result_sequence`: the step showing and its time.
		public int ResultStep;
		public int ResultStepMs;
		// The level-ups step's page under `level_page_lines`.
		public int LevelPage;
		// The name of the member whose defeat came last in the battle's final round, "" without one.
		public string LastFallen = "";
		// Time the first command window has waited (`command_open_delay`), and whether it has opened.
		public int CommandWaitMs;
		public bool CommandsOpened;
	}

	// The battle file's `result_sequence`, null without one (the result window alone).
	private List<(string Phase, int Ms)>? ResultSequence => Layout.Screen(BattleScreen).ResultSequence;

	private bool LevelUpsApart => ResultSequence?.Any(s => s.Phase == "level_ups") == true;

	// The first step at or after `from` that has something to show (level-ups only with a level gained, the defeat only
	// after one, the victory's outcome not after a defeat `defeat_message` shows), -1 past the end.
	private int ResultStepFrom(int from)
	{
		List<(string Phase, int Ms)> steps = ResultSequence ?? [];
		for (int i = from; i < steps.Count; i++)
		{
			bool shows = steps[i].Phase switch
			{
				"defeat" => Defeated,
				"victory" => !Defeated || DefeatKey.Length == 0,
				"level_ups" => LevelLines(ResultLines(_s.BattleInfo())).Count > 0,
				_ => true,
			};
			if (shows)
			{
				return i;
			}
		}
		return -1;
	}

	private bool Defeated => _s.BattleInfo() is { } info && (info.GameOver || info.Outcome == Outcome.Defeat);

	// The battle layout's `defeat_message`: a text key whose {0} is the member who fell last, shown in the result window
	// (and the `defeat` step) in place of the outcome banner after a defeat; "" leaves the defeat as it was.
	private string DefeatKey => Ui.ParamText(BattleScreen, "defeat_message", UiData(), "");

	private string DefeatText => Defeated && DefeatKey.Length > 0 ? T(DefeatKey, _battle.LastFallen) : "";

	// The party member the last CombatantDefeated of the final round's events names; a developer's lose, which drops the
	// party's hit points to 0 without such an event, gives the last member down.
	private string LastFallenMember()
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		BattleEvent? fell = _s.LastEvents().LastOrDefault(e => e.Type == BattleEventType.CombatantDefeated && all.ElementAtOrDefault(e.Target)?.Side == Side.Party);
		return fell is not null ? all[fell.Target].Name : all.LastOrDefault(c => c.Side == Side.Party && !c.Alive)?.Name ?? "";
	}

	// After a game over whose layout ignores Confirm, the steps with an `ms` go on by themselves up to the last one shown,
	// which stays until `game_over_return_ms`.
	private void UpdateTimedResultSteps(int dtMs)
	{
		BattleUi b = _battle;
		if (b.ResultStep < 0)
		{
			return;
		}
		b.ResultStepMs += dtMs;
		int ms = ResultSequence![b.ResultStep].Ms;
		if (ms > 0 && b.ResultStepMs >= ms && ResultStepFrom(b.ResultStep + 1) >= 0)
		{
			NextResultStep();
		}
	}

	private void NextResultStep()
	{
		_battle.ResultStep = ResultStepFrom(_battle.ResultStep + 1);
		_battle.ResultStepMs = 0;
		_battle.ResultPage = 0;
		_battle.LevelPage = 0;
		if (_battle.ResultStep < 0)
		{
			EndBattleScreen();
		}
		else if (ResultSequence![_battle.ResultStep].Phase == "level_ups")
		{
			_audio.PlayCue("level_up");
		}
	}

	// The result sequence's steps: each goes on with Confirm or after its `ms` (above 0); the result step turns its pages
	// first on Confirm, the level-ups step on Confirm or after `ms` each; Back ends the battle at once, as on the result
	// window.
	private void UpdateResultSequence(int dtMs, ActionInput input)
	{
		BattleUi b = _battle;
		if (b.ResultStep < 0)
		{
			EndBattleScreen();
			return;
		}
		(string phase, int ms) = ResultSequence![b.ResultStep];
		b.ResultStepMs += dtMs;
		if (input.Back)
		{
			EndBattleScreen();
		}
		else if (phase == "result" && input.Confirm && b.ResultPage + 1 < ResultPages())
		{
			b.ResultPage++;
		}
		else if (phase == "level_ups" && (input.Confirm || ms > 0 && b.ResultStepMs >= ms) && b.LevelPage + 1 < LevelPages())
		{
			b.LevelPage++;
			b.ResultStepMs = 0;
		}
		else if (input.Confirm || ms > 0 && b.ResultStepMs >= ms)
		{
			NextResultStep();
		}
	}

	private readonly BattleUi _battle = new();
	private readonly BattleTimeline _timeline = new();
	private readonly BattleShow _show = new();
	// The terrain of the cell the party stood on when the battle began (floor, water, soft), for the backdrop.
	private string _battleTerrain = "";
	// The cell the party stood on when the battle began, for the area's backdrop map.
	private (int X, int Y) _battleCell = (-1, -1);
	// Rounds whose playback was compared with the session as it ended, and what differed.
	private int _syncChecks;
	private readonly List<string> _syncProblems = [];

	private string _probeEnemyGroups = "none";
	private string _probeCommands = "-";
	private string _probeEnemyMarks = "-";
	private string _probeEnemyIcons = "none";
	private string _probePlaces = "none";

	// Every presentation step started, by its description, for headless expectations.
	public List<string> PresentedSteps { get; } = [];

	// With `command_top`, the battle layout's `command_rows.member` can leave commands out of the members' rows.
	private IEnumerable<string> CommandMenu => LayoutSwitch("command_top") ? Layout.Screen(BattleScreen).MemberCommands ?? [.. UiFields.MemberCommands] : UiFields.MemberCommands;

	// A row of the command menu: a command, or in a vehicle battle one of the vehicle's skills.
	private sealed record CommandRow(string Command, string Text, BattleOption? Option = null)
	{
		public bool Usable => Option?.Usable ?? true;
	}

	// A vehicle has its attack, its skills and escape, or the battle layout's `command_rows.vehicle`; the members have the
	// whole menu.
	private List<CommandRow> CommandRows()
	{
		IReadOnlyList<Combatant> all = StageCombatants();
		string vehicle = StageInfo()?.Vehicle ?? "";
		if (vehicle.Length > 0 && _battle.Actor < all.Count && all[_battle.Actor].Id == vehicle && _stage is null)
		{
			return [.. (Layout.Screen(BattleScreen).VehicleCommands ?? [.. UiFields.VehicleDefault]).SelectMany(c => c == "skills"
				? _s.BattleOptions(_battle.Actor).Where(o => o.AbilityKind == AbilityKind.Skill).Select(o => new CommandRow("skill", OptionText(o), o))
				: [new CommandRow(c, T("ui.battle.command." + c))])];
		}
		return [.. CommandMenu.Select(c => new CommandRow(c, T("ui.battle.command." + c)))];
	}

	private string OptionText(BattleOption o) => _battle.Worn.Contains(o.Id) ? T("ui.battle.worn_row", o.Name)
		: o.Name + (o.Cost > 0 ? " " + T("ui.battle.cost", o.Cost) : "") + (o.Uses >= 0 ? " " + T("ui.common.count", o.MaxUses >= 0 ? $"{o.Uses}/{o.MaxUses}" : o.Uses) : "");

	// The actor's worn pieces that have a battle use, as Item options: nothing is paid and the piece stays on.
	private IEnumerable<BattleOption> WornOptions(int actor)
	{
		string member = actor < StageCombatants().Count ? StageCombatants()[actor].Id : "";
		Member? m = _s.Party().FirstOrDefault(x => x.Id == member);
		foreach (string piece in m?.Equipment.Where(e => e.Length > 0) ?? [])
		{
			if (_data.PieceUses.TryGetValue(piece, out string? use))
			{
				yield return new BattleOption(piece, _s.DisplayName(piece), CommandKind.Item, null, _data.AbilityTargeting.GetValueOrDefault(use), 0, -1, CancelReason.None);
			}
		}
	}

	private BattleContext Context()
	{
		string screen = BattleScreen;
		return new BattleContext(_s, _battle.Labels, PresentationTable, _data, T, target => Marked(screen, new UiContext(), "critical", target)) { Shown = Shown };
	}

	private void StartBattle()
	{
		_battle.Active = true;
		_battle.CommandWaitMs = 0;
		_battle.CommandsOpened = false;
		_battle.Labels = _s.Combatants().Select(c => c.Label).ToList();
		_battle.Log.Clear();
		_battle.Auto = false;
		_timeline.Clear();
		_falls.Clear();
		_show.Start(_s.Combatants(), _s.BattleInfo()?.PartyCount ?? 0, _s.BattleInfo()?.FormationSize ?? 0);
		_battleTerrain = _s.Field() is { } view ? (view.Cell(view.Info.LeaderX, view.Info.LeaderY) & CellFlags.TerrainMask) switch
		{
			CellFlags.Water => "water",
			CellFlags.Soft => "soft",
			CellFlags.Floor => "floor",
			_ => "",
		} : "";
		_battleCell = _s.Field() is { } at ? (at.Info.LeaderX, at.Info.LeaderY) : (-1, -1);
		_backdropRun = BattleRun("backdrop");
		_persistBackdrop = null;
		_backdropStartMs = _clockMs;
		string enemies = ListText(_s.Combatants().Where(c => c.Side == Side.Enemy).Select(c => c.Label));
		BattleStep intro = Context().Step(StepKind.Intro, enemies, "intro " + enemies, []);
		intro.Lines.Add(T("ui.battle.log.enemies", enemies));
		intro.LineEvents.Add(-1);
		intro.Cues.Add("battle_start");
		AttachScripts([intro]);
		if (TransitionStep() is { } transition)
		{
			_timeline.Add([transition]);
		}
		_timeline.Add([intro]);
		_battle.Phase = BattlePhase.Playback;
	}

	// A notice during a battle (a level-up, an autosave) plays as its own step after what is already queued.
	private void AddBattleNotice(Notice n)
	{
		// A result sequence with a level-ups step shows them there, after the result window.
		if (n.Key is "game.level_up" or "game.learned" && LevelUpsApart)
		{
			_battle.Log.Add(n.Text);
			return;
		}
		BattleStep step = Context().Step(StepKind.Notice, n.Text, "notice " + n.Text, []);
		step.Lines.Add(n.Text);
		step.LineEvents.Add(-1);
		step.Sub = n.Key;
		if (NoticeCue(n) is { } cue)
		{
			step.Cues.Add(cue);
		}
		AttachScripts([step]);
		_timeline.Add([step]);
	}

	private void NextActor(int from)
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		BattleInfo? info = _s.BattleInfo();
		if (info is not null && info.Live && info.PartyActsNextRound)
		{
			if (from == 0 && _battle.Phase != BattlePhase.Top && LayoutSwitch("command_top"))
			{
				_battle.Phase = BattlePhase.Top;
				_cursor = _macroRound && MacroFlowRemember ? Math.Max(0, Array.IndexOf(TopMenu, "macro")) : 0;
				_macroRound = false;
				return;
			}
			for (int i = from; i < info.PartyCount; i++)
			{
				// In a vehicle battle the vehicle takes the commands and the members ride along.
				if (all[i].Alive && (info.Vehicle.Length == 0 || all[i].Id == info.Vehicle))
				{
					_battle.Actor = i;
					_battle.Phase = BattlePhase.Command;
					_cursor = from == 0 && _macroRound && MacroFlowRemember ? Math.Max(0, CommandRows().FindIndex(r => r.Command == "macro")) : 0;
					_macroRound = false;
					return;
				}
			}
		}
		Resolve();
	}

	private void Resolve()
	{
		if (_battle.Auto && !FillAuto())
		{
			_battle.Auto = false;
			TakeNotices();
			NextActor(0);
			return;
		}
		List<BattleStep> round = BattleTimeline.Build(_s.ResolveRound(), Context());
		AttachScripts(round);
		_timeline.Add(round);
		TakeNotices();
		_battle.Phase = BattlePhase.Playback;
	}

	// The script verb `equip MEMBER PIECE [SLOT]`: the piece worn as the equip screen would (at that place, `left_hand` for
	// a second weapon), the field menu opened around it when the field is up; null when it is worn, else the refusal.
	public string? ScriptEquip(string member, string piece, string? slot)
	{
		EquipSlot place = default;
		if (slot is not null && !Enum.TryParse(slot.Replace("_", ""), true, out place))
		{
			return $"no equipment place '{slot}': {string.Join(", ", Enum.GetNames<EquipSlot>())}";
		}
		bool opened = _s.Mode == Mode.Field && _s.OpenMenu().Ok;
		PsivResult result = slot is null ? _s.Equip(member, piece) : _s.EquipAt(member, piece, place);
		if (opened)
		{
			_s.CloseMenu();
		}
		return result.Ok ? null : result.Error!.Text;
	}

	// The script verb and console command `combo RECIPE` while the members choose: DevCombo makes the recipe's parts this
	// round's leading commands, the members left without one defend, and the round resolves; null when it starts, else why
	// not.
	public string? ScriptCombo(string recipe)
	{
		if (!_battle.Active || _battle.Phase is not (BattlePhase.Command or BattlePhase.Top))
		{
			return "the members are not choosing commands";
		}
		PsivResult set = _s.DevCombo(recipe);
		if (!set.Ok)
		{
			return set.Error!.Text;
		}
		foreach (Combatant c in _s.Combatants().Where(c => c.Side == Side.Party && c.Alive && c.CommandKind == CommandKind.None))
		{
			_s.SetCommand(c.Index, CommandKind.Defend);
		}
		Resolve();
		return null;
	}

	private void SetAndAdvance(CommandKind kind, string? id, int target)
	{
		PsivResult result = _s.SetCommand(_battle.Actor, kind, id, target);
		if (!result.Ok)
		{
			_battle.Log.Add(T("ui.battle.log.refused", result.Error!.Text));
			_battle.Phase = BattlePhase.Command;
			return;
		}
		NextActor(_battle.Actor + 1);
	}

	private void ChooseTargets(Targeting targeting)
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		_battle.Targets = targeting switch
		{
			Targeting.OneEnemy => all.Where(c => c.Side == Side.Enemy && c.Alive).Select(c => c.Index).ToList(),
			Targeting.OneAlly => all.Where(c => c.Side == Side.Party && c.Alive).Select(c => c.Index).ToList(),
			Targeting.OneFallenAlly => all.Where(c => c.Side == Side.Party && !c.Alive).Select(c => c.Index).ToList(),
			_ => [],
		};
		_battle.Refused.Clear();
		if (_battle.Option is { } option && targeting is Targeting.OneAlly or Targeting.OneFallenAlly && _battle.Actor < all.Count)
		{
			IReadOnlyList<string> unreachable = _s.AlliesOutOfReach(option.Command, option.Id, all[_battle.Actor].Id);
			_battle.Refused.UnionWith(_battle.Targets.Where(t => unreachable.Contains(all[t].Id)));
		}
	}

	private void UpdateBattle(int dtMs, ActionInput input)
	{
		BattleUi b = _battle;
		if (input.Pressed(GameAction.TextLog))
		{
			b.LogShown = !b.LogShown;
		}
		if (Choosing(b.Phase) && input.Pressed(GameAction.Pause))
		{
			OpenPause();
			return;
		}
		if (Choosing(b.Phase) && input.Pressed(GameAction.Extras))
		{
			OpenExtras();
			return;
		}
		switch (b.Phase)
		{
		case BattlePhase.Playback:
			if (b.Auto && input.Back)
			{
				_s.CancelAuto();
				b.Log.Add(T("ui.battle.log.auto_stops"));
			}
			PlaybackTimeline(dtMs, input.Down(GameAction.FastForward), input.Confirm);
			// The battle file's `command_open_delay`: after the intro the first command window waits that long.
			if (_timeline.Done && !b.CommandsOpened && _s.Mode == Mode.Battle && !b.Auto && b.CommandWaitMs < Ui.Param(BattleScreen, "command_open_delay", UiData(), 0))
			{
				b.CommandWaitMs += dtMs;
				break;
			}
			if (_timeline.Done)
			{
				b.CommandsOpened = true;
				_syncChecks++;
				if (_show.Differences(_s.Combatants(), b.Labels) is { Length: > 0 } differences)
				{
					_syncProblems.Add(differences);
					_log("battle: the playback ended away from the session: " + differences);
				}
				_show.Reset(_s.Combatants());
				if (_s.Mode != Mode.Battle)
				{
					b.LastFallen = LastFallenMember();
					b.Phase = BattlePhase.Ended;
					b.ResultPage = 0;
					b.EndedMs = 0;
					b.ResultStep = ResultStepFrom(0);
					b.ResultStepMs = 0;
					b.LevelPage = 0;
					if (b.ResultStep >= 0 && ResultSequence![b.ResultStep].Phase == "level_ups")
					{
						_audio.PlayCue("level_up");
					}
				}
				else if (b.Auto)
				{
					Resolve();
				}
				else
				{
					NextActor(0);
				}
			}
			break;
		case BattlePhase.Ended:
			// A game over returns to the title by itself after the timing `game_over_return_ms` (0 waits), and on Confirm
			// or Back unless the battle layout's `game_over_confirm` is 0.
			bool gameOver = _s.BattleInfo()?.GameOver == true;
			b.EndedMs += dtMs;
			bool confirms = ResultConfirms;
			if (_outro is null && gameOver && Timing("game_over_return_ms") is > 0 and int back && b.EndedMs >= back)
			{
				EndBattleScreen();
			}
			else if (_outro is null && confirms && ResultSequence is not null)
			{
				UpdateResultSequence(dtMs, input);
			}
			else if (_outro is null && ResultSequence is not null)
			{
				UpdateTimedResultSteps(dtMs);
			}
			else if (_outro is null && confirms && input.Confirm && b.ResultPage + 1 < ResultPages())
			{
				b.ResultPage++;
			}
			else if (_outro is null && confirms && (input.Confirm || input.Back))
			{
				EndBattleScreen();
			}
			break;
		case BattlePhase.Top:
			string[] top = TopMenu;
			Move(input, top.Length);
			if (!input.Confirm)
			{
				break;
			}
			b.MenuIndex = _cursor;
			switch (top[_cursor])
			{
			case "fight":
				NextActor(0);
				break;
			case "macro" when _s.Macros().Count == 0 && MacroSlotLabels is null:
				Say(T("ui.battle.no_macros"));
				b.Log.Add(T("ui.battle.no_macros"));
				break;
			case "macro":
				b.MenuChoice = "macro";
				b.FromTop = true;
				_cursor = LastMacroRow();
				b.Phase = BattlePhase.Macro;
				break;
			default:
				RunAway();
				break;
			}
			break;
		case BattlePhase.Command:
			List<CommandRow> rows = CommandRows();
			if (CommandRowAxis)
			{
				MoveAcross(input, rows.Count);
			}
			else
			{
				Move(input, rows.Count);
			}
			if (input.Back && LayoutSwitch("command_top") && FirstActor() == b.Actor)
			{
				b.Phase = BattlePhase.Top;
				_cursor = 0;
				break;
			}
			if (!input.Confirm)
			{
				break;
			}
			CommandRow row = rows[Math.Clamp(_cursor, 0, rows.Count - 1)];
			b.MenuChoice = row.Command;
			b.FromTop = false;
			b.MenuIndex = Math.Clamp(_cursor, 0, rows.Count - 1);
			_cursor = 0;
			if (row.Option is { } skill)
			{
				ChooseOption(skill);
				break;
			}
			switch (b.MenuChoice)
			{
			case "attack":
				ChooseTargets(Targeting.OneEnemy);
				b.Option = null;
				b.Phase = BattlePhase.Target;
				break;
			case "technique":
			case "skill":
			case "item":
				b.Options = _s.BattleOptions(b.Actor).Where(o => b.MenuChoice switch
				{
					"technique" => o.AbilityKind == AbilityKind.Technique,
					"skill" => o.AbilityKind == AbilityKind.Skill,
					_ => o.Command == CommandKind.Item,
				}).ToList();
				b.Worn.Clear();
				if (b.MenuChoice == "item")
				{
					List<BattleOption> worn = [.. WornOptions(b.Actor)];
					b.Worn.UnionWith(worn.Select(o => o.Id));
					bool wornFirst = Ui.ParamText(BattleScreen, "item_order", UiData(), "items_then_equipment") == "equipment_then_items";
					b.Options.InsertRange(wornFirst ? 0 : b.Options.Count, worn);
				}
				b.Phase = b.Options.Count > 0 ? BattlePhase.Option : BattlePhase.Command;
				if (b.Options.Count == 0)
				{
					b.Log.Add(T("ui.battle.log.nothing_to_use." + b.MenuChoice));
				}
				break;
			// A vehicle's grouped row (`command_rows.vehicle` "weapons" or "options"): its skills as a list of their own.
			case "weapons" or "options":
				b.Options = [.. _s.BattleOptions(b.Actor).Where(o => o.AbilityKind == AbilityKind.Skill)];
				b.Worn.Clear();
				b.Phase = b.Options.Count > 0 ? BattlePhase.Option : BattlePhase.Command;
				if (b.Options.Count == 0)
				{
					b.Log.Add(T("ui.battle.log.nothing_to_use.skill"));
					_cursor = b.MenuIndex;
				}
				break;
			case "defend":
				SetAndAdvance(CommandKind.Defend, null, -1);
				break;
			case "escape":
				SetAndAdvance(CommandKind.Escape, null, -1);
				break;
			case "macro":
				_cursor = LastMacroRow();
				b.Phase = BattlePhase.Macro;
				break;
			case "auto" when LayoutSwitch("auto_choose"):
				_autoChoiceStep = 0;
				b.Phase = BattlePhase.AutoChoice;
				break;
			case "auto":
				if (_s.Settings().AutoMacro.Length == 0 && _s.Macros().Count > 0)
				{
					Report(_s.SetAutoMacro(_s.Macros()[0]));
				}
				PsivResult auto = _s.StartAuto();
				if (auto.Ok)
				{
					b.Auto = true;
					TakeNotices();
					Resolve();
				}
				else
				{
					b.Log.Add(T("ui.battle.log.refused", auto.Error!.Text));
				}
				break;
			}
			break;
		case BattlePhase.Option:
			Move(input, b.Options.Count);
			if (input.Back)
			{
				b.Phase = BattlePhase.Command;
				// Back from a vehicle's grouped list lands on its row.
				if (b.MenuChoice is "weapons" or "options")
				{
					_cursor = b.MenuIndex;
				}
			}
			else if (input.Confirm)
			{
				ChooseOption(b.Options[_cursor]);
			}
			break;
		case BattlePhase.Target:
			Move(input, b.Targets.Count);
			if (input.Back || b.Targets.Count == 0)
			{
				b.Phase = BattlePhase.Command;
			}
			else if (input.Confirm && b.Refused.Contains(b.Targets[_cursor]))
			{
				_audio.PlayCue("refuse");
			}
			else if (input.Confirm)
			{
				int target = b.Targets[_cursor];
				if (b.Option is null)
				{
					SetAndAdvance(CommandKind.Attack, null, target);
				}
				else
				{
					SetAndAdvance(b.Option.Command, b.Option.Id, target);
				}
			}
			break;
		case BattlePhase.Macro:
			List<string> macros = BattleMacros();
			Move(input, macros.Count);
			if (input.Back || macros.Count == 0)
			{
				b.Phase = b.FromTop ? BattlePhase.Top : BattlePhase.Command;
			}
			else if (input.Confirm && macros[_cursor].Length == 0)
			{
				// An empty slot of `macro_slots`: nothing to play.
				_audio.PlayCue("refuse");
			}
			else if (input.Confirm)
			{
				_lastMacro = macros[_cursor];
				_macroRound = true;
				PsivResult applied = _s.ApplyMacro(macros[_cursor], out IReadOnlyList<MacroStepResult> steps, MenuMacroFallback);
				if (!applied.Ok)
				{
					b.Log.Add(T("ui.battle.log.refused", applied.Error!.Text));
					b.Phase = BattlePhase.Command;
				}
				else
				{
					foreach (MacroStepResult step in steps.Where(x => x.Status != MacroStepStatus.Applied))
					{
						b.Log.Add(T("ui.battle.log.macro_step", step.Member, T(NameKind.MacroStepStatus, (int)step.Status), T(NameKind.MacroIssue, (int)step.Issue)));
					}
					Resolve();
				}
			}
			break;
		case BattlePhase.AutoChoice:
			Move(input, _autoChoiceStep == 0 ? AutoTroubles.Length : AutoPolicies.Length);
			if (input.Back)
			{
				b.Phase = _autoChoiceStep == 0 ? BattlePhase.Command : BattlePhase.AutoChoice;
				_cursor = _autoChoiceStep == 0 ? b.MenuIndex : Array.IndexOf(AutoTroubles, _autoChoiceTrouble);
				_autoChoiceStep = 0;
			}
			else if (input.Confirm && _autoChoiceStep == 0)
			{
				_autoChoiceTrouble = AutoTroubles[_cursor];
				_autoChoiceStep = 1;
				_cursor = 0;
			}
			else if (input.Confirm)
			{
				StartAutoPlay(_autoChoiceTrouble, AutoPolicies[_cursor]);
			}
			break;
		}
	}

	// Auto play under the trouble mode and policy the player chose at the Auto row (`auto_choose`): every round filled by
	// FillAutoRound with the auto play macro, else the save's auto macro, under them.
	private void StartAutoPlay(MacroTrouble trouble, MacroPolicy policy)
	{
		string? macro = _bindings.AutoPlayMacro.Length > 0 && _s.Macros().Contains(_bindings.AutoPlayMacro) ? _bindings.AutoPlayMacro : null;
		PsivResult set = _s.SetAutoPlayMacro(macro, trouble, policy);
		if (!set.Ok)
		{
			_battle.Log.Add(T("ui.battle.log.refused", set.Error!.Text));
			_battle.Phase = BattlePhase.Command;
			return;
		}
		_autoChosen = true;
		_battle.Log.Add(T("ui.battle.log.auto_mode", T(NameKind.MacroTrouble, (int)trouble), T(NameKind.MacroPolicy, (int)policy)));
		_battle.Auto = true;
		TakeNotices();
		Resolve();
	}

	// The macro list's row for the macro played last under `macro_flow` "remember", else the first.
	private int LastMacroRow() => MacroFlowRemember ? Math.Max(0, BattleMacros().IndexOf(_lastMacro)) : 0;

	// The battle layout's `macro_slots`: the Macro command's fixed slots by label, null without them.
	private List<string>? MacroSlotLabels => Layout.Screen(BattleScreen).MacroSlots;

	// The party's macros, or with the battle layout's `macro_auto_first` the one auto-battle plays (Settings.AutoMacro)
	// first. Under `macro_slots` one entry per slot: the save's macros in order, "" for an empty slot.
	private List<string> BattleMacros()
	{
		IReadOnlyList<string> macros = _s.Macros();
		if (MacroSlotLabels is { } slots)
		{
			return [.. slots.Select((_, i) => i < macros.Count ? macros[i] : "")];
		}
		string auto = _s.Settings().AutoMacro;
		return LayoutSwitch("macro_auto_first") && macros.Contains(auto) ? [auto, .. macros.Where(m => m != auto)] : [.. macros];
	}

	// The result window's pages: one, or as many as the battle layout's `result_page_lines` lines a page make.
	private int ResultPages()
	{
		int per = Ui.Param(BattleScreen, "result_page_lines", UiData(), 0);
		return per <= 0 ? 1 : Math.Max(1, (ShownResultLines(ResultLines(_s.BattleInfo())).Count + per - 1) / per);
	}

	// The level-ups step's pages: one, or as many as the battle layout's `level_page_lines` lines a page make.
	private int LevelPages()
	{
		int per = Ui.Param(BattleScreen, "level_page_lines", UiData(), 0);
		return per <= 0 ? 1 : Math.Max(1, (LevelLines(ResultLines(_s.BattleInfo())).Count + per - 1) / per);
	}

	// Left and Right walk the command rows laid out as one row.
	private void MoveAcross(ActionInput input, int count)
	{
		if (count == 0)
		{
			return;
		}
		if (input.Pressed(GameAction.Left))
		{
			_cursor = (_cursor + count - 1) % count;
		}
		if (input.Pressed(GameAction.Right))
		{
			_cursor = (_cursor + 1) % count;
		}
		_cursor = Math.Clamp(_cursor, 0, count - 1);
	}

	// The first member who takes commands this round, as NextActor would pick it.
	private int FirstActor()
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		BattleInfo? info = _s.BattleInfo();
		for (int i = 0; info is not null && i < info.PartyCount; i++)
		{
			if (all[i].Alive && (info.Vehicle.Length == 0 || all[i].Id == info.Vehicle))
			{
				return i;
			}
		}
		return -1;
	}

	// The top-level Run: the party tries to get away at once, every member who takes commands set to escape.
	private void RunAway()
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		BattleInfo? info = _s.BattleInfo();
		int first = FirstActor();
		for (int i = first; first >= 0 && info is not null && i < info.PartyCount; i++)
		{
			if (all[i].Alive && (info.Vehicle.Length == 0 || all[i].Id == info.Vehicle))
			{
				PsivResult result = _s.SetCommand(i, CommandKind.Escape, null, -1);
				if (!result.Ok)
				{
					_battle.Log.Add(T("ui.battle.log.refused", result.Error!.Text));
				}
			}
		}
		Resolve();
	}

	private void ChooseOption(BattleOption option)
	{
		_battle.Option = option;
		_cursor = 0;
		ChooseTargets(option.Targeting);
		if (_battle.Targets.Count == 0 && option.Targeting is not (Targeting.OneEnemy or Targeting.OneAlly or Targeting.OneFallenAlly))
		{
			SetAndAdvance(option.Command, option.Id, -1);
		}
		else
		{
			_battle.Phase = BattlePhase.Target;
		}
	}

	private string _probePartyStatus = "-";
	private string _probeFigurePoses = "none";

	// The battle probes `expect state` reads.
	private string BattleState(string key) => key switch
	{
		"timescale" => SpeedPercent().ToString(CultureInfo.InvariantCulture),
		"step" => _timeline.Current?.Description ?? "-",
		"battlephase" => _battle.Active ? _battle.Phase.ToString() : "-",
		"battlelog" => string.Join(" | ", _battle.Log),
		"battlelines" => _battle.Log.Count.ToString(CultureInfo.InvariantCulture),
		"battlelogshown" => _battle.LogShown ? "on" : "off",
		"battlesync" => $"{_syncChecks} checked, " + (_syncProblems.Count == 0 ? "all match" : string.Join("; ", _syncProblems)),
		"battleshake" => _probeShake.ToString(CultureInfo.InvariantCulture),
		// The screen's jolt the last battle frame drew, `X,Y`.
		"battlejolt" => _probeJolt,
		// Each enemy standing, front to back: its label, x, foot row and size.
		"enemyplaces" => _probeEnemyPlaces,
		// Each sprite instance the last frame drew: `TRACK frame N [flip] [trail]`.
		"animsprites" => _probeSprites.Count == 0 ? "none" : string.Join(", ", _probeSprites),
		"screenlight" => _probeLight.ToString("0.00", CultureInfo.InvariantCulture),
		"flash" => _probeFlash ? "on" : "off",
		"backdrop" => _probeBackdrop,
		"battleoverlay" => _probeOverlay,
		"figuresizes" => _probeBodies,
		"effectsize" => _probeEffectSize,
		"shown" => string.Join(", ", _show.All.Select((c, i) => $"{(i < _battle.Labels.Count ? _battle.Labels[i] : "?")} {c.Hp}/{c.MaxHp} {c.Tp}/{c.MaxTp}" +
			(c.Statuses.Count > 0 ? " " + string.Join(" ", c.Statuses) : "") + (c.Alive ? "" : " down"))),
		"victory" => _probeVictory,
		"formation" => $"{_show.Slots}: " + string.Join(" ", _show.All.Where(c => c.Position >= 0).Select(c => c.Position.ToString(CultureInfo.InvariantCulture))),
		// The command menu's rows for the phase, the one under the cursor in brackets.
		// Under the battle layout's `macro_slots`, the highlighted slot's macro steps (`none` for an empty slot), `-` off the list.
		"macrolines" => _battle.Active && _battle.Phase == BattlePhase.Macro ? _probeMacroLines : "-",
		"battlemenu" => _battle.Active && MenuRows(StageCombatants()) is { } menu
			? string.Join(" | ", menu.Items.Select((item, i) => i == _cursor ? $"[{item}]" : item)) : "-",
		// The rows of that menu that cannot be chosen (greyed), `-` for none.
		"battleunusable" => _battle.Active && MenuRows(StageCombatants()) is { Usable: { } usable } rows && usable.Contains(false)
			? string.Join(" | ", rows.Items.Where((_, i) => !usable[i])) : "-",
		// Each party combatant as its slot shows its statuses: the name's color from the layout's statuses table and the icons.
		"partystatus" => _probePartyStatus,
		// Each figure the last frame drew in a pose other than idle, by label: `Gel Mite A attack, Test Fighter flourish`.
		"figureposes" => _probeFigurePoses,
		// The enemies standing as the layouts' `enemy_groups` lists them: `Gel Mite x2, Rust Hound`.
		"enemygroups" => _probeEnemyGroups,
		// Each party combatant's chosen command as the `command` field names it, `-` for none.
		"commands" => _probeCommands,
		// What the last frame drew under the enemies: `labels N bars N`.
		"enemymarks" => _probeEnemyMarks,
		// The status icons the last frame drew under each enemy with a status, in order: `Gel Mite A sleep, Rust Hound none`.
		"enemyicons" => _probeEnemyIcons,
		// Each party figure drawn: its name, the middle x and the feet's y.
		"partyplaces" => _probePlaces,
		// The floating texts the last frame drew: `Gel Mite A 13 damage, Gel Mite B 24 Resisted resisted`.
		"numbers" => _probeNumbers.Count == 0 ? "none" : string.Join(", ", _probeNumbers),
		_ => AnimState(key) is var anim && anim != "?" ? anim : FrontState(key),
	};
}
