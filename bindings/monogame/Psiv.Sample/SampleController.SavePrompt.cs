using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateSavePrompt(ActionInput input)
	{
		List<string> slots = ManualSlots();
		_list.Move(input, slots.Count + 1);
		if (input.Back || (input.Confirm && _list.Cursor == slots.Count))
		{
			Report(_s.CloseMenu());
		}
		else if (input.Confirm)
		{
			SaveTo(slots[_list.Cursor], () => Report(_s.CloseMenu()));
		}
	}

	private void DrawSavePrompt(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		List<string> slots = ManualSlots();
		int w = 760;
		int h = s.Height - 120;
		int x = (s.Width - w) / 2;
		int y = 60;
		Style.Window(s, x, y, w, h, "Save");
		s.Text("Save your progress?", x + 28, y + Px(28), Style.Accent);
		int listTop = y + Px(72);
		int leave = Px(40);
		DrawSlots(s, _list, x + 40, listTop, w - 70, h - (listTop - y) - leave - 20, slots, _s.Saves() ?? [], _list.Cursor < slots.Count);
		Style.ListRow(s, "Don't save", x + 40, y + h - leave - 10, w - 84, _list.Cursor == slots.Count, height: Px(28));
		if (_list.Cursor == slots.Count)
		{
			_probeSelected = "Don't save";
		}
	}
}
