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

	private int SpeedPercent() => (int)MathF.Round(SpeedOf(_s.Profile().BattleSpeed) * 100);

	private enum BattlePhase { Command, Option, Target, Macro, Playback, Ended, Top }

	// The phases with a menu to choose from.
	private static bool Choosing(BattlePhase phase) => phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro or BattlePhase.Top;

	// The top-level step's rows, before the members' commands when the battle layout's `command_top` is on; its
	// `command_rows.top` can leave some out.
	private string[] TopMenu => [.. Layout.Screen(BattleScreen).TopCommands ?? [.. UiFields.TopCommands]];

	// The battle layout's command navigation: `command_axis` "row" makes Left and Right walk the command rows (Up and Down
	// then do nothing), and `command_top` puts the top-level step (fight, macro, run) before the members' commands each round.
	private string BattleScreen => StageLayout() == BattleLayout.Front ? "battle_front" : "battle_side";

	private bool LayoutSwitch(string name) => Ui.Param(BattleScreen, name, UiData(), 0) != 0;

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
		public List<string> Labels = [];
		public readonly List<string> Log = [];
		public bool Auto;
		public bool LogShown;
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

	// A vehicle has only its attack, its skills and escape; the members have the whole menu.
	private List<CommandRow> CommandRows()
	{
		IReadOnlyList<Combatant> all = StageCombatants();
		string vehicle = StageInfo()?.Vehicle ?? "";
		if (vehicle.Length > 0 && _battle.Actor < all.Count && all[_battle.Actor].Id == vehicle && _stage is null)
		{
			return [new("attack", T("ui.battle.command.attack")),
				.. _s.BattleOptions(_battle.Actor).Where(o => o.AbilityKind == AbilityKind.Skill).Select(o => new CommandRow("skill", OptionText(o), o)),
				new("escape", T("ui.battle.command.escape"))];
		}
		return [.. CommandMenu.Select(c => new CommandRow(c, T("ui.battle.command." + c)))];
	}

	private string OptionText(BattleOption o) => _battle.Worn.Contains(o.Id) ? T("ui.battle.worn_row", o.Name)
		: o.Name + (o.Cost > 0 ? " " + T("ui.battle.cost", o.Cost) : "") + (o.Uses >= 0 ? " " + T("ui.common.count", o.Uses) : "");

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
		string screen = StageLayout() == BattleLayout.Front ? "battle_front" : "battle_side";
		return new BattleContext(_s, _battle.Labels, PresentationTable, _data, T, target => Marked(screen, new UiContext(), "critical", target));
	}

	private void StartBattle()
	{
		_battle.Active = true;
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
				_cursor = 0;
				return;
			}
			for (int i = from; i < info.PartyCount; i++)
			{
				// In a vehicle battle the vehicle takes the commands and the members ride along.
				if (all[i].Alive && (info.Vehicle.Length == 0 || all[i].Id == info.Vehicle))
				{
					_battle.Actor = i;
					_battle.Phase = BattlePhase.Command;
					_cursor = 0;
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
			if (_timeline.Done)
			{
				_syncChecks++;
				if (_show.Differences(_s.Combatants(), b.Labels) is { Length: > 0 } differences)
				{
					_syncProblems.Add(differences);
					_log("battle: the playback ended away from the session: " + differences);
				}
				_show.Reset(_s.Combatants());
				if (_s.Mode != Mode.Battle)
				{
					b.Phase = BattlePhase.Ended;
					b.ResultPage = 0;
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
			if (_outro is null && input.Confirm && b.ResultPage + 1 < ResultPages())
			{
				b.ResultPage++;
			}
			else if (_outro is null && (input.Confirm || input.Back))
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
			case "macro" when _s.Macros().Count == 0:
				Say(T("ui.battle.no_macros"));
				b.Log.Add(T("ui.battle.no_macros"));
				break;
			case "macro":
				b.MenuChoice = "macro";
				b.FromTop = true;
				_cursor = 0;
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
					foreach (BattleOption worn in WornOptions(b.Actor))
					{
						b.Worn.Add(worn.Id);
						b.Options.Add(worn);
					}
				}
				b.Phase = b.Options.Count > 0 ? BattlePhase.Option : BattlePhase.Command;
				if (b.Options.Count == 0)
				{
					b.Log.Add(T("ui.battle.log.nothing_to_use." + b.MenuChoice));
				}
				break;
			case "defend":
				SetAndAdvance(CommandKind.Defend, null, -1);
				break;
			case "escape":
				SetAndAdvance(CommandKind.Escape, null, -1);
				break;
			case "macro":
				b.Phase = BattlePhase.Macro;
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
			IReadOnlyList<string> macros = _s.Macros();
			Move(input, macros.Count);
			if (input.Back || macros.Count == 0)
			{
				b.Phase = b.FromTop ? BattlePhase.Top : BattlePhase.Command;
			}
			else if (input.Confirm)
			{
				PsivResult applied = _s.ApplyMacro(macros[_cursor], out IReadOnlyList<MacroStepResult> steps, BattleMacroFallback);
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
		}
	}

	// The result window's pages: one, or as many as the battle layout's `result_page_lines` lines a page make.
	private int ResultPages()
	{
		int per = Ui.Param(StageLayout() == BattleLayout.Front ? "battle_front" : "battle_side", "result_page_lines", UiData(), 0);
		return per <= 0 ? 1 : Math.Max(1, (ResultLines(_s.BattleInfo()).Count + per - 1) / per);
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
		"timescale" => ((int)MathF.Round(SpeedOf(_s.Profile().BattleSpeed) * 100)).ToString(CultureInfo.InvariantCulture),
		"step" => _timeline.Current?.Description ?? "-",
		"battlephase" => _battle.Active ? _battle.Phase.ToString() : "-",
		"battlelog" => string.Join(" | ", _battle.Log),
		"battlelines" => _battle.Log.Count.ToString(CultureInfo.InvariantCulture),
		"battlelogshown" => _battle.LogShown ? "on" : "off",
		"battlesync" => $"{_syncChecks} checked, " + (_syncProblems.Count == 0 ? "all match" : string.Join("; ", _syncProblems)),
		"battleshake" => _probeShake.ToString(CultureInfo.InvariantCulture),
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
		"battlemenu" => _battle.Active && MenuRows(StageCombatants()) is { } menu
			? string.Join(" | ", menu.Items.Select((item, i) => i == _cursor ? $"[{item}]" : item)) : "-",
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
