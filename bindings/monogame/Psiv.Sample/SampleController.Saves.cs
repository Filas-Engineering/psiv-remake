using System.Globalization;

using Psiv.Session;

namespace Psiv.Sample;

// The slot list the save prompt, the field menu's Save page and the title's Load screen share, and the Load screen.
public sealed partial class SampleController
{
	private const long UnixYear2000 = 946_684_800;

	private List<string> ManualSlots() => Enumerable.Range(0, _s.GameInfo().ManualSlots).Select(i => $"manual_{i}").ToList();

	private static string Clock(long ms)
	{
		long seconds = ms / 1000;
		return string.Create(CultureInfo.InvariantCulture, $"{seconds / 3600}:{seconds / 60 % 60:00}:{seconds % 60:00}");
	}

	// One row per slot, two lines each: the slot with its area, play time and difficulty, then the party and when it was
	// written. Without the wall clock the session writes a counter instead of a time, shown as none.
	private void DrawSlots(IDrawSurface s, ScrollList list, int x, int y, int w, int h, IReadOnlyList<string> slots, IReadOnlyList<SaveSummary> saves,
		bool active = true, UiContext? page = null)
	{
		UiContext data = page ?? UiArea(x, y, w, h);
		Ui.Draw("save_slots", s, s, data
			.Set("slots", slots.Select((slot, i) => SlotRow(data, slot, i, saves)).ToList())
			.Set("list", list)
			.Set("active", active));
	}

	private UiContext SlotRow(UiContext data, string slot, int index, IReadOnlyList<SaveSummary> saves)
	{
		SaveSummary? save = saves.FirstOrDefault(v => v.Slot == slot);
		string name = slot + (save is { Autosave: true } && AutosaveOn ? " " + T("ui.saves.autosave") : "") + (_dev && save is { Dev: true } ? " " + T("ui.saves.dev") : "");
		string head = save is null ? $"{name}   {T("ui.saves.empty")}"
			: save.Damaged ? $"{name}   {T("ui.saves.damaged")}"
			: $"{name}   {_s.DisplayName(save.Area)}   {Clock(save.PlayTimeMs)}   {_s.DisplayName(save.Difficulty)}";
		string detail = "", party = "", written = "";
		if (save is { Damaged: false })
		{
			party = string.Join(", ", save.Party.Select(p => p.Split(':') is [string id, string level] ? $"{_s.DisplayName(id)} {T("ui.common.level_short", level)}" : p));
			written = save.WriteTime >= UnixYear2000 ? DateTimeOffset.FromUnixTimeSeconds(save.WriteTime).ToLocalTime().ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture) : "-";
			detail = T("ui.saves.written", party, written);
		}
		else if (save is { Damaged: true })
		{
			detail = save.Error;
		}
		return new UiContext(data)
			.Set("text", slot + (save is null ? " empty" : save.Damaged ? " damaged" : " " + save.Area) + (_dev && save is { Dev: true } ? " dev" : ""))
			.Set("head", head)
			.Set("enabled", save is not { Damaged: true })
			.Set("detail", detail)
			.Set("detail_color", save is { Damaged: true } ? "bad" : "dim")
			.Set("number", index + 1)
			.Set("slot", slot)
			.Set("empty", save is null)
			.Set("damaged", save is { Damaged: true })
			.Set("area", save is { Damaged: false } ? _s.DisplayName(save.Area) : "")
			.Set("play_time", save is { Damaged: false } ? Clock(save.PlayTimeMs) : "")
			.Set("difficulty", save is { Damaged: false } ? _s.DisplayName(save.Difficulty) : "")
			.Set("party_text", party)
			.Set("leader", save is { Damaged: false, Party: [string first, ..] } ? _s.DisplayName(first.Split(':')[0]) : "")
			.Set("level", save is { Damaged: false, Party: [string lead, ..] } && lead.Split(':') is [_, string lv] && int.TryParse(lv, CultureInfo.InvariantCulture, out int n) ? n : 0)
			.Set("written", written)
			.Set("newest", save is { Newest: true })
			.Set("write_time", save?.WriteTime ?? 0);
	}

	// The field menu's Save page: with the save_slots layout's `system_menu`, a system menu first: save (then the slots) or
	// quit to the title. The session loads saves only from the title, so there is no load entry here.
	private static readonly string[] SystemMenu = ["save", "quit"];
	private string? _saveMode;
	private int _systemCursor;

	private bool SystemMenuOn => Ui.Param("save_slots", "system_menu", UiData(), 0) != 0;

	private bool SaveAtTop => !SystemMenuOn || _saveMode is null;

	private void UpdateSavePage(ActionInput input)
	{
		List<string> slots = ManualSlots();
		if (SystemMenuOn && _saveMode is null)
		{
			if (input.Pressed(GameAction.Up))
			{
				_systemCursor = (_systemCursor + SystemMenu.Length - 1) % SystemMenu.Length;
			}
			if (input.Pressed(GameAction.Down))
			{
				_systemCursor = (_systemCursor + 1) % SystemMenu.Length;
			}
			if (input.Confirm && SystemMenu[_systemCursor] == "quit")
			{
				_confirm.Ask(T("ui.pause.quit_confirm"), () => Report(_s.QuitToTitle()));
			}
			else if (input.Confirm)
			{
				_saveMode = SystemMenu[_systemCursor];
				_list.Reset();
			}
			return;
		}
		_list.Move(input, slots.Count);
		if (input.Back && SystemMenuOn)
		{
			_saveMode = null;
		}
		else if (input.Confirm)
		{
			SaveTo(slots[_list.Cursor]);
		}
	}

	private void DrawSavePage(IDrawSurface t, int x, int y, int w, int h)
	{
		bool system = SystemMenuOn && _saveMode is null;
		UiContext page = UiArea(x, y, w, h)
			.Set("system", system)
			.Set("system_rows", SystemMenu.Select(row => T("ui.saves.system." + row)).ToList())
			.Set("system_cursor", _systemCursor)
			.Set("mode", SystemMenuOn ? _saveMode ?? "" : "save");
		if (system)
		{
			_probeSelected = T("ui.saves.system." + SystemMenu[_systemCursor]);
		}
		DrawSlots(t, _list, x, y, w, h, ManualSlots(), _s.Saves() ?? [], active: !system, page);
	}

	// Saves to the slot, asking first when a save is there.
	private void SaveTo(string slot, Action? after = null)
	{
		void Write()
		{
			PsivResult saved = _s.SaveManual(slot);
			Report(saved);
			if (saved.Ok)
			{
				after?.Invoke();
			}
		}
		if (_s.Saves()?.Any(v => v.Slot == slot) == true)
		{
			_confirm.Ask(T("ui.saves.overwrite", slot), Write);
		}
		else
		{
			Write();
		}
	}

	// Every slot with a file, manual ones first, then the autosaves.
	// game.json's `autosave` (R175): false writes no autosave, so the lists mark none and the title offers no retry.
	private bool AutosaveOn => (_gameInfo ??= _s.GameInfo()).Autosave;

	private List<SaveSummary> LoadableSaves() => [.. (_s.Saves() ?? []).OrderBy(v => v.Autosave)];

	// The Load list's slots: those with a file, manual first, or with the load_game layout's `show_empty` every manual slot
	// (empty ones too) before the autosaves.
	private List<string> LoadSlots(List<SaveSummary> saves) => Ui.Param("load_game", "show_empty", UiData(), 0) != 0
		? [.. ManualSlots(), .. saves.Where(v => v.Autosave && !ManualSlots().Contains(v.Slot)).Select(v => v.Slot)]
		: [.. saves.Select(v => v.Slot)];

	// The Load list opens on its first row, or on the save Continue would load (SaveSummary.Newest) when it opens for the
	// title's Continue (`continue` "choose") or the load_game layout's `select_newest` is on.
	private void OpenLoadList(bool newest)
	{
		_loadOverlay = true;
		_list.Reset();
		if (newest || Ui.Param("load_game", "select_newest", UiData(), 0) != 0)
		{
			List<SaveSummary> saves = LoadableSaves();
			_list.Cursor = Math.Max(0, LoadSlots(saves).FindIndex(slot => saves.Any(v => v.Slot == slot && v.Newest)));
		}
	}

	private void UpdateLoadGame(ActionInput input)
	{
		List<SaveSummary> saves = LoadableSaves();
		List<string> slots = LoadSlots(saves);
		MoveLoadCursor(input, slots.Count);
		if (input.Back)
		{
			_loadOverlay = false;
		}
		else if (input.Confirm && slots.Count > 0)
		{
			SaveSummary? save = saves.FirstOrDefault(v => v.Slot == slots[_list.Cursor]);
			if (save is null)
			{
				_audio.PlayCue("refuse");
			}
			else if (save.Damaged)
			{
				Say(T("ui.saves.cannot_load", save.Slot));
			}
			else
			{
				Report(_s.Load(save.Slot));
			}
		}
	}

	// The load_game layout's `navigation`: `list` (Up and Down), `grid` (`columns` wide: Left and Right step, Up and Down
	// move a row, stopping at the edges) or `row` (Left and Right, wrapping).
	private void MoveLoadCursor(ActionInput input, int count)
	{
		string navigation = Ui.ParamText("load_game", "navigation", UiData(), "list");
		if (navigation == "list" || count == 0)
		{
			_list.Move(input, count);
			return;
		}
		int at = Math.Clamp(_list.Cursor, 0, count - 1);
		int columns = navigation == "grid" ? Math.Max(1, Ui.Param("load_game", "columns", UiData(), 1)) : count;
		bool wrap = navigation == "row";
		if (input.Pressed(GameAction.Left))
		{
			at = wrap ? (at + count - 1) % count : Math.Max(0, at - 1);
		}
		if (input.Pressed(GameAction.Right))
		{
			at = wrap ? (at + 1) % count : Math.Min(count - 1, at + 1);
		}
		if (navigation == "grid" && input.Pressed(GameAction.Up) && at - columns >= 0)
		{
			at -= columns;
		}
		if (navigation == "grid" && input.Pressed(GameAction.Down) && at + columns < count)
		{
			at += columns;
		}
		_list.Cursor = at;
	}

	// The Load screen's own slot blocks: each slot's row fields, its party as members (id, name, level, portrait, standing
	// sprite) and whether the cursor is on it. Made only when the layout reads `slots`.
	private List<UiContext> LoadBlocks(UiContext data, Art art, List<string> slots, List<SaveSummary> saves)
	{
		if (!Layout.Screen("load_game").Reads("slots"))
		{
			return [];
		}
		var blocks = new List<UiContext>();
		for (int i = 0; i < slots.Count; i++)
		{
			SaveSummary? save = saves.FirstOrDefault(v => v.Slot == slots[i]);
			List<UiContext> members = save is { Damaged: false } ? [.. save.Party.Select(p => p.Split(':')).Select((p, k) =>
			{
				SaveMember? points = save.Members?.ElementAtOrDefault(k) is { } m && m.Id == p[0] ? m : null;
				return new UiContext()
					.Set("id", p[0])
					.Set("name", _s.DisplayName(p[0]))
					.Set("level", p.Length > 1 && int.TryParse(p[1], CultureInfo.InvariantCulture, out int level) ? level : 0)
					.Set("portrait", art.Portrait("", p[0]))
					.Set("sprite", art.PersonFrame(p[0], Direction.South, 0))
					.Set("hp", Known(points?.Hp))
					.Set("hp_max", Known(points?.MaxHp))
					.Set("tp", Known(points?.Tp))
					.Set("tp_max", Known(points?.MaxTp))
					.Set("statuses", string.Join(" ", (points?.Statuses ?? []).Select(_s.DisplayName)));
			})] : [];
			blocks.Add(SlotRow(data, slots[i], i, saves)
				.Set("money", save is { Damaged: false } ? Known(save.Money) : "")
				.Set("autosave", save is { Autosave: true })
				.Set("members", members)
				.Set("member_count", members.Count)
				.Set("selected", i == _list.Cursor));
		}
		return blocks;
	}

	// A save from before save schema 16 has no money or points (-1): "", so a layout's text shows nothing.
	private static object Known(int? value) => value is int v and >= 0 ? v : "";

	// The Load hint: with the layout's `navigation_hint`, the directions its navigation moves by (`ui.saves.hint_grid`,
	// `ui.saves.hint_row`, each falling back to `ui.saves.hint`).
	private string LoadHint(UiContext data)
	{
		string navigation = Ui.ParamText("load_game", "navigation", data, "list");
		string[] keys = [Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Left), Hint(GameAction.Right), Hint(GameAction.Confirm), Hint(GameAction.Back)];
		if (Ui.Param("load_game", "navigation_hint", data, 0) != 0 && navigation is ("grid" or "row") && _s.FormatText("ui.saves.hint_" + navigation, keys) is var own
			&& own != "ui.saves.hint_" + navigation)
		{
			return Shown(own);
		}
		return T("ui.saves.hint", keys[0], keys[1], keys[4], keys[5]);
	}

	private void DrawLoadGame(IDrawSurface s, IDrawSurface t, Art art)
	{
		List<SaveSummary> saves = LoadableSaves();
		List<string> slots = LoadSlots(saves);
		UiContext data = UiData();
		Ui.Draw("load_game", s, t, data
			.Set("scene", (UiSlot)((x, y, w, h) => DrawTitleScene(t, art, x, y, w, h)))
			.Set("has_saves", saves.Count > 0)
			.Set("slots", LoadBlocks(data, art, slots, saves))
			.Set("cursor", _list.Cursor)
			.Set("navigation", Ui.ParamText("load_game", "navigation", data, "list"))
			.Set("columns", Math.Max(1, Ui.Param("load_game", "columns", data, 1)))
			.Set("slot_list", (UiSlot)((x, y, w, h) => DrawSlots(t, _list, x, y, w, h, slots, saves)))
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(t, t.Width - x - w, y + h, right: true)))
			.Set("hint", LoadHint(data)));
	}
}
