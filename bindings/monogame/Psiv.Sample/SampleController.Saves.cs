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
		int rowH = Px(56);
		list.Draw(s, x, y, w, h, rowH, slots.Count, (i, ry, on) =>
		{
			SaveSummary? save = saves.FirstOrDefault(v => v.Slot == slots[i]);
			string name = slots[i] + (save is { Autosave: true } ? " " + T("ui.saves.autosave") : "") + (_dev && save is { Dev: true } ? " " + T("ui.saves.dev") : "");
			string head = save is null ? $"{name}   {T("ui.saves.empty")}"
				: save.Damaged ? $"{name}   {T("ui.saves.damaged")}"
				: $"{name}   {_s.DisplayName(save.Area)}   {Clock(save.PlayTimeMs)}   {_s.DisplayName(save.Difficulty)}";
			Style.ListRow(s, Style.Fit(s, head, w - 40), x, ry, w - 14, on, save is not { Damaged: true }, height: rowH - Px(6));
			if (save is { Damaged: false })
			{
				string party = string.Join(", ", save.Party.Select(p => p.Split(':') is [string id, string level] ? $"{_s.DisplayName(id)} {T("ui.common.level_short", level)}" : p));
				string written = save.WriteTime >= UnixYear2000 ? DateTimeOffset.FromUnixTimeSeconds(save.WriteTime).ToLocalTime().ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture) : "-";
				s.Text(Style.Fit(s, T("ui.saves.written", party, written), w - 60, Style.Small), x + 34, ry + Px(26), Style.Dim, Style.Small);
			}
			else if (save is { Damaged: true } && save.Error.Length > 0)
			{
				s.Text(Style.Fit(s, save.Error, w - 60, Style.Small), x + 34, ry + Px(26), Style.Bad, Style.Small);
			}
		}, active);
		if (active && slots.Count > 0)
		{
			_probeSelected = slots[list.Cursor] + (saves.FirstOrDefault(v => v.Slot == slots[list.Cursor]) is { } shown ? shown.Damaged ? " damaged" : " " + shown.Area : " empty") +
				(_dev && saves.FirstOrDefault(v => v.Slot == slots[list.Cursor]) is { Dev: true } ? " dev" : "");
			_probeTop = list.Top;
		}
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

	private void DrawLoadGame(IDrawSurface s, Art art)
	{
		DrawTitleScene(s, art);
		s.Fill(0, 0, s.Width, s.Height, Microsoft.Xna.Framework.Color.Black * 0.5f);
		int x = 120;
		int w = s.Width - 240;
		Style.Window(s, x, 50, w, s.Height - 100, T("ui.saves.load_title"));
		List<SaveSummary> saves = LoadableSaves();
		if (saves.Count == 0)
		{
			s.Text(T("ui.saves.none"), x + 40, 90, Style.Dim);
		}
		DrawSlots(s, _list, x + 40, 84, w - 70, s.Height - 190, saves.Select(v => v.Slot).ToList(), saves);
		DrawToasts(s, x + 24, s.Height - 92, right: true);
		Widgets.Hints(s, x + 24, s.Height - 50 - Px(36), w - 48, T("ui.saves.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back)));
	}
}
