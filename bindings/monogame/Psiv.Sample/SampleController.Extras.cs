using Psiv.Session;

namespace Psiv.Sample;

// The Extras menu (its own screen, `ui/extras.json`): opened by the Extras action from the title, the field and a battle's
// command menus, or by the field menu's `extras` entry. Its entries come from the layout's `entries`: settings rows change
// in place, the options and controls open over it, the photosensitivity notice shows in it, the developer tools' console
// opens with --dev, and a field menu page opens the field menu there. The title leaves out the field menu's pages, a
// battle shows only UiFields.BattleExtras. A page the field menu's entries leave out opens the field menu on that page alone.
public sealed partial class SampleController
{
	private bool _extrasOverlay;
	private bool _extrasNotice;
	// The options were opened on their Controls page from Extras, so leaving the page leaves them.
	private bool _controlsOnly;
	// The field menu's tab the next ResetUi starts on, for a page Extras opens.
	private int _menuTabNext;
	// A page the field menu's entries leave out, which the next ResetUi opens the menu on alone.
	private string? _menuOnlyNext;
	private readonly ScrollList _extrasList = new();

	// The System action: the field menu with the system page as its only tab, whatever its entries list.
	private void OpenSystem()
	{
		_menuOnlyNext = "system";
		_menuTabNext = 0;
		PsivResult opened = _s.OpenMenu();
		if (!opened.Ok)
		{
			_menuOnlyNext = null;
		}
		Report(opened);
	}

	private void OpenExtras()
	{
		_extrasOverlay = true;
		_extrasNotice = false;
		_extrasList.Reset();
	}

	private List<(string Id, string Label)> ExtrasRows()
	{
		var rows = new List<(string, string)>();
		foreach ((string id, string? label) in Layout.Screen("extras").Entries ?? [.. UiFields.ExtrasDefault.Select(e => (e, (string?)null))])
		{
			bool page = UiFields.MenuPages.Contains(id) && id != "options";
			bool shown = _battle.Active ? UiFields.BattleExtras.Contains(id)
				: id == "dev" ? _dev
				: !page || _s.Mode != Mode.Title;
			if (shown)
			{
				rows.Add((id, label ?? id switch
				{
					"options" => "ui.menu.tab.options",
					"controls" => "ui.options.controls",
					"photosensitivity" or "dev" => "ui.extras." + id,
					_ when page => MenuLabel(id),
					_ => "",
				}));
			}
		}
		return rows;
	}

	private void UpdateExtras(ActionInput input)
	{
		if (_extrasNotice)
		{
			if (input.Back || input.Confirm)
			{
				_extrasNotice = false;
			}
			return;
		}
		List<(string Id, string Label)> rows = ExtrasRows();
		if (input.Back || input.Pressed(GameAction.Extras) || rows.Count == 0)
		{
			_extrasOverlay = false;
			return;
		}
		_extrasList.Move(input, rows.Count);
		string id = rows[_extrasList.Cursor].Id;
		int by = input.Pressed(GameAction.Left) ? -1 : input.Pressed(GameAction.Right) || input.Confirm ? 1 : 0;
		if (UiFields.SettingRows.Contains(id))
		{
			if (by != 0)
			{
				Report(SettingRow(id)!.Change!(by));
			}
			return;
		}
		if (!input.Confirm)
		{
			return;
		}
		switch (id)
		{
		case "options":
			OpenOptions();
			break;
		case "controls":
			OpenOptions();
			_controlsPage = true;
			_controlsOnly = true;
			_optSub.Reset();
			break;
		case "photosensitivity":
			_extrasNotice = true;
			break;
		case "dev":
			_extrasOverlay = false;
			_console = true;
			break;
		default:
			_extrasOverlay = false;
			bool listed = MenuEntries.Contains(id);
			if (_beforeOverlay == Screen.Menu)
			{
				_menuOnly = listed ? null : id;
				_tab = Array.IndexOf(MenuTabs, id);
				_menuPageOpen = true;
				_pickMember = null;
				_pickAction = null;
				_list.Reset();
				_sub.Reset();
				ResetMenuPages();
				ResetMenuChild();
			}
			else
			{
				_menuOnlyNext = listed ? null : id;
				_menuTabNext = listed ? Array.IndexOf(MenuEntries, id) : 0;
				_menuPageNext = true;
				PsivResult opened = _s.OpenMenu();
				_menuPageNext &= opened.Ok;
				Report(opened);
			}
			break;
		}
	}

	private void DrawExtras(IDrawSurface s, IDrawSurface t, Art art)
	{
		List<(string Id, string Label)> rows = ExtrasRows();
		UiContext data = UiData();
		string description = "";
		if (rows.Count > 0 && rows[Math.Clamp(_extrasList.Cursor, 0, rows.Count - 1)].Id is var at && UiFields.SettingRows.Contains(at))
		{
			description = SettingRow(at)!.Description;
		}
		Ui.Draw("extras", s, t, data
			.Set("scene", (UiSlot)((_, _, _, _) => DrawUnder("extras", () =>
			{
				switch (_beforeOverlay)
				{
				case Screen.Title: DrawTitle(s, t, art); break;
				case Screen.Menu: DrawMenu(s, t, art); break;
				case Screen.Battle: DrawBattle(s, art); break;
				default: DrawField(s, art); break;
				}
			})))
			.Set("rows", rows.Select(r => UiFields.SettingRows.Contains(r.Id) ? SettingItem(data, r.Id) : new UiContext(data).Set("id", r.Id).Set("text", T(r.Label))
				.Set("label", T(r.Label)).Set("value", "").Set("changeable", false)).ToList())
			.Set("list", _extrasList)
			.Set("view", _extrasNotice ? "notice" : "list")
			.Set("notice", T("ui.options.flashing_notice"))
			.Set("description", description)
			.Set("in_battle", _battle.Active)
			.Set("on_title", _s.Mode == Mode.Title)
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(t, t.Width - x - w, y + h, right: true)))
			.Set("hint", _extrasNotice ? T("ui.menu.hint.back", Hint(GameAction.Back))
				: T("ui.options.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Left), Hint(GameAction.Right), Hint(GameAction.Confirm), Hint(GameAction.Back))));
	}
}
