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

	private void DrawSavePrompt(IDrawSurface s, IDrawSurface t, Art art)
	{
		List<string> slots = ManualSlots();
		Ui.Draw("save_prompt", s, t, UiData()
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art, "save_prompt")))
			.Set("slot_list", (UiSlot)((x, y, w, h) => DrawSlots(t, _list, x, y, w, h, slots, _s.Saves() ?? [], _list.Cursor < slots.Count)))
			.Set("leave", new List<string> { T("ui.save_prompt.dont_save") })
			.Set("leave_selected", _list.Cursor == slots.Count));
		if (_list.Cursor == slots.Count)
		{
			_probeSelected = T("ui.save_prompt.dont_save");
		}
	}
}
