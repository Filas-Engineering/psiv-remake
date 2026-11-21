using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The front layout's own parts: the members seen from behind making room for the command window, the backdrop's ground
// under the status strip, and the probes that read the strip, the ranks and what the windows cover.
public sealed partial class SampleController
{
	// The status strip's slots the `strip` probe reports, one per member of the largest party.
	private const int StripSlots = 5;
	private const int HintBarHeight = 30;

	private string _probeStrip = "-";
	private string _probeRanks = "-";
	private string _probeFigures = "-";
	private string _probeOverlaps = "-";
	private string _probeMarks = "none";
	private string _probeFlying = "none";

	// While the command window is open the members make room for it: the window goes beside the member choosing, toward
	// the screen's middle, and the members on that side step along past it, as close to their places as the screen allows,
	// so the window hides no one. `actor` is the member choosing, -1 with no window; `windowWidth` the laid-out window's.
	// Returns each member's middle; `windowX` the window's middle, or -1 when there is none.
	private static Dictionary<int, int> FrontMemberCenters(IReadOnlyList<Combatant> all, string vehicle, int actor, int windowWidth, StageRules rules, int screenWidth,
		out int windowX)
	{
		var members = all.Where(c => c.Side == Side.Party && c.Id != vehicle).Select(c => c.Index).ToList();
		Dictionary<int, int> centers = members.Select((index, slot) => (index, slot)).ToDictionary(m => m.index, m => rules.PartyX + m.slot * rules.PartyDx);
		windowX = -1;
		if (!centers.ContainsKey(actor))
		{
			return centers;
		}
		int gap = rules.StepGap;
		var row = members.Select(m => (Key: m, Width: rules.PartySize, Pref: centers[m])).ToList();
		int at = row.FindIndex(m => m.Key == actor);
		bool right = centers[actor] <= screenWidth / 2;
		int windowPref = centers[actor] + (right ? 1 : -1) * (rules.PartySize / 2 + gap + windowWidth / 2);
		row.Insert(right ? at + 1 : at, (-1, windowWidth, windowPref));
		var x = row.Select(m => m.Pref).ToArray();
		for (int k = 0; k < row.Count; k++)
		{
			int least = k == 0 ? rules.StepMargin + row[k].Width / 2 : x[k - 1] + (row[k - 1].Width + row[k].Width) / 2 + gap;
			x[k] = Math.Max(x[k], least);
		}
		for (int k = row.Count - 1; k >= 0; k--)
		{
			int most = k == row.Count - 1 ? screenWidth - rules.StepMargin - row[k].Width / 2 : x[k + 1] - (row[k + 1].Width + row[k].Width) / 2 - gap;
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
		// A logical screen no taller than the backdrop has no ground left to draw under it.
		if (s.Height <= bottom)
		{
			return;
		}
		s.Clip(0, bottom, s.Width, s.Height - bottom);
		s.Image(backdrop, jolt.X - 8, jolt.Y + bottom - ground * 62 / 100, s.Width + 16, ground, Color.White);
		s.Unclip();
	}

	// The strip's slots in marching order, `-` for one without a member, and the vehicle's plate.
	private static string StripProbe(List<UiContext> members, List<UiContext> ride)
	{
		static string Marked(UiContext m) => $"{m.Get("name")}{((bool)m.Get("acting")! ? " acting" : "")}{((bool)m.Get("hit")! ? " hit" : "")}";
		return string.Join(" | ", Enumerable.Range(0, StripSlots).Select(slot => slot < members.Count ? Marked(members[slot]) : "-").Concat(ride.Select(m => "vehicle " + Marked(m))));
	}

	// What covers the enemies' labels or a party figure as far as it shows above the strip: the command window with its
	// title plate, the description, the keys' line and the action's name plate, as the layout placed them.
	private void FrontProbes(IDrawSurface s, IReadOnlyList<Combatant> all, Dictionary<int, Rectangle> bodies, Dictionary<string, Rectangle> labels, string screen, UiContext ui)
	{
		var covers = new List<(string Name, Rectangle Box)>();
		if (Ui.WindowRect(screen, "command", s, ui) is { } command)
		{
			// The window's title plate stands 14 above it.
			covers.Add(("command window", new Rectangle(command.X, command.Y - 14, command.Width, command.Height + 14)));
		}
		if (Ui.WindowRect(screen, "description", s, ui) is { } description)
		{
			covers.Add(("description", description));
		}
		if (Ui.WindowRect(screen, "keys", s, ui) is { } keys)
		{
			covers.Add(("keys", keys));
		}
		int stripTop = Ui.WindowRect(screen, "strip", s, ui)?.Y ?? s.Height;
		var targets = labels.Select(l => (Name: l.Key + " label", Box: l.Value)).ToList();
		foreach (int i in bodies.Keys.Where(i => i < all.Count && all[i].Side == Side.Party).Order())
		{
			Rectangle r = bodies[i];
			targets.Add((all[i].Name, new Rectangle(r.X, r.Y, r.Width, Math.Max(0, stripTop - r.Y))));
		}
		if (Ui.WindowRect(screen, "name_plate", s, ui) is { } plate)
		{
			covers.Add(("name plate", plate));
		}
		var overlaps = covers.SelectMany(c => targets.Where(t => c.Box.Intersects(t.Box)).Select(t => $"{c.Name} over {t.Name}")).ToList();
		_probeOverlaps = overlaps.Count == 0 ? "none" : string.Join(", ", overlaps);
		int enemies = Math.Max(1, _show.Slots);
		_probeRanks = enemies <= Ui.Param(screen, "enemy_row_max", ui, 0) ? enemies.ToString(System.Globalization.CultureInfo.InvariantCulture) : $"{(enemies + 1) / 2}+{enemies / 2}";
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
		"flying" => _probeFlying,
		_ => "?",
	};
}
