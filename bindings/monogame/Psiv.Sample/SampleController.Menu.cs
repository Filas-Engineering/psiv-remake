using System.Globalization;

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

	private void DrawMenu(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		Style.Window(s, 24, 56, s.Width - 48, s.Height - 72, T("ui.menu.title"));
		string money = T("ui.common.money", _s.WorldInfo().Money);
		int moneyX = s.Width - 64 - s.Measure(money);
		Widgets.Tabs(s, 48, 64, moneyX - 72, [.. MenuTabs.Select(tab => T("ui.menu.tab." + tab.ToLowerInvariant()))], _tab, Px);
		s.Text(money, moneyX, 64 + Px(5), Style.Text);
		int x = 56;
		int y = 64 + Px(60);
		int w = s.Width - 112;
		int h = s.Height - 66 - Px(10) - y;
		switch (MenuTabs[_tab])
		{
		case "Status": DrawStatus(s, art, x, y, w, h); break;
		case "Items": DrawItemsPage(s, x, y, w, h); break;
		case "Equip": DrawEquip(s, x, y, w, h); break;
		case "Order":
			List<Member> lineup = LineupMembers();
			DrawItems(s, _list, x, y, w, h, lineup.Select((m, i) => (_orderFrom == i ? "> " : "") +
				$"{i + 1}. {MemberRow(m)}" + string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList());
			break;
		case "Macros": DrawMacros(s, x, y, w, h); break;
		case "Combos": DrawCombos(s, x, y, w, h); break;
		case "Quests": DrawQuestLog(s, x, y, w, h); break;
		case "Codex": DrawCodex(s, x, y, w, h); break;
		case "Log": DrawTextLog(s, x, y, w, h); break;
		case "Save": DrawSlots(s, _list, x, y, w, h, ManualSlots(), _s.Saves() ?? []); break;
		case "Options":
			s.Text(T("ui.menu.options_about"), x, y, Style.Text);
			Style.ListRow(s, T("ui.menu.open_options"), x + 20, y + Px(44), Px(260), true, height: Px(28));
			_probeSelected = T("ui.menu.open_options");
			break;
		}
		Widgets.Hints(s, x, s.Height - 66, w, MenuHint());
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

	private void DrawItemsPage(IDrawSurface s, int x, int y, int w, int h)
	{
		var items = _s.Inventory(_itemSort).Where(i => !i.IsEquipment).ToList();
		int listW = _pickAction is null ? w : w / 2 - 20;
		s.Text(T("ui.items.sorted_by", T(NameKind.ItemSort, (int)_itemSort)), x, y - Px(26), Style.Dim, Style.Small);
		string totals = T("ui.items.totals", items.Count, items.Sum(i => i.Count));
		s.Text(totals, x + listW - s.Measure(totals, Style.Small) - 20, y - Px(26), Style.Dim, Style.Small);
		if (items.Count == 0)
		{
			s.Text(T("ui.items.none"), x + 14, y, Style.Dim);
		}
		int descH = Px(56);
		DrawItems(s, _list, x, y, listW, h - descH, items.Select(i => T("ui.common.item_count", i.Name, i.Count) + (i.Field ? "" : "  " + T("ui.items.battle_only"))).ToList(), _pickAction is null);
		if (items.Count > 0)
		{
			s.Fill(x, y + h - descH, listW - 14, 1, Style.PanelEdge * 0.4f);
			DrawDescription(s, DescriptionOf(items[Math.Clamp(_list.Cursor, 0, items.Count - 1)]), x + 14, y + h - descH + Px(8), listW - 40);
		}
		if (_pickAction is not null)
		{
			s.Text(T("ui.menu.use_on_whom", _s.DisplayName(_pickAction)), x + w / 2, y, Style.Accent);
			DrawItems(s, _sub, x + w / 2, y + Px(40), w / 2, h - Px(40), LineupMembers().Select(m => T("ui.menu.member_points", m.Name, m.Hp, m.MaxHp, m.Tp, m.MaxTp)).ToList());
		}
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

	private void DrawCombos(IDrawSurface s, int x, int y, int w, int h)
	{
		ComboLog log = _s.ComboLog();
		int total = Math.Max(log.Total, log.Entries.Count);
		s.Text(T("ui.combos.discovered", log.Entries.Count, log.Total), x, y, Style.Accent);
		int detailH = Px(96);
		var rows = log.Entries.Select(e => T("ui.combos.row", e.ResultName, string.Join(" + ", e.ParticipantNames)))
			.Concat(Enumerable.Repeat(T("ui.combos.unknown"), total - log.Entries.Count)).ToList();
		DrawItems(s, _list, x, y + Px(36), w, h - Px(36) - detailH, rows, enabled: rows.Select((_, i) => i < log.Entries.Count).ToList());
		int dy = y + h - detailH + Px(8);
		s.Fill(x, dy - Px(8), w, 1, Style.PanelEdge * 0.4f);
		if (_list.Cursor < log.Entries.Count)
		{
			ComboEntry e = log.Entries[_list.Cursor];
			s.Text(Style.Fit(s, T("ui.combos.members", ListText(e.ParticipantNames)), w, Style.Small), x, dy, Style.Text, Style.Small);
			s.Text(Style.Fit(s, T("ui.combos.abilities", ListText(e.AbilityNames)), w, Style.Small), x, dy + Px(26), Style.Text, Style.Small);
			s.Text(T("ui.combos.first_found", Clock(e.PlayTimeMs), Hint(GameAction.Confirm)), x, dy + Px(52), Style.Dim, Style.Small);
		}
		else if (total > 0)
		{
			s.Text(T("ui.combos.not_discovered"), x, dy, Style.Dim, Style.Small);
		}
	}

	private void DrawCodex(IDrawSurface s, int x, int y, int w, int h)
	{
		IReadOnlyList<CodexEntry> entries = _s.Encyclopedia();
		int listW = w / 3;
		if (entries.Count == 0)
		{
			s.Text(T("ui.codex.none"), x + 14, y, Style.Dim);
			return;
		}
		DrawItems(s, _list, x, y, listW, h, entries.Select(e => e.Title).ToList());
		CodexEntry entry = entries[Math.Clamp(_list.Cursor, 0, entries.Count - 1)];
		int tx = x + listW + 24;
		s.Text(entry.Title, tx, y, Style.Accent);
		int ty = y + Px(Style.LineHeight) + Px(6);
		foreach (string l in Style.Wrap(s, entry.Text, w - listW - 24, Style.Small))
		{
			if (ty + Px(22) > y + h)
			{
				break;
			}
			s.Text(l, tx, ty, Style.Text, Style.Small);
			ty += Px(22);
		}
	}

	// The quest log as rows: headings, the objective and each job, with blank rows between the parts.
	private List<(string Text, bool Heading)> QuestRows()
	{
		QuestLog log = _s.QuestLog();
		var rows = new List<(string, bool)> { (T("ui.quests.objective"), true), (log.Objective.Length == 0 ? "-" : log.Objective, false), ("", false), (T("ui.quests.jobs"), true) };
		rows.AddRange(log.Jobs.Select(j => (T("ui.quests.job", j.Title, T(NameKind.JobState, (int)j.State)), false)));
		return rows;
	}

	private void DrawQuestLog(IDrawSurface s, int x, int y, int w, int h)
	{
		List<(string Text, bool Heading)> rows = QuestRows();
		_list.DrawView(s, x, y, w, h, Px(30), rows.Count, (i, ry, _) =>
		{
			if (rows[i].Text.Length > 0)
			{
				s.Text(Style.Fit(s, rows[i].Text, w - 40), rows[i].Heading ? x : x + 20, ry, rows[i].Heading ? Style.Accent : Style.Text);
			}
		});
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

	private void DrawTextLog(IDrawSurface s, int x, int y, int w, int height)
	{
		List<string> pages = _s.TextLog().SelectMany(page => Style.Wrap(s, page, w - 20, Style.Small)).ToList();
		int lineH = Px(22);
		int lines = Math.Max(1, height / lineH);
		int back = Math.Clamp(_list.Cursor, 0, Math.Max(0, pages.Count - lines));
		_list.Cursor = back;
		int first = Math.Max(0, pages.Count - lines - back);
		var visible = pages.Skip(first).Take(lines).ToList();
		for (int i = 0; i < visible.Count; i++)
		{
			s.Text(visible[i], x, y + i * lineH, Style.Text, Style.Small);
		}
		if (pages.Count > lines)
		{
			Widgets.ScrollBar(s, x + w - 6, y, lines * lineH - 6, first, lines, pages.Count);
		}
		_probeTop = first;
	}
}
