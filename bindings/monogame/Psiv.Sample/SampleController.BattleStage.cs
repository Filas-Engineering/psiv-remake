using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The battle drawn: the backdrop, the enemies on the left and the party on the right as figures, the playback's steps
// on them (stepping forward, banners, animations, effects, hit reactions, floating numbers, bars, status icons, falls),
// the party's panels and the command menu, the log when it is shown, and the result screen.
public sealed partial class SampleController
{
	private const int BattlePanelTop = 420;
	private const int BattleMenuWidth = 300;
	private const int BattleLogLines = 5;
	private const int EffectScale = 3;

	private int _probeShake;
	private bool _probeFlash;
	private string _probeVictory = "-";

	// Where a combatant stands: its center x, the y of its feet and its size. Enemies fill the left in one row, or in
	// two ranks from five on, the first ones in front; the party stands in a diagonal on the right.
	private static (int X, int Foot, int Size) Place(int index, int partyCount, int enemyCount)
	{
		if (index < partyCount)
		{
			return (912 + index * 68, 236 + index * 40, 80);
		}
		int j = index - partyCount;
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
		IReadOnlyList<Combatant> all = _s.Combatants();
		BattleInfo? info = _s.BattleInfo();
		int partyCount = info?.PartyCount ?? all.Count(c => c.Side == Side.Party);
		int enemyCount = all.Count - partyCount;
		bool commanding = b.Phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro;
		int target = b.Phase == BattlePhase.Target && b.Targets.Count > 0 ? b.Targets[Math.Clamp(_cursor, 0, b.Targets.Count - 1)] : -1;
		float pulse = 0.5f + 0.5f * MathF.Sin(_clockMs / 140f);
		BattleStep? step = b.Phase == BattlePhase.Playback ? _timeline.Current : null;
		float p = _timeline.Progress;
		BattleTurn? turn = step?.Turn;
		bool reduced = _s.Profile().ReducedFlash;
		List<ShownCombatant> shown = _show.All;

		// The screen shakes with a heavy effect and with a critical hit, easing off over the step; reduced flash damps it.
		float shake = step switch
		{
			{ Kind: StepKind.Effect, Turn.Look.Shake: > 0 } => step.Turn!.Look!.Shake * (1f - p),
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
		s.Image(art.Backdrop(field?.AreaKind ?? AreaKind.Field, field?.AreaId ?? "", info?.Encounter ?? ""), jolt.X - 8, jolt.Y - 8, s.Width + 16, BattlePanelTop + 8, Color.White);

		// Each figure's picture, offset and tint for the step playing, drawn back to front.
		var bodies = new Dictionary<int, Rectangle>();
		var order = Enumerable.Range(0, Math.Min(all.Count, shown.Count)).OrderBy(i => Place(i, partyCount, enemyCount).Foot).ToList();
		string vehicle = info?.Vehicle ?? "";
		foreach (int i in order)
		{
			Combatant c = all[i];
			ShownCombatant sc = shown[i];
			bool enemy = c.Side == Side.Enemy;
			// In a vehicle battle the members ride inside it: only the vehicle stands on the field, at the party's middle.
			if (vehicle.Length > 0 && !enemy && c.Id != vehicle)
			{
				continue;
			}
			(int x, int foot, int size) = vehicle.Length > 0 && c.Id == vehicle ? (1012, 336, 128) : Place(i, partyCount, enemyCount);
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
			int toward = enemy ? 1 : -1;
			int reach = enemy ? 24 : 44;
			float dx = 0;
			string animation = "idle";
			Color tint = Color.White;
			if (turn is not null && turn.Actors.Contains(i))
			{
				dx = step!.Kind switch
				{
					StepKind.Forward => toward * reach * Ease(p),
					StepKind.Back => toward * reach * (1f - Ease(p)),
					_ => toward * reach,
				};
				if (step.Kind == StepKind.Animate && turn.Look is { } look)
				{
					animation = look.Animation;
					if (look.Animation == "attack")
					{
						dx += toward * 16 * MathF.Sin(MathF.PI * p);
					}
					else if (look.Animation is "cast" or "item")
					{
						string element = look.Effect.Contains('_') ? look.Effect[(look.Effect.IndexOf('_') + 1)..] : "none";
						tint = Color.Lerp(Color.White, Art.ElementColor(element), 0.5f * MathF.Sin(MathF.PI * p));
					}
				}
			}
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
			Figure figure = !enemy && c.Id == vehicle ? art.VehicleFigure(c.Id) : art.BattleFigure(c.Id, enemy);
			FrameRun run = figure.Animations.GetValueOrDefault(animation) ?? figure.Idle;
			bool idle = run == figure.Idle;
			int image = figure.Images[run.FrameAt(idle ? _clockMs + i * 137 : _timeline.ElapsedMs, loop: idle)];
			int left = x - size / 2 + (int)MathF.Round(dx) + jolt.X, top = foot - size + jolt.Y;
			Rectangle body = art.Stand(image, left, top, size);
			bodies[i] = body;
			s.Image(art.Shadow, left, top + size / 6, size, size, Color.White * alpha);
			s.Image(image, body.X, body.Y, body.Width, body.Height, tint * alpha);
		}

		// The effect on each target, centered on its body.
		if (step is { Kind: StepKind.Effect, Turn.Look: { } fx } && art.Effect(fx.Effect) is { } effect)
		{
			int frame = effect.Images[effect.Idle.FrameAt(_timeline.ElapsedMs, loop: false)];
			(int ew, int eh) = art.SizeOf(frame);
			foreach (int t in turn!.Targets.Where(bodies.ContainsKey))
			{
				Rectangle r = bodies[t];
				s.Image(frame, r.Center.X - ew * EffectScale / 2, r.Center.Y - eh * EffectScale / 2, ew * EffectScale, eh * EffectScale, Color.White);
			}
		}

		// Labels, statuses and HP bars under the enemies, as before: one per enemy, its group's name and number.
		foreach (int i in order.Where(i => all[i].Side == Side.Enemy && (shown[i].Alive || (shown[i].AliveStep is not null && shown[i].AliveStep == step))))
		{
			(int x, int foot, _) = Place(i, partyCount, enemyCount);
			bool targeted = i == target;
			string label = Style.Fit(s, all[i].Label, 190, Style.Small);
			int lw = s.Measure(label, Style.Small);
			s.Fill(x - lw / 2 - 8, foot + 6, lw + 16, 24, Color.Black * 0.6f);
			s.Text(label, x - lw / 2, foot + 9, targeted ? Style.Accent : Style.Text, Style.Small);
			Style.Bar(s, x - 50, foot + 34, 100, 6, Shown(shown[i], step, p).Hp, shown[i].MaxHp, Style.Bad);
			DrawIcons(s, art, shown[i], step, p, x - 50, foot + 44);
		}

		// The target cursor over the figure being chosen, and over the retarget step's new target as it moves there.
		if (target >= 0 && bodies.TryGetValue(target, out Rectangle aim))
		{
			s.Image(art.Pointer, aim.Center.X - 18, aim.Y - 40 + (int)(5 * pulse), 36, 36, Color.White);
		}
		if (step is { Kind: StepKind.Retarget } && bodies.TryGetValue(step.To, out Rectangle to))
		{
			Rectangle from = bodies.GetValueOrDefault(step.From, to);
			float e = Ease(p);
			s.Image(art.Pointer, (int)MathHelper.Lerp(from.Center.X, to.Center.X, e) - 18, (int)MathHelper.Lerp(from.Y, to.Y, e) - 40, 36, 36, Color.White);
		}

		// Floating numbers and words over each target: damage, critical, misses, weak and resisted hits, healing, statuses.
		if (step is { Kind: StepKind.Result or StepKind.RoundEnd })
		{
			var stacked = new Dictionary<int, int>();
			BattleEvent? previous = null;
			foreach (BattleEvent e in step.Events)
			{
				int t = e.Type == BattleEventType.ItemHadNoEffect ? e.Actor : e.Target;
				if (!bodies.TryGetValue(t, out Rectangle r) || Floating(e, previous) is not { } floating)
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
				int fy = r.Y - 26 - rise - n * (floating.Size + 6);
				s.Text(floating.Text, r.Center.X - w / 2 + 2, fy + 2, Color.Black * (0.8f * fade), floating.Size);
				s.Text(floating.Text, r.Center.X - w / 2, fy, floating.Color * fade, floating.Size);
			}
		}

		// A full-screen flash at the start of a big effect, left out with reduced flash.
		_probeFlash = false;
		if (step is { Kind: StepKind.Effect, Turn.Look.Flash: true } && !reduced && p < 0.25f)
		{
			_probeFlash = true;
			s.Fill(0, 0, s.Width, BattlePanelTop, Color.White * (0.55f * (1f - p * 4f)));
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
		DrawBanner(s, step, p, b.LogShown ? BattleLogLines * 22 + 48 : 20);

		// The party, one column each: portrait, name, status icons and names, HP and TP bars, the command already chosen.
		var party = all.Where(c => c.Side == Side.Party).ToList();
		int panelW = s.Width - 32 - BattleMenuWidth - 12;
		int panelH = s.Height - BattlePanelTop - 16;
		Style.Box(s, 16, BattlePanelTop, panelW, panelH);
		int colW = (panelW - 16) / Math.Max(1, party.Count);
		for (int i = 0; i < party.Count; i++)
		{
			Combatant c = party[i];
			ShownCombatant sc = shown.ElementAtOrDefault(c.Index) ?? new ShownCombatant(c);
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
			int portrait = c.Id == (info?.Vehicle ?? "") ? art.VehicleFrame(c.Id, Direction.South, 0) : art.Portrait("", c.Id);
			s.Image(portrait, cx + 8, cy + 8, 64, 64, sc.Alive ? Color.White : new Color(80, 80, 92));
			s.Text(Style.Fit(s, c.Name, cw - 90), cx + 80, cy + 12, !sc.Alive ? Style.Dim : acting ? Style.Accent : Style.Text);
			string state = !sc.Alive ? "Fallen" : ((c.Defending ? "Defending " : "") + string.Join(" ", sc.Statuses.Select(_s.DisplayName))).Trim();
			DrawIcons(s, art, sc, step, p, cx + 80, cy + 38);
			if (state.Length > 0)
			{
				s.Text(Style.Fit(s, state, cw - 90, Style.Small), cx + 80, cy + 60, sc.Alive ? Style.Dim : Style.Bad, Style.Small);
			}
			(int hp, int tp) = Shown(sc, step, p);
			int bw = cw - 24;
			Widgets.ValueBar(s, cx + 12, cy + 86, bw, "HP", hp, sc.MaxHp, hp * 4 <= sc.MaxHp ? Style.Bad : Style.Good);
			Widgets.ValueBar(s, cx + 12, cy + 130, bw, "TP", tp, sc.MaxTp, Style.Tp);
			if (c.CommandKind != CommandKind.None)
			{
				string command = c.CommandId.Length > 0 ? _s.DisplayName(c.CommandId) : GameSession.NameOf(NameKind.CommandKind, (int)c.CommandKind);
				s.Text(Style.Fit(s, "Next: " + command, bw, Style.Small), cx + 12, cy + 180, Style.Dim, Style.Small);
			}
		}

		int mx = s.Width - 16 - BattleMenuWidth;
		string actor = b.Actor < all.Count ? all[b.Actor].Name : "";
		string title = b.Phase switch
		{
			BattlePhase.Command => actor,
			BattlePhase.Option => b.MenuChoice,
			BattlePhase.Target => "Target",
			BattlePhase.Macro => "Macro",
			BattlePhase.Ended => "Battle over",
			_ => "Battle",
		};
		Style.Window(s, mx, BattlePanelTop, BattleMenuWidth, panelH, title);
		void Menu(IReadOnlyList<string> items, IReadOnlyList<bool>? usable = null)
		{
			int rows = (panelH - 44) / 28;
			int first = Math.Clamp(_cursor - rows + 1, 0, Math.Max(0, items.Count - rows));
			for (int i = first; i < Math.Min(items.Count, first + rows); i++)
			{
				Style.ListRow(s, Style.Fit(s, items[i], BattleMenuWidth - 70, Style.Small), mx + 24, BattlePanelTop + 30 + (i - first) * 28,
					BattleMenuWidth - 36, i == _cursor, usable is null || usable[i], Style.Small);
			}
		}
		switch (b.Phase)
		{
		case BattlePhase.Command:
			Menu(CommandMenu);
			break;
		case BattlePhase.Option:
			Menu(b.Options.Select(o => o.Name + (o.Cost > 0 ? $" ({o.Cost} TP)" : "") + (o.Uses >= 0 ? $" x{o.Uses}" : "")).ToList(), b.Options.Select(o => o.Usable).ToList());
			if (b.Options.Count > 0)
			{
				BattleOption option = b.Options[Math.Clamp(_cursor, 0, b.Options.Count - 1)];
				BattleDescription(s, _s.Description(option.Command == CommandKind.Item ? DescriptionKind.Item : DescriptionKind.Ability, option.Id));
			}
			break;
		case BattlePhase.Target:
			Menu(b.Targets.Select(t => t < all.Count ? (all[t].Side == Side.Party ? all[t].Name : all[t].Label) : "?").ToList());
			if (target >= 0 && target < all.Count && all[target].Side == Side.Enemy)
			{
				BattleDescription(s, _s.Description(DescriptionKind.Enemy, all[target].Id));
			}
			break;
		case BattlePhase.Macro:
			Menu(_s.Macros());
			break;
		case BattlePhase.Playback:
			s.Text(b.Auto ? "Auto-battle" : "...", mx + 24, BattlePanelTop + 34, Style.Text);
			if (b.Auto)
			{
				s.Text(Style.Fit(s, $"{Hint(GameAction.Back)} stops after this round", BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + 64, Style.Dim, Style.Small);
			}
			s.Text(Style.Fit(s, $"{Hint(GameAction.Confirm)} hurries", BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + panelH - 82, Style.Dim, Style.Small);
			s.Text(Style.Fit(s, $"{Hint(GameAction.FastForward)} (hold) skips the round", BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + panelH - 58, Style.Dim, Style.Small);
			s.Text(Style.Fit(s, $"{Hint(GameAction.TextLog)} log", BattleMenuWidth - 48, Style.Small), mx + 24, BattlePanelTop + panelH - 34, Style.Dim, Style.Small);
			break;
		case BattlePhase.Ended:
			s.Text(BattleTimeline.OutcomeText(info?.Outcome ?? Outcome.Ongoing), mx + 24, BattlePanelTop + 34, Style.Accent, Style.Title);
			s.Text($"{Hint(GameAction.Confirm)}: continue", mx + 24, BattlePanelTop + 84, Style.Text);
			DrawResult(s, info);
			break;
		}
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
		Color weak = new(255, 168, 80), resisted = new(150, 200, 255), status = new(206, 160, 255);
		return e.Type switch
		{
			BattleEventType.DamageDealt when previous is { Type: BattleEventType.Critical } => new($"{e.Amount}!", Style.Accent, NumberSize + 8),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Immune => new("Immune", Style.Dim, Style.Body),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Weak => new($"{e.Amount} Weak", weak, NumberSize),
			BattleEventType.DamageDealt when e.Effectiveness == Effectiveness.Resisted => new($"{e.Amount} Resisted", resisted, NumberSize),
			BattleEventType.DamageDealt => new(e.Amount.ToString(System.Globalization.CultureInfo.InvariantCulture), Style.Text, NumberSize),
			BattleEventType.Missed => new("Miss", Style.Dim, Style.Body),
			BattleEventType.Healed => new($"+{e.Amount}", Style.Good, NumberSize),
			BattleEventType.Revived => new($"Revived +{e.Amount}", Style.Good, Style.Body),
			BattleEventType.StatusApplied => new(_s.DisplayName(e.Status), status, Style.Body),
			BattleEventType.StatusResisted => new(e.Effectiveness == Effectiveness.Immune ? "Immune" : "Resisted", Style.Dim, Style.Body),
			BattleEventType.StatusRemoved when e.StatusEnd != StatusEnd.Defeated => new(_s.DisplayName(e.Status) + " ends", Style.Dim, Style.Body),
			BattleEventType.StatModified => new($"{GameSession.NameOf(NameKind.Stat, (int)e.Stat)} {(e.Amount > 0 ? "up" : "down")}", e.Amount > 0 ? Style.Good : weak, Style.Body),
			BattleEventType.ItemHadNoEffect => new("No effect", Style.Dim, Style.Body),
			_ => null,
		};
	}

	// The step's words: the action's name over its whole turn, and the opening, the round, a cancel, a notice or the
	// outcome on their own, fading in and out.
	private void DrawBanner(IDrawSurface s, BattleStep? step, float p, int y)
	{
		if (step is null)
		{
			return;
		}
		bool turnName = step.Kind is StepKind.Banner or StepKind.Animate or StepKind.Effect or StepKind.Result or StepKind.Retarget;
		string text = turnName ? BannerOf(step) : step.Text;
		if (text.Length == 0 || step.Kind is StepKind.Forward or StepKind.Back)
		{
			return;
		}
		float alpha = turnName ? 1f : Math.Clamp(Math.Min(p * 6f, (1f - p) * 6f), 0f, 1f);
		int size = step.Kind is StepKind.Outcome or StepKind.Opening or StepKind.Round ? Style.Title : Style.Body;
		text = Style.Fit(s, text, s.Width - 160, size);
		int w = s.Measure(text, size) + 48;
		int h = size + 22;
		int x = (s.Width - w) / 2;
		Style.Box(s, x, y, w, h, 0.95f * alpha);
		s.Text(text, x + 24, y + 10, (step.Kind == StepKind.Cancel ? Style.Bad : Style.Accent) * alpha, size);
	}

	private static string BannerOf(BattleStep step) => step.Kind == StepKind.Banner ? step.Text : step.Turn?.Name ?? "";

	// The result screen over the battlefield: experience, money, drops, and each level-up with what it taught.
	private void DrawResult(IDrawSurface s, BattleInfo? info)
	{
		BattleResult? result = _s.BattleResult();
		var lines = new List<(string Text, Color Color)>();
		if (result is not null && result.Outcome is Outcome.Victory or Outcome.ScriptedEnd)
		{
			lines.Add(($"Experience {result.Experience}", Style.Text));
			lines.Add(($"Money {result.Money}", Style.Text));
			if (result.Drops.Count > 0)
			{
				lines.Add(("Found " + string.Join(", ", result.Drops.Select(d => d.Value > 1 ? $"{_s.DisplayName(d.Key)} x{d.Value}" : _s.DisplayName(d.Key))), Style.Text));
			}
			foreach (LevelUp up in result.LevelUps)
			{
				lines.Add(($"{up.Name} reached level {up.Level}!", Style.Accent));
				if (up.Learned.Count > 0)
				{
					lines.Add(("  Learned " + string.Join(", ", up.Learned.Select(_s.DisplayName)), Style.Good));
				}
			}
		}
		else if (info?.GameOver == true)
		{
			lines.Add(("The party has fallen.", Style.Bad));
		}
		string heading = BattleTimeline.OutcomeText(info?.Outcome ?? Outcome.Ongoing);
		_probeVictory = string.Join(" / ", lines.Select(l => l.Text.Trim()).Prepend(heading));
		int w = 640, h = 84 + lines.Count * 30;
		int x = (s.Width - w) / 2, y = Math.Max(24, (BattlePanelTop - h) / 2);
		Style.Window(s, x, y, w, h, "Result");
		s.Text(heading, x + (w - s.Measure(heading, Style.Title)) / 2, y + 22, Style.Accent, Style.Title);
		for (int i = 0; i < lines.Count; i++)
		{
			s.Text(Style.Fit(s, lines[i].Text, w - 64), x + 32, y + 70 + i * 30, lines[i].Color);
		}
	}

	// A strip at the top of the battlefield, under the log when it shows, with the highlighted option's or targeted
	// enemy's description, clear of the figures and their labels.
	private void BattleDescription(IDrawSurface s, string? text)
	{
		int h = Px(22) * 2 + 16;
		int y = _battle.LogShown ? BattleLogLines * 22 + 48 : 16;
		Style.Box(s, 16, y, s.Width - 32, h, 0.9f);
		DrawDescription(s, text, 32, y + 8, s.Width - 64);
	}
}
