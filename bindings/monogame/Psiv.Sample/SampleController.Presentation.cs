using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	// Script actions play for their own length, held between these so the sample stays brisk.
	private const int ShortestAnimationMs = 150;
	private const int LongestAnimationMs = 600;

	// A presentation action being played: the session waits on it until FinishAction. Only an action the sample has
	// no picture for (`labeled`) slides its subject across the top.
	private sealed class Animation(PresentationAction action, int lengthMs, bool labeled = false)
	{
		public PresentationAction Action { get; } = action;
		public int LengthMs { get; } = lengthMs;
		public bool Labeled { get; } = labeled;
		public int ElapsedMs { get; set; }
		public float Progress => LengthMs == 0 ? 1f : Math.Clamp(ElapsedMs / (float)LengthMs, 0f, 1f);
	}

	private readonly List<Animation> _animations = [];
	// The screen's darkness after the last fade, 0 to 1.
	private float _darkness;

	// The labels sliding across the top, one per playing action that has no picture of its own.
	public IEnumerable<string> Labels => _animations.Where(a => a.Labeled).Select(a => LabelOf(a.Action));

	private static string LabelOf(PresentationAction a) => a.Subject.Length > 0 ? a.Subject : a.Description;

	private void Present(PresentationAction a, FieldView? view, List<string> lineup)
	{
		if (a.Phase == ActionPhase.Cancel)
		{
			_animations.RemoveAll(x => x.Action.Handle == a.Handle);
			_field.StopPose(a.Handle);
			EndSceneAction(a);
			_log($"action cancelled: {a.Handle}");
			return;
		}
		_log("action: " + a.Description + (a.Instant ? " (instant)" : ""));
		// Scenes, sound and warps have their own presentation; the length is what the event waits, 0 for done at once.
		int? ownMs = a.Type switch
		{
			ActionType.PanelShow or ActionType.PanelHide or ActionType.PanelClear or ActionType.CameraCut or ActionType.CameraMove or ActionType.Shake => PresentScene(a),
			ActionType.MusicPlay or ActionType.MusicStop or ActionType.MusicFade or ActionType.AmbientPlay or ActionType.AmbientStop or ActionType.Sound => PresentAudio(a),
			// The new area's banner shows the arrival; the session has already moved the party.
			ActionType.Warp => 0,
			_ => null,
		};
		if (ownMs is { } own)
		{
			if (own == 0 || a.Instant)
			{
				if (!a.Instant)
				{
					_s.FinishAction(a.Handle);
				}
				return;
			}
			var animation = new Animation(a, own);
			_animations.Add(animation);
			FollowSceneAction(animation);
			return;
		}
		bool actor = view?.Actors.Any(x => x.Id == a.Subject) ?? false;
		// An actor outside the party's area has nothing here to show, so its action is done at once.
		if (!actor && _actorIds.Contains(a.Subject))
		{
			if (!a.Instant)
			{
				_s.FinishAction(a.Handle);
			}
			return;
		}
		// Any other subject is a party member, in the lineup or not. One outside it that a turn or pose finds without a
		// cell steps up next to the leader first.
		bool member = !actor && a.Subject.Length > 0;
		if (member && view is not null && a.Type is ActionType.Face or ActionType.Animate)
		{
			_field.Arrive(a.Subject, lineup, view);
		}
		int? spriteMs = view is null ? null : a.Type switch
		{
			ActionType.Move or ActionType.Face or ActionType.ShowActor or ActionType.HideActor when actor => ActorAction(a, view),
			ActionType.Move or ActionType.Face when member => MemberAction(a, view, lineup),
			ActionType.Animate when member || actor => PoseAction(a),
			_ => null,
		};
		if (a.Instant)
		{
			EndAnimation(a);
			return;
		}
		if (spriteMs == 0)
		{
			_s.FinishAction(a.Handle);
			return;
		}
		if (spriteMs is { } length)
		{
			_animations.Add(new Animation(a, length));
			return;
		}
		if (a.Type is ActionType.FadeIn or ActionType.FadeOut)
		{
			_animations.Add(new Animation(a, Math.Clamp((int)(a.Seconds * 1000), ShortestAnimationMs, LongestAnimationMs)));
			return;
		}
		int labelLength = a.Type == ActionType.Move ? Math.Clamp((int)(a.Seconds * 1000), ShortestAnimationMs, LongestAnimationMs) : ShortestAnimationMs;
		_animations.Add(new Animation(a, labelLength, labeled: true));
	}

	// Milliseconds per cell of a scripted walk: the area's step time, faster or slower with the move's speed.
	private int CellMs(PresentationAction a) => (int)(Math.Max(1, _s.FieldInfo()!.StepMs) / (a.Speed > 0 ? a.Speed : 1.0));

	// A script actor's move or turn plays on its sprite: the session already reports where it ends, and the sprite
	// walks there from where it was drawn. Null when the actor has not been drawn yet.
	private int? ActorAction(PresentationAction a, FieldView view)
	{
		FieldActor actor = view.Actors.First(x => x.Id == a.Subject);
		if (_field.ActorCell(actor.Id) is not { } from)
		{
			return null;
		}
		if (a.Type != ActionType.Move || a.Instant)
		{
			return ShortestAnimationMs;
		}
		// The sprite walks there a cell per step time; the event waits until it arrives.
		int cellMs = CellMs(a);
		_field.Pace(actor.Id, cellMs);
		int cells = Math.Abs(actor.X - from.X) + Math.Abs(actor.Y - from.Y);
		return cells > 0 ? cells * cellMs : ShortestAnimationMs;
	}

	// A party member's move or turn is presentation the game does not keep (it keeps only the leader's cell, which a
	// script does not move), so the sample follows it: a move walks the member to the marker or position, a turn faces
	// it toward a marker, actor or member. Instant records (the rebuild after a skip or a restore) put the end state
	// up at once. Null when the target is not in the area.
	private int? MemberAction(PresentationAction a, FieldView view, List<string> lineup)
	{
		(int X, int Y)? target = a.Type == ActionType.Move && a.HasPosition
			? ((int)Math.Round(a.X, MidpointRounding.AwayFromZero), (int)Math.Round(a.Z, MidpointRounding.AwayFromZero))
			: view.Markers.FirstOrDefault(m => m.Name == a.Target) is { } marker ? (marker.X, marker.Y)
			: a.Type == ActionType.Face && view.Actors.FirstOrDefault(x => x.Id == a.Target) is { } actor ? _field.ActorCell(actor.Id) ?? (actor.X, actor.Y)
			: a.Type == ActionType.Face ? _field.MemberCell(a.Target, lineup)
			: null;
		if (target is not { } cell)
		{
			return null;
		}
		if (a.Type == ActionType.Face)
		{
			_field.FaceMember(a.Subject, lineup, cell.X, cell.Y);
			return ShortestAnimationMs;
		}
		int cellMs = a.Instant ? 0 : CellMs(a);
		int cells = _field.MoveMember(a.Subject, lineup, cell.X, cell.Y, cellMs);
		return cells > 0 ? cells * cellMs : ShortestAnimationMs;
	}

	// An `anim` plays a pose on the person. A looping one repeats until another pose replaces it or the scene ends,
	// and is done as soon as it starts, as the script format says; the event waits for one play of any other.
	private int PoseAction(PresentationAction a)
	{
		Pose pose = Art.PoseFor(a.Target);
		_field.StartPose(a.Subject, pose, a.Handle, _clockMs, a.Loop);
		return a.Loop ? 0 : Art.PoseLengthMs(pose);
	}

	// The end state of an action, for one that finished or was put up at once.
	private void EndAnimation(PresentationAction a)
	{
		if (a.Type == ActionType.FadeOut)
		{
			_darkness = 1f;
		}
		else if (a.Type == ActionType.FadeIn)
		{
			_darkness = 0f;
		}
	}

	private void AdvanceAnimations(int dtMs)
	{
		for (int i = 0; i < _animations.Count; i++)
		{
			Animation animation = _animations[i];
			animation.ElapsedMs += dtMs;
			if (animation.ElapsedMs >= animation.LengthMs)
			{
				EndAnimation(animation.Action);
				EndSceneAction(animation.Action);
				_s.FinishAction(animation.Action.Handle);
				_animations.RemoveAt(i--);
			}
		}
	}

	private void DrawAnimations(IDrawSurface s)
	{
		float darkness = _darkness;
		foreach (Animation a in _animations)
		{
			switch (a.Action.Type)
			{
			case ActionType.FadeIn:
				darkness = 1f - a.Progress;
				break;
			case ActionType.FadeOut:
				darkness = a.Progress;
				break;
			case var _ when a.Labeled:
				string label = LabelOf(a.Action);
				int x = 20 + (int)((s.Width - s.Measure(label, Style.Small) - 64) * a.Progress);
				Style.Plate(s, label, x, 52, Style.Accent);
				break;
			}
		}
		if (darkness > 0f)
		{
			s.Fill(0, 0, s.Width, s.Height, Color.Black * darkness);
		}
	}
}
