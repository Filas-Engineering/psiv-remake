using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateField(int dtMs, ActionInput input)
	{
		if (input.Confirm)
		{
			Report(_s.Interact());
		}
		else if (input.Pressed(GameAction.Menu))
		{
			Report(_s.OpenMenu());
		}
		else if (input.Pressed(GameAction.PartyTalk))
		{
			Report(_s.PartyTalk());
		}
		else if (input.Pressed(GameAction.QuestLog))
		{
			_questOverlay = true;
		}
		else if (input.Pressed(GameAction.TextLog))
		{
			_logOverlay = true;
		}
		_s.Update(dtMs, input.Held);
	}

	private bool DrawField(IDrawSurface s, Art art)
	{
		FieldView? view = _s.Field();
		if (view is null)
		{
			return false;
		}
		_field.Draw(s, art, view, _clockMs, Lineup());
		return true;
	}

	private void DrawHud(IDrawSurface s)
	{
		FieldInfo? f = _s.FieldInfo();
		if (f is null)
		{
			return;
		}
		WorldInfo w = _s.WorldInfo();
		long seconds = w.PlayTimeMs / 1000;
		Style.Plate(s, f.AreaName, 16, 12, Style.Accent);
		string status = $"Money {w.Money}    {seconds / 3600}:{seconds / 60 % 60:00}:{seconds % 60:00}";
		Style.Plate(s, status, s.Width - s.Measure(status, Style.Small) - 40, 12, Style.Text);
	}
}
