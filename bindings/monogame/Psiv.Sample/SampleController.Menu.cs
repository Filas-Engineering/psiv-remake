using Microsoft.Xna.Framework;
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

	private string MenuPage => _menuChild ?? OnlyChild ?? MenuTab;

	// An `immediate` entry's one usable child (listed by its `when` and `enabled`), which opens in place of the children's
	// list.
	private string? OnlyChild => _menuOnly is null && Layout.Screen("menu").Immediate.Contains(MenuTab)
		&& ShownChildren(MenuTab).Where(c => ChildEnabled(MenuTab, c.Page)).ToList() is [var only] ? only.Page : null;

	// A tab's children its list shows: those whose `when` holds.
	private List<(string Page, string? Label)> ShownChildren(string tab) =>
		[.. (Layout.Screen("menu").Children.GetValueOrDefault(tab) ?? []).Where(c => Layout.Screen("menu").ChildWhen.GetValueOrDefault($"{tab}/{c.Page}") is not { } when || Ui.Holds(when, MenuFields()))];

	private bool ChildEnabled(string tab, string page) => Layout.Screen("menu").ChildEnabled.GetValueOrDefault($"{tab}/{page}") is not { } enabled || Ui.Holds(enabled, MenuFields());

	// The menu screen's fields a child's `when` and `enabled` read.
	private UiContext MenuFields() => UiData()
		.Set("tabs", MenuEntries.Select(tab => T(MenuLabel(tab))).ToList())
		.Set("page_ids", MenuEntries.ToList())
		.Set("tab", _tab)
		.Set("navigation", MenuListMode ? "list" : "tabs")
		.Set("list_cursor", _tab)
		.Set("page_open", MenuPageShown)
		.Set("entry_label", T(MenuLabel(MenuTab)))
		.Set("entry_page", MenuTab)
		.Set("money", T("ui.common.money", _s.WorldInfo().Money))
		.Set("money_value", _s.WorldInfo().Money)
		.Set("item_count", _s.Inventory(ItemSort.Id).Count(i => !i.IsEquipment))
		.Set("macro_count", _s.Macros().Count)
		.Set("party", LineupMembers().Select(m => new UiContext().Set("name", m.Name).Set("level", m.Level).Set("hp", m.Hp).Set("max_hp", m.MaxHp)
			.Set("tp", m.Tp).Set("max_tp", m.MaxTp).Set("alive", m.Hp > 0)).ToList());

	// The page an `immediate` entry acted on last, with its tab, so it acts once each time it opens.
	private string? _immediateActed;

	private bool ImmediateDue()
	{
		string key = $"{_tab} {MenuPage}";
		if (!Layout.Screen("menu").Immediate.Contains(MenuPage) || _immediateActed == key)
		{
			return false;
		}
		_immediateActed = key;
		return true;
	}

	// The page a tab's `children` opened, until Back or another tab.
	private string? _menuChild;
	private readonly ScrollList _childList = new();

	// The selected tab's children while they are listed, before one is chosen.
	private List<(string Page, string? Label)>? MenuChildren =>
		MenuPageShown && _menuOnly is null && _menuChild is null && OnlyChild is null && Layout.Screen("menu").Children.ContainsKey(MenuTab) ? ShownChildren(MenuTab) : null;

	private void ResetMenuChild()
	{
		_menuChild = null;
		_immediateActed = null;
		_childList.Reset();
	}

	// The party talk's lines the page shows with the menu_options layout's `party_talk` "page" (PartyTalkLines), each as its
	// pages with the control tags acted on.
	private List<(TalkLine Line, List<string> Pages)> _talkLines = [];

	private bool PartyTalkOnPage => PartyTalkMode == "page";

	private readonly ScrollList _third = new();
	// The Items page's order; the bag has none of its own, so this is the sample's own choice and is not saved.
	private ItemSort _itemSort = ItemSort.Id;
	// The menu_items layout's `actions` 1: Confirm on an item opens these for it instead of using it; with its `discard` 1
	// also Discard, for an item the data lets the bag throw away.
	private string[] ItemActions(InventoryEntry? item) => item is { Discardable: true } && Ui.Param("menu_items", "discard", UiData(), 0) != 0
		? ["use", "look", "discard"] : ["use", "look"];
	// Discard's steps: `count` (how many, for a stack) and `confirm`; null while the actions are listed.
	private string? _discardStep;
	private int _discardCount;
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
		_talkSpeakers = null;
		_itemActing = null;
		_itemLooking = false;
		_itemResult = "";
		_discardStep = null;
	}

	// Where the page is, for headless expectations.
	private string Page() => Screen switch
	{
		Screen.Menu when !MenuPageShown => "entries",
		Screen.Menu when MenuChildren is not null => "children",
		Screen.Menu => MenuPage switch
		{
			"status" => _pickMember is null ? "members" : _pickAction is not null ? "target" : _statusPage == 0 ? "stats" : "abilities",
			"items" => _pickAction is not null ? "target" : _itemLooking ? "look" : _discardStep is { } step ? "discard_" + step : _itemActing is not null ? "actions" : "items",
			"equip" => _pickMember is null ? "members" : _equipSlot is null ? "slots" : "pieces",
			"order" => _orderFrom is null ? "lineup" : "swap",
			"macros" => _macroStage.ToString().ToLowerInvariant(),
			"techniques" or "skills" or "abilities" => _pickMember is null ? "members" : _pickAction is not null ? "target" : "list",
			"system" => _saveMode is null ? "system" : "slots",
			"party_talk" when _talkSpeakers is not null => "speakers",
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
		"party_talk" => _talkSpeakers is null,
		_ => true,
	};

	private List<Member> LineupMembers() => _s.Party().Where(m => m.InLineup).ToList();

	// The menu layout's `navigation` "list": the entries are a list chosen with Up, Down and Confirm, and a page shows only
	// once confirmed.
	private bool MenuListMode => Ui.ParamText("menu", "navigation", UiData(), "tabs") == "list";

	// In list mode, whether the highlighted entry's page is open; a page Extras or the System action opens is open from the
	// start.
	private bool _menuPageOpen;
	private bool _menuPageNext;

	private bool MenuPageShown => _menuPageOpen || _menuOnly is not null || !MenuListMode;

	private void ResetMenuPage()
	{
		_pickMember = null;
		_list.Reset();
		_sub.Reset();
		ResetMenuPages();
		ResetMenuChild();
	}

	private void UpdateMenuList(ActionInput input)
	{
		if (input.Back)
		{
			Report(_s.CloseMenu());
			return;
		}
		int step = input.Pressed(GameAction.Up) ? -1 : input.Pressed(GameAction.Down) ? 1 : 0;
		if (step != 0)
		{
			_tab = (_tab + MenuTabs.Length + step) % MenuTabs.Length;
			ResetMenuPage();
		}
		else if (input.Confirm)
		{
			_menuPageOpen = true;
			ResetMenuPage();
		}
	}

	private void UpdateMenu(int dtMs, ActionInput input)
	{
		if (!MenuPageShown)
		{
			UpdateMenuList(input);
			return;
		}
		if (MenuAtTop())
		{
			int step = MenuListMode ? 0 : input.Pressed(GameAction.PagePrev) ? -1 : input.Pressed(GameAction.PageNext) ? 1 : 0;
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
			if (input.Back && MenuListMode && _menuOnly is null)
			{
				_menuPageOpen = false;
				ResetMenuPage();
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
			if (input.Confirm && (children.Count == 0 || !ChildEnabled(MenuTab, children[_childList.Cursor].Page)))
			{
				_audio.PlayCue("refuse");
			}
			else if (input.Confirm)
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
		case "party_talk" when _talkSpeakers is not null: UpdateTalkSpeakers(dtMs, input); break;
		// An `immediate` entry acts as Confirm on its one row would, once each time it opens; a Confirm in the frame it
		// opens counts as that once.
		case "party_talk" or "extras" when ImmediateDue() || input.Confirm:
			ConfirmRowPage();
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
		List<InventoryEntry> items = ItemRows();
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
		else if (input.Confirm && lineup.Count > 0 && OutOfReach(CommandKind.Item, null).Contains(lineup[_sub.Cursor].Id))
		{
			_audio.PlayCue("refuse");
		}
		else if (input.Confirm && lineup.Count > 0)
		{
			ItemReport(_s.UseItem(_pickAction, lineup[_sub.Cursor].Id));
			_pickAction = null;
		}
	}

	// The lineup members the item or ability being used cannot reach (AlliesOutOfReach), greyed before the pick; Confirm on
	// one does nothing.
	private HashSet<string> OutOfReach(CommandKind kind, string? user) => _pickAction is null ? [] : [.. _s.AlliesOutOfReach(kind, _pickAction, user)];

	// The target choice as items: `text` as the plain list shows it, `name`, and `reachable`.
	private List<UiContext> TargetRows(UiContext data, CommandKind kind, string? user, Func<Member, string> text)
	{
		HashSet<string> unreachable = OutOfReach(kind, user);
		return _pickAction is null ? [] : [.. LineupMembers().Select(m => new UiContext(data).Set("text", text(m)).Set("name", m.Name).Set("reachable", !unreachable.Contains(m.Id)))];
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

	// An item's actions: Back closes them, as does the item running out; look shows its description until Confirm or Back;
	// discard asks how many of a stack (Up and Right more, Down and Left fewer), then for a confirm, and Back steps back.
	private void UpdateItemActions(ActionInput input, InventoryEntry? item)
	{
		if (_itemLooking)
		{
			_itemLooking = !input.Confirm && !input.Back;
			return;
		}
		if (_discardStep == "confirm")
		{
			if (input.Confirm)
			{
				ItemReport(_s.Discard(_itemActing!, _discardCount));
				_itemActing = null;
				_discardStep = null;
			}
			else if (input.Back)
			{
				_discardStep = item is { Count: > 1 } ? "count" : null;
			}
			return;
		}
		if (_discardStep == "count" && item is not null)
		{
			int step = input.Pressed(GameAction.Up) || input.Pressed(GameAction.Right) ? 1 : input.Pressed(GameAction.Down) || input.Pressed(GameAction.Left) ? -1 : 0;
			_discardCount = Math.Clamp(_discardCount + step, 1, item.Count);
			_discardStep = input.Confirm ? "confirm" : input.Back ? null : _discardStep;
			return;
		}
		_discardStep = null;
		string[] actions = ItemActions(item);
		_itemActionList.Move(input, actions.Length);
		if (input.Back || item is null)
		{
			_itemActing = null;
		}
		else if (input.Confirm && actions[_itemActionList.Cursor] == "look")
		{
			_itemLooking = true;
		}
		else if (input.Confirm && actions[_itemActionList.Cursor] == "discard")
		{
			_discardCount = 1;
			_discardStep = item.Count > 1 ? "count" : "confirm";
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
			_itemResult = Shown(error.Text);
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
		if (_talkSpeakers is not null && TalkHidesMenu)
		{
			DrawField(s, art);
			DrawTalkSpeakers(s, t, art, 0, 0, s.Width, s.Height, "talk_speaker");
			return;
		}
		if (!MenuPageShown)
		{
			_probeSelected = T(MenuLabel(MenuTab));
		}
		Ui.Draw("menu", s, t, UiData()
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art, "menu")))
			.Set("tabs", MenuTabs.Select(tab => T(MenuLabel(tab))).ToList())
			.Set("page_ids", MenuTabs.ToList())
			.Set("tab", _tab)
			.Set("money", T("ui.common.money", _s.WorldInfo().Money))
			.Set("page", (UiSlot)((x, y, w, h) => DrawMenuPage(s, t, art, x, y, w, h)))
			.Set("navigation", MenuListMode ? "list" : "tabs")
			.Set("list_cursor", _tab)
			.Set("page_open", MenuPageShown)
			.Set("entry_label", T(MenuLabel(MenuTab)))
			.Set("entry_page", MenuTab)
			.Set("hint", MenuHint())
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(s, s.Width - x - w, y + h, right: true)))
			.Set("money_value", _s.WorldInfo().Money)
			.Set("item_count", _s.Inventory(ItemSort.Id).Count(i => !i.IsEquipment))
			.Set("macro_count", _s.Macros().Count)
			.Set("party", LineupMembers().Select(m => new UiContext().Set("name", m.Name).Set("level", m.Level).Set("hp", m.Hp).Set("max_hp", m.MaxHp)
				.Set("tp", m.Tp).Set("max_tp", m.MaxTp).Set("portrait", art.Portrait("", m.Id)).Set("alive", m.Hp > 0)).ToList()));
	}

	// The data of a layout drawn into the rectangle another layout's slot gave it.
	private UiContext UiArea(int x, int y, int w, int h) => UiData().Set("area_x", x).Set("area_y", y).Set("area_w", w).Set("area_h", h);

	private void DrawMenuPage(IDrawSurface s, IDrawSurface t, Art art, int x, int y, int w, int h)
	{
		if (!MenuPageShown)
		{
			return;
		}
		if (MenuChildren is { } children)
		{
			(int tabX, _, int tabW, _) = Ui.SelectedTab;
			Ui.Draw("menu_children", s, t, UiArea(x, y, w, h)
				.Set("label", T(MenuLabel(MenuTab)))
				.Set("children", children.Select(c => T(c.Label ?? "ui.menu.tab." + c.Page)).ToList())
				.Set("child_rows", children.Select(c => new UiContext().Set("text", T(c.Label ?? "ui.menu.tab." + c.Page)).Set("enabled", ChildEnabled(MenuTab, c.Page))).ToList())
				.Set("list", _childList)
				.Set("tab_x", tabX)
				.Set("tab_w", tabW)
				.Set("list_w", Ui.Param("menu_children", "list_w", UiData(), 0))
				.Set("row_h", Ui.Param("menu_children", "row_h", UiData(), 0))
				.Set("rows_max", Ui.Param("menu_children", "rows", UiData(), 8))
				.Set("list_pad", Ui.Param("menu_children", "pad", UiData(), 24))
				.Set("text_size", Ui.Param("menu_children", "text_size", UiData(), 0)));
			return;
		}
		switch (MenuPage)
		{
		case "status": DrawStatus(s, t, art, x, y, w, h); break;
		case "items": DrawItemsPage(s, t, x, y, w, h); break;
		case "equip": DrawEquip(s, t, x, y, w, h); break;
		case "techniques" or "skills" or "abilities": DrawAbilitiesPage(s, t, x, y, w, h); break;
		case "system": DrawSystemPage(s, t, x, y, w, h); break;
		case "party_talk" when PartyTalkMode == "speakers":
			DrawTalkSpeakers(s, t, art, x, y, w, h, null);
			break;
		case "party_talk" when PartyTalkOnPage:
			UiContext data = UiArea(x, y, w, h);
			Ui.Draw("menu_options", s, t, data
				.Set("options", new List<string> { T("ui.menu.party_talk") })
				.Set("talking", _talkLines.Count > 0)
				.Set("talk", _talkLines.Select(l => new UiContext(data).Set("text", (l.Line.SpeakerName.Length > 0 ? l.Line.SpeakerName + ": " : "") + string.Join(" ", l.Pages))
					.Set("speaker", l.Line.SpeakerName).Set("speaker_id", l.Line.Speaker).Set("portrait", art.Portrait("", l.Line.Speaker))
					.Set("line", string.Join(" ", l.Pages)).Set("pages", l.Pages)).ToList())
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

	// The party talk's lines in the slot: speaker names in the styles' text style `talk_name` and lines in `talk` (size and
	// color; accent and text without them), sized again by the menu_options params `talk_name_size` and `talk_size`, rows
	// `talk_row_h` apart (else the line height for the size), inside `talk_pad` and at most `talk_rows` rows (0: as many
	// as fit). Each of a line's pages starts a row of its own.
	private void DrawTalkBox(IDrawSurface t, int x, int y, int w, int h)
	{
		UiContext ctx = UiData();
		(int? lineSize, Color lineColor) = TalkStyle("talk", Style.Text);
		(int? nameSize, Color nameColor) = TalkStyle("talk_name", Style.Accent);
		int size = Ui.Param("menu_options", "talk_size", ctx, lineSize ?? Style.Body);
		int nameAt = Ui.Param("menu_options", "talk_name_size", ctx, nameSize ?? size);
		int line = Ui.Param("menu_options", "talk_row_h", ctx, 0) is > 0 and int rowH ? Px(rowH)
			: size == Style.Body && nameAt == Style.Body ? Px(Style.LineHeight) : Px(Math.Max(size, nameAt) * Style.LineHeight / Style.Body);
		int pad = Ui.Param("menu_options", "talk_pad", ctx, 0);
		int most = Ui.Param("menu_options", "talk_rows", ctx, 0);
		(x, y, w, h) = (x + pad, y + pad, Math.Max(1, w - 2 * pad), h - 2 * pad);
		int ty = y, rows = 0;
		bool Fits() => ty + line <= y + h && (most <= 0 || rows < most);
		foreach ((TalkLine talk, List<string> pages) in _talkLines)
		{
			if (talk.SpeakerName.Length > 0 && Fits())
			{
				t.Text(talk.SpeakerName, x, ty, nameColor, nameAt);
				ty += line;
				rows++;
			}
			foreach (string row in pages.SelectMany(page => Style.Wrap(t, page, w, size)).TakeWhile(_ => Fits()))
			{
				t.Text(row, x, ty, lineColor, size);
				ty += line;
				rows++;
			}
		}
	}

	private (int? Size, Color Color) TalkStyle(string name, Color fallback) =>
		Layout.Styles.TextStyles.TryGetValue(name, out UiTextStyle? style)
			? (style.Size?.Int(new StylesScope(Layout.Styles)), style.Color is { } c ? Ui.ColorOf(c) : fallback) : (null, fallback);

	// Confirm on the one row of the party talk or the extras page.
	private void ConfirmRowPage()
	{
		if (MenuPage == "extras")
		{
			OpenExtras();
			return;
		}
		if (PartyTalkMode == "speakers")
		{
			IReadOnlyList<TalkLine> lines = _s.PartyTalkLines();
			if (lines.Count == 0)
			{
				Say(T("game.refuse.nobody_talks"));
				_audio.PlayCue("refuse");
				return;
			}
			StartTalkSpeakers(lines);
			return;
		}
		if (PartyTalkOnPage)
		{
			// The lines come from PartyTalkLines, which shows and marks nothing, so the menu stays open; the field's
			// action still goes through PartyTalk.
			_talkLines = [.. _s.PartyTalkLines().Select(l => (l, ShownPages(l.Text)))];
			foreach ((TalkLine talk, List<string> pages) in _talkLines)
			{
				_log($"talk: {talk.SpeakerName}: {string.Join(" ", pages)}");
			}
			if (_talkLines.Count == 0)
			{
				Say(T("game.refuse.nobody_talks"));
				_audio.PlayCue("refuse");
			}
			return;
		}
		Report(_s.CloseMenu());
		Report(_s.PartyTalk());
	}

	// A page that is one row, always selected, which Confirm acts on: the menu_options layout.
	private void MenuRow(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h, string row)
	{
		Ui.Draw("menu_options", s, t, UiArea(x, y, w, h).Set("options", new List<string> { row }));
		_probeSelected = row;
	}

	private string MenuHint()
	{
		if (!MenuPageShown)
		{
			return string.Join("   ", T("ui.menu.hint.choose", Hint(GameAction.Up), Hint(GameAction.Down)), T("ui.menu.hint.open", Hint(GameAction.Confirm)),
				T("ui.menu.hint.close", Hint(GameAction.Back)));
		}
		string[] tabs = MenuAtTop() && !MenuListMode ? [T("ui.menu.hint.tab", Hint(GameAction.PagePrev), Hint(GameAction.PageNext))] : [];
		string back = T(MenuAtTop() && _menuChild is null && (!MenuListMode || _menuOnly is not null) ? "ui.menu.hint.close" : "ui.menu.hint.back", Hint(GameAction.Back));
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

	// The Items page's rows: one per item with its count, or under the menu_items layout's `stack_rows` "single" one per
	// copy.
	private List<InventoryEntry> ItemRows()
	{
		List<InventoryEntry> items = [.. _s.Inventory(_itemSort).Where(i => !i.IsEquipment)];
		return Ui.ParamText("menu_items", "stack_rows", UiData(), "stacked") == "single" ? [.. items.SelectMany(i => Enumerable.Repeat(i, i.Count))] : items;
	}

	// An item row's fields for a layout that draws the count apart: `name`, `count` (the copies the row stands for) and
	// `count_text`, the count by the screen's `count_format` ("{0}" when left out) or, for a count of 1, its
	// `count_format_one` when given.
	private UiContext CountFields(UiContext row, string screen, string name, int count) =>
		row.Set("name", name).Set("count", count).Set("count_text", CountText(screen, count));

	private string CountText(string screen, int count) => string.Format(System.Globalization.CultureInfo.InvariantCulture,
		count == 1 && Layout.Screen(screen).Params.ContainsKey("count_format_one") ? Ui.ParamText(screen, "count_format_one", UiData(), "{0}")
			: Ui.ParamText(screen, "count_format", UiData(), "{0}"), count);

	private void DrawItemsPage(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		List<InventoryEntry> items = ItemRows();
		bool single = Ui.ParamText("menu_items", "stack_rows", UiData(), "stacked") == "single";
		string description = "";
		if (items.Count > 0)
		{
			description = DescriptionOf(items[Math.Clamp(_list.Cursor, 0, items.Count - 1)]) ?? "";
			_probeDescription = description.Length == 0 ? "-" : description;
		}
		InventoryEntry? acting = items.FirstOrDefault(i => i.Id == _itemActing);
		InventoryEntry? looked = _itemLooking ? acting : null;
		string[] actions = ItemActions(acting);
		Ui.Draw("menu_items", s, t, SetBag(UiArea(x, y, w, h))
			.Set("picking", _pickAction is not null)
			.Set("sort_name", T(NameKind.ItemSort, (int)_itemSort))
			.Set("item_count", items.DistinctBy(i => i.Id).Count())
			.Set("item_total", items.DistinctBy(i => i.Id).Sum(i => i.Count))
			.Set("items", items.Select(i => ItemRowText(i, single)).ToList())
			.Set("item_rows", items.Select(i => CountFields(new UiContext(UiArea(x, y, w, h)).Set("text", ItemRowText(i, single)), "menu_items", i.Name, single ? 1 : i.Count)).ToList())
			.Set("list", _list)
			.Set("description", description)
			.Set("target_name", _pickAction is null ? "" : _s.DisplayName(_pickAction))
			.Set("target_count", _pickAction is null ? 0 : items.FirstOrDefault(i => i.Id == _pickAction)?.Count ?? 0)
			.Set("target_count_text", _pickAction is null ? "" : CountText("menu_items", items.FirstOrDefault(i => i.Id == _pickAction)?.Count ?? 0))
			.Set("members", _pickAction is null ? new List<string>() : LineupMembers().Select(m => T("ui.menu.member_points", m.Name, m.Hp, m.MaxHp, m.Tp, m.MaxTp)).ToList())
			.Set("member_rows", TargetRows(UiArea(x, y, w, h), CommandKind.Item, null, m => T("ui.menu.member_points", m.Name, m.Hp, m.MaxHp, m.Tp, m.MaxTp)))
			.Set("member_list", _sub)
			.Set("acting", _itemActing is not null)
			.Set("actions", actions.Select(a => T("ui.items.action." + a)).ToList())
			.Set("action_ids", actions.ToList())
			.Set("discarding", _discardStep is not null)
			.Set("discard_step", _discardStep ?? "")
			.Set("discard_count", _discardCount)
			.Set("discard_max", acting?.Count ?? 0)
			.Set("discard_name", acting?.Name ?? "")
			.Set("discard_count_text", acting is null ? "" : CountText("menu_items", _discardCount))
			.Set("discard_text", _discardStep switch
			{
				"count" => T("ui.items.discard_count", acting?.Name ?? "", _discardCount, acting?.Count ?? 0),
				"confirm" => T("ui.items.discard_confirm", _s.DisplayName(_itemActing ?? ""), _discardCount, Hint(GameAction.Confirm), Hint(GameAction.Back)),
				_ => "",
			})
			.Set("action_list", _itemActionList)
			.Set("looking", looked is not null)
			.Set("look_text", looked is null ? "" : DescriptionOf(looked) is { Length: > 0 } looks ? looks : T("ui.common.no_description"))
			.Set("result_text", _itemResult)
			.Set("result_ok", _itemResultOk));
	}

	// A row of the Items page's plain list: the name with its count (the name alone for a copy of its own), and what calls a
	// vehicle or works only in battle.
	private string ItemRowText(InventoryEntry i, bool single) => (single ? i.Name : T("ui.common.item_count", i.Name, i.Count)) +
		(i.CallsVehicle.Length > 0 ? "  " + T("ui.items.calls", _s.DisplayName(i.CallsVehicle)) : i.Field ? "" : "  " + T("ui.items.battle_only"));

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
			.Set("titles", entries.Select(e => Shown(e.Title)).ToList())
			.Set("list", _list)
			.Set("title", Shown(entry?.Title ?? ""))
			.Set("body", Shown(entry?.Text ?? "")));
	}

	// The quest log as rows: headings, the objective and each job, with blank rows between the parts.
	private List<(string Text, bool Heading)> QuestRows()
	{
		QuestLog log = _s.QuestLog();
		var rows = new List<(string, bool)> { (T("ui.quests.objective"), true), (log.Objective.Length == 0 ? "-" : Shown(log.Objective), false), ("", false), (T("ui.quests.jobs"), true) };
		rows.AddRange(log.Jobs.Select(j => (T("ui.quests.job", Shown(j.Title), T(NameKind.JobState, (int)j.State)), false)));
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
			.Select(entry => Shown(entry).Trim())
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
