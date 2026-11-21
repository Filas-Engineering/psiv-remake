using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateNameEntry(ActionInput input)
	{
		foreach (Keys k in input.Raw.PressedKeys)
		{
			if (k is >= Keys.A and <= Keys.Z && _nameBuffer.Length < 16)
			{
				char c = (char)('a' + (k - Keys.A));
				_nameBuffer += _nameBuffer.Length == 0 || _nameBuffer[^1] == ' ' ? char.ToUpperInvariant(c) : c;
			}
			else if (k == Keys.Space && _nameBuffer.Length < 16)
			{
				_nameBuffer += ' ';
			}
		}
		if ((input.Raw.Pressed(Keys.Back) || input.ButtonPressed(GameAction.Back)) && _nameBuffer.Length > 0)
		{
			_nameBuffer = _nameBuffer[..^1];
		}
		if (input.Raw.Pressed(Keys.Enter) || input.ButtonPressed(GameAction.Confirm))
		{
			Report(_s.EnterName(_nameBuffer.Length == 0 ? _s.DisplayName(_s.NameEntryMember) : _nameBuffer));
		}
	}

	private void DrawNameEntry(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int w = 760;
		int x = (s.Width - w) / 2;
		int y = 180;
		Style.Window(s, x, y, w, 220, "Name");
		string member = _s.NameEntryMember;
		s.Image(art.ActorFrame(Art.LookFor(member), Direction.South, 0), x + 28, y + 40, 96, 96, Color.White);
		s.Text($"A name for {_s.DisplayName(member)}:", x + 150, y + 36, Style.Accent);
		s.Fill(x + 150, y + 80, 420, 50, Style.Track);
		Style.Frame(s, x + 150, y + 80, 420, 50, Style.PanelEdge);
		string cursor = _clockMs / 400 % 2 == 0 ? "_" : " ";
		s.Text(_nameBuffer + cursor, x + 166, y + 88, Style.Text, Style.Title);
		s.Text(_usingGamepad
			? $"Type letters   Backspace or {Hint(GameAction.Back)} erase   Enter or {Hint(GameAction.Confirm)} confirm (empty keeps the name)"
			: "Type letters   Backspace erase   Enter confirm (empty keeps the name)", x + 28, y + 176, Style.Dim, Style.Small);
	}
}
