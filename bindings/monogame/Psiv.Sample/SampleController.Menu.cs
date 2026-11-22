using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// The field menu: tabs along the top, one page each, in the menu layout's `entries` order; an entry with `children` lists
// them in place of its page, and the one chosen opens there. Status, Equip and Macros have their own files; the technique,
// skill and system pages are in SampleController.MenuPages.
public sealed partial class SampleController
{
	// The layout's entries; while Extras has opened a page they leave out, that page alone, until the menu closes.
	private string[] MenuTabs => _menuOnly is { } only ? [only] : MenuEntries;

	private string[] MenuEntries => [.. Layout.Screen("menu").Entries?.Select(e => e.Page) ?? UiFields.MenuDefault];

	private string? _menuOnly;

	private string MenuLabel(string page) => Layout.Screen("menu").Entries?.FirstOrDefault(e => e.Page == page).Label ?? "ui.menu.tab." + page;

	private string MenuTab => MenuTabs[Math.Clamp(_tab, 0, MenuTabs.Length - 1)];

	private string MenuPage => _menuChild ?? MenuTab;

	// The page a tab's `children` opened, until Back or another tab.
	private string? _menuChild;
	private readonly ScrollList _childList = new();

	// The selected tab's children while they are listed, before one is chosen.
	private List<(string Page, string? Label)>? MenuChildren =>
		_menuOnly is null && _menuChild is null && Layout.Screen("menu").Children.GetValueOrDefault(MenuTab) is { } children ? children : null;

	private void ResetMenuChild()
	{
		_menuChild = null;
		_childList.Reset();
	}

	// The party talk's lines the page shows with the menu_options layout's `party_talk` "page".
	private List<(string Speaker, string Line, string Text)> _talkLines = [];
	// The notices of the party talk now starting go to _talkLines.
	private bool _talkToPage;

	private bool PartyTalkOnPage => Ui.ParamText("menu_options", "party_talk", UiData(), "close") == "page";

	private readonly ScrollList _third = new();
	// The Items page's order; the bag has none of its own, so this is the sample's own choice and is not saved.
	private ItemSort _itemSort = ItemSort.Id;
	// The menu_items layout's `actions` 1: Confirm on an item opens these for it instead of using it. The session has no
	// call that throws an item away, so there is no discard.
	private static readonly string[] ItemActions = ["use", "look"];
	// The item whose actions are open, and whether its description shows (look).
	private string? _itemActing;
	private bool _itemLooking;
	private readonly ScrollList _itemActionList = new();
	// The last use's outcome (the session's notice or refusal) and whether it went through, for a layout that shows
	// `result_text`.
	private string _itemResult = "";
	private bool _itemResultOk;

	private bool ItemActionsOn => Ui.Param("menu_items", "actions", UiData(), 0) != 0;

	// A layout that shows `result_text` takes the uses' outcomes there instead of the notices.
	private bool ItemResultShown => Layout.Screen("menu_items").Reads("result_text");

	// The text log's lines as the last frame showed them, joined by " | ".
	private string _probeLogLines = "-";
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
		_saveMode = null;
		_systemCursor = 0;
		_talkLines = [];
		_itemActing = null;
		_itemLooking = false;
		_itemResult = "";
	}

	// Where the page is, for headless expectations.
	private string Page() => Screen switch
	{
		Screen.Menu when MenuChildren is not null => "children",
		Screen.Menu => MenuPage switch
		{
			"status" => _pickMember is null ? "members" : _pickAction is not null ? "target" : _statusPage == 0 ? "stats" : "abilities",
			"items" => _pickAction is not null ? "target" : _itemLooking ? "look" : _itemActing is not null ? "actions" : "items",
			"equip" => _pickMember is null ? "members" : _equipSlot is null ? "slots" : "pieces",
			"order" => _orderFrom is null ? "lineup" : "swap",
			"macros" => _macroStage.ToString().ToLowerInvariant(),
			"techniques" or "skills" or "abilities" => _pickMember is null ? "members" : _pickAction is not null ? "target" : "list",
			"system" => _saveMode is null ? "system" : "slots",
			_ => MenuPage,
		},
		Screen.Options => _controlsPage ? "controls" : "options",
		Screen.Extras => _extrasNotice ? "notice" : "extras",
		_ => Screen.ToString().ToLowerInvariant(),
	};

	private bool MenuAtTop() => MenuChildren is not null || MenuPage switch
	{
		"status" or "techniques" or "skills" or "abilities" => _pickMember is null,
		"equip" => _pickMember is null || (!EquipMembersFirst && _equipSlot is null),
		"items" => _pickAction is null && _itemActing is null,
		"order" => _orderFrom is null,
		"macros" => _macroStage == MacroStage.List,
		"save" => SaveAtTop,
		"system" => _saveMode is null,
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
				_pickMember = null;
				_list.Reset();
				_sub.Reset();
				ResetMenuPages();
				ResetMenuChild();
				return;
			}
			if (input.Back && _menuChild is not null)
			{
				_menuChild = null;
				_list.Reset();
				_sub.Reset();
				ResetMenuPages();
				return;
			}
			if (input.Back || _menuOnly == "system" && input.Pressed(GameAction.System) && SystemActionOn)
			{
				Report(_s.CloseMenu());
				return;
			}
		}
		if (MenuChildren is { } children)
		{
			_childList.Move(input, children.Count);
			if (input.Confirm)
			{
				_menuChild = children[_childList.Cursor].Page;
				_pickMember = null;
				_list.Reset();
				_sub.Reset();
				ResetMenuPages();
			}
			return;
		}
		switch (MenuPage)
		{
		case "status": UpdateStatus(input); break;
		case "items": UpdateItems(input); break;
		case "equip": UpdateEquip(input); break;
		case "order": UpdateOrder(input); break;
		case "macros": UpdateMacros(input); break;
		case "techniques" or "skills" or "abilities": UpdateAbilitiesPage(input); break;
		case "system": UpdateSystemPage(input); break;
		case "party_talk" when input.Confirm && PartyTalkOnPage:
			// The session talks only in the field, so the menu closes for the talk and opens again where it was.
			Report(_s.CloseMenu());
			_talkLines = [];
			_talkToPage = true;
			Report(_s.PartyTalk());
			Report(_s.OpenMenu());
			break;
		case "party_talk" when input.Confirm:
			Report(_s.CloseMenu());
			Report(_s.PartyTalk());
			break;
		case "extras" when input.Confirm:
			OpenExtras();
			break;
		case "combos":
			ComboLog combos = _s.ComboLog();
			_list.Move(input, Math.Max(combos.Total, combos.Entries.Count));
			if (input.Confirm && _list.Cursor < combos.Entries.Count)
			{
				MakeComboMacro(combos.Entries[_list.Cursor]);
			}
			break;
		case "quests":
			_list.MoveView(input, QuestRows().Count);
			break;
		case "codex":
			_list.Move(input, _s.Encyclopedia().Count);
			break;
		case "log":
			ScrollTextLog(input);
			break;
		case "save":
			UpdateSavePage(input);
			break;
		case "options":
			if (input.Confirm)
			{
				OpenOptions();
			}
			break;
		}
	}

	private void UpdateItems(ActionInput input)
	{
		if (_pickAction is null && _itemActing is null && input.Pressed(GameAction.PartyTalk))
		{
			_itemSort = (ItemSort)(((int)_itemSort + 1) % Enum.GetValues<ItemSort>().Length);
			_list.Reset();
		}
		var items = _s.Inventory(_itemSort).Where(i => !i.IsEquipment).ToList();
		if (_itemActing is not null)
		{
			UpdateItemActions(input, items.FirstOrDefault(i => i.Id == _itemActing));
			return;
		}
		if (_pickAction is null)
		{
			_list.Move(input, items.Count);
			if (input.Confirm && items.Count > 0 && ItemActionsOn)
			{
				_itemActing = items[_list.Cursor].Id;
				_itemActionList.Reset();
			}
			else if (input.Confirm && items.Count > 0)
			{
				UseItem(items[_list.Cursor]);
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
			ItemReport(_s.UseItem(_pickAction, lineup[_sub.Cursor].Id));
			_pickAction = null;
		}
	}

	private void UseItem(InventoryEntry item)
	{
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
			ItemReport(_s.UseItem(item.Id));
		}
	}

	// An item's actions: Back closes them, as does the item running out; look shows its description until Confirm or Back.
	private void UpdateItemActions(ActionInput input, InventoryEntry? item)
	{
		if (_itemLooking)
		{
			_itemLooking = !input.Confirm && !input.Back;
			return;
		}
		_itemActionList.Move(input, ItemActions.Length);
		if (input.Back || item is null)
		{
			_itemActing = null;
		}
		else if (input.Confirm && ItemActions[_itemActionList.Cursor] == "look")
		{
			_itemLooking = true;
		}
		else if (input.Confirm)
		{
			_itemActing = null;
			UseItem(item);
		}
	}

	// A refusal to the layout's `result_text` when it shows one, else to the notices; a use's own notice follows the same
	// way (TakeNotices).
	private void ItemReport(PsivResult used)
	{
		_itemResult = "";
		_itemResultOk = used.Ok;
		if (used.Error is { } error && ItemResultShown)
		{
			_itemResult = error.Text;
			_log("notice: " + error.Text);
			_refused = true;
			return;
		}
		Report(used);
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
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art, "menu")))
			.Set("tabs", MenuTabs.Select(tab => T(MenuLabel(tab))).ToList())
			.Set("page_ids", MenuTabs.ToList())
			.Set("tab", _tab)
			.Set("money", T("ui.common.money", _s.WorldInfo().Money))
			.Set("page", (UiSlot)((x, y, w, h) => DrawMenuPage(s, t, art, x, y, w, h)))
			.Set("hint", MenuHint())
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(s, s.Width - x - w, y + h, right: true)))
			.Set("money_value", _s.WorldInfo().Money)
			.Set("party", LineupMembers().Select(m => new UiContext().Set("name", m.Name).Set("level", m.Level).Set("hp", m.Hp).Set("max_hp", m.MaxHp)
				.Set("tp", m.Tp).Set("max_tp", m.MaxTp).Set("portrait", art.Portrait("", m.Id)).Set("alive", m.Hp > 0)).ToList()));
	}

	// The data of a layout drawn into the rectangle another layout's slot gave it.
	private UiContext UiArea(int x, int y, int w, int h) => UiData().Set("area_x", x).Set("area_y", y).Set("area_w", w).Set("area_h", h);

	private void DrawMenuPage(IDrawSurface s, IDrawSurface t, Art art, int x, int y, int w, int h)
	{
		if (MenuChildren is { } children)
		{
			(int tabX, _, int tabW, _) = Ui.SelectedTab;
			Ui.Draw("menu_children", s, t, UiArea(x, y, w, h)
				.Set("label", T(MenuLabel(MenuTab)))
				.Set("children", children.Select(c => T(c.Label ?? "ui.menu.tab." + c.Page)).ToList())
				.Set("list", _childList)
				.Set("tab_x", tabX)
				.Set("tab_w", tabW));
			return;
		}
		switch (MenuPage)
		{
		case "status": DrawStatus(s, t, art, x, y, w, h); break;
		case "items": DrawItemsPage(s, t, x, y, w, h); break;
		case "equip": DrawEquip(s, t, x, y, w, h); break;
		case "techniques" or "skills" or "abilities": DrawAbilitiesPage(s, t, x, y, w, h); break;
		case "system": DrawSystemPage(s, t, x, y, w, h); break;
		case "party_talk" when PartyTalkOnPage:
			UiContext data = UiArea(x, y, w, h);
			Ui.Draw("menu_options", s, t, data
				.Set("options", new List<string> { T("ui.menu.party_talk") })
				.Set("talking", _talkLines.Count > 0)
				.Set("talk", _talkLines.Select(l => new UiContext(data).Set("text", l.Text).Set("speaker", l.Speaker).Set("line", l.Line)).ToList())
				.Set("talk_box", (UiSlot)((bx, by, bw, bh) => DrawTalkBox(t, bx, by, bw, bh))));
			_probeSelected = T("ui.menu.party_talk");
			break;
		case "party_talk":
			MenuRow(s, t, x, y, w, h, T("ui.menu.party_talk"));
			break;
		case "extras":
			MenuRow(s, t, x, y, w, h, T("ui.menu.open_extras"));
			break;
		case "order":
			Ui.Draw("menu_order", s, t, UiArea(x, y, w, h)
				.Set("rows", LineupMembers().Select((m, i) => (_orderFrom == i ? "> " : "") + $"{i + 1}. {MemberRow(m)}" + string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList())
				.Set("lineup", LineupMembers().Select((m, i) => new UiContext().Set("text", MemberRow(m)).Set("name", m.Name).Set("level", m.Level).Set("hp", m.Hp)
					.Set("max_hp", m.MaxHp).Set("tp", m.Tp).Set("max_tp", m.MaxTp).Set("number", i + 1).Set("picked", _orderFrom == i)
					.Set("statuses", string.Join(" ", m.Statuses.Select(st => st.Name)))).ToList())
				.Set("list", _list));
			break;
		case "macros": DrawMacros(s, t, x, y, w, h); break;
		case "combos": DrawCombos(s, t, x, y, w, h); break;
		case "quests": DrawQuestLog(s, t, x, y, w, h); break;
		case "codex": DrawCodex(s, t, x, y, w, h); break;
		case "log": DrawTextLog(s, t, x, y, w, h); break;
		case "save": DrawSavePage(t, x, y, w, h); break;
		case "options":
			MenuRow(s, t, x, y, w, h, T("ui.menu.open_options"));
			break;
		}
	}

	// The party talk's lines in the slot's rectangle: each speaker's name, then the line wrapped under it.
	private void DrawTalkBox(IDrawSurface t, int x, int y, int w, int h)
	{
		int line = Px(Style.LineHeight);
		int ty = y;
		foreach ((string speaker, string said, _) in _talkLines)
		{
			if (speaker.Length > 0 && ty + line <= y + h)
			{
				t.Text(speaker, x, ty, Style.Accent);
				ty += line;
			}
			foreach (string row in Style.Wrap(t, said, w).TakeWhile(_ => ty + line <= y + h))
			{
				t.Text(row, x, ty, Style.Text);
				ty += line;
			}
		}
	}

	// A party talk line as the page shows it: the speaker is the lineup member whose name starts the notice.
	private void TakeTalkLine(Notice n)
	{
		string? speaker = LineupMembers().Select(m => m.Name).FirstOrDefault(name => n.Text.StartsWith(name + ": ", StringComparison.Ordinal));
		_talkLines.Add(speaker is null ? ("", n.Text, n.Text) : (speaker, n.Text[(speaker.Length + 2)..], n.Text));
	}

	// A page that is one row, always selected, which Confirm acts on: the menu_options layout.
	private void MenuRow(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h, string row)
	{
		Ui.Draw("menu_options", s, t, UiArea(x, y, w, h).Set("options", new List<string> { row }));
		_probeSelected = row;
	}

	private string MenuHint()
	{
		string[] tabs = MenuAtTop() ? [T("ui.menu.hint.tab", Hint(GameAction.PagePrev), Hint(GameAction.PageNext))] : [];
		string back = T(MenuAtTop() && _menuChild is null ? "ui.menu.hint.close" : "ui.menu.hint.back", Hint(GameAction.Back));
		string scroll = T("ui.menu.hint.scroll", Hint(GameAction.Up), Hint(GameAction.Down));
		string choose = T("ui.menu.hint.choose", Hint(GameAction.Up), Hint(GameAction.Down));
		string use = T("ui.menu.hint.use", Hint(GameAction.Confirm));
		string[] page = MenuChildren is not null ? [.. tabs, choose, use, back] : MenuPage switch
		{
			"status" when _pickMember is not null && _pickAction is null =>
				[T("ui.menu.hint.stats_or_abilities", Hint(GameAction.PagePrev), Hint(GameAction.PageNext)), .. _statusPage == 0 ? [scroll] : new[] { choose, use }, back],
			"quests" or "log" => [.. tabs, scroll, back],
			"order" => [.. tabs, choose, T(_orderFrom is null ? "ui.menu.hint.pick" : "ui.menu.hint.swap", Hint(GameAction.Confirm)), back],
			"items" when _pickAction is null && _itemActing is null => [.. tabs, choose, use, T("ui.menu.hint.sort", Hint(GameAction.PartyTalk)), back],
			"macros" when _macroStage == MacroStage.Name =>
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
		InventoryEntry? looked = _itemLooking ? items.FirstOrDefault(i => i.Id == _itemActing) : null;
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
			.Set("member_list", _sub)
			.Set("acting", _itemActing is not null)
			.Set("actions", ItemActions.Select(a => T("ui.items.action." + a)).ToList())
			.Set("action_ids", ItemActions.ToList())
			.Set("action_list", _itemActionList)
			.Set("looking", looked is not null)
			.Set("look_text", looked is null ? "" : DescriptionOf(looked) is { Length: > 0 } looks ? looks : T("ui.common.no_description"))
			.Set("result_text", _itemResult)
			.Set("result_ok", _itemResultOk));
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
		// The log keeps each line as written; its control tags are acted on in the dialogue, not shown here.
		List<string> lines = _s.TextLog()
			.Select(entry => string.Join(" ", Parse(entry, null).Select(page => page.Text.Trim()).Where(text => text.Length > 0)))
			.SelectMany(entry => Style.Wrap(t, entry, width, ToastTextSize())).ToList();
		int back = Math.Clamp(_list.Cursor, 0, Math.Max(0, lines.Count - rows));
		_list.Cursor = back;
		int first = Math.Max(0, lines.Count - rows - back);
		List<string> shown = lines.Skip(first).Take(rows).ToList();
		_probeLogLines = shown.Count == 0 ? "none" : string.Join(" | ", shown);
		Ui.Draw("menu_log", s, t, data
			.Set("lines", shown)
			.Set("first", first)
			.Set("rows", rows)
			.Set("total", lines.Count));
		_probeTop = first;
	}
}
