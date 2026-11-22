using Psiv.Session;

namespace Psiv.Sample;

// The menu's Macros page: the save's macros, and for each its steps. A step is built member, command, ability or
// item, then target rule; the session refuses what does not fit, and marks steps that cannot run now.
public sealed partial class SampleController
{
	private enum MacroStage { List, Actions, Steps, StepActions, Member, Command, Pick, Target, TargetMember, TargetEnemy, Name, Options }

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
	// The item step names an equipment piece (SetMacroPieceStep).
	private bool _draftPiece;

	// Who a step may name: the party's members, then its vehicles in the area, whose steps run only in battles aboard.
	private List<(string Id, string Name, bool Vehicle)> StepActors() =>
		[.. _s.Party().Select(m => (m.Id, m.Name, false)), .. _s.FieldVehicles().Select(v => (v.Id, v.Name, true))];

	private bool DraftVehicle => _data.VehicleSkills.ContainsKey(_draftMember) && !_s.Party().Any(m => m.Id == _draftMember);

	// A vehicle attacks, uses one of its skills or escapes.
	private CommandKind[] DraftCommands => DraftVehicle ? [CommandKind.Attack, CommandKind.Ability, CommandKind.Escape] : StepCommands;

	// What a step may pick: an ability of the member (a vehicle's skills), or an item of the bag, then the equipment pieces
	// with a battle use in the bag and on the member.
	private List<(string Id, string Text, bool Piece)> DraftPicks()
	{
		if (_draftKind == CommandKind.Ability && DraftVehicle)
		{
			return [.. _data.VehicleSkills[_draftMember].Select(id => (id, _s.DisplayName(id), false))];
		}
		if (_draftKind == CommandKind.Ability)
		{
			return [.. _s.MemberAbilities(_draftMember).Select(a => (a.Id, a.Name + (a.Cost > 0 ? "  " + T("ui.common.tp_cost", a.Cost) : "") + (a.MaxUses >= 0 ? $"  {a.Uses}/{a.MaxUses}" : ""), false))];
		}
		IReadOnlyList<InventoryEntry> bag = _s.Inventory();
		var picks = bag.Where(i => !i.IsEquipment).Select(i => (i.Id, T("ui.common.item_count", i.Name, i.Count), false)).ToList();
		picks.AddRange(bag.Where(i => i.IsEquipment && _data.PieceUses.ContainsKey(i.Id)).Select(i => (i.Id, T("ui.common.item_count", i.Name, i.Count), true)));
		if (_s.Party().FirstOrDefault(m => m.Id == _draftMember) is { } member)
		{
			picks.AddRange(member.Equipment.Select((id, i) => (id, i)).Where(x => _data.PieceUses.ContainsKey(x.id) && !picks.Any(p => p.Item1 == x.id))
				.Select(x => (x.id, member.EquipmentNames[x.i], true)));
		}
		return picks;
	}

	private string StepText(MacroStep step)
	{
		string command = step.Id.Length > 0 ? step.IdName : T(NameKind.CommandKind, (int)step.CommandKind);
		string target = step.TargetKind switch
		{
			MacroTargetKind.Member => step.TargetMemberName,
			MacroTargetKind.Enemy => T("ui.macros.enemy_target", step.TargetEnemy + 1),
			_ => T(NameKind.MacroTargetKind, (int)step.TargetKind),
		};
		return T("ui.macros.step", step.MemberName, command, target) + (step.Issue != MacroIssue.None ? $"  [{T(NameKind.MacroIssue, (int)step.Issue)}]" : "");
	}

	// After delete: whether auto-battle plays this macro every round (stuck members taking the automatic choice), and how a
	// macro used from the battle menu treats a member whose step cannot run; both the sample's, kept in controls.json.
	// With the Macros page's `options` param, an Options action last, for the macro's options (SetMacroOptions, SetMacroPolicy).
	private List<string> MacroActions(string name) =>
		[.. new[] { "edit_steps", "rename", "move_up", "move_down", _s.Settings().AutoMacro == name ? "stop_auto" : "use_auto", "delete",
			_bindings.AutoPlayMacro == name ? "stop_auto_play" : "use_auto_play", _bindings.MacroFallbackAutomatic ? "fallback_automatic" : "fallback_basic" }
			.Concat(MacroOptionsShown ? ["options"] : []).Select(a => T("ui.macros." + a))];

	private bool MacroOptionsShown => Ui.Param("menu_macros", "options", UiData(), 0) != 0;

	// The macro's options as rows: what repeating it does in trouble, who takes core's choice in a recover round, what a
	// member without a step does, and the automatic choice's policy; Default leaves each to the next level.
	private List<string> MacroOptionRows(Macro macro) =>
	[
		T("ui.macros.option.on_trouble", T(NameKind.MacroTrouble, (int)macro.OnTrouble)),
		T("ui.macros.option.recover_who", T(NameKind.MacroRecoverWho, (int)macro.RecoverWho)),
		T("ui.macros.option.unnamed", T(NameKind.MacroUnnamed, (int)macro.Unnamed)),
		T("ui.macros.option.policy", T(NameKind.MacroPolicy, (int)macro.Policy)),
	];

	// Steps the option at `row` by `by` through the session, which keeps it in the save.
	private void StepMacroOption(Macro macro, int row, int by)
	{
		Report(row switch
		{
			0 => _s.SetMacroOptions(macro.Name, Step(macro.OnTrouble, by), macro.RecoverWho, macro.Unnamed),
			1 => _s.SetMacroOptions(macro.Name, macro.OnTrouble, Step(macro.RecoverWho, by), macro.Unnamed),
			2 => _s.SetMacroOptions(macro.Name, macro.OnTrouble, macro.RecoverWho, Step(macro.Unnamed, by)),
			_ => _s.SetMacroPolicy(macro.Name, Step(macro.Policy, by)),
		});
	}

	private string[] StepActions => [.. new[] { "change", "move_up", "move_down", "remove" }.Select(a => T("ui.macros." + a))];

	// Each macro, marked when it is the auto macro and when a step cannot run now, then a row to make a new one.
	private List<string> MacroRows(IReadOnlyList<Macro> macros) =>
		[.. macros.Select(m => m.Name + (m.Name == _s.Settings().AutoMacro ? "  " + T("ui.macros.auto_mark") : "") + (m.Steps.Any(st => st.Issue != MacroIssue.None) ? "  !" : "")),
			T("ui.macros.new")];

	// The rows of the stage's own list, for its cursor and its drawing.
	private List<string> StageRows(IReadOnlyList<Macro> macros, Macro? macro) => _macroStage switch
	{
		MacroStage.List => MacroRows(macros),
		MacroStage.Actions when macro is not null => MacroActions(macro.Name),
		MacroStage.Steps when macro is not null => [.. macro.Steps.Select(StepText), T("ui.macros.add_step")],
		MacroStage.StepActions => [.. StepActions],
		MacroStage.Member => [.. StepActors().Select(a => a.Name)],
		MacroStage.TargetMember => [.. _s.Party().Select(m => m.Name)],
		MacroStage.Command => [.. DraftCommands.Select(k => T(NameKind.CommandKind, (int)k))],
		MacroStage.Pick => [.. DraftPicks().Select(p => p.Text)],
		MacroStage.Target => [.. StepTargets.Select(t => T(NameKind.MacroTargetKind, (int)t))],
		MacroStage.TargetEnemy => [.. Enumerable.Range(1, EnemyPositions).Select(i => T("ui.macros.enemy_position", i))],
		MacroStage.Options when macro is not null => MacroOptionRows(macro),
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
		if (_macroStage == MacroStage.Options && macro is not null && !input.Back)
		{
			int by = input.Confirm || input.Pressed(GameAction.Right) ? 1 : input.Pressed(GameAction.Left) ? -1 : 0;
			if (by != 0)
			{
				StepMacroOption(macro, list.Cursor, by);
			}
			return;
		}
		if (input.Back)
		{
			Stage(_macroStage switch
			{
				MacroStage.Actions or MacroStage.Steps => MacroStage.List,
				MacroStage.Options => MacroStage.Actions,
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
			_draftMember = StepActors()[at].Id;
			Stage(MacroStage.Command);
			break;
		case MacroStage.Command:
			_draftKind = DraftCommands[at];
			_draftId = null;
			_draftPiece = false;
			Stage(_draftKind is CommandKind.Ability or CommandKind.Item ? MacroStage.Pick : MacroStage.Target);
			break;
		case MacroStage.Pick:
			(_draftId, _, _draftPiece) = DraftPicks()[at];
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
		case 6:
			_bindings.AutoPlayMacro = _bindings.AutoPlayMacro == macro.Name ? "" : macro.Name;
			SaveControls();
			break;
		case 7:
			_bindings.MacroFallbackAutomatic = !_bindings.MacroFallbackAutomatic;
			SaveControls();
			break;
		case 8:
			Stage(MacroStage.Options);
			break;
		case 5:
			_confirm.Ask(T("ui.macros.delete_confirm", macro.Name), () =>
			{
				PsivResult deleted = _s.DeleteMacro(macro.Name);
				Report(deleted);
				if (deleted.Ok)
				{
					Say(T("ui.macros.deleted", macro.Name));
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
		PsivResult result = _draftPiece && _draftId is not null
			? _s.SetMacroPieceStep(macro.Name, _stepIndex, _draftMember, _draftId, target, targetMember, targetEnemy)
			: _s.SetMacroStep(macro.Name, _stepIndex, _draftMember, _draftKind, _draftId, target, targetMember, targetEnemy);
		Report(result);
		if (result.Ok)
		{
			Stage(MacroStage.Steps);
			_sub.Cursor = _stepIndex;
		}
	}

	// The battle's auto play (Resolve): with an auto play macro chosen, every round is filled with it, each member whose
	// step cannot run taking core's automatic choice (FillAutoRound after SetAutoPlayMacro); otherwise the save's auto macro
	// through StartAuto and FillFromAuto, as before. A trouble mode or policy chosen at the Auto row (`auto_choose`) or in
	// the settings rows `auto_trouble` and `auto_policy` goes with the macro, the save's auto macro then also filling the
	// round through FillAutoRound. A round filled that way logs the trouble it met. False when auto play stops.
	private bool FillAuto()
	{
		bool modes = _bindings.AutoTrouble != MacroTrouble.Default || _bindings.AutoPolicy != MacroPolicy.Default;
		string macro = _bindings.AutoPlayMacro.Length > 0 && _s.Macros().Contains(_bindings.AutoPlayMacro) ? _bindings.AutoPlayMacro : "";
		if (!_autoChosen && macro.Length == 0 && !modes)
		{
			return _s.FillFromAuto();
		}
		if (!_autoChosen && _autoPlaying != (macro.Length > 0 ? macro : SaveAutoMacro))
		{
			_autoPlaying = macro.Length > 0 ? macro : SaveAutoMacro;
			Report(modes ? _s.SetAutoPlayMacro(macro.Length > 0 ? macro : null, _bindings.AutoTrouble, _bindings.AutoPolicy) : _s.SetAutoPlayMacro(macro));
			if (macro.Length > 0)
			{
				_battle.Log.Add(T("ui.battle.log.auto_play", macro));
			}
		}
		_s.FillAutoRound();
		if (_s.TroubleReport(null, out MacroTroubleReport trouble).Ok && trouble.Holds)
		{
			_battle.Log.Add(TroubleText(trouble));
		}
		return true;
	}

	// The round's trouble (AutoRound.Trouble) for the log: the members in trouble, then those core's choice took over.
	private string TroubleText(MacroTroubleReport trouble)
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		string Names(IReadOnlyList<int> who) => who.Count == 0 ? T("ui.battle.log.trouble_nobody")
			: ListText(who.Where(i => i >= 0 && i < all.Count).Select(i => all[i].Name));
		return T("ui.battle.log.trouble", T(NameKind.MacroTrouble, (int)trouble.Mode), Names([.. trouble.Fallen.Concat(trouble.TroubledMembers).Distinct()]),
			Names(trouble.Recovered));
	}

	// The auto play macro given to the session for the battle under way, "" before its first round; SaveAutoMacro stands for
	// the save's auto macro given with a trouble mode or policy.
	private string _autoPlaying = "";
	private const string SaveAutoMacro = "\u0001save";

	// The Auto row gave the session a trouble mode and policy for the battle under way (`auto_choose`).
	private bool _autoChosen;

	// How a macro picked from the battle menu treats a member whose step cannot run.
	private MacroFallback BattleMacroFallback => _bindings.MacroFallbackAutomatic ? MacroFallback.Automatic : MacroFallback.BasicAttack;

	private void DrawMacros(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		IReadOnlyList<Macro> macros = _s.MacroList();
		Macro? macro = _list.Cursor < macros.Count ? macros[_list.Cursor] : null;
		bool building = _macroStage is not (MacroStage.List or MacroStage.Steps or MacroStage.Actions or MacroStage.Name);
		UiContext data = UiArea(x, y, w, h)
			.Set("macro_count", macros.Count)
			.Set("macro_rows", MacroRows(macros))
			.Set("list", _list)
			.Set("stage", _macroStage.ToString().ToLowerInvariant())
			.Set("building", building)
			.Set("has_macro", macro is not null)
			.Set("name_prompt", _renaming is null ? T("ui.macros.name_new") : T("ui.macros.rename_to", _renaming))
			.Set("name_pad", (UiSlot)((nx, ny, nw, _) => _macroName.Draw(t, nx, ny, nw, _clockMs, Px, Pad("menu_macros", UiArea(x, y, w, h)), PadLabels(), Timing("caret_ms"))))
			.Set("sub_list", _sub)
			.Set("stage_list", _third);
		data.Set("on_trouble", macro is null ? "" : GameSession.NameOf(NameKind.MacroTrouble, (int)macro.OnTrouble))
			.Set("recover_who", macro is null ? "" : GameSession.NameOf(NameKind.MacroRecoverWho, (int)macro.RecoverWho))
			.Set("unnamed", macro is null ? "" : GameSession.NameOf(NameKind.MacroUnnamed, (int)macro.Unnamed))
			.Set("policy", macro is null ? "" : GameSession.NameOf(NameKind.MacroPolicy, (int)macro.Policy))
			.Set("option_rows", macro is null ? new List<string>() : MacroOptionRows(macro));
		if (macro is not null)
		{
			data.Set("title", macro.Name + (macro.Name == _s.Settings().AutoMacro ? "  " + T("ui.macros.used_for_auto") : ""))
				.Set("actions", MacroActions(macro.Name))
				.Set("step_notes", macro.Steps.Select(step => new UiContext(data).Set("text", StepText(step)).Set("ok", step.Issue == MacroIssue.None)).ToList())
				.Set("steps", macro.Steps.Select(step => new UiContext(data).Set("text", StepText(step)).Set("enabled", step.Issue == MacroIssue.None))
					.Append(new UiContext(data).Set("text", T("ui.macros.add_step")).Set("enabled", true)).ToList());
			if (building)
			{
				data.Set("what", _macroStage switch
				{
					MacroStage.StepActions => T("ui.macros.step_number", _stepIndex + 1),
					MacroStage.Member => T("ui.macros.who"),
					MacroStage.Command => T("ui.macros.does_what", _s.DisplayName(_draftMember)),
					MacroStage.Pick => T(_draftKind == CommandKind.Ability ? "ui.macros.which_ability" : "ui.macros.which_item"),
					MacroStage.Target => T("ui.macros.aimed_at"),
					MacroStage.TargetMember => T("ui.macros.which_member"),
					MacroStage.Options => T("ui.macros.options_title"),
					_ => T("ui.macros.which_enemy"),
				}).Set("stage_rows", StageRows(macros, macro));
			}
		}
		Ui.Draw("menu_macros", s, t, data);
	}
}
