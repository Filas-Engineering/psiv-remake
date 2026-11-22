using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// The animation previewer and console commands (--dev): a select key or script played on dummies, seeking and
// stepping by frame (backward too, since every frame is drawn from script time), the frame ruler overlay, and `anim why`
// and `anim expand`.
public sealed partial class SampleController
{
	private const Keys StepBackKey = Keys.OemComma;

	// The script the previewer forces for the kind of step it previews, and what it was asked to play.
	private string? _forcedScript;
	private string? _forcedReaction;
	private string? _animPreview;
	private string _animPreviewLayout = "";
	private string _animPreviewEnemy = "";
	private int _animPreviewCount = 3;
	private int _animPreviewTargets;

	private string ScriptKind(string id)
	{
		JsonObject? s = Anim.Scripts.GetValueOrDefault(id);
		for (int depth = 0; s is not null && depth < 9; depth++)
		{
			if (s["phases"] is JsonObject phases && phases.Count > 0)
			{
				string first = phases.First().Key;
				return BattleAnimation.TurnPhases.Contains(first) ? "turn" : first switch { "step" => "step", "change" => "change", _ => "battle" };
			}
			if (s.ContainsKey("length"))
			{
				return "reaction";
			}
			s = BattleAnimationRun.Str(s["extends"]) is { } parent ? Anim.Scripts.GetValueOrDefault(parent) : null;
		}
		return "turn";
	}

	// `anim ...` in the console.
	private bool AnimCommand(string[] w)
	{
		switch (w)
		{
		case ["anim", "why", var key]:
		{
			string? id = Anim.Find(key);
			DevLog($"{key} -> {id ?? "-"}{(id is null ? "" : $" ({ScriptKind(id)} script{(Anim.FromFile ? ", over the built-in set" : ", built-in")})")}");
			if (_timeline.Current is { Lookup.Count: > 0 } step)
			{
				DevLog("this step: " + string.Join(" | ", step.Lookup));
			}
			return true;
		}
		case ["anim", "expand", var key]:
		{
			string? id = Anim.Find(key) ?? (Anim.Scripts.ContainsKey(key) ? key : null);
			if (id is null)
			{
				DevLog($"refused: no script for '{key}'", bad: true);
				return false;
			}
			string json = Expand(id, key).ToJsonString(new JsonSerializerOptions { WriteIndented = false });
			_log("anim: " + json);
			DevLog(json);
			return true;
		}
		case ["anim", "seek", var n] when int.TryParse(n, CultureInfo.InvariantCulture, out int frame) && _animPreview is not null:
			AnimSeek(frame * BattleClock.Frame);
			return true;
		case ["anim", "step", var n] when int.TryParse(n, CultureInfo.InvariantCulture, out int frames) && _animPreview is not null:
			AnimSeek(PreviewFrame() + frames * BattleClock.Frame);
			return true;
		case ["preview", "anim", var key, .. var rest]:
			_animPreviewLayout = "";
			_animPreviewEnemy = "";
			_animPreviewCount = 3;
			_animPreviewTargets = 0;
			for (int k = 0; k + 1 < rest.Length; k += 2)
			{
				switch (rest[k])
				{
				case "layout":
					_animPreviewLayout = rest[k + 1];
					break;
				case "enemy":
					_animPreviewEnemy = rest[k + 1];
					break;
				case "count" when int.TryParse(rest[k + 1], CultureInfo.InvariantCulture, out int count) && count is >= 1 and <= 8:
					_animPreviewCount = count;
					break;
				case "targets" when int.TryParse(rest[k + 1], CultureInfo.InvariantCulture, out int targets) && targets is >= 1 and <= 8:
					_animPreviewTargets = targets;
					break;
				default:
					DevLog(T("ui.dev.unreadable"), bad: true);
					return false;
				}
			}
			return StartAnimPreview(key);
		}
		DevLog(T("ui.dev.unreadable"), bad: true);
		return false;
	}

	// The script a key selects, flattened, with the look and timings of the key's action filled in: JSON to paste into
	// a file.
	private JsonObject Expand(string id, string key)
	{
		ActionLook? look = null;
		string[] parts = key.Split(':', 2);
		if (parts is ["ability" or "item", var ability])
		{
			string a = ability.Split('/')[0];
			look = PresentationTable.For(a, _data.Actions.GetValueOrDefault(a) ?? new ActionShape("technique", "damage", "none"));
		}
		else if (parts is ["change", var change])
		{
			string[] ce = change.Split('.', 2);
			look = PresentationTable.ChangeFor(ce[0] is "join" or "merge" or "transform" ? ce[0] : "merge", ce.Length > 1 ? ce[1] : "");
		}
		look ??= PresentationTable.For("", new ActionShape("attack", "damage", "none"));
		string kind = ScriptKind(id);
		AnimInputs inputs = Inputs(null, look, [], 0);
		List<(string, long, bool)> phases = kind switch
		{
			"turn" => [.. BattleAnimation.TurnPhases.Select(p => (p, (long)(p switch { "animate" => look.AnimateMs, "effect" => look.EffectMs, _ => PresentationTable.Ms(p) }) * Anim.RateMilli, true))],
			"step" => [("step", (long)PresentationTable.Ms("round") * Anim.RateMilli, true)],
			"change" => [("change", (long)look.EffectMs * Anim.RateMilli, true)],
			"battle" => [.. BattleAnimation.BattlePhases.Select(p => (p, 0L, true))],
			_ => [],
		};
		AnimRun run = BattleAnimationRun.Instantiate(Anim, id, key, kind, inputs, phases, 60 * BattleClock.Frame);
		JsonNode Time(long units) => units % BattleClock.Frame != 0 && units % run.RateMilli == 0 ? $"{units / run.RateMilli}ms" : (int)(units / BattleClock.Frame);
		JsonNode? Fill(JsonNode? n) => n switch
		{
			JsonObject o => new JsonObject(o.Select(p => KeyValuePair.Create(p.Key, Fill(p.Value)))),
			JsonArray a => new JsonArray([.. a.Select(Fill)]),
			JsonValue v when BattleAnimationRun.Str(v) is { } s && s.StartsWith("$look.", StringComparison.Ordinal) => s switch
			{
				"$look.effect" => look.Effect, "$look.cue" => look.Cue, "$look.animation" => look.Animation, "$look.shake" => look.Shake, "$look.flash" => look.Flash,
				"$look.animate_ms" => $"{look.AnimateMs}ms", "$look.effect_ms" => $"{look.EffectMs}ms", _ => v.DeepClone(),
			},
			_ => n?.DeepClone(),
		};
		var tracks = new JsonArray();
		foreach (AnimTrack t in run.Tracks)
		{
			var def = (JsonObject)Fill(t.Def)!;
			def["at"] = Time(t.Start);
			if (t.Type is not ("sound" or "release"))
			{
				def["length"] = Time(t.Length);
			}
			if (t.Type == "release")
			{
				def["at"] = new JsonArray([.. t.Ats.Select(Time)]);
			}
			tracks.Add(def);
		}
		var result = new JsonObject();
		if (run.Phases.Count > 0)
		{
			result["phases"] = new JsonObject(run.Phases.Select(p => KeyValuePair.Create(p.Name, (JsonNode?)Time(p.Length))));
		}
		else
		{
			result["length"] = Time(run.Total);
		}
		result["tracks"] = tracks;
		return new JsonObject { [id] = result };
	}

	// A select key or script on the previewer's dummies: a turn of the key's shape, and for a change, reaction or step
	// script the moment it plays on.
	private bool StartAnimPreview(string key)
	{
		string? id = Anim.Find(key) ?? (Anim.Scripts.ContainsKey(key) ? key : null);
		if (id is null)
		{
			DevLog($"refused: no script for '{key}'", bad: true);
			return false;
		}
		string kind = ScriptKind(id);
		// A key of an encounter (`encounter:ENC/...`, or ending in `/encounter:ENC`) plays in that encounter, on dummies of an
		// enemy it lists unless the key or `enemy` names one.
		string encounter = key.Split('/').FirstOrDefault(p => p.StartsWith("encounter:", StringComparison.Ordinal))?[10..] ?? "";
		string inner = key.StartsWith("encounter:", StringComparison.Ordinal) && key.Contains('/') ? key[(key.IndexOf('/') + 1)..] : key;
		// An enemy's key has a dummy enemy act on the members; an item's has a member use it (a worn piece its use).
		string[] head = inner.Split('/')[0].Split(':', 2);
		string? enemyAction = inner.Split('/') is [var e, var action, ..] && e.StartsWith("enemy:", StringComparison.Ordinal) ? action : null;
		string actionId = head is ["item", var piece] && _data.PieceUses.TryGetValue(piece, out string? use) ? use
			: head is ["ability" or "item", var a0] ? a0 : enemyAction is not null and not ("attack" or "defend") ? enemyAction : "";
		string entry = PresentationTable.Actions.ContainsKey(actionId) ? actionId
			: _data.Actions.TryGetValue(actionId, out ActionShape? shape) ? $"{shape.Kind}.{shape.Effect}"
			: enemyAction == "defend" ? "defend"
			: kind == "reaction" || inner.StartsWith("weapon:", StringComparison.Ordinal) || enemyAction is not null ? "attack" : "technique.damage";
		if (!PresentationTable.Actions.ContainsKey(entry) && !PresentationTable.Fallbacks.ContainsKey(entry))
		{
			entry = "technique.damage";
		}
		if (_animPreviewLayout.Length > 0)
		{
			SetBattleLayout(_animPreviewLayout);
		}
		_forcedScript = id;
		_forcedReaction = kind == "reaction" ? id : null;
		string? dummy = _animPreviewEnemy.Length > 0 ? _animPreviewEnemy : enemyAction is not null ? head[1]
			: encounter.Length > 0 ? _data.EnemyEncounters.Where(p => p.Value == encounter).Select(p => p.Key).Order(StringComparer.Ordinal).FirstOrDefault() : null;
		bool ok = StartStagePreview(entry, inner.StartsWith("change:", StringComparison.Ordinal) ? inner[7..] : null, dummy, inner.StartsWith("combo:", StringComparison.Ordinal),
			_animPreviewCount, _animPreviewTargets, enemyAction is not null, head[0] == "item", encounter);
		_forcedScript = null;
		if (ok)
		{
			_animPreview = key;
			_overlays.Add("anim");
			AnimLog($"preview {id} ({key})");
		}
		return ok;
	}

	// The previewed run's script time now.
	private long PreviewFrame() => _timeline.Current is { } step ? step.At + _timeline.Elapsed : 0;

	// Seeks the previewed script to `target` script time: the preview starts again and plays quietly up to it, which
	// draws the same as playing there since every track is drawn from script time.
	private void AnimSeek(long target)
	{
		if (_animPreview is not { } key)
		{
			return;
		}
		target = Math.Max(0, target);
		bool overlay = _overlays.Contains("anim");
		StartAnimPreview(key);
		if (!overlay)
		{
			_overlays.Remove("anim");
		}
		string? id = Anim.Find(key) ?? key;
		for (int guard = 0; guard < 64; guard++)
		{
			foreach (BattleStep started in _timeline.Current is null ? _timeline.Advance(0, 100) : [])
			{
				PlayStep(started, quiet: true);
			}
			if (_timeline.Current is not { } step)
			{
				break;
			}
			if (step.Run?.ScriptId == id && step.At + step.Length > target || !_timeline.Pending.Any())
			{
				long into = Math.Clamp(target - step.At, 0, Math.Max(0, step.Length - 1));
				_timeline.Seek(into);
				ReleaseDue(step, into, quiet: true);
				break;
			}
			CutStep(step, 0, quiet: true, "seek");
			foreach (BattleStep next in _timeline.Hurry())
			{
				PlayStep(next, quiet: true);
			}
		}
		_paused = true;
		// The windows show the seeked frame as playing there would have: none is still opening or closing.
		Ui.Settle(StageLayout() == BattleLayout.Front ? "battle_front" : "battle_side");
		AnimLog($"seek @{Frames(PreviewFrame())}");
	}

	// The frame ruler under the stage while a script is previewed: phases, tracks, sounds, releases and the frame.
	private void DrawAnimRuler(IDrawSurface s)
	{
		if (_animPreview is null || _timeline.Current is not { Run: { } run } step || !_overlays.Contains("anim"))
		{
			return;
		}
		long now = step.At + _timeline.Elapsed;
		int x0 = 16, width = s.Width - 32, y = s.Height - 150;
		long total = Math.Max(1, run.Total);
		int X(long t) => x0 + (int)(width * Math.Clamp(t, 0, total) / total);
		s.Fill(x0 - 4, y - 4, width + 8, 140, Color.Black * 0.75f);
		foreach ((string name, long start, long length, bool present) in run.Phases.Where(p => p.Present))
		{
			s.Fill(X(start), y, Math.Max(1, X(start + length) - X(start) - 1), 14, new Color(64, 84, 140));
			s.Text(name, X(start) + 2, y, Style.Text, OverlayText);
		}
		int row = 0;
		foreach (AnimTrack t in run.Tracks.Take(7))
		{
			int ty = y + 18 + row++ * 14;
			Color color = t.Active(now) ? Style.Accent : Style.Dim;
			s.Fill(X(t.Start), ty + 4, Math.Max(2, X(t.End) - X(t.Start)), 6, color);
			s.Text($"{t.Label} {t.Type}", x0 + width - 200, ty, color, OverlayText);
		}
		foreach (ReleaseUnit u in step.Units)
		{
			s.Fill(X(step.At + u.At), y + 118, 2, 10, u.Released ? Style.Good : Style.Bad);
		}
		s.Fill(X(now), y - 4, 2, 140, Color.White);
		s.Text($"{run.ScriptId} frame {Frames(now)}/{Frames(run.Total)}  released {Released(step)}", x0, y + 122, Style.Text, OverlayText);
		_overlayDrawn["anim"] = $"{run.ScriptId} {Frames(now)}";
	}
}
