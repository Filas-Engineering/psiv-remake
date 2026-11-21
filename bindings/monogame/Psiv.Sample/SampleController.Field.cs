using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	// The cell a click asked the leader to walk to, and the frame time gathered toward its next step. While it lasts the
	// session's path walk moves the leader a cell per area step, and no held direction reaches the session.
	private (int X, int Y)? _clickWalk;
	private int _clickWalkMs;

	// A screen pixel of the field view as a cell, by where the last frame drew the map.
	private (int X, int Y) CellAt(int x, int y) =>
		((int)Math.Floor((x + _field.CameraX) / (double)Art.Tile), (int)Math.Floor((y + _field.CameraY) / (double)Art.Tile));

	// The screen pixel at the center of a cell as the last frame drew it, for scripts that click a cell.
	public (int X, int Y) CellCenter(int x, int y) => (x * Art.Tile - _field.CameraX + Art.Tile / 2, y * Art.Tile - _field.CameraY + Art.Tile / 2);

	// Click-to-walk through the session's WalkTo and WalkStep: a step each time the area's step time has passed, so the
	// leader keeps its walking pace and game time runs as it does under a held direction. Any action key, or leaving the
	// field, ends it.
	private bool UpdateClickWalk(int dtMs, ActionInput input)
	{
		if (input.Raw.Click is { } click && _s.FieldInfo() is { PlayerControl: true } field)
		{
			(int x, int y) = CellAt(click.X, click.Y);
			PsivResult started = _s.WalkTo(x, y);
			Report(started);
			_clickWalk = started.Ok ? (x, y) : null;
			// The first step goes at once.
			_clickWalkMs = field.StepMs;
		}
		if (_clickWalk is null)
		{
			return false;
		}
		if (input.Held != Direction.None || input.Confirm || input.Back || input.Pressed(GameAction.Menu))
		{
			_clickWalk = null;
			return false;
		}
		int stepMs = _s.FieldInfo()?.StepMs ?? 250;
		while (_clickWalk is not null && _clickWalkMs >= stepMs)
		{
			_clickWalkMs -= stepMs;
			WalkStatus status = _s.WalkStep();
			if (status != WalkStatus.Walking)
			{
				if (status == WalkStatus.NoPath)
				{
					Say("There is no way there.");
				}
				_clickWalk = null;
			}
			if (_s.Mode != Mode.Field)
			{
				_clickWalk = null;
			}
		}
		_clickWalkMs += dtMs;
		return true;
	}

	private void UpdateField(int dtMs, ActionInput input)
	{
		if (input.Pressed(GameAction.Pause))
		{
			OpenPause();
			return;
		}
		if (UpdateClickWalk(dtMs, input))
		{
			return;
		}
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
		List<string> lineup = Lineup();
		_field.Track(view, _clockMs, lineup);
		_field.Draw(s, art, view, _clockMs, lineup, _camera is null ? null : CameraFocus(view), ShakeOffset());
		if (_clickWalk is { } goal)
		{
			(int gx, int gy) = CellCenter(goal.X, goal.Y);
			int r = Art.Tile / 3 + (int)(3 * MathF.Sin(_clockMs / 120f));
			Style.Frame(s, gx - r, gy - r, 2 * r, 2 * r, Style.Accent);
		}
		DrawPanels(s, art, view);
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
