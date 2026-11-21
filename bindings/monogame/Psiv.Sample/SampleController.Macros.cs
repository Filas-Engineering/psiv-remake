using Psiv.Session;

namespace Psiv.Sample;

// The menu's Macros page: the save's macros, and for each its steps. A step is built member, command, ability or
// item, then target rule; the session refuses what does not fit, and marks steps that cannot run now.
public sealed partial class SampleController
{
	private enum MacroStage { List, Actions, Steps, StepActions, Member, Command, Pick, Target, TargetMember, TargetEnemy, Name }

	private static readonly CommandKind[] StepCommands = [CommandKind.Attack, CommandKind.Ability, CommandKind.Item, CommandKind.Defend, CommandKind.Escape];
	private static readonly MacroTargetKind[] StepTargets = [MacroTargetKind.Default, MacroTargetKind.Self, MacroTargetKind.Member, MacroTargetKind.Enemy, MacroTargetKind.All];
	private const int EnemyPositions = 8;

	private MacroStage _macroStage;
	private readonly TextEntry _macroName = new(16);
	// Null while naming a new macro, else the macro being renamed.
	private string? _renaming;
	// The step being written: its index (the step count to add one), member, command and ability or item.
	private int _stepIndex;
	private string _draftMember = "";
	private CommandKind _draftKind;
	private string? _draftId;

	private static string Words(MacroIssue issue) => Words(GameSession.NameOf(NameKind.MacroIssue, (int)issue));

	private static string StepText(MacroStep step)
	{
		string command = step.Id.Length > 0 ? step.IdName : Words(GameSession.NameOf(NameKind.CommandKind, (int)step.CommandKind));
		string target = step.TargetKind switch
		{
			MacroTargetKind.Member => step.TargetMemberName,
			MacroTargetKind.Enemy => $"enemy {step.TargetEnemy + 1}",
			_ => Words(GameSession.NameOf(NameKind.MacroTargetKind, (int)step.TargetKind)),
		};
		return $"{step.MemberName}: {command} -> {target}" + (step.Issue != MacroIssue.None ? $"  [{Words(step.Issue)}]" : "");
	}

	private List<string> MacroActions(string name) =>
		["Edit steps", "Rename", "Move up", "Move down", _s.Settings().AutoMacro == name ? "Stop using for auto-battle" : "Use for auto-battle", "Delete"];

	private static string[] StepActions => ["Change", "Move up", "Move down", "Remove"];

	// Each macro, marked when it is the auto macro and when a step cannot run now, then a row to make a new one.
	private List<string> MacroRows(IReadOnlyList<Macro> macros) =>
		[.. macros.Select(m => m.Name + (m.Name == _s.Settings().AutoMacro ? "  (auto)" : "") + (m.Steps.Any(st => st.Issue != MacroIssue.None) ? "  !" : "")), "+ New macro"];

	// The rows of the stage's own list, for its cursor and its drawing.
	private List<string> StageRows(IReadOnlyList<Macro> macros, Macro? macro) => _macroStage switch
	{
		MacroStage.List => MacroRows(macros),
		MacroStage.Actions when macro is not null => MacroActions(macro.Name),
		MacroStage.Steps when macro is not null => [.. macro.Steps.Select(StepText), "+ Add step"],
		MacroStage.StepActions => [.. StepActions],
		MacroStage.Member or MacroStage.TargetMember => [.. _s.Party().Select(m => m.Name)],
		MacroStage.Command => [.. StepCommands.Select(k => Words(GameSession.NameOf(NameKind.CommandKind, (int)k)))],
		MacroStage.Pick => _draftKind == CommandKind.Ability
			? [.. _s.MemberAbilities(_draftMember).Select(a => a.Name + (a.Cost > 0 ? $"  {a.Cost} TP" : "") + (a.MaxUses >= 0 ? $"  {a.Uses}/{a.MaxUses}" : ""))]
			: [.. _s.Inventory().Where(i => !i.IsEquipment).Select(i => $"{i.Name} x{i.Count}")],
		MacroStage.Target => [.. StepTargets.Select(t => Words(GameSession.NameOf(NameKind.MacroTargetKind, (int)t)))],
		MacroStage.TargetEnemy => [.. Enumerable.Range(1, EnemyPositions).Select(i => $"Enemy {i}")],
		_ => [],
	};

	// The list each stage moves: macros on the left, then the macro's actions or steps, then the step being built.
	private ScrollList StageList => _macroStage switch
	{
		MacroStage.List => _list,
		MacroStage.Actions or MacroStage.Steps => _sub,
		_ => _third,
	};

	private void Stage(MacroStage stage)
	{
		_macroStage = stage;
		if (StageList != _list)
		{
			StageList.Reset();
		}
	}

	private void UpdateMacros(ActionInput input)
	{
		IReadOnlyList<Macro> macros = _s.MacroList();
		Macro? macro = _list.Cursor < macros.Count ? macros[_list.Cursor] : null;
		if (_macroStage == MacroStage.Name)
		{
			bool? done = _macroName.Update(input);
			if (done == false)
			{
				Stage(_renaming is null ? MacroStage.List : MacroStage.Actions);
			}
			else if (done == true)
			{
				string name = _macroName.Buffer.Trim();
				PsivResult result = _renaming is null ? _s.CreateMacro(name) : _s.RenameMacro(_renaming, name);
				Report(result);
				if (result.Ok)
				{
					_list.Cursor = _s.MacroList().Select(m => m.Name).ToList().IndexOf(name);
					Stage(_renaming is null ? MacroStage.List : MacroStage.Actions);
				}
			}
			return;
		}
		List<string> rows = StageRows(macros, macro);
		ScrollList list = StageList;
		list.Move(input, rows.Count);
		if (input.Back)
		{
			Stage(_macroStage switch
			{
				MacroStage.Actions or MacroStage.Steps => MacroStage.List,
				MacroStage.StepActions or MacroStage.Member => MacroStage.Steps,
				MacroStage.Command => MacroStage.Member,
				MacroStage.Pick => MacroStage.Command,
				MacroStage.Target => _draftId is not null ? MacroStage.Pick : MacroStage.Command,
				_ => MacroStage.Target,
			});
			if (_macroStage == MacroStage.Steps)
			{
				_sub.Cursor = _stepIndex;
			}
			return;
		}
		if (!input.Confirm || rows.Count == 0)
		{
			return;
		}
		int at = list.Cursor;
		switch (_macroStage)
		{
		case MacroStage.List when macro is null:
			_renaming = null;
			_macroName.Reset();
			Stage(MacroStage.Name);
			break;
		case MacroStage.List:
			Stage(MacroStage.Actions);
			break;
		case MacroStage.Actions when macro is not null:
			RunMacroAction(macro, at);
			break;
		case MacroStage.Steps when macro is not null:
			_stepIndex = at;
			Stage(at == macro.Steps.Count ? MacroStage.Member : MacroStage.StepActions);
			break;
		case MacroStage.StepActions when macro is not null:
			RunStepAction(macro, at);
			break;
		case MacroStage.Member:
			_draftMember = _s.Party()[at].Id;
			Stage(MacroStage.Command);
			break;
		case MacroStage.Command:
			_draftKind = StepCommands[at];
			_draftId = null;
			Stage(_draftKind is CommandKind.Ability or CommandKind.Item ? MacroStage.Pick : MacroStage.Target);
			break;
		case MacroStage.Pick:
			_draftId = _draftKind == CommandKind.Ability ? _s.MemberAbilities(_draftMember)[at].Id : _s.Inventory().Where(i => !i.IsEquipment).ToList()[at].Id;
			Stage(MacroStage.Target);
			break;
		case MacroStage.Target when macro is not null:
			switch (StepTargets[at])
			{
			case MacroTargetKind.Member: Stage(MacroStage.TargetMember); break;
			case MacroTargetKind.Enemy: Stage(MacroStage.TargetEnemy); break;
			default: WriteStep(macro, StepTargets[at]); break;
			}
			break;
		case MacroStage.TargetMember when macro is not null:
			WriteStep(macro, MacroTargetKind.Member, _s.Party()[at].Id);
			break;
		case MacroStage.TargetEnemy when macro is not null:
			WriteStep(macro, MacroTargetKind.Enemy, null, at);
			break;
		}
	}

	private void RunMacroAction(Macro macro, int action)
	{
		int position = _list.Cursor;
		switch (action)
		{
		case 0:
			Stage(MacroStage.Steps);
			break;
		case 1:
			_renaming = macro.Name;
			_macroName.Reset(macro.Name);
			Stage(MacroStage.Name);
			break;
		case 2 or 3:
			int to = position + (action == 2 ? -1 : 1);
			PsivResult moved = _s.MoveMacro(macro.Name, to);
			Report(moved);
			if (moved.Ok)
			{
				_list.Cursor = to;
			}
			break;
		case 4:
			Report(_s.SetAutoMacro(_s.Settings().AutoMacro == macro.Name ? null : macro.Name));
			break;
		case 5:
			_confirm.Ask($"Delete the macro {macro.Name}?", () =>
			{
				PsivResult deleted = _s.DeleteMacro(macro.Name);
				Report(deleted);
				if (deleted.Ok)
				{
					Say($"Deleted {macro.Name}.");
					Stage(MacroStage.List);
				}
			});
			break;
		}
	}

	private void RunStepAction(Macro macro, int action)
	{
		switch (action)
		{
		case 0:
			Stage(MacroStage.Member);
			return;
		case 1 or 2:
			int to = _stepIndex + (action == 1 ? -1 : 1);
			PsivResult moved = _s.MoveMacroStep(macro.Name, _stepIndex, to);
			Report(moved);
			if (moved.Ok)
			{
				_stepIndex = to;
			}
			break;
		case 3:
			Report(_s.RemoveMacroStep(macro.Name, _stepIndex));
			break;
		}
		Stage(MacroStage.Steps);
		_sub.Cursor = _stepIndex;
	}

	private void WriteStep(Macro macro, MacroTargetKind target, string? targetMember = null, int targetEnemy = -1)
	{
		PsivResult result = _s.SetMacroStep(macro.Name, _stepIndex, _draftMember, _draftKind, _draftId, target, targetMember, targetEnemy);
		Report(result);
		if (result.Ok)
		{
			Stage(MacroStage.Steps);
			_sub.Cursor = _stepIndex;
		}
	}

	private void DrawMacros(IDrawSurface s, int x, int y, int w, int h)
	{
		IReadOnlyList<Macro> macros = _s.MacroList();
		Macro? macro = _list.Cursor < macros.Count ? macros[_list.Cursor] : null;
		int leftW = w * 2 / 5 - 10;
		int rx = x + leftW + 30;
		int rw = w - leftW - 30;
		MacroStage stage = _macroStage;
		s.Text($"Macros ({macros.Count})", x, y, Style.Accent, Style.Small);
		DrawItems(s, _list, x, y + Px(30), leftW, h - Px(30), MacroRows(macros), stage == MacroStage.List);
		if (macro is null)
		{
			s.Text("A new macro starts empty; add a step per member.", rx, y + Px(30), Style.Dim, Style.Small);
			if (stage == MacroStage.Name)
			{
				DrawMacroName(s, rx, y, rw);
			}
			return;
		}
		// The macro's steps, or its actions, on the right; the step being built under them.
		string title = macro.Name + (macro.Name == _s.Settings().AutoMacro ? "  (used for auto-battle)" : "");
		s.Text(Style.Fit(s, title, rw, Style.Small), rx, y, Style.Accent, Style.Small);
		if (stage == MacroStage.Name)
		{
			DrawMacroName(s, rx, y, rw);
			return;
		}
		int stepsH = Px(30) * Math.Min(6, macro.Steps.Count + 1);
		if (stage == MacroStage.Actions)
		{
			DrawItems(s, _sub, rx, y + Px(30), rw, h - Px(30), MacroActions(macro.Name));
			int ay = y + Px(30) + Px(30) * 6 + Px(16);
			foreach (MacroStep step in macro.Steps)
			{
				s.Text(Style.Fit(s, StepText(step), rw - 20, Style.Small), rx + 14, ay, step.Issue == MacroIssue.None ? Style.Dim : Style.Bad, Style.Small);
				ay += Px(24);
			}
			return;
		}
		List<string> steps = [.. macro.Steps.Select(StepText), "+ Add step"];
		bool onSteps = stage == MacroStage.Steps || stage == MacroStage.List;
		DrawItems(s, _sub, rx, y + Px(30), rw, stage == MacroStage.List ? h - Px(30) : stepsH, steps, stage == MacroStage.Steps,
			steps.Select((_, i) => i >= macro.Steps.Count || macro.Steps[i].Issue == MacroIssue.None).ToList());
		if (onSteps)
		{
			return;
		}
		int by = y + Px(30) + stepsH + Px(14);
		string what = stage switch
		{
			MacroStage.StepActions => $"Step {_stepIndex + 1}",
			MacroStage.Member => "Who acts?",
			MacroStage.Command => $"{_s.DisplayName(_draftMember)} does what?",
			MacroStage.Pick => _draftKind == CommandKind.Ability ? "Which ability?" : "Which item?",
			MacroStage.Target => "Aimed at?",
			MacroStage.TargetMember => "Which member?",
			_ => "Which enemy position?",
		};
		s.Fill(rx, by - Px(6), rw, 1, Style.PanelEdge * 0.4f);
		s.Text(what, rx, by, Style.Accent, Style.Small);
		DrawItems(s, _third, rx, by + Px(30), rw, y + h - by - Px(30), StageRows(macros, macro));
	}

	private void DrawMacroName(IDrawSurface s, int x, int y, int w)
	{
		s.Text(_renaming is null ? "Name the new macro:" : $"Rename {_renaming} to:", x, y + Px(64), Style.Accent);
		_macroName.Draw(s, x, y + Px(100), w - 20, _clockMs, Px);
	}
}
