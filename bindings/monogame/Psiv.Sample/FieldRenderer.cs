using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Draws a FieldView: a camera that follows the leader, tiles chosen from neighbors and cell coordinates,
// and actors that glide between cells. The game moves everyone a whole cell at a time; the glides only
// change what is drawn between those moves, over the area's step time, so game timing is untouched.
public sealed class FieldRenderer
{
	private const int Tile = Art.Tile;
	private const int BannerMs = 2600;
	private const int WaterFrameMs = 220;
	private const int Followers = 3;

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

		// Returns true when it started a one-cell move.
		public bool Go(int x, int y, int lengthMs)
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
			_elapsedMs = 0;
			_lengthMs = lengthMs;
			IdleMs = 0;
			Steps++;
			return true;
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
		public bool Walking => _elapsedMs < (_cells.Count - 1) * _cellMs;
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
			if (!Walking)
			{
				_cells.RemoveRange(0, _cells.Count - 1);
				_elapsedMs = 0;
				_cellMs = Math.Max(1, cellMs);
			}
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

	private string _area = "";
	private int _lastClock = -1;
	private readonly Glide _leader = new();
	private readonly Glide[] _followers = [new(), new(), new()];
	private readonly List<(int X, int Y)> _trail = [];
	private readonly Dictionary<int, Glide> _enemies = [];
	private readonly Dictionary<string, ActorWalk> _actors = [];
	// Milliseconds per cell for each actor's next walk, from its move's speed.
	private readonly Dictionary<string, int> _paces = [];
	private int _bannerLeftMs;
	private string _bannerTitle = "";
	private string _bannerKind = "";

	public int CameraX { get; private set; }
	public int CameraY { get; private set; }
	// Where the leader was last drawn, in screen pixels (its top edge).
	public int LeaderScreenY { get; private set; }

	// The actors drawn in the last frame: where, and whether they were between cells.
	public Dictionary<string, (float X, float Y, bool Walking)> DrawnActors { get; } = [];

	// The cell an actor is drawn at or walking to, if it is on screen in this area.
	public (int X, int Y)? ActorCell(string id) => _actors.TryGetValue(id, out ActorWalk? w) ? w.Target : null;

	// The time per cell of the actor's next walk; without one the actor is put at its new cell at once.
	public void Pace(string id, int cellMs) => _paces[id] = cellMs;

	public static string KindName(AreaKind kind) => kind switch { AreaKind.Town => "Town", AreaKind.Dungeon => "Dungeon", _ => "Field" };

	// Follows the view's positions; `clockMs` is the controller's clock, so the glides advance with the
	// frame time the session got. `lineup` is the party in walking order (the leader first).
	private void Track(FieldView view, int clockMs, IReadOnlyList<string> lineup)
	{
		int dt = _lastClock < 0 ? 0 : Math.Max(0, clockMs - _lastClock);
		_lastClock = clockMs;
		FieldInfo f = view.Info;
		int step = Math.Max(1, f.StepMs);
		if (f.AreaId != _area)
		{
			_area = f.AreaId;
			_leader.Snap(f.LeaderX, f.LeaderY);
			_trail.Clear();
			foreach (Glide g in _followers)
			{
				g.Snap(f.LeaderX, f.LeaderY);
				g.Facing = f.Facing;
			}
			_enemies.Clear();
			_actors.Clear();
			_bannerLeftMs = BannerMs;
			_bannerTitle = f.AreaName;
			_bannerKind = KindName(f.AreaKind);
			dt = 0;
		}
		_bannerLeftMs = Math.Max(0, _bannerLeftMs - dt);

		int oldX = _leader.X, oldY = _leader.Y;
		_leader.Advance(dt);
		if (_leader.Go(f.LeaderX, f.LeaderY, step))
		{
			_trail.Insert(0, (oldX, oldY));
			if (_trail.Count > Followers)
			{
				_trail.RemoveAt(_trail.Count - 1);
			}
		}
		else if (_leader.X != oldX || _leader.Y != oldY)
		{
			_trail.Clear();
		}
		_leader.Facing = f.Facing;
		for (int i = 0; i < _followers.Length; i++)
		{
			Glide g = _followers[i];
			g.Advance(dt);
			(int tx, int ty) = i < _trail.Count ? _trail[i] : (_leader.X, _leader.Y);
			if (i >= lineup.Count - 1 || i >= _trail.Count)
			{
				if (g.X != tx || g.Y != ty)
				{
					g.Snap(tx, ty);
					g.Facing = _leader.Facing;
				}
				continue;
			}
			g.Go(tx, ty, step);
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
			g.Go(e.X, e.Y, step);
		}
		foreach (int gone in _enemies.Keys.Where(k => !seen.Contains(k)).ToList())
		{
			_enemies.Remove(gone);
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
					// Only a move the controller paced walks; anything else (a load, a rebuild) puts the actor there.
					if (_paces.Remove(a.Id, out int pace))
					{
						w.WalkTo(a.X, a.Y, pace);
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

	private static int Axis(float leader, int areaPx, int viewPx)
	{
		if (areaPx <= viewPx)
		{
			return -(viewPx - areaPx) / 2;
		}
		return Math.Clamp((int)MathF.Round(leader * Tile + Tile / 2f - viewPx / 2f), 0, areaPx - viewPx);
	}

	public void Draw(IDrawSurface s, Art art, FieldView view, int clockMs, IReadOnlyList<string> lineup)
	{
		Track(view, clockMs, lineup);
		FieldInfo f = view.Info;
		Art.TileSet tiles = art.Tiles(Art.ThemeOf(f.AreaKind));
		CameraX = Axis(_leader.PX, f.Width * Tile, s.Width);
		CameraY = Axis(_leader.PY, f.Height * Tile, s.Height);
		int x0 = Math.Max(0, CameraX / Tile - 1), x1 = Math.Min(f.Width - 1, (CameraX + s.Width) / Tile + 1);
		int y0 = Math.Max(0, CameraY / Tile - 1), y1 = Math.Min(f.Height - 1, (CameraY + s.Height) / Tile + 1);
		int waterFrame = clockMs / WaterFrameMs % Art.WaterFrames;

		CellFlags Terrain(int x, int y) => view.Cell(x, y) & CellFlags.TerrainMask;
		for (int y = y0; y <= y1; y++)
		{
			for (int x = x0; x <= x1; x++)
			{
				DrawCell(s, tiles, Terrain, x, y, x * Tile - CameraX, y * Tile - CameraY, Noise.Hash(x, y, f.AreaId.Length), waterFrame);
			}
		}

		float pulse = 0.6f + 0.4f * MathF.Sin(clockMs / 260f);
		foreach (FieldExit e in view.Exits)
		{
			int sx = e.X * Tile - CameraX, sy = e.Y * Tile - CameraY;
			int dir = e.X == 0 ? 3 : e.X == f.Width - 1 ? 1 : e.Y == 0 ? 0 : e.Y == f.Height - 1 ? 2 : -1;
			if (dir < 0)
			{
				s.Image(art.Door, sx, sy, Tile, Tile, Color.White);
			}
			else
			{
				s.Fill(sx + 4, sy + 4, Tile - 8, Tile - 8, new Color(120, 240, 160) * (0.18f * pulse));
				s.Image(art.ExitArrow[dir], sx, sy, Tile, Tile, Color.White * pulse);
			}
		}

		// Everything standing on the map, drawn top row first so lower sprites overlap higher ones.
		var sprites = new List<(float Y, Action Draw)>();
		// Speech bubbles and alerts, drawn after every sprite so a lower one never covers them.
		var overlays = new List<Action>();
		// Actors standing on their talk points while those have something to say.
		var talkers = new HashSet<string>();
		// A bubble sits on the cell above its talker, so it is left out while the leader stands there.
		bool BubbleFree(int x, int y) => _leader.X != x || _leader.Y != y - 1;
		void Put(float cx, float cy, Action<int, int> draw)
		{
			int sx = (int)MathF.Round(cx * Tile) - CameraX, sy = (int)MathF.Round(cy * Tile) - CameraY;
			if (sx > -Tile * 2 && sy > -Tile * 2 && sx < s.Width + Tile && sy < s.Height + Tile)
			{
				sprites.Add((cy, () => draw(sx, sy)));
			}
		}
		foreach (FieldPoint p in view.Points)
		{
			switch (p.Kind)
			{
			case PointKind.Step:
				if (p.Active)
				{
					s.Image(art.Rune, p.X * Tile - CameraX, p.Y * Tile - CameraY, Tile, Tile, Color.White * (0.35f + 0.3f * pulse));
				}
				break;
			case PointKind.Chest:
				Put(p.X, p.Y, (sx, sy) => s.Image(p.Active ? art.ChestClosed : art.ChestOpen, sx, sy, Tile, Tile, Color.White));
				break;
			case PointKind.Shop:
				Put(p.X, p.Y, (sx, sy) => s.Image(art.Stall, sx, sy, Tile, Tile, Color.White));
				break;
			case PointKind.Rest:
				Put(p.X, p.Y, (sx, sy) => s.Image(art.Bed, sx, sy, Tile, Tile, Color.White));
				break;
			case PointKind.Talk:
				FieldActor? actor = view.Actors.FirstOrDefault(a => a.Id == p.Id);
				if (actor is null)
				{
					Put(p.X, p.Y, (sx, sy) =>
					{
						s.Image(art.Noticeboard, sx, sy, Tile, Tile, Color.White);
						if (p.Active && BubbleFree(p.X, p.Y))
						{
							int bob = (int)(2 * MathF.Sin(clockMs / 300f + p.X));
							overlays.Add(() => s.Image(art.Bubble, sx + Tile / 2, sy - Tile / 2 + bob, Tile / 2 + 8, Tile / 2 + 8, Color.White));
						}
					});
				}
				else if (p.Active && actor.Visible && _actors.TryGetValue(actor.Id, out ActorWalk? w) && !w.Walking && w.Target == (p.X, p.Y))
				{
					// The actor is drawn below, with the bubble while it stands on its talk point.
					talkers.Add(actor.Id);
				}
				break;
			}
		}
		DrawnActors.Clear();
		foreach (FieldActor a in view.Actors)
		{
			if (!a.Visible || !_actors.TryGetValue(a.Id, out ActorWalk? w))
			{
				continue;
			}
			ActorLook look = Art.LookFor(a.Id);
			Direction facing = w.Facing;
			int frame = w.Frame;
			bool talks = talkers.Contains(a.Id) && BubbleFree(a.X, a.Y);
			DrawnActors[a.Id] = (w.PX, w.PY, w.Walking);
			Put(w.PX, w.PY, (sx, sy) =>
			{
				s.Image(art.Shadow, sx, sy + 3, Tile, Tile, Color.White);
				s.Image(art.ActorFrame(look, facing, frame), sx, sy, Tile, Tile, Color.White);
				if (talks)
				{
					int bob = (int)(2 * MathF.Sin(clockMs / 300f + a.X));
					overlays.Add(() => s.Image(art.Bubble, sx + Tile / 2, sy - Tile / 2 + bob, Tile / 2 + 8, Tile / 2 + 8, Color.White));
				}
			});
		}
		foreach (FieldEnemy e in view.Enemies)
		{
			if (!_enemies.TryGetValue(e.Id, out Glide? g))
			{
				continue;
			}
			uint seed = Noise.Hash(e.Encounter);
			int frame = (clockMs / 280 + e.Id) % 2;
			bool chasing = e.State == EnemyState.Chasing;
			Put(g.PX, g.PY, (sx, sy) =>
			{
				s.Image(art.Shadow, sx, sy + 2, Tile, Tile, Color.White);
				s.Image(art.Creature(seed, frame), sx, sy, Tile, Tile, Color.White);
				if (chasing)
				{
					overlays.Add(() => s.Image(art.Alert, sx + Tile / 4, sy - Tile / 2 - 8, Tile / 2, Tile / 2 + 8, Color.White));
				}
			});
		}
		for (int i = Math.Min(_followers.Length, lineup.Count - 1) - 1; i >= 0; i--)
		{
			Glide g = _followers[i];
			if (g.X == _leader.X && g.Y == _leader.Y)
			{
				continue;
			}
			ActorLook look = Art.LookFor(lineup[i + 1]);
			Direction facing = g.Facing;
			int frame = g.Frame;
			// A follower sorts just above anyone on its own row, so the leader stays in front.
			Put(g.PX, g.PY - 0.01f, (sx, sy) =>
			{
				s.Image(art.Shadow, sx, sy + 3, Tile, Tile, Color.White);
				s.Image(art.ActorFrame(look, facing, frame), sx, sy, Tile, Tile, Color.White);
			});
		}
		Direction leaderFacing = _leader.Facing;
		int leaderFrame = _leader.Frame;
		ActorLook leaderLook = lineup.Count > 0 ? Art.LookFor(lineup[0]) : Art.Hero;
		Put(_leader.PX, _leader.PY, (sx, sy) =>
		{
			s.Image(art.Shadow, sx, sy + 3, Tile, Tile, Color.White);
			s.Image(art.ActorFrame(leaderLook, leaderFacing, leaderFrame), sx, sy, Tile, Tile, Color.White);
		});
		LeaderScreenY = (int)MathF.Round(_leader.PY * Tile) - CameraY;
		foreach ((float _, Action draw) in sprites.OrderBy(x => x.Y))
		{
			draw();
		}
		foreach (Action draw in overlays)
		{
			draw();
		}
	}

	public static void DrawCell(IDrawSurface s, Art.TileSet t, Func<int, int, CellFlags> terrain, int x, int y, int sx, int sy, uint hash, int waterFrame)
	{
		CellFlags c = terrain(x, y);
		const int edge = 3;
		if (c == CellFlags.Wall)
		{
			bool faceDown = terrain(x, y + 1) != CellFlags.Wall;
			if (faceDown)
			{
				s.Image(t.WallFace[hash % 5 == 0 ? 1 : 0], sx, sy, Tile, Tile, Color.White);
			}
			else
			{
				s.Image(t.WallTop[hash % 3 == 0 ? 1 : 0], sx, sy, Tile, Tile, Color.White);
				if (terrain(x, y - 1) != CellFlags.Wall)
				{
					s.Fill(sx, sy, Tile, edge, t.Edge);
				}
			}
			if (terrain(x - 1, y) != CellFlags.Wall)
			{
				s.Fill(sx, sy, edge, Tile, t.Edge);
			}
			if (terrain(x + 1, y) != CellFlags.Wall)
			{
				s.Fill(sx + Tile - edge, sy, edge, Tile, t.Edge);
			}
			return;
		}
		if (c == CellFlags.Water)
		{
			s.Image(t.Water[(waterFrame + (int)(hash % 2)) % Art.WaterFrames], sx, sy, Tile, Tile, Color.White);
			Color foam = new Color(230, 244, 255) * 0.7f;
			if (terrain(x, y - 1) != CellFlags.Water)
			{
				s.Fill(sx, sy, Tile, 5, t.Bank);
				s.Fill(sx, sy + 5, Tile, 2, foam);
			}
			if (terrain(x - 1, y) != CellFlags.Water)
			{
				s.Fill(sx, sy, 2, Tile, foam);
			}
			if (terrain(x + 1, y) != CellFlags.Water)
			{
				s.Fill(sx + Tile - 2, sy, 2, Tile, foam);
			}
			if (terrain(x, y + 1) != CellFlags.Water)
			{
				s.Fill(sx, sy + Tile - 2, Tile, 2, foam);
			}
			return;
		}
		// Floor: one of four variants, with an occasional decoration, from the cell's coordinates.
		int image = hash % 11 == 0 ? t.Decor[(hash >> 8) % (uint)t.Decor.Length] : t.Floor[(hash >> 4) % (uint)t.Floor.Length];
		s.Image(image, sx, sy, Tile, Tile, Color.White);
		if (terrain(x, y - 1) == CellFlags.Wall)
		{
			s.Fill(sx, sy, Tile, 9, Color.Black * 0.28f);
		}
		if (terrain(x - 1, y) == CellFlags.Wall)
		{
			s.Fill(sx, sy, 6, Tile, Color.Black * 0.16f);
		}
	}

	// The area's name, shown for a moment after arriving.
	public void DrawBanner(IDrawSurface s)
	{
		if (_bannerLeftMs <= 0)
		{
			return;
		}
		int shown = BannerMs - _bannerLeftMs;
		float alpha = Math.Clamp(Math.Min(shown / 300f, _bannerLeftMs / 500f), 0f, 1f);
		int w = Math.Max(s.Measure(_bannerTitle, Style.Title), s.Measure(_bannerKind, Style.Small)) + 80;
		int x = (s.Width - w) / 2;
		int y = 70 - (int)((1f - alpha) * 16);
		Style.Box(s, x, y, w, 86, alpha);
		s.Text(_bannerKind, (s.Width - s.Measure(_bannerKind, Style.Small)) / 2, y + 14, Style.Dim * alpha, Style.Small);
		s.Text(_bannerTitle, (s.Width - s.Measure(_bannerTitle, Style.Title)) / 2, y + 38, Style.Accent * alpha, Style.Title);
	}
}
