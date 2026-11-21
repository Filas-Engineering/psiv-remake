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
		bool active = true)
	{
		UiContext data = UiArea(x, y, w, h);
		UiContext Row(string slot)
		{
			SaveSummary? save = saves.FirstOrDefault(v => v.Slot == slot);
			string name = slot + (save is { Autosave: true } ? " " + T("ui.saves.autosave") : "") + (_dev && save is { Dev: true } ? " " + T("ui.saves.dev") : "");
			string head = save is null ? $"{name}   {T("ui.saves.empty")}"
				: save.Damaged ? $"{name}   {T("ui.saves.damaged")}"
				: $"{name}   {_s.DisplayName(save.Area)}   {Clock(save.PlayTimeMs)}   {_s.DisplayName(save.Difficulty)}";
			string detail = "";
			if (save is { Damaged: false })
			{
				string party = string.Join(", ", save.Party.Select(p => p.Split(':') is [string id, string level] ? $"{_s.DisplayName(id)} {T("ui.common.level_short", level)}" : p));
				string written = save.WriteTime >= UnixYear2000 ? DateTimeOffset.FromUnixTimeSeconds(save.WriteTime).ToLocalTime().ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture) : "-";
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
				.Set("detail_color", save is { Damaged: true } ? "bad" : "dim");
		}
		Ui.Draw("save_slots", s, s, data
			.Set("slots", slots.Select(Row).ToList())
			.Set("list", list)
			.Set("active", active));
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
			.Set("scene", (UiSlot)((_, _, _, _) => DrawTitleScene(t, art)))
			.Set("has_saves", saves.Count > 0)
			.Set("slot_list", (UiSlot)((x, y, w, h) => DrawSlots(t, _list, x, y, w, h, saves.Select(v => v.Slot).ToList(), saves)))
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(t, t.Width - x - w, y + h, right: true)))
			.Set("hint", T("ui.saves.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back))));
	}
}
