using System.Globalization;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The battle-level scripts: the transition into battle (a step of its own before the intro), the transition out (after
// the result screen, the field's input held), the action name's window, and the status scripts (idle while a status is
// shown, a tick on each round's banner).
public sealed partial class SampleController
{
	private AnimRun? _outro;
	private int _outroMs;
	private readonly Dictionary<(int, string), AnimRun> _statusIdle = [];

	// The transition into battle as its own step, when its script has a length.
	private BattleStep? TransitionStep()
	{
		AnimRun run = BattleRun("transition_in");
		if (run.Total <= 0 || run.Phases.Count == 0)
		{
			return null;
		}
		string kind = TransitionKind(run);
		return new BattleStep { Kind = StepKind.Transition, Description = "transition " + kind, Run = run, At = 0, Length = run.Total, LengthMs = (int)(run.Total / Math.Max(1, run.RateMilli)) };
	}

	private string TransitionKind(AnimRun run) =>
		run.Tracks.Where(t => t.Type == "transition").Select(t => Anim.Transitions.GetValueOrDefault(BattleAnimationRun.Str(t.Def["transition"]) ?? "")?["kind"])
			.Select(BattleAnimationRun.Str).OfType<string>().FirstOrDefault() ?? "none";

	// Confirm on the result screen: the transition out plays, then the field takes input again.
	private void EndBattleScreen()
	{
		AnimRun run = BattleRun("transition_out");
		if (run.Total > 0 && run.Phases.Count > 0)
		{
			_outro = run;
			_outroMs = 0;
			_log("present: transition out " + TransitionKind(run));
			return;
		}
		_battle.Active = false;
	}

	// The transition out's clock; true while it holds the field's input.
	private bool UpdateOutro(int dtMs)
	{
		if (_outro is not { } run)
		{
			return false;
		}
		_outroMs += dtMs;
		long now = (long)_outroMs * run.RateMilli;
		// The battle stays on screen under the cover; the field comes back for the reveal.
		if (_battle.Active && now >= run.PhaseStart("reveal"))
		{
			_battle.Active = false;
		}
		if (now >= run.Total)
		{
			_outro = null;
			return false;
		}
		return true;
	}

	// Which phase a battle transition is in and how far, 0 to 1 of the cover.
	private static (string Phase, long Local) TransitionPhase(AnimRun run, long now)
	{
		(string Name, long Start, long Length) p = run.PhaseAt(Math.Min(now, Math.Max(0, run.Total - 1)));
		return (p.Name, now - p.Start);
	}

	// A sequence's colors, the scene first (transparent): each phase's color in order.
	private static List<Color>? SequenceColors(JsonObject def) => def["phases"] is JsonArray phases
		? [Color.Transparent, .. phases.OfType<JsonObject>().Select(p => BattleAnimationEval.ParseColor(BattleAnimationRun.Str(p["color"]) ?? "#000000", _ => Color.Black))]
		: null;

	// A sequence transition: under `cover` the screen goes to each phase's color in turn, in `steps` steps `step_frames`
	// apart, and holds it `hold`; under `hold` the last color; under `reveal` the last color steps back to the next scene
	// at the last phase's rate.
	private void DrawSequence(IDrawSurface s, JsonObject def, AnimRun run, string phase, long local)
	{
		var phases = (def["phases"] as JsonArray ?? []).OfType<JsonObject>().ToList();
		List<Color> colors = SequenceColors(def)!;
		long Each(JsonObject p) => Math.Max(1, BattleAnimationRun.Time(p["step_frames"] ?? JsonValue.Create(4), run));
		int Steps(JsonObject p) => (int)(BattleAnimationRun.Num(p["steps"]) ?? 1);
		Color shown = colors[^1];
		if (phase == "cover")
		{
			long at = local;
			for (int i = 0; i < phases.Count; i++)
			{
				long ramp = Steps(phases[i]) * Each(phases[i]), hold = BattleAnimationRun.Time(phases[i]["hold"], run);
				if (at < ramp + hold)
				{
					float level = at >= ramp ? 1f : Math.Min(1f, (at / Each(phases[i]) + 1) / (float)Steps(phases[i]));
					shown = Color.Lerp(colors[i], colors[i + 1], level);
					_probeTransition += $" phase {i} {(at >= ramp ? "hold" : "step")}";
					break;
				}
				at -= ramp + hold;
			}
		}
		else if (phase == "reveal")
		{
			JsonObject last = phases[^1];
			float level = Math.Min(1f, (local / Each(last) + 1) / (float)Steps(last));
			shown = colors[^1] * (1f - level);
		}
		s.Fill(0, 0, s.Width, s.Height, shown);
	}

	// Draws the cover of a transition over the frame. `now` is script time; under `cover` the scene being left shows.
	private void DrawTransition(IDrawSurface s, AnimRun run, long now)
	{
		bool reduced = _s.Profile().ReducedFlash;
		foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "transition"))
		{
			if (Anim.Transitions.GetValueOrDefault(BattleAnimationRun.Str(t.Def["transition"]) ?? "") is not { } def)
			{
				continue;
			}
			string kind = BattleAnimationRun.Str(def["kind"]) ?? "fade";
			(string phase, long local) = TransitionPhase(run, now);
			long len = run.Phases.First(p => p.Name == phase).Length;
			float u = BattleAnimationEval.Ease(BattleAnimationRun.Str(def["ease"]) ?? "linear", BattleClock.Ratio(local, Math.Max(1, len)));
			float cover = phase switch { "cover" => u, "hold" => 1f, _ => 1f - u };
			Color color = BattleAnimationRun.Str(def["color"]) is { } c ? AnimColor(c, null) : Color.Black;
			_probeTransition = $"{kind} {phase} {Frames(local)}/{Frames(len)}";
			if (kind == "flash" && (reduced || !FlashAllowed(run, t.Index)))
			{
				kind = "fade";
			}
			if (kind == "sequence" && SequenceColors(def) is { } colors)
			{
				// A sequence whose steps raise the light by a flash's step counts each toward the flash limit; under reduced
				// flash or past the limit it plays as a fade to its last color.
				int flashes = colors.Zip(colors.Skip(1)).Count(p => Luma(p.Second) * p.Second.A / 255f - Luma(p.First) * p.First.A / 255f >= FlashStep);
				if (reduced || flashes > 0 && !FlashAllowed(run, t.Index, flashes))
				{
					kind = "fade";
					color = colors[^1];
				}
			}
			switch (kind)
			{
			case "sequence":
				DrawSequence(s, def, run, phase, local);
				break;
			case "fade":
			case "split" when phase != "reveal":
				if (def["steps"] is JsonValue sv && sv.TryGetValue(out int steps))
				{
					// The cover and the reveal may step at their own rates.
					JsonNode? stepFrames = phase == "reveal" ? def["reveal_step_frames"] ?? def["step_frames"] : def["cover_step_frames"] ?? def["step_frames"];
					long each = Math.Max(1, BattleAnimationRun.Time(stepFrames ?? JsonValue.Create(4), run));
					int level = (int)Math.Min(steps, local / each);
					cover = phase switch { "cover" => level / (float)steps, "hold" => 1f, _ => 1f - level / (float)steps };
				}
				s.Fill(0, 0, s.Width, s.Height, color * cover);
				break;
			case "split":
				break;
			case "mosaic" when phase == "hold":
				s.Fill(0, 0, s.Width, s.Height, color);
				break;
			case "mosaic":
				_probeTransition += $" block {MosaicBlock()}";
				break;
			case "flash":
			{
				int count = (int)(BattleAnimationRun.Num(def["count"]) ?? 2);
				long on = BattleAnimationRun.Time(def["on"] ?? JsonValue.Create(3), run), off = BattleAnimationRun.Time(def["off"] ?? JsonValue.Create(3), run);
				if (phase == "cover" && local < count * (on + off))
				{
					if (local % (on + off) < on)
					{
						s.Fill(0, 0, s.Width, s.Height, color);
					}
					break;
				}
				s.Fill(0, 0, s.Width, s.Height, Color.Black * cover);
				break;
			}
			case "wipe":
			{
				string from = BattleAnimationRun.Str(def["from"]) ?? "left";
				int w = (int)(s.Width * cover), h = (int)(s.Height * cover);
				(int x, int y, int ww, int hh) = from switch
				{
					"right" => (s.Width - w, 0, w, s.Height),
					"top" => (0, 0, s.Width, h),
					"bottom" => (0, s.Height - h, s.Width, h),
					_ => (0, 0, w, s.Height),
				};
				if (ww > 0 && hh > 0)
				{
					s.Fill(x, y, ww, hh, color);
				}
				break;
			}
			case "iris":
			{
				Vector2 center = new(s.Width / 2f, s.Height / 2f);
				string shape = BattleAnimationRun.Str(def["shape"]) ?? "circle";
				float radius = (1f - cover) * MathF.Sqrt(s.Width * s.Width + s.Height * s.Height) / 2f;
				for (int y = 0; y < s.Height; y += 4)
				{
					float dy = MathF.Abs(y + 2 - center.Y);
					float half = shape switch
					{
						"square" => dy <= radius ? radius : -1,
						"diamond" => radius - dy,
						_ => dy <= radius ? MathF.Sqrt(radius * radius - dy * dy) : -1,
					};
					if (half < 0)
					{
						s.Fill(0, y, s.Width, 4, color);
						continue;
					}
					int l = (int)(center.X - half), r = (int)(center.X + half);
					if (l > 0)
					{
						s.Fill(0, y, l, 4, color);
					}
					if (r < s.Width)
					{
						s.Fill(r, y, s.Width - r, 4, color);
					}
				}
				break;
			}
			case "tiles":
			case "spiral":
			{
				int tile = (int)(BattleAnimationRun.Num(def["tile"]) ?? 40);
				int cols = (s.Width + tile - 1) / tile, rows = (s.Height + tile - 1) / tile;
				var cells = Enumerable.Range(0, cols * rows).Select(k => (X: k % cols, Y: k / cols)).ToList();
				string order = kind == "spiral" ? "spiral" : BattleAnimationRun.Str(def["order"]) ?? "rows";
				float cx = (cols - 1) / 2f, cy = (rows - 1) / 2f;
				cells = order switch
				{
					"columns" => [.. cells.OrderBy(c => c.X).ThenBy(c => c.Y)],
					"checker" => [.. cells.OrderBy(c => (c.X + c.Y) % 2).ThenBy(c => c.Y).ThenBy(c => c.X)],
					"diagonal" => [.. cells.OrderBy(c => c.X + c.Y).ThenBy(c => c.X)],
					"random" => [.. cells.OrderBy(c => Noise.Hash(c.X, c.Y, 7))],
					"spiral" or "spiral_in" => [.. cells.OrderBy(c => MathF.Max(MathF.Abs(c.X - cx), MathF.Abs(c.Y - cy))).ThenBy(c => MathF.Atan2(c.Y - cy, c.X - cx))],
					_ => cells,
				};
				if (order == "spiral_in" || kind == "spiral" && BattleAnimationRun.Str(def["from"]) == "edge")
				{
					cells.Reverse();
				}
				int covered = (int)MathF.Round(cover * cells.Count);
				foreach ((int x, int y) in cells.Take(covered))
				{
					s.Fill(x * tile, y * tile, tile, tile, color);
				}
				break;
			}
			}
		}
	}

	// A mosaic transition's block size now, in logical pixels: under `cover` the scene breaks into blocks growing to
	// `block` in `steps` levels, under `reveal` the next scene sharpens from them; 0 while none plays or it holds its color.
	private int MosaicBlock()
	{
		AnimRun? run = Screen == Screen.Battle && _battle.Phase == BattlePhase.Playback && _timeline.Current is { Kind: StepKind.Transition, Run: { } intro } ? intro : _outro;
		if (run is null)
		{
			return 0;
		}
		long now = run == _outro ? (long)_outroMs * run.RateMilli : _timeline.Elapsed;
		foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "transition"))
		{
			if (Anim.Transitions.GetValueOrDefault(BattleAnimationRun.Str(t.Def["transition"]) ?? "") is { } def && BattleAnimationRun.Str(def["kind"]) == "mosaic")
			{
				(string phase, long local) = TransitionPhase(run, now);
				if (phase is not ("cover" or "reveal"))
				{
					return 0;
				}
				long len = Math.Max(1, run.Phases.First(p => p.Name == phase).Length);
				float u = BattleAnimationEval.Ease(BattleAnimationRun.Str(def["ease"]) ?? "linear", BattleClock.Ratio(local, len));
				int block = (int)(BattleAnimationRun.Num(def["block"]) ?? 32), steps = Math.Max(1, (int)(BattleAnimationRun.Num(def["steps"]) ?? 4));
				return Math.Max(1, (int)MathF.Ceiling((phase == "cover" ? u : 1f - u) * steps)) * block / steps;
			}
		}
		return 0;
	}

	// The split reveal: the backdrop slides in from one side and the figures from the other.
	private (int Backdrop, int Figures, bool Vertical) SplitShift(AnimRun run, long now)
	{
		foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "transition"))
		{
			if (Anim.Transitions.GetValueOrDefault(BattleAnimationRun.Str(t.Def["transition"]) ?? "") is { } def && BattleAnimationRun.Str(def["kind"]) == "split")
			{
				(string phase, long local) = TransitionPhase(run, now);
				bool vertical = BattleAnimationRun.Str(def["axis"]) == "vertical";
				if (phase != "reveal")
				{
					return (0, 0, vertical);
				}
				float speed = (float)(BattleAnimationRun.Num(def["speed"]) ?? 16);
				long frames = BattleAnimationRun.Time(def["frames"] ?? JsonValue.Create(20), run);
				float left = Math.Max(0, (frames - local) / (float)BattleClock.Frame) * speed;
				return (-(int)left, (int)left, vertical);
			}
		}
		return (0, 0, false);
	}

	// The name plate's opacity when the step's script shapes it with name_window tracks; null leaves it to the layout.
	private float? NameWindow(BattleStep? step)
	{
		if (step?.Run is not { } run || !run.Tracks.Any(t => t.Type == "name_window"))
		{
			return null;
		}
		long now = step.At + _timeline.Elapsed;
		float best = 0f;
		foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "name_window" && t.Active(now)))
		{
			long fade = BattleAnimationRun.Time(t.Def["fade"], run);
			long local = now - t.Start;
			float a = fade <= 0 ? 1f : Math.Min(1f, Math.Min(BattleClock.Ratio(local, fade), BattleClock.Ratio(t.Length - local, fade)));
			best = Math.Max(best, a);
		}
		return best;
	}

	// The idle script of each status a figure shows (`status:ID/idle`), looping on the controller's clock.
	private IEnumerable<(AnimRun Run, long Now)> StatusIdle(int i, ShownCombatant sc)
	{
		foreach (string status in sc.Statuses)
		{
			if (Anim.Find($"status:{status}/idle", StageLayout() == BattleLayout.Front) is not { } id)
			{
				continue;
			}
			if (!_statusIdle.TryGetValue((i, status), out AnimRun? run) || run.ScriptId != id)
			{
				AnimInputs inputs = Inputs(null, null, [], 0);
				inputs.Self = i;
				run = _statusIdle[(i, status)] = BattleAnimationRun.Instantiate(Anim, id, $"status:{status}/idle", "reaction", inputs, [], 60 * BattleClock.Frame);
			}
			if (run.Loop > 0)
			{
				yield return (run, (long)_clockMs * run.RateMilli % run.Loop);
			}
		}
	}

	// Each round's banner ticks every status with a `status:ID/tick` script on the figures that show it.
	private void StatusTicks(BattleStep step)
	{
		for (int i = 0; i < _show.All.Count; i++)
		{
			foreach (string status in _show.All[i].Statuses)
			{
				if (Anim.Find($"status:{status}/tick", StageLayout() == BattleLayout.Front) is not { } id)
				{
					continue;
				}
				AnimInputs inputs = Inputs(null, null, [], step.Length);
				inputs.Self = i;
				AnimRun run = BattleAnimationRun.Instantiate(Anim, id, $"status:{status}/tick", "reaction", inputs, [], step.Length);
				step.Reactions.Add(new Reaction { Run = run, Key = $"status:{status}/tick", Figure = i, Start = 0 });
				AnimLog($"tick {status} {_show.All[i].Label} @{Frames(step.At)}");
			}
		}
	}
}
