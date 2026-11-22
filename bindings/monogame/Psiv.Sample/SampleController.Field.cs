using Microsoft.Xna.Framework;

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
		((int)Math.Floor((x + _field.CameraX) / (double)_field.Cell), (int)Math.Floor((y + _field.CameraY) / (double)_field.Cell));

	// The screen pixel at the center of a cell as the last frame drew it, for scripts that click a cell.
	public (int X, int Y) CellCenter(int x, int y) => (x * _field.Cell - _field.CameraX + _field.Cell / 2, y * _field.Cell - _field.CameraY + _field.Cell / 2);

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

	// The real-time field input (the window's own when no script plays it, `fieldinput realtime` in scripts): walking
	// led by the sample, a run, forgiving interaction and the interaction prompt. Off, the field plays as it always has:
	// the held direction goes to the session as it is, so scripts' `press` and `hold` keep their meaning.
	public bool RealtimeField
	{
		get => _realtimeField;
		set
		{
			_realtimeField = value;
			EndWalk();
		}
	}
	private bool _realtimeField;

	// The step the leader is drawn taking: its direction and cell, the time gone and its length, and whether the session
	// has put the leader there yet. A step is only drawn ahead of the session, so the session walks every cell itself.
	private sealed class WalkStep(Direction direction, int x, int y, int elapsedMs, int lengthMs, int percent, bool led)
	{
		public Direction Direction { get; } = direction;
		// The session's pace for the step: 100, or the run's speed.
		public int Percent { get; } = percent;
		public int X { get; } = x;
		public int Y { get; } = y;
		public int ElapsedMs { get; set; } = elapsedMs;
		public int LengthMs { get; } = lengthMs;
		// False for a conveyor's move, which the session made before the sample saw it.
		public bool Led { get; } = led;
		public bool Arrived { get; set; } = !led;
	}

	private WalkStep? _walk;
	// The directions held, the latest pressed last, and the one pressed during the current step, which goes next.
	private readonly List<Direction> _heldDirections = [];
	private Direction _bufferedDirection = Direction.None;
	private bool _interactQueued;
	// The session's milliseconds owed to a running step, past the whole ones it was given.
	private int _runCarry;
	// With game.json's run_speed_percent the session runs the leader itself: the run input goes to it with every update
	// (`Update(dt, held, running)`), the step times, the step mask and the belt's progress come from it, and the profile
	// keeps Always run, so running changes neither play time nor visible enemies. Without it the sample keeps its own run
	// (the layout's run_speed_percent timing), which gives the session its milliseconds faster.
	private bool SessionRun => _data.RunSpeedPercent > 0;
	private bool _running;
	// The target the interaction prompt points at, by the last update; null when none.
	private InteractTarget? _promptTarget;

	private void EndWalk()
	{
		_walk = null;
		_field.LeaderStep = null;
		_bufferedDirection = Direction.None;
		_interactQueued = false;
		_heldDirections.Clear();
		_promptTarget = null;
	}

	private static readonly (GameAction Action, Direction Direction)[] DirectionActions =
		[(GameAction.Up, Direction.North), (GameAction.Right, Direction.East), (GameAction.Down, Direction.South), (GameAction.Left, Direction.West)];

	private static (int X, int Y) StepFrom(int x, int y, Direction d) => d switch
	{
		Direction.North => (x, y - 1),
		Direction.East => (x + 1, y),
		Direction.South => (x, y + 1),
		_ => (x - 1, y),
	};

	// Whether the session will take the leader onto the cell: the map's walkable cells on foot, what the vehicle crosses
	// aboard, never a parked vehicle or a visible enemy (walking into one is the session's to answer: a battle or a turn).
	private bool Enterable(FieldView view, int x, int y)
	{
		FieldInfo f = view.Info;
		if (view.Vehicles.Any(v => !v.Aboard && v.X == x && v.Y == y) || view.Enemies.Any(e => e.X == x && e.Y == y))
		{
			return false;
		}
		if (f.Aboard.Length == 0)
		{
			return view.Cell(x, y).HasFlag(CellFlags.Walkable);
		}
		CellFlags cell = view.Cell(x, y);
		string terrain = cell.HasFlag(CellFlags.Barrier) ? "barrier" : (cell & CellFlags.TerrainMask) switch
		{
			CellFlags.Floor => "floor",
			CellFlags.Water => "water",
			CellFlags.Soft => "soft",
			_ => "wall",
		};
		return x >= 0 && y >= 0 && x < f.Width && y < f.Height && _data.VehicleCrosses.GetValueOrDefault(f.Aboard)?.Contains(terrain) == true &&
			!view.Points.Any(p => p.X == x && p.Y == y && p.Kind is PointKind.Talk or PointKind.Chest or PointKind.Shop or PointKind.Rest or PointKind.Port or PointKind.Helm);
	}

	// The real-time walk for one frame, which gives the session the frame's time itself. A step starts the moment a
	// direction is wanted and its cell can be entered, and is drawn from then over the session's step time; the session
	// is held that way on the same clock, so it puts the leader there by the time the drawn step ends, and gets no
	// direction after it arrives. The frame is split where a step ends, so the next one starts at once, on the same
	// clock: the pace is the session's own and the glide never waits. A direction pressed during a step goes next, even
	// after it is let go; otherwise the latest pressed of those held goes on. A running step gives the session its
	// milliseconds faster, so the session walks a cell per its own step time while the drawn step is shorter.
	private void RealtimeWalk(int dtMs, ActionInput input)
	{
		_heldDirections.RemoveAll(d => !input.Down(DirectionActions.First(x => x.Direction == d).Action));
		foreach ((GameAction action, Direction direction) in DirectionActions.Where(x => input.Down(x.Action) && !_heldDirections.Contains(x.Direction)))
		{
			_heldDirections.Add(direction);
			if (_walk is not null)
			{
				_bufferedDirection = direction;
			}
		}
		if (input.Pressed(GameAction.RunToggle))
		{
			ToggleAlwaysRun();
		}
		_running = input.Down(GameAction.Run);
		int percent = SessionRun ? 100 : AlwaysRunOn != input.Down(GameAction.Run) ? Math.Max(100, Timing("run_speed_percent")) : 100;
		int left = dtMs;
		while (_s.FieldInfo() is { } f)
		{
			if (_walk is { Led: false } carried)
			{
				carried.ElapsedMs += left;
				_field.LeaderStep = carried.ElapsedMs < carried.LengthMs ? (carried.X, carried.Y, carried.LengthMs, carried.ElapsedMs) : null;
				_walk = carried.ElapsedMs < carried.LengthMs ? carried : null;
				Carry(left);
				return;
			}
			if (_walk is { } walk)
			{
				// Drawn at its end with the session not there yet, it waits; a step the session has not taken in twice its
				// length (something the guess missed stood in the way) is dropped and the leader glides back.
				int part = Math.Min(left, (walk.ElapsedMs >= walk.LengthMs ? 2 : 1) * walk.LengthMs - walk.ElapsedMs);
				if (!Feed(walk.Arrived ? Direction.None : walk.Direction, walk.Arrived || SessionRun ? part : RunMs(part, walk.Percent)))
				{
					return;
				}
				walk.ElapsedMs += part;
				left -= part;
				f = _s.FieldInfo()!;
				walk.Arrived |= (f.LeaderX, f.LeaderY) == (walk.X, walk.Y);
				bool ended = walk.ElapsedMs >= walk.LengthMs && walk.Arrived;
				if (!ended && walk.ElapsedMs < 2 * walk.LengthMs)
				{
					_field.LeaderStep = (walk.X, walk.Y, walk.LengthMs, walk.ElapsedMs);
					return;
				}
				_walk = null;
				_field.LeaderStep = null;
				if (_interactQueued)
				{
					_interactQueued = false;
					Interact(_s.Field()!);
					Feed(Direction.None, left);
					return;
				}
				if (!ended)
				{
					Feed(Direction.None, left);
					return;
				}
				continue;
			}
			Direction next = _bufferedDirection != Direction.None ? _bufferedDirection : _heldDirections.Count > 0 ? _heldDirections[^1] : Direction.None;
			_bufferedDirection = Direction.None;
			// A running belt takes no input.
			bool onBelt = SessionRun ? f.ConveyorStepMs > 0 : _data.BeltMs.GetValueOrDefault(f.AreaId)?.ContainsKey((f.LeaderX, f.LeaderY)) == true;
			if (next == Direction.None || !f.PlayerControl || onBelt || _s.Field() is not { } view)
			{
				Carry(left);
				return;
			}
			if (f.Facing != next)
			{
				_s.Face(next);
			}
			(int x, int y) = StepFrom(f.LeaderX, f.LeaderY, next);
			if (SessionRun ? (f.StepMask & (1 << (int)next)) == 0 : !Enterable(view, x, y))
			{
				// Blocked, or a visible enemy there: the session turns the leader, or starts the battle.
				Feed(next, left);
				return;
			}
			if (SessionRun)
			{
				// The session's own step time, with the run input held or not, from the input it has already spent on it.
				int length = Math.Max(1, _running ? f.RunStepMs : f.LeaderStepMs);
				int gone = Math.Clamp(f.StepProgressMs, 0, length - 1);
				_walk = new WalkStep(next, x, y, gone, length, Math.Max(1, f.StepMs) * 100 / length, led: true);
				_field.LeaderStep = (x, y, length, gone);
				continue;
			}
			_walk = new WalkStep(next, x, y, 0, Math.Max(1, f.LeaderStepMs * 100 / percent), percent, led: true);
			_field.LeaderStep = (x, y, _walk.LengthMs, 0);
		}
	}

	// The session's update for part of the frame; false once the field is left (an event, a battle, another area), which
	// ends the walk.
	private bool Feed(Direction held, int ms)
	{
		string area = _s.FieldInfo()?.AreaId ?? "";
		if (ms > 0 && SessionRun)
		{
			_s.Update(ms, held, _running);
		}
		else if (ms > 0)
		{
			_s.Update(ms, held);
		}
		if (_s.Mode == Mode.Field && _s.FieldInfo()?.AreaId == area)
		{
			return true;
		}
		EndWalk();
		return false;
	}

	// The frame's rest with no direction. A move the sample did not lead, a conveyor carrying the party, glides over the
	// conveyor's own time per cell from the frame it shows.
	private void Carry(int ms)
	{
		FieldInfo f = _s.FieldInfo()!;
		if (SessionRun)
		{
			// The belt's own time per cell, from the progress the session has made toward its next step since this one.
			if (Feed(Direction.None, ms) && _s.FieldInfo() is { } moved && Math.Abs(moved.LeaderX - f.LeaderX) + Math.Abs(moved.LeaderY - f.LeaderY) == 1 &&
				f.ConveyorStepMs > 0)
			{
				int gone = Math.Clamp(moved.ConveyorStepMs > 0 ? moved.ConveyorProgressMs : 0, 0, f.ConveyorStepMs - 1);
				_walk = new WalkStep(Direction.None, moved.LeaderX, moved.LeaderY, gone, f.ConveyorStepMs, 100, led: false);
				_field.LeaderStep = (moved.LeaderX, moved.LeaderY, f.ConveyorStepMs, gone);
			}
			return;
		}
		if (Feed(Direction.None, ms) && _s.FieldInfo() is { } now && Math.Abs(now.LeaderX - f.LeaderX) + Math.Abs(now.LeaderY - f.LeaderY) == 1 &&
			_data.BeltMs.GetValueOrDefault(f.AreaId)?.TryGetValue((f.LeaderX, f.LeaderY), out int beltMs) == true)
		{
			_walk = new WalkStep(Direction.None, now.LeaderX, now.LeaderY, 0, beltMs, 100, led: false);
			_field.LeaderStep = (now.LeaderX, now.LeaderY, beltMs, 0);
		}
	}

	private int RunMs(int ms, int percent)
	{
		int total = ms * percent + _runCarry;
		_runCarry = total % 100;
		return total / 100;
	}

	// What Confirm or Interact acts on: the cell the leader faces when something there answers, else a neighboring
	// cell that does, the sides before the back (clockwise first), which the leader turns to face. Aboard there is
	// nothing to interact with; Confirm steps off.
	// Under game.json's `interact: "nearby"` it is the session's own target (Session::interactTarget): Ready false when
	// interact would refuse, Across when used over a counter, Keeper the actor keeping a shop or rest point.
	private sealed record InteractTarget(int X, int Y, Direction Facing, string Verb, bool Ready = true, bool Across = false, string Keeper = "");

	private InteractTarget? FindInteractTarget(FieldView view)
	{
		FieldInfo f = view.Info;
		if (_data.InteractNearby)
		{
			return _s.InteractTarget() is { } t ? new InteractTarget(t.X, t.Y, t.Facing, PromptWord(t.Kind), t.Ready, t.Across, t.Keeper) : null;
		}
		if (!f.PlayerControl || f.Aboard.Length > 0)
		{
			return null;
		}
		Direction facing = f.Facing;
		foreach (int quarters in new[] { 0, 1, 3, 2 })
		{
			Direction d = (Direction)(((int)facing + quarters) % 4);
			(int x, int y) = StepFrom(f.LeaderX, f.LeaderY, d);
			if (InteractVerb(view, x, y) is { } verb)
			{
				return new InteractTarget(x, y, d, verb);
			}
		}
		return null;
	}

	// What the session's interaction does with a cell, as the prompt's word, or null when it would only refuse.
	private string? InteractVerb(FieldView view, int x, int y)
	{
		if (view.Info.AreaKind == AreaKind.Field && view.Vehicles.Any(v => !v.Aboard && v.X == x && v.Y == y))
		{
			return T("ui.prompt.board");
		}
		return view.Points.FirstOrDefault(p => p.X == x && p.Y == y && p.Kind is not (PointKind.Step or PointKind.Gangway or PointKind.Conveyor)) switch
		{
			{ Kind: PointKind.Talk, Active: true } => T("ui.hint.talk"),
			{ Kind: PointKind.Chest, Active: true } => T("ui.prompt.open"),
			{ Kind: PointKind.Shop, Active: true } => T("ui.prompt.shop"),
			{ Kind: PointKind.Rest } => T("ui.prompt.rest"),
			{ Kind: PointKind.Port, Active: true } => T("ui.prompt.travel"),
			{ Kind: PointKind.Helm } when view.Info.ShipPort.Length > 0 => T("ui.prompt.travel"),
			_ => null,
		};
	}

	// The prompt's word for what the session's target is: a sign or other hook with no actor is read.
	private string PromptWord(InteractKind kind) => T(kind switch
	{
		InteractKind.Vehicle => "ui.prompt.board",
		InteractKind.Port or InteractKind.Helm => "ui.prompt.travel",
		InteractKind.Talk => "ui.prompt.read",
		InteractKind.Actor => "ui.prompt.talk",
		InteractKind.Chest => "ui.prompt.open",
		InteractKind.Shop => "ui.prompt.shop",
		_ => "ui.prompt.rest",
	});

	// Under the `nearby` rule the session turns the leader to its target itself.
	private void Interact(FieldView view)
	{
		InteractTarget? target = FindInteractTarget(view);
		if (!_data.InteractNearby && target is not null && target.Facing != view.Info.Facing)
		{
			_s.Face(target.Facing);
		}
		TurnKeeper(view, _data.InteractNearby ? target?.Keeper : _s.InteractTarget()?.Keeper);
		Report(_s.Interact());
	}

	// A shop's or rest point's keeper turns toward the leader while the prompt points at its counter and while its menu is
	// open; anyone else faces as the session says.
	private void TurnKeeper(FieldView view, string? keeper)
	{
		_field.FacingOverrides.Clear();
		if (keeper is { Length: > 0 } && view.Actors.FirstOrDefault(a => a.Id == keeper) is { } k)
		{
			int dx = view.Info.LeaderX - k.X, dy = view.Info.LeaderY - k.Y;
			if (dx != 0 || dy != 0)
			{
				_field.FacingOverrides[keeper] = Math.Abs(dx) > Math.Abs(dy) ? (dx > 0 ? Direction.East : Direction.West) : (dy > 0 ? Direction.South : Direction.North);
			}
		}
	}

	// The profile's Always run, for the session's run (game.json's run_speed_percent) and the sample's own alike.
	private void ToggleAlwaysRun()
	{
		Profile p = _s.Profile();
		Report(_s.SetProfile(p with { AlwaysRun = !p.AlwaysRun }));
	}

	private bool AlwaysRunOn => _s.Profile().AlwaysRun;

	// How the field draws an actor now: its figure (`enemy ID`, `look NAME` or `own`), the library's animation with its
	// milliseconds left and in all, and `hidden` or `removed`.
	private string ActorLookProbe(string id) => _s.Field()?.Actors.FirstOrDefault(a => a.Id == id) is { } a
		? (a.SpriteEnemy.Length > 0 ? "enemy " + a.SpriteEnemy : a.Look.Length > 0 ? "look " + a.Look : "own") +
			(a.Animation.Length > 0 && a.AnimationTotalMs > 0 ? $" {a.Animation} {a.AnimationMs}/{a.AnimationTotalMs}" : "") +
			(a.Removal != ActorRemoval.None ? " removed" : a.Visible ? "" : " hidden")
		: "-";

	// The real-time field input's probes: the mode, the leader as drawn in field pixels, the step being drawn (direction,
	// cell, time gone of its length, the session's pace), the direction buffered for after it, the directions held (the
	// latest last), the prompt's target as drawn, what Confirm would act on now, and the two options.
	private string FieldState(string key)
	{
		static string Name(Direction d) => d.ToString().ToLowerInvariant();
		(float lx, float ly) = _field.Leader(Lineup());
		return key switch
		{
			"fieldinput" => _realtimeField ? "realtime" : "classic",
			"leaderpos" => _s.FieldInfo() is null ? "-" : FormattableString.Invariant($"{lx * _field.Cell:0.#} {ly * _field.Cell:0.#}"),
			"walkstep" => _walk is { } w ? $"{(w.Led ? Name(w.Direction) : "carried")} {w.X} {w.Y} {Math.Min(w.ElapsedMs, w.LengthMs)}/{w.LengthMs} {w.Percent}%" : "-",
			"walkbuffer" => _bufferedDirection == Direction.None ? "-" : Name(_bufferedDirection),
			"held" => _heldDirections.Count == 0 ? "-" : string.Join(" ", _heldDirections.Select(Name)),
			"interactprompt" => _promptTarget is { } p ? $"{p.Verb} at {p.X} {p.Y}" + (p.Ready ? "" : " refused") : "none",
			"interacttarget" => _s.Field() is { } view && FindInteractTarget(view) is { } t
				? $"{t.Verb} at {t.X} {t.Y} facing {Name(t.Facing)}" + (t.Across ? " across" : "") + (t.Keeper.Length > 0 ? $" kept by {t.Keeper}" : "") + (t.Ready ? "" : " refused")
				: "none",
			"always_run" => AlwaysRunOn ? "on" : "off",
			_ => _bindings.InteractPrompt ? "on" : "off",
		};
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
			EndWalk();
			return;
		}
		if (_realtimeField)
		{
			UpdateRealtimeField(dtMs, input);
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

	private void UpdateRealtimeField(int dtMs, ActionInput input)
	{
		_promptTarget = null;
		if (_s.FieldInfo() is not { } f)
		{
			_s.Update(dtMs);
			return;
		}
		if (input.Confirm || input.Pressed(GameAction.Interact))
		{
			if (f.Aboard.Length > 0)
			{
				Report(_s.LeaveVehicle());
			}
			else if (_walk is not null)
			{
				_interactQueued = true;
			}
			else
			{
				Interact(_s.Field()!);
			}
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
		if (_s.Mode != Mode.Field || Screen != Screen.Field)
		{
			EndWalk();
			_s.Update(dtMs);
			return;
		}
		RealtimeWalk(dtMs, input);
		if (_walk is null && _bindings.InteractPrompt && _s.Mode == Mode.Field && _s.Field() is { } view)
		{
			_promptTarget = FindInteractTarget(view);
			TurnKeeper(view, _data.InteractNearby ? _promptTarget?.Keeper : _s.InteractTarget()?.Keeper);
		}
		else if (_s.Mode == Mode.Field)
		{
			_field.FacingOverrides.Clear();
		}
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
		_field.Draw(s, art, view, _clockMs, lineup, _camera is null ? null : CameraFocus(view), ShakeOffset(), Layout.Styles.Param("field_cell"));
		if (_clickWalk is { } goal)
		{
			(int gx, int gy) = CellCenter(goal.X, goal.Y);
			int r = _field.Cell / 3 + (int)(3 * MathF.Sin(_clockMs / 120f)) * _field.Cell / Art.Tile;
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
		// The interaction prompt: the pointer bobbing over what the leader would act on, and the layout's `prompt` fields.
		(int px, int py) = _promptTarget is { } target ? CellCenter(target.X, target.Y) : (0, 0);
		// Like a speech bubble, the pointer is left out while the leader stands on the cell it would cover.
		if (_promptTarget is { } marked && _s.FieldInfo() is { } here && (here.LeaderX, here.LeaderY) != (marked.X, marked.Y - 1))
		{
			int cell = _field.Cell, bob = (int)(3 * MathF.Sin(_clockMs / 180f)) * cell / Art.Tile;
			s.Image(art.Pointer, px - cell / 2, py - cell - cell / 6 + bob, cell, cell, Color.White);
		}
		ctx.Set("prompt_visible", _promptTarget is not null)
			.Set("prompt_ready", _promptTarget?.Ready ?? true)
			.Set("prompt_key", _promptTarget is null ? "" : Hint(GameAction.Confirm))
			.Set("prompt_text", _promptTarget?.Verb ?? "")
			.Set("prompt_x", px)
			.Set("prompt_y", py);
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
		visible &= left > 0 && Ui.Param("field", "area_banner", UiData(), 1) != 0;
		return ctx.Set("banner_visible", visible)
			.Set("banner_title", _field.BannerTitle)
			.Set("banner_kind", visible ? AreaKindText(_field.BannerKind) : "")
			.Set("banner_alpha", alpha)
			.Set("banner_drop", (int)((1f - alpha) * 16))
			.Set("banner_ms", shown)
			.Set("banner_left_ms", left);
	}
}
