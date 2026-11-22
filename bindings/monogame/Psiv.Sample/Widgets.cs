using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

namespace Psiv.Sample;

// A list's cursor and the first row shown. The window follows the cursor, so a list longer than its box scrolls.
public sealed class ScrollList
{
	public int Cursor { get; set; }
	public int Top { get; private set; }
	// Rows that fit at the last draw.
	public int Rows { get; private set; } = 1;

	public void Reset()
	{
		Cursor = 0;
		Top = 0;
	}

	// Up and Down, wrapping at the ends.
	public void Move(ActionInput input, int count)
	{
		if (count == 0)
		{
			Cursor = 0;
			return;
		}
		if (input.Pressed(GameAction.Up))
		{
			Cursor = (Cursor + count - 1) % count;
		}
		if (input.Pressed(GameAction.Down))
		{
			Cursor = (Cursor + 1) % count;
		}
		Cursor = Math.Clamp(Cursor, 0, count - 1);
	}

	// A list with nothing to pick: Up and Down move the first row shown, without wrapping.
	public void MoveView(ActionInput input, int count)
	{
		int last = Math.Max(0, count - Rows);
		Cursor = Math.Clamp(Cursor, 0, last);
		if (input.Pressed(GameAction.Up))
		{
			Cursor = Math.Max(0, Cursor - 1);
		}
		if (input.Pressed(GameAction.Down))
		{
			Cursor = Math.Min(last, Cursor + 1);
		}
	}

	// Calls `row(index, y, selected)` for each row in view and draws the scroll bar when the list does not fit. An
	// inactive list keeps its cursor, which may stand on a row drawn elsewhere.
	public void Draw(IDrawSurface s, int x, int y, int w, int h, int rowH, int count, Action<int, int, bool> row, bool active = true)
	{
		Rows = Math.Max(1, h / rowH);
		if (!active)
		{
			Top = Math.Clamp(Top, 0, Math.Max(0, count - Rows));
			DrawRows(s, x, y, w, rowH, count, row, -1);
			return;
		}
		Cursor = Math.Clamp(Cursor, 0, Math.Max(0, count - 1));
		if (Cursor < Top)
		{
			Top = Cursor;
		}
		if (Cursor >= Top + Rows)
		{
			Top = Cursor - Rows + 1;
		}
		Top = Math.Clamp(Top, 0, Math.Max(0, count - Rows));
		DrawRows(s, x, y, w, rowH, count, row, Cursor);
	}

	// The view of MoveView: the cursor is the first row shown.
	public void DrawView(IDrawSurface s, int x, int y, int w, int h, int rowH, int count, Action<int, int, bool> row)
	{
		Rows = Math.Max(1, h / rowH);
		Cursor = Math.Clamp(Cursor, 0, Math.Max(0, count - Rows));
		Top = Cursor;
		DrawRows(s, x, y, w, rowH, count, row, -1);
	}

	private void DrawRows(IDrawSurface s, int x, int y, int w, int rowH, int count, Action<int, int, bool> row, int selected)
	{
		for (int i = Top; i < Math.Min(count, Top + Rows); i++)
		{
			row(i, y + (i - Top) * rowH, i == selected);
		}
		if (count > Rows)
		{
			Widgets.ScrollBar(s, x + w - 6, y, Rows * rowH - 6, Top, Rows, count);
		}
	}
}

// A Yes/No question over the screen that asked it. No is selected first.
public sealed class ConfirmPrompt
{
	private Action? _yes;
	private bool _onYes;
	// A question with several answers instead of yes and no (Choose): each answer's words and what it does; Back takes
	// `_back`.
	private List<(string Text, Action Act)>? _choices;
	private int _cursor, _back;

	public string? Question { get; private set; }
	public bool OnYes => _onYes;
	public IReadOnlyList<string>? Choices => _choices?.Select(c => c.Text).ToList();
	public int Cursor => _choices is null ? (_onYes ? 0 : 1) : _cursor;

	public void Ask(string question, Action yes)
	{
		Question = question;
		_yes = yes;
		_onYes = false;
		_choices = null;
	}

	public void Choose(string question, List<(string Text, Action Act)> choices, int start, int back)
	{
		Question = question;
		_choices = choices;
		_cursor = start;
		_back = back;
	}

	public void Close() => Question = null;

	public void Update(ActionInput input)
	{
		if (_choices is { } choices)
		{
			if (input.Pressed(GameAction.Up) || input.Pressed(GameAction.Left))
			{
				_cursor = (_cursor + choices.Count - 1) % choices.Count;
			}
			if (input.Pressed(GameAction.Down) || input.Pressed(GameAction.Right))
			{
				_cursor = (_cursor + 1) % choices.Count;
			}
			if (input.Back || input.Confirm)
			{
				Question = null;
				_choices = null;
				choices[input.Confirm ? _cursor : _back].Act();
			}
			return;
		}
		if (input.Pressed(GameAction.Up) || input.Pressed(GameAction.Down) || input.Pressed(GameAction.Left) || input.Pressed(GameAction.Right))
		{
			_onYes = !_onYes;
		}
		if (input.Back)
		{
			Question = null;
		}
		else if (input.Confirm)
		{
			Question = null;
			if (_onYes)
			{
				_yes?.Invoke();
			}
		}
	}
}

// Typed text for a name: letters from the keyboard, or picked from an on-screen grid with the directions and the
// gamepad's Confirm button, so a gamepad alone can type. Backspace or the gamepad's Back erases, Enter or the
// gamepad's Menu button (or the grid's OK) confirms; Escape, or the gamepad's Back on an empty name, cancels.
public sealed class TextEntry(int maxLength)
{
	private const string Space = "Space";
	private const string Erase = "Del";
	private const string Done = "OK";

	private static readonly string[][] Grid =
	[
		[.. "ABCDEFGHIJKLM".Select(c => c.ToString())],
		[.. "NOPQRSTUVWXYZ".Select(c => c.ToString())],
		[.. "abcdefghijklm".Select(c => c.ToString())],
		[.. "nopqrstuvwxyz".Select(c => c.ToString())],
		[.. "0123456789-'.".Select(c => c.ToString())],
		[Space, Erase, Done],
	];

	private int _row;
	private int _col;

	public string Buffer { get; private set; } = "";

	public string Cell => Grid[_row][Math.Min(_col, Grid[_row].Length - 1)];

	public void Reset(string text = "")
	{
		Buffer = text;
		_row = 0;
		_col = 0;
	}

	private void Type(char c)
	{
		if (Buffer.Length < maxLength)
		{
			Buffer += c;
		}
	}

	// Null while typing; true to confirm, false to cancel. Typed letters start each word in upper case.
	public bool? Update(ActionInput input)
	{
		foreach (Keys k in input.Raw.PressedKeys)
		{
			if (k is >= Keys.A and <= Keys.Z)
			{
				char c = (char)('a' + (k - Keys.A));
				Type(Buffer.Length == 0 || Buffer[^1] == ' ' ? char.ToUpperInvariant(c) : c);
			}
			else if (k is >= Keys.D0 and <= Keys.D9)
			{
				Type((char)('0' + (k - Keys.D0)));
			}
			else if (k == Keys.Space)
			{
				Type(' ');
			}
		}
		if (input.Pressed(GameAction.Up))
		{
			_row = (_row + Grid.Length - 1) % Grid.Length;
		}
		if (input.Pressed(GameAction.Down))
		{
			_row = (_row + 1) % Grid.Length;
		}
		_col = Math.Min(_col, Grid[_row].Length - 1);
		if (input.Pressed(GameAction.Left))
		{
			_col = (_col + Grid[_row].Length - 1) % Grid[_row].Length;
		}
		if (input.Pressed(GameAction.Right))
		{
			_col = (_col + 1) % Grid[_row].Length;
		}
		if (input.Raw.Pressed(Keys.Escape) || (input.ButtonPressed(GameAction.Back) && Buffer.Length == 0))
		{
			return false;
		}
		if ((input.Raw.Pressed(Keys.Back) || input.ButtonPressed(GameAction.Back)) && Buffer.Length > 0)
		{
			Buffer = Buffer[..^1];
		}
		if (input.Raw.Pressed(Keys.Enter) || input.ButtonPressed(GameAction.Menu))
		{
			return true;
		}
		if (input.ButtonPressed(GameAction.Confirm))
		{
			switch (Cell)
			{
			case Done:
				return true;
			case Erase:
				Buffer = Buffer.Length > 0 ? Buffer[..^1] : Buffer;
				break;
			case Space:
				Type(' ');
				break;
			default:
				Type(Cell[0]);
				break;
			}
		}
		return null;
	}

	// The height the text box and the grid take.
	public static int Height(Func<int, int> px) => px(50) + px(14) + Grid.Length * px(36);

	// The text box and the grid under it, its last row's cells worded as `labels` says; returns the height used.
	public int Draw(IDrawSurface s, int x, int y, int w, int clockMs, Func<int, int> px, (string Space, string Erase, string Done) labels, int caretMs = 400)
	{
		int boxH = px(50);
		s.Fill(x, y, w, boxH, Style.Track);
		Style.Frame(s, x, y, w, boxH, Style.PanelEdge);
		string caret = clockMs / Math.Max(1, caretMs) % 2 == 0 ? "_" : " ";
		s.Text(Buffer + caret, x + px(16), y + px(8), Style.Text, Style.Title);
		int cellW = Math.Min(px(44), w / Grid[0].Length);
		int cellH = px(36);
		int gy = y + boxH + px(14);
		for (int r = 0; r < Grid.Length; r++)
		{
			int width = r == Grid.Length - 1 ? cellW * Grid[0].Length / Grid[r].Length : cellW;
			for (int c = 0; c < Grid[r].Length; c++)
			{
				bool on = r == _row && c == Math.Min(_col, Grid[r].Length - 1);
				int cx = x + c * width;
				int cy = gy + r * cellH;
				if (on)
				{
					s.Fill(cx + 1, cy + 1, width - 2, cellH - 2, Style.Row);
					Style.Frame(s, cx + 1, cy + 1, width - 2, cellH - 2, Style.Accent);
				}
				string label = Grid[r][c] switch { Space => labels.Space, Erase => labels.Erase, Done => labels.Done, string cell => cell };
				s.Text(label, cx + (width - s.Measure(label)) / 2, cy + (cellH - px(24)) / 2, on ? Style.Accent : Style.Text);
			}
		}
		return Height(px);
	}
}

public static class Widgets
{
	public static void ScrollBar(IDrawSurface s, int x, int y, int h, int top, int rows, int count)
	{
		s.Fill(x, y, 4, h, Style.Track);
		int thumb = Math.Max(12, h * rows / count);
		int at = (h - thumb) * top / Math.Max(1, count - rows);
		s.Fill(x, y + at, 4, thumb, Style.Accent);
	}

	// Tabs that do not all fit show a run around the selected one, with marks where more are hidden.
	public static void Tabs(IDrawSurface s, int x, int y, int w, IReadOnlyList<string> tabs, int selected, Func<int, int> px)
	{
		int Width(int i) => s.Measure(tabs[i]) + px(28);
		int first = 0;
		int last = tabs.Count - 1;
		int Span() => Enumerable.Range(first, last - first + 1).Sum(i => Width(i) + px(8)) + (first > 0 || last < tabs.Count - 1 ? px(48) : 0);
		while (Span() > w && last > first)
		{
			if (last > selected)
			{
				last--;
			}
			else
			{
				first++;
			}
		}
		int tx = x;
		if (first > 0 || last < tabs.Count - 1)
		{
			s.Text("<", tx, y + px(5), first > 0 ? Style.Accent : Style.Dim);
			tx += px(24);
		}
		int h = px(32);
		for (int i = first; i <= last; i++)
		{
			if (i == selected)
			{
				s.Fill(tx, y, Width(i), h, Style.Row);
				s.Fill(tx, y + h - 2, Width(i), 2, Style.Accent);
			}
			s.Text(tabs[i], tx + px(14), y + px(5), i == selected ? Style.Accent : Style.Dim);
			tx += Width(i) + px(8);
		}
		if (first > 0 || last < tabs.Count - 1)
		{
			s.Text(">", tx, y + px(5), last < tabs.Count - 1 ? Style.Accent : Style.Dim);
		}
	}

	// A label, the numbers at the right and a bar under them; returns the height used.
	public static int ValueBar(IDrawSurface s, int x, int y, int w, string label, int value, int max, Color color, int gap = 22, int barH = 10)
	{
		string numbers = $"{value}/{max}";
		s.Text(label, x, y, Style.Dim, Style.Small);
		s.Text(numbers, x + w - s.Measure(numbers, Style.Small), y, Style.Text, Style.Small);
		Style.Bar(s, x, y + gap, w, barH, value, max, color);
		return gap + barH;
	}

	// The keys that work here, dim and cut to the width.
	public static void Hints(IDrawSurface s, int x, int y, int w, string hints) => s.Text(Style.Fit(s, hints, w, Style.Small), x, y, Style.Dim, Style.Small);

	// Rows of "label  before -> after", the after value green when it went up and red when it went down.
	public static void Compare(IDrawSurface s, int x, int y, int w, int rowH, IReadOnlyList<(string Label, int Before, int After)> rows)
	{
		int beforeX = x + w * 55 / 100;
		int arrowX = x + w * 70 / 100;
		int afterX = x + w * 82 / 100;
		for (int i = 0; i < rows.Count; i++)
		{
			(string label, int before, int after) = rows[i];
			int ry = y + i * rowH;
			s.Text(label, x, ry, Style.Text, Style.Small);
			s.Text(before.ToString(System.Globalization.CultureInfo.InvariantCulture), beforeX, ry, Style.Dim, Style.Small);
			s.Text("->", arrowX, ry, Style.Dim, Style.Small);
			s.Text(after.ToString(System.Globalization.CultureInfo.InvariantCulture), afterX, ry, after > before ? Style.Good : after < before ? Style.Bad : Style.Text, Style.Small);
		}
	}
}
