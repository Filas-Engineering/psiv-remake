using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateDialogue(int dtMs, ActionInput input)
	{
		if (input.Pressed(GameAction.Skip) && _s.CanSkip)
		{
			Report(_s.Skip());
			return;
		}
		if (_s.Line() is { } line)
		{
			if (line.Display != _lastLine)
			{
				_lastLine = line.Display;
				_log("line: " + line.Display);
			}
			if (input.Confirm)
			{
				_s.ConfirmLine();
				_lastLine = "";
			}
			return;
		}
		if (_s.Choice() is { } choice)
		{
			Move(input, choice.Options.Count);
			if (input.Confirm)
			{
				_log($"choose: {_cursor} {choice.Options[_cursor]}");
				Report(_s.Choose(_cursor));
				_cursor = 0;
			}
			return;
		}
		_s.Update(dtMs);
	}

	private const int DialogueHeight = 176;

	// The box sits at the bottom of the screen, or at the top when the leader stands where it would cover them.
	private int DialogueTop(IDrawSurface s, bool field)
	{
		int bottom = s.Height - DialogueHeight - 16;
		return field && _field.LeaderScreenY + Art.Tile > bottom - 8 ? 36 : bottom;
	}

	private void DrawDialogue(IDrawSurface s, Art art)
	{
		bool field = DrawField(s, art);
		int y = DialogueTop(s, field);
		if (y > s.Height / 2)
		{
			_field.DrawBanner(s);
		}
		int x = 24;
		int w = s.Width - 48;
		// Between lines (a script actor walking, a fade) the field shows without an empty box.
		if (!_s.LinePending && _s.Choice() is null)
		{
			return;
		}
		Style.Box(s, x, y, w, DialogueHeight);
		if (_s.Line() is { } line)
		{
			int tx = x + 26;
			if (line.SpeakerName.Length > 0)
			{
				Style.Plate(s, line.SpeakerName, x + 18, y - 16, Style.Accent, Style.Body);
				int px = x + 22;
				int py = y + 34;
				s.Fill(px - 2, py - 2, 100, 100, Style.PanelEdge);
				s.Fill(px, py, 96, 96, Style.Track);
				s.Fill(px, py + 56, 96, 40, Style.PanelTop);
				ActorLook look = Art.LookFor(line.SpeakerId.Length > 0 ? line.SpeakerId : line.SpeakerName);
				s.Image(art.ActorFrame(look, Direction.South, 0), px + 8, py + 8, 80, 80, Color.White);
				tx = px + 122;
			}
			int ty = y + 30;
			foreach (string l in Style.Wrap(s, line.Text, x + w - tx - 30))
			{
				s.Text(l, tx, ty, Style.Text);
				ty += Style.LineHeight;
			}
			string hint = $"{Hint(GameAction.Confirm)} next" + (_s.CanSkip ? $"   {Hint(GameAction.Skip)} skip" : "");
			s.Text(hint, x + w - s.Measure(hint, Style.Small) - 58, y + DialogueHeight - 32, Style.Dim, Style.Small);
			s.Image(art.Pointer, x + w - 48, y + DialogueHeight - 40 + _clockMs / 260 % 2 * 3, 24, 24, Color.White);
		}
		else if (_s.Choice() is { } choice)
		{
			s.Text(choice.Prompt.Length == 0 ? "Choose:" : choice.Prompt, x + 26, y + 22, Style.Accent);
			for (int i = 0; i < choice.Options.Count; i++)
			{
				Style.ListRow(s, choice.Options[i], x + 44, y + 62 + i * 30, 420, i == _cursor);
			}
		}
	}
}
