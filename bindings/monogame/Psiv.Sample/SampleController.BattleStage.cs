using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The battle drawn in either layout (the presentation table's, SampleController.BattleFront.cs for the front one's own
// parts): the backdrop, the figures, the playback's steps on them (stepping forward, banners, animations, effects, hit
// reactions, floating numbers, bars, status icons, falls), the party's panels or status strip and the command menu, the
// log when it is shown, and the result screen.
public sealed partial class SampleController
{
	private const int BattlePanelTop = 420;
	private const int BattleMenuWidth = 300;
	private const int BattleLogLines = 5;
	private const int EffectScale = 3;

	private int _probeShake;
	private bool _probeFlash;
	private string _probeVictory = "-";
	private BattleLayout? _layoutOverride;
	// The layout the last battle frame was drawn in.
	private BattleLayout _stageLayout = BattleLayout.Front;

	// The flag's or script's layout over the presentation table's; "data" goes back to the table's.
	public bool SetBattleLayout(string name)
	{
		if (name == "data")
		{
			_layoutOverride = null;
			return true;
		}
		if (BattlePresentation.ParseLayout(name) is not { } layout)
		{
			return false;
		}
		_layoutOverride = layout;
		return true;
	}

	private BattleLayout StageLayout() => _layoutOverride ?? PresentationTable.LayoutFor(StageInfo()?.Encounter ?? "", _s.FieldInfo()?.AreaId ?? "");

	// Where a combatant stands in the side layout: its center x, the y of its feet and its size. Enemies fill the left by
	// formation position over `enemyCount` places, in one row, or in two ranks from five on, the first ones in front; the
	// party stands in a diagonal on the right.
	private static (int X, int Foot, int Size) Place(int index, int partyCount, int position, int enemyCount)
	{
		if (index < partyCount)
		{
			return (912 + index * 68, 236 + index * 40, 80);
		}
		int j = position;
		int size = enemyCount <= 3 ? 112 : enemyCount <= 5 ? 96 : 80;
		if (enemyCount <= 4)
		{
			int spacing = Math.Min(200, 640 / enemyCount);
			return (420 + (int)((j - (enemyCount - 1) / 2f) * spacing), 352 - j % 2 * 28, size);
		}
		int front = (enemyCount + 1) / 2;
		bool back = j >= front;
		int k = back ? j - front : j, m = back ? enemyCount - front : front;
		int rankSpacing = Math.Min(170, 640 / m);
		return (420 + (int)((k - (m - 1) / 2f) * rankSpacing) + (back ? rankSpacing / 2 : 0), back ? 292 : 376, size);
	}

	private void DrawBattle(IDrawSurface s, Art art)
	{
		BattleUi b = _battle;
		IReadOnlyList<Combatant> all = StageCombatants();
		BattleInfo? info = StageInfo();
		int partyCount = info?.PartyCount ?? all.Count(c => c.Side == Side.Party);
		int enemyCount = Math.Max(1, _show.Slots);
		bool commanding = b.Phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro;
		int target = b.Phase == BattlePhase.Target && b.Targets.Count > 0 ? b.Targets[Math.Clamp(_cursor, 0, b.Targets.Count - 1)] : -1;
		float pulse = 0.5f + 0.5f * MathF.Sin(_clockMs / 140f);
		BattleStep? step = b.Phase == BattlePhase.Playback ? _timeline.Current : null;
		float p = _timeline.Progress;
		BattleTurn? turn = step?.Turn;
		bool reduced = _s.Profile().ReducedFlash;
		List<ShownCombatant> shown = _show.All;
		_stageLayout = StageLayout();
		bool front = _stageLayout == BattleLayout.Front;
		int stageBottom = front ? StripTop : BattlePanelTop;

		// The screen shakes with a heavy effect, a critical hit and a heavy change of form, easing off over the step; reduced
		// flash damps it.
		float shake = step switch
		{
			{ Kind: StepKind.Effect, Turn.Look.Shake: > 0 } => step.Turn!.Look!.Shake * (1f - p),
			{ Change.Shake: > 0 } => step.Change!.Shake * (1f - p),
			{ Kind: StepKind.Result } when step.Events.Any(e => e.Type == BattleEventType.Critical) => 6f * (1f - p),
			_ => 0f,
		};
		if (reduced)
		{
			shake /= 4f;
		}
		_probeShake = (int)MathF.Round(shake);
		var jolt = new Point((int)MathF.Round(shake * MathF.Sin(_clockMs * 0.09f)), (int)MathF.Round(shake * MathF.Cos(_clockMs * 0.13f)));

		FieldInfo? field = _s.FieldInfo();
		int backdrop = art.Backdrop(field?.AreaKind ?? AreaKind.Field, field?.AreaId ?? "", info?.Encounter ?? "");
		if (front)
		{
			DrawFrontBackdrop(s, backdrop, jolt);
		}
		else
		{
			s.Image(backdrop, jolt.X - 8, jolt.Y - 8, s.Width + 16, BattlePanelTop + 8, Color.White);
		}

		// Each figure's picture, offset and tint for the step playing, drawn back to front. In the front layout a vehicle
		// stands left of the middle, clear of the action's name plate.
		string vehicle = info?.Vehicle ?? "";
		// In the front layout the members step aside for the command window, so it hides none of them.
		Dictionary<int, int> centers = front ? FrontMemberCenters(all, vehicle, commanding && MenuRows(all) is not null ? b.Actor : -1, out _commandX) : [];
		(int X, int Foot, int Size) Where(int i)
		{
			if (vehicle.Length > 0 && all[i].Id == vehicle)
			{
				return front ? (SlotX(1) + SlotWidth / 2, StripTop + 8, 160) : (1012, 336, 128);
			}
			if (!front)
			{
				return Place(i, partyCount, shown[i].Position, enemyCount);
			}
			(int X, int Foot, int Size) f = PlaceFront(i, partyCount, shown[i].Position, enemyCount);
			return centers.TryGetValue(i, out int cx) ? (cx, f.Foot, f.Size) : f;
		}
		var bodies = new Dictionary<int, Rectangle>();
		var figures = new List<string>();
		var order = Enumerable.Range(0, Math.Min(all.Count, shown.Count)).OrderBy(i => Where(i).Foot).ToList();
		foreach (int i in order)
		{
			Combatant c = all[i];
			ShownCombatant sc = shown[i];
			bool enemy = c.Side == Side.Enemy;
			string id = enemy ? sc.Id : c.Id;
			// In a vehicle battle the members ride inside it: only the vehicle stands on the field, at the party's middle.
			if (vehicle.Length > 0 && !enemy && c.Id != vehicle)
			{
				continue;
			}
			(int x, int foot, int size) = Where(i);
			float alpha = 1f;
			bool fading = sc.AliveStep is not null && sc.AliveStep == step;
			if (!sc.Alive)
			{
				if (fading)
				{
					alpha = 1f - p;
				}
				else if (enemy)
				{
					continue;
				}
			}
			else if (fading)
			{
				alpha = p;
			}
			// A joining enemy fades in rising into its place; one merging away slides into the enemy it merges with.
			bool changing = enemy && sc.ChangeStep is not null && sc.ChangeStep == step;
			float changeDx = 0, changeDy = 0;
			if (changing && step!.Kind == StepKind.Join)
			{
				alpha = Ease(Math.Min(1f, p * 1.6f));
				changeDy = 36 * (1f - Ease(p));
			}
			if (step is { Kind: StepKind.Merge } && i == step.From && step.To >= 0 && step.To < shown.Count)
			{
				changeDx = (Where(step.To).X - x) * Ease(Math.Min(1f, p * 1.4f));
			}
			// In the side layout a figure steps toward the other side; in the front one an enemy comes toward the camera
			// and a member rises out of the line, the member choosing a command already a little.
			int toward = enemy ? 1 : -1;
			float reach = front ? (enemy ? 20 : c.Id == vehicle ? 12 : 24) : enemy ? 24 : 44;
			float move = front && !enemy && commanding && i == b.Actor ? 12 : 0;
			string animation = "idle";
			Color tint = Color.White;
			if (turn is not null && turn.Actors.Contains(i))
			{
				move = reach * step!.Kind switch
				{
					StepKind.Forward => Ease(p),
					StepKind.Back => 1f - Ease(p),
					_ => 1f,
				};
				if (step.Kind == StepKind.Animate && turn.Look is { } look)
				{
					animation = look.Animation;
					if (look.Animation == "attack")
					{
						move += (front ? 10 : 16) * MathF.Sin(MathF.PI * p);
					}
					else if (look.Animation is "cast" or "item")
					{
						string element = look.Effect.Contains('_') ? look.Effect[(look.Effect.IndexOf('_') + 1)..] : "none";
						tint = Color.Lerp(Color.White, Art.ElementColor(element), 0.5f * MathF.Sin(MathF.PI * p));
					}
				}
			}
			float dx = (front ? 0 : toward * move) + changeDx, dy = (front ? toward * move : 0) + changeDy;
			bool hurt = step is { Kind: StepKind.Result } && step.Events.Any(e => e.Type == BattleEventType.DamageDealt && e.Target == i);
			if (hurt)
			{
				animation = "hurt";
				dx += MathF.Sin(p * 40f) * 6f * (1f - p);
				if (p < 0.4f)
				{
					tint = Color.Lerp(Color.White, new Color(255, 120, 110), 0.7f);
				}
			}
			if (!sc.Alive && !enemy)
			{
				animation = "defeat";
				tint = new Color(150, 150, 170);
			}
			Figure figure = enemy ? art.BattleFigure(id, true)
				: c.Id == vehicle ? (front ? art.VehicleBackFigure(c.Id) : art.VehicleFigure(c.Id))
				: front ? art.BackFigure(c.Id) : art.BattleFigure(c.Id, false);
			if (!enemy)
			{
				figures.Add($"{c.Id} {(c.Id == vehicle ? "vehicle " : "")}{(front ? "back" : "side")}");
			}
			FrameRun run = figure.Animations.GetValueOrDefault(animation) ?? figure.Idle;
			bool idle = run == figure.Idle;
			int image = figure.Images[run.FrameAt(idle ? _clockMs + i * 137 : _timeline.ElapsedMs, loop: idle)];
			int left = x - size / 2 + (int)MathF.Round(dx) + jolt.X, top = foot - size + (int)MathF.Round(dy) + jolt.Y;
			Rectangle body = art.Stand(image, left, top, size);
			bodies[i] = body;
			// The members seen from behind stand in the strip's shadow, so only the others cast one.
			if (!front || enemy)
			{
				s.Image(art.Shadow, left, top + size / 6, size, size, Color.White * alpha);
			}
			// A change of form: the old form fades out as the new one, lit white, fades in.
			if (changing && step!.Kind is StepKind.Transform or StepKind.Merge && sc.PreviousId.Length > 0)
			{
				Figure old = art.BattleFigure(sc.PreviousId, true);
				int oldImage = old.Images[old.Idle.FrameAt(_clockMs + i * 137, loop: true)];
				Rectangle was = art.Stand(oldImage, left, top, size);
				s.Image(oldImage, was.X, was.Y, was.Width, was.Height, Color.White * (alpha * (1f - Ease(p))));
				tint = Color.Lerp(Color.White * 4f, tint, Ease(p));
				alpha *= Ease(p);
			}
			s.Image(image, body.X, body.Y, body.Width, body.Height, tint * alpha);
		}
		_probeFigures = figures.Count == 0 ? "none" : string.Join(", ", figures);

		// Where effects and numbers land: on each figure, and in the front layout on the slot of a member with none.
		var anchors = new Dictionary<int, Rectangle>(bodies);
		var stage = new FrontStage();
		if (front)
		{
			stage = DrawFrontStrip(s, art, all, vehicle, commanding, target, step, p, pulse, reduced);
			foreach ((int index, Rectangle slot) in stage.Slots.Where(x => !anchors.ContainsKey(x.Key)))
			{
				anchors[index] = slot;
			}
			stage.Banner = DrawBanner(s, step, p, 0, front, aboveStrip: true);
		}
		bool Behind(int i) => front && i < partyCount;

		// A change of the formation plays its effect on the enemy it brings or changes.
		if (step is { Change: { } change, To: >= 0 } && art.Effect(change.Effect) is { } changeFx && anchors.TryGetValue(step.To, out Rectangle into))
		{
			int frame = changeFx.Images[changeFx.Idle.FrameAt(_timeline.ElapsedMs, loop: false)];
			(int ew, int eh) = art.SizeOf(frame);
			int scale = EffectScale + 1;
			s.Image(frame, into.Center.X - ew * scale / 2, into.Center.Y - eh * scale / 2, ew * scale, eh * scale, Color.White);
		}

		// The effect on each target, centered on its body (on a member seen from behind, its head); in the front layout an
		// effect on several targets sweeps along them.
		if (step is { Kind: StepKind.Effect, Turn.Look: { } fx } && art.Effect(fx.Effect) is { } effect)
		{
			List<int> hit = [.. turn!.Targets.Where(anchors.ContainsKey)];
			if (front)
			{
				hit = [.. hit.OrderBy(t => anchors[t].Center.X)];
			}
			int stagger = front && hit.Count > 1 ? Math.Min(90, fx.EffectMs / (2 * hit.Count)) : 0;
			for (int k = 0; k < hit.Count; k++)
			{
				int ms = _timeline.ElapsedMs - k * stagger;
				if (ms < 0)
				{
					continue;
				}
				int frame = effect.Images[effect.Idle.FrameAt(ms, loop: false)];
				(int ew, int eh) = art.SizeOf(frame);
				int scale = front && !Behind(hit[k]) ? EffectScale + 1 : EffectScale;
				Rectangle r = anchors[hit[k]];
				int cy = Behind(hit[k]) ? r.Y + r.Height * 3 / 8 : r.Center.Y;
				s.Image(frame, r.Center.X - ew * scale / 2, cy - eh * scale / 2, ew * scale, eh * scale, Color.White);
			}
		}

		// Labels, statuses and HP bars under the enemies, as before: one per enemy, its group's name and number.
		// An enemy merging away takes no label: it is sliding into the other one.
		foreach (int i in order.Where(i => all[i].Side == Side.Enemy && (shown[i].Alive || (shown[i].AliveStep is not null && shown[i].AliveStep == step))
			&& !(step is { Kind: StepKind.Merge } && i == step.From)))
		{
			(int x, int foot, _) = Where(i);
			bool targeted = i == target;
			string label = Style.Fit(s, shown[i].Label, 190, Style.Small);
			int lw = s.Measure(label, Style.Small);
			s.Fill(x - lw / 2 - 8, foot + 6, lw + 16, 24, Color.Black * 0.6f);
			s.Text(label, x - lw / 2, foot + 9, targeted ? Style.Accent : Style.Text, Style.Small);
			Style.Bar(s, x - 50, foot + 34, 100, 6, Shown(shown[i], step, p).Hp, shown[i].MaxHp, Style.Bad);
			DrawIcons(s, art, shown[i], step, p, x - 50, foot + 44);
			int lx = Math.Min(x - lw / 2 - 8, x - 50);
			stage.Labels[shown[i].Label] = new Rectangle(lx, foot + 6, Math.Max(x - lx, lw / 2 + 8) * 2, 58);
		}

		// The target cursor over the figure being chosen (a member seen from behind gets it over the command window), and
		// over the retarget step's new target as it moves there.
		int PointerY(int i, Rectangle r) => Behind(i) ? r.Y - 8 : r.Y - 40;
		if (target >= 0 && !Behind(target) && bodies.TryGetValue(target, out Rectangle aim))
		{
			s.Image(art.Pointer, aim.Center.X - 18, PointerY(target, aim) + (int)(5 * pulse), 36, 36, Color.White);
		}
		if (step is { Kind: StepKind.Retarget } && bodies.TryGetValue(step.To, out Rectangle to))
		{
			Rectangle from = bodies.GetValueOrDefault(step.From, to);
			float e = Ease(p);
			s.Image(art.Pointer, (int)MathHelper.Lerp(from.Center.X, to.Center.X, e) - 18, (int)MathHelper.Lerp(PointerY(step.From, from), PointerY(step.To, to), e), 36, 36, Color.White);
		}

		// Floating numbers and words over each target: damage, critical, misses, weak and resisted hits, healing, statuses.
		// Over a member seen from behind they rise from its head, clear of the enemies' labels.
		var marks = new List<string>();
		if (step is { Kind: StepKind.Result } && art.Effect(PresentationTable.ChangeFor("instant_defeat", "").Effect) is not null)
		{
			// The instant defeat's mark, stamped on the enemy it felled for the whole step.
			foreach (BattleEvent e in step.Events.Where(e => e.Type == BattleEventType.Message && e.Message == "battle.instant_defeat" && anchors.ContainsKey(e.Target)))
			{
				string enemyId = e.Target < shown.Count ? shown[e.Target].Id : "";
				if (art.Effect(PresentationTable.ChangeFor("instant_defeat", enemyId).Effect) is not { } mark)
				{
					continue;
				}
				int frame = mark.Images[mark.Idle.FrameAt(_timeline.ElapsedMs, loop: true)];
				(int ew, int eh) = art.SizeOf(frame);
				int scale = EffectScale + 1;
				Rectangle r = anchors[e.Target];
				s.Image(frame, r.Center.X - ew * scale / 2, r.Center.Y - eh * scale / 2, ew * scale, eh * scale, Color.White * (p < 0.85f ? 1f : (1f - p) / 0.15f));
				marks.Add(e.Target < shown.Count ? shown[e.Target].Label : "?");
			}
		}
		_probeMarks = marks.Count == 0 ? "none" : string.Join(", ", marks);
		if (step is { Kind: StepKind.Result or StepKind.RoundEnd })
		{
			var stacked = new Dictionary<int, int>();
			BattleEvent? previous = null;
			foreach (BattleEvent e in step.Events)
			{
				int t = e.Type == BattleEventType.ItemHadNoEffect ? e.Actor : e.Target;
				if (!anchors.TryGetValue(t, out Rectangle r) || Floating(e, previous) is not { } floating)
				{
					previous = e;
					continue;
				}
				previous = e;
				int n = stacked.GetValueOrDefault(t);
				stacked[t] = n + 1;
				float fade = p < 0.7f ? 1f : (1f - p) / 0.3f;
				int rise = (int)(30 * Ease(Math.Min(1f, p * 2f)));
				int w = s.Measure(floating.Text, floating.Size);
				int fy = (Behind(t) ? r.Y + 36 : r.Y - 26) - rise - n * (floating.Size + 6);
				s.Text(floating.Text, r.Center.X - w / 2 + 2, fy + 2, Color.Black * (0.8f * fade), floating.Size);
				s.Text(floating.Text, r.Center.X - w / 2, fy, floating.Color * fade, floating.Size);
			}
		}

		// A full-screen flash at the start of a big effect, left out with reduced flash.
		_probeFlash = false;
		if ((step is { Kind: StepKind.Effect, Turn.Look.Flash: true } || step is { Change.Flash: true }) && !reduced && p < 0.25f)
		{
			_probeFlash = true;
			s.Fill(0, 0, s.Width, stageBottom, Color.White * (0.55f * (1f - p * 4f)));
		}

		if (b.LogShown)
		{
			Style.Box(s, 16, 12, s.Width - 32, BattleLogLines * 22 + 24, 0.94f);
			var wrapped = new List<(string Text, bool Newest)>();
			for (int i = Math.Max(0, b.Log.Count - BattleLogLines); i < b.Log.Count; i++)
			{
				foreach (string l in Style.Wrap(s, b.Log[i], s.Width - 80, Style.Small))
				{
					wrapped.Add((l, i == b.Log.Count - 1));
				}
			}
			var lines = wrapped.Skip(Math.Max(0, wrapped.Count - BattleLogLines)).ToList();
			for (int i = 0; i < lines.Count; i++)
			{
				s.Text(lines[i].Text, 36, 24 + i * 22, lines[i].Newest ? Style.Text : Style.Dim, Style.Small);
			}
		}
		DrawBanner(s, step, p, b.LogShown ? BattleLogLines * 22 + 48 : 20, front);
		if (front)
		{
			DrawFrontMenus(s, all, info, bodies, target, stage);
			return;
		}

		// The party, one column each: portrait, name, status icons and names, HP and TP bars, the command already chosen.
		var party = all.Where(c => c.Side == Side.Party).ToList();
		int panelW = s.Width - 32 - BattleMenuWidth - 12;
		int panelH = s.Height - BattlePanelTop - 16;
		Style.Box(s, 16, BattlePanelTop, panelW, panelH);
		int colW = (panelW - 16) / Math.Max(1, party.Count);
		for (int i = 0; i < party.Count; i++)
		{
			Combatant c = party[i];
			ShownCombatant sc = shown.ElementAtOrDefault(c.Index) ?? new ShownCombatant(c, -1);
			int cx = 24 + i * colW;
			int cy = BattlePanelTop + 10;
			int cw = colW - 8;
			int ch = panelH - 20;
			bool acting = commanding ? c.Index == b.Actor : turn?.Actors.Contains(c.Index) == true;
			bool targeted = c.Index == target || (step?.Kind is StepKind.Effect or StepKind.Result && turn?.Targets.Contains(c.Index) == true);
			if (acting)
			{
				s.Fill(cx, cy, cw, ch, Style.Row);
			}
			if (acting || targeted)
			{
				Style.Frame(s, cx, cy, cw, ch, targeted ? Style.Accent * (0.4f + 0.6f * pulse) : Style.Accent);
			}
			int portrait = c.Id == vehicle ? art.VehicleFrame(c.Id, Direction.South, 0) : art.Portrait("", c.Id);
			s.Image(portrait, cx + 8, cy + 8, 64, 64, sc.Alive ? Color.White : new Color(80, 80, 92));
			s.Text(Style.Fit(s, c.Name, cw - 90), cx + 80, cy + 12, !sc.Alive ? Style.Dim : acting ? Style.Accent : Style.Text);
			string state = MemberState(c, sc);
			DrawIcons(s, art, sc, step, p, cx + 80, cy + 38);
			if (state.Length > 0)
			{
				s.Text(Style.Fit(s, state, cw - 90, Style.Small), cx + 80, cy + 60, sc.Alive ? Style.Dim : Style.Bad, Style.Small);
			}
			(int hp, int tp) = Shown(sc, step, p);
			int bw = cw - 24;
			Widgets.ValueBar(s, cx + 12, cy + 86, bw, T("ui.battle.hp"), hp, sc.MaxHp, hp * 4 <= sc.MaxHp ? Style.Bad : Style.Good);
			Widgets.ValueBar(s, cx + 12, cy + 130, bw, T("ui.battle.tp"), tp, sc.MaxTp, Style.Tp);
			if (NextCommand(c) is { } command)
			{
				s.Text(Style.Fit(s, T("ui.battle.next", command), bw, Style.Small), cx + 12, cy + 180, Style.Dim, Style.Small);
			}
		}

		int mx = s.Width - 16 - BattleMenuWidth;
		Style.Window(s, mx, BattlePanelTop, BattleMenuWidth, panelH, MenuTitle(all));
		if (MenuRows(all) is { } menu)
		{
			int rows = (panelH - 44) / 28;
			int first = Math.Clamp(_cursor - rows + 1, 0, Math.Max(0, menu.Items.Count - rows));
			for (int i = first; i < Math.Min(menu.Items.Count, first + rows); i++)
			{
				Style.ListRow(s, Style.Fit(s, menu.Items[i], BattleMenuWidth - 70, Style.Small), mx + 24, BattlePanelTop + 30 + (i - first) * 28,
					BattleMenuWidth - 36, i == _cursor, menu.Usable is null || menu.Usable[i], Style.Small);
			}
		}
		MenuDescription(s, all, target);
		switch (b.Phase)
		{
		case BattlePhase.Playback:
			s.Text(b.Auto ? T("ui.battle.auto") : T("ui.battle.playing"), mx + 24, BattlePanelTop + 34, Style.Text);
			if (b.Auto)
			{
				s.Text(Style.Fit(s, T("ui.battle.hint.auto_stops", Hint(GameAction.Back)), BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + 64, Style.Dim, Style.Small);
			}
			s.Text(Style.Fit(s, T("ui.battle.hint.hurry", Hint(GameAction.Confirm)), BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + panelH - 82, Style.Dim, Style.Small);
			s.Text(Style.Fit(s, T("ui.battle.hint.skip", Hint(GameAction.FastForward)), BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + panelH - 58, Style.Dim, Style.Small);
			s.Text(Style.Fit(s, T("ui.battle.hint.log", Hint(GameAction.TextLog)), BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + panelH - 34, Style.Dim, Style.Small);
			break;
		case BattlePhase.Ended:
			s.Text(OutcomeText(info?.Outcome ?? Outcome.Ongoing), mx + 24, BattlePanelTop + 34, Style.Accent, Style.Title);
			s.Text(T("ui.battle.hint.continue", Hint(GameAction.Confirm)), mx + 24, BattlePanelTop + 84, Style.Text);
			DrawResult(s, info, BattlePanelTop);
			break;
		}
	}

	// A member's state under its name: fallen, defending and its statuses.
	private string MemberState(Combatant c, ShownCombatant sc) =>
		!sc.Alive ? T("ui.battle.fallen") : string.Join(" ", sc.Statuses.Select(_s.DisplayName).Prepend(c.Defending ? T("ui.battle.defending") : "").Where(x => x.Length > 0));

	private string? NextCommand(Combatant c) => c.CommandKind == CommandKind.None ? null
		: c.CommandId.Length > 0 ? _s.DisplayName(c.CommandId) : T(NameKind.CommandKind, (int)c.CommandKind);

	private string OutcomeText(Outcome outcome) => outcome == Outcome.Ongoing ? "" : T("ui.battle.outcome." + GameSession.NameOf(NameKind.Outcome, (int)outcome));

	private string MenuTitle(IReadOnlyList<Combatant> all) => _battle.Phase switch
	{
		BattlePhase.Command => _battle.Actor < all.Count ? all[_battle.Actor].Name : "",
		BattlePhase.Option => T("ui.battle.command." + _battle.MenuChoice),
		BattlePhase.Target => T("ui.battle.title.target"),
		BattlePhase.Macro => T("ui.battle.title.macro"),
		BattlePhase.Ended => T("ui.battle.title.over"),
		_ => T("ui.battle.title.battle"),
	};

	// The command menu's rows for the phase, and which can be chosen; null outside the menus.
	private (List<string> Items, List<bool>? Usable)? MenuRows(IReadOnlyList<Combatant> all)
	{
		BattleUi b = _battle;
		return b.Phase switch
		{
			BattlePhase.Command when CommandRows() is var rows => ([.. rows.Select(r => r.Text)], [.. rows.Select(r => r.Usable)]),
			BattlePhase.Option => (b.Options.Select(OptionText).ToList(), b.Options.Select(o => o.Usable).ToList()),
			BattlePhase.Target => (b.Targets.Select(t => t < all.Count ? (all[t].Side == Side.Party ? all[t].Name : all[t].Label) : "?").ToList(), null),
			BattlePhase.Macro => ([.. _s.Macros()], null),
			_ => null,
		};
	}

	// The highlighted option's or targeted enemy's description strip; its box, or an empty one when none shows.
	private Rectangle MenuDescription(IDrawSurface s, IReadOnlyList<Combatant> all, int target)
	{
		BattleUi b = _battle;
		if (b.Phase == BattlePhase.Option && b.Options.Count > 0)
		{
			BattleOption option = b.Options[Math.Clamp(_cursor, 0, b.Options.Count - 1)];
			if (b.Worn.Contains(option.Id))
			{
				string? piece = _s.Description(DescriptionKind.Equipment, option.Id);
				return BattleDescription(s, T("ui.battle.worn_note") + (piece is { Length: > 0 } ? " " + piece : ""));
			}
			return BattleDescription(s, _s.Description(option.Command == CommandKind.Item ? DescriptionKind.Item : DescriptionKind.Ability, option.Id));
		}
		if (b.Phase == BattlePhase.Target && target >= 0 && target < all.Count && all[target].Side == Side.Enemy)
		{
			return BattleDescription(s, _s.Description(DescriptionKind.Enemy, all[target].Id));
		}
		return Rectangle.Empty;
	}


	// A bar's numbers while the step that changed them plays, else where it left them.
	private static (int Hp, int Tp) Shown(ShownCombatant c, BattleStep? step, float p) => c.BarStep is not null && c.BarStep == step
		? ((int)MathF.Round(MathHelper.Lerp(c.HpFrom, c.Hp, Ease(p))), (int)MathF.Round(MathHelper.Lerp(c.TpFrom, c.Tp, Ease(p))))
		: (c.Hp, c.Tp);

	// Status icons in a row: a new one grows in, one that ends blinks out while its step plays.
	private static void DrawIcons(IDrawSurface s, Art art, ShownCombatant c, BattleStep? step, float p, int x, int y)
	{
		const int icon = 20;
		int at = 0;
		foreach (string id in c.Statuses)
		{
			int grow = c.Appeared.GetValueOrDefault(id) is { } appeared && appeared == step ? (int)(icon * (0.4f + 0.6f * Ease(p))) : icon;
			s.Image(art.StatusIcon(id), x + at + (icon - grow) / 2, y + (icon - grow) / 2, grow, grow, Color.White);
			at += icon + 2;
		}
		foreach ((string id, BattleStep leaving) in c.Leaving.Where(l => l.Step == step))
		{
			if ((int)(p * 8) % 2 == 0)
			{
				s.Image(art.StatusIcon(id), x + at, y, icon, icon, Color.White * (1f - p));
			}
			at += icon + 2;
		}
	}

	private readonly record struct FloatingText(string Text, Color Color, int Size);

	private const int NumberSize = 28;

	// What floats over a target for one event; `previous` tells a critical hit's damage.
	private FloatingText? Floating(BattleEvent e, BattleEvent? previous)
	{
		Color weak = new(255, 168, 80), resisted = new(150, 200, 255), status = new(206, 160, 255), felled = new(255, 80, 80);
		string stat = T("ui.battle.stat." + GameSession.NameOf(NameKind.Stat, (int)e.Stat));
		return e.Type switch
		{
			BattleEventType.DamageDealt when previous is { Type: BattleEventType.Critical } => new(T("ui.battle.float.critical", e.Amount), Style.Accent, NumberSize + 8),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Immune => new(T("ui.battle.float.immune"), Style.Dim, Style.Body),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Weak => new(T("ui.battle.float.weak", e.Amount), weak, NumberSize),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Resisted => new(T("ui.battle.float.resisted_damage", e.Amount), resisted, NumberSize),
			BattleEventType.DamageDealt => new(e.Amount.ToString(System.Globalization.CultureInfo.InvariantCulture), Style.Text, NumberSize),
			BattleEventType.Missed => new(T("ui.battle.float.miss"), Style.Dim, Style.Body),
			// Technique points restored float in the TP bar's color, with their unit, apart from hit points' green numbers.
			BattleEventType.Healed when e.Resource == ResourceKind.Tp && e.Amount == 0 => null,
			BattleEventType.Healed when e.Resource == ResourceKind.Tp => new(T("ui.battle.float.tp", e.Amount), Style.Tp, NumberSize - 4),
			BattleEventType.Healed => new($"+{e.Amount}", Style.Good, NumberSize),
			BattleEventType.Revived => new(T("ui.battle.float.revived", e.Amount), Style.Good, Style.Body),
			BattleEventType.StatusApplied => new(_s.DisplayName(e.Status), status, Style.Body),
			BattleEventType.StatusResisted => new(T(e.Effectiveness == Effectiveness.Immune ? "ui.battle.float.immune" : "ui.battle.float.resisted"), Style.Dim, Style.Body),
			BattleEventType.StatusRemoved when e.StatusEnd != StatusEnd.Defeated => new(T("ui.battle.float.ends", _s.DisplayName(e.Status)), Style.Dim, Style.Body),
			BattleEventType.StatModified => new(T(e.Amount > 0 ? "ui.battle.float.stat_up" : e.Amount < 0 ? "ui.battle.float.stat_down" : "ui.battle.float.stat_normal", stat),
				e.Amount > 0 ? Style.Good : e.Amount < 0 ? weak : Style.Dim, Style.Body),
			BattleEventType.ItemHadNoEffect => new(T("ui.battle.float.no_effect"), Style.Dim, Style.Body),
			BattleEventType.Message when e.Message == "battle.instant_defeat" => new(T("ui.battle.float.felled"), felled, NumberSize),
			BattleEventType.Message when e.Message.StartsWith("battle.instant_defeat", StringComparison.Ordinal) =>
				new(T(e.Effectiveness == Effectiveness.Immune ? "ui.battle.float.immune" : "ui.battle.float.resisted"), Style.Dim, Style.Body),
			_ => null,
		};
	}

	// The step's words: the action's name over its whole turn, and the opening, the round, a cancel, a notice or the
	// outcome on their own, fading in and out. In the front layout the action's name and a cancel are a framed name plate
	// just above the status strip (`aboveStrip`), the rest stays at `y`. Returns the box drawn.
	private Rectangle DrawBanner(IDrawSurface s, BattleStep? step, float p, int y, bool front, bool aboveStrip = false)
	{
		if (step is null)
		{
			return Rectangle.Empty;
		}
		bool turnName = step.Kind is StepKind.Banner or StepKind.Animate or StepKind.Effect or StepKind.Result or StepKind.Retarget;
		if (front && (turnName || step.Kind == StepKind.Cancel) != aboveStrip)
		{
			return Rectangle.Empty;
		}
		string text = turnName ? BannerOf(step) : step.Text;
		if (text.Length == 0 || step.Kind is StepKind.Forward or StepKind.Back)
		{
			return Rectangle.Empty;
		}
		float alpha = turnName ? 1f : Math.Clamp(Math.Min(p * 6f, (1f - p) * 6f), 0f, 1f);
		int size = step.Kind is StepKind.Outcome or StepKind.Opening or StepKind.Round ? Style.Title : Style.Body;
		text = Style.Fit(s, text, s.Width - 160, size);
		int w = s.Measure(text, size) + 48;
		int h = size + 22;
		int x = (s.Width - w) / 2;
		if (aboveStrip)
		{
			y = StripTop - h - 6;
		}
		Style.Box(s, x, y, w, h, 0.95f * alpha);
		if (aboveStrip)
		{
			Style.Frame(s, x + 6, y + 6, w - 12, h - 12, Style.Accent * (0.5f * alpha), 1);
		}
		s.Text(text, x + 24, y + 10, (step.Kind == StepKind.Cancel ? Style.Bad : Style.Accent) * alpha, size);
		return new Rectangle(x, y, w, h);
	}

	private static string BannerOf(BattleStep step) => step.Kind == StepKind.Banner ? step.Text : step.Turn?.Name ?? "";

	// The result screen over the battlefield: experience, money, drops, and each level-up with what it taught.
	private void DrawResult(IDrawSurface s, BattleInfo? info, int stageBottom)
	{
		BattleResult? result = _s.BattleResult();
		var lines = new List<(string Text, Color Color)>();
		if (result is not null && result.Outcome is Outcome.Victory or Outcome.ScriptedEnd)
		{
			lines.Add((T("ui.battle.result.experience", result.Experience), Style.Text));
			lines.Add((T("ui.battle.result.money", result.Money), Style.Text));
			if (result.Drops.Count > 0)
			{
				lines.Add((T("ui.battle.result.found", ListText(result.Drops.Select(d => d.Value > 1 ? T("ui.common.item_count", _s.DisplayName(d.Key), d.Value) : _s.DisplayName(d.Key)))), Style.Text));
			}
			foreach (LevelUp up in result.LevelUps)
			{
				lines.Add((T("ui.battle.result.level_up", up.Name, up.Level), Style.Accent));
				if (up.Learned.Count > 0)
				{
					lines.Add(("  " + T("ui.battle.result.learned", ListText(up.Learned.Select(_s.DisplayName))), Style.Good));
				}
			}
		}
		else if (info?.GameOver == true)
		{
			lines.Add((T("ui.battle.result.fallen"), Style.Bad));
		}
		string heading = OutcomeText(info?.Outcome ?? Outcome.Ongoing);
		_probeVictory = string.Join(" / ", lines.Select(l => l.Text.Trim()).Prepend(heading));
		int w = 640, h = 84 + lines.Count * 30;
		int x = (s.Width - w) / 2, y = Math.Max(24, (stageBottom - h) / 2);
		Style.Window(s, x, y, w, h, T("ui.battle.result.title"));
		s.Text(heading, x + (w - s.Measure(heading, Style.Title)) / 2, y + 22, Style.Accent, Style.Title);
		for (int i = 0; i < lines.Count; i++)
		{
			s.Text(Style.Fit(s, lines[i].Text, w - 64), x + 32, y + 70 + i * 30, lines[i].Color);
		}
	}

	// A strip at the top of the battlefield, under the log when it shows, with the highlighted option's or targeted
	// enemy's description, clear of the figures and their labels.
	private Rectangle BattleDescription(IDrawSurface s, string? text)
	{
		int h = Px(22) * 2 + 16;
		int y = _battle.LogShown ? BattleLogLines * 22 + 48 : 16;
		Style.Box(s, 16, y, s.Width - 32, h, 0.9f);
		DrawDescription(s, text, 32, y + 8, s.Width - 64);
		return new Rectangle(16, y, s.Width - 32, h);
	}
}
