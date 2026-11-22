using System.Globalization;

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
	private int _probeShake;
	private string _probeJolt = "0,0";
	private string _probeEnemyPlaces = "none";
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
	// SlotX and SlotFoot: the file's `party_slot_N_x` and `party_slot_N_foot` per marching place, -1 keeping the computed one.
	// StepAside: the members beside the open command window step aside for it (`step_aside`, 0 turns it off).
	private sealed record StageRules(int StageBottom, int PartyX, int PartyDx, int PartyFoot, int PartyDy, int PartySize, int VehicleX, int VehicleFoot,
		int VehicleSize, int StepGap, int StepMargin, int EnemyX, int RowMax, int RowSpan, int RowSpacingMax, int RowFoot, int RowStagger, bool RanksInterleaved,
		int RankSpan, int RankSpacingMax, int FrontFoot, int BackFoot, int[] SlotX, int[] SlotFoot, bool StepAside)
	{
		public int MemberX(int slot) => slot < SlotX.Length && SlotX[slot] >= 0 ? SlotX[slot] : PartyX + slot * PartyDx;

		public int MemberFoot(int slot) => slot < SlotFoot.Length && SlotFoot[slot] >= 0 ? SlotFoot[slot] : PartyFoot + slot * PartyDy;
	}

	// A param the screen's file leaves out takes the built-in file's value.
	private StageRules Rules(string screen, UiContext ctx)
	{
		int P(string name) => Ui.Param(screen, name, ctx, 0);
		return new StageRules(P("stage_bottom"), P("party_x"), P("party_dx"), P("party_foot"), P("party_dy"), P("party_size"), P("vehicle_x"), P("vehicle_foot"),
			P("vehicle_size"), P("step_gap"), P("step_margin"), P("enemy_x"), P("enemy_row_max"), P("enemy_row_span"), P("enemy_row_spacing_max"), P("enemy_row_foot"),
			P("enemy_row_stagger"), P("enemy_ranks_interleaved") != 0, P("enemy_rank_span"), P("enemy_rank_spacing_max"), P("enemy_front_foot"), P("enemy_back_foot"),
			[.. Enumerable.Range(0, 5).Select(n => Ui.Param(screen, $"party_slot_{n}_x", ctx, -1))], [.. Enumerable.Range(0, 5).Select(n => Ui.Param(screen, $"party_slot_{n}_foot", ctx, -1))],
			Ui.Param(screen, "step_aside", ctx, 1) != 0);
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
		// Sprites with palette tracks (the shadow, the pointer) step on the battle's clock, an entry track from the battle's
		// start, as the field steps them on its own.
		int clock = _clockMs, began = _backdropStartMs;
		art.MotionTime = w => w.Flag is { } flag && !_s.Flag(flag) || w.UnlessFlag is { } unless && _s.Flag(unless) ? null : w.Entry ? Math.Max(0, clock - began) : clock;
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
		_probeSprites.Clear();
		bool front = _stageLayout == BattleLayout.Front;
		string screen = BattleScreenOf(_stageLayout);
		string vehicle = info?.Vehicle ?? "";
		(List<string> Items, List<bool>? Usable)? menu = MenuRows(all);
		UiContext ui = BattleWindows(s, all, info, menu, target, screen);
		StageRules rules = Rules(screen, ui);
		int stageBottom = rules.StageBottom;
		int Param(string name) => Ui.Param(screen, name, ui, 0);
		int effectScale = Param("effect_scale"), effectScaleLarge = Param("effect_scale_large");
		int pointerSize = Param("pointer_size"), pointerMemberY = Param("pointer_member_y");
		Icons icons = new(Param("icon_size"), Param("icon_gap"));
		// The figures' steps, lunges, shakes and hovering, and the screen's shake, in percent of the 1280x720 distances.
		float motion = Ui.Param(screen, "motion_percent", ui, 100) / 100f;

		var ctx = new StageCtx
		{
			Step = step, Elapsed = _timeline.Elapsed, Front = front, Motion = motion, Reduced = reduced, PartyCount = partyCount, Vehicle = vehicle, All = all,
			EffectScale = effectScale, EffectScaleLarge = effectScaleLarge, StageBottom = stageBottom, Width = s.Width, Height = s.Height,
		};
		// The screen shakes by the largest of the scripts' shakes (reduced flash damps them), in percent of the layout's motion.
		float shake = Shake(ctx, out Point backdropShift, out Point figureShift, out int joltMs);
		// A vertical split reveal brings the enemies' labels up with their figures.
		int splitY = 0;
		if (step is { Kind: StepKind.Transition, Run: { } split })
		{
			(int bx, int fx, bool vertical) = SplitShift(split, _timeline.Elapsed);
			if (vertical)
			{
				backdropShift.Y += bx;
				figureShift.Y += fx;
				splitY = fx;
			}
			else
			{
				backdropShift.X += bx;
				figureShift.X += fx;
			}
		}
		_probeShake = (int)MathF.Round(shake);
		var jolt = new Point((int)MathF.Round(shake * MathF.Sin(joltMs * 0.09f)), (int)MathF.Round(shake * MathF.Cos(joltMs * 0.13f)));
		_probeJolt = $"{jolt.X},{jolt.Y}";

		FieldInfo? field = _s.FieldInfo();
		(int backdrop, _probeBackdrop) = art.Backdrop(field?.AreaKind ?? AreaKind.Field, field?.AreaId ?? "", info?.Encounter ?? "", _battleTerrain, _battleCell, _s.Flag);

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
				f = (rules.MemberX(i), rules.MemberFoot(i), rules.PartySize);
			}
			else if (!enemyPlaces.TryGetValue(shown[i].Position, out f))
			{
				f = enemyPlaces[shown[i].Position] = PlaceEnemy(screen, rules, ui, shown[i].Position, enemyCount);
				if (Placement(info?.Encounter ?? "", enemyCount, shown[i].Position, front) is { } placed && Ui.Param(screen, $"enemy_slot_{shown[i].Position}_x", ui, -1) < 0)
				{
					int size = placed.Rank is null ? f.Size : Ui.Param(screen, "enemy_size", new UiContext(ui).Set("enemy_count", enemyCount).Set("enemy_back", placed.Rank == "back"), 0);
					f = enemyPlaces[shown[i].Position] = (placed.X, placed.Foot, size);
				}
			}
			return centers.TryGetValue(i, out int cx) ? (cx, f.Foot, f.Size) : f;
		}
		ctx.Where = Where;
		var bodies = ctx.Bodies;
		var figures = new List<string>();
		var posed = new List<string>();
		var hovering = new List<string>();
		// The figures' pictures go on the stage when the layout draws its `stage` slot; where each lands is known now.
		var pictures = new List<Action>();
		// The battle layout's `party_figures` and `enemy_figures` (expressions, default 1) leave a side's figures out of the
		// stage slot, its backdrop staying; their places still anchor effects and numbers.
		bool partyFigures = Ui.Param(screen, "party_figures", ui, 1) != 0, enemyFigures = Ui.Param(screen, "enemy_figures", ui, 1) != 0;
		HashSet<int>? solo = SoloMembers(step, commanding, b.Actor, partyCount);
		bool hideFigure = false;
		var hidden = new List<Action>();
		void Picture(int image, int x, int y, int w, int h, Color tint)
		{
			if (!hideFigure)
			{
				pictures.Add(() => s.Image(image, x, y, w, h, tint));
			}
		}
		bool Behind(int i) => front && i < partyCount;
		ctx.Behind = Behind;
		_probeFigureLooks.Clear();
		var order = Enumerable.Range(0, Math.Min(all.Count, shown.Count)).OrderBy(i => Where(i).Foot).ToList();
		foreach (int i in order)
		{
			Combatant c = all[i];
			ShownCombatant sc = shown[i];
			bool enemy = c.Side == Side.Enemy;
			hideFigure = !(enemy ? enemyFigures : partyFigures) || !enemy && solo is not null && !solo.Contains(i);
			string id = enemy ? sc.Id : c.Id;
			// In a vehicle battle the members ride inside it: only the vehicle stands on the field, at the party's middle.
			if (vehicle.Length > 0 && !enemy && c.Id != vehicle)
			{
				continue;
			}
			(int x, int foot, int size) = Where(i);
			FigureLook fl = Figure(i, sc, ctx, front && !enemy && commanding && i == b.Actor);
			if (fl.Hidden)
			{
				continue;
			}
			if (fl.PlaceOf >= 0 && fl.PlaceOf < shown.Count)
			{
				(int px, int pf, _) = Where(fl.PlaceOf);
				(x, foot) = (px + (int)MathF.Round(fl.PlaceDx), pf + (int)MathF.Round(fl.PlaceDy));
			}
			// In the side layout a figure steps toward the other side; in the front one an enemy comes toward the camera
			// and a member rises out of the line.
			int toward = enemy ? 1 : -1;
			float dx = (front ? 0 : toward * fl.Fwd) + fl.Sx, dy = (front ? toward * fl.Fwd : 0) + fl.Sy;
			dx += fl.AddX;
			dy += fl.AddY;
			string animation = fl.Pose;
			Color tint = fl.Tint;
			float alpha = fl.Alpha;
			Figure figure = enemy ? art.BattleFigure(id, true)
				: c.Id == vehicle ? (front ? art.VehicleBackFigure(c.Id) : art.VehicleFigure(c.Id))
				: front ? art.BackFigure(c.Id) : art.BattleFigure(c.Id, false);
			if (!enemy && !hideFigure)
			{
				figures.Add($"{c.Id} {(c.Id == vehicle ? "vehicle " : "")}{(front ? "back" : "side")}");
			}
			// An action's own pose, when the figure has it; else the action's animation plays, said once per figure and pose.
			if (fl.LookPose is { } pose)
			{
				if (figure.Animations.ContainsKey(pose))
				{
					animation = pose;
				}
				else
				{
					MissingPose($"figure:{(enemy ? "enemy" : front ? "back" : "party")}:{c.Id}", pose, $"its {animation} animation plays");
				}
			}
			if (animation != "idle")
			{
				posed.Add($"{c.Label} {animation}");
			}
			FrameRun run = figure.Animations.GetValueOrDefault(animation) ?? figure.Idle;
			bool idle = run == figure.Idle;
			int cell = fl.Frame >= 0 ? -1 : run.FrameAt(idle ? fl.IdleMs ?? _clockMs + i * 137 : fl.PoseMs < 0 ? _timeline.ElapsedMs : fl.PoseMs, loop: idle || fl.PoseLoop);
			int image = fl.Frame >= 0 ? figure.Images[Math.Clamp(fl.Frame, 0, figure.Images.Length - 1)] : figure.Images[cell];
			int left = x - size / 2 + (int)MathF.Round(dx) + jolt.X + figureShift.X, top = foot - size + (int)MathF.Round(dy) + jolt.Y + figureShift.Y;
			// In the front layout a flying enemy hovers over its place, bobbing, while its shadow stays on the ground.
			int hover = fl.IdleLift is float idleLift ? (int)MathF.Round(idleLift) : front && enemy && c.Flying ? size / 5 + (int)MathF.Round(5 * motion * MathF.Sin((_clockMs + i * 211) / 420f)) : 0;
			int lift = (int)MathF.Round(fl.Lift);
			Rectangle body = art.Stand(image, left, top - hover - lift, size);
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
			// A change of form: the old form fades out as the new one fades in.
			if (fl.Crossfade >= 0 && sc.PreviousId.Length > 0)
			{
				Figure old = art.BattleFigure(sc.PreviousId, true);
				int oldImage = old.Images[old.Idle.FrameAt(_clockMs + i * 137, loop: true)];
				Rectangle was = art.Stand(oldImage, left, top, size);
				Picture(oldImage, was.X, was.Y, was.Width, was.Height, Color.White * (alpha * (1f - fl.Crossfade)));
				tint = Color.Lerp(Color.White * 4f, tint, fl.Crossfade);
				alpha *= fl.Crossfade;
			}
			int recolored = 0;
			if (fl.Palette is { Count: > 0 } && Resolve(fl.Palette, figure.Palette) is { Count: > 0 } palette)
			{
				image = s.Derive(image, PaletteKey(palette), (px, _, _) => Recolor(px, palette));
				recolored = palette.Count;
			}
			FigurePicture(s, hideFigure ? hidden : pictures, image, body, tint * alpha, fl, foot + (int)MathF.Round(dy) + jolt.Y + figureShift.Y, i);
			_probeFigureLooks[c.Label] = $"pose {animation} frame {(fl.Frame >= 0 ? fl.Frame : cell)} offset {(int)MathF.Round(dx)},{(int)MathF.Round(dy)}" +
				$" color #{tint.R:x2}{tint.G:x2}{tint.B:x2} alpha {alpha.ToString("0.00", CultureInfo.InvariantCulture)}" + (fl.Life.Length > 0 ? " " + fl.Life : "")
					+ (recolored > 0 ? $" palette {recolored}" : "");
		}
		hideFigure = false;
		_lastBodies = new Dictionary<int, Rectangle>(bodies);
		_probeFigures = figures.Count == 0 ? "none" : string.Join(", ", figures);
		_probeFigurePoses = posed.Count == 0 ? "none" : string.Join(", ", posed);
		_probeBodies = string.Join(", ", bodies.OrderBy(x => x.Key).Select(x => $"{all[x.Key].Label} {x.Value.Width}x{x.Value.Height} {x.Value.Bottom - Where(x.Key).Foot}"));
		_probeFlying = hovering.Count == 0 ? "none" : string.Join(", ", hovering);
		_probePlaces = string.Join(", ", bodies.Keys.Where(k => k < partyCount).Order().Select(k => $"{all[k].Name} {Where(k).X} {Where(k).Foot}").DefaultIfEmpty("none"));
		_probeEnemyPlaces = string.Join(", ", order.Where(k => k >= partyCount && shown[k].Alive).Select(k => $"{shown[k].Label} {Where(k).X} {Where(k).Foot} {Where(k).Size}").DefaultIfEmpty("none"));
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
		List<(Rectangle Area, Color Color)> stageFills = Fills(ctx, "stage", out bool flash);
		_probeFlash = flash;
		ctx.Anchors = anchors;

		void DrawEffects()
		{
			DrawSprites(s, art, ctx, "effects");

			// Labels, statuses and HP bars under the enemies, as before: one per enemy, its group's name and number.
			// An enemy merging away takes no label: it is sliding into the other one.
			int labelWidth = Param("label_max_width"), labelY = Param("label_y"), labelH = Param("label_h"), labelPad = Param("label_pad"), labelTextY = Param("label_text_y");
			int barY = Param("enemy_bar_y"), barW = Param("enemy_bar_w"), barH = Param("enemy_bar_h"), iconsY = Param("enemy_icons_y");
			bool showLabels = Ui.Param(screen, "enemy_labels", ui, 1) != 0, showBars = Ui.Param(screen, "enemy_bars", ui, 1) != 0;
			Dictionary<string, UiStatusLook>? enemyLooks = Ui.Param(screen, "enemy_statuses", ui, 1) != 0 ? Layout.Screen(screen).Statuses : null;
			int labelsDrawn = 0, barsDrawn = 0;
			var enemyIcons = new List<string>();
			foreach (int i in order.Where(i => all[i].Side == Side.Enemy && (shown[i].Alive || (shown[i].AliveStep is not null && shown[i].AliveStep == step))
				&& !(step is { Kind: StepKind.Merge } && i == step.From)))
			{
				(int x, int foot, _) = Where(i);
				x += figureShift.X;
				foot += splitY;
				bool targeted = i == target;
				string label = Style.Fit(s, shown[i].Label, labelWidth, Style.Small);
				int lw = s.Measure(label, Style.Small);
				if (showLabels)
				{
					s.Fill(x - lw / 2 - labelPad, foot + labelY, lw + 2 * labelPad, labelH, Color.Black * 0.6f);
					s.Text(label, x - lw / 2, foot + labelY + labelTextY, targeted ? Style.Accent : Style.Text, Style.Small);
					labelsDrawn++;
				}
				if (showBars)
				{
					Style.Bar(s, x - barW / 2, foot + barY, barW, barH, Shown(shown[i], step).Hp, shown[i].MaxHp, Style.Bad);
					barsDrawn++;
				}
				DrawIcons(s, art, icons, shown[i], step, x - barW / 2, foot + iconsY, enemyLooks);
				if (shown[i].Statuses.Count > 0)
				{
					List<string> drawn = [.. Iconed(shown[i].Statuses, enemyLooks ?? [])];
					enemyIcons.Add($"{shown[i].Label} {(drawn.Count > 0 ? string.Join(" ", drawn) : "none")}");
				}
				// Without its label the enemy's mark is its bar and icons.
				int lx = showLabels ? Math.Min(x - lw / 2 - labelPad, x - barW / 2) : x - barW / 2;
				int ly = showLabels ? labelY : showBars ? barY : iconsY;
				labels[shown[i].Label] = new Rectangle(lx, foot + ly, Math.Max(x - lx, showLabels ? lw / 2 + labelPad : 0) * 2, iconsY + icons.Size - ly);
			}
			_probeEnemyMarks = $"labels {labelsDrawn} bars {barsDrawn}";
			_probeEnemyIcons = enemyIcons.Count == 0 ? "none" : string.Join(", ", enemyIcons);

			// The target cursor over the figure being chosen (a member seen from behind gets it over the command window), and
			// over the retarget step's new target as it moves there.
			int pointerY = Param("pointer_y"), pointerBob = Param("pointer_bob");
			int PointerY(int i, Rectangle r) => Behind(i) ? r.Y - pointerMemberY : r.Y - pointerY;
			if (target >= 0 && !Behind(target) && bodies.TryGetValue(target, out Rectangle aim))
			{
				s.Image(art.Pointer, aim.Center.X - pointerSize / 2, PointerY(target, aim) + (int)(pointerBob * pulse), pointerSize, pointerSize, Color.White);
			}
			if (step is { Kind: StepKind.Retarget } && bodies.TryGetValue(step.To, out Rectangle to))
			{
				Rectangle from = bodies.GetValueOrDefault(step.From, to);
				float e = Ease(p);
				s.Image(art.Pointer, (int)MathHelper.Lerp(from.Center.X, to.Center.X, e) - pointerSize / 2, (int)MathHelper.Lerp(PointerY(step.From, from), PointerY(step.To, to), e),
					pointerSize, pointerSize, Color.White);
			}

			// Floating numbers and words over each target: damage, critical, misses, weak and resisted hits, healing, statuses.
			// Over a member seen from behind they rise from its head, clear of the enemies' labels.
			var marks = new List<string>();
			DrawSprites(s, art, ctx, "over_labels", marks);
			_probeMarks = marks.Count == 0 ? "none" : string.Join(", ", marks);
			if (step is { Kind: StepKind.Result or StepKind.RoundEnd })
			{
				UseTextRole(s, TextRole.BattleNumbers);
				var sizes = new NumberSizes(Param("number_size"), Param("number_critical_size"), Param("number_tp_size"), Param("number_word_size"));
				int numberY = Param("number_y"), numberMemberY = Param("number_member_y"), numberRise = Param("number_rise"), numberGap = Param("number_gap");
				var stacked = new Dictionary<int, int>();
				// The layout's `damage_numbers` "rolled" floats what the roll gave (BattleEvent.Rolled) before hit points, a shield's
				// cap or the maximum took any off; "applied", the default, the change made.
				bool rolledNumbers = Ui.ParamText(screen, "damage_numbers", ui, "applied") == "rolled";
				_probeNumbers.Clear();
				BattleEvent? previous = null;
				foreach (BattleEvent e in step.Events)
				{
					ReleaseUnit? unit = step.Units.FirstOrDefault(u => u.Events.Contains(e));
					if (unit is { Released: false })
					{
						previous = e;
						continue;
					}
					float p = Since(step, unit is null ? 0 : unit.Forced ? step.Length : unit.At);
					int t = e.Type == BattleEventType.ItemHadNoEffect ? e.Actor : e.Target;
					(BattleEvent plain, BattleEvent? previousEvent) = Plain(e, previous, screen, ui);
					if (rolledNumbers && plain.Type is BattleEventType.DamageDealt or BattleEventType.Healed && plain.Rolled > 0)
					{
						plain = plain with { Amount = plain.Rolled };
					}
					if (!anchors.TryGetValue(t, out Rectangle r) || e.Type == BattleEventType.Missed && !Marked(screen, ui, "miss", t)
						|| Floating(plain, previousEvent, sizes) is not { } floating)
					{
						previous = e;
						continue;
					}
					previous = e;
					int n = stacked.GetValueOrDefault(t);
					stacked[t] = n + 1;
					_probeNumbers.Add($"{_show.All.ElementAtOrDefault(t)?.Label ?? "?"} {floating.Text} {NumberKind(plain, previousEvent)}");
					DrawNumber(s, e, floating, p, r, Behind(t), n, numberY, numberMemberY, numberRise, numberGap, NumberKind(plain, previousEvent),
						_timeline.Elapsed - (unit is null ? 0 : unit.At), t < partyCount, step.Length - (unit is null ? 0 : unit.Forced ? step.Length : unit.At));
				}
				UseTextRole(s, TextRole.Menu);
			}
			DrawSprites(s, art, ctx, "top");

			// The scripts' flashes and fills over the stage.
			foreach ((Rectangle area, Color color) in stageFills)
			{
				s.Fill(area.X, area.Y, area.Width, area.Height, color);
			}
		}

		ui.Set("stage", (UiSlot)((_, _, _, _) =>
		{
			var shifted = new Point(jolt.X + backdropShift.X, jolt.Y + backdropShift.Y);
			if (!DrawScriptedBackdrop(s, art, ctx, backdrop, shifted, front, Param("backdrop_h"), Param("ground_h")))
			{
				if (front)
				{
					DrawFrontBackdrop(s, backdrop, shifted, Param("backdrop_h"), Param("ground_h"));
				}
				else
				{
					s.Image(backdrop, shifted.X - 8, shifted.Y - 8, s.Width + 16, stageBottom + 8, Color.White);
				}
			}
			DrawSprites(s, art, ctx, "under_figures");
			foreach (Action picture in pictures)
			{
				picture();
			}
			DrawSprites(s, art, ctx, "over_figures");
		}));
		ui.Set("effects", (UiSlot)((_, _, _, _) => DrawEffects()));
		// A member being targeted may stand behind the command window: its pointer stays on top.
		ui.Set("target_pointer", (UiSlot)((_, _, _, _) =>
		{
			if (target >= 0 && target < all.Count && all[target].Side == Side.Party && bodies.TryGetValue(target, out Rectangle aim))
			{
				s.Image(art.Pointer, aim.Center.X - pointerSize / 2, aim.Y - pointerMemberY, pointerSize, pointerSize, Color.White);
			}
		}));

		// The party's status: each member's numbers, marks and status icons, and in the front layout the strip slot it
		// stands over, where effects land on a member with no figure.
		Dictionary<string, UiStatusLook> looks = Layout.Screen(screen).Statuses;
		var partyStatus = new List<string>();
		var commands = new List<string>();
		Dictionary<string, int> levels = _s.Party().ToDictionary(m => m.Id, m => m.Level);
		UiContext Member(Combatant c)
		{
			ShownCombatant sc = shown.ElementAtOrDefault(c.Index) ?? new ShownCombatant(c, -1);
			(int hp, int tp) = Shown(sc, step);
			string? next = NextCommand(c);
			string statusColor = sc.Statuses.Select(id => looks.GetValueOrDefault(id)).Where(l => l?.Color is not null).OrderByDescending(l => l!.Priority).FirstOrDefault()?.Color ?? "";
			List<string> iconed = [.. Iconed(sc.Statuses, looks)];
			partyStatus.Add($"{c.Name}: {(statusColor.Length > 0 ? statusColor : "-")} {(iconed.Count > 0 ? string.Join(" ", iconed) : "none")}");
			string command = CommandName(c.CommandKind, c.CommandId);
			commands.Add($"{c.Name} {(command.Length > 0 ? command : "-")}");
			UiContext member = new UiContext(ui)
				.Set("name", c.Name)
				.Set("alive", sc.Alive)
				.Set("acting", commanding ? c.Index == b.Actor : turn?.Actors.Contains(c.Index) == true)
				.Set("targeted", c.Index == target || (step?.Kind is StepKind.Effect or StepKind.Result && turn?.Targets.Contains(c.Index) == true))
				.Set("hit", step is { Kind: StepKind.Result } && step.Units.Any(u => u.Released && u.Events.Any(e => e.Type == BattleEventType.DamageDealt && e.Target == c.Index)))
				.Set("hp", hp)
				.Set("max_hp", sc.MaxHp)
				.Set("hp_low", hp * 4 <= sc.MaxHp)
				.Set("tp", tp)
				.Set("max_tp", sc.MaxTp)
				.Set("state", MemberState(c, sc))
				.Set("next", next is null ? "" : T("ui.battle.next", next))
				.Set("status_color", statusColor)
				.Set("icon_count", iconed.Count + sc.Leaving.Count(l => l.Step == step && looks.GetValueOrDefault(l.Id)?.Icon != false))
				.Set("portrait", c.Id == vehicle ? art.VehicleFrame(c.Id, Direction.South, 0) : art.Portrait("", c.Id))
				.Set("icons", (UiSlot)((x, y, _, _) => DrawIcons(s, art, icons, sc, step, x, y, looks)))
				.Set("anchor", (UiSlot)((x, y, w, h) => anchors.TryAdd(c.Index, new Rectangle(x, y, w, h))))
				.Set("command", command)
				.Set("command_icon", command.Length > 0 ? art.CommandIcon(command) : -1)
				.Set("level", levels.GetValueOrDefault(c.Id))
				.Set("fallen", !sc.Alive)
				.Set("slot", c.Index);
			foreach (string status in _data.Statuses)
			{
				member.Set(UiFields.StatusFlag + status, sc.Statuses.Contains(status));
			}
			return member;
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
		_probePartyStatus = string.Join(", ", partyStatus);
		_probeCommands = string.Join(", ", commands);

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
			.Set("key_confirm", Hint(GameAction.Confirm))
			.Set("key_back", Hint(GameAction.Back))
			.Set("key_fast_forward", Hint(GameAction.FastForward))
			.Set("key_text_log", Hint(GameAction.TextLog))
			.Set("hint", b.Phase switch
			{
				BattlePhase.Playback => T("ui.battle.hint.playback", Hint(GameAction.Confirm), Hint(GameAction.FastForward), Hint(GameAction.TextLog)) +
					(b.Auto ? T("ui.battle.hint.playback_auto", Hint(GameAction.Back)) : ""),
				BattlePhase.Ended when !ResultConfirms => "",
				BattlePhase.Ended => T("ui.battle.hint.continue", Hint(GameAction.Confirm)),
				_ => T("ui.battle.hint.command", Hint(GameAction.Confirm), Hint(GameAction.Back), Hint(GameAction.TextLog), Hint(GameAction.Pause)),
			})
			.Set("outcome", OutcomeText(info?.Outcome ?? Outcome.Ongoing));
		int page = 0, pages = 0;
		List<UiContext> results = b.Phase == BattlePhase.Ended ? ResultLines(info) : [];
		ui.Set("result_lines", b.Phase == BattlePhase.Ended ? ResultPage(ShownResultLines(results), screen, out page, out pages) : [])
			.Set("level_lines", LevelLines(results))
			.Set("result_phase", b.Phase != BattlePhase.Ended ? "" : ResultSequence is { } sequence ? b.ResultStep >= 0 ? sequence[b.ResultStep].Phase : "" : "result")
			.Set("result_phase_ms", b.Phase == BattlePhase.Ended && ResultSequence is not null ? b.ResultStepMs : 0)
			.Set("result_page", b.Phase == BattlePhase.Ended ? page + 1 : 0)
			.Set("result_pages", b.Phase == BattlePhase.Ended ? pages : 0)
			.Set("result_more", b.Phase == BattlePhase.Ended && page + 1 < pages)
			.Set("result_step", b.Phase == BattlePhase.Ended)
			.Set("result_confirm", b.Phase == BattlePhase.Ended && ResultConfirms)
			.Set("transition_playing", TransitionPlaying)
			.Set("page_arrow", PageArrow);
		// The list open and the command row it came from; the member choosing or acting; and whether the turn playing is a
		// command a member chose from a list rather than a plain attack.
		bool listed = b.Phase is BattlePhase.Option or BattlePhase.Macro or BattlePhase.Target;
		ui.Set("list_kind", b.Phase switch
			{
				BattlePhase.Command => "command",
				BattlePhase.Top => "top",
				BattlePhase.Option => b.MenuChoice,
				BattlePhase.Macro => "macro",
				BattlePhase.Target => "target",
				BattlePhase.AutoChoice => "auto",
				_ => "",
			})
			.Set("list_index", listed ? b.MenuIndex : -1);
		int partyCount = info?.PartyCount ?? all.Count(c => c.Side == Side.Party);
		BattleTurn? playing = step?.Turn;
		int actorSlot = b.Phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro && !b.FromTop ? b.Actor
			: playing is { Actors: [int first, ..] } && first < partyCount ? first : -1;
		string actionCommand = playing is null ? "" : playing.Combo ? "combo" : CommandName(playing.Command, playing.Ability);
		ui.Set("actor_slot", actorSlot)
			.Set("action_command", actionCommand)
			.Set("action_chosen", playing is { Actors: [int actor, ..] } && actor < partyCount && actionCommand is not ("" or "attack"));
		// A row's own fields, then the screen's: a row template may read the cursor and the phase. An option's name, cost and
		// uses left (-1 for none) stand apart from its row text.
		List<string> rowCommands = b.Phase switch
		{
			BattlePhase.Command => [.. CommandRows().Select(r => r.Command)],
			BattlePhase.Top => [.. TopMenu],
			_ => [],
		};
		// An item option's row also reads its copies as `count` (other rows keep `count` for the rows' count) and as
		// `count_text`.
		BattleOption? ItemOption(int i) => b.Phase == BattlePhase.Option && i < b.Options.Count && b.Options[i] is { Command: CommandKind.Item, Uses: >= 0 } o
			&& !b.Worn.Contains(o.Id) ? o : null;
		List<UiContext> menuRows = menu is { } m ? [.. m.Items.Select((item, i) => new UiContext(ui).Set("text", item).Set("usable", m.Usable is null || m.Usable[i])
			.Set("command", i < rowCommands.Count ? rowCommands[i] : "")
			.Set("icon", i < rowCommands.Count && b.Phase == BattlePhase.Command && _art is { } icons ? icons.CommandIcon(rowCommands[i]) : -1)
			.Set("name", b.Phase == BattlePhase.Option && i < b.Options.Count ? b.Options[i].Name : item)
			.Set("cost", b.Phase == BattlePhase.Option && i < b.Options.Count ? b.Options[i].Cost : 0)
			.Set("uses", b.Phase == BattlePhase.Option && i < b.Options.Count ? b.Options[i].Uses : -1)
			.Set("max_uses", b.Phase == BattlePhase.Option && i < b.Options.Count ? b.Options[i].MaxUses : -1)
			.Set("count_text", ItemOption(i) is { } counted ? CountText(screen, counted.Uses) : ""))] : [];
		for (int i = 0; i < menuRows.Count; i++)
		{
			if (ItemOption(i) is { } option)
			{
				menuRows[i].Set("count", option.Uses);
			}
		}
		ui.Set("menu_items", menuRows);
		// The enemies standing, by name in the order they first stand, each with how many share it and the first one's level.
		List<UiContext> groups = [.. _show.All.Select((c, i) => (Shown: c, Index: i)).Where(x => x.Index < all.Count && all[x.Index].Side == Side.Enemy && x.Shown.Alive)
			.GroupBy(x => _s.DisplayName(x.Shown.Id))
			.Select(g => new UiContext().Set("name", g.Key).Set("number", g.Count()).Set("level", all[g.First().Index].Level)
				.Set("text", g.Count() > 1 ? T("ui.common.item_count", g.Key, g.Count()) : g.Key))];
		_probeEnemyGroups = groups.Count == 0 ? "none" : string.Join(", ", groups.Select(g => g.Get("text")));
		ui.Set("enemy_groups", groups);
		string? description = MenuDescription(all, target, out bool described);
		List<UiContext> checks = MacroChecks(ui, out bool trouble);
		ui.Set("description_shown", described || checks.Count > 0).Set("description", description ?? "")
			.Set("macro_steps", checks)
			.Set("macro_trouble", trouble)
			.Set("macro_trouble_text", trouble ? T("ui.battle.macro_trouble") : "");
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
		if (NameWindow(step) is float nameAlpha && step?.Turn is { } named)
		{
			ui.Set("banner", nameAlpha > 0 ? named.Name : "").Set("banner_plate", true).Set("banner_alpha", nameAlpha);
		}

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

	// A command as the layouts' `command` fields name it: attack, technique, skill, combo, item, defend, escape, or "".
	private string CommandName(CommandKind kind, string id) => kind switch
	{
		CommandKind.None => "",
		CommandKind.Ability => _data.Actions.GetValueOrDefault(id)?.Kind is { } k && AssetManifest.CommandNames.Contains(k) ? k : "technique",
		_ => kind.ToString().ToLowerInvariant(),
	};

	// A member's state under its name: fallen, defending and its statuses.
	private string MemberState(Combatant c, ShownCombatant sc) =>
		!sc.Alive ? T("ui.battle.fallen") : string.Join(" ", sc.Statuses.Select(_s.DisplayName).Prepend(c.Defending ? T("ui.battle.defending") : "").Where(x => x.Length > 0));

	private string? NextCommand(Combatant c) => c.CommandKind == CommandKind.None ? null
		: c.CommandId.Length > 0 ? _s.DisplayName(c.CommandId) : T(NameKind.CommandKind, (int)c.CommandKind);

	private string OutcomeText(Outcome outcome) => outcome == Outcome.Ongoing ? "" : T("ui.battle.outcome." + GameSession.NameOf(NameKind.Outcome, (int)outcome));

	private string MenuTitle(IReadOnlyList<Combatant> all) => _battle.Phase switch
	{
		BattlePhase.Command => _battle.Actor < all.Count ? all[_battle.Actor].Name : "",
		BattlePhase.Top => T("ui.battle.title.top"),
		BattlePhase.Option => T("ui.battle.command." + _battle.MenuChoice),
		BattlePhase.Target => T("ui.battle.title.target"),
		BattlePhase.Macro => T("ui.battle.title.macro"),
		BattlePhase.AutoChoice => T(_autoChoiceStep == 0 ? "ui.battle.title.auto_trouble" : "ui.battle.title.auto_policy"),
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
			BattlePhase.Top => ([.. TopMenu.Select(t => T("ui.battle.top." + t))], null),
			BattlePhase.Option => (b.Options.Select(OptionText).ToList(), b.Options.Select(o => o.Usable).ToList()),
			BattlePhase.Target => (b.Targets.Select(t => t < all.Count ? (all[t].Side == Side.Party ? all[t].Name : all[t].Label) : "?").ToList(),
				b.Refused.Count == 0 ? null : [.. b.Targets.Select(t => !b.Refused.Contains(t))]),
			BattlePhase.Macro => ([.. BattleMacros().Select(m => LayoutSwitch("macro_auto_first") && m == _s.Settings().AutoMacro ? T("ui.battle.auto_macro_row", m) : m)], null),
			BattlePhase.AutoChoice => (_autoChoiceStep == 0 ? [.. AutoTroubles.Select(t => T(NameKind.MacroTrouble, (int)t))] : [.. AutoPolicies.Select(p => T(NameKind.MacroPolicy, (int)p))], null),
			_ => null,
		};
	}

	// With the battle layout's `macro_check`, the highlighted macro's steps as the session would play them this round
	// (CheckMacro): each step's text, `ok`, and the issue or refusal of one that cannot run; `trouble` whether the round
	// would be in trouble (TroubleReport). Empty in other phases and without the param.
	private List<UiContext> MacroChecks(UiContext ui, out bool trouble)
	{
		trouble = false;
		List<string> macros = _battle.Phase == BattlePhase.Macro && LayoutSwitch("macro_check") ? BattleMacros() : [];
		if (macros.Count == 0)
		{
			_probeMacroCheck = "-";
			return [];
		}
		string name = macros[Math.Clamp(_cursor, 0, macros.Count - 1)];
		if (!_s.CheckMacro(name, out IReadOnlyList<MacroStepCheck> checks).Ok)
		{
			_probeMacroCheck = "-";
			return [];
		}
		trouble = _s.TroubleReport(name, out MacroTroubleReport report).Ok && report.Holds;
		IReadOnlyList<MacroStep> steps = _s.MacroList().FirstOrDefault(m => m.Name == name)?.Steps ?? [];
		var rows = new List<UiContext>();
		for (int i = 0; i < checks.Count; i++)
		{
			MacroStepCheck c = checks[i];
			string step = i < steps.Count ? StepText(steps[i] with { Issue = MacroIssue.None }) : _s.DisplayName(c.Member);
			string issue = c.Ok ? "" : c.RefusalText.Length > 0 ? c.RefusalText
				: c.Status.Length > 0 ? $"{T(NameKind.MacroIssue, (int)c.Issue)} ({_s.DisplayName(c.Status)})" : T(NameKind.MacroIssue, (int)c.Issue);
			rows.Add(new UiContext(ui).Set("text", c.Ok ? step : T("ui.battle.macro_step_issue", step, issue)).Set("ok", c.Ok).Set("issue", issue));
		}
		_probeMacroCheck = string.Join(" / ", rows.Select(r => (bool)r.Get("ok")! ? "ok" : (string)r.Get("issue")!)) + (trouble ? " / trouble" : "");
		return rows;
	}

	// The macro check the last frame drew: each step `ok` or its issue, then `trouble` when the round would be in trouble.
	private string _probeMacroCheck = "-";

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
				string? piece = Description(DescriptionKind.Equipment, option.Id);
				return T("ui.battle.worn_note") + (piece is { Length: > 0 } ? " " + piece : "");
			}
			return Description(option.Command == CommandKind.Item ? DescriptionKind.Item : DescriptionKind.Ability, option.Id);
		}
		// The top-level step's Macro says so when there are none to choose.
		if (b.Phase == BattlePhase.Top && TopMenu is var top && _cursor < top.Length && top[_cursor] == "macro" && _s.Macros().Count == 0)
		{
			return T("ui.battle.no_macros");
		}
		if (b.Phase == BattlePhase.Target && target >= 0 && target < all.Count && all[target].Side == Side.Enemy)
		{
			return Description(DescriptionKind.Enemy, all[target].Id);
		}
		described = false;
		return null;
	}

	// How far a step is past `at` (clock units into it) toward its end, 0 to 1: the step's progress for what started with it.
	private float Since(BattleStep? step, long at) => step is null || step != _timeline.Current ? 1f
		: Math.Clamp(BattleClock.Ratio(_timeline.Elapsed - at, step.Length - at), 0f, 1f);

	// A bar's numbers while the release that changed them plays, else where it left them.
	private (int Hp, int Tp) Shown(ShownCombatant c, BattleStep? step)
	{
		if (c.BarStep is null || c.BarStep != step)
		{
			return (c.Hp, c.Tp);
		}
		float p = Since(step, c.BarAt);
		return ((int)MathF.Round(MathHelper.Lerp(c.HpFrom, c.Hp, Ease(p))), (int)MathF.Round(MathHelper.Lerp(c.TpFrom, c.Tp, Ease(p))));
	}

	// The status icons' size and the gap between them (the battle layout's `icon_size` and `icon_gap`).
	private readonly record struct Icons(int Size, int Gap);

	// Status icons in a row: a new one grows in, one that ends blinks out while its step plays. `looks` (the party's
	// slots) leaves out the statuses shown without an icon and puts the rest in priority order.
	private void DrawIcons(IDrawSurface s, Art art, Icons icons, ShownCombatant c, BattleStep? step, int x, int y, Dictionary<string, UiStatusLook>? looks = null)
	{
		int icon = icons.Size;
		int at = 0;
		foreach (string id in Iconed(c.Statuses, looks ?? []))
		{
			int grow = c.Appeared.TryGetValue(id, out (BattleStep Step, long At) appeared) && appeared.Step == step ? (int)(icon * (0.4f + 0.6f * Ease(Since(step, appeared.At)))) : icon;
			s.Image(art.StatusIcon(id), x + at + (icon - grow) / 2, y + (icon - grow) / 2, grow, grow, Color.White);
			at += icon + icons.Gap;
		}
		foreach ((string id, BattleStep _, long leftAt) in c.Leaving.Where(l => l.Step == step && looks?.GetValueOrDefault(l.Id)?.Icon != false))
		{
			float p = Since(step, leftAt);
			if ((int)(p * 8) % 2 == 0)
			{
				s.Image(art.StatusIcon(id), x + at, y, icon, icon, Color.White * (1f - p));
			}
			at += icon + icons.Gap;
		}
	}

	private static IEnumerable<string> Iconed(IEnumerable<string> statuses, Dictionary<string, UiStatusLook> looks) =>
		statuses.Where(id => looks.GetValueOrDefault(id)?.Icon != false).OrderByDescending(id => looks.GetValueOrDefault(id)?.Priority ?? 0);

	private readonly record struct FloatingText(string Text, Color Color, int Size);

	// The floating numbers' text sizes (the battle layout's `number_size`, `number_critical_size`, `number_tp_size` and
	// `number_word_size`, the last for the words).
	private readonly record struct NumberSizes(int Number, int Critical, int Tp, int Word);

	// The floating texts the last frame drew: each target's label, the text and its number style.
	private readonly List<string> _probeNumbers = [];

	// Whether the battle layout shows a result's mark (`effectiveness`, `critical`, `miss`) on a target: the param for its
	// side (`NAME_marks_party`, `NAME_marks_enemy`), else `NAME_marks`, else on.
	private bool Marked(string screen, UiContext ui, string what, int target) =>
		Ui.Param(screen, $"{what}_marks_{(target < (StageInfo()?.PartyCount ?? 0) ? "party" : "enemy")}", ui, Ui.Param(screen, $"{what}_marks", ui, 1)) != 0;

	// A damage event as its number shows it: without its weak, resisted or immune mark, and without the critical hit
	// before it, where the layout turns those marks off.
	private (BattleEvent Event, BattleEvent? Previous) Plain(BattleEvent e, BattleEvent? previous, string screen, UiContext ui)
	{
		if (e.Type != BattleEventType.DamageDealt)
		{
			return (e, previous);
		}
		if (e.Effectiveness != Effectiveness.Normal && !Marked(screen, ui, "effectiveness", e.Target))
		{
			e = e with { Effectiveness = Effectiveness.Normal };
		}
		if (previous is { Type: BattleEventType.Critical } && !Marked(screen, ui, "critical", e.Target))
		{
			previous = null;
		}
		return (e, previous);
	}

	// What floats over a target for one event; `previous` tells a critical hit's damage.
	private FloatingText? Floating(BattleEvent e, BattleEvent? previous, NumberSizes sizes)
	{
		Color weak = new(255, 168, 80), resisted = new(150, 200, 255), status = new(206, 160, 255), felled = new(255, 80, 80);
		string stat = T("ui.battle.stat." + GameSession.NameOf(NameKind.Stat, (int)e.Stat));
		return e.Type switch
		{
			BattleEventType.DamageDealt when previous is { Type: BattleEventType.Critical } => new(T("ui.battle.float.critical", e.Amount), Style.Accent, sizes.Critical),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Immune => new(T("ui.battle.float.immune"), Style.Dim, sizes.Word),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Weak => new(T("ui.battle.float.weak", e.Amount), weak, sizes.Number),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Resisted => new(T("ui.battle.float.resisted_damage", e.Amount), resisted, sizes.Number),
			BattleEventType.DamageDealt => new(e.Amount.ToString(System.Globalization.CultureInfo.InvariantCulture), Style.Text, sizes.Number),
			BattleEventType.Missed => new(T("ui.battle.float.miss"), Style.Dim, sizes.Word),
			// Technique points restored float in the TP bar's color, with their unit, apart from hit points' green numbers.
			BattleEventType.Healed when e.Resource == ResourceKind.Tp && e.Amount == 0 => null,
			BattleEventType.Healed when e.Resource == ResourceKind.Tp => new(T("ui.battle.float.tp", e.Amount), Style.Tp, sizes.Tp),
			BattleEventType.Healed => new($"+{e.Amount}", Style.Good, sizes.Number),
			BattleEventType.Revived => new(T("ui.battle.float.revived", e.Amount), Style.Good, sizes.Word),
			BattleEventType.StatusApplied => new(_s.DisplayName(e.Status), status, sizes.Word),
			BattleEventType.StatusResisted => new(T(e.Effectiveness == Effectiveness.Immune ? "ui.battle.float.immune" : "ui.battle.float.resisted"), Style.Dim, sizes.Word),
			BattleEventType.StatusRemoved when e.StatusEnd != StatusEnd.Defeated => new(T("ui.battle.float.ends", _s.DisplayName(e.Status)), Style.Dim, sizes.Word),
			BattleEventType.StatModified => new(T(e.Amount > 0 ? "ui.battle.float.stat_up" : e.Amount < 0 ? "ui.battle.float.stat_down" : "ui.battle.float.stat_normal", stat),
				e.Amount > 0 ? Style.Good : e.Amount < 0 ? weak : Style.Dim, sizes.Word),
			BattleEventType.ItemHadNoEffect => new(T("ui.battle.float.no_effect"), Style.Dim, sizes.Word),
			BattleEventType.Message when e.Message == "battle.instant_defeat" => new(T("ui.battle.float.felled"), felled, sizes.Number),
			BattleEventType.Message when e.Message.StartsWith("battle.instant_defeat", StringComparison.Ordinal) =>
				new(T(e.Effectiveness == Effectiveness.Immune ? "ui.battle.float.immune" : "ui.battle.float.resisted"), Style.Dim, sizes.Word),
			_ => null,
		};
	}

	private static string BannerOf(BattleStep step) => step.Kind == StepKind.Banner ? step.Text : step.Turn?.Name ?? "";

	// The result screen's lines: experience, money, drops, and each level-up with what it taught; or the party's fall.
	// The result lines of the page showing, when the layout's `result_page_lines` pages them; all of them otherwise.
	private List<UiContext> ResultPage(List<UiContext> lines, string screen, out int page, out int pages)
	{
		int per = Ui.Param(screen, "result_page_lines", UiData(), 0);
		pages = per <= 0 ? 1 : Math.Max(1, (lines.Count + per - 1) / per);
		page = Math.Clamp(_battle.ResultPage, 0, pages - 1);
		return per <= 0 ? lines : [.. lines.Skip(page * per).Take(per)];
	}

	// The result window's lines: without the level-ups when the result sequence shows them in a step of their own.
	private List<UiContext> ShownResultLines(List<UiContext> lines) => LevelUpsApart ? [.. lines.Except(LevelLines(lines))] : lines;

	private static List<UiContext> LevelLines(List<UiContext> lines) => [.. lines.Where(l => (string)l.Get("kind")! is "level_up" or "learned")];

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
