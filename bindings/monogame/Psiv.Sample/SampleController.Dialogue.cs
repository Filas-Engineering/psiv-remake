using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	// Auto-advance waits this long per character of the line once it is all shown, and at least the minimum.
	private const int AutoAdvanceMsPerChar = 50;
	private const int AutoAdvanceMinMs = 1000;

	private string _lastLine = "";
	// Frame time since the line came up, which reveals the text at the profile's speed, and since it was all shown.
	private int _lineMs;
	private int _shownMs;

	// Characters a second for each text speed; Instant shows the line at once.
	private static int CharsPerSecond(TextSpeed speed) => speed switch
	{
		TextSpeed.Slow => 25,
		TextSpeed.Normal => 50,
		TextSpeed.Fast => 100,
		_ => int.MaxValue,
	};

	private int Revealed(string text)
	{
		int cps = CharsPerSecond(_s.Profile().TextSpeed);
		return cps == int.MaxValue ? text.Length : (int)Math.Min(text.Length, (long)_lineMs * cps / 1000);
	}

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
				_lineMs = 0;
				_shownMs = 0;
				_log("line: " + line.Display);
			}
			else if (Revealed(line.Text) < line.Text.Length)
			{
				_lineMs += dtMs;
			}
			else
			{
				_shownMs += dtMs;
			}
			bool shown = Revealed(line.Text) == line.Text.Length;
			if (input.Confirm && !shown)
			{
				_lineMs = int.MaxValue / 2;
			}
			else if (input.Confirm || (shown && _s.Profile().AutoAdvance && _shownMs >= Math.Max(AutoAdvanceMinMs, line.Text.Length * AutoAdvanceMsPerChar)))
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

	private int DialogueHeight => Px(176);

	// The box sits at the bottom of the screen, or at the top when the leader stands where it would cover them.
	private int DialogueTop(IDrawSurface s, bool field)
	{
		int bottom = s.Height - DialogueHeight - 16;
		return field && _field.LeaderScreenY + Art.Tile > bottom - 8 ? 36 : bottom;
	}

	// `s` draws the field and `t` the box, with the profile's text size.
	private void DrawDialogue(IDrawSurface s, IDrawSurface t, Art art)
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
		Style.Box(t, x, y, w, DialogueHeight);
		if (_s.Line() is { } line)
		{
			int tx = x + 26;
			if (line.SpeakerName.Length > 0)
			{
				Style.Plate(t, line.SpeakerName, x + 18, y - Px(16), Style.Accent, Style.Body);
				int px = x + 22;
				int py = y + Px(34);
				t.Fill(px - 2, py - 2, 100, 100, Style.PanelEdge);
				t.Fill(px, py, 96, 96, Style.Track);
				t.Fill(px, py + 56, 96, 40, Style.PanelTop);
				ActorLook look = Art.LookFor(line.SpeakerId.Length > 0 ? line.SpeakerId : line.SpeakerName);
				t.Image(art.ActorFrame(look, Direction.South, 0), px + 8, py + 8, 80, 80, Color.White);
				tx = px + 122;
			}
			// Wrapped as a whole first, so a word being revealed does not jump to the next line.
			int left = Revealed(line.Text);
			bool all = left == line.Text.Length;
			int ty = y + Px(30);
			foreach (string l in Style.Wrap(t, line.Text, x + w - tx - 30))
			{
				if (left <= 0 && !all)
				{
					break;
				}
				t.Text(all ? l : l[..Math.Min(l.Length, left)], tx, ty, Style.Text);
				left -= l.Length + 1;
				ty += Px(Style.LineHeight);
			}
			string hint = $"{Hint(GameAction.Confirm)} next" + (_s.CanSkip ? $"   {Hint(GameAction.Skip)} skip" : "");
			t.Text(hint, x + w - t.Measure(hint, Style.Small) - 58, y + DialogueHeight - Px(32), Style.Dim, Style.Small);
			if (all)
			{
				t.Image(art.Pointer, x + w - 48, y + DialogueHeight - 40 + _clockMs / 260 % 2 * 3, 24, 24, Color.White);
			}
		}
		else if (_s.Choice() is { } choice)
		{
			t.Text(choice.Prompt.Length == 0 ? "Choose:" : choice.Prompt, x + 26, y + Px(22), Style.Accent);
			for (int i = 0; i < choice.Options.Count; i++)
			{
				Style.ListRow(t, choice.Options[i], x + 44, y + Px(62) + i * Px(30), 420, i == _cursor, height: Px(28));
			}
		}
	}
}
