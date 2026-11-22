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
			switch (kind)
			{
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
			if (Anim.Find($"status:{status}/idle") is not { } id)
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
				if (Anim.Find($"status:{status}/tick") is not { } id)
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
