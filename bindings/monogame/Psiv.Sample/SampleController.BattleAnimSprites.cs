using System.Globalization;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Sprites, figure pictures, floating numbers and the backdrop as scripts draw them.
public sealed partial class SampleController
{
	private readonly Dictionary<string, int> _animImages = [];
	// Each sprite instance the last battle frame drew: its track, frame, whether mirrored and whether a trail's copy.
	private readonly List<string> _probeSprites = [];

	// An animation file's enemy place for formation place `position`, by encounter, then by the exact enemy count, then
	// count:5 for five or more; with its rank when the place names one.
	private (int X, int Foot, string? Rank)? Placement(string encounter, int count, int position, bool front)
	{
		foreach (string key in new[] { "encounter:" + encounter, "count:" + count.ToString(CultureInfo.InvariantCulture), "count:" + Math.Min(count, 5).ToString(CultureInfo.InvariantCulture) })
		{
			if (Anim.Placements.TryGetValue(key, out JsonNode? node) && BattleAnimationRun.Pick(node, front) is JsonArray places && position < places.Count
				&& places[position] is JsonArray xy)
			{
				return ((int)(BattleAnimationRun.Num(xy[0]) ?? 0), (int)(BattleAnimationRun.Num(xy[1]) ?? 0), xy.Count > 2 ? BattleAnimationRun.Str(xy[2]) : null);
			}
		}
		return null;
	}

	// Opaque pixels whose color is a palette entry's source take its replacement.
	internal static Color[] Recolor(Color[] pixels, IReadOnlyList<(Color From, Color To)> palette)
	{
		var result = (Color[])pixels.Clone();
		for (int k = 0; k < result.Length; k++)
		{
			Color p = result[k];
			if (p.A != 255)
			{
				continue;
			}
			foreach ((Color from, Color to) in palette)
			{
				if (p.R == from.R && p.G == from.G && p.B == from.B)
				{
					result[k] = new Color(to.R, to.G, to.B, (byte)255);
					break;
				}
			}
		}
		return result;
	}

	// A palette's index sources take the picture's palette entry; without one, or past its end, they are left out.
	internal static IReadOnlyList<(Color From, Color To)> Resolve(IReadOnlyList<(Color From, Color To)> palette, IReadOnlyList<Color>? declared) =>
		[.. palette.Where(p => p.From.A != 0 || declared is not null && p.From.R < declared.Count).Select(p => p.From.A != 0 ? p : (declared![p.From.R], p.To))];

	private static string PaletteKey(IReadOnlyList<(Color From, Color To)> palette) => "palette:" + string.Join(",", palette.Select(p => $"{p.From.PackedValue:x}>{p.To.PackedValue:x}"));

	private static Color[] Silhouette(Color[] pixels, int w, int h) => [.. pixels.Select(p => new Color(p.A, p.A, p.A, p.A))];

	// A figure's picture: whole, cut into vanishing bands or sinking under its ground line, and its added color drawn
	// over it additively as its silhouette.
	private void FigurePicture(IDrawSurface s, List<Action> pictures, int image, Rectangle body, Color color, FigureLook fl, int footLine, int index)
	{
		string kind = fl.Defeat is { } d ? BattleAnimationRun.Str(d["kind"]) ?? "fade" : "";
		if (kind == "bands")
		{
			int bands = (int)(BattleAnimationRun.Num(fl.Defeat!["bands"]) ?? 8);
			int gone = (int)(fl.DefeatT * bands);
			var order = Enumerable.Range(0, bands).OrderBy(k => Noise.Hash(index, k)).ToList();
			for (int k = 0; k < bands; k++)
			{
				if (order.IndexOf(k) < gone)
				{
					continue;
				}
				int y0 = body.Y + body.Height * k / bands, y1 = body.Y + body.Height * (k + 1) / bands;
				if (y1 > y0)
				{
					pictures.Add(() =>
					{
						s.Clip(body.X, y0, Math.Max(1, body.Width), y1 - y0);
						s.Image(image, body.X, body.Y, body.Width, body.Height, color);
						s.Unclip();
					});
				}
			}
			return;
		}
		if (kind == "sink")
		{
			int down = (int)(body.Height * fl.DefeatT);
			int visible = footLine - body.Y;
			if (visible > 0)
			{
				pictures.Add(() =>
				{
					s.Clip(body.X, body.Y, Math.Max(1, body.Width), visible);
					s.Image(image, body.X, body.Y + down, body.Width, body.Height, color);
					s.Unclip();
				});
			}
			return;
		}
		pictures.Add(() => s.Image(image, body.X, body.Y, body.Width, body.Height, color));
		if (fl.Add.R > 0 || fl.Add.G > 0 || fl.Add.B > 0)
		{
			int sil = s.Derive(image, "silhouette", Silhouette);
			float a = color.A / 255f;
			var add = new Color((byte)(fl.Add.R * a), (byte)(fl.Add.G * a), (byte)(fl.Add.B * a), (byte)0);
			pictures.Add(() => s.ImageEx(sil, body.X, body.Y, body.Width, body.Height, add, false));
		}
	}

	// The sprites of one layer from the step's script and its reactions.
	private void DrawSprites(IDrawSurface s, Art art, StageCtx c, string layer, List<string>? marks = null)
	{
		var idle = Enumerable.Range(0, Math.Min(_show.All.Count, c.All.Count)).Where(i => _show.All[i].Alive).SelectMany(i => StatusIdle(i, _show.All[i])).Select(x => (x.Run, x.Now, false));
		foreach ((AnimRun run, long now, bool only) in Runs(c.Step, c.Elapsed).Concat(idle))
		{
			if (only)
			{
				continue;
			}
			foreach (AnimTrack t in run.Tracks)
			{
				if (t.Type != "sprite" || now < t.Start || t.HasLength && now >= t.End || (BattleAnimationRun.Str(t.Def["layer"]) ?? "effects") != layer || !Holds(run, t.Def))
				{
					continue;
				}
				// A sprite marked `"flash": true` counts toward the flash limit and is damped under reduced flash unless it has
				// its own `reduced`.
				bool marked = BattleAnimationRun.Bool(t.Def["flash"]) == true;
				if (marked && !FlashAllowed(run, t.Index))
				{
					continue;
				}
				string policy = Reduced(t, marked ? "damp" : "keep");
				DrawSprite(s, art, c, run, t, now, marks, c.Reduced && policy == "damp" ? Anim.Damp : 1f);
			}
		}
	}

	private void DrawSprite(IDrawSurface s, Art art, StageCtx c, AnimRun run, AnimTrack t, long now, List<string>? marks, float damp)
	{
		bool front = c.Front;
		string effect = BattleAnimationRun.Str(BattleAnimationRun.Pick(t.Def["effect"], front)) ?? "none";
		if (effect == "$look.effect")
		{
			effect = run.In.Look?.Effect ?? "none";
		}
		if (effect == "none")
		{
			return;
		}
		JsonObject? anim = Anim.Effects.GetValueOrDefault(effect);
		JsonNode? F(string field) => BattleAnimationRun.Pick(t.Def[field] ?? anim?[field], front);
		string sheet = anim is null ? effect : BattleAnimationRun.Str(anim["sheet"]) ?? effect;
		if (art.Effect(sheet) is not { } fx)
		{
			return;
		}
		int rate = Math.Max(1, run.RateMilli);
		// Where instances play: one per figure of a subject, one at the targets' middle, one per target's column, or a point.
		var spots = new List<(int Figure, Vector2 At)>();
		string spawn = BattleAnimationRun.Str(BattleAnimationRun.Pick(t.Def["spawn"], front)) ?? (t.Def["spawn"] is JsonObject ? "" : "targets");
		string anchor = BattleAnimationRun.Str(F("anchor")) ?? "auto";
		if (spawn is "all_targets" or "columns")
		{
			var rects = Subject(run, "targets").Where(c.Anchors.ContainsKey).Select(i => (i, c.Anchors[i])).ToList();
			if (spawn == "all_targets" && rects.Count > 0)
			{
				spots.Add((-1, new Vector2((float)rects.Average(r => r.Item2.Center.X), (float)rects.Average(r => r.Item2.Center.Y))));
			}
			int columnY = (int)(BattleAnimationRun.Num(F("column_y")) ?? c.StageBottom / 2);
			if (spawn == "columns")
			{
				spots.AddRange(rects.OrderBy(r => r.Item2.Center.X).Select(r => (r.i, new Vector2(r.Item2.Center.X, columnY))));
			}
		}
		else if (t.Def["spawn"] is JsonObject || spawn.StartsWith("screen:", StringComparison.Ordinal) || spawn.Contains(":center") || spawn.StartsWith("row:", StringComparison.Ordinal)
			|| spawn.LastIndexOf(':') is var colon && colon > 0 && BattleAnimation.Anchors.Contains(spawn[(colon + 1)..]))
		{
			if (PointOf(run, t.Def["spawn"], c) is { } point)
			{
				spots.Add((-1, point));
			}
		}
		else
		{
			List<int> who = [.. Subject(run, spawn).Where(c.Anchors.ContainsKey)];
			string order = BattleAnimationRun.Str(BattleAnimationRun.Pick(t.Def["order"], front)) ?? "event";
			IEnumerable<int> sorted = order switch
			{
				"left_to_right" => who.OrderBy(i => c.Anchors[i].Center.X),
				"right_to_left" => who.OrderByDescending(i => c.Anchors[i].Center.X),
				_ => Ordered(who, i => i, order, run),
			};
			spots.AddRange(sorted.Select(i => (i, Anchor(c.Anchors[i], anchor, c.Behind(i)))));
		}
		string? staggerText = BattleAnimationRun.Str(BattleAnimationRun.Pick(t.Def["stagger"], front));
		long stagger = staggerText == "sweep" ? spots.Count > 1 ? Math.Min(90, (run.In.Look?.EffectMs ?? 0) / (2 * spots.Count)) * (long)rate : 0
			: BattleAnimationRun.Time(BattleAnimationRun.Pick(t.Def["stagger"], front), run);
		bool sheetTiming = F("frames") is null && F("durations") is null && anim is null;
		int loops = (int)(BattleAnimationRun.Num(F("loops")) ?? 0);
		for (int k = 0; k < spots.Count; k++)
		{
			long local = now - t.Start - k * stagger;
			if (local < 0)
			{
				continue;
			}
			(int fig, Vector2 at) = spots[k];
			// A projectile's direction (the dominant axis of its whole travel) picks its `directions` frames, and with `flip: auto`
			// mirrors it traveling right when it has no right frames.
			JsonObject? travel = F("travel") as JsonObject;
			Vector2 from = Vector2.Zero, to = Vector2.Zero;
			string direction = "";
			if (travel is not null)
			{
				from = PointOf(run, travel["from"] ?? JsonValue.Create("user:auto"), c, fig) ?? at;
				to = travel["to"] is JsonNode tn ? PointOf(run, tn, c, fig) ?? at : at;
				Vector2 d = to - from;
				direction = MathF.Abs(d.X) >= MathF.Abs(d.Y) ? d.X > 0 ? "right" : "left" : d.Y > 0 ? "down" : "up";
			}
			JsonObject? directions = F("directions") as JsonObject;
			JsonArray? frameList = directions?[direction] as JsonArray ?? F("frames") as JsonArray;
			int frame;
			bool over;
			if (sheetTiming && frameList is null)
			{
				int ms = (int)(local / rate);
				frame = fx.Idle.FrameAt(ms, loop: loops != 0);
				over = !t.HasLength && loops == 0 && ms >= fx.Idle.Ms.Sum();
			}
			else
			{
				int[] frames = frameList is not null ? [.. frameList.Select(x => (int)(BattleAnimationRun.Num(x) ?? 0))] : fx.Idle.Frames;
				long[] durations = F("durations") switch
				{
					JsonArray da => [.. da.Select(x => BattleAnimationRun.Time(x, run))],
					JsonNode one => [.. frames.Select(_ => BattleAnimationRun.Time(one, run))],
					_ => [.. fx.Idle.Ms.Select(ms => (long)ms * rate)],
				};
				if (durations.Length < frames.Length)
				{
					durations = [.. durations, .. Enumerable.Repeat(durations.LastOrDefault(), frames.Length - durations.Length)];
				}
				frame = BattleAnimationEval.Frame(frames, durations, loops, BattleAnimationRun.Bool(F("pingpong")) == true, BattleAnimationRun.Time(F("hold_last"), run), local, out over);
			}
			// A projectile travels from one point to another, its trail of fading copies behind it; its impact animation plays
			// where it lands.
			var points = new List<(Vector2 At, float Alpha)>();
			if (travel is not null)
			{
				long frames = BattleAnimationRun.Time(travel["frames"], run, BattleClock.Frame);
				Rectangle size = fig >= 0 && c.Anchors.TryGetValue(fig, out Rectangle fr) ? fr : new Rectangle(0, 0, 64, 64);
				float height = BattleAnimationEval.Dist(travel["height"] ?? JsonValue.Create("0.5h"), 24, size, c.Motion);
				Vector2 Flight(long l)
				{
					float u = BattleAnimationEval.Ease(BattleAnimationRun.Str(travel["ease"]) ?? "linear", BattleClock.Ratio(l, frames));
					Vector2 p = Vector2.Lerp(from, to, u);
					switch (BattleAnimationRun.Str(travel["path"]))
					{
					case "arc":
						p.Y -= height * MathF.Sin(MathF.PI * u);
						break;
					case "wave":
						Vector2 dir = to - from;
						var normal = dir == Vector2.Zero ? Vector2.UnitY : Vector2.Normalize(new Vector2(-dir.Y, dir.X));
						p += normal * height * MathF.Sin(2f * MathF.PI * (float)(BattleAnimationRun.Num(travel["waves"]) ?? 1) * u);
						break;
					}
					return p;
				}
				if (local >= frames)
				{
					if (local < frames + BattleAnimationRun.Time(F("hold_last"), run))
					{
						at = to;
					}
					else
					{
						if (BattleAnimationRun.Str(F("impact")) is { } impact)
						{
							DrawImpact(s, art, c, run, impact, to, local - frames, fig);
						}
						continue;
					}
				}
				else
				{
					at = Flight(local);
					if (travel["trail"] is JsonObject trail)
					{
						long gap = Math.Max(BattleClock.Frame, BattleAnimationRun.Time(trail["gap"] ?? JsonValue.Create(2), run));
						float fade = (float)(BattleAnimationRun.Num(trail["alpha"]) ?? 0.5), copy = 1f;
						for (int j = 1; j <= (int)(BattleAnimationRun.Num(trail["count"]) ?? 3) && local - j * gap >= 0; j++)
						{
							copy *= fade;
							points.Add((Flight(local - j * gap), copy));
						}
					}
				}
			}
			else if (over)
			{
				continue;
			}
			if (frame < 0 || frame >= fx.Images.Length)
			{
				continue;
			}
			points.Reverse();
			points.Add((at, 1f));
			foreach ((Vector2 point, float copyAlpha) in points)
			{
				DrawInstance(s, art, c, run, t, fx, F, frame, fig, point, k, local, anchor, copyAlpha * damp, direction, directions, marks is not null && copyAlpha == 1f ? marks : null,
					copyAlpha < 1f);
			}
		}
	}

	// One drawn instance of a sprite at its point: its factor, offsets, path, origin, color, fade and blend.
	private void DrawInstance(IDrawSurface s, Art art, StageCtx c, AnimRun run, AnimTrack t, Figure fx, Func<string, JsonNode?> F, int frame, int fig, Vector2 at, int k, long local,
			string anchor, float damp, string direction, JsonObject? directions, List<string>? marks, bool trail)
		{
		int rate = Math.Max(1, run.RateMilli);
		JsonArray? offsets = F("offsets") as JsonArray;
		int image = fx.Images[frame];
		int layoutFactor = c.Front && fig >= 0 && !c.Behind(fig) ? c.EffectScaleLarge : c.EffectScale;
		JsonNode? scale = F("scale");
		int factor = BattleAnimationRun.Str(scale) switch
		{
			"large" => c.EffectScaleLarge,
			{ } times when times.StartsWith('x') => Math.Max(1, (int)Math.Round(layoutFactor * double.Parse(times[1..], CultureInfo.InvariantCulture))),
			_ => BattleAnimationRun.Num(scale) is double n ? Math.Max(1, (int)Math.Round(n)) : layoutFactor,
		};
		(int ew, int eh) = art.DrawnEffectSize(image, factor);
		float dx = 0, dy = 0;
		Rectangle body = fig >= 0 && c.Anchors.TryGetValue(fig, out Rectangle br) ? br : default;
		if (offsets is not null && offsets.Count > 0 && offsets[Math.Min(k, offsets.Count - 1)] is JsonArray ok)
		{
			dx += BattleAnimationEval.Dist(ok[0], 24, body, c.Motion);
			dy += BattleAnimationEval.Dist(ok[1], 24, body, c.Motion);
		}
		if (F("offset") is JsonArray of)
		{
			dx += BattleAnimationEval.Dist(of[0], 24, body, c.Motion);
			dy += BattleAnimationEval.Dist(of[1], 24, body, c.Motion);
		}
		if (F("jitter") is JsonArray jit)
		{
			dx += (BattleAnimationEval.Hash(run.ScriptId, t.Index, k) * 2 - 1) * BattleAnimationEval.Dist(jit[0], 24, body, c.Motion);
			dy += (BattleAnimationEval.Hash(run.ScriptId, t.Index, k + 977) * 2 - 1) * BattleAnimationEval.Dist(jit[1], 24, body, c.Motion);
		}
		if (F("path") is JsonArray path)
		{
			(float px, float py) = PathAt(path, local, run, body, c);
			dx += px;
			dy += py;
		}
		float ox = 0.5f, oy = anchor == "feet" ? 1f : 0.5f;
		if (F("origin") is JsonArray origin)
		{
			ox = (float)(BattleAnimationRun.Num(origin[0]) ?? 0.5);
			oy = (float)(BattleAnimationRun.Num(origin[1]) ?? 0.5);
		}
		int x = (int)at.X + (int)MathF.Round(dx) - (ox == 0.5f ? ew / 2 : (int)(ew * ox));
		int y = (int)at.Y + (int)MathF.Round(dy) - (oy == 0.5f ? eh / 2 : (int)(eh * oy));
		Color tint = Color.White;
		float alpha = 1f;
		Color add = Color.Transparent;
		if (F("color") is JsonObject color)
		{
			long life = t.HasLength ? t.Length : fx.Idle.Ms.Sum() * (long)rate;
			float u = BattleClock.Ratio(local, Math.Max(1, life));
			ColorTransform ct = color.ContainsKey("from")
				? BattleAnimationEval.Lerp(BattleAnimationEval.Ct(color["from"], n => AnimColor(n, run.In.Look)), BattleAnimationEval.Ct(color["to"], n => AnimColor(n, run.In.Look)),
					BattleAnimationEval.Ease(BattleAnimationRun.Str(color["ease"]), u))
				: color.ContainsKey("keys") ? BattleAnimationEval.ColorAt(new JsonObject { ["mode"] = "keys", ["keys"] = color["keys"]!.DeepClone() }, u, local, life, run, n => AnimColor(n, run.In.Look)) ?? ColorTransform.None
				: BattleAnimationEval.Ct(color, n => AnimColor(n, run.In.Look));
				tint = ct.Multiply;
				alpha = ct.Alpha;
				add = ct.Add;
				if (ct.Palette is { } recolor && Resolve(recolor, fx.Palette) is { Count: > 0 } resolved)
				{
					image = s.Derive(image, PaletteKey(resolved), (px, _, _) => Recolor(px, resolved));
				}
			}
		if (BattleAnimationRun.Str(F("fade_out")) is { } fade && fade.EndsWith('%'))
		{
			double share = double.Parse(fade[..^1], CultureInfo.InvariantCulture) / 100;
			float u = BattleClock.Ratio(local, t.Length);
			alpha *= u < (float)(1 - share) ? 1f : (1f - u) / (float)share;
		}
		if (damp != 1f)
		{
			alpha *= damp;
		}
		bool additive = BattleAnimationRun.Str(F("blend")) == "additive";
		string flip = BattleAnimationRun.Str(F("flip")) ?? "none";
		bool flipX = flip == "x" || flip == "auto" && direction == "right" && directions?["right"] is null;
		Color draw = alpha == 1f ? tint : tint * alpha;
		if (additive)
		{
			draw = new Color(draw.R, draw.G, draw.B, (byte)0);
		}
		if (additive || flipX)
		{
			s.ImageEx(image, x, y, ew, eh, draw, flipX);
		}
		else
		{
			s.Image(image, x, y, ew, eh, draw);
		}
		if (add.R > 0 || add.G > 0 || add.B > 0)
		{
			s.ImageEx(s.Derive(image, "silhouette", Silhouette), x, y, ew, eh, new Color((byte)(add.R * alpha), (byte)(add.G * alpha), (byte)(add.B * alpha), (byte)0), flipX);
		}
		if (run.Kind == "turn")
		{
			_probeEffectSize = $"{ew}x{eh}";
		}
		_probeSprites.Add($"{t.Label} frame {frame}{(flipX ? " flip" : "")}{(trail ? " trail" : "")}");
		if (marks is not null && fig >= 0 && fig < _show.All.Count)
		{
			marks.Add(_show.All[fig].Label);
		}
	}

	private void DrawImpact(IDrawSurface s, Art art, StageCtx c, AnimRun run, string impact, Vector2 at, long local, int fig)
	{
		JsonObject? anim = Anim.Effects.GetValueOrDefault(impact);
		string sheet = anim is null ? impact : BattleAnimationRun.Str(anim["sheet"]) ?? impact;
		if (art.Effect(sheet) is not { } fx)
		{
			return;
		}
		long[] durations = anim?["durations"] switch
		{
			JsonArray da => [.. da.Select(x => BattleAnimationRun.Time(x, run))],
			JsonNode one => [.. fx.Idle.Frames.Select(_ => BattleAnimationRun.Time(one, run))],
			_ => [.. fx.Idle.Ms.Select(ms => (long)ms * run.RateMilli)],
		};
		int[] frames = anim?["frames"] is JsonArray fa ? [.. fa.Select(x => (int)(BattleAnimationRun.Num(x) ?? 0))] : fx.Idle.Frames;
		int frame = BattleAnimationEval.Frame(frames, durations.Length >= frames.Length ? durations : [.. frames.Select(_ => durations.LastOrDefault())], 0, false, 0, local, out bool over);
		if (over || frame < 0 || frame >= fx.Images.Length)
		{
			return;
		}
		int image = fx.Images[frame];
		int factor = c.Front && fig >= 0 && !c.Behind(fig) ? c.EffectScaleLarge : c.EffectScale;
		if (BattleAnimationRun.Str(anim?["scale"]) is { } x && x.StartsWith('x'))
		{
			factor = Math.Max(1, (int)Math.Round(factor * double.Parse(x[1..], CultureInfo.InvariantCulture)));
		}
		(int ew, int eh) = art.DrawnEffectSize(image, factor);
		Color draw = BattleAnimationRun.Str(anim?["blend"]) == "additive" ? new Color(255, 255, 255, 0) : Color.White;
		s.ImageEx(image, (int)at.X - ew / 2, (int)at.Y - eh / 2, ew, eh, draw, false);
	}

	// A sprite's path: [frame, dx, dy] keys from the instance's start, linear between them.
	private static (float X, float Y) PathAt(JsonArray path, long local, AnimRun run, Rectangle body, StageCtx c)
	{
		var keys = path.OfType<JsonArray>().Select(k => (At: BattleAnimationRun.Time(k[0], run), X: BattleAnimationEval.Dist(k[1], 24, body, c.Motion), Y: BattleAnimationEval.Dist(k[2], 24, body, c.Motion)))
			.OrderBy(k => k.At).ToList();
		if (keys.Count == 0)
		{
			return (0, 0);
		}
		if (local <= keys[0].At)
		{
			return (keys[0].X, keys[0].Y);
		}
		for (int i = 1; i < keys.Count; i++)
		{
			if (local < keys[i].At)
			{
				float u = BattleClock.Ratio(local - keys[i - 1].At, keys[i].At - keys[i - 1].At);
				return (keys[i - 1].X + (keys[i].X - keys[i - 1].X) * u, keys[i - 1].Y + (keys[i].Y - keys[i - 1].Y) * u);
			}
		}
		return (keys[^1].X, keys[^1].Y);
	}

	// A floating number: the built-in rise over its life, or its kind's style from the animation file. `rest` is the clock
	// units from its release to its step's end.
	private void DrawNumber(IDrawSurface s, BattleEvent e, FloatingText floating, float p, Rectangle r, bool behind, int n, int numberY, int numberMemberY, int numberRise, int numberGap,
		string kind, long local, bool party, long rest)
	{
		if (!Anim.Numbers.TryGetValue(kind, out JsonObject? style))
		{
			float fade = p < 0.7f ? 1f : (1f - p) / 0.3f;
			int rise = (int)(numberRise * Ease(Math.Min(1f, p * 2f)));
			int w = s.Measure(floating.Text, floating.Size);
			int fy = (behind ? r.Y + numberMemberY : r.Y - numberY) - rise - n * (floating.Size + numberGap);
			s.Text(floating.Text, r.Center.X - w / 2 + 2, fy + 2, Color.Black * (0.8f * fade), floating.Size);
			s.Text(floating.Text, r.Center.X - w / 2, fy, floating.Color * fade, floating.Size);
			return;
		}
		AnimRun timing = StatusRun(-1);
		int size = style["size"] is JsonValue sv && sv.TryGetValue(out int px) ? px : floating.Size;
		Color color = BattleAnimationRun.Str(style["color"]) is { } cn ? AnimColor(cn, null) : floating.Color;
		string motion = BattleAnimationRun.Str(style["motion"]) ?? "rise";
		long delay = BattleAnimationRun.Time(style["delay"], timing);
		if (local < delay)
		{
			return;
		}
		int width = s.Measure(floating.Text, size);
		if (motion == "window")
		{
			bool front = _stageLayout == BattleLayout.Front;
			JsonNode? padNode = BattleAnimationRun.Pick(style["pad"], front);
			int padX = (int)(BattleAnimationEval.Px(padNode is JsonArray xy ? xy[0] : padNode) ?? 6), padY = (int)(BattleAnimationEval.Px(padNode is JsonArray xy2 ? xy2[1] : padNode) ?? 6);
			JsonNode? rowNode = BattleAnimationRun.Pick(style["row"], front);
			int row = (int)(BattleAnimationEval.Px(rowNode is JsonObject sides ? sides[party ? "party" : "enemy"] : rowNode) ?? 40);
			long open = Math.Max(1, BattleAnimationRun.Time(style["open"] ?? JsonValue.Create(6), timing));
			int full = width + 2 * padX, h = size + 2 * padY;
			float shown = Math.Min(1f, BattleClock.Ratio(local - delay, open));
			// It closes sideways over the last `close` of its life, its text gone as it starts.
			if (style["close"] is JsonNode closeNode)
			{
				long life = style["life"] is JsonNode lifeNode ? BattleAnimationRun.Time(lifeNode, timing, rest, rest, out _) ?? rest : rest;
				long close = Math.Max(1, BattleAnimationRun.Time(closeNode, timing, life, life, out _) ?? 1);
				if (local >= life)
				{
					return;
				}
				shown = Math.Min(shown, BattleClock.Ratio(life - local, close));
			}
			int wNow = Math.Max(2, (int)(full * shown));
			int bx = r.Center.X - wNow / 2, by = row + n * (h + 4);
			if (BattleAnimationRun.Str(style["frame"]) is { } frame)
			{
				Ui.DrawFrame(frame, s, new Rectangle(bx, by, wNow, h), 1f);
			}
			else
			{
				s.Fill(bx, by, wNow, h, Style.Panel);
				Style.Frame(s, bx, by, wNow, h, Style.PanelEdge);
			}
			if (wNow >= full)
			{
				s.Text(floating.Text, r.Center.X - width / 2, by + padY, color, size);
			}
			return;
		}
		double fadeShare = BattleAnimationRun.Str(style["fade"]) is { } f && f.EndsWith('%') ? double.Parse(f[..^1], CultureInfo.InvariantCulture) / 100 : 0.3;
		float alpha = fadeShare <= 0 || p < (float)(1 - fadeShare) ? 1f : (1f - p) / (float)fadeShare;
		float riseBy = BattleAnimationRun.Str(style["rise"]) is "layout" or null && style["rise"] is not JsonValue { } ? numberRise : BattleAnimationEval.Px(style["rise"]) ?? numberRise;
		double riseShare = BattleAnimationRun.Str(style["rise_time"]) is { } rt && rt.EndsWith('%') ? double.Parse(rt[..^1], CultureInfo.InvariantCulture) / 100 : 0.5;
		float scale = 1f;
		float lift = motion switch
		{
			"rise" => riseBy * Ease(Math.Min(1f, p / (float)riseShare)),
			"float" => riseBy * p,
			"bounce" => BounceAt(p, riseBy, (int)(BattleAnimationRun.Num(style["bounces"]) ?? 2)),
			_ => 0f,
		};
		if (motion == "pop")
		{
			long popTime = Math.Max(1, BattleAnimationRun.Time(style["pop_time"] ?? JsonValue.Create(6), timing));
			float pop = (float)(BattleAnimationRun.Num(style["pop"]) ?? 1.5);
			scale = pop + (1f - pop) * Math.Min(1f, BattleClock.Ratio(local - delay, popTime));
		}
		int drawSize = Math.Max(4, (int)MathF.Round(size * scale));
		int dw = s.Measure(floating.Text, drawSize);
		int stack = BattleAnimationEval.Px(style["stack"]) is float sp ? (int)sp : numberGap;
		int y = (behind ? r.Y + numberMemberY : r.Y - numberY) - (int)lift - n * (size + stack);
		if (style["shadow"] is not JsonValue { } noShadow || noShadow.TryGetValue(out bool on) && on)
		{
			int sx = 2, sy = 2;
			Color sc = Color.Black * 0.8f;
			if (style["shadow"] is JsonArray sh)
			{
				sx = (int)(BattleAnimationRun.Num(sh[0]) ?? 2);
				sy = (int)(BattleAnimationRun.Num(sh[1]) ?? 2);
				sc = AnimColor(BattleAnimationRun.Str(sh[2]) ?? "#000000cc", null);
			}
			s.Text(floating.Text, r.Center.X - dw / 2 + sx, y + sy, sc * alpha, drawSize);
		}
		s.Text(floating.Text, r.Center.X - dw / 2, y, color * alpha, drawSize);
	}

	private static float BounceAt(float p, float height, int bounces)
	{
		int count = Math.Max(1, bounces + 1);
		float part = Math.Min(p * count, count - 0.0001f);
		int k = (int)part;
		return height * MathF.Pow(0.5f, k) * MathF.Sin(MathF.PI * (part - k));
	}

	// The backdrop with the scripts' effects on it (scroll, wave, offset, swap, restore, tints); false when none plays,
	// so the stage draws it as it always has.
	private bool DrawScriptedBackdrop(IDrawSurface s, Art art, StageCtx c, int backdrop, Point shift, bool front, int height, int ground)
	{
		var effects = new List<string>();
		Color tint = Color.White;
		float tintAlpha = 1f;
		Color tintAdd = Color.Transparent;
		IReadOnlyList<(Color From, Color To)>? tintPalette = null;
		float ox = 0, oy = 0;
		int image = _persistBackdrop ?? backdrop;
		if (_persistBackdrop is not null)
		{
			effects.Add("swap");
		}
		(int Image, float Alpha)? over = null;
		(float Amplitude, float Wavelength, long Period, int Band, long Local)? wave = null;
		foreach ((AnimRun run, long now, bool only) in Runs(c.Step, c.Elapsed).Concat(BackdropLoop()))
		{
			if (only)
			{
				continue;
			}
			// In start order, so a restore after a swap crossfades back from it whatever the order the script lists them in.
			foreach (AnimTrack t in run.Tracks.Where(t => t.Type is "backdrop" or "color" && now >= t.Start && Holds(run, t.Def)).OrderBy(t => t.Start))
			{
				bool persist = BattleAnimationRun.Bool(t.Def["persist"]) == true;
				// A swap's picture stays after its crossfade until its script ends (for the battle with `persist`) or a restore.
				bool active = t.Active(now) || persist || BattleAnimationRun.Str(t.Def["effect"]) == "swap";
				long local = now - t.Start;
				if (t.Type == "color")
				{
					if (BattleAnimationRun.Str(t.Def["on"]) != "backdrop" || !t.Active(now))
					{
						continue;
					}
					string mode = BattleAnimationRun.Str(t.Def["mode"]) ?? "set";
					if (c.Reduced && mode == "flash" && Reduced(t, "drop") == "drop")
					{
						continue;
					}
					(bool draw, float strength) = FlashGate(t, run, c.Reduced);
					if (!draw)
					{
						continue;
					}
					if (BattleAnimationEval.ColorAt(t.Def, t.T(now), local, t.Length, run, n => AnimColor(n, run.In.Look)) is { } ct)
					{
						if (strength < 1f)
						{
							ct = BattleAnimationEval.Lerp(new ColorTransform(Color.White, Color.Transparent, 1f, null), ct, strength);
						}
						if (c.Reduced && mode == "cycle" && BattleAnimationEval.Ct((t.Def["colors"] as JsonArray)?.FirstOrDefault(), n => AnimColor(n, run.In.Look)) is var first)
						{
							ct = first;
						}
						tint = ct.Multiply;
						tintAlpha = ct.Alpha;
						tintAdd = ct.Add;
						tintPalette = ct.Palette;
						effects.Add("tint");
					}
					continue;
				}
				if (!active)
				{
					continue;
				}
				string effect = BattleAnimationRun.Str(t.Def["effect"]) ?? "";
				effects.Add(effect);
				switch (effect)
				{
				case "scroll":
					if (t.Def["speed"] is JsonArray speed)
					{
						float frames = local / (float)BattleClock.Frame;
						ox += (float)(BattleAnimationRun.Num(speed[0]) ?? 0) * frames;
						oy += (float)(BattleAnimationRun.Num(speed[1]) ?? 0) * frames;
					}
					break;
				case "offset":
					if (t.Def["by"] is JsonArray by)
					{
						float e = BattleAnimationEval.Ease(BattleAnimationRun.Str(t.Def["ease"]) ?? "smooth", t.T(now));
						ox += BattleAnimationEval.Dist(by[0], 24, default, 1f) * e;
						oy += BattleAnimationEval.Dist(by[1], 24, default, 1f) * e;
					}
					break;
				case "wave":
					wave = ((float)(BattleAnimationRun.Num(t.Def["amplitude"]) ?? 3), (float)(BattleAnimationRun.Num(t.Def["wavelength"]) ?? 48),
						Math.Max(1, BattleAnimationRun.Time(t.Def["period"], run, 60 * BattleClock.Frame)), (int)(BattleAnimationRun.Num(t.Def["band"]) ?? 4), local);
					break;
				case "darken":
					tint = new Color(96, 96, 96);
					break;
				case "tint":
					tint = BattleAnimationEval.Ct(t.Def["to"], n => AnimColor(n, run.In.Look)).Multiply;
					break;
				case "swap":
				{
					int? next = BattleAnimationRun.Str(t.Def["image"]) is { } file ? AnimImage(s, file) : BattleAnimationRun.Str(t.Def["key"]) is { } key ? key == "generated" ? backdrop : art.BackdropByKey(key) : null;
					if (next is int n)
					{
						if (!t.Active(now) && persist)
						{
							_persistBackdrop = n;
						}
						float a = t.Length <= 0 || !t.Active(now) ? 1f : Math.Min(1f, t.T(now));
						over = (n, a);
						if (a >= 1f)
						{
							image = n;
							over = null;
						}
					}
					break;
				}
				case "restore":
					if (over is not null || image != backdrop)
					{
						int from = over?.Image ?? image;
						image = from;
						over = (backdrop, Math.Min(1f, t.T(now)));
						if (over.Value.Alpha >= 1f)
						{
							image = backdrop;
							over = null;
							_persistBackdrop = null;
						}
					}
					break;
				}
			}
		}
		_probeBackdropFx = effects.Count == 0 ? "none" : string.Join(", ", effects.Distinct());
		if (effects.Count == 0)
		{
			return false;
		}
		int w = s.Width + 16, h = front ? height : c.StageBottom + 8;
		// A color track's whole transform: the multiply in the tint, then its opacity, palette and added light.
		if (tintAlpha != 1f)
		{
			tint *= tintAlpha;
		}
		tintPalette = tintPalette is null ? null : Resolve(tintPalette, null);
		string paletteKey = tintPalette is { Count: > 0 } ? PaletteKey(tintPalette) : "";
		void Draw(int img, int x, int y, int dw, int dh, Color color)
		{
			s.Image(paletteKey.Length > 0 ? s.Derive(img, paletteKey, (px, _, _) => Recolor(px, tintPalette!)) : img, x, y, dw, dh, color);
			if (tintAdd.R > 0 || tintAdd.G > 0 || tintAdd.B > 0)
			{
				float a = color.A / 255f;
				s.ImageEx(s.Derive(img, "silhouette", Silhouette), x, y, dw, dh, new Color((byte)(tintAdd.R * a), (byte)(tintAdd.G * a), (byte)(tintAdd.B * a), (byte)0), false);
			}
		}
		void Plane(int img, Color color)
		{
			int x0 = shift.X - 8 + (int)MathF.Round(ox % w), y0 = shift.Y - 8 + (int)MathF.Round(oy);
			if (wave is { } wv)
			{
				for (int y = 0; y < h; y += wv.Band)
				{
					float phase = 2f * MathF.PI * ((y0 + y) / wv.Wavelength + BattleClock.Ratio(wv.Local % wv.Period, wv.Period));
					int dx = (int)MathF.Round(wv.Amplitude * MathF.Sin(phase));
					if (y0 + y + wv.Band <= 0 || y0 + y >= s.Height)
					{
						continue;
					}
					s.Clip(0, Math.Max(0, y0 + y), s.Width, Math.Max(1, Math.Min(wv.Band, s.Height - Math.Max(0, y0 + y))));
					Draw(img, x0 + dx, y0, w, h, color);
					s.Unclip();
				}
			}
			else
			{
				Draw(img, x0, y0, w, h, color);
				if (ox % w != 0)
				{
					Draw(img, x0 + (ox > 0 ? -w : w), y0, w, h, color);
				}
			}
			if (front && s.Height > height - 8)
			{
				int bottom = height - 8;
				s.Clip(0, bottom, s.Width, s.Height - bottom);
				Draw(img, shift.X - 8, shift.Y + bottom - ground * 62 / 100, s.Width + 16, ground, color);
				s.Unclip();
			}
		}
		Plane(image, tint);
		if (over is { } o)
		{
			Plane(o.Image, tint * o.Alpha);
		}
		return true;
	}

	private int? AnimImage(IDrawSurface s, string file)
	{
		if (_assets is null)
		{
			return null;
		}
		string full = _assets.Manifest.Full(file);
		if (!_animImages.TryGetValue(full, out int image))
		{
			if (!File.Exists(full))
			{
				return null;
			}
			Color[] pixels = s.ReadImage(full, out int w, out int h);
			image = _animImages[full] = s.CreateImage(w, h, pixels);
		}
		return image;
	}

	// The battle's looping backdrop script, on the controller's clock.
	private IEnumerable<(AnimRun Run, long Now, bool FiguresOnly)> BackdropLoop()
	{
		if (_backdropRun is { Loop: > 0 } run)
		{
			yield return (run, (long)(_clockMs - _backdropStartMs) * run.RateMilli % run.Loop, false);
		}
	}
}
