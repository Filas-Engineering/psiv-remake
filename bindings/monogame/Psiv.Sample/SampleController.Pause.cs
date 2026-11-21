using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The pause overlay over the field or a battle's command menus. The session gets no updates meanwhile, so nothing
// moves and play time stops.
public sealed partial class SampleController
{
	private string[] PauseRows => [T("ui.pause.resume"), T("ui.pause.options"), T("ui.pause.quit")];

	private readonly ScrollList _pauseList = new();

	private void OpenPause()
	{
		_pause = true;
		_pauseList.Reset();
	}

	private void UpdatePause(ActionInput input)
	{
		_pauseList.Move(input, PauseRows.Length);
		if (input.Back || input.Pressed(GameAction.Pause) || (input.Confirm && _pauseList.Cursor == 0))
		{
			_pause = false;
		}
		else if (input.Confirm && _pauseList.Cursor == 1)
		{
			OpenOptions();
		}
		else if (input.Confirm)
		{
			_confirm.Ask(T("ui.pause.quit_confirm"), () => Report(_s.QuitToTitle()));
		}
	}

	private void DrawPause(IDrawSurface s, IDrawSurface t, Art art)
	{
		if (_battle.Active)
		{
			DrawBattle(s, art);
		}
		else
		{
			DrawField(s, art);
			DrawHud(s);
		}
		s.Fill(0, 0, s.Width, s.Height, Color.Black * 0.55f);
		int rowH = Px(32);
		int w = Px(360);
		int h = Px(40) + PauseRows.Length * rowH + Px(20);
		int x = (s.Width - w) / 2;
		int y = (s.Height - h) / 2;
		Style.Window(t, x, y, w, h, T("ui.pause.title"));
		string[] rows = PauseRows;
		for (int i = 0; i < rows.Length; i++)
		{
			Style.ListRow(t, rows[i], x + Px(40), y + Px(34) + i * rowH, w - Px(70), i == _pauseList.Cursor, height: Px(28));
		}
		_probeSelected = rows[_pauseList.Cursor];
		DrawHint(t, T("ui.pause.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back)));
	}
}
