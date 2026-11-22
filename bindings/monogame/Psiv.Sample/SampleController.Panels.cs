using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// A scene's comic panels, its camera and its shakes. A panel is a framed window over the dimmed field, in a layout
// slot, showing the live field scene through a named camera: tiles, actors, the party, poses and walks all keep
// playing inside it. A camera looks where game.json says (a marker of the party's area, a person it follows or the
// leader) at its zoom; a camera the data gives no target, or whose target is not in view, is the marker of the same
// name in the party's area, else the person (actor or party member) with that id, else the leader. The camera a
// `camera cut` or `camera move` names is the scene's camera: the field outside the panels centers on it, and a panel
// showing it pushes in by a third, gliding over the move's seconds. Transitions last the action's duration from the
// data. All of it lasts until the event ends.
public sealed partial class SampleController
{
	private const string DefaultSlot = "center";

	private sealed class Panel(string id, string camera, string slot)
	{
		public string Id { get; } = id;
		public string Camera { get; set; } = camera;
		public string Slot { get; set; } = slot;
		// The show or hide playing on it; with none it is fully shown.
		public Animation? Transition { get; set; }
		public bool Hiding { get; set; }
		public float Shown => Transition is null ? 1f : Hiding ? 1f - Transition.Progress : Transition.Progress;
	}

	private readonly List<Panel> _panels = [];
	private Dictionary<string, Camera>? _cameraData;
	private readonly HashSet<string> _unknownSlots = [];
	// The scene's camera, null while the field follows the leader, and its glide from where the field was centered.
	private string? _camera;
	private (float X, float Y) _cameraFrom;
	private Animation? _cameraMove;
	private Animation? _shake;

	// The panels file's param, else `fallback`.
	private int PanelParam(string name, int fallback) => Ui.Param("panels", name, UiData(), fallback);

	// Every slot in the screen's own pixels, inside the margins, with gutters between neighbors; null for a name that
	// is not a slot. Slots tile the screen above the band `bottom` leaves free for the dialogue box (by default its
	// height at 100% text size, its margin and a gutter), so lines read below the panels as captions do.
	private Rectangle? SlotRect(string slot, int width, int height)
	{
		int margin = PanelParam("margin", 24), gutter = PanelParam("gutter", 16);
		int bottom = PanelParam("bottom", DialogueParam("box_h", DialogueBoxHeight) + DialogueParam("box_margin", 16) + gutter);
		int x = margin, y = margin, w = width - 2 * margin, h = height - margin - bottom;
		int halfW = (w - gutter) / 2, halfH = (h - gutter) / 2;
		int thirdW = (w - 2 * gutter) / 3, thirdH = (h - 2 * gutter) / 3;
		return slot switch
		{
			"full" => new(x, y, w, h),
			"left" => new(x, y, halfW, h),
			"right" => new(x + w - halfW, y, halfW, h),
			"top" => new(x, y, w, halfH),
			"bottom" => new(x, y + h - halfH, w, halfH),
			"top_left" => new(x, y, halfW, halfH),
			"top_right" => new(x + w - halfW, y, halfW, halfH),
			"bottom_left" => new(x, y + h - halfH, halfW, halfH),
			"bottom_right" => new(x + w - halfW, y + h - halfH, halfW, halfH),
			"wide_top" => new(x, y, w, thirdH),
			"wide_bottom" => new(x, y + h - thirdH, w, thirdH),
			"tall_left" => new(x, y, thirdW, h),
			"tall_right" => new(x + w - thirdW, y, thirdW, h),
			"center" => new(x + w / 5, y + h / 8, w * 3 / 5, h * 3 / 4),
			_ => null,
		};
	}

	private string KnownSlot(string slot)
	{
		// Only the name decides whether there is a rectangle.
		if (SlotRect(slot, 0, 0) is not null)
		{
			return slot;
		}
		if (_unknownSlots.Add(slot))
		{
			_log($"panel: no layout slot '{slot}', using {DefaultSlot}");
		}
		return DefaultSlot;
	}

	// Panel, camera and shake actions. Returns the milliseconds the event waits, or 0 when the action is done at once.
	private int PresentScene(PresentationAction a)
	{
		switch (a.Type)
		{
		case ActionType.PanelShow:
			_panelShows++;
			Panel? panel = _panels.FirstOrDefault(p => p.Id == a.Subject);
			if (panel is null)
			{
				panel = new Panel(a.Subject, a.Target, KnownSlot(a.Slot));
				_panels.Add(panel);
			}
			else
			{
				panel.Camera = a.Target;
				panel.Slot = KnownSlot(a.Slot);
			}
			panel.Hiding = false;
			panel.Transition = null;
			return a.Instant ? 0 : TransitionMs(a);
		case ActionType.PanelHide:
			if (_panels.FirstOrDefault(p => p.Id == a.Subject) is not { } hidden)
			{
				return 0;
			}
			if (a.Instant)
			{
				_panels.Remove(hidden);
				return 0;
			}
			hidden.Hiding = true;
			return TransitionMs(a);
		case ActionType.PanelClear:
			if (a.Instant || _panels.Count == 0)
			{
				_panels.Clear();
				return 0;
			}
			foreach (Panel p in _panels)
			{
				p.Hiding = true;
			}
			return TransitionMs(a);
		case ActionType.CameraCut:
		case ActionType.CameraMove:
			_cameraFrom = CameraFocus(_s.Field());
			_camera = a.Subject;
			_cameraMove = null;
			return a.Type == ActionType.CameraMove && !a.Instant ? Math.Max(1, (int)(a.Duration * 1000)) : 0;
		case ActionType.Shake:
			return a.Instant ? 0 : Math.Max(1, (int)(a.Seconds * 1000));
		// The event waits out the move's seconds while SceneFocus follows the session's camera.
		case ActionType.CameraTo:
		case ActionType.CameraReset:
			_sceneJumpPending |= a.Instant;
			return a.Instant ? 0 : (int)((a.Duration > 0 ? a.Duration : a.Seconds) * 1000);
		}
		return 0;
	}

	private int TransitionMs(PresentationAction a) => a.Duration > 0 ? (int)(a.Duration * 1000) : Timing("panel_transition_ms");

	// The scene action's animation has started: the panels it shows or hides, the camera glide or the shake follow it.
	private void FollowSceneAction(Animation animation)
	{
		PresentationAction a = animation.Action;
		switch (a.Type)
		{
		case ActionType.PanelShow:
		case ActionType.PanelHide:
			if (_panels.FirstOrDefault(p => p.Id == a.Subject) is { } panel)
			{
				panel.Transition = animation;
			}
			break;
		case ActionType.PanelClear:
			foreach (Panel p in _panels.Where(p => p.Hiding))
			{
				p.Transition = animation;
			}
			break;
		case ActionType.CameraMove:
			_cameraMove = animation;
			break;
		case ActionType.Shake:
			_shake = animation;
			break;
		}
	}

	// The end state of a scene action that finished, or that a skip cancelled before the rebuild puts it up again.
	private void EndSceneAction(PresentationAction a)
	{
		_panels.RemoveAll(p => p.Transition?.Action.Handle == a.Handle && p.Hiding);
		foreach (Panel p in _panels.Where(p => p.Transition?.Action.Handle == a.Handle))
		{
			p.Transition = null;
		}
		if (_cameraMove?.Action.Handle == a.Handle)
		{
			_cameraMove = null;
		}
		if (_shake?.Action.Handle == a.Handle)
		{
			_shake = null;
		}
	}

	private static float Ease(float t) => t * t * (3f - 2f * t);

	private Camera? CameraData(string camera) => (_cameraData ??= _s.Cameras().ToDictionary(c => c.Id)).GetValueOrDefault(camera);

	// Where a camera looks, in cells: its target from the data when that is in view; otherwise the marker of its name,
	// else the person of its name, else the leader.
	private (float X, float Y) Resolve(string camera, FieldView view)
	{
		Camera? data = CameraData(camera);
		if (data?.Target == CameraTarget.Marker && data.Area == view.Info.AreaId && view.Markers.FirstOrDefault(m => m.Name == data.Marker) is { } target)
		{
			return (target.X, target.Y);
		}
		if (data?.Target == CameraTarget.Follow && _field.DrawnActors.TryGetValue(data.Follow, out DrawnPerson followed))
		{
			return (followed.X, followed.Y);
		}
		if (data?.Target == CameraTarget.Leader)
		{
			return _field.Leader(Lineup());
		}
		if (view.Markers.FirstOrDefault(m => m.Name == camera) is { } marker)
		{
			return (marker.X, marker.Y);
		}
		if (_field.DrawnActors.TryGetValue(camera, out DrawnPerson person))
		{
			return (person.X, person.Y);
		}
		return _field.Leader(Lineup());
	}

	// Where the field outside the panels is centered: the scene's camera, gliding there during a move.
	private (float X, float Y) CameraFocus(FieldView? view)
	{
		if (view is null || _camera is null)
		{
			return _field.Leader(Lineup());
		}
		(float x, float y) = Resolve(_camera, view);
		if (_cameraMove is null)
		{
			return (x, y);
		}
		float t = Ease(_cameraMove.Progress);
		return (_cameraFrom.X + (x - _cameraFrom.X) * t, _cameraFrom.Y + (y - _cameraFrom.Y) * t);
	}

	// The session's scene camera (`camera to`, `camera reset`): the state last seen, where the ease started, where the field
	// was last centered, and the state an instant action jumped to. The session runs the camera down on its ticks, so
	// between ticks the ease goes on by frame time, at most a tick ahead of it.
	private (bool Held, int X, int Y, int TotalMs) _sceneKey;
	private (float X, float Y) _sceneFrom;
	private (float X, float Y)? _lastFocus;
	private (bool Held, int X, int Y, int TotalMs)? _sceneJump;
	private bool _sceneJumpPending;
	private int _sceneMs = -1;
	private int _sceneMsClock;

	// Where the scene camera centers the field: easing from where the view was to its cell over its time while held,
	// and back to the leader over its time once reset; null when it is not held and not on its way back.
	private (float X, float Y)? SceneFocus(List<string> lineup)
	{
		SceneCamera c = _s.SceneCamera();
		var key = (c.Held, c.X, c.Y, c.TotalMs);
		(float X, float Y) leader = _field.Leader(lineup);
		if (key != _sceneKey)
		{
			_sceneFrom = _lastFocus ?? leader;
			_sceneKey = key;
		}
		if (_sceneJumpPending)
		{
			_sceneJump = key;
			_sceneJumpPending = false;
		}
		if (c.Ms != _sceneMs)
		{
			_sceneMs = c.Ms;
			_sceneMsClock = _clockMs;
		}
		int left = Math.Max(0, c.Ms - Math.Min(_clockMs - _sceneMsClock, (_gameInfo ??= _s.GameInfo()).TickMs));
		float t = c.TotalMs > 0 && left > 0 && _sceneJump != key ? Ease(1f - left / (float)c.TotalMs) : 1f;
		if (!c.Held && t >= 1f)
		{
			return null;
		}
		(float X, float Y) to = c.Held ? (c.X, c.Y) : leader;
		return (_sceneFrom.X + (to.X - _sceneFrom.X) * t, _sceneFrom.Y + (to.Y - _sceneFrom.Y) * t);
	}

	// The scene camera as the `scenecamera` probe reads it: `held X Y` or `leader`, then `MS/TOTAL` while it moves.
	private string SceneCameraProbe()
	{
		SceneCamera c = _s.SceneCamera();
		return (c.Held ? $"held {c.X} {c.Y}" : "leader") + (c.Ms > 0 ? $" {c.Ms}/{c.TotalMs}" : "");
	}

	// A panel's cell size: the field's, times its camera's zoom, and a third more while that camera is the scene's.
	private int PanelTileSize(Panel p)
	{
		float zoom = (float)(CameraData(p.Camera)?.Zoom ?? 1.0);
		if (p.Camera == _camera)
		{
			zoom *= _cameraMove is null ? 4f / 3f : 1f + Ease(_cameraMove.Progress) / 3f;
		}
		return Math.Max(4, (int)MathF.Round(Layout.Styles.Param("field_cell") * zoom));
	}

	private int ShakeAmplitude()
	{
		if (_shake is null)
		{
			return FxShake();
		}
		// The styles' `field_shake`: pixels a shake of strength 1 moves the view; the profile's reduced flash damps it to a quarter.
		float pixels = Layout.Styles.Param("field_shake") * (float)Math.Max(0.0, _shake.Action.Strength) * (1f - _shake.Progress);
		return (int)MathF.Round(_s.Profile().ReducedFlash ? pixels / 4 : pixels);
	}

	private Point ShakeOffset()
	{
		int a = ShakeAmplitude();
		return a == 0 ? Point.Zero : new Point((int)MathF.Round(a * MathF.Sin(_clockMs * 0.11f)), (int)MathF.Round(a * MathF.Sin(_clockMs * 0.17f + 1f)));
	}

	// Where a panel sits as it slides in from the nearest edge of the screen, or out to it.
	private Rectangle PanelRect(Panel p, int width, int height)
	{
		Rectangle r = SlotRect(p.Slot, width, height)!.Value;
		float off = (1f - Ease(p.Shown)) * PanelParam("slide", 64);
		int left = r.Left, right = width - r.Right, top = r.Top, bottom = height - r.Bottom;
		int nearest = Math.Min(Math.Min(left, right), Math.Min(top, bottom));
		Point d = nearest == left ? new(-1, 0) : nearest == right ? new(1, 0) : nearest == top ? new(0, -1) : new(0, 1);
		r.Offset((int)(d.X * off), (int)(d.Y * off));
		return r;
	}

	// The panels over the field, dimmed behind them as they come in, each the live scene through its camera.
	private void DrawPanels(IDrawSurface s, Art art, FieldView view)
	{
		if (_panels.Count == 0)
		{
			return;
		}
		List<string> lineup = Lineup();
		Point shake = ShakeOffset();
		UiContext ctx = UiData().Set("dim", 0.45f * _panels.Max(p => p.Shown));
		ctx.Set("panels", _panels.Select(p =>
		{
			float shown = p.Shown;
			Rectangle r = PanelRect(p, s.Width, s.Height);
			NineSliceImage? frame = Style.Skin?.CameraFrames.GetValueOrDefault(p.Camera) ?? Style.Skin?.PanelFrame;
			return new UiContext(ctx)
				.Set("left", r.X)
				.Set("top", r.Y)
				.Set("width", r.Width)
				.Set("height", r.Height)
				.Set("shown", shown)
				.Set("shadow_alpha", 0.7f * shown)
				.Set("skinned", frame is not null)
				.Set("frame", (UiSlot)((x, y, w, h) => Style.NineSlice(s, frame!, x, y, w, h, Color.White * shown)))
				// The live scene clipped to the panel, darkened while it comes in or goes.
				.Set("scene", (UiSlot)((x, y, w, h) =>
				{
					int tile = PanelTileSize(p);
					s.Clip(x, y, w, h);
					_field.DrawView(s, art, view, _clockMs, lineup, new Rectangle(x, y, w, h), Resolve(p.Camera, view), tile, shake);
					if (shown < 1f)
					{
						s.Fill(x, y, w, h, Color.Black * (1f - shown));
					}
					s.Unclip();
				}));
		}).ToList());
		Ui.Draw("panels", s, s, ctx);
	}

	// The dialogue box goes where it covers the fewest panels, then the least of them; a tie keeps it at the bottom,
	// where it covers none unless a larger text size makes it taller.
	private bool DialogueOnTop(IDrawSurface s, int bottomY, int topY)
	{
		(int Count, long Area) Covered(int y)
		{
			var box = new Rectangle(24, y, s.Width - 48, DialogueHeight);
			int count = 0;
			long area = 0;
			foreach (Panel p in _panels.Where(p => !p.Hiding))
			{
				Rectangle overlap = Rectangle.Intersect(box, SlotRect(p.Slot, s.Width, s.Height)!.Value);
				if (!overlap.IsEmpty)
				{
					count++;
					area += (long)overlap.Width * overlap.Height;
				}
			}
			return (count, area);
		}
		return Covered(topY).CompareTo(Covered(bottomY)) < 0;
	}
}
