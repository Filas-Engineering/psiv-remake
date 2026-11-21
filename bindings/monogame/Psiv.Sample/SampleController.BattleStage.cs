using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The battle drawn in either layout (the presentation table's, SampleController.BattleFront.cs for the front one's own
// parts): the backdrop, the figures, the playback's steps on them (stepping forward, banners, animations, effects, hit
// reactions, floating numbers, bars, status icons, falls), the party's panels or status strip and the command menu, the
// log when it is shown, and the result screen. The layout files `battle_front` and `battle_side` place the windows and
// give the stage's placement rules as params; the stage itself is code drawn into the files' `stage` and `effects` slots.
public sealed partial class SampleController
{
	private const int EffectScale = 3;

	private int _probeShake;
	private bool _probeFlash;
	private string _probeBackdrop = "-";
	private string _probeOverlay = "-";
	// Each figure drawn: its label, the drawn size and how far its bottom lies below the ground line.
	private string _probeBodies = "-";
	// The last effect frame drawn on a target, as WxH screen pixels.
	private string _probeEffectSize = "-";
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

	// The stage's placement rules from the layout file's params: the members stand from (party_x, party_foot) a step of
	// (party_dx, party_dy) apart; enemies fill `enemy_row_span` around enemy_x in one row up to `enemy_row_max`, then two
	// ranks, the first ones in front, either interleaved over `enemy_rank_span` or each rank spread on its own and the back
	// one shifted half a place.
	private sealed record StageRules(int StageBottom, int PartyX, int PartyDx, int PartyFoot, int PartyDy, int PartySize, int VehicleX, int VehicleFoot,
		int VehicleSize, int StepGap, int StepMargin, int EnemyX, int RowMax, int RowSpan, int RowSpacingMax, int RowFoot, int RowStagger, bool RanksInterleaved,
		int RankSpan, int RankSpacingMax, int FrontFoot, int BackFoot);

	// A param the screen's file leaves out takes the built-in file's value.
	private StageRules Rules(string screen, UiContext ctx)
	{
		int P(string name) => Ui.Param(screen, name, ctx, 0);
		return new StageRules(P("stage_bottom"), P("party_x"), P("party_dx"), P("party_foot"), P("party_dy"), P("party_size"), P("vehicle_x"), P("vehicle_foot"),
			P("vehicle_size"), P("step_gap"), P("step_margin"), P("enemy_x"), P("enemy_row_max"), P("enemy_row_span"), P("enemy_row_spacing_max"), P("enemy_row_foot"),
			P("enemy_row_stagger"), P("enemy_ranks_interleaved") != 0, P("enemy_rank_span"), P("enemy_rank_spacing_max"), P("enemy_front_foot"), P("enemy_back_foot"));
	}

	// Where the enemy at formation position `j` of `n` places stands: its center x, the y of its feet and its size. The
	// file's `enemy_slot_J_x` and `enemy_slot_J_foot` override the computed place, -1 keeping it.
	private (int X, int Foot, int Size) PlaceEnemy(string screen, StageRules r, UiContext ctx, int j, int n)
	{
		int x, foot;
		bool back = false;
		if (n <= r.RowMax)
		{
			int spacing = Math.Min(r.RowSpacingMax, r.RowSpan / n);
			x = r.EnemyX + (int)((j - (n - 1) / 2f) * spacing);
			foot = r.RowFoot - j % 2 * r.RowStagger;
		}
		else
		{
			int first = (n + 1) / 2;
			back = j >= first;
			if (r.RanksInterleaved)
			{
				int place = back ? 2 * (j - first) + 1 : 2 * j;
				float gap = r.RankSpan / (float)n;
				x = r.EnemyX + (int)((place - (n - 1) / 2f) * gap);
			}
			else
			{
				int k = back ? j - first : j, m = back ? n - first : first;
				int rankSpacing = Math.Min(r.RankSpacingMax, r.RankSpan / m);
				x = r.EnemyX + (int)((k - (m - 1) / 2f) * rankSpacing) + (back ? rankSpacing / 2 : 0);
			}
			foot = back ? r.BackFoot : r.FrontFoot;
		}
		int size = Ui.Param(screen, "enemy_size", new UiContext(ctx).Set("enemy_count", n).Set("enemy_back", back), 0);
		int ox = Ui.Param(screen, $"enemy_slot_{j}_x", ctx, -1), ofoot = Ui.Param(screen, $"enemy_slot_{j}_foot", ctx, -1);
		return (ox >= 0 ? ox : x, ofoot >= 0 ? ofoot : foot, size);
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
		string screen = front ? "battle_front" : "battle_side";
		string vehicle = info?.Vehicle ?? "";
		(List<string> Items, List<bool>? Usable)? menu = MenuRows(all);
		UiContext ui = BattleWindows(s, all, info, menu, target, screen);
		StageRules rules = Rules(screen, ui);
		int stageBottom = rules.StageBottom;

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
		(int backdrop, _probeBackdrop) = art.Backdrop(field?.AreaKind ?? AreaKind.Field, field?.AreaId ?? "", info?.Encounter ?? "", _battleTerrain);

		// Each figure's picture, offset and tint for the step playing, drawn back to front. In the front layout a vehicle
		// stands left of the middle, clear of the action's name plate. In the front layout the members step aside for the
		// command window, so it hides none of them.
		int commandWidth = front && menu is not null ? Ui.WindowRect(screen, "command", s, ui)?.Width ?? 0 : 0;
		int commandCenter = -1;
		Dictionary<int, int> centers = front ? FrontMemberCenters(all, vehicle, commanding && menu is not null ? b.Actor : -1, commandWidth, rules, s.Width, out commandCenter) : [];
		var enemyPlaces = new Dictionary<int, (int X, int Foot, int Size)>();
		(int X, int Foot, int Size) Where(int i)
		{
			if (vehicle.Length > 0 && all[i].Id == vehicle)
			{
				return (rules.VehicleX, rules.VehicleFoot, rules.VehicleSize);
			}
			(int X, int Foot, int Size) f;
			if (i < partyCount)
			{
				f = (rules.PartyX + i * rules.PartyDx, rules.PartyFoot + i * rules.PartyDy, rules.PartySize);
			}
			else if (!enemyPlaces.TryGetValue(shown[i].Position, out f))
			{
				f = enemyPlaces[shown[i].Position] = PlaceEnemy(screen, rules, ui, shown[i].Position, enemyCount);
			}
			return centers.TryGetValue(i, out int cx) ? (cx, f.Foot, f.Size) : f;
		}
		var bodies = new Dictionary<int, Rectangle>();
		var figures = new List<string>();
		var hovering = new List<string>();
		// The figures' pictures go on the stage when the layout draws its `stage` slot; where each lands is known now.
		var pictures = new List<Action>();
		void Picture(int image, int x, int y, int w, int h, Color tint) => pictures.Add(() => s.Image(image, x, y, w, h, tint));
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
			// An enemy plays its fallen pose, when it has one, while it fades.
			else if (!sc.Alive && fading)
			{
				animation = "defeat";
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
			// In the front layout a flying enemy hovers over its place, bobbing, while its shadow stays on the ground.
			int hover = front && enemy && c.Flying ? size / 5 + (int)MathF.Round(5 * MathF.Sin((_clockMs + i * 211) / 420f)) : 0;
			Rectangle body = art.Stand(image, left, top - hover, size);
			bodies[i] = body;
			// The members seen from behind stand in the strip's shadow, so only the others cast one.
			if ((!front || enemy) && figure.Shadow)
			{
				float shadow = hover > 0 ? 0.6f : 1f;
				int shrink = hover > 0 ? size / 6 : 0;
				Picture(art.Shadow, body.X + shrink / 2, top + size / 6 + shrink / 2, body.Width - shrink, size - shrink, Color.White * (alpha * shadow));
			}
			if (hover > 0)
			{
				hovering.Add(c.Label);
			}
			// A change of form: the old form fades out as the new one, lit white, fades in.
			if (changing && step!.Kind is StepKind.Transform or StepKind.Merge && sc.PreviousId.Length > 0)
			{
				Figure old = art.BattleFigure(sc.PreviousId, true);
				int oldImage = old.Images[old.Idle.FrameAt(_clockMs + i * 137, loop: true)];
				Rectangle was = art.Stand(oldImage, left, top, size);
				Picture(oldImage, was.X, was.Y, was.Width, was.Height, Color.White * (alpha * (1f - Ease(p))));
				tint = Color.Lerp(Color.White * 4f, tint, Ease(p));
				alpha *= Ease(p);
			}
			Picture(image, body.X, body.Y, body.Width, body.Height, tint * alpha);
		}
		_probeFigures = figures.Count == 0 ? "none" : string.Join(", ", figures);
		_probeBodies = string.Join(", ", bodies.OrderBy(x => x.Key).Select(x => $"{all[x.Key].Label} {x.Value.Width}x{x.Value.Height} {x.Value.Bottom - Where(x.Key).Foot}"));
		_probeFlying = hovering.Count == 0 ? "none" : string.Join(", ", hovering);
		// A vehicle battle's overlay (a cockpit, a hull's rim) goes over the stage and its figures, under the effects and
		// every window.
		_probeOverlay = "-";
		if (vehicle.Length > 0 && art.VehicleOverlay(vehicle) is int overlay)
		{
			Picture(overlay, jolt.X, jolt.Y, s.Width, stageBottom, Color.White);
			_probeOverlay = art.FromFile("vehicle_overlay:" + vehicle) ? vehicle : "every vehicle";
		}

		// Where effects and numbers land: on each figure, and in the front layout on the slot of a member with none (the
		// strip's `anchor` slots add those as the layout draws them).
		var anchors = new Dictionary<int, Rectangle>(bodies);
		var labels = new Dictionary<string, Rectangle>();
		bool Behind(int i) => front && i < partyCount;
		bool flash = (step is { Kind: StepKind.Effect, Turn.Look.Flash: true } || step is { Change.Flash: true }) && !reduced && p < 0.25f;
		_probeFlash = flash;

		void DrawEffects()
		{
			// A change of the formation plays its effect on the enemy it brings or changes.
			if (step is { Change: { } change, To: >= 0 } && art.Effect(change.Effect) is { } changeFx && anchors.TryGetValue(step.To, out Rectangle into))
			{
				int frame = changeFx.Images[changeFx.Idle.FrameAt(_timeline.ElapsedMs, loop: false)];
				(int ew, int eh) = art.DrawnEffectSize(frame, EffectScale + 1);
				s.Image(frame, into.Center.X - ew / 2, into.Center.Y - eh / 2, ew, eh, Color.White);
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
					(int ew, int eh) = art.DrawnEffectSize(frame, front && !Behind(hit[k]) ? EffectScale + 1 : EffectScale);
					Rectangle r = anchors[hit[k]];
					int cy = Behind(hit[k]) ? r.Y + r.Height * 3 / 8 : r.Center.Y;
					s.Image(frame, r.Center.X - ew / 2, cy - eh / 2, ew, eh, Color.White);
					_probeEffectSize = $"{ew}x{eh}";
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
				labels[shown[i].Label] = new Rectangle(lx, foot + 6, Math.Max(x - lx, lw / 2 + 8) * 2, 58);
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
					(int ew, int eh) = art.DrawnEffectSize(frame, EffectScale + 1);
					Rectangle r = anchors[e.Target];
					s.Image(frame, r.Center.X - ew / 2, r.Center.Y - eh / 2, ew, eh, Color.White * (p < 0.85f ? 1f : (1f - p) / 0.15f));
					marks.Add(e.Target < shown.Count ? shown[e.Target].Label : "?");
				}
			}
			_probeMarks = marks.Count == 0 ? "none" : string.Join(", ", marks);
			if (step is { Kind: StepKind.Result or StepKind.RoundEnd })
			{
				UseTextRole(s, TextRole.BattleNumbers);
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
				UseTextRole(s, TextRole.Menu);
			}

			// A full-screen flash at the start of a big effect, left out with reduced flash.
			if (flash)
			{
				s.Fill(0, 0, s.Width, stageBottom, Color.White * (0.55f * (1f - p * 4f)));
			}
		}

		ui.Set("stage", (UiSlot)((_, _, _, _) =>
		{
			if (front)
			{
				DrawFrontBackdrop(s, backdrop, jolt);
			}
			else
			{
				s.Image(backdrop, jolt.X - 8, jolt.Y - 8, s.Width + 16, stageBottom + 8, Color.White);
			}
			foreach (Action picture in pictures)
			{
				picture();
			}
		}));
		ui.Set("effects", (UiSlot)((_, _, _, _) => DrawEffects()));
		// A member being targeted may stand behind the command window: its pointer stays on top.
		ui.Set("target_pointer", (UiSlot)((_, _, _, _) =>
		{
			if (target >= 0 && target < all.Count && all[target].Side == Side.Party && bodies.TryGetValue(target, out Rectangle aim))
			{
				s.Image(art.Pointer, aim.Center.X - 18, aim.Y - 8, 36, 36, Color.White);
			}
		}));

		// The party's status: each member's numbers, marks and status icons, and in the front layout the strip slot it
		// stands over, where effects land on a member with no figure.
		UiContext Member(Combatant c)
		{
			ShownCombatant sc = shown.ElementAtOrDefault(c.Index) ?? new ShownCombatant(c, -1);
			(int hp, int tp) = Shown(sc, step, p);
			string? next = NextCommand(c);
			return new UiContext(ui)
				.Set("name", c.Name)
				.Set("alive", sc.Alive)
				.Set("acting", commanding ? c.Index == b.Actor : turn?.Actors.Contains(c.Index) == true)
				.Set("targeted", c.Index == target || (step?.Kind is StepKind.Effect or StepKind.Result && turn?.Targets.Contains(c.Index) == true))
				.Set("hit", step is { Kind: StepKind.Result } && step.Events.Any(e => e.Type == BattleEventType.DamageDealt && e.Target == c.Index))
				.Set("hp", hp)
				.Set("max_hp", sc.MaxHp)
				.Set("hp_low", hp * 4 <= sc.MaxHp)
				.Set("tp", tp)
				.Set("max_tp", sc.MaxTp)
				.Set("state", MemberState(c, sc))
				.Set("next", next is null ? "" : T("ui.battle.next", next))
				.Set("icon_count", sc.Statuses.Count + sc.Leaving.Count(l => l.Step == step))
				.Set("portrait", c.Id == vehicle ? art.VehicleFrame(c.Id, Direction.South, 0) : art.Portrait("", c.Id))
				.Set("icons", (UiSlot)((x, y, _, _) => DrawIcons(s, art, sc, step, p, x, y)))
				.Set("anchor", (UiSlot)((x, y, w, h) => anchors.TryAdd(c.Index, new Rectangle(x, y, w, h))));
		}
		List<Combatant> partySide = [.. all.Where(c => c.Side == Side.Party)];
		List<UiContext> party = [.. partySide.Select(Member)];
		List<UiContext> members = [.. partySide.Zip(party).Where(m => m.First.Id != vehicle).Select(m => m.Second)];
		List<UiContext> ride = [.. partySide.Zip(party).Where(m => vehicle.Length > 0 && m.First.Id == vehicle).Select(m => m.Second).Take(1)];
		ui.Set("party", party)
			.Set("members", members)
			.Set("vehicle", ride)
			.Set("target_pulse", 0.4f + 0.6f * pulse)
			.Set("hit_flash", !reduced && p < 0.5f)
			.Set("hit_fade", 1f - p * 2f);
		if (front)
		{
			_probeStrip = StripProbe(members, ride);
		}

		// The command window goes beside the member choosing, where the members made room for it, else beside the actor.
		Rectangle actor = bodies.TryGetValue(b.Actor, out Rectangle actorBody) ? actorBody
			: new Rectangle(s.Width / 2 - 64, (Ui.WindowRect(screen, "strip", s, ui)?.Y ?? 0) - 64, 128, 128);
		ui.Set("command_center", commandCenter)
			.Set("actor_left", actor.Left)
			.Set("actor_right", actor.Right)
			.Set("actor_center", actor.Center.X);

		Ui.Draw(screen, s, s, ui);
		if (front)
		{
			FrontProbes(s, all, bodies, labels, screen, ui);
		}
	}

	// The battle's windows' data: the menu, the description, the step's banner, the log, the keys and the result.
	private UiContext BattleWindows(IDrawSurface s, IReadOnlyList<Combatant> all, BattleInfo? info, (List<string> Items, List<bool>? Usable)? menu, int target,
		string screen)
	{
		BattleUi b = _battle;
		BattleStep? step = b.Phase == BattlePhase.Playback ? _timeline.Current : null;
		UiContext ui = UiData()
			.Set("phase", b.Phase.ToString().ToLowerInvariant())
			.Set("auto", b.Auto)
			.Set("cursor", _cursor)
			.Set("menu_shown", menu is not null)
			.Set("menu_title", MenuTitle(all))
			.Set("menu_items", menu is { } m ? m.Items.Select((item, i) => new UiContext().Set("text", item).Set("usable", m.Usable is null || m.Usable[i])).ToList() : [])
			.Set("key_confirm", Hint(GameAction.Confirm))
			.Set("key_back", Hint(GameAction.Back))
			.Set("key_fast_forward", Hint(GameAction.FastForward))
			.Set("key_text_log", Hint(GameAction.TextLog))
			.Set("hint", b.Phase switch
			{
				BattlePhase.Playback => T("ui.battle.hint.playback", Hint(GameAction.Confirm), Hint(GameAction.FastForward), Hint(GameAction.TextLog)) +
					(b.Auto ? T("ui.battle.hint.playback_auto", Hint(GameAction.Back)) : ""),
				BattlePhase.Ended => T("ui.battle.hint.continue", Hint(GameAction.Confirm)),
				_ => T("ui.battle.hint.command", Hint(GameAction.Confirm), Hint(GameAction.Back), Hint(GameAction.TextLog), Hint(GameAction.Pause)),
			})
			.Set("outcome", OutcomeText(info?.Outcome ?? Outcome.Ongoing))
			.Set("result_lines", b.Phase == BattlePhase.Ended ? ResultLines(info) : []);
		string? description = MenuDescription(all, target, out bool described);
		ui.Set("description_shown", described).Set("description", description ?? "");
		if (described)
		{
			_probeDescription = string.IsNullOrEmpty(description) ? "-" : description;
		}

		// The step's words: the action's name over its whole turn (in the front layout a framed name plate, with a cancel),
		// and the opening, the round, a notice or the outcome on their own, fading in and out.
		bool turnName = step?.Kind is StepKind.Banner or StepKind.Animate or StepKind.Effect or StepKind.Result or StepKind.Retarget;
		string banner = step is null || step.Kind is StepKind.Forward or StepKind.Back ? "" : turnName ? BannerOf(step) : step.Text;
		ui.Set("banner", banner)
			.Set("banner_plate", turnName || step?.Kind == StepKind.Cancel)
			.Set("banner_big", step?.Kind is StepKind.Outcome or StepKind.Opening or StepKind.Round)
			.Set("banner_cancel", step?.Kind == StepKind.Cancel)
			.Set("banner_alpha", turnName ? 1f : Math.Clamp(Math.Min(_timeline.Progress * 6f, (1f - _timeline.Progress) * 6f), 0f, 1f));

		// The log's newest lines: its last entries wrapped to the log window, the newest entry's lines bright.
		ui.Set("log_shown", b.LogShown);
		var lines = new List<UiContext>();
		if (b.LogShown)
		{
			int rows = Ui.Param(screen, "log_rows", ui, 0);
			int width = (Ui.WindowRect(screen, "log", s, ui)?.Width ?? 0) - Ui.Param(screen, "log_wrap_margin", ui, 0);
			for (int i = Math.Max(0, b.Log.Count - rows); i < b.Log.Count; i++)
			{
				foreach (string l in Style.Wrap(s, b.Log[i], width, Style.Small))
				{
					lines.Add(new UiContext().Set("text", l).Set("newest", i == b.Log.Count - 1));
				}
			}
			lines = [.. lines.Skip(Math.Max(0, lines.Count - rows))];
		}
		return ui.Set("log_lines", lines);
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

	// The highlighted option's or targeted enemy's description (null or "" when the data has none); `described` false when
	// no description shows.
	private string? MenuDescription(IReadOnlyList<Combatant> all, int target, out bool described)
	{
		BattleUi b = _battle;
		described = true;
		if (b.Phase == BattlePhase.Option && b.Options.Count > 0)
		{
			BattleOption option = b.Options[Math.Clamp(_cursor, 0, b.Options.Count - 1)];
			if (b.Worn.Contains(option.Id))
			{
				string? piece = _s.Description(DescriptionKind.Equipment, option.Id);
				return T("ui.battle.worn_note") + (piece is { Length: > 0 } ? " " + piece : "");
			}
			return _s.Description(option.Command == CommandKind.Item ? DescriptionKind.Item : DescriptionKind.Ability, option.Id);
		}
		if (b.Phase == BattlePhase.Target && target >= 0 && target < all.Count && all[target].Side == Side.Enemy)
		{
			return _s.Description(DescriptionKind.Enemy, all[target].Id);
		}
		described = false;
		return null;
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

	private static string BannerOf(BattleStep step) => step.Kind == StepKind.Banner ? step.Text : step.Turn?.Name ?? "";

	// The result screen's lines: experience, money, drops, and each level-up with what it taught; or the party's fall.
	private List<UiContext> ResultLines(BattleInfo? info)
	{
		BattleResult? result = _s.BattleResult();
		var lines = new List<UiContext>();
		UiContext Line(string kind, string text) => new UiContext().Set("kind", kind).Set("text", text);
		if (result is not null && result.Outcome is Outcome.Victory or Outcome.ScriptedEnd)
		{
			lines.Add(Line("experience", T("ui.battle.result.experience", result.Experience)));
			lines.Add(Line("money", T("ui.battle.result.money", result.Money)));
			if (result.Drops.Count > 0)
			{
				lines.Add(Line("found", T("ui.battle.result.found", ListText(result.Drops.Select(d => d.Value > 1 ? T("ui.common.item_count", _s.DisplayName(d.Key), d.Value) : _s.DisplayName(d.Key))))));
			}
			foreach (LevelUp up in result.LevelUps)
			{
				lines.Add(Line("level_up", T("ui.battle.result.level_up", up.Name, up.Level)));
				if (up.Learned.Count > 0)
				{
					lines.Add(Line("learned", T("ui.battle.result.learned", ListText(up.Learned.Select(_s.DisplayName)))));
				}
			}
		}
		else if (info?.GameOver == true)
		{
			lines.Add(Line("fallen", T("ui.battle.result.fallen")));
		}
		_probeVictory = string.Join(" / ", lines.Select(l => ((string)l.Get("text")!).Trim()).Prepend(OutcomeText(info?.Outcome ?? Outcome.Ongoing)));
		return lines;
	}
}
