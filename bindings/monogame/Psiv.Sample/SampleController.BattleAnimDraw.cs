using System.Globalization;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Drawing the scripts: each figure's offset, pose and color from its tracks, states and reactions, the sprites, the
// shake and the fills. Every value comes from script time, so a seek draws what playing there would have drawn.
public sealed partial class SampleController
{
	private string _probeTransition = "-";
	private string _probeBackdropFx = "none";

	// What the stage needs to place what scripts draw.
	private sealed class StageCtx
	{
		public BattleStep? Step;
		public long Elapsed;
		public bool Front;
		public float Motion;
		public bool Reduced;
		public int PartyCount;
		public string Vehicle = "";
		public IReadOnlyList<Combatant> All = [];
		public Func<int, (int X, int Foot, int Size)> Where = _ => (0, 0, 0);
		public Dictionary<int, Rectangle> Bodies = [];
		public Dictionary<int, Rectangle> Anchors = [];
		public int EffectScale;
		public int EffectScaleLarge;
		public int StageBottom;
		public int Width;
		public int Height;
		public Func<int, bool> Behind = _ => false;
	}

	// A figure as its scripts leave it at a frame.
	private sealed class FigureLook
	{
		public float Fwd;
		public float Sx;
		public float Sy;
		public float Lift;
		public float AddX;
		public float AddY;
		public string Pose = "idle";
		public int PoseMs = -1;
		public bool PoseLoop;
		public string? LookPose;
		public int Frame = -1;
		public Color Tint = Color.White;
		public float Alpha = 1f;
		public Color Add = Color.Transparent;
		public IReadOnlyList<(Color From, Color To)>? Palette;
		public bool Hidden;
		public int PlaceOf = -1;
		public float PlaceDx;
		public float PlaceDy;
		public float Crossfade = -1f;
		public JsonObject? Defeat;
		public float DefeatT;
		public long DefeatLocal;
		public string Life = "";
	}

	private Color AnimColor(string name, ActionLook? look)
	{
		if (name == "$look.element_color")
		{
			string effect = look?.Effect ?? "";
			return Art.ElementColor(effect.Contains('_') ? effect[(effect.IndexOf('_') + 1)..] : "none");
		}
		if (name.StartsWith('#'))
		{
			return BattleAnimationEval.ParseColor(name, _ => null);
		}
		return Layout.Styles.Colors.TryGetValue(name, out Color c) ? c : name switch
		{
			"text" => Style.Text, "dim" => Style.Dim, "accent" => Style.Accent, "bad" => Style.Bad, "good" => Style.Good, "tp" => Style.Tp, _ => Color.White,
		};
	}

	// The scripts that draw this frame: the turn's held script (figures only), the step's, and the reactions started.
	private static IEnumerable<(AnimRun Run, long Now, bool FiguresOnly)> Runs(BattleStep? step, long elapsed)
	{
		if (step is null)
		{
			yield break;
		}
		if (step.Held is { } held && held != step.Run)
		{
			yield return (held, step.HeldAt, true);
		}
		if (step.Run is { } run)
		{
			yield return (run, step.At + elapsed, false);
		}
		foreach (Reaction r in step.Reactions.Where(r => elapsed >= r.Start))
		{
			yield return (r.Run, elapsed - r.Start, false);
		}
	}

	private IEnumerable<string> Scopes(int i, StageCtx c)
	{
		Combatant who = c.All[i];
		string id = who.Side == Side.Enemy ? _show.All[i].Id : who.Id;
		if (who.Side == Side.Enemy)
		{
			return [$"enemy:{id}", "enemy"];
		}
		return id == c.Vehicle ? [$"vehicle:{id}", "vehicle", $"party:{id}", "party"] : [$"party:{id}", "party"];
	}

	private sealed record MoveItem(JsonObject Def, long Start, long Length, AnimRun Run);

	private static float ItemT(MoveItem m, long now) => m.Length <= 0 ? 1f : BattleClock.Ratio(now - m.Start, m.Length);

	// The chained moves on one figure: each goes from where the ones before left it at its start to its own end.
	private (float F, float X, float Y, float L) Chain(List<MoveItem> items, int k, long now, int i, StageCtx c)
	{
		if (k < 0)
		{
			return (0f, 0f, 0f, 0f);
		}
		MoveItem m = items[k];
		if (now < m.Start)
		{
			return Chain(items, k - 1, now, i, c);
		}
		(float F, float X, float Y, float L) prev = Chain(items, k - 1, m.Start, i, c);
		float t = ItemT(m, now);
		// A `sine` move ends where it started.
		bool done = t >= 1f;
		bool back = BattleAnimationRun.Str(m.Def["ease"]) == "sine";
		JsonObject d = m.Def;
		bool front = c.Front;
		float speed = (float)(BattleAnimationRun.Num(d["speed"]) ?? 0);
		float tt = speed > 0 ? Math.Min(1f, t * speed) : t;
		string kind = BattleAnimationRun.Str(d["kind"]) ?? "forward";
		string? easeName = BattleAnimationRun.Str(d["ease"]) ?? "smooth";
		float e = BattleAnimationEval.Ease(easeName, tt);
		(int X, int Foot, int Size) place = c.Where(i);
		Rectangle body = c.Bodies.GetValueOrDefault(i, new Rectangle(place.X - place.Size / 2, place.Foot - place.Size, place.Size, place.Size));
		float unit = ForwardUnit(i, c);
		float D(string field, string def = "0") => BattleAnimationEval.Dist(BattleAnimationRun.Pick(d[field], front) ?? JsonValue.Create(def), unit, body, c.Motion);
		(float, float) By()
		{
			JsonArray? by = BattleAnimationRun.Pick(d["by"], front) as JsonArray;
			float bx = by is null ? 0 : BattleAnimationEval.Dist(by[0], unit, body, c.Motion), byy = by is null ? 0 : BattleAnimationEval.Dist(by[1], unit, body, c.Motion);
			return BattleAnimationRun.Str(d["space"]) == "forward" ? ToScreen(bx, byy, i, c) : (bx, byy);
		}
		switch (kind)
		{
		case "forward":
		{
			float dist = D("distance", "1f");
			return (done ? back ? prev.F : prev.F + dist : prev.F + dist * e, prev.X, prev.Y, 0f);
		}
		case "home":
			return done ? (0f, 0f, 0f, 0f) : (prev.F * (1f - e), prev.X * (1f - e), prev.Y * (1f - e), 0f);
		case "offset":
		case "exit":
		{
			(float bx, float byy) = By();
			return done ? back ? prev : (prev.F, prev.X + bx, prev.Y + byy, 0f) : (prev.F, prev.X + bx * e, prev.Y + byy * e, 0f);
		}
		case "enter":
		{
			(float bx, float byy) = By();
			return done ? (prev.F, prev.X, prev.Y, 0f) : (prev.F, prev.X + bx * (1f - e), prev.Y + byy * (1f - e), 0f);
		}
		case "hop":
			return (prev.F, prev.X, prev.Y, done ? 0f : D("height", "0.3h") * MathF.Sin(MathF.PI * tt));
		case "to":
		case "arc":
		{
			Vector2 target = PointOf(m.Run, d["to"], c, i) ?? new Vector2(place.X, place.Foot);
			bool toPlace = BattleAnimationRun.Str(BattleAnimationRun.Pick(d["to"], front))?.EndsWith(":place", StringComparison.Ordinal) == true;
			float ox = toPlace ? place.X : body.Center.X, oy = toPlace ? place.Foot : body.Center.Y;
			float reach = (float)(BattleAnimationRun.Num(d["reach"]) ?? 1);
			float tx = (target.X - ox) * reach, ty = (target.Y - oy) * reach;
			float shortBy = D("stop_short");
			if (shortBy != 0)
			{
				float len = MathF.Sqrt(tx * tx + ty * ty);
				if (len > 0)
				{
					float k2 = Math.Max(0, len - shortBy) / len;
					(tx, ty) = (tx * k2, ty * k2);
				}
			}
			string axis = BattleAnimationRun.Str(d["axis"]) ?? "both";
			if (axis == "x")
			{
				ty = prev.Y;
			}
			else if (axis == "y")
			{
				tx = prev.X;
			}
			float lift = kind == "arc" && !done ? D("height", "0.3h") * MathF.Sin(MathF.PI * tt) : 0f;
			return done ? back ? prev : (prev.F, tx, ty, 0f) : (prev.F, prev.X + (tx - prev.X) * e, prev.Y + (ty - prev.Y) * e, lift);
		}
		}
		return prev;
	}

	private static (float X, float Y) ToScreen(float along, float across, int i, StageCtx c)
	{
		int toward = i >= c.PartyCount ? 1 : -1;
		return c.Front ? (across, toward * along) : (toward * along, across);
	}

	private float ForwardUnit(int i, StageCtx c)
	{
		bool enemy = c.All[i].Side == Side.Enemy;
		return (c.Front ? (enemy ? 20 : c.All[i].Id == c.Vehicle ? 12 : 24) : enemy ? 24 : 44) * c.Motion;
	}

	// A POINT on the stage: a subject's anchor, a side's middle, a rank, a screen position, with an optional offset.
	private Vector2? PointOf(AnimRun run, JsonNode? node, StageCtx c, int self = -1)
	{
		node = BattleAnimationRun.Pick(node, c.Front);
		Vector2 offset = Vector2.Zero;
		if (node is JsonObject o)
		{
			if (o["offset"] is JsonArray off)
			{
				offset = new Vector2(BattleAnimationEval.Dist(off[0], 24, default, c.Motion), BattleAnimationEval.Dist(off[1], 24, default, c.Motion));
			}
			node = o["point"];
		}
		string? s = BattleAnimationRun.Str(node);
		if (s is null)
		{
			return null;
		}
		if (s.StartsWith("screen:", StringComparison.Ordinal))
		{
			string[] xy = s[7..].Split(',');
			float P(string v, int size) => v.EndsWith('%') ? float.Parse(v[..^1], CultureInfo.InvariantCulture) * size / 100f : float.Parse(v, CultureInfo.InvariantCulture);
			return new Vector2(P(xy[0], c.Width), P(xy[1], c.Height)) + offset;
		}
		if (s == "stage:center")
		{
			return new Vector2(c.Width / 2f, c.StageBottom / 2f) + offset;
		}
		if (s is "party:center" or "enemies:center" or "row:front" or "row:back")
		{
			bool party = s == "party:center";
			var rects = c.Anchors.Where(a => a.Key < c.All.Count && (c.All[a.Key].Side == Side.Party) == party).Select(a => a.Value).ToList();
			if (s.StartsWith("row:", StringComparison.Ordinal) && rects.Count > 0)
			{
				int foot = s == "row:front" ? rects.Max(r => r.Bottom) : rects.Min(r => r.Bottom);
				rects = [.. rects.Where(r => r.Bottom == foot)];
			}
			return rects.Count == 0 ? null : new Vector2((float)rects.Average(r => r.Center.X), (float)rects.Average(r => s.StartsWith("row:", StringComparison.Ordinal) ? r.Bottom : r.Center.Y)) + offset;
		}
		int last = s.LastIndexOf(':');
		string anchor = "auto";
		string subject = s;
		if (last > 0 && BattleAnimation.Anchors.Contains(s[(last + 1)..]))
		{
			anchor = s[(last + 1)..];
			subject = s[..last];
		}
		List<int> who = subject == "self" && self >= 0 && run.In.Self < 0 ? [self] : Subject(run, subject);
		if (who.Count == 0)
		{
			return null;
		}
		int fig = who[0];
		if (anchor == "place")
		{
			return new Vector2(c.Where(fig).X, c.Where(fig).Foot) + offset;
		}
		return c.Anchors.TryGetValue(fig, out Rectangle r2) ? Anchor(r2, anchor, c.Behind(fig)) + offset : null;
	}

	private static Vector2 Anchor(Rectangle r, string anchor, bool behind) => anchor switch
	{
		"center" => new Vector2(r.Center.X, r.Center.Y),
		"feet" => new Vector2(r.Center.X, r.Bottom),
		"head" => new Vector2(r.Center.X, behind ? r.Y + r.Height * 3 / 8 : r.Y + r.Height / 4),
		"top" => new Vector2(r.Center.X, r.Y),
		"left" => new Vector2(r.X, r.Center.Y),
		"right" => new Vector2(r.Right, r.Center.Y),
		_ => new Vector2(r.Center.X, behind ? r.Y + r.Height * 3 / 8 : r.Center.Y),
	};

	// The state a track or the battle plays on a figure, with its variant (`attack:CLASS`) for a member's weapon.
	private JsonObject? StateDef(int i, StageCtx c, string name)
	{
		if (name == "attack" && WeaponOf(i) is { } w)
		{
			name = "attack:" + w.Class;
		}
		return Anim.State(Scopes(i, c), name);
	}

	// Applies a state at progress t (local clock units since it started, `length` its length) to a figure's look.
	private void ApplyState(FigureLook fl, JsonObject? def, string name, float t, long local, long length, AnimRun? run, ActionLook? look, StageCtx c)
	{
		int ms = (int)(local / Math.Max(1, _timeline.RateMilli));
		if (def is null)
		{
			fl.Pose = name;
			fl.PoseMs = ms;
			fl.LookPose = null;
			return;
		}
		if (BattleAnimationRun.Str(def["pose"]) is { } pose)
		{
			fl.Pose = pose;
			fl.PoseMs = ms;
			fl.PoseLoop = BattleAnimationRun.Bool(def["loop"]) == true;
			fl.LookPose = null;
		}
		if (def["color"] is JsonObject color)
		{
			ApplyColor(fl, color, t, local, length, run, look, c);
		}
		if (def["opacity"] is JsonObject op)
		{
			float speed = (float)(BattleAnimationRun.Num(op["speed"]) ?? 0);
			float tt = speed > 0 ? Math.Min(1f, t * speed) : t;
			float e = BattleAnimationEval.Ease(BattleAnimationRun.Str(op["ease"]) ?? "linear", tt);
			float from = (float)(BattleAnimationRun.Num(op["from"]) ?? 1), to = (float)(BattleAnimationRun.Num(op["to"]) ?? 1);
			fl.Alpha = from + (to - from) * e;
		}
		if (BattleAnimationRun.Str(def["previous"]) == "crossfade")
		{
			fl.Crossfade = BattleAnimationEval.Ease(BattleAnimationRun.Str(def["ease"]) ?? "smooth", t);
		}
	}

	private void ApplyColor(FigureLook fl, JsonObject color, float t, long local, long length, AnimRun? run, ActionLook? look, StageCtx c)
	{
		Color Named(string n) => AnimColor(n, look ?? run?.In.Look);
		ColorTransform? ct;
		if (color.ContainsKey("mode") && run is not null)
		{
			float ct_t = t;
			long len = length;
			if (BattleAnimationRun.Str(color["length"]) is { } l && l.EndsWith('%'))
			{
				float share = (float)(double.Parse(l[..^1], CultureInfo.InvariantCulture) / 100);
				if (!(t < share))
				{
					return;
				}
				ct_t = t / share;
				len = (long)(length * share);
			}
			else if (color["length"] is JsonNode abs)
			{
				len = BattleAnimationRun.Time(abs, run, length);
				if (local >= len)
				{
					return;
				}
				ct_t = BattleClock.Ratio(local, len);
			}
			ct = BattleAnimationEval.ColorAt(color, ct_t, local, len, run, Named);
		}
		else
		{
			ct = BattleAnimationEval.Ct(color, Named);
		}
		if (ct is not { } v)
		{
			return;
		}
		if (c.Reduced && color["mode"] is not null && BattleAnimationRun.Str(color["mode"]) == "cycle")
		{
			v = BattleAnimationEval.Ct((color["colors"] as JsonArray)?.FirstOrDefault(), Named);
		}
		fl.Tint = v.Multiply;
		if (v.Alpha != 1f)
		{
			fl.Alpha *= v.Alpha;
		}
		fl.Add = v.Add;
		fl.Palette = v.Palette ?? fl.Palette;
	}

	private bool On(AnimRun run, AnimTrack t, int i) => BattleAnimationRun.Str(t.Def["on"]) is { } on && Subject(run, on).Contains(i);

	// The look the step's scripts and the battle's own states give figure `i`.
	private FigureLook Figure(int i, ShownCombatant sc, StageCtx c, bool choosing)
	{
		var fl = new FigureLook();
		BattleStep? step = c.Step;
		if (choosing && StateDef(i, c, "choosing")?["motion"] is JsonObject chooseMotion)
		{
			fl.Fwd = BattleAnimationEval.Dist(BattleAnimationRun.Pick(chooseMotion["distance"], c.Front) ?? JsonValue.Create("1f"), ForwardUnit(i, c), default, c.Motion);
		}
		foreach ((AnimRun run, long now, bool _) in Runs(step, c.Elapsed).Concat(StatusIdle(i, sc).Select(x => (x.Run, x.Now, false))))
		{
			var chain = new List<MoveItem>();
			foreach (AnimTrack t in run.Tracks.Where(t => now >= t.Start && t.Type is "move" or "state" && On(run, t, i) && Holds(run, t.Def)))
			{
				if (t.Type == "move")
				{
					string kind = BattleAnimationRun.Str(t.Def["kind"]) ?? "forward";
					if (kind is "shake" or "sine")
					{
						if (t.Active(now))
						{
							Additive(fl, t.Def, t.T(now), now - t.Start, run, c);
						}
						continue;
					}
					chain.Add(new MoveItem(t.Def, t.Start, t.Length, run));
				}
				else if (StateDef(i, c, StateName(t, run)) is { } def && def["motion"] is JsonObject motion)
				{
					string kind = BattleAnimationRun.Str(motion["kind"]) ?? "forward";
					long length = motion["length"] is JsonNode ml ? BattleAnimationRun.Time(ml, run, t.Length) : StateLength(def, t, run);
					if (kind is "shake" or "sine")
					{
						if (now < t.Start + length)
						{
							Additive(fl, motion, length <= 0 ? 1f : BattleClock.Ratio(now - t.Start, length), now - t.Start, run, c);
						}
						continue;
					}
					chain.Add(new MoveItem(motion, t.Start, length, run));
				}
			}
			chain.Sort((a, b) => a.Start.CompareTo(b.Start));
			(float f, float x, float y, float l) = Chain(chain, chain.Count - 1, now, i, c);
			if (chain.Count > 0)
			{
				fl.Fwd += f;
				fl.Sx += x;
				fl.Sy += y;
				fl.Lift += l;
			}
			foreach (AnimTrack t in run.Tracks.Where(t => t.Active(now) && t.Type is "state" or "pose" or "color" && On(run, t, i) && Holds(run, t.Def)))
			{
				long local = now - t.Start;
				switch (t.Type)
				{
				case "state":
				{
					string name = StateName(t, run);
					JsonObject? def = StateDef(i, c, name);
					long length = def is null ? t.Length : StateLength(def, t, run);
					if (local >= length && length > 0)
					{
						continue;
					}
					float st = length == t.Length ? t.T(now) : BattleClock.Ratio(local, length);
					ApplyState(fl, def, name, st, local, length, run, run.In.Look, c);
					if (BattleAnimationRun.Str(t.Def["state"]) == "$look.animation" && run.In.Look?.Pose is { } lookPose)
					{
						fl.LookPose = lookPose;
					}
					break;
				}
				case "pose":
					if (BattleAnimationRun.Str(t.Def["pose"]) == "hidden")
					{
						fl.Hidden = true;
						break;
					}
					if (BattleAnimationRun.Str(t.Def["pose"]) is { } pose)
					{
						fl.Pose = pose;
						fl.PoseMs = (int)(local / Math.Max(1, _timeline.RateMilli));
						fl.PoseLoop = BattleAnimationRun.Bool(t.Def["loop"]) == true;
						fl.LookPose = null;
					}
					if (t.Def["frame"] is JsonValue fv && fv.TryGetValue(out int frame))
					{
						fl.Frame = frame;
					}
					if (BattleAnimationRun.Str(t.Def["place_of"]) is { } other && Subject(run, other).FirstOrDefault(-1) is >= 0 and var o)
					{
						fl.PlaceOf = o;
						if (BattleAnimationRun.Pick(t.Def["offset"], c.Front) is JsonArray off)
						{
							fl.PlaceDx = BattleAnimationEval.Dist(off[0], ForwardUnit(i, c), default, c.Motion);
							fl.PlaceDy = BattleAnimationEval.Dist(off[1], ForwardUnit(i, c), default, c.Motion);
						}
					}
					break;
				case "color":
					ApplyColor(fl, t.Def, t.T(now), local, t.Length, run, run.In.Look, c);
					break;
				}
			}
		}
		// The battle's own states: a member down, an enemy falling, a combatant rising, a status shown.
		if (step is not null && sc.AliveStep == step || !sc.Alive)
		{
			bool falling = !sc.Alive && step is not null && sc.AliveStep == step;
			string life = !sc.Alive ? falling ? (sc.FelledInstantly ? "defeat:instant" : "defeat") : "fallen" : "rise";
			if (!sc.Alive && !falling && c.All[i].Side == Side.Enemy)
			{
				fl.Hidden = true;
				fl.Life = "gone";
				return fl;
			}
			JsonObject? def = StateDef(i, c, life);
			long len = step?.Length ?? 0;
			long local = life == "fallen" ? 0 : c.Elapsed - sc.AliveAt;
			long rest = Math.Max(0, len - sc.AliveAt);
			long length = def?["length"] is JsonNode ln && step?.Run is { } r ? BattleAnimationRun.Time(ln, r, rest, rest, out _) ?? rest : rest;
			float t = life == "fallen" ? 1f : Math.Clamp(length == rest ? BattleClock.Ratio(c.Elapsed - sc.AliveAt, rest) : BattleClock.Ratio(local, length), 0f, 1f);
			fl.Life = life;
			ApplyState(fl, def, life, t, local, length, step?.Run, null, c);
			if (life == "fallen")
			{
				fl.PoseMs = _timeline.ElapsedMs;
			}
			if (def is not null && BattleAnimationRun.Str(def["kind"]) is { } kind && falling)
			{
				long delay = def["delay"] is JsonNode dn && step?.Run is { } dr ? BattleAnimationRun.Time(dn, dr) : 0;
				float dt = delay > 0 ? Math.Clamp(BattleClock.Ratio(local - delay, Math.Max(1, length - delay)), 0f, 1f) : t;
				fl.Defeat = def;
				fl.DefeatT = dt;
				fl.DefeatLocal = local;
				switch (kind)
				{
				case "fade":
					fl.Alpha = 1f - dt;
					break;
				case "flash_fade":
					long flash = def["flash"] is JsonNode fn && step?.Run is { } fr ? BattleAnimationRun.Time(fn, fr) : 0;
					if (local < flash)
					{
						fl.Add = Color.White;
					}
					else
					{
						fl.Alpha = 1f - BattleClock.Ratio(local - flash, Math.Max(1, length - flash));
					}
					break;
				case "blink" when c.Reduced:
					fl.Alpha = 1f - dt;
					break;
				case "blink":
					long period = Math.Max(4 * BattleClock.Frame, def["period"] is JsonNode pn && step?.Run is { } pr ? BattleAnimationRun.Time(pn, pr) : 4 * BattleClock.Frame);
					fl.Alpha = dt >= 1f || local / period % 2 == 1 ? 0f : 1f;
					break;
				case "slide":
					if (def["by"] is JsonArray by)
					{
						fl.Sx += BattleAnimationEval.Dist(by[0], ForwardUnit(i, c), default, c.Motion) * dt;
						fl.Sy += BattleAnimationEval.Dist(by[1], ForwardUnit(i, c), default, c.Motion) * dt;
					}
					fl.Alpha = 1f - dt;
					break;
				case "frames":
					fl.Alpha = dt >= 1f ? 0f : 1f;
					break;
				}
			}
		}
		foreach (string status in sc.Statuses)
		{
			if (Anim.State(Scopes(i, c), "status:" + status) is { } def)
			{
				ApplyState(fl, def, "status:" + status, 0f, (long)_clockMs * _timeline.RateMilli, 0, StatusRun(i), null, c);
				if (def["motion"] is JsonObject motion && BattleAnimationRun.Str(motion["kind"]) is "shake" or "sine")
				{
					Additive(fl, motion, 0f, (long)_clockMs * _timeline.RateMilli, StatusRun(i), c);
				}
			}
		}
		return fl;
	}

	private AnimRun StatusRun(int figure) => new() { ScriptId = "status", Key = "status", Kind = "reaction", In = new AnimInputs { Self = figure, Front = StageLayout() == BattleLayout.Front }, RateMilli = _timeline.RateMilli };

	private static string StateName(AnimTrack t, AnimRun run)
	{
		string name = BattleAnimationRun.Str(t.Def["state"]) ?? "idle";
		return name == "$look.animation" ? run.In.Look?.Animation ?? "none" : name;
	}

	private static long StateLength(JsonObject def, AnimTrack t, AnimRun run) =>
		def["length"] is JsonNode n && BattleAnimationRun.Str(n) is not "$phase" ? BattleAnimationRun.Time(n, run, t.PhaseLength, t.Length, out _) ?? t.Length : t.Length;

	private void Additive(FigureLook fl, JsonObject d, float t, long local, AnimRun run, StageCtx c)
	{
		string axis = BattleAnimationRun.Str(d["axis"]) ?? "x";
		float value;
		if (BattleAnimationRun.Str(d["kind"]) == "shake")
		{
			float amp = (float)(BattleAnimationRun.Num(d["amplitude"]) ?? 0), freq = (float)(BattleAnimationRun.Num(d["frequency"]) ?? 40);
			value = BattleAnimationRun.Str(d["decay"]) == "none" ? MathF.Sin(t * freq) * amp * c.Motion : MathF.Sin(t * freq) * amp * c.Motion * (1f - t);
		}
		else
		{
			float amp = BattleAnimationEval.Dist(d["amplitude"] ?? JsonValue.Create(4), 24, default, c.Motion);
			long period = Math.Max(1, BattleAnimationRun.Time(d["period"], run, 60 * BattleClock.Frame));
			long phase = BattleAnimationRun.Time(d["phase"], run);
			value = amp * MathF.Sin(2f * MathF.PI * BattleClock.Ratio((local + phase) % period, period));
		}
		if (axis == "y")
		{
			fl.AddY += value;
		}
		else if (axis == "forward")
		{
			fl.Fwd += value;
		}
		else
		{
			fl.AddX += value;
		}
	}

	// The screen's shake: the largest of the shakes playing, damped with reduced flash, in percent of the layout's motion.
	private float Shake(StageCtx c, out Point backdrop, out Point figures)
	{
		float shake = 0f;
		backdrop = figures = Point.Zero;
		foreach ((AnimRun run, long now, bool only) in Runs(c.Step, c.Elapsed))
		{
			if (only)
			{
				continue;
			}
			foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "shake" && t.Active(now) && Holds(run, t.Def)))
			{
				string policy = Reduced(t, "damp");
				if (c.Reduced && policy == "drop")
				{
					continue;
				}
				JsonNode? a = BattleAnimationRun.Pick(t.Def["amplitude"], c.Front);
				float amp = BattleAnimationRun.Str(a) == "$look.shake" ? run.In.Look?.Shake ?? 0 : (float)(BattleAnimationRun.Num(a) ?? 0);
				if (t.Def["offsets"] is JsonObject offsets)
				{
					long frame = (now - t.Start) / BattleClock.Frame;
					foreach ((string layer, JsonNode? list) in offsets)
					{
						if (list is JsonArray l && l.Count > 0 && l[(int)(frame % l.Count)] is JsonArray xy)
						{
							var p = new Point((int)(BattleAnimationRun.Num(xy[0]) ?? 0), (int)(BattleAnimationRun.Num(xy[1]) ?? 0));
							if (c.Reduced && policy == "damp")
							{
								p = new Point((int)(p.X * Anim.Damp), (int)(p.Y * Anim.Damp));
							}
							if (layer is "backdrop" or "screen")
							{
								backdrop += p;
							}
							if (layer is "figures" or "screen")
							{
								figures += p;
							}
						}
					}
				}
				if (amp <= 0)
				{
					continue;
				}
				float tt = t.T(now);
				float v = BattleAnimationRun.Str(t.Def["decay"]) switch { "none" => amp, "out" => amp * (1f - tt) * (1f - tt), _ => amp * (1f - tt) };
				if (c.Reduced && policy == "damp")
				{
					v *= Anim.Damp;
				}
				shake = Math.Max(shake, v);
			}
		}
		return shake;
	}

	// The track's reduced flash policy: its own, the file's for its type, else the built-in one.
	private string Reduced(AnimTrack t, string builtin) =>
		BattleAnimationRun.Str(t.Def["reduced"]) ?? Anim.ReducedPolicy.GetValueOrDefault(t.Type) ?? builtin;

	// Flashes and fills on the stage or the whole frame.
	private List<(Rectangle Area, Color Color)> Fills(StageCtx c, string area, out bool flashed)
	{
		var fills = new List<(Rectangle, Color)>();
		flashed = false;
		foreach ((AnimRun run, long now, bool only) in Runs(c.Step, c.Elapsed))
		{
			if (only)
			{
				continue;
			}
			foreach (AnimTrack t in run.Tracks.Where(t => t.Type is "flash" or "color" && t.Active(now) && Holds(run, t.Def)))
			{
				string on = t.Type == "flash" ? BattleAnimationRun.Str(t.Def["area"]) ?? "stage" : BattleAnimationRun.Str(t.Def["on"]) ?? "";
				if (on != area)
				{
					continue;
				}
				Rectangle rect = area == "frame" ? new Rectangle(0, 0, c.Width, c.Height) : new Rectangle(0, 0, c.Width, c.StageBottom);
				float tt = t.T(now);
				if (t.Type == "flash")
				{
					if (c.Reduced && Reduced(t, "drop") == "drop")
					{
						continue;
					}
					float amount = (float)(BattleAnimationRun.Num(t.Def["amount"]) ?? 0.55);
					Color color = BattleAnimationRun.Str(t.Def["color"]) is { } name ? AnimColor(name, run.In.Look) : Color.White;
					fills.Add((rect, color * (amount * (1f - tt))));
					flashed = true;
					continue;
				}
				string mode = BattleAnimationRun.Str(t.Def["mode"]) ?? "set";
				if (c.Reduced && mode == "flash" && Reduced(t, "drop") == "drop")
				{
					continue;
				}
				// A fill is the `to` color at the track's strength: a tint at its amount, an add additively. The strength is the
				// track's curve read through a white add.
				var probe = (JsonObject)t.Def.DeepClone();
				probe["to"] = new JsonObject { ["add"] = "#ffffff" };
				probe.Remove("colors");
				if (mode == "cycle")
				{
					probe["mode"] = "set";
				}
				float k = (BattleAnimationEval.ColorAt(probe, tt, now - t.Start, t.Length, run, _ => Color.White)?.Add.R ?? 0) / 255f;
				JsonObject? to = BattleAnimationRun.Pick(t.Def["to"], c.Front) as JsonObject ?? (t.Def["colors"] as JsonArray)?.FirstOrDefault() as JsonObject;
				if (to?["tint"] is JsonNode tint)
				{
					float amount = BattleAnimationRun.Num(to["amount"]) is double am ? (float)am : 1f;
					fills.Add((rect, AnimColor(BattleAnimationRun.Str(tint)!, run.In.Look) * (amount * k)));
				}
				if (BattleAnimationRun.Str(to?["add"]) is { } add)
				{
					Color ac = AnimColor(add, run.In.Look);
					fills.Add((rect, new Color((byte)(ac.R * k), (byte)(ac.G * k), (byte)(ac.B * k), (byte)0)));
				}
			}
		}
		return fills;
	}
}
