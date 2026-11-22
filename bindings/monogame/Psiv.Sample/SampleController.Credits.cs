using Psiv.Session;

namespace Psiv.Sample;

// The credits a script's `credits` plays (MenuKind.Credits): the entries that have come up, each after its gap of empty
// lines, headings in the title style and lines in the body style, scrolled so the newest stays in view. The session moves
// them on with each update; Confirm or Back ends them, and so does the session's own end once every entry has had its
// time. CloseMenu() tells the session they finished, and the event goes on (an `end game` after them returns to the title).
public sealed partial class SampleController
{
	// The credits' lines as the last frame laid them out: how many show and how far they scrolled.
	private string _probeCredits = "-";

	private void UpdateCredits(int dtMs, ActionInput input)
	{
		_s.Update(dtMs);
		if (_s.Credits() is not { } credits)
		{
			return;
		}
		bool done = credits.Shown >= credits.Entries.Count && credits.ElapsedMs >= credits.TotalMs;
		if (input.Confirm || input.Back || done)
		{
			Report(_s.CloseMenu());
		}
	}

	private void DrawCredits(IDrawSurface s, IDrawSurface t)
	{
		CreditsInfo? credits = _s.Credits();
		var lines = new List<UiContext>();
		UiContext data = UiData();
		int y = 0, line = Px(Style.LineHeight) + Px(8);
		for (int i = 0; credits is not null && i < Math.Min(credits.Shown, credits.Entries.Count); i++)
		{
			CreditsEntry entry = credits.Entries[i];
			y += entry.Gap * line;
			lines.Add(new UiContext(data).Set("text", Shown(entry.Text)).Set("heading", entry.Kind == CreditsEntryKind.Heading).Set("y", y));
			y += entry.Kind == CreditsEntryKind.Heading ? line + Px(12) : line;
		}
		// Scrolled so the newest line stays inside the layout's `roll` window.
		int rollH = Ui.WindowRect("credits", "roll", t, data)?.Height ?? t.Height - 120;
		int scroll = Math.Max(0, y - rollH);
		_probeCredits = credits is null ? "-" : $"{lines.Count}/{credits.Entries.Count} scroll {scroll}";
		Ui.Draw("credits", s, t, data
			.Set("lines", lines)
			.Set("scroll", scroll)
			.Set("shown", lines.Count)
			.Set("total", credits?.Entries.Count ?? 0)
			.Set("hint", T("ui.credits.hint", Hint(GameAction.Confirm))));
	}
}
