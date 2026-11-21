using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The front layout's own parts: where the enemies and the members seen from behind stand, the status strip of five
// slots along the bottom, the vehicle's plate, the command window beside the member choosing, the keys' line, and the
// probes that read them.
public sealed partial class SampleController
{
	private const int StripTop = 580;
	private const int StripHeight = 106;
	private const int StripSlots = 5;
	private const int SlotWidth = 240;
	private const int SlotGap = 12;
	private const int CommandWidth = 360;
	private const int CommandHeight = 114;
	private const int HintBarHeight = 30;

	private string _probeStrip = "-";
	private string _probeRanks = "-";
	private string _probeFigures = "-";
	private string _probeOverlaps = "-";
	private string _probeMarks = "none";
	// Where the command window's middle goes, from the members' arrangement; -1 to place it by the actor.
	private int _commandX = -1;

	// What the front layout drew that later parts place themselves by: each member's slot, the enemies' labels and the
	// action's name plate.
	private sealed class FrontStage
	{
		public Dictionary<int, Rectangle> Slots { get; } = [];
		public Dictionary<string, Rectangle> Labels { get; } = [];
		public Rectangle Banner;
	}

	private static int SlotX(int slot) => 16 + slot * (SlotWidth + SlotGap);

	private const int BackFigureSize = 144;

	// A member stands over its slot in marching order, head and shoulders above the strip. Enemies stand by formation
	// position over `enemyCount` places in one row across the middle, or from five on in two ranks: the first ones in
	// front, the rest smaller and further back, in the gaps.
	private static (int X, int Foot, int Size) PlaceFront(int index, int partyCount, int position, int enemyCount)
	{
		if (index < partyCount)
		{
			return (SlotX(index) + SlotWidth / 2, StripTop + 8, BackFigureSize);
		}
		int j = position;
		if (enemyCount <= 4)
		{
			int spacing = Math.Min(280, 1040 / enemyCount);
			return (640 + (int)((j - (enemyCount - 1) / 2f) * spacing), 356, enemyCount <= 3 ? 176 : 160);
		}
		int front = (enemyCount + 1) / 2;
		bool back = j >= front;
		int place = back ? 2 * (j - front) + 1 : 2 * j;
		float gap = 1100f / enemyCount;
		return (640 + (int)((place - (enemyCount - 1) / 2f) * gap), back ? 282 : 360, back ? 112 : 136);
	}

	// While the command window is open the members make room for it: the window goes beside the member choosing, toward
	// the screen's middle, and the members on that side step along past it, as close to their slots as the screen allows,
	// so the window hides no one. `actor` is the member choosing, -1 with no window. Returns each member's middle;
	// `windowX` the window's middle, or -1 when there is none.
	private static Dictionary<int, int> FrontMemberCenters(IReadOnlyList<Combatant> all, string vehicle, int actor, out int windowX)
	{
		var members = all.Where(c => c.Side == Side.Party && c.Id != vehicle).Select(c => c.Index).ToList();
		Dictionary<int, int> centers = members.Select((index, slot) => (index, slot)).ToDictionary(m => m.index, m => SlotX(m.slot) + SlotWidth / 2);
		windowX = -1;
		if (!centers.ContainsKey(actor))
		{
			return centers;
		}
		const int gap = 8;
		var row = members.Select(m => (Key: m, Width: BackFigureSize, Pref: centers[m])).ToList();
		int at = row.FindIndex(m => m.Key == actor);
		bool right = centers[actor] <= 640;
		int windowPref = centers[actor] + (right ? 1 : -1) * (BackFigureSize / 2 + gap + CommandWidth / 2);
		row.Insert(right ? at + 1 : at, (-1, CommandWidth, windowPref));
		var x = row.Select(m => m.Pref).ToArray();
		for (int k = 0; k < row.Count; k++)
		{
			int least = k == 0 ? 16 + row[k].Width / 2 : x[k - 1] + (row[k - 1].Width + row[k].Width) / 2 + gap;
			x[k] = Math.Max(x[k], least);
		}
		for (int k = row.Count - 1; k >= 0; k--)
		{
			int most = k == row.Count - 1 ? 1264 - row[k].Width / 2 : x[k + 1] - (row[k + 1].Width + row[k].Width) / 2 - gap;
			x[k] = Math.Min(x[k], most);
		}
		for (int k = 0; k < row.Count; k++)
		{
			if (row[k].Key < 0)
			{
				windowX = x[k];
			}
			else
			{
				centers[row[k].Key] = x[k];
			}
		}
		return centers;
	}

	// The backdrop over the stage, its ground (below 62% of its height, past the generated horizon's shadow) drawn again,
	// taller, under it down to the screen's bottom, so the members stand on ground.
	private static void DrawFrontBackdrop(IDrawSurface s, int backdrop, Point jolt)
	{
		const int height = 488;
		s.Image(backdrop, jolt.X - 8, jolt.Y - 8, s.Width + 16, height, Color.White);
		const int ground = 640;
		int bottom = height - 8;
		s.Clip(0, bottom, s.Width, s.Height - bottom);
		s.Image(backdrop, jolt.X - 8, jolt.Y + bottom - ground * 62 / 100, s.Width + 16, ground, Color.White);
		s.Unclip();
	}

	// The status strip: five equal slots, one per member in marching order, an empty frame for each slot without one; and
	// in a vehicle battle the vehicle's own plate above the strip's right end.
	private FrontStage DrawFrontStrip(IDrawSurface s, Art art, IReadOnlyList<Combatant> all, string vehicle, bool commanding, int target, BattleStep? step, float p,
		float pulse, bool reduced)
	{
		var stage = new FrontStage();
		List<ShownCombatant> shown = _show.All;
		BattleTurn? turn = step?.Turn;
		var members = all.Where(c => c.Side == Side.Party && c.Id != vehicle).ToList();
		var probe = new List<string>();
		(bool Acting, bool Targeted, bool Hit) Marks(int index) => (
			commanding ? index == _battle.Actor : turn?.Actors.Contains(index) == true,
			index == target || (step?.Kind is StepKind.Effect or StepKind.Result && turn?.Targets.Contains(index) == true),
			step is { Kind: StepKind.Result } && step.Events.Any(e => e.Type == BattleEventType.DamageDealt && e.Target == index));
		// A hit slot flashes red as the hit lands; with reduced flash its frame only turns red.
		void Highlight(int x, int y, int w, int h, (bool Acting, bool Targeted, bool Hit) m)
		{
			if (m.Acting)
			{
				s.Fill(x + 4, y + 4, w - 8, h - 8, Style.Row);
			}
			if (m.Hit && !reduced && p < 0.5f)
			{
				s.Fill(x + 4, y + 4, w - 8, h - 8, Style.Bad * (0.55f * (1f - p * 2f)));
			}
			if (m.Acting || m.Targeted || m.Hit)
			{
				Style.Frame(s, x, y, w, h, m.Hit ? Style.Bad : m.Targeted ? Style.Accent * (0.4f + 0.6f * pulse) : Style.Accent);
			}
		}
		for (int slot = 0; slot < StripSlots; slot++)
		{
			int x = SlotX(slot);
			if (slot >= members.Count)
			{
				s.Fill(x + 2, StripTop + 2, SlotWidth - 4, StripHeight - 4, Style.Panel * 0.5f);
				Style.Frame(s, x, StripTop, SlotWidth, StripHeight, Style.PanelEdge * 0.45f);
				probe.Add("-");
				continue;
			}
			Combatant c = members[slot];
			ShownCombatant sc = shown.ElementAtOrDefault(c.Index) ?? new ShownCombatant(c, -1);
			(bool Acting, bool Targeted, bool Hit) m = Marks(c.Index);
			Style.Box(s, x, StripTop, SlotWidth, StripHeight);
			Highlight(x, StripTop, SlotWidth, StripHeight, m);
			int icons = (sc.Statuses.Count + sc.Leaving.Count(l => l.Step == step)) * 22;
			s.Text(Style.Fit(s, c.Name, SlotWidth - 24 - icons, Style.Body), x + 12, StripTop + 9, !sc.Alive ? Style.Dim : m.Acting ? Style.Accent : Style.Text, Style.Body);
			DrawIcons(s, art, sc, step, p, x + SlotWidth - 10 - icons, StripTop + 10);
			(int hp, int tp) = Shown(sc, step, p);
			StripBar(s, x + 12, StripTop + 38, SlotWidth - 24, T("ui.battle.hp"), hp, sc.MaxHp, hp * 4 <= sc.MaxHp ? Style.Bad : Style.Good);
			StripBar(s, x + 12, StripTop + 60, SlotWidth - 24, T("ui.battle.tp"), tp, sc.MaxTp, Style.Tp);
			string state = sc.Alive && NextCommand(c) is { } command ? T("ui.battle.next", command) : MemberState(c, sc);
			if (state.Length > 0)
			{
				s.Text(Style.Fit(s, state, SlotWidth - 24, Style.Small), x + 12, StripTop + 82, sc.Alive ? Style.Dim : Style.Bad, Style.Small);
			}
			stage.Slots[c.Index] = new Rectangle(x, StripTop - 64, SlotWidth, 128);
			probe.Add(c.Name + (m.Acting ? " acting" : "") + (m.Hit ? " hit" : ""));
		}
		if (all.FirstOrDefault(c => c.Side == Side.Party && c.Id == vehicle && vehicle.Length > 0) is { } ride)
		{
			const int w = 320, h = 64;
			int y = StripTop - h - 8, x0 = s.Width - 16 - w;
			ShownCombatant sc = shown.ElementAtOrDefault(ride.Index) ?? new ShownCombatant(ride, -1);
			(bool Acting, bool Targeted, bool Hit) m = Marks(ride.Index);
			Style.Box(s, x0, y, w, h);
			Highlight(x0, y, w, h, m);
			s.Text(Style.Fit(s, ride.Name, w - 24, Style.Small), x0 + 12, y + 8, m.Acting ? Style.Accent : Style.Text, Style.Small);
			(int hp, int tp) = Shown(sc, step, p);
			StripBar(s, x0 + 12, y + 26, w / 2 - 20, T("ui.battle.hp"), hp, sc.MaxHp, hp * 4 <= sc.MaxHp ? Style.Bad : Style.Good);
			StripBar(s, x0 + w / 2 + 4, y + 26, w / 2 - 16, T("ui.battle.tp"), tp, sc.MaxTp, Style.Tp);
			probe.Add("vehicle " + ride.Name + (m.Acting ? " acting" : "") + (m.Hit ? " hit" : ""));
		}
		_probeStrip = string.Join(" | ", probe);
		return stage;
	}

	// A label, the numbers at the right and the bar between them.
	private static void StripBar(IDrawSurface s, int x, int y, int w, string label, int value, int max, Color color)
	{
		string numbers = $"{value}/{max}";
		int nw = s.Measure(numbers, Style.Small);
		s.Text(label, x, y, Style.Dim, Style.Small);
		s.Text(numbers, x + w - nw, y, Style.Text, Style.Small);
		Style.Bar(s, x + 32, y + 6, Math.Max(8, w - 32 - nw - 10), 8, value, max, color);
	}

	// The command window beside the member choosing (on the side of the screen's middle, where the members made room for
	// it), in two columns of four rows over the strip's top edge and under the enemies' labels; the description strip;
	// the keys' line under the strip; the result window; and the probe of what covers the labels or a party figure.
	private void DrawFrontMenus(IDrawSurface s, IReadOnlyList<Combatant> all, BattleInfo? info, Dictionary<int, Rectangle> bodies, int target, FrontStage stage)
	{
		BattleUi b = _battle;
		var covers = new List<(string Name, Rectangle Box)>();
		if (MenuRows(all) is { } menu)
		{
			Rectangle actor = bodies.TryGetValue(b.Actor, out Rectangle body) ? body : stage.Slots.GetValueOrDefault(b.Actor, new Rectangle(s.Width / 2 - 64, StripTop - 64, 128, 128));
			int y = StripTop - CommandHeight - 4;
			int x = _commandX >= 0 ? _commandX - CommandWidth / 2
				: Math.Clamp(actor.Center.X <= s.Width / 2 ? actor.Right + 8 : actor.Left - 8 - CommandWidth, 16, s.Width - 16 - CommandWidth);
			Style.Window(s, x, y, CommandWidth, CommandHeight, MenuTitle(all));
			int colW = (CommandWidth - 32) / 2;
			int first = _cursor / 8 * 8;
			for (int k = 0; k < 8 && first + k < menu.Items.Count; k++)
			{
				int i = first + k;
				Style.ListRow(s, Style.Fit(s, menu.Items[i], colW - 24, Style.Small), x + 22 + k / 4 * colW, y + 18 + k % 4 * 22, colW - 4, i == _cursor,
					menu.Usable is null || menu.Usable[i], Style.Small, 22);
			}
			if (menu.Items.Count > 8)
			{
				string pages = T("ui.battle.pages", first / 8 + 1, (menu.Items.Count + 7) / 8);
				s.Text(pages, x + CommandWidth - 14 - s.Measure(pages, Style.Small), y + 4, Style.Dim, Style.Small);
			}
			covers.Add(("command window", new Rectangle(x, y - 14, CommandWidth, CommandHeight + 14)));
			// A member being targeted may stand behind the window: its pointer stays on top.
			if (target >= 0 && target < all.Count && all[target].Side == Side.Party && bodies.TryGetValue(target, out Rectangle aim))
			{
				s.Image(_art!.Pointer, aim.Center.X - 18, aim.Y - 8, 36, 36, Color.White);
			}
		}
		if (MenuDescription(s, all, target) is { IsEmpty: false } description)
		{
			covers.Add(("description", description));
		}
		string hint = b.Phase switch
		{
			BattlePhase.Playback => T("ui.battle.hint.playback", Hint(GameAction.Confirm), Hint(GameAction.FastForward), Hint(GameAction.TextLog)) +
				(b.Auto ? T("ui.battle.hint.playback_auto", Hint(GameAction.Back)) : ""),
			BattlePhase.Ended => T("ui.battle.hint.continue", Hint(GameAction.Confirm)),
			_ => T("ui.battle.hint.command", Hint(GameAction.Confirm), Hint(GameAction.Back), Hint(GameAction.TextLog), Hint(GameAction.Pause)),
		};
		s.Fill(0, s.Height - HintBarHeight, s.Width, HintBarHeight, Color.Black * 0.6f);
		hint = Style.Fit(s, hint, s.Width - 32, Style.Small);
		s.Text(hint, (s.Width - s.Measure(hint, Style.Small)) / 2, s.Height - HintBarHeight + 6, Style.Dim, Style.Small);
		covers.Add(("keys", new Rectangle(0, s.Height - HintBarHeight, s.Width, HintBarHeight)));
		if (b.Phase == BattlePhase.Ended)
		{
			DrawResult(s, info, StripTop);
		}

		// Every party figure as far as it shows above the strip: the member acting and the others beside it.
		var targets = stage.Labels.Select(l => (Name: l.Key + " label", Box: l.Value)).ToList();
		foreach (int i in bodies.Keys.Where(i => i < all.Count && all[i].Side == Side.Party).Order())
		{
			Rectangle r = bodies[i];
			targets.Add((all[i].Name, new Rectangle(r.X, r.Y, r.Width, Math.Max(0, StripTop - r.Y))));
		}
		var overlaps = covers.SelectMany(c => targets.Where(t => c.Box.Intersects(t.Box)).Select(t => $"{c.Name} over {t.Name}")).ToList();
		overlaps.AddRange(stage.Labels.Where(l => !stage.Banner.IsEmpty && stage.Banner.Intersects(l.Value)).Select(l => $"name plate over {l.Key} label"));
		_probeOverlaps = overlaps.Count == 0 ? "none" : string.Join(", ", overlaps);
		int enemies = Math.Max(1, _show.Slots);
		_probeRanks = enemies <= 4 ? enemies.ToString(System.Globalization.CultureInfo.InvariantCulture) : $"{(enemies + 1) / 2}+{enemies / 2}";
	}

	// The front layout's probes, `-` while the side layout draws the battle.
	private string FrontState(string key) => _stageLayout != BattleLayout.Front && key is "strip" or "ranks" or "overlaps" ? "-" : key switch
	{
		"battlelayout" => BattlePresentation.NameOf(StageLayout()),
		"strip" => _probeStrip,
		"ranks" => _probeRanks,
		"figures" => _probeFigures,
		"overlaps" => _probeOverlaps,
		"marks" => _probeMarks,
		_ => "?",
	};
}
