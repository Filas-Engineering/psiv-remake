using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateInn(ActionInput input)
	{
		Move(input, 2);
		if (input.Back || (input.Confirm && _cursor == 1))
		{
			Report(_s.CloseMenu());
		}
		else if (input.Confirm)
		{
			Report(_s.Rest());
		}
	}

	private void DrawInn(IDrawSurface s, Art art)
	{
		bool field = DrawField(s, art);
		int y = DialogueTop(s, field);
		Style.Box(s, 24, y, s.Width - 48, DialogueHeight);
		Style.Plate(s, "Inn", 42, y - 16, Style.Accent, Style.Body);
		s.Image(art.Bed, 48, y + 34, 96, 96, Color.White);
		InnInfo? inn = _s.Inn();
		s.Text($"A night's rest costs {inn?.Cost ?? 0}. You have {_s.WorldInfo().Money}.", 170, y + 30, Style.Text);
		DrawList(s, 186, y + 74, ["Rest", "Leave"], width: 300);
	}
}
