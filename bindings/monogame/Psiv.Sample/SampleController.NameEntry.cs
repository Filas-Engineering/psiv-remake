using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private readonly TextEntry _nameEntry = new(16);

	private void UpdateNameEntry(ActionInput input)
	{
		if (_nameEntry.Update(input) == true)
		{
			Report(_s.EnterName(_nameEntry.Buffer.Length == 0 ? _s.DisplayName(_s.NameEntryMember) : _nameEntry.Buffer));
		}
	}

	private void DrawNameEntry(IDrawSurface s, IDrawSurface t, Art art)
	{
		string member = _s.NameEntryMember;
		Ui.Draw("name_entry", s, t, UiData()
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art)))
			.Set("portrait", art.Portrait("", member))
			.Set("member_name", _s.DisplayName(member))
			.Set("entry", (UiSlot)((x, y, w, _) => _nameEntry.Draw(t, x, y, w, _clockMs, Px, PadLabels(), Timing("caret_ms"))))
			.Set("entry_h", TextEntry.Height(Px))
			.Set("hint", _usingGamepad
				? T("ui.name_entry.hint_pad", Hint(GameAction.Confirm), Hint(GameAction.Back), Hint(GameAction.Menu), T("ui.name_pad.done"))
				: T("ui.name_entry.hint_keys", KeyText(Keys.Back), KeyText(Keys.Enter))));
	}
}
