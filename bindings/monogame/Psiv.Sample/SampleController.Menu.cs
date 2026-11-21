using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// The field menu: tabs along the top, one page each. Status, Equip and Macros have their own files.
public sealed partial class SampleController
{
	private static readonly string[] MenuTabs = ["Status", "Items", "Equip", "Save", "Order", "Macros", "Combos", "Quests", "Codex", "Log", "Options"];

	private readonly ScrollList _third = new();
	// The Items page's order; the bag has none of its own, so this is the sample's own choice and is not saved.
	private ItemSort _itemSort = ItemSort.Id;
	// The lineup position picked first on the Order page.
	private int? _orderFrom;

	private void ResetMenuPages()
	{
		_third.Reset();
		_orderFrom = null;
		_statusPage = 0;
		_equipSlot = null;
		_equipChange = null;
		_macroStage = MacroStage.List;
	}

	// Where the page is, for headless expectations.
	private string Page() => Screen switch
	{
		Screen.Menu => MenuTabs[_tab] switch
		{
			"Status" => _pickMember is null ? "members" : _pickAction is not null ? "target" : _statusPage == 0 ? "stats" : "abilities",
			"Items" => _pickAction is null ? "items" : "target",
			"Equip" => _pickMember is null ? "members" : _equipSlot is null ? "slots" : "pieces",
			"Order" => _orderFrom is null ? "lineup" : "swap",
			"Macros" => _macroStage.ToString().ToLowerInvariant(),
			_ => MenuTabs[_tab].ToLowerInvariant(),
		},
		Screen.Options => _controlsPage ? "controls" : "options",
		_ => Screen.ToString().ToLowerInvariant(),
	};

	private bool MenuAtTop() => MenuTabs[_tab] switch
	{
		"Status" or "Equip" => _pickMember is null,
		"Items" => _pickAction is null,
		"Order" => _orderFrom is null,
		"Macros" => _macroStage == MacroStage.List,
		_ => true,
	};

	private List<Member> LineupMembers() => _s.Party().Where(m => m.InLineup).ToList();

	private void UpdateMenu(ActionInput input)
	{
		if (MenuAtTop())
		{
			int step = input.Pressed(GameAction.PagePrev) ? -1 : input.Pressed(GameAction.PageNext) ? 1 : 0;
			if (step != 0)
			{
				_tab = (_tab + MenuTabs.Length + step) % MenuTabs.Length;
				_list.Reset();
				_sub.Reset();
				ResetMenuPages();
				return;
			}
			if (input.Back)
			{
				Report(_s.CloseMenu());
				return;
			}
		}
		switch (MenuTabs[_tab])
		{
		case "Status": UpdateStatus(input); break;
		case "Items": UpdateItems(input); break;
		case "Equip": UpdateEquip(input); break;
		case "Order": UpdateOrder(input); break;
		case "Macros": UpdateMacros(input); break;
		case "Combos":
			ComboLog combos = _s.ComboLog();
			_list.Move(input, Math.Max(combos.Total, combos.Entries.Count));
			if (input.Confirm && _list.Cursor < combos.Entries.Count)
			{
				MakeComboMacro(combos.Entries[_list.Cursor]);
			}
			break;
		case "Quests":
			_list.MoveView(input, QuestRows().Count);
			break;
		case "Codex":
			_list.Move(input, _s.Encyclopedia().Count);
			break;
		case "Log":
			ScrollTextLog(input);
			break;
		case "Save":
			List<string> slots = ManualSlots();
			_list.Move(input, slots.Count);
			if (input.Confirm)
			{
				SaveTo(slots[_list.Cursor]);
			}
			break;
		case "Options":
			if (input.Confirm)
			{
				OpenOptions();
			}
			break;
		}
	}

	private void UpdateItems(ActionInput input)
	{
		if (_pickAction is null && input.Pressed(GameAction.PartyTalk))
		{
			_itemSort = (ItemSort)(((int)_itemSort + 1) % Enum.GetValues<ItemSort>().Length);
			_list.Reset();
		}
		var items = _s.Inventory(_itemSort).Where(i => !i.IsEquipment).ToList();
		if (_pickAction is null)
		{
			_list.Move(input, items.Count);
			if (input.Confirm && items.Count > 0)
			{
				InventoryEntry item = items[_list.Cursor];
				if (item.Travel is { } kind)
				{
					PickTravel(kind, null, item.Id);
				}
				else if (item.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
				{
					_pickAction = item.Id;
					_sub.Reset();
				}
				else
				{
					Report(_s.UseItem(item.Id));
				}
			}
			return;
		}
		List<Member> lineup = LineupMembers();
		_sub.Move(input, lineup.Count);
		if (input.Back)
		{
			_pickAction = null;
		}
		else if (input.Confirm && lineup.Count > 0)
		{
			Report(_s.UseItem(_pickAction, lineup[_sub.Cursor].Id));
			_pickAction = null;
		}
	}

	private void UpdateOrder(ActionInput input)
	{
		List<Member> lineup = LineupMembers();
		_list.Move(input, lineup.Count);
		if (input.Back)
		{
			_orderFrom = null;
		}
		else if (input.Confirm && lineup.Count > 0)
		{
			if (_orderFrom is not { } from)
			{
				_orderFrom = _list.Cursor;
			}
			else
			{
				if (from != _list.Cursor)
				{
					Report(_s.SwapLineup(from, _list.Cursor));
				}
				_orderFrom = null;
			}
		}
	}

	private void DrawMenu(IDrawSurface s, IDrawSurface t, Art art)
	{
		Ui.Draw("menu", s, t, UiData()
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art)))
			.Set("tabs", MenuTabs.Select(tab => T("ui.menu.tab." + tab.ToLowerInvariant())).ToList())
			.Set("tab", _tab)
			.Set("money", T("ui.common.money", _s.WorldInfo().Money))
			.Set("page", (UiSlot)((x, y, w, h) => DrawMenuPage(s, t, art, x, y, w, h)))
			.Set("hint", MenuHint())
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(s, s.Width - x - w, y + h, right: true))));
	}

	// The data of a layout drawn into the rectangle another layout's slot gave it.
	private UiContext UiArea(int x, int y, int w, int h) => UiData().Set("area_x", x).Set("area_y", y).Set("area_w", w).Set("area_h", h);

	private void DrawMenuPage(IDrawSurface s, IDrawSurface t, Art art, int x, int y, int w, int h)
	{
		switch (MenuTabs[_tab])
		{
		case "Status": DrawStatus(s, t, art, x, y, w, h); break;
		case "Items": DrawItemsPage(s, t, x, y, w, h); break;
		case "Equip": DrawEquip(s, t, x, y, w, h); break;
		case "Order":
			Ui.Draw("menu_order", s, t, UiArea(x, y, w, h)
				.Set("rows", LineupMembers().Select((m, i) => (_orderFrom == i ? "> " : "") + $"{i + 1}. {MemberRow(m)}" + string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList())
				.Set("list", _list));
			break;
		case "Macros": DrawMacros(s, t, x, y, w, h); break;
		case "Combos": DrawCombos(s, t, x, y, w, h); break;
		case "Quests": DrawQuestLog(s, t, x, y, w, h); break;
		case "Codex": DrawCodex(s, t, x, y, w, h); break;
		case "Log": DrawTextLog(s, t, x, y, w, h); break;
		case "Save": DrawSlots(t, _list, x, y, w, h, ManualSlots(), _s.Saves() ?? []); break;
		case "Options":
			Ui.Draw("menu_options", s, t, UiArea(x, y, w, h).Set("options", new List<string> { T("ui.menu.open_options") }));
			_probeSelected = T("ui.menu.open_options");
			break;
		}
	}

	private string MenuHint()
	{
		string[] tabs = MenuAtTop() ? [T("ui.menu.hint.tab", Hint(GameAction.PagePrev), Hint(GameAction.PageNext))] : [];
		string back = T(MenuAtTop() ? "ui.menu.hint.close" : "ui.menu.hint.back", Hint(GameAction.Back));
		string scroll = T("ui.menu.hint.scroll", Hint(GameAction.Up), Hint(GameAction.Down));
		string choose = T("ui.menu.hint.choose", Hint(GameAction.Up), Hint(GameAction.Down));
		string use = T("ui.menu.hint.use", Hint(GameAction.Confirm));
		string[] page = MenuTabs[_tab] switch
		{
			"Status" when _pickMember is not null && _pickAction is null =>
				[T("ui.menu.hint.stats_or_abilities", Hint(GameAction.PagePrev), Hint(GameAction.PageNext)), .. _statusPage == 0 ? [scroll] : new[] { choose, use }, back],
			"Quests" or "Log" => [.. tabs, scroll, back],
			"Order" => [.. tabs, choose, T(_orderFrom is null ? "ui.menu.hint.pick" : "ui.menu.hint.swap", Hint(GameAction.Confirm)), back],
			"Items" when _pickAction is null => [.. tabs, choose, use, T("ui.menu.hint.sort", Hint(GameAction.PartyTalk)), back],
			"Macros" when _macroStage == MacroStage.Name =>
				[T("ui.menu.hint.type", Hint(GameAction.Confirm)), T("ui.menu.hint.name_confirm", KeyText(Keys.Enter), Hint(GameAction.Menu)), T("ui.menu.hint.cancel", KeyText(Keys.Escape))],
			_ => [.. tabs, choose, use, back],
		};
		return string.Join("   ", page);
	}

	private void DrawItemsPage(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		var items = _s.Inventory(_itemSort).Where(i => !i.IsEquipment).ToList();
		string description = "";
		if (items.Count > 0)
		{
			description = DescriptionOf(items[Math.Clamp(_list.Cursor, 0, items.Count - 1)]) ?? "";
			_probeDescription = description.Length == 0 ? "-" : description;
		}
		Ui.Draw("menu_items", s, t, UiArea(x, y, w, h)
			.Set("picking", _pickAction is not null)
			.Set("sort_name", T(NameKind.ItemSort, (int)_itemSort))
			.Set("item_count", items.Count)
			.Set("item_total", items.Sum(i => i.Count))
			.Set("items", items.Select(i => T("ui.common.item_count", i.Name, i.Count) +
				(i.CallsVehicle.Length > 0 ? "  " + T("ui.items.calls", _s.DisplayName(i.CallsVehicle)) : i.Field ? "" : "  " + T("ui.items.battle_only"))).ToList())
			.Set("list", _list)
			.Set("description", description)
			.Set("target_name", _pickAction is null ? "" : _s.DisplayName(_pickAction))
			.Set("members", _pickAction is null ? new List<string>() : LineupMembers().Select(m => T("ui.menu.member_points", m.Name, m.Hp, m.MaxHp, m.Tp, m.MaxTp)).ToList())
			.Set("member_list", _sub));
	}

	// "Make macro" (UI-011): a macro named after the combination, its first 16 bytes, filled by the session.
	private void MakeComboMacro(ComboEntry entry)
	{
		string name = entry.ResultName;
		while (System.Text.Encoding.UTF8.GetByteCount(name) > 16)
		{
			name = name[..^1];
		}
		PsivResult result = _s.MacroFromCombo(entry.Recipe, name);
		Report(result);
		if (result.Ok)
		{
			Say(T("ui.combos.made_macro", name.Trim()));
		}
	}

	private void DrawCombos(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		ComboLog log = _s.ComboLog();
		int total = Math.Max(log.Total, log.Entries.Count);
		var rows = log.Entries.Select(e => T("ui.combos.row", e.ResultName, string.Join(" + ", e.ParticipantNames)))
			.Concat(Enumerable.Repeat(T("ui.combos.unknown"), total - log.Entries.Count)).ToList();
		int cursor = Math.Clamp(_list.Cursor, 0, Math.Max(0, rows.Count - 1));
		ComboEntry? entry = cursor < log.Entries.Count ? log.Entries[cursor] : null;
		UiContext data = UiArea(x, y, w, h);
		Ui.Draw("menu_combos", s, t, data
			.Set("discovered", log.Entries.Count)
			.Set("log_total", log.Total)
			.Set("total", total)
			.Set("rows", rows.Select((row, i) => new UiContext(data).Set("text", row).Set("enabled", i < log.Entries.Count)).ToList())
			.Set("list", _list)
			.Set("has_entry", entry is not null)
			.Set("members_text", entry is null ? "" : T("ui.combos.members", ListText(entry.ParticipantNames)))
			.Set("abilities_text", entry is null ? "" : T("ui.combos.abilities", ListText(entry.AbilityNames)))
			.Set("found_time", entry is null ? "" : Clock(entry.PlayTimeMs))
			.Set("make_key", Hint(GameAction.Confirm)));
	}

	private void DrawCodex(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		IReadOnlyList<CodexEntry> entries = _s.Encyclopedia();
		CodexEntry? entry = entries.Count > 0 ? entries[Math.Clamp(_list.Cursor, 0, entries.Count - 1)] : null;
		Ui.Draw("menu_codex", s, t, UiArea(x, y, w, h)
			.Set("titles", entries.Select(e => e.Title).ToList())
			.Set("list", _list)
			.Set("title", entry?.Title ?? "")
			.Set("body", entry?.Text ?? ""));
	}

	// The quest log as rows: headings, the objective and each job, with blank rows between the parts.
	private List<(string Text, bool Heading)> QuestRows()
	{
		QuestLog log = _s.QuestLog();
		var rows = new List<(string, bool)> { (T("ui.quests.objective"), true), (log.Objective.Length == 0 ? "-" : log.Objective, false), ("", false), (T("ui.quests.jobs"), true) };
		rows.AddRange(log.Jobs.Select(j => (T("ui.quests.job", j.Title, T(NameKind.JobState, (int)j.State)), false)));
		return rows;
	}

	private void DrawQuestLog(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		UiContext data = UiArea(x, y, w, h);
		Ui.Draw("menu_quests", s, t, data
			.Set("rows", QuestRows().Select(r => new UiContext(data).Set("text", r.Text).Set("heading", r.Heading)).ToList())
			.Set("list", _list));
		_probeTop = _list.Top;
	}

	// The text log opens at its newest lines; Up goes back through older ones.
	private void ScrollTextLog(ActionInput input)
	{
		if (input.Pressed(GameAction.Up))
		{
			_list.Cursor++;
		}
		if (input.Pressed(GameAction.Down))
		{
			_list.Cursor = Math.Max(0, _list.Cursor - 1);
		}
	}

	private void DrawTextLog(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		UiContext data = UiArea(x, y, w, h);
		int width = Ui.Param("menu_log", "wrap_width", data, w - 20);
		int rows = Math.Max(1, Ui.Param("menu_log", "rows", data, h / Px(22)));
		List<string> lines = _s.TextLog().SelectMany(page => Style.Wrap(t, page, width, Style.Small)).ToList();
		int back = Math.Clamp(_list.Cursor, 0, Math.Max(0, lines.Count - rows));
		_list.Cursor = back;
		int first = Math.Max(0, lines.Count - rows - back);
		Ui.Draw("menu_log", s, t, data
			.Set("lines", lines.Skip(first).Take(rows).ToList())
			.Set("first", first)
			.Set("rows", rows)
			.Set("total", lines.Count));
		_probeTop = first;
	}
}
