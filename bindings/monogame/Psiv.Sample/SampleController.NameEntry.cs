using Microsoft.Xna.Framework;

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

	private void DrawNameEntry(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int w = Math.Max(760, Px(44) * 13 + 220);
		int x = (s.Width - w) / 2;
		int h = Px(380) + Px(40);
		int y = Math.Max(48, (s.Height - h) / 2);
		Style.Window(s, x, y, w, h, "Name");
		string member = _s.NameEntryMember;
		s.Image(art.Portrait("", member), x + 28, y + 40, 96, 96, Color.White);
		s.Text($"A name for {_s.DisplayName(member)}:", x + 150, y + Px(36), Style.Accent);
		int used = _nameEntry.Draw(s, x + 150, y + Px(76), w - 190, _clockMs, Px);
		Widgets.Hints(s, x + 28, y + Px(76) + used + Px(12), w - 56, _usingGamepad
			? $"{Hint(GameAction.Confirm)} type   {Hint(GameAction.Back)} erase   {Hint(GameAction.Menu)} or OK confirm (empty keeps the name)"
			: "Type letters or pick them   Backspace erase   Enter confirm (empty keeps the name)");
	}
}
