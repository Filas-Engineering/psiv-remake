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

	// The slot list is the Save page's (`_saveMode` "save", the cursor in `_list`); the rows' cursor is `_sub`.
	private void UpdateSystemPage(ActionInput input)
	{
		if (_saveMode is not null)
		{
			List<string> slots = ManualSlots();
			_list.Move(input, slots.Count);
			if (input.Back)
			{
				_saveMode = null;
			}
			else if (input.Confirm)
			{
				SaveTo(slots[_list.Cursor]);
			}
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
		case "save":
			_saveMode = "save";
			_list.Reset();
			break;
		case "quit":
			_confirm.Ask(T("ui.pause.quit_confirm"), () => Report(_s.QuitToTitle()));
			break;
		case string setting:
			Report(SettingRow(setting).Change!(1));
			break;
		}
	}

	private void DrawSystemPage(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		if (_saveMode is not null)
		{
			DrawSlots(t, _list, x, y, w, h, ManualSlots(), _s.Saves() ?? []);
			return;
		}
		List<string> rows = SystemRows();
		UiContext data = UiArea(x, y, w, h);
		List<UiContext> items = [.. rows.Select(id => id switch
		{
			"save" or "quit" => new UiContext(data).Set("id", id).Set("text", T("ui.saves.system." + id)).Set("label", T("ui.saves.system." + id)).Set("value", "")
				.Set("changeable", false),
			_ => SettingItem(data, id),
		})];
		int cursor = Math.Clamp(_sub.Cursor, 0, rows.Count - 1);
		Ui.Draw("menu_system", s, t, data
			.Set("rows", items)
			.Set("list", _sub)
			.Set("description", rows[cursor] is "save" or "quit" ? "" : SettingRow(rows[cursor]).Description));
	}

	// A settings row as a list item: `text` the label and value together, as the Options screen shows them.
	private UiContext SettingItem(UiContext parent, string id)
	{
		OptionRow row = SettingRow(id);
		return new UiContext(parent).Set("id", id).Set("text", $"{row.Label}: {row.Value}").Set("label", row.Label).Set("value", row.Value).Set("changeable", row.Change is not null);
	}
}
