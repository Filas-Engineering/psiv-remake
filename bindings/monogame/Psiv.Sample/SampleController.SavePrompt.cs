using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateSavePrompt(ActionInput input)
	{
		int slots = _s.GameInfo().ManualSlots;
		Move(input, slots + 1);
		if (input.Back || (input.Confirm && _cursor == slots))
		{
			Report(_s.CloseMenu());
		}
		else if (input.Confirm)
		{
			PsivResult saved = _s.SaveManual($"manual_{_cursor}");
			Report(saved);
			if (saved.Ok)
			{
				Report(_s.CloseMenu());
			}
		}
	}

	private void DrawSavePrompt(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int slots = _s.GameInfo().ManualSlots;
		int w = 560;
		int h = 110 + (slots + 1) * 30;
		int x = (s.Width - w) / 2;
		int y = (s.Height - h) / 2;
		Style.Window(s, x, y, w, h, "Save");
		s.Text("Save your progress?", x + 28, y + 28, Style.Accent);
		DrawList(s, x + 40, y + 72, Enumerable.Range(0, slots).Select(i => $"manual_{i}").Append("Don't save").ToList(), width: w - 80);
	}
}
