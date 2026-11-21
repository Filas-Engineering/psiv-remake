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
	// field, ends it. A click on a window the layout marks `blocks_clicks` does not walk.
	private bool UpdateClickWalk(int dtMs, ActionInput input)
	{
		if (input.Raw.Click is { } click && !Ui.Blocks(click.X, click.Y) && _s.FieldInfo() is { PlayerControl: true } field)
		{
			(int x, int y) = CellAt(click.X, click.Y);
			PsivResult started = _s.WalkTo(x, y);
			Report(started);
			_clickWalk = started.Ok ? (x, y) : null;
			// The first step goes at once.
			_clickWalkMs = field.LeaderStepMs;
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
		int stepMs = _s.FieldInfo()?.LeaderStepMs ?? 250;
		while (_clickWalk is not null && _clickWalkMs >= stepMs)
		{
			_clickWalkMs -= stepMs;
			WalkStatus status = _s.WalkStep();
			if (status != WalkStatus.Walking)
			{
				if (status == WalkStatus.NoPath)
				{
					Say(T("ui.field.no_way"));
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
			// Aboard, Confirm steps off; on foot it talks, opens, boards the vehicle ahead or opens a port.
			Report(_s.FieldInfo() is { Aboard.Length: > 0 } ? _s.LeaveVehicle() : _s.Interact());
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
		string follower = _s.Follower();
		if (follower.Length > 0)
		{
			lineup.Add(follower);
		}
		_field.Track(view, _clockMs, lineup);
		_field.Draw(s, art, view, _clockMs, lineup, _camera is null ? null : CameraFocus(view), ShakeOffset());
		if (_clickWalk is { } goal)
		{
			(int gx, int gy) = CellCenter(goal.X, goal.Y);
			int r = Art.Tile / 3 + (int)(3 * MathF.Sin(_clockMs / 120f));
			Style.Frame(s, gx - r, gy - r, 2 * r, 2 * r, Style.Accent);
		}
		if (_dev)
		{
			DrawFieldOverlays(s, view);
		}
		DrawPanels(s, art, view);
		return true;
	}

	// The field: the map, then the HUD, the area banner, the toasts and the keys' hint over it.
	private void DrawFieldScreen(IDrawSurface s, IDrawSurface t, Art art)
	{
		DrawField(s, art);
		bool aboard = _s.FieldInfo()?.Aboard.Length > 0;
		UiContext ctx = SetBanner(SetHud(UiData()), true);
		Ui.Draw("field", s, t, ctx
			.Set("toasts", ToastRows(s, 16).Select(r => new UiContext(ctx).Set("text", r.Shown).Set("alpha", r.Alpha).Set("box_alpha", r.Alpha * 0.95f).Set("width", r.BoxW)).ToList())
			.Set("hint", T("ui.hint.field", Walk(), T(aboard ? "ui.hint.drive" : "ui.hint.walk"), Hint(GameAction.Confirm), T(aboard ? "ui.hint.step_off" : "ui.hint.talk"),
				Hint(GameAction.Menu), Hint(GameAction.QuestLog), Hint(GameAction.PartyTalk), Hint(GameAction.TextLog), Hint(GameAction.Pause))));
	}

	// The HUD's fields: the area's name and the money and play time, for the field and the pause over it.
	private UiContext SetHud(UiContext ctx, bool visible = true)
	{
		if (!visible || _s.FieldInfo() is not { } f)
		{
			return ctx.Set("hud", false).Set("area", "").Set("status", "");
		}
		WorldInfo w = _s.WorldInfo();
		long seconds = w.PlayTimeMs / 1000;
		return ctx.Set("hud", true).Set("area", f.AreaName).Set("status", $"{T("ui.common.money", w.Money)}    {seconds / 3600}:{seconds / 60 % 60:00}:{seconds % 60:00}");
	}

	// The area banner's fields: shown for a moment after arriving, fading in over 300 ms and out over the last 500 ms
	// while it settles from 16 pixels higher.
	private UiContext SetBanner(UiContext ctx, bool visible)
	{
		int left = _field.BannerLeftMs;
		int shown = FieldRenderer.BannerMs - left;
		float alpha = Math.Clamp(Math.Min(shown / 300f, left / 500f), 0f, 1f);
		visible &= left > 0;
		return ctx.Set("banner_visible", visible)
			.Set("banner_title", _field.BannerTitle)
			.Set("banner_kind", visible ? AreaKindText(_field.BannerKind) : "")
			.Set("banner_alpha", alpha)
			.Set("banner_drop", (int)((1f - alpha) * 16))
			.Set("banner_ms", shown)
			.Set("banner_left_ms", left);
	}
}
