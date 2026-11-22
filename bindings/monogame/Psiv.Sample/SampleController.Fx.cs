using Microsoft.Xna.Framework;

namespace Psiv.Sample;

// The dialogue's `fx` tag: a step script of screen-level tracks (flash, color on the stage or the frame, shake, sound)
// played over whatever the screen draws, outside battle, on the controller's clock.
public sealed partial class SampleController
{
	private AnimRun? _fx;
	private int _fxStartMs;
	private long _fxPlayed = -1;

	private void StartFx(string id)
	{
		AnimInputs inputs = Inputs(null, null, [], 0);
		_fx = BattleAnimationRun.Instantiate(Anim, id, "fx", "step", inputs, [("step", 60 * BattleClock.Frame, true)]);
		_fxStartMs = _clockMs;
		_fxPlayed = -1;
		_log("fx: " + id);
	}

	private long FxNow() => _fx is null ? 0 : (long)(_clockMs - _fxStartMs) * _fx.RateMilli;

	// Sounds the fx reached since the last frame; the fx ends with its script.
	private void UpdateFx()
	{
		if (_fx is not { } run)
		{
			return;
		}
		long now = FxNow();
		foreach (AnimTrack t in run.Tracks.Where(t => t.Type == "sound"))
		{
			SoundsOf(run, t, _fxPlayed, now, inclusive: false);
		}
		_fxPlayed = now;
		if (now >= run.Total)
		{
			_fx = null;
		}
	}

	private StageCtx FxStage(IDrawSurface s) => new()
	{
		Step = new BattleStep { Run = _fx, Length = _fx?.Total ?? 0 }, Elapsed = FxNow(), Front = true, Motion = 1f, Reduced = _s.Profile().ReducedFlash, All = [],
		StageBottom = s.Height, Width = s.Width, Height = s.Height,
	};

	private void DrawFx(IDrawSurface s)
	{
		if (_fx is null)
		{
			return;
		}
		StageCtx c = FxStage(s);
		foreach ((Rectangle area, Color color) in Fills(c, "stage", out _).Concat(Fills(c, "frame", out _)))
		{
			s.Fill(0, 0, s.Width, s.Height, color);
		}
	}

	private int FxShake() => _fx is null || _s is null ? 0 : (int)MathF.Round(Shake(new StageCtx { Step = new BattleStep { Run = _fx, Length = _fx.Total }, Elapsed = FxNow(), Reduced = _s.Profile().ReducedFlash, All = [] }, out _, out _, out _));
}
