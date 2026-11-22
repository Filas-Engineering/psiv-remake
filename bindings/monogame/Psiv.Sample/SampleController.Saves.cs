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
		UiContext Row(string slot, int index)
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
				.Set("written", written);
		}
		Ui.Draw("save_slots", s, s, data
			.Set("slots", slots.Select(Row).ToList())
			.Set("list", list)
			.Set("active", active));
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

	private void UpdateLoadGame(ActionInput input)
	{
		List<SaveSummary> saves = LoadableSaves();
		_list.Move(input, saves.Count);
		if (input.Back)
		{
			_loadOverlay = false;
		}
		else if (input.Confirm && saves.Count > 0)
		{
			SaveSummary save = saves[_list.Cursor];
			if (save.Damaged)
			{
				Say(T("ui.saves.cannot_load", save.Slot));
			}
			else
			{
				Report(_s.Load(save.Slot));
			}
		}
	}

	private void DrawLoadGame(IDrawSurface s, IDrawSurface t, Art art)
	{
		List<SaveSummary> saves = LoadableSaves();
		Ui.Draw("load_game", s, t, UiData()
			.Set("scene", (UiSlot)((x, y, w, h) => DrawTitleScene(t, art, x, y, w, h)))
			.Set("has_saves", saves.Count > 0)
			.Set("slot_list", (UiSlot)((x, y, w, h) => DrawSlots(t, _list, x, y, w, h, saves.Select(v => v.Slot).ToList(), saves)))
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(t, t.Width - x - w, y + h, right: true)))
			.Set("hint", T("ui.saves.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back))));
	}
}
