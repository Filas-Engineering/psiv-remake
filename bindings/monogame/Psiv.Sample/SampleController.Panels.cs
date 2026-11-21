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
	private const int PanelTransitionMs = 350;
	private const int PanelSlide = 64;
	private const int PanelMargin = 24;
	private const int PanelGutter = 16;
	private const int PanelBorder = 4;
	private const int PanelTile = Art.Tile;
	// Pixels a shake of strength 1 moves the view; the profile's reduced flash damps it to a quarter.
	private const int ShakePixels = 10;
	private const string DefaultSlot = "center";
	// The dialogue box at 100% text size with its margin and a gutter: the band at the bottom the slots leave free.
	private const int DialogueSpace = DialogueBoxHeight + 16 + PanelGutter;

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

	// Every slot in the screen's own pixels, inside the margins, with gutters between neighbors; null for a name that
	// is not a slot. Slots tile the screen above the dialogue box's place at the bottom, so lines read below the panels
	// as captions do.
	private static Rectangle? SlotRect(string slot, int width, int height)
	{
		int x = PanelMargin, y = PanelMargin, w = width - 2 * PanelMargin, h = height - PanelMargin - DialogueSpace;
		int halfW = (w - PanelGutter) / 2, halfH = (h - PanelGutter) / 2;
		int thirdW = (w - 2 * PanelGutter) / 3, thirdH = (h - 2 * PanelGutter) / 3;
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
		}
		return 0;
	}

	private static int TransitionMs(PresentationAction a) => a.Duration > 0 ? (int)(a.Duration * 1000) : PanelTransitionMs;

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

	// A panel's cell size: the field's, times its camera's zoom, and a third more while that camera is the scene's.
	private int PanelTileSize(Panel p)
	{
		float zoom = (float)(CameraData(p.Camera)?.Zoom ?? 1.0);
		if (p.Camera == _camera)
		{
			zoom *= _cameraMove is null ? 4f / 3f : 1f + Ease(_cameraMove.Progress) / 3f;
		}
		return Math.Max(4, (int)MathF.Round(PanelTile * zoom));
	}

	private int ShakeAmplitude()
	{
		if (_shake is null)
		{
			return 0;
		}
		float pixels = ShakePixels * (float)Math.Max(0.0, _shake.Action.Strength) * (1f - _shake.Progress);
		return (int)MathF.Round(_s.Profile().ReducedFlash ? pixels / 4 : pixels);
	}

	private Point ShakeOffset()
	{
		int a = ShakeAmplitude();
		return a == 0 ? Point.Zero : new Point((int)MathF.Round(a * MathF.Sin(_clockMs * 0.11f)), (int)MathF.Round(a * MathF.Sin(_clockMs * 0.17f + 1f)));
	}

	// Where a panel sits as it slides in from the nearest edge of the screen, or out to it.
	private static Rectangle PanelRect(Panel p, int width, int height)
	{
		Rectangle r = SlotRect(p.Slot, width, height)!.Value;
		float off = (1f - Ease(p.Shown)) * PanelSlide;
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
		s.Fill(0, 0, s.Width, s.Height, Color.Black * (0.45f * _panels.Max(p => p.Shown)));
		Point shake = ShakeOffset();
		foreach (Panel p in _panels)
		{
			float shown = p.Shown;
			Rectangle r = PanelRect(p, s.Width, s.Height);
			s.Fill(r.X + 6, r.Y + 6, r.Width, r.Height, Style.PanelShadow * (0.7f * shown));
			s.Fill(r.X - PanelBorder, r.Y - PanelBorder, r.Width + 2 * PanelBorder, r.Height + 2 * PanelBorder, Color.Black * shown);
			s.Fill(r.X - PanelBorder + 1, r.Y - PanelBorder + 1, r.Width + 2 * PanelBorder - 2, r.Height + 2 * PanelBorder - 2, Style.Text * shown);
			s.Fill(r.X, r.Y, r.Width, r.Height, Style.Background);
			int tile = PanelTileSize(p);
			s.Clip(r.X, r.Y, r.Width, r.Height);
			_field.DrawView(s, art, view, _clockMs, lineup, r, Resolve(p.Camera, view), tile, shake);
			if (shown < 1f)
			{
				s.Fill(r.X, r.Y, r.Width, r.Height, Color.Black * (1f - shown));
			}
			s.Unclip();
		}
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
