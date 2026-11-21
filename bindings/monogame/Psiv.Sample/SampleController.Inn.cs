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

	private void DrawInn(IDrawSurface s, IDrawSurface t, Art art)
	{
		bool field = DrawField(s, art);
		int y = DialogueTop(s, field);
		Style.Box(t, 24, y, s.Width - 48, DialogueHeight);
		Style.Plate(t, "Inn", 42, y - Px(16), Style.Accent, Style.Body);
		t.Image(AreaTiles(art).Bed, 48, y + 34, 96, 96, Color.White);
		InnInfo? inn = _s.Inn();
		t.Text($"A night's rest costs {inn?.Cost ?? 0}. You have {_s.WorldInfo().Money}.", 170, y + Px(30), Style.Text);
		for (int i = 0; i < 2; i++)
		{
			Style.ListRow(t, i == 0 ? "Rest" : "Leave", 186, y + Px(74) + i * Px(30), 300, i == _cursor, height: Px(28));
		}
	}
}
