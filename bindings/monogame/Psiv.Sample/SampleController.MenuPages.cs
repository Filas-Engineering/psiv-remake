using Psiv.Session;

namespace Psiv.Sample;

// The field menu's technique, skill and ability pages (a member, then their list of that kind, then a target for a field
// use) and its system page (the slot list, quitting, and settings rows changed with Confirm).
public sealed partial class SampleController
{
	// The menu_abilities layout's `unusable` "hide" leaves out what cannot be used in the field.
	private List<AbilityInfo> PageAbilities(string member)
	{
		bool hide = Ui.ParamText("menu_abilities", "unusable", UiData(), "dim") == "hide";
		return [.. _s.MemberAbilities(member).Where(a => (a.Field || !hide) && MenuPage switch
		{
			"techniques" => a.Kind == AbilityKind.Technique,
			"skills" => a.Kind == AbilityKind.Skill,
			_ => true,
		})];
	}

	private void UpdateAbilitiesPage(ActionInput input)
	{
		List<Member> lineup = LineupMembers();
		if (_pickMember is null)
		{
			_list.Move(input, lineup.Count);
			if (input.Confirm && lineup.Count > 0)
			{
				_pickMember = lineup[_list.Cursor].Id;
				_sub.Reset();
			}
			return;
		}
		if (_pickAction is not null)
		{
			_third.Move(input, lineup.Count);
			if (input.Back)
			{
				_pickAction = null;
			}
			else if (input.Confirm && lineup.Count > 0 && OutOfReach(CommandKind.Ability, _pickMember).Contains(lineup[_third.Cursor].Id))
			{
				_audio.PlayCue("refuse");
			}
			else if (input.Confirm && lineup.Count > 0)
			{
				Report(_s.UseAbility(_pickMember, _pickAction, lineup[_third.Cursor].Id));
				_pickAction = null;
			}
			return;
		}
		if (input.Back)
		{
			_pickMember = null;
			return;
		}
		List<AbilityInfo> abilities = PageAbilities(_pickMember);
		_sub.Move(input, abilities.Count);
		if (input.Confirm && abilities.Count > 0)
		{
			_abilityResult = "";
			AbilityInfo ability = abilities[_sub.Cursor];
			if (ability.Travel is { } kind)
			{
				PickTravel(kind, _pickMember, ability.Id);
			}
			else if (ability.Field && ability.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
			{
				_pickAction = ability.Id;
				_third.Reset();
			}
			else
			{
				Report(_s.UseAbility(_pickMember, ability.Id));
			}
		}
	}

	private void DrawAbilitiesPage(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		List<Member> lineup = LineupMembers();
		UiContext data = UiArea(x, y, w, h).Set("kind", MenuPage);
		if (_pickMember is null || lineup.FirstOrDefault(m => m.Id == _pickMember) is not { } member)
		{
			Ui.Draw("menu_abilities", s, t, data
				.Set("view", "members")
				.Set("members", lineup.Select(MemberRow).ToList())
				.Set("list", _list));
			return;
		}
		List<AbilityInfo> abilities = PageAbilities(member.Id);
		string description = "";
		if (abilities.Count > 0)
		{
			description = Description(DescriptionKind.Ability, abilities[Math.Clamp(_sub.Cursor, 0, abilities.Count - 1)].Id) ?? "";
			_probeDescription = description.Length == 0 ? "-" : description;
		}
		Ui.Draw("menu_abilities", s, t, data
			.Set("result_text", _abilityResult)
			.Set("result_ok", false)
			.Set("view", "list")
			.Set("name", member.Name)
			.Set("picking", _pickAction is not null)
			.Set("abilities", abilities.Select(a => new UiContext(data)
				.Set("text", a.Name)
				.Set("label", a.Name + (a.Field ? "  *" : ""))
				.Set("usable", a.Field)
				.Set("kind", T(NameKind.AbilityKind, (int)a.Kind))
				.Set("cost", a.Cost > 0 ? T("ui.common.tp_cost", a.Cost) : "-")
				.Set("uses", a.MaxUses >= 0 ? $"{a.Uses}/{a.MaxUses}" : "-")).ToList())
			.Set("sub_list", _sub)
			.Set("description", description)
			.Set("target_name", _pickAction is null ? "" : _s.DisplayName(_pickAction))
			.Set("targets", _pickAction is null ? new List<string>() : lineup.Select(m => T("ui.status.member_hp", m.Name, m.Hp, m.MaxHp)).ToList())
			.Set("target_rows", TargetRows(data, CommandKind.Ability, _pickMember, m => T("ui.status.member_hp", m.Name, m.Hp, m.MaxHp)))
			.Set("target_list", _third));
	}

	private List<string> SystemRows() => Layout.Screen("menu_system").Rows ?? [.. UiFields.SystemDefault];

	// The menu_system layout's `settings` "picker": Confirm on a settings row opens a list of its levels (`_sysPicker` the
	// row's id, its cursor `_sysPickerList`); Confirm chooses one and, under `picker_confirm` "close", closes the menu, else
	// returns to the rows; Back closes only the picker.
	private string? _sysPicker;
	private readonly ScrollList _sysPickerList = new();

	private bool SystemPicker => Ui.ParamText("menu_system", "settings", UiData(), "step") == "picker";

	// The sample's own prompt for a setting's picker when the text tables have none: `ui.options.<id>_prompt`.
	private string PickerPrompt(string id) => T($"ui.options.{id}_prompt");

	private void UpdateSystemPicker(ActionInput input)
	{
		OptionRow row = SettingRow(_sysPicker!)!;
		_sysPickerList.Move(input, row.Levels!.Count);
		if (input.Back)
		{
			_sysPicker = null;
		}
		else if (input.Confirm)
		{
			_sysPicker = null;
			Report(row.Pick!(_sysPickerList.Cursor));
			if (Ui.ParamText("menu_system", "picker_confirm", UiData(), "rows") == "close")
			{
				Report(_s.CloseMenu());
			}
		}
	}

	// The slot list is the Save page's (`_saveMode` "save", the cursor in `_list`); the rows' cursor is `_sub`.
	private void UpdateSystemPage(ActionInput input)
	{
		if (_saveMode is not null)
		{
			List<string> slots = ManualSlots();
			if (UpdateSaveFlow(input))
			{
				return;
			}
			_list.Move(input, slots.Count);
			if (input.Back)
			{
				_saveMode = null;
			}
			else if (input.Confirm)
			{
				SaveFromList(slots[_list.Cursor]);
			}
			return;
		}
		if (_sysPicker is not null)
		{
			UpdateSystemPicker(input);
			return;
		}
		List<string> rows = SystemRows();
		_sub.Move(input, rows.Count);
		if (!input.Confirm)
		{
			return;
		}
		switch (rows[_sub.Cursor])
		{
		case "controls":
			OpenOptions();
			_controlsPage = true;
			_controlsOnly = true;
			_optSub.Reset();
			break;
		case "save":
			_saveMode = "save";
			_list.Reset();
			break;
		case "quit":
			_confirm.Ask(T("ui.pause.quit_confirm"), () => Report(_s.QuitToTitle()));
			break;
		case string setting when SystemPicker && SettingRow(setting) is { Levels: not null } picked:
			_sysPicker = setting;
			_sysPickerList.Reset();
			_sysPickerList.Cursor = Math.Max(0, picked.Current);
			break;
		case string setting when SettingRow(setting) is { Change: not null } row:
			Report(row.Change(1));
			break;
		}
	}

	private void DrawSystemPage(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		// The save_slots layout's `over_page`: the system page's own windows stay drawn under the slot list.
		if (_saveMode is not null && Ui.Param("save_slots", "over_page", UiData(), 0) == 0)
		{
			DrawSlots(t, _list, x, y, w, h, ManualSlots(), _s.Saves() ?? []);
			return;
		}
		DrawSystemRows(s, t, x, y, w, h);
		if (_saveMode is not null)
		{
			DrawSlots(t, _list, x, y, w, h, ManualSlots(), _s.Saves() ?? []);
		}
	}

	private void DrawSystemRows(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		List<string> rows = SystemRows();
		UiContext data = UiArea(x, y, w, h);
		List<UiContext> items = [.. rows.Select(id => id switch
		{
			"save" or "quit" => new UiContext(data).Set("id", id).Set("text", T("ui.saves.system." + id)).Set("label", T("ui.saves.system." + id)).Set("value", "")
				.Set("changeable", false),
			"controls" => new UiContext(data).Set("id", id).Set("text", T("ui.options.controls")).Set("label", T("ui.options.controls")).Set("value", "")
				.Set("changeable", false),
			_ => SettingItem(data, id),
		})];
		int cursor = Math.Clamp(_sub.Cursor, 0, rows.Count - 1);
		OptionRow? picking = _sysPicker is { } id ? SettingRow(id) : null;
		Ui.Draw("menu_system", s, t, data
			.Set("rows", items)
			.Set("list", _sub)
			.Set("description", picking is not null ? PickerPrompt(_sysPicker!)
				: rows[cursor] is "save" or "quit" ? "" : rows[cursor] == "controls" ? T("ui.options.controls_help") : SettingRow(rows[cursor])?.Description ?? "")
			.Set("picking", picking is not null)
			.Set("picker_setting", _sysPicker ?? "")
			.Set("picker_title", picking?.Label ?? "")
			.Set("picker_prompt", picking is not null ? PickerPrompt(_sysPicker!) : "")
			.Set("picker_rows", picking is null ? new List<UiContext>() : [.. picking.Levels!.Select((level, i) => new UiContext(data).Set("text", level).Set("current", i == picking.Current))])
			.Set("picker_list", _sysPickerList));
	}

	// A settings row as a list item: `text` the label and value together, as the Options screen shows them, or under the
	// menu_system layout's `row_value` "hide" the label alone (`value` then empty).
	private UiContext SettingItem(UiContext parent, string id)
	{
		OptionRow row = SettingRow(id)!;
		bool hide = Ui.ParamText("menu_system", "row_value", UiData(), "show") == "hide";
		return new UiContext(parent).Set("id", id).Set("text", hide ? row.Label : $"{row.Label}: {row.Value}").Set("label", row.Label).Set("value", hide ? "" : row.Value)
			.Set("changeable", row.Change is not null);
	}
}
