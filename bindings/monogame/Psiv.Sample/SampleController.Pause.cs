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
		string[] rows = PauseRows;
		Ui.Draw("pause", s, t, SetHud(UiData(), !_battle.Active)
			.Set("scene", (UiSlot)((_, _, _, _) =>
			{
				if (_battle.Active)
				{
					DrawBattle(s, art);
				}
				else
				{
					DrawField(s, art);
				}
			}))
			.Set("rows", rows.ToList())
			.Set("cursor", _pauseList.Cursor)
			.Set("hint", T("ui.pause.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back))));
		_probeSelected = rows[_pauseList.Cursor];
	}
}
