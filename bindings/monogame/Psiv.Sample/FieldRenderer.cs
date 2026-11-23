using System.Globalization;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// A person as the last field frame drew it, in cells; `Marked` when its pose drew motion marks.
// Stacked: a follower standing on the leader's cell, listed there but not painted, the leader covering it.
public readonly record struct DrawnPerson(float X, float Y, bool Walking, Direction Facing, Pose Pose, bool Marked, bool Stacked = false);

// Draws a FieldView: a camera that follows the leader, tiles chosen from neighbors and cell coordinates,
// and actors that glide between cells. The game moves everyone a whole cell at a time; the glides only
// change what is drawn between those moves, over the area's step time, so game timing is untouched.
public sealed class FieldRenderer
{
	private const int Tile = Art.Tile;
	public const int BannerMs = 2600;
	// Every member of a full lineup (rules::kMaxPartySize, 5) walks behind the leader, and a follower who is not a
	// member walks last.
	private const int Followers = 5;

	// A position that slides from where it was drawn to the cell the game reports.
	private sealed class Glide
	{
		private float _fromX;
		private float _fromY;
		private int _elapsedMs;
		private int _lengthMs;

		public int X { get; private set; }
		public int Y { get; private set; }
		public int Steps { get; private set; }
		public int IdleMs { get; private set; }
		public Direction Facing { get; set; } = Direction.South;

		public float Progress => _lengthMs <= 0 ? 1f : Math.Clamp(_elapsedMs / (float)_lengthMs, 0f, 1f);
		public float PX => _fromX + (X - _fromX) * Progress;
		public float PY => _fromY + (Y - _fromY) * Progress;

		// Stand, walk A or walk B: one walking frame per step, standing in between.
		public int Frame => IdleMs > 80 ? 0 : Progress < 0.5f ? 1 + Steps % 2 : 0;

		public void Snap(int x, int y)
		{
			X = x;
			Y = y;
			_fromX = x;
			_fromY = y;
			_lengthMs = 0;
			IdleMs = 1000;
		}

		// Returns true when it started a one-cell move, `elapsedMs` of it already gone.
		public bool Go(int x, int y, int lengthMs, int elapsedMs = 0)
		{
			if (x == X && y == Y)
			{
				return false;
			}
			if (Math.Abs(x - X) + Math.Abs(y - Y) != 1)
			{
				Snap(x, y);
				return false;
			}
			Facing = x > X ? Direction.East : x < X ? Direction.West : y > Y ? Direction.South : Direction.North;
			_fromX = PX;
			_fromY = PY;
			X = x;
			Y = y;
			_elapsedMs = elapsedMs;
			_lengthMs = lengthMs;
			IdleMs = 0;
			Steps++;
			return true;
		}

		// Takes over a person drawn at (x, y) somewhere else: it walks to the cell it holds, a step time per cell.
		public void Catch(float x, float y, int stepMs)
		{
			float dx = X - x, dy = Y - y;
			if (dx != 0f || dy != 0f)
			{
				Facing = Math.Abs(dx) > Math.Abs(dy) ? (dx > 0 ? Direction.East : Direction.West) : (dy > 0 ? Direction.South : Direction.North);
			}
			_fromX = x;
			_fromY = y;
			_elapsedMs = 0;
			_lengthMs = stepMs * Math.Max(1, (int)MathF.Ceiling(Math.Abs(dx) + Math.Abs(dy)));
			IdleMs = 0;
			Steps++;
		}

		public void Advance(int dtMs)
		{
			_elapsedMs += dtMs;
			if (Progress >= 1f)
			{
				IdleMs += dtMs;
			}
		}
	}

	// A script actor walking cell by cell to where the session says it stands: along x, then along y, the order the
	// game takes its facing from. Its position is a function of the time since the walk began, so a walk of n cells
	// takes exactly n cell times, which is how long the controller holds the move action.
	private sealed class ActorWalk
	{
		private readonly List<(int X, int Y)> _cells = [];
		private int _elapsedMs;
		private int _cellMs = 250;

		public Direction Facing { get; set; } = Direction.South;
		public (int X, int Y) Target => _cells[^1];
		// The cells of the walk under way, from where it began, for the route probe.
		public IReadOnlyList<(int X, int Y)> Cells => _cells;
		public bool Walking => _elapsedMs < (_cells.Count - 1) * _cellMs;
		public int CellMs => _cellMs;
		// The cell it is stepping into, or standing on.
		public (int X, int Y) Cell => _cells[Math.Min(_elapsedMs / _cellMs + 1, _cells.Count - 1)];
		public float PX { get; private set; }
		public float PY { get; private set; }
		public int Frame { get; private set; }

		public ActorWalk(int x, int y)
		{
			_cells.Add((x, y));
			Place();
		}

		public void WalkTo(int x, int y, int cellMs)
		{
			Restart(cellMs);
			(int cx, int cy) = Target;
			while (cx != x)
			{
				cx += Math.Sign(x - cx);
				_cells.Add((cx, cy));
			}
			while (cy != y)
			{
				cy += Math.Sign(y - cy);
				_cells.Add((cx, cy));
			}
			Place();
		}

		// Walks the cells in order, each a neighbor of the one before, starting next to the target.
		public void WalkAlong(IEnumerable<(int X, int Y)> path, int cellMs)
		{
			Restart(cellMs);
			_cells.AddRange(path);
			Place();
		}

		private void Restart(int cellMs)
		{
			if (!Walking)
			{
				_cells.RemoveRange(0, _cells.Count - 1);
				_elapsedMs = 0;
				_cellMs = Math.Max(1, cellMs);
			}
		}

		public void Advance(int dtMs)
		{
			_elapsedMs += dtMs;
			Place();
		}

		private void Place()
		{
			int last = _cells.Count - 1;
			int segment = Math.Min(_elapsedMs / _cellMs, last);
			if (segment >= last)
			{
				(PX, PY) = _cells[last];
				Frame = 0;
				return;
			}
			float t = (_elapsedMs - segment * _cellMs) / (float)_cellMs;
			(int ax, int ay) = _cells[segment];
			(int bx, int by) = _cells[segment + 1];
			PX = ax + (bx - ax) * t;
			PY = ay + (by - ay) * t;
			Facing = bx > ax ? Direction.East : bx < ax ? Direction.West : by > ay ? Direction.South : Direction.North;
			Frame = t < 0.5f ? 1 + segment % 2 : 0;
		}
	}

	private sealed class MemberPlace(ActorWalk walk, Direction facing)
	{
		public ActorWalk Walk { get; set; } = walk;
		public Direction Facing { get; set; } = facing;
	}

	private sealed record PoseRun(string Animation, ulong Handle, int StartMs, bool Loop);

	private string _area = "";
	// The clock when the party entered the area shown, where the `entry` clocks of its motion start.
	private int _enteredMs;
	private int _lastClock = -1;
	private readonly Glide _leader = new();
	private readonly Glide[] _followers = [new(), new(), new(), new(), new()];
	// Whether a follower past the end of FollowerLine stands on the leader (the fallback arrival) or on the line's last cell.
	private bool _lineEndsOnLeader;
	// The cells the fallback arrival (FollowersBehind) put the followers on, nearest first, kept behind the session's trail
	// until that trail is long enough for the line.
	private readonly List<(int X, int Y)> _arrival = [];
	// The cells a script's walk of the leader left, newest first, ahead of the session's trail: the session's leader does
	// not move during a scene.
	private readonly List<(int X, int Y)> _scriptTrail = [];
	// The cell a script's walk of the leader last stepped into, which the followers not in the scene trail behind.
	private (int X, int Y)? _scriptedLeaderCell;
	private readonly Dictionary<int, Glide> _enemies = [];
	// Visible enemies a script moves (`enemy.<zone>.<n>`), walking along their route until they arrive.
	private readonly Dictionary<int, ActorWalk> _enemyWalks = [];
	private readonly Dictionary<int, (List<(int X, int Y)> Cells, int CellMs)> _enemyRoutes = [];
	private readonly Dictionary<string, ActorWalk> _actors = [];
	// Milliseconds per cell for each actor's next walk, from its move's speed.
	private readonly Dictionary<string, int> _paces = [];
	// The cells of each actor's next walk, from its move's record (PresentedAction::route), starting where it stood.
	private readonly Dictionary<string, List<(int X, int Y)>> _routes = [];
	// Party members a script moved or turned during the current scene, drawn there instead of in the walking line,
	// and members outside the lineup a script moved, turned or posed, drawn until the scene ends.
	private readonly Dictionary<string, MemberPlace> _members = [];
	// Members a scene left out of the line, walking back to their places in it once the scene is over.
	private readonly Dictionary<string, ActorWalk> _returns = [];
	// The pose each person is playing, from a script's `anim`.
	private readonly Dictionary<string, PoseRun> _poseRuns = [];
	private int _bannerLeftMs;
	private string _bannerTitle = "";
	private AreaKind _bannerKind;

	// The area's name and kind, shown for BannerMs after arriving; BannerLeftMs is 0 once it is gone.
	public string BannerTitle => _bannerTitle;
	public AreaKind BannerKind => _bannerKind;
	public int BannerLeftMs => _bannerLeftMs;

	// Whether a story flag is set, and an area's map as written, for the cells a whole-area picture shows changed.
	public Func<string, bool> Flag { get; set; } = _ => false;
	public Func<string, AreaMap?> MapOf { get; set; } = _ => null;

	// What the last frame drew from the asset directory's field entries: the whole-area picture with its above picture
	// and how many cells came from a change's picture or from tiles, and each object by key with its state and frame.
	public string DrawnPicture { get; private set; } = "-";
	public List<string> DrawnObjects { get; } = [];
	// The area's motion as the last frame drew it: each palette track's step (the area's, then its tileset's), each frame
	// run's frame (`-` while its flags keep it off) and how many layers showed behind and in front, or `-` without any.
	public string DrawnMotion { get; private set; } = "-";

	public int CameraX { get; private set; }
	public int CameraY { get; private set; }
	// The cell size the last frame drew the field at, in screen pixels.
	public int Cell { get; private set; } = Tile;
	// Where the leader was last drawn, in screen pixels (its top edge).
	public int LeaderScreenY { get; private set; }

	// Where the leader is drawn now, in cells: its scripted place during a scene, the platform carrying it, otherwise its glide.
	public (float X, float Y) Leader(IReadOnlyList<string> lineup) =>
		lineup.Count > 0 && _members.TryGetValue(lineup[0], out MemberPlace? m) ? (m.Walk.PX, m.Walk.PY) : RideAt() ?? (_leader.PX, _leader.PY);

	// The moving platforms of the area (GameSession.Platforms), set before each Track, and the cell size their pixel places
	// are given at (GameInfo.CellPixels).
	public IReadOnlyList<Platform> Platforms { get; set; } = [];
	public int CellPixels { get; set; } = 16;
	// The platform carrying the party by the last Track, null on foot.
	private Platform? _ride;

	// Where each follower stands on a platform, in the field's own pixels from the leader.
	private static readonly (int X, int Y)[] RideSpots = [(-14, -4), (14, -4), (-7, -10), (7, -10), (0, -14)];

	private (float X, float Y) PlatformAt(Platform p) => (p.PixelX / (float)Math.Max(1, CellPixels), p.PixelY / (float)Math.Max(1, CellPixels));

	// The platform's place while it carries the party and the leader's step onto it has ended.
	private (float X, float Y)? RideAt() => _ride is { } ride && _leader.Progress >= 1f ? PlatformAt(ride) : null;

	// The platforms the last frame drew: `ID PX PY` in the data's pixels (CellPixels a cell), with `carrying` for the one ridden.
	public string DrawnPlatforms { get; private set; } = "-";
	// Each exit the last frame drew: `X Y closed`, or `X Y FIRES` and `marker` (the field's exit_markers picture), `arrow`
	// or `door`.
	public string DrawnExits { get; private set; } = "-";

	// The people drawn in the last frame, actors and party members, by id: where, whether between cells, facing, pose.
	public Dictionary<string, DrawnPerson> DrawnActors { get; } = [];
	// Everything the last frame stood on the map (people, vehicles, enemies, points' objects), each with its cell and the
	// half cell above it as drawn, and the area's above picture with where it went; for the `abovecover` probe.
	public List<(string Who, Rectangle Rect)> DrawnSprites { get; } = [];
	public (int Image, Rectangle Rect)? DrawnAbove { get; private set; }
	// The bubbles and marks the last frame drew over the above picture, on purpose.
	public List<Rectangle> DrawnOverlays { get; } = [];
	// The sheet pose each person posing in the last frame drew from its asset sheet, by id.
	public Dictionary<string, string> DrawnSheetPoses { get; } = [];
	// The vehicles the last frame drew, by id: "X Y" of the cell under each, with " aboard" for the one the party rides.
	public Dictionary<string, string> DrawnVehicles { get; } = [];
	// Conveyor cells drawn in the last frame, running or still, for the headless probe.
	public int DrawnConveyors { get; private set; }
	public int RunningConveyors { get; private set; }
	// Cells of tile step hooks in the last frame's area, and those of hooks whose flags hold, drawn with a rune.
	public int DrawnTileHooks { get; private set; }
	public int ActiveTileHooks { get; private set; }

	// With the real-time field input the controller leads the leader's glide: the cell it walks to, the step's length
	// and how much of it has gone, from the moment the step starts rather than from the session's report of the new cell.
	public (int X, int Y, int LengthMs, int ElapsedMs)? LeaderStep { get; set; }

	// Actors whose id names an encounter, drawn as its field enemy rather than as a person.
	public Func<string, bool> CreatureLook { get; set; } = _ => false;
	// The encounter whose field figure an enemy's figure is drawn with (an actor's `sprite_enemy`), null for the enemy's own.
	public Func<string, string?> EnemyEncounter { get; set; } = _ => null;
	// A wandering or patrolling actor's speed in percent of the area's step time, null when it does neither.
	public Func<string, int?> WanderPercent { get; set; } = _ => null;
	// A talk point's `actor_at` cell, where its actor stands apart from the hook's cell; null for none.
	public Func<string, (int X, int Y)?> ActorHome { get; set; } = _ => null;
	// Actors drawn facing another way than the session reports while standing: a shop's or rest point's keeper turned
	// toward the leader.
	public Dictionary<string, Direction> FacingOverrides { get; } = [];
	// The visible enemies a script addresses by name, as the last frame drew them: "X Y", or "walking".
	public Dictionary<string, string> DrawnEnemies { get; } = [];

	// The cells of the walk an actor is drawing (`X,Y X,Y ...` from where it began), `-` when it stands.
	public string RouteProbe(string id) => _actors.TryGetValue(id, out ActorWalk? w) && w.Walking ? string.Join(" ", w.Cells.Select(c => $"{c.X},{c.Y}")) : "-";

	// The cell an actor is drawn at or walking to, if it is on screen in this area.
	public (int X, int Y)? ActorCell(string id) => _actors.TryGetValue(id, out ActorWalk? w) ? w.Target : null;

	// The time per cell of the actor's next walk; without one the actor is put at its new cell at once.
	public void Pace(string id, int cellMs) => _paces[id] = cellMs;

	// The actor's next walk goes along these cells (the move's record), a cell per `cellMs`.
	public void Route(string id, IEnumerable<(int X, int Y)> cells, int cellMs)
	{
		_routes[id] = [.. cells];
		_paces[id] = cellMs;
	}

	// A visible enemy a script moves walks along its route, from the cell it is drawn on.
	public void WalkEnemy(int id, IEnumerable<(int X, int Y)> cells, int cellMs) => _enemyRoutes[id] = ([.. cells], cellMs);

	// Where a party member is drawn or walking to: its scripted cell during a scene, the cell it is walking back to
	// after one, otherwise its place in the walking line (the leader's cell, or the trail behind it). A member
	// outside the lineup has none until a script moves, turns or poses it.
	public (int X, int Y)? MemberCell(string id, IReadOnlyList<string> lineup)
	{
		if (_members.TryGetValue(id, out MemberPlace? m))
		{
			return m.Walk.Target;
		}
		if (_returns.TryGetValue(id, out ActorWalk? back))
		{
			return back.Target;
		}
		int i = IndexOf(lineup, id);
		return i < 0 ? null : i > 0 && i - 1 < _followers.Length ? (_followers[i - 1].X, _followers[i - 1].Y) : (_leader.X, _leader.Y);
	}

	private static int IndexOf(IReadOnlyList<string> lineup, string id)
	{
		for (int i = 0; i < lineup.Count; i++)
		{
			if (lineup[i] == id)
			{
				return i;
			}
		}
		return -1;
	}

	private MemberPlace Place(string id, IReadOnlyList<string> lineup, int x, int y)
	{
		if (!_members.TryGetValue(id, out MemberPlace? m))
		{
			if (_returns.Remove(id, out ActorWalk? back))
			{
				_members[id] = m = new MemberPlace(back, back.Facing);
				return m;
			}
			int i = IndexOf(lineup, id);
			Direction facing = i > 0 && i - 1 < _followers.Length ? _followers[i - 1].Facing : i == 0 ? _leader.Facing : Direction.South;
			_members[id] = m = new MemberPlace(new ActorWalk(x, y), facing);
		}
		return m;
	}

	// A script moves a party member: it walks there along x, then along y, a cell per `cellMs` (0 puts it there at
	// once), and ends facing its last leg, as the game does for actors. A member outside the lineup that has no cell
	// yet is put there at once, as the game puts an actor without a talk point. Returns the cells walked.
	public int MoveMember(string id, IReadOnlyList<string> lineup, int x, int y, int cellMs)
	{
		if (MemberCell(id, lineup) is not (int fx, int fy))
		{
			Place(id, lineup, x, y);
			return 0;
		}
		MemberPlace m = Place(id, lineup, fx, fy);
		if ((x, y) != (fx, fy))
		{
			m.Facing = y != fy ? (y > fy ? Direction.South : Direction.North) : (x > fx ? Direction.East : Direction.West);
		}
		if (cellMs > 0)
		{
			m.Walk.WalkTo(x, y, cellMs);
		}
		else
		{
			m.Walk = new ActorWalk(x, y);
		}
		return Math.Abs(x - fx) + Math.Abs(y - fy);
	}

	// A script turns a party member toward a cell, along the longer axis (a tie turns it north or south), as the
	// game turns actors. Nothing happens when the cell is its own.
	public void FaceMember(string id, IReadOnlyList<string> lineup, int x, int y)
	{
		if (MemberCell(id, lineup) is not (int fx, int fy))
		{
			return;
		}
		int dx = x - fx, dy = y - fy;
		if (dx != 0 || dy != 0)
		{
			Place(id, lineup, fx, fy).Facing = Math.Abs(dx) > Math.Abs(dy) ? (dx > 0 ? Direction.East : Direction.West) : (dy > 0 ? Direction.South : Direction.North);
		}
	}

	// A member outside the lineup that a scene turns or poses before any move has given it a cell steps up next to
	// the leader, on the nearest walkable cell nobody drawn stands on: behind the leader first, then on its right and
	// its left, then in front of it, and further out in the same order when those are all taken.
	public void Arrive(string id, IReadOnlyList<string> lineup, FieldView view)
	{
		if (MemberCell(id, lineup) is not null)
		{
			return;
		}
		(int X, int Y) leader = LeaderCell(lineup);
		Direction facing = lineup.Count > 0 && _members.TryGetValue(lineup[0], out MemberPlace? scripted) ? scripted.Facing : _leader.Facing;
		Direction[] order = [Turn(facing, 2), Turn(facing, 1), Turn(facing, 3), facing];
		HashSet<(int X, int Y)> taken = Taken(view, lineup, id);
		var seen = new HashSet<(int X, int Y)> { leader };
		var queue = new Queue<(int X, int Y)>([leader]);
		while (queue.Count > 0)
		{
			(int x, int y) = queue.Dequeue();
			foreach (Direction d in order)
			{
				(int nx, int ny) = Step(x, y, d);
				if (!view.Cell(nx, ny).HasFlag(CellFlags.Walkable) || !seen.Add((nx, ny)))
				{
					continue;
				}
				if (!taken.Contains((nx, ny)))
				{
					Place(id, lineup, nx, ny);
					return;
				}
				queue.Enqueue((nx, ny));
			}
		}
		Place(id, lineup, leader.X, leader.Y);
	}

	private (int X, int Y) LeaderCell(IReadOnlyList<string> lineup) =>
		lineup.Count > 0 && _members.TryGetValue(lineup[0], out MemberPlace? m) ? m.Walk.Target : (_leader.X, _leader.Y);

	// The direction `quarters` quarter turns clockwise from `d`.
	private static Direction Turn(Direction d, int quarters) => (Direction)(((int)d + quarters) % 4);

	private static (int X, int Y) Step(int x, int y, Direction d) => d switch
	{
		Direction.North => (x, y - 1),
		Direction.East => (x + 1, y),
		Direction.South => (x, y + 1),
		_ => (x - 1, y),
	};

	// The cells someone other than `self` is drawn on or walking to: the leader, the followers in line and the places
	// in it, the members a scene placed or that are walking back, and the visible actors.
	private HashSet<(int X, int Y)> Taken(FieldView view, IReadOnlyList<string> lineup, string self)
	{
		var taken = new HashSet<(int X, int Y)>();
		void Walker(ActorWalk w)
		{
			taken.Add(w.Target);
			taken.Add(((int)MathF.Round(w.PX), (int)MathF.Round(w.PY)));
		}
		if (lineup.Count == 0 || lineup[0] != self)
		{
			taken.Add(LeaderCell(lineup));
		}
		for (int i = 0; i < Math.Min(_followers.Length, lineup.Count - 1); i++)
		{
			string id = lineup[i + 1];
			// The place in the line, where a member walking back is going.
			if (id != self && !_members.ContainsKey(id))
			{
				taken.Add((_followers[i].X, _followers[i].Y));
			}
		}
		foreach ((string id, MemberPlace m) in _members)
		{
			if (id != self)
			{
				Walker(m.Walk);
			}
		}
		foreach ((string id, ActorWalk w) in _returns)
		{
			if (id != self)
			{
				Walker(w);
			}
		}
		foreach (FieldActor a in view.Actors)
		{
			if (a.Visible && _actors.TryGetValue(a.Id, out ActorWalk? w))
			{
				Walker(w);
			}
		}
		return taken;
	}

	// The shortest way from `from` to `to` over walkable cells nobody else is drawn on or walking to, trying north,
	// east, south and west from each cell, without the start. Null when there is none within a few cells of the
	// straight walk.
	private static List<(int X, int Y)>? PathAround(FieldView view, HashSet<(int X, int Y)> taken, (int X, int Y) from, (int X, int Y) to)
	{
		int longest = Math.Abs(to.X - from.X) + Math.Abs(to.Y - from.Y) + 6;
		var back = new Dictionary<(int X, int Y), (int X, int Y)> { [from] = from };
		var queue = new Queue<((int X, int Y) Cell, int Length)>([(from, 0)]);
		while (queue.Count > 0)
		{
			((int x, int y), int length) = queue.Dequeue();
			if ((x, y) == to)
			{
				var path = new List<(int X, int Y)>();
				for ((int X, int Y) c = to; c != from; c = back[c])
				{
					path.Add(c);
				}
				path.Reverse();
				return path;
			}
			if (length == longest)
			{
				continue;
			}
			foreach (Direction d in (Direction[])[Direction.North, Direction.East, Direction.South, Direction.West])
			{
				(int X, int Y) next = Step(x, y, d);
				bool open = next == to || (view.Cell(next.X, next.Y).HasFlag(CellFlags.Walkable) && !taken.Contains(next));
				if (open && back.TryAdd(next, (x, y)))
				{
					queue.Enqueue((next, length + 1));
				}
			}
		}
		return null;
	}

	// A script animation's pose from `startMs` on the controller's clock; a looping one repeats until another replaces it.
	public void StartPose(string id, string animation, ulong handle, int startMs, bool loop) => _poseRuns[id] = new PoseRun(animation, handle, startMs, loop);

	// A skip abandoned the animation: its pose stops.
	public void StopPose(ulong handle)
	{
		foreach (string id in _poseRuns.Where(x => x.Value.Handle == handle).Select(x => x.Key).ToList())
		{
			_poseRuns.Remove(id);
		}
	}

	// The scene is over and every pose stops. The leader is drawn at its cell again at once, since the player has it
	// back; other lineup members the scene moved walk back to their places in the line; anyone else leaves.
	// While background events run (`backgroundRunning`) the actors' poses play on: those events animate them beside the field.
	public void EndScene(IReadOnlyList<string> lineup, bool backgroundRunning = false)
	{
		foreach (string id in _poseRuns.Keys.Where(id => !backgroundRunning || IndexOf(lineup, id) >= 0 || _members.ContainsKey(id)).ToList())
		{
			_poseRuns.Remove(id);
		}
		foreach ((string id, MemberPlace m) in _members)
		{
			if (IndexOf(lineup, id) > 0)
			{
				if (!m.Walk.Walking)
				{
					m.Walk.Facing = m.Facing;
				}
				_returns[id] = m.Walk;
			}
		}
		_members.Clear();
	}

	// A new area or the title: nothing a scene did is drawn any more.
	public void ClearScene()
	{
		_members.Clear();
		_returns.Clear();
		_poseRuns.Clear();
	}

	private (string Animation, int ElapsedMs, bool Loop)? PoseAt(string id, int clockMs, Art art)
	{
		if (!_poseRuns.TryGetValue(id, out PoseRun? run))
		{
			return null;
		}
		int elapsed = Math.Max(0, clockMs - run.StartMs);
		return run.Loop || elapsed < art.PoseLengthMs(id, run.Animation) || Art.Vanishes(Art.PoseFor(run.Animation)) ? (run.Animation, elapsed, run.Loop) : null;
	}

	// The animation the library is playing on an actor (FieldActor.Animation) and how far through it is, 0 to 1; null when
	// none plays or it has no length, which leaves the actor to the sample's own poses.
	private static (string Animation, float T)? Played(FieldActor a) =>
		a.Animation.Length > 0 && a.AnimationTotalMs > 0 && a.AnimationMs > 0 ? (a.Animation, 1f - a.AnimationMs / (float)a.AnimationTotalMs) : null;

	public static string KindName(AreaKind kind) => kind switch { AreaKind.Town => "Town", AreaKind.Dungeon => "Dungeon", _ => "Field" };

	// The session's followers (Session::followers): the lineup after the leader and the follower actor on the leader's
	// trail, set each frame before Track.
	public IReadOnlyList<Follower> SessionFollowers { get; set; } = [];

	// The fallback for data whose game.json does not set `followers_arrive`: true (the field layout's `followers_arrive`
	// "behind") puts each follower a cell behind the one before on arrival, against the leader's facing, while those cells
	// can be walked, the rest stacked; the session's trail then grows in front of those cells.
	public bool FollowersBehind { get; set; }

	private void Arrive(FieldView view, IReadOnlyList<string> lineup)
	{
		FieldInfo f = view.Info;
		_arrival.Clear();
		_scriptTrail.Clear();
		(int dx, int dy) = f.Facing switch { Direction.North => (0, 1), Direction.South => (0, -1), Direction.East => (-1, 0), _ => (1, 0) };
		(int x, int y) = (f.LeaderX, f.LeaderY);
		int count = Math.Min(_followers.Length, Math.Max(0, lineup.Count - 1));
		while (FollowersBehind && f.Aboard.Length == 0 && _arrival.Count < count && view.Cell(x + dx, y + dy).HasFlag(CellFlags.Walkable))
		{
			(x, y) = (x + dx, y + dy);
			_arrival.Add((x, y));
		}
		List<(int X, int Y)> line = FollowerLine(f);
		for (int i = 0; i < _followers.Length; i++)
		{
			(int fx, int fy) = i < line.Count ? line[i] : line.Count > 0 && !_lineEndsOnLeader ? line[^1] : (f.LeaderX, f.LeaderY);
			_followers[i].Snap(fx, fy);
			_followers[i].Facing = f.Facing;
		}
	}

	// The cells the followers walk to, nearest first: the session's trail as far as it has cells of its own (a follower
	// past its end stands where the last one does), then the fallback arrival's cells (a follower past those stands on the
	// leader).
	private List<(int X, int Y)> FollowerLine(FieldInfo f)
	{
		var line = new List<(int X, int Y)>();
		foreach (Follower x in SessionFollowers)
		{
			(int X, int Y) cell = (x.X, x.Y);
			if (cell == (line.Count > 0 ? line[^1] : (f.LeaderX, f.LeaderY)))
			{
				break;
			}
			line.Add(cell);
		}
		if (line.Count >= SessionFollowers.Count || f.Aboard.Length > 0 || f.RidingPlatform.Length > 0)
		{
			_arrival.Clear();
		}
		line.AddRange(_arrival);
		_lineEndsOnLeader = _arrival.Count > 0;
		return line;
	}

	// Follows the view's positions; `clockMs` is the controller's clock, so the glides advance with the
	// frame time the session got. `lineup` is the party in walking order (the leader first).
	public void Track(FieldView view, int clockMs, IReadOnlyList<string> lineup)
	{
		int dt = _lastClock < 0 ? 0 : Math.Max(0, clockMs - _lastClock);
		_lastClock = clockMs;
		FieldInfo f = view.Info;
		int step = Math.Max(1, f.LeaderStepMs);
		bool aboard = f.Aboard.Length > 0;
		bool entered = f.AreaId != _area;
		if (entered)
		{
			_area = f.AreaId;
			_enteredMs = clockMs;
			_leader.Snap(f.LeaderX, f.LeaderY);
			Arrive(view, lineup);
			_enemies.Clear();
			_enemyWalks.Clear();
			_enemyRoutes.Clear();
			_actors.Clear();
			_routes.Clear();
			ClearScene();
			// The banner follows the session's arrival announcement (game.json's arrive_notice and the area's own).
			_bannerLeftMs = f.AnnounceArrival ? BannerMs : 0;
			_bannerTitle = f.AreaName;
			_bannerKind = f.AreaKind;
			dt = 0;
		}
		_bannerLeftMs = Math.Max(0, _bannerLeftMs - dt);

		int oldX = _leader.X, oldY = _leader.Y;
		_leader.Advance(dt);
		_ride = f.RidingPlatform.Length > 0 ? Platforms.FirstOrDefault(p => p.Id == f.RidingPlatform) : null;
		// A step led in the area left behind never carries over: the new area shows the leader on its own cell at once.
		(int leaderX, int leaderY, int glideMs, int glideGone) = entered ? (f.LeaderX, f.LeaderY, step, 0) : LeaderStep ?? (f.LeaderX, f.LeaderY, step, 0);
		if (_ride is not null && LeaderStep is null && _leader.Progress >= 1f)
		{
			// The platform carries the party: the leader's cell follows it without a step, and the followers ride along.
			_leader.Snap(f.LeaderX, f.LeaderY);
		}
		else if (!_leader.Go(leaderX, leaderY, glideMs, glideGone) && (_leader.X != oldX || _leader.Y != oldY))
		{
			// A warp or a load within the area: the leader is put down, and the followers arrive with it.
			Arrive(view, lineup);
		}
		_leader.Facing = f.Facing;
		foreach (MemberPlace m in _members.Values)
		{
			m.Walk.Advance(dt);
		}
		List<(int X, int Y)> line = FollowerLine(f);
		// The leader drawn a step ahead of the session (the real-time glide leads it) has left the session's cell already.
		if ((_leader.X, _leader.Y) != (f.LeaderX, f.LeaderY) && Math.Abs(_leader.X - f.LeaderX) + Math.Abs(_leader.Y - f.LeaderY) == 1 && !aboard && _ride is null)
		{
			line.Insert(0, (f.LeaderX, f.LeaderY));
		}
		// A script walking the leader leads the line as the player does: each cell it steps into leaves the one before on
		// the trail, and the followers glide behind at its pace. A scene that places other members too keeps the line
		// where it was, for them to walk back to.
		(int X, int Y) head = (_leader.X, _leader.Y);
		if (lineup.Count > 0 && _members.TryGetValue(lineup[0], out MemberPlace? scripted) && lineup.Skip(1).All(id => !_members.ContainsKey(id)))
		{
			head = scripted.Walk.Cell;
			if ((_scriptedLeaderCell ?? scripted.Walk.Cells[0]) is var before && before != head && Math.Abs(before.X - head.X) + Math.Abs(before.Y - head.Y) == 1)
			{
				_scriptTrail.Insert(0, before);
				if (_scriptTrail.Count > Followers)
				{
					_scriptTrail.RemoveAt(_scriptTrail.Count - 1);
				}
				(glideMs, glideGone) = (scripted.Walk.CellMs, 0);
			}
			_scriptedLeaderCell = head;
			line.InsertRange(0, _scriptTrail);
		}
		else
		{
			_scriptedLeaderCell = null;
			_scriptTrail.Clear();
		}
		for (int i = 0; i < _followers.Length; i++)
		{
			Glide g = _followers[i];
			g.Advance(dt);
			(int tx, int ty) = i < line.Count ? line[i] : line.Count > 0 && !_lineEndsOnLeader ? line[^1] : head;
			if ((g.X, g.Y) == (tx, ty))
			{
				continue;
			}
			if (i >= lineup.Count - 1 || (tx, ty) == head || !g.Go(tx, ty, glideMs, glideGone))
			{
				g.Snap(tx, ty);
				g.Facing = _leader.Facing;
			}
		}

		// Members walking back after a scene go a cell per step to the cell their place in the line holds, around the
		// people drawn and the cells nobody can stand on, or along x and then y when there is no way round. If the
		// leader sets off first, the trail takes each of them over from where it is drawn.
		bool leaderMoved = _leader.X != oldX || _leader.Y != oldY;
		foreach (string id in _returns.Keys.ToList())
		{
			ActorWalk w = _returns[id];
			w.Advance(dt);
			int i = IndexOf(lineup, id) - 1;
			if (i < 0 || i >= _followers.Length)
			{
				_returns.Remove(id);
				continue;
			}
			Glide g = _followers[i];
			if (leaderMoved)
			{
				g.Catch(w.PX, w.PY, step);
				_returns.Remove(id);
			}
			else if (!w.Walking && w.Target == (g.X, g.Y))
			{
				// Back in line, it turns the way the leader faces.
				g.Facing = _leader.Facing;
				_returns.Remove(id);
			}
			else if (!w.Walking)
			{
				if (PathAround(view, Taken(view, lineup, id), w.Target, (g.X, g.Y)) is { } path)
				{
					w.WalkAlong(path, step);
				}
				else
				{
					w.WalkTo(g.X, g.Y, step);
				}
			}
		}

		var seen = new HashSet<int>();
		foreach (FieldEnemy e in view.Enemies)
		{
			seen.Add(e.Id);
			if (!_enemies.TryGetValue(e.Id, out Glide? g))
			{
				g = new Glide();
				g.Snap(e.X, e.Y);
				_enemies[e.Id] = g;
			}
			g.Advance(dt);
			if (_enemyRoutes.Remove(e.Id, out var route) && (g.X, g.Y) != (e.X, e.Y))
			{
				var walk = new ActorWalk(g.X, g.Y);
				walk.WalkAlong(route.Cells.SkipWhile(c => c == (g.X, g.Y)), route.CellMs);
				_enemyWalks[e.Id] = walk;
				g.Snap(e.X, e.Y);
			}
			if (_enemyWalks.TryGetValue(e.Id, out ActorWalk? walking))
			{
				walking.Advance(dt);
				if (!walking.Walking || walking.Target != (e.X, e.Y))
				{
					_enemyWalks.Remove(e.Id);
					g.Snap(e.X, e.Y);
				}
				continue;
			}
			g.Go(e.X, e.Y, step);
		}
		foreach (int gone in _enemies.Keys.Where(k => !seen.Contains(k)).ToList())
		{
			_enemies.Remove(gone);
			_enemyWalks.Remove(gone);
		}

		var present = new HashSet<string>();
		foreach (FieldActor a in view.Actors)
		{
			present.Add(a.Id);
			if (!_actors.TryGetValue(a.Id, out ActorWalk? w))
			{
				_actors[a.Id] = w = new ActorWalk(a.X, a.Y);
			}
			else
			{
				w.Advance(dt);
				if (w.Target != (a.X, a.Y))
				{
					// Only a move the controller paced walks, along its record's route when it has one; a wanderer's step
					// glides over its own time per cell; anything else (a load, a rebuild, a place) puts the actor there.
					(int X, int Y) from = w.Target;
					if (_routes.Remove(a.Id, out List<(int X, int Y)>? route) && route.Count > 0 && route[^1] == (a.X, a.Y) && _paces.Remove(a.Id, out int routePace))
					{
						w.WalkAlong(route.SkipWhile(c => c == from), routePace);
					}
					else if (_paces.Remove(a.Id, out int pace))
					{
						w.WalkTo(a.X, a.Y, pace);
					}
					else if ((a.Wanders || a.Patrols) && WanderPercent(a.Id) is int percent && Math.Abs(a.X - from.X) + Math.Abs(a.Y - from.Y) == 1)
					{
						w.WalkTo(a.X, a.Y, Math.Max(1, Math.Max(1, f.StepMs) * 100 / Math.Max(1, percent)));
					}
					else
					{
						_actors[a.Id] = w = new ActorWalk(a.X, a.Y);
					}
				}
			}
			if (!w.Walking)
			{
				w.Facing = a.Facing;
			}
		}
		foreach (string gone in _actors.Keys.Where(k => !present.Contains(k)).ToList())
		{
			_actors.Remove(gone);
		}
	}

	private static int Axis(float focus, int tile, int areaPx, int viewPx)
	{
		if (areaPx <= viewPx)
		{
			return -(viewPx - areaPx) / 2;
		}
		return Math.Clamp((int)MathF.Round(focus * tile + tile / 2f - viewPx / 2f), 0, areaPx - viewPx);
	}

	// The field as the window shows it, after Track: the whole surface around `focus` (in cells; the leader when null),
	// `cell` pixels a cell, moved by `shake` pixels. The people drawn are what headless expectations read.
	public void Draw(IDrawSurface s, Art art, FieldView view, int clockMs, IReadOnlyList<string> lineup, (float X, float Y)? focus, Point shake, int cell)
	{
		Cell = cell;
		Render(s, art, view, clockMs, lineup, new Rectangle(0, 0, s.Width, s.Height), focus ?? Leader(lineup), cell, shake, record: true);
	}

	// The same live scene inside `viewport`, `tile` pixels a cell, around `focus`: what a panel's camera sees. Nothing
	// is tracked or recorded, so a panel never moves anyone.
	public void DrawView(IDrawSurface s, Art art, FieldView view, int clockMs, IReadOnlyList<string> lineup, Rectangle viewport, (float X, float Y) focus,
		int tile, Point shake) => Render(s, art, view, clockMs, lineup, viewport, focus, tile, shake, record: false);

	private void Render(IDrawSurface s, Art art, FieldView view, int clockMs, IReadOnlyList<string> lineup, Rectangle viewport, (float X, float Y) focus,
		int tile, Point shake, bool record)
	{
		FieldInfo f = view.Info;
		Art.TileSet tiles = art.Tiles(f.AreaKind, f.AreaId);
		MemberPlace? scriptedLeader = lineup.Count > 0 ? _members.GetValueOrDefault(lineup[0]) : null;
		float leaderY = Leader(lineup).Y;
		// Pixels at the field's own scale, scaled to this view's cell size.
		int Px(int pixels) => pixels * tile / Tile;
		int cameraX = Axis(focus.X, tile, f.Width * tile, viewport.Width) + shake.X;
		int cameraY = Axis(focus.Y, tile, f.Height * tile, viewport.Height) + shake.Y;
		if (record)
		{
			CameraX = cameraX;
			CameraY = cameraY;
			DrawnSprites.Clear();
			DrawnOverlays.Clear();
			DrawnAbove = null;
		}
		// Screen positions of a cell's top left corner.
		int ox = viewport.X - cameraX, oy = viewport.Y - cameraY;
		int x0 = Math.Max(0, cameraX / tile - 1), x1 = Math.Min(f.Width - 1, (cameraX + viewport.Width) / tile + 1);
		int y0 = Math.Max(0, cameraY / tile - 1), y1 = Math.Min(f.Height - 1, (cameraY + viewport.Height) / tile + 1);
		int waterFrame = tiles.WaterFrame(clockMs);
		FieldLook look = art.FieldLookOf(f.AreaId);
		AreaPicture? picture = art.AreaPictureOf(f.AreaId);
		AreaMap? map = MapOf(f.AreaId);
		AreaMap? written = picture is null ? null : map;
		LoadedMotion? motion = art.AreaMotionOf(f.AreaId);
		var shown = new List<string>();
		Func<int, int>? paint = Palette(s, motion?.Motion.Palettes ?? [], clockMs, "palettes", shown);
		Func<int, int>? paintTiles = Palette(s, tiles.Palettes, clockMs, "tiles", shown);
		art.MotionTime = w => MotionMs(w, clockMs);
		if (art.SpriteSteps().ToList() is { Count: > 0 } spriteSteps)
		{
			shown.Add("sprites " + string.Join(" ", spriteSteps));
		}
		int Paint(int image) => paint?.Invoke(image) ?? image;
		int behind = motion is null ? 0 : DrawLayers(s, motion, front: false, viewport, cameraX, cameraY, tile, clockMs);

		CellFlags Terrain(int x, int y) => view.Cell(x, y) & CellFlags.TerrainMask;
		void Tiled(int x, int y)
		{
			DrawCell(s, tiles, Terrain, x, y, ox + x * tile, oy + y * tile, Noise.Hash(x, y, f.AreaId.Length), waterFrame, tile, clockMs, look, paintTiles);
			if (view.Cell(x, y).HasFlag(CellFlags.Barrier))
			{
				DrawBarrier(s, ox + x * tile, oy + y * tile, Noise.Hash(x, y, 7), tile);
			}
		}
		if (picture is not null && written is not null && written.Width == f.Width && written.Height == f.Height)
		{
			// The picture shows the map as written. A cell whose flag's change has a picture of its own shows that, the
			// latest set change winning as in the game; any other cell whose terrain is not as written (a change without
			// a picture, a broken barrier) shows its tile.
			s.Image(Paint(picture.Ground), ox, oy, f.Width * tile, f.Height * tile, Color.White);
			var patched = new Dictionary<(int X, int Y), int>();
			foreach ((string flag, IReadOnlyDictionary<(int X, int Y), int> cells) in picture.Patches.Where(p => Flag(p.Flag)))
			{
				foreach ((string _, (int X, int Y)[] changed) in written.Changes.Where(c => c.Flag == flag))
				{
					foreach ((int X, int Y) c in changed.Where(cells.ContainsKey))
					{
						patched[c] = cells[c];
					}
				}
			}
			int fromPatches = 0, fromTiles = 0;
			for (int y = y0; y <= y1; y++)
			{
				for (int x = x0; x <= x1; x++)
				{
					if (patched.TryGetValue((x, y), out int image))
					{
						s.Image(Paint(image), ox + x * tile, oy + y * tile, tile, tile, Color.White);
						fromPatches++;
					}
					else if ((view.Cell(x, y) & AreaMap.TerrainBits) != written.Written[y * f.Width + x])
					{
						Tiled(x, y);
						fromTiles++;
					}
				}
			}
			if (record)
			{
				DrawnPicture = $"{f.AreaId}{(picture.Above is null ? "" : " above")} patched {fromPatches} tiled {fromTiles}";
			}
		}
		else
		{
			picture = null;
			if (record)
			{
				DrawnPicture = "-";
			}
			for (int y = y0; y <= y1; y++)
			{
				for (int x = x0; x <= x1; x++)
				{
					Tiled(x, y);
				}
			}
		}

		var runFrames = new List<string>();
		if (motion is not null)
		{
			DrawRuns(s, motion, above: false, ox, oy, tile, clockMs, Paint, runFrames);
		}

		float pulse = 0.6f + 0.4f * MathF.Sin(clockMs / 260f);
		var exitsDrawn = new List<string>();
		foreach (FieldExit e in view.Exits)
		{
			int sx = ox + e.X * tile, sy = oy + e.Y * tile;
			// A closed exit is drawn as its row's tile (a wall unless the data names another), with a door over a tile that is
			// not a wall: it still blocks.
			if (!e.Open)
			{
				if (e.ClosedTile != '#')
				{
					s.Image(tiles.Door, sx, sy, tile, tile, Color.White);
				}
				exitsDrawn.Add($"{e.X} {e.Y} closed");
				continue;
			}
			int dir = e.X == 0 ? 3 : e.X == f.Width - 1 ? 1 : e.Y == 0 ? 0 : e.Y == f.Height - 1 ? 2 : -1;
			string fires = Art.ExitFiresName(e.Fires);
			if (art.ExitMarker(e.Fires, clockMs) is { } marker)
			{
				s.Image(marker, sx, sy, tile, tile, Color.White);
				exitsDrawn.Add($"{e.X} {e.Y} {fires} marker");
			}
			else if (dir < 0)
			{
				s.Image(tiles.Door, sx, sy, tile, tile, Color.White);
				exitsDrawn.Add($"{e.X} {e.Y} {fires} door");
			}
			else
			{
				s.Fill(sx + Px(4), sy + Px(4), tile - Px(8), tile - Px(8), new Color(120, 240, 160) * (0.18f * pulse));
				s.Image(tiles.ExitArrow[dir], sx, sy, tile, tile, Color.White * pulse);
				exitsDrawn.Add($"{e.X} {e.Y} {fires} arrow");
			}
		}
		if (record)
		{
			DrawnExits = exitsDrawn.Count == 0 ? "none" : string.Join(", ", exitsDrawn);
		}
		// Platforms lie on the ground layer at their pixel places; what stands on them is drawn over them.
		var platformsDrawn = new List<string>();
		foreach (Platform p in Platforms)
		{
			(float px, float py) = PlatformAt(p);
			s.Image(art.Sprite("platform", art.Platform), ox + (int)MathF.Round(px * tile), oy + (int)MathF.Round(py * tile), tile, tile, Color.White);
			platformsDrawn.Add(FormattableString.Invariant($"{p.Id} {px * CellPixels:0.#} {py * CellPixels:0.#}") + (p.Carrying ? " carrying" : ""));
		}
		if (record)
		{
			DrawnPlatforms = platformsDrawn.Count == 0 ? "-" : string.Join(", ", platformsDrawn);
		}

		// Everything standing on the map, drawn top row first so lower sprites overlap higher ones.
		var sprites = new List<(float Y, Action Draw)>();
		// Speech bubbles and alerts, drawn after every sprite so a lower one never covers them.
		var overlays = new List<Action>();
		// Actors standing on their talk points while those have something to say.
		var talkers = new HashSet<string>();
		// A bubble sits on the cell above its talker, so it is left out while the leader stands there.
		bool BubbleFree(int x, int y) => _leader.X != x || _leader.Y != y - 1;
		// Speech bubbles, the `!` marks and pose marks go over the above picture, drawn after it.
		void Overlay(int image, int x, int y, int w, int h)
		{
			overlays.Add(() => s.Image(image, x, y, w, h, Color.White));
			if (record)
			{
				DrawnOverlays.Add(new Rectangle(x, y, w, h));
			}
		}
		// What the next Put stands for, in DrawnSprites when it is not named.
		string putWho = "object";
		void Put(float cx, float cy, Action<int, int> draw, float sortBias = 0f, string? who = null)
		{
			int sx = ox + (int)MathF.Round(cx * tile), sy = oy + (int)MathF.Round(cy * tile);
			if (sx > viewport.X - tile * 2 && sy > viewport.Y - tile * 2 && sx < viewport.Right + tile && sy < viewport.Bottom + tile)
			{
				sprites.Add((cy + sortBias, () => draw(sx, sy)));
				if (record)
				{
					DrawnSprites.Add((who ?? putWho, new Rectangle(sx, sy - tile / 2, tile, tile + tile / 2)));
				}
			}
		}
		// A person, actor or party member, in its pose if it is playing one: the shadow stays on the ground while a
		// pose lifts the body. `extra` draws on top, given the body's screen position. `look` stands in for a person
		// with no id.
		// `figure` is the person whose pictures it wears (a script's `look`), `creatureKey` the encounter whose field figure it
		// wears (an enemy's figure), and `played` the library's animation with how far through it is.
		void Person(string id, float px, float py, Direction facing, int frame, bool walking, float sortBias = 0f, Action<int, int>? extra = null, ActorLook? look = null,
			int raise = 0, string? figure = null, string? creatureKey = null, (string Animation, float T)? played = null)
		{
			string who = figure ?? id;
			bool creature = look is null && (creatureKey is not null || (figure is null && CreatureLook(id)));
			int image = creature ? art.FieldEnemy(creatureKey ?? id, clockMs, 0) : look is null ? art.PersonFrame(who, facing, frame) : art.ActorFrame(look, facing, frame);
			int lift = 0;
			bool alert = false;
			MotionMark[] marks = [];
			Pose pose = Pose.None;
			(float alpha, int dx, float scatter) = (1f, 0, 0f);
			if (look is null && played is { } lib)
			{
				// The library times it: its pose plays stretched over the library's length.
				pose = Art.PoseFor(lib.Animation);
				if (Art.Vanishes(pose) && lib.T >= 1f)
				{
					return;
				}
				int length = art.PoseLengthMs(who, lib.Animation);
				(int posed, lift, alert, marks) = art.PoseFrame(who, facing, lib.Animation, (int)(lib.T * length), false);
				image = creature ? image : posed;
				(alpha, dx, scatter) = Art.SpriteLook(pose, lib.T);
				if (record && art.SheetPose(who, lib.Animation) is { } sheetPose)
				{
					DrawnSheetPoses[id] = sheetPose;
				}
			}
			else if (look is null && PoseAt(id, clockMs, art) is { } run)
			{
				pose = Art.PoseFor(run.Animation);
				int length = art.PoseLengthMs(who, run.Animation);
				if (Art.Vanishes(pose) && !run.Loop && run.ElapsedMs >= length)
				{
					return;
				}
				(int posed, lift, alert, marks) = art.PoseFrame(who, facing, run.Animation, run.ElapsedMs, run.Loop);
				// A creature keeps its own picture; the pose lifts it.
				image = creature ? image : posed;
				(alpha, dx, scatter) = Art.SpriteLook(pose, (run.Loop ? run.ElapsedMs % length : run.ElapsedMs) / (float)Math.Max(1, length));
				if (record && art.SheetPose(who, run.Animation) is { } sheetPose)
				{
					DrawnSheetPoses[id] = sheetPose;
				}
			}
			if (record)
			{
				DrawnActors[id] = new DrawnPerson(px, py, walking, facing, pose, marks.Length > 0);
			}
			lift = Px(lift + raise);
			putWho = id.Length > 0 ? id : "person";
			Put(px, py, (sx, sy) =>
			{
				if (look is not null || creature || art.PersonShadow(who))
				{
					s.Image(art.Shadow, sx, sy + Px(3), tile, tile, Color.White * alpha);
				}
				Rectangle body = art.Stand(image, sx + Px(dx), sy - lift, tile);
				s.Image(image, body.X, body.Y, body.Width, body.Height, Color.White * alpha);
				// Dissolving, the body breaks into motes that rise and fade as it does.
				for (int i = 0; scatter > 0f && i < 10; i++)
				{
					int mx = body.X + body.Width * (i * 37 % 100) / 100;
					int my = body.Bottom - body.Height * (i * 53 % 100) / 100 - (int)(scatter * tile * (0.5f + i % 3 * 0.25f));
					s.Fill(mx, my, Px(3), Px(3), new Color(236, 240, 255) * (1f - scatter));
				}
				if (alert)
				{
					Overlay(art.Alert, sx + tile / 4, sy - lift - tile / 2 - Px(8), tile / 2, tile / 2 + Px(8));
				}
				foreach (MotionMark mark in marks)
				{
					Overlay(mark.Image, sx + mark.Dx * tile / Pixels.Size, sy - lift + mark.Dy * tile / Pixels.Size, tile, tile);
				}
				extra?.Invoke(sx, sy - lift);
			}, sortBias);
		}
		// Belts sharing a cell (one per flag state, so a belt can reverse) draw one: the running one, else the first.
		var belts = new HashSet<FieldPoint>();
		foreach (IGrouping<(int, int), FieldPoint> cell in view.Points.Where(p => p.Kind == PointKind.Conveyor).GroupBy(p => (p.X, p.Y)))
		{
			belts.Add(cell.FirstOrDefault(p => p.Active) ?? cell.First());
		}
		DrawnConveyors = belts.Count;
		RunningConveyors = belts.Count(p => p.Active);
		// A tile step hook fires on every cell its character marks in the map as written, so its rune goes on each of them
		// while its flags hold, as a step hook point's does.
		int tileCells = 0, activeCells = 0;
		foreach (TileHook hook in map?.TileHooks ?? [])
		{
			bool active = (hook.RequireFlag.Length == 0 || Flag(hook.RequireFlag)) && (hook.BlockFlag.Length == 0 || !Flag(hook.BlockFlag));
			tileCells += hook.Cells.Length;
			if (!active)
			{
				continue;
			}
			activeCells += hook.Cells.Length;
			if (!look.TileHooks)
			{
				continue;
			}
			int? overlay = art.TileHookOverlay(clockMs);
			foreach ((int x, int y) in hook.Cells)
			{
				s.Image(overlay ?? tiles.Rune, ox + x * tile, oy + y * tile, tile, tile, overlay is null ? Color.White * (0.35f + 0.3f * pulse) : Color.White);
			}
		}
		if (record)
		{
			DrawnTileHooks = tileCells;
			ActiveTileHooks = activeCells;
		}
		if (record)
		{
			DrawnObjects.Clear();
		}
		IReadOnlyList<LoadedObject> objects = art.ObjectsIn(f.AreaId);
		// An object with a picture of its own: a person's standing frame on its cell, or frames filling its size in cells
		// around the anchor cell, which stands on the object's cell and sorts it by that row.
		void PutObject(LoadedObject o, int x, int y, bool opened)
		{
			ObjectEntry e = o.Entry;
			if (e.Character is { } character)
			{
				int person = art.PersonFrame(character, e.Facing, 0);
				Put(x, y, (sx, sy) =>
				{
					s.Image(art.Shadow, sx, sy + Px(3), tile, tile, Color.White);
					Rectangle body = art.Stand(person, sx, sy, tile);
					s.Image(person, body.X, body.Y, body.Width, body.Height, Color.White);
				});
				if (record)
				{
					DrawnObjects.Add($"{e.Key} {character} {e.Facing.ToString().ToLowerInvariant()}");
				}
				return;
			}
			bool showOpen = opened && e.Open is not null;
			ObjectState state = showOpen ? e.Open! : e.Closed!;
			int frame = state.Run.FrameAt(clockMs, loop: true);
			int image = (showOpen ? o.Open : o.Closed)[frame];
			Put(x, y, (sx, sy) => s.Image(image, sx - state.AnchorX * tile, sy - state.AnchorY * tile, state.Width * tile, state.Height * tile, Color.White));
			if (record)
			{
				DrawnObjects.Add($"{e.Key} {(opened && e.Open is not null ? "open" : "closed")} {state.Width}x{state.Height} frame {frame}");
			}
		}
		foreach (LoadedObject o in objects.Where(o => o.Entry.At is not null))
		{
			PutObject(o, o.Entry.At!.Value.X, o.Entry.At.Value.Y, o.Entry.OpenFlag is { } flag && Flag(flag));
		}
		foreach (FieldPoint p in view.Points)
		{
			putWho = $"{p.Kind.ToString().ToLowerInvariant()}:{p.Id}";
			// An interact hook solid only while its flags hold shows nothing while they do not: the party walks over its cell.
			if (p.Kind == PointKind.Talk && !p.Active && map?.SolidWhileActive.Contains(p.Id) == true)
			{
				continue;
			}
			if (objects.FirstOrDefault(o => o.Entry.Point == p.Id) is { } own)
			{
				bool opened = own.Entry.OpenFlag is { } flag ? Flag(flag) : p.Kind == PointKind.Chest && !p.Active;
				if (p.Kind == PointKind.Port && !p.Active)
				{
					continue;
				}
				PutObject(own, p.X, p.Y, opened);
				if (p.Kind == PointKind.Talk && p.Active && BubbleFree(p.X, p.Y))
				{
					int bob = (int)(2 * MathF.Sin(clockMs / 300f + p.X));
					Overlay(art.Bubble, ox + p.X * tile + tile / 2, oy + p.Y * tile - tile / 2 + bob, tile / 2 + Px(8), tile / 2 + Px(8));
				}
				continue;
			}
			switch (p.Kind)
			{
			case PointKind.Step:
				if (p.Active)
				{
					s.Image(tiles.Rune, ox + p.X * tile, oy + p.Y * tile, tile, tile, Color.White * (0.35f + 0.3f * pulse));
				}
				break;
			case PointKind.Chest:
				Put(p.X, p.Y, (sx, sy) => s.Image(p.Active ? tiles.ChestClosed : tiles.ChestOpen, sx, sy, tile, tile, Color.White));
				break;
			case PointKind.Shop:
				Put(p.X, p.Y, (sx, sy) => s.Image(tiles.Stall, sx, sy, tile, tile, Color.White));
				break;
			case PointKind.Rest:
				Put(p.X, p.Y, (sx, sy) => s.Image(tiles.Bed, sx, sy, tile, tile, Color.White));
				break;
			case PointKind.Port:
				// A port appears once its story opens it.
				if (p.Active)
				{
					Put(p.X, p.Y, (sx, sy) => s.Image(art.Sprite("pier", art.Pier), sx, sy, tile, tile, Color.White));
				}
				break;
			case PointKind.Helm:
				Put(p.X, p.Y, (sx, sy) => DrawHelm(s, sx, sy, tile, pulse));
				break;
			case PointKind.Gangway:
				DrawGangway(s, ox + p.X * tile, oy + p.Y * tile, tile, pulse);
				break;
			case PointKind.Conveyor when !look.Conveyors || !belts.Contains(p):
				break;
			case PointKind.Conveyor when art.ConveyorOverlay(p.Direction, p.Active, clockMs) is int belt:
				s.Image(belt, ox + p.X * tile, oy + p.Y * tile, tile, tile, Color.White);
				break;
			case PointKind.Conveyor:
				DrawConveyor(s, ox + p.X * tile, oy + p.Y * tile, tile, p.Active, p.Direction, clockMs);
				break;
			case PointKind.Talk:
				FieldActor? actor = view.Actors.FirstOrDefault(a => a.Id == p.Id);
				if (actor is null)
				{
					Put(p.X, p.Y, (sx, sy) =>
					{
						s.Image(tiles.Sign, sx, sy, tile, tile, Color.White);
						if (p.Active && BubbleFree(p.X, p.Y))
						{
							int bob = (int)(2 * MathF.Sin(clockMs / 300f + p.X));
							Overlay(art.Bubble, sx + tile / 2, sy - tile / 2 + bob, tile / 2 + Px(8), tile / 2 + Px(8));
						}
					});
				}
				else if (p.Active && actor.Visible && _actors.TryGetValue(actor.Id, out ActorWalk? w) && !w.Walking && (w.Target == (p.X, p.Y) || w.Target == ActorHome(actor.Id)))
				{
					// The actor is drawn below, with the bubble while it stands on its talk point.
					talkers.Add(actor.Id);
				}
				break;
			}
		}
		if (record)
		{
			DrawnActors.Clear();
			DrawnSheetPoses.Clear();
			DrawnVehicles.Clear();
		}
		foreach (FieldVehicle v in view.Vehicles)
		{
			// The ridden vehicle glides with the leader and is drawn over it, the leader raised to sit in the hull.
			(float vx, float vy, Direction facing) = v.Aboard ? (_leader.PX, _leader.PY, f.Facing) : (v.X, v.Y, v.Facing);
			int image = art.VehicleFrame(v.Id, facing, clockMs);
			putWho = "vehicle:" + v.Id;
			Put(vx, vy, (sx, sy) =>
			{
				s.Image(art.Shadow, sx, sy + Px(3), tile, tile, Color.White);
				Rectangle body = art.Stand(image, sx, sy, tile);
				s.Image(image, body.X, body.Y, body.Width, body.Height, Color.White);
			}, v.Aboard ? 0.02f : 0f);
			if (record)
			{
				DrawnVehicles[v.Id] = $"{v.X} {v.Y}" + (v.Aboard ? " aboard" : "");
			}
		}
		foreach (FieldActor a in view.Actors)
		{
			// A removed actor is gone; a hidden one shows only while the animation hiding it plays.
			(string Animation, float T)? played = Played(a);
			if (a.Removal != ActorRemoval.None || !_actors.TryGetValue(a.Id, out ActorWalk? w) ||
				(!a.Visible && !(played is { } hiding && Art.Vanishes(Art.PoseFor(hiding.Animation)))))
			{
				continue;
			}
			// A pose has the person's attention, so the bubble waits until it ends.
			bool talks = talkers.Contains(a.Id) && BubbleFree(a.X, a.Y) && PoseAt(a.Id, clockMs, art) is null && played is null;
			int bob = (int)(2 * MathF.Sin(clockMs / 300f + a.X));
			// The game stands an actor no script has turned facing south; its sheet may say otherwise.
			Direction facing = !w.Walking && FacingOverrides.TryGetValue(a.Id, out Direction turned) ? turned
				: !a.Scripted && art.StartFacing(a.Id) is { } start ? start : w.Facing;
			string? creatureKey = a.SpriteEnemy.Length > 0 ? EnemyEncounter(a.SpriteEnemy) ?? a.SpriteEnemy : null;
			// A path actor is not cell-bound: it stands at its home plus the path's pixel offset.
			(float ax, float ay) = a.OnPath ? (a.HomeX + a.PathX / (float)Math.Max(1, CellPixels), a.HomeY + a.PathY / (float)Math.Max(1, CellPixels)) : (w.PX, w.PY);
			Person(a.Id, ax, ay, facing, w.Frame, w.Walking, extra: talks
				? (sx, sy) => Overlay(art.Bubble, sx + tile / 2, sy - tile / 2 + bob, tile / 2 + Px(8), tile / 2 + Px(8))
				: null, figure: a.Look.Length > 0 ? a.Look : null, creatureKey: creatureKey, played: played);
		}
		if (record)
		{
			DrawnEnemies.Clear();
		}
		foreach (FieldEnemy e in view.Enemies)
		{
			if (!_enemies.TryGetValue(e.Id, out Glide? g))
			{
				continue;
			}
			int creature = art.FieldEnemy(e.Encounter, clockMs, e.Id);
			bool chasing = e.State == EnemyState.Chasing;
			ActorWalk? scripted = _enemyWalks.GetValueOrDefault(e.Id);
			(float ex, float ey) = scripted is null ? (g.PX, g.PY) : (scripted.PX, scripted.PY);
			if (record && e.Actor.Length > 0)
			{
				DrawnEnemies[e.Actor] = scripted is { Walking: true } || g.Progress < 1f ? "walking" : $"{e.X} {e.Y}";
			}
			putWho = "enemy:" + (e.Actor.Length > 0 ? e.Actor : e.Id.ToString(System.Globalization.CultureInfo.InvariantCulture));
			Put(ex, ey, (sx, sy) =>
			{
				s.Image(art.Shadow, sx, sy + Px(2), tile, tile, Color.White);
				Rectangle body = art.Stand(creature, sx, sy, tile);
				s.Image(creature, body.X, body.Y, body.Width, body.Height, Color.White);
				if (chasing)
				{
					Overlay(art.Alert, sx + tile / 4, sy - tile / 2 - Px(8), tile / 2, tile / 2 + Px(8));
				}
			});
		}
		for (int i = Math.Min(_followers.Length, lineup.Count - 1) - 1; i >= 0; i--)
		{
			string id = lineup[i + 1];
			// A follower sorts just above anyone on its own row, so the leader stays in front.
			if (_members.TryGetValue(id, out MemberPlace? m))
			{
				Person(id, m.Walk.PX, m.Walk.PY, m.Walk.Walking ? m.Walk.Facing : m.Facing, m.Walk.Frame, m.Walk.Walking, -0.01f);
				continue;
			}
			if (_returns.TryGetValue(id, out ActorWalk? back))
			{
				Person(id, back.PX, back.PY, back.Facing, back.Frame, back.Walking, -0.01f);
				continue;
			}
			Glide g = _followers[i];
			if (RideAt() is { } riding)
			{
				// Riding, the followers stand on the platform around the leader, behind it.
				(int dx, int dy) = RideSpots[i % RideSpots.Length];
				Person(id, riding.X + dx / (float)Tile, riding.Y + dy / (float)Tile, _leader.Facing, 0, false, -0.01f);
				continue;
			}
			if ((g.X, g.Y) == (_scriptedLeaderCell ?? (_leader.X, _leader.Y)))
			{
				if (record)
				{
					DrawnActors[id] = new DrawnPerson(g.PX, g.PY, false, g.Facing, Pose.None, false, true);
				}
				continue;
			}
			Person(id, g.PX, g.PY, g.Facing, g.Frame, g.Progress < 1f, -0.01f);
		}
		foreach ((string id, MemberPlace m) in _members)
		{
			if (IndexOf(lineup, id) < 0)
			{
				Person(id, m.Walk.PX, m.Walk.PY, m.Walk.Walking ? m.Walk.Facing : m.Facing, m.Walk.Frame, m.Walk.Walking);
			}
		}
		string leaderId = lineup.Count > 0 ? lineup[0] : "";
		ActorLook? noLeader = lineup.Count > 0 ? null : Art.Hero;
		if (scriptedLeader is { } sl)
		{
			Person(leaderId, sl.Walk.PX, sl.Walk.PY, sl.Walk.Walking ? sl.Walk.Facing : sl.Facing, sl.Walk.Frame, sl.Walk.Walking, look: noLeader);
		}
		else
		{
			(float lx, float ly) = RideAt() ?? (_leader.PX, _leader.PY);
			Person(leaderId, lx, ly, _leader.Facing, _leader.Frame, _leader.Progress < 1f, look: noLeader, raise: f.Aboard.Length > 0 ? 5 : 0);
		}
		if (record)
		{
			LeaderScreenY = (int)MathF.Round(leaderY * tile) - CameraY;
		}
		foreach ((float _, Action draw) in sprites.OrderBy(x => x.Y))
		{
			draw();
		}
		if (picture?.Above is int above)
		{
			above = Paint(above);
			s.Image(above, ox, oy, f.Width * tile, f.Height * tile, Color.White);
			if (record)
			{
				DrawnAbove = (above, new Rectangle(ox, oy, f.Width * tile, f.Height * tile));
			}
		}
		if (motion is not null)
		{
			DrawRuns(s, motion, above: true, ox, oy, tile, clockMs, Paint, runFrames);
			int front = DrawLayers(s, motion, front: true, viewport, cameraX, cameraY, tile, clockMs);
			if (motion.Motion.Runs.Count > 0)
			{
				shown.Add("runs " + string.Join(",", runFrames));
			}
			if (motion.Motion.Layers.Count > 0)
			{
				shown.Add($"back {behind} front {front}");
			}
		}
		if (record)
		{
			DrawnMotion = shown.Count == 0 ? "-" : string.Join(" ", shown);
		}
		foreach (Action draw in overlays)
		{
			draw();
		}
	}

	// Where a piece of the area's motion stands: off (null) while its flags keep it off, else its clock's milliseconds.
	private long? MotionMs(MotionWhen w, int clockMs) =>
		w.Flag is { } flag && !Flag(flag) || w.UnlessFlag is { } unless && Flag(unless) ? null : w.Entry ? Math.Max(0, clockMs - _enteredMs) : clockMs;

	// The recolor of the palette tracks that show, the first track naming a color winning, or null when none changes one;
	// each track's step goes into `shown` after `label`.
	private Func<int, int>? Palette(IDrawSurface s, IReadOnlyList<PaletteTrack> tracks, int clockMs, string label, List<string> shown)
	{
		if (tracks.Count == 0)
		{
			return null;
		}
		var pairs = new List<(Color From, Color To)>();
		var steps = new List<string>();
		foreach (PaletteTrack t in tracks)
		{
			if (MotionMs(t.When, clockMs) is not long ms)
			{
				steps.Add("-");
				continue;
			}
			int step = t.StepAt(ms);
			steps.Add(step.ToString(CultureInfo.InvariantCulture));
			pairs.AddRange(t.Steps[step].Where(p => !pairs.Any(q => q.From == p.From)));
		}
		shown.Add(label + " " + string.Join(",", steps));
		if (pairs.Count == 0)
		{
			return null;
		}
		string key = "area_palette:" + string.Join(",", pairs.Select(p => $"{p.From.PackedValue:x}>{p.To.PackedValue:x}"));
		return image => s.Derive(image, key, (px, _, _) => SampleController.Recolor(px, pairs));
	}

	// The frame runs on the ground or over the above picture, recolored as the area's pictures are.
	private void DrawRuns(IDrawSurface s, LoadedMotion motion, bool above, int ox, int oy, int tile, int clockMs, Func<int, int> paint, List<string> frames)
	{
		int cell = motion.Motion.Cell;
		for (int i = 0; i < motion.Motion.Runs.Count; i++)
		{
			AreaRun r = motion.Motion.Runs[i];
			if (r.Above != above)
			{
				continue;
			}
			if (MotionMs(r.When, clockMs) is not long ms)
			{
				frames.Add("-");
				continue;
			}
			int frame = r.FrameAt(ms);
			frames.Add(frame.ToString(CultureInfo.InvariantCulture));
			int x = ox + r.X * tile / cell, y = oy + r.Y * tile / cell;
			s.Image(paint(motion.Runs[i][frame]), x, y, ox + (r.X + r.W) * tile / cell - x, oy + (r.Y + r.H) * tile / cell - y, Color.White);
		}
	}

	// The layers behind the area or in front of everything on it, at the pictures' scale (`tile` screen pixels a cell of
	// the entry's), each moving its scroll factor times the camera plus its speed, repeated across the viewport along the
	// axes it repeats on; bands and a wave over lines move rows apart, a wave over columns columns. How many showed.
	private int DrawLayers(IDrawSurface s, LoadedMotion motion, bool front, Rectangle viewport, int cameraX, int cameraY, int tile, int clockMs)
	{
		double k = tile / (double)motion.Motion.Cell;
		int drawn = 0;
		foreach (LoadedLayer loaded in motion.Layers.Where(l => l.Layer.Front == front))
		{
			AreaLayer l = loaded.Layer;
			if (MotionMs(l.When, clockMs) is not long ms)
			{
				continue;
			}
			drawn++;
			double sec = ms / 1000.0, w = l.Width * k, h = l.Height * k;
			double Origin(int view, int camera, float scroll, float speed, float at) => view - camera * (double)scroll + (speed * sec + at) * k;
			// A wave or a band can move a row or column out by up to a copy, so one more copy is drawn on each side.
			int extra = loaded.Strips.Length > 0 ? 1 : 0;
			IEnumerable<double> Copies(double start, double size, int from, int length, bool repeat)
			{
				if (!repeat)
				{
					yield return start;
					yield break;
				}
				double first = start - Math.Ceiling((start - from) / size) * size - extra * size;
				for (double c = first; c < from + length + extra * size; c += size)
				{
					yield return c;
				}
			}
			int Wave(int index) => l.Wave is { } wave ? (int)Math.Round(wave.Amplitude * Math.Sin(2 * Math.PI * (index - wave.Speed * sec) / wave.Wavelength)) : 0;
			void Draw(int image, double x, double y, double dw, double dh)
			{
				int left = (int)Math.Round(x), top = (int)Math.Round(y), right = (int)Math.Round(x + dw), bottom = (int)Math.Round(y + dh);
				if (right > viewport.Left && left < viewport.Right && bottom > viewport.Top && top < viewport.Bottom && right > left && bottom > top)
				{
					s.Image(image, left, top, right - left, bottom - top, Color.White);
				}
			}
			double x0 = Origin(viewport.X, cameraX, l.ScrollX, l.SpeedX, l.AtX), y0 = Origin(viewport.Y, cameraY, l.ScrollY, l.SpeedY, l.AtY);
			if (loaded.Strips.Length == 0)
			{
				foreach (double y in Copies(y0, h, viewport.Y, viewport.Height, l.RepeatY))
				{
					foreach (double x in Copies(x0, w, viewport.X, viewport.Width, l.RepeatX))
					{
						Draw(loaded.Image, x, y, w, h);
					}
				}
			}
			else if (l.Wave is { Columns: true })
			{
				for (int c = 0; c < l.Width; c++)
				{
					foreach (double x in Copies(x0, w, viewport.X, viewport.Width, l.RepeatX))
					{
						foreach (double y in Copies(y0 + Wave(c) * k, h, viewport.Y, viewport.Height, l.RepeatY))
						{
							Draw(loaded.Strips[c], x + c * k, y, k, h);
						}
					}
				}
			}
			else
			{
				for (int r = 0; r < l.Height; r++)
				{
					double rowX = (l.Bands.FirstOrDefault(b => b.First <= r && r <= b.Last) is { } band ? Origin(viewport.X, cameraX, band.Scroll, band.Speed, l.AtX) : x0) + Wave(r) * k;
					foreach (double y in Copies(y0, h, viewport.Y, viewport.Height, l.RepeatY))
					{
						foreach (double x in Copies(rowX, w, viewport.X, viewport.Width, l.RepeatX))
						{
							Draw(loaded.Strips[r], x, y + r * k, w, k);
						}
					}
				}
			}
		}
		return drawn;
	}

	// `look` turns off the walls' shadows and the water's foam; `clockMs` steps the tileset's animated tiles.
	public static void DrawCell(IDrawSurface s, Art.TileSet t, Func<int, int, CellFlags> terrain, int x, int y, int sx, int sy, uint hash, int waterFrame, int tile = Tile,
		int clockMs = 0, FieldLook? look = null, Func<int, int>? paint = null)
	{
		int Paint(int image) => paint?.Invoke(image) ?? image;
		int Px(int pixels) => pixels * tile / Tile;
		bool shadows = look?.WallShadows ?? true, foam = look?.WaterFoam ?? true;
		CellFlags c = terrain(x, y);
		int edge = Px(3);
		if (c == CellFlags.Wall)
		{
			bool faceDown = terrain(x, y + 1) != CellFlags.Wall;
			if (faceDown)
			{
				s.Image(Paint(t.Frame(Art.TileSet.Variant(t.WallFace, hash, t.WallFaceAltOneIn), clockMs)), sx, sy, tile, tile, Color.White);
			}
			else
			{
				s.Image(Paint(t.Frame(Art.TileSet.Variant(t.WallTop, hash, t.WallTopAltOneIn), clockMs)), sx, sy, tile, tile, Color.White);
				if (terrain(x, y - 1) != CellFlags.Wall)
				{
					s.Fill(sx, sy, tile, edge, t.Edge);
				}
			}
			if (terrain(x - 1, y) != CellFlags.Wall)
			{
				s.Fill(sx, sy, edge, tile, t.Edge);
			}
			if (terrain(x + 1, y) != CellFlags.Wall)
			{
				s.Fill(sx + tile - edge, sy, edge, tile, t.Edge);
			}
			return;
		}
		if (c == CellFlags.Water)
		{
			s.Image(Paint(t.Water[(waterFrame + (int)(hash % 2)) % t.Water.Length]), sx, sy, tile, tile, Color.White);
			if (!foam)
			{
				return;
			}
			Color spray = new Color(230, 244, 255) * 0.7f;
			if (terrain(x, y - 1) != CellFlags.Water)
			{
				s.Fill(sx, sy, tile, Px(5), t.Bank);
				s.Fill(sx, sy + Px(5), tile, Px(2), spray);
			}
			if (terrain(x - 1, y) != CellFlags.Water)
			{
				s.Fill(sx, sy, Px(2), tile, spray);
			}
			if (terrain(x + 1, y) != CellFlags.Water)
			{
				s.Fill(sx + tile - Px(2), sy, Px(2), tile, spray);
			}
			if (terrain(x, y + 1) != CellFlags.Water)
			{
				s.Fill(sx, sy + tile - Px(2), tile, Px(2), spray);
			}
			return;
		}
		if (c == CellFlags.Soft)
		{
			// Soft ground: sand only a vehicle crosses, rippled.
			s.Fill(sx, sy, tile, tile, new Color(214, 186, 128));
			for (int i = 0; i < 3; ++i)
			{
				int ry = Px(8 + i * 14 + (int)((hash >> i) % 4));
				s.Fill(sx + Px(4 + (int)((hash >> (i + 3)) % 8)), sy + ry, tile / 2, Px(2), new Color(176, 146, 92));
			}
			return;
		}
		// Floor: one of the variants, with an occasional decoration, from the cell's coordinates.
		int image = hash % (uint)t.DecorOneIn == 0 ? t.Decor[(hash >> 8) % (uint)t.Decor.Length] : t.Floor[(hash >> 4) % (uint)t.Floor.Length];
		s.Image(Paint(t.Frame(image, clockMs)), sx, sy, tile, tile, Color.White);
		if (!shadows)
		{
			return;
		}
		if (terrain(x, y - 1) == CellFlags.Wall)
		{
			s.Fill(sx, sy, tile, Px(9), Color.Black * 0.28f);
		}
		if (terrain(x - 1, y) == CellFlags.Wall)
		{
			s.Fill(sx, sy, Px(6), tile, Color.Black * 0.16f);
		}
	}

	// The area's map as written, from its kind's generated tiles at `cell` pixels a cell, the water at its first frame:
	// the export's starting point for a whole-area picture.
	public static Color[] WrittenPixels(AreaMap map, AreaKind kind, string area, int cell)
	{
		var canvas = new CanvasSurface(map.Width * cell, map.Height * cell);
		Art.TileSet tiles = new Art(canvas).Tiles(kind, area);
		CellFlags Terrain(int x, int y) => x < 0 || y < 0 || x >= map.Width || y >= map.Height ? CellFlags.Wall : map.Written[y * map.Width + x] & CellFlags.TerrainMask;
		for (int y = 0; y < map.Height; y++)
		{
			for (int x = 0; x < map.Width; x++)
			{
				DrawCell(canvas, tiles, Terrain, x, y, x * cell, y * cell, Noise.Hash(x, y, area.Length), 0, cell);
				if (map.Written[y * map.Width + x].HasFlag(CellFlags.Barrier))
				{
					DrawBarrier(canvas, x * cell, y * cell, Noise.Hash(x, y, 7), cell);
				}
			}
		}
		return canvas.Pixels;
	}

	// A barrier wall a vehicle can break: the wall drawn under it, frosted, with cracks.
	private static void DrawBarrier(IDrawSurface s, int sx, int sy, uint hash, int tile)
	{
		int Px(int pixels) => pixels * tile / Tile;
		s.Fill(sx + Px(2), sy + Px(2), tile - Px(4), tile - Px(4), new Color(200, 236, 255) * 0.55f);
		Color crack = new Color(70, 110, 150) * 0.9f;
		int cx = Px(10 + (int)(hash % 20));
		s.Fill(sx + cx, sy + Px(6), Px(2), tile / 2, crack);
		s.Fill(sx + cx - Px(8), sy + tile / 2, Px(10), Px(2), crack);
		s.Fill(sx + Px(4), sy + tile - Px(10), tile - Px(8), Px(2), new Color(240, 250, 255) * 0.8f);
	}

	// A ship's helm: a post with a lit console.
	private static void DrawHelm(IDrawSurface s, int sx, int sy, int tile, float pulse)
	{
		int Px(int pixels) => pixels * tile / Tile;
		s.Fill(sx + Px(12), sy + Px(18), tile - Px(24), tile - Px(22), new Color(92, 62, 38));
		s.Fill(sx + Px(6), sy + Px(8), tile - Px(12), Px(14), new Color(60, 44, 30));
		s.Fill(sx + Px(9), sy + Px(11), tile - Px(18), Px(8), new Color(250, 214, 120) * (0.6f + 0.4f * pulse));
	}

	// The gangway: planks over the deck's edge, back ashore.
	private static void DrawGangway(IDrawSurface s, int sx, int sy, int tile, float pulse)
	{
		int Px(int pixels) => pixels * tile / Tile;
		for (int i = 0; i < 4; ++i)
		{
			s.Fill(sx + Px(4), sy + Px(4 + i * 10), tile - Px(8), Px(7), new Color(150, 110, 70));
		}
		s.Fill(sx, sy, Px(3), tile, new Color(120, 240, 160) * (0.3f * pulse));
	}

	// Belt slats that run toward the belt's direction while it is on.
	private static void DrawConveyor(IDrawSurface s, int sx, int sy, int tile, bool running, Direction direction, int clockMs)
	{
		int Px(int pixels) => pixels * tile / Tile;
		s.Fill(sx + Px(2), sy + Px(2), tile - Px(4), tile - Px(4), new Color(70, 70, 80));
		int shift = running ? clockMs / 40 % 12 : 0;
		bool across = direction is Direction.East or Direction.West;
		int sign = direction is Direction.East or Direction.South ? 1 : -1;
		for (int i = 0; i < 4; ++i)
		{
			int along = ((i * 12 + sign * shift) % 48 + 48) % 48;
			if (across)
			{
				s.Fill(sx + Px(along), sy + Px(6), Px(4), tile - Px(12), new Color(190, 180, 120));
			}
			else
			{
				s.Fill(sx + Px(6), sy + Px(along), tile - Px(12), Px(4), new Color(190, 180, 120));
			}
		}
	}
}
