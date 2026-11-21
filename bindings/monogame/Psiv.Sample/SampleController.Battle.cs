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

	private enum BattlePhase { Command, Option, Target, Macro, Playback, Ended }

	private sealed class BattleUi
	{
		public bool Active;
		public BattlePhase Phase;
		public int Actor;
		public string MenuChoice = "";
		public List<BattleOption> Options = [];
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
	// Rounds whose playback was compared with the session as it ended, and what differed.
	private int _syncChecks;
	private readonly List<string> _syncProblems = [];

	// Every presentation step started, by its description, for headless expectations.
	public List<string> PresentedSteps { get; } = [];

	private static readonly string[] CommandMenu = ["Attack", "Technique", "Skill", "Item", "Defend", "Escape", "Macro", "Auto"];

	private BattleContext Context()
	{
		int partyCount = StageInfo()?.PartyCount ?? 0;
		return new BattleContext(_s, _battle.Labels, PresentationTable, _data, e => BattleCue(e, partyCount));
	}

	private void StartBattle()
	{
		_battle.Active = true;
		_battle.Labels = _s.Combatants().Select(c => c.Label).ToList();
		_battle.Log.Clear();
		_battle.Auto = false;
		_timeline.Clear();
		_show.Reset(_s.Combatants());
		string enemies = string.Join(", ", _s.Combatants().Where(c => c.Side == Side.Enemy).Select(c => c.Label));
		BattleStep intro = Context().Step(StepKind.Intro, enemies, "intro " + enemies, []);
		intro.Lines.Add($"Enemies: {enemies}.");
		intro.Cues.Add("battle_start");
		_timeline.Add([intro]);
		_battle.Phase = BattlePhase.Playback;
	}

	// A notice during a battle (a level-up, an autosave) plays as its own step after what is already queued.
	private void AddBattleNotice(Notice n)
	{
		BattleStep step = Context().Step(StepKind.Notice, n.Text, "notice " + n.Text, []);
		step.Lines.Add(n.Text);
		if (NoticeCue(n) is { } cue)
		{
			step.Cues.Add(cue);
		}
		_timeline.Add([step]);
	}

	private void NextActor(int from)
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		BattleInfo? info = _s.BattleInfo();
		if (info is not null && info.Live && info.PartyActsNextRound)
		{
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
		if (_battle.Auto && !_s.FillFromAuto())
		{
			_battle.Auto = false;
			TakeNotices();
			NextActor(0);
			return;
		}
		_timeline.Add(BattleTimeline.Build(_s.ResolveRound(), Context()));
		TakeNotices();
		_battle.Phase = BattlePhase.Playback;
	}

	// A step starts: its log lines, its cues (none while fast-forwarding) and what it changes on the stage.
	private void PlayStep(BattleStep step, bool quiet)
	{
		PresentedSteps.Add(step.Description);
		_log("present: " + step.Description);
		foreach (string line in step.Lines)
		{
			_battle.Log.Add(line);
			_log("battle: " + line);
		}
		if (!quiet)
		{
			foreach (string cue in step.Cues)
			{
				_audio.PlayCue(cue);
			}
		}
		_show.Apply(step);
	}

	private void SetAndAdvance(CommandKind kind, string? id, int target)
	{
		PsivResult result = _s.SetCommand(_battle.Actor, kind, id, target);
		if (!result.Ok)
		{
			_battle.Log.Add("! " + result.Error!.Text);
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
		if (b.Phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro && input.Pressed(GameAction.Pause))
		{
			OpenPause();
			return;
		}
		switch (b.Phase)
		{
		case BattlePhase.Playback:
			if (b.Auto && input.Back)
			{
				_s.CancelAuto();
				b.Log.Add("Auto-battle will stop after this round.");
			}
			bool fast = input.Down(GameAction.FastForward);
			List<BattleStep> started = fast ? _timeline.SkipAll() : input.Confirm ? _timeline.Hurry() : _timeline.Advance(dtMs * SpeedOf(_s.Profile().BattleSpeed));
			foreach (BattleStep step in started)
			{
				PlayStep(step, quiet: fast);
			}
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
			if (input.Confirm || input.Back)
			{
				b.Active = false;
			}
			break;
		case BattlePhase.Command:
			Move(input, CommandMenu.Length);
			if (!input.Confirm)
			{
				break;
			}
			b.MenuChoice = CommandMenu[_cursor];
			_cursor = 0;
			switch (b.MenuChoice)
			{
			case "Attack":
				ChooseTargets(Targeting.OneEnemy);
				b.Option = null;
				b.Phase = BattlePhase.Target;
				break;
			case "Technique":
			case "Skill":
			case "Item":
				b.Options = _s.BattleOptions(b.Actor).Where(o => b.MenuChoice switch
				{
					"Technique" => o.AbilityKind == AbilityKind.Technique,
					"Skill" => o.AbilityKind == AbilityKind.Skill,
					_ => o.Command == CommandKind.Item,
				}).ToList();
				b.Phase = b.Options.Count > 0 ? BattlePhase.Option : BattlePhase.Command;
				if (b.Options.Count == 0)
				{
					b.Log.Add($"No {b.MenuChoice.ToLowerInvariant()} to use.");
				}
				break;
			case "Defend":
				SetAndAdvance(CommandKind.Defend, null, -1);
				break;
			case "Escape":
				SetAndAdvance(CommandKind.Escape, null, -1);
				break;
			case "Macro":
				b.Phase = BattlePhase.Macro;
				break;
			case "Auto":
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
					b.Log.Add("! " + auto.Error!.Text);
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
				b.Option = b.Options[_cursor];
				_cursor = 0;
				ChooseTargets(b.Option.Targeting);
				if (b.Targets.Count == 0 && b.Option.Targeting is not (Targeting.OneEnemy or Targeting.OneAlly or Targeting.OneFallenAlly))
				{
					SetAndAdvance(b.Option.Command, b.Option.Id, -1);
				}
				else
				{
					b.Phase = BattlePhase.Target;
				}
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
				b.Phase = BattlePhase.Command;
			}
			else if (input.Confirm)
			{
				PsivResult applied = _s.ApplyMacro(macros[_cursor], out IReadOnlyList<MacroStepResult> steps);
				if (!applied.Ok)
				{
					b.Log.Add("! " + applied.Error!.Text);
					b.Phase = BattlePhase.Command;
				}
				else
				{
					foreach (MacroStepResult step in steps.Where(x => x.Status != MacroStepStatus.Applied))
					{
						b.Log.Add($"  {step.Member}: {GameSession.NameOf(NameKind.MacroStepStatus, (int)step.Status)} ({GameSession.NameOf(NameKind.MacroIssue, (int)step.Issue)})");
					}
					Resolve();
				}
			}
			break;
		}
	}

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
		"flash" => _probeFlash ? "on" : "off",
		"shown" => string.Join(", ", _show.All.Select((c, i) => $"{(i < _battle.Labels.Count ? _battle.Labels[i] : "?")} {c.Hp}/{c.MaxHp} {c.Tp}/{c.MaxTp}" +
			(c.Statuses.Count > 0 ? " " + string.Join(" ", c.Statuses) : "") + (c.Alive ? "" : " down"))),
		"victory" => _probeVictory,
		_ => FrontState(key),
	};
}
