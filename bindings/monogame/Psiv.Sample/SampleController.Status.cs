using System.Globalization;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The menu's Status page: pick a member, then their stats or their abilities; field abilities can be used from here.
public sealed partial class SampleController
{
	// 0: stats, 1: abilities.
	private int _statusPage;

	private static string Signed(int value) => value.ToString("+0;-0;0", CultureInfo.InvariantCulture);

	private static string BonusText(StatBonus b)
	{
		var parts = new List<string>();
		void Add(string name, int value)
		{
			if (value != 0)
			{
				parts.Add($"{name} {Signed(value)}");
			}
		}
		Add("HP", b.MaxHp);
		Add("TP", b.MaxTp);
		Add("Str", b.Strength);
		Add("Men", b.Mental);
		Add("Agi", b.Agility);
		Add("Dex", b.Dexterity);
		Add("Def", b.Defense);
		Add("Crit", b.Critical);
		Add("Eva", b.Evasion);
		return parts.Count == 0 ? "no bonus" : string.Join(", ", parts);
	}

	private static string Words(string name) => name.Replace('_', ' ');

	private void UpdateStatus(ActionInput input)
	{
		List<Member> lineup = LineupMembers();
		if (_pickMember is null)
		{
			_list.Move(input, lineup.Count);
			if (input.Confirm && lineup.Count > 0)
			{
				_pickMember = lineup[_list.Cursor].Id;
				_statusPage = 0;
				_sub.Reset();
			}
			return;
		}
		if (_pickAction is not null)
		{
			_third.Move(input, lineup.Count);
			if (input.Back)
			{
				_pickAction = null;
			}
			else if (input.Confirm && lineup.Count > 0)
			{
				Report(_s.UseAbility(_pickMember, _pickAction, lineup[_third.Cursor].Id));
				_pickAction = null;
			}
			return;
		}
		if (input.Back)
		{
			_pickMember = null;
			return;
		}
		if (input.Pressed(GameAction.PagePrev) || input.Pressed(GameAction.PageNext))
		{
			_statusPage = 1 - _statusPage;
			_sub.Reset();
			return;
		}
		if (_statusPage == 0)
		{
			_sub.MoveView(input, StatusRows(_pickMember).Count);
			return;
		}
		IReadOnlyList<AbilityInfo> abilities = _s.MemberAbilities(_pickMember);
		_sub.Move(input, abilities.Count);
		if (input.Confirm && abilities.Count > 0)
		{
			AbilityInfo ability = abilities[_sub.Cursor];
			if (ability.Field && ability.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
			{
				_pickAction = ability.Id;
				_third.Reset();
			}
			else
			{
				Report(_s.UseAbility(_pickMember, ability.Id));
			}
		}
	}

	// The stats page in two columns of (label, value) rows; a row with no value is a heading.
	private (List<(string Label, string Value)> Left, List<(string Label, string Value)> Right) StatusColumns(string memberId)
	{
		Member m = _s.Party().First(x => x.Id == memberId);
		MemberDetail? d = _s.MemberDetail(memberId);
		var left = new List<(string, string)>();
		var right = new List<(string, string)>();
		if (d is null)
		{
			return (left, right);
		}
		string Stat(int total, Func<StatBonus, int> bonus)
		{
			int sum = d.EquipmentBonus.Sum(bonus);
			return total.ToString(CultureInfo.InvariantCulture) + (sum != 0 ? $" ({Signed(sum)})" : "");
		}
		left.Add(("Stats", ""));
		left.Add(("Strength", Stat(m.Strength, b => b.Strength)));
		left.Add(("Mental", Stat(m.Mental, b => b.Mental)));
		left.Add(("Agility", Stat(m.Agility, b => b.Agility)));
		left.Add(("Dexterity", Stat(m.Dexterity, b => b.Dexterity)));
		left.Add(("Defense", Stat(m.Defense, b => b.Defense)));
		left.Add(("Critical", Stat(d.Critical, b => b.Critical)));
		left.Add(("Evasion", Stat(d.Evasion, b => b.Evasion)));
		left.Add(("Attack element", d.AttackElement == Element.None ? "none" : Words(GameSession.NameOf(NameKind.Element, (int)d.AttackElement))));
		left.Add(("Element affinities", ""));
		foreach ((Element element, int percent) in d.Affinities)
		{
			left.Add((Words(GameSession.NameOf(NameKind.Element, (int)element)), $"{percent}%"));
		}
		left.Add(("Status resistances", ""));
		if (d.StatusResists.Count == 0)
		{
			left.Add(("none", "-"));
		}
		foreach (StatusResist r in d.StatusResists)
		{
			left.Add((r.Name, $"{r.Percent}%"));
		}
		right.Add(("Equipment", ""));
		for (int slot = 0; slot < m.Equipment.Count; slot++)
		{
			string piece = m.EquipmentNames[slot].Length > 0 ? m.EquipmentNames[slot] : "-";
			right.Add((Words(GameSession.NameOf(NameKind.EquipSlot, slot)), piece));
			if (m.Equipment[slot].Length > 0 && slot < d.EquipmentBonus.Count)
			{
				right.Add(("", BonusText(d.EquipmentBonus[slot])));
			}
		}
		right.Add(("Statuses", ""));
		if (m.Statuses.Count == 0)
		{
			right.Add(("none", "-"));
		}
		foreach (ActiveStatus st in m.Statuses)
		{
			right.Add((st.Name, st.Remaining > 0 ? $"{st.Remaining} left" : "-"));
		}
		return (left, right);
	}

	private List<(string Label, string Value)> StatusRows(string memberId)
	{
		var (left, right) = StatusColumns(memberId);
		return left.Count >= right.Count ? left : right;
	}

	private void DrawStatus(IDrawSurface s, Art art, int x, int y, int w, int h)
	{
		List<Member> lineup = LineupMembers();
		if (_pickMember is null)
		{
			DrawItems(s, _list, x, y, w, h, lineup.Select(m => $"{m.Name}  Lv{m.Level}  HP {m.Hp}/{m.MaxHp}  TP {m.Tp}/{m.MaxTp}  EXP {m.Experience}" +
				string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList());
			return;
		}
		Member? member = lineup.FirstOrDefault(m => m.Id == _pickMember);
		MemberDetail? detail = _s.MemberDetail(_pickMember);
		if (member is null || detail is null)
		{
			return;
		}
		// The header: portrait, name and level, experience, HP and TP.
		s.Fill(x, y, 84, 84, Style.Track);
		s.Image(art.ActorFrame(Art.LookFor(member.Id), Direction.South, 0), x + 2, y + 2, 80, 80, Color.White);
		int hx = x + 104;
		s.Text($"{member.Name}   Lv {member.Level}", hx, y, Style.Accent);
		s.Text($"EXP {member.Experience}   next level in {detail.ExperienceToNext}", hx, y + Px(30), Style.Dim, Style.Small);
		int barW = Math.Min(300, (w - 104) / 2 - 30);
		int barsY = y + Px(56);
		Widgets.ValueBar(s, hx, barsY, barW, "HP", member.Hp, member.MaxHp, member.Hp * 4 <= member.MaxHp ? Style.Bad : Style.Good, Px(20), 8);
		Widgets.ValueBar(s, hx + barW + 40, barsY, barW, "TP", member.Tp, member.MaxTp, Style.Tp, Px(20), 8);
		Widgets.Tabs(s, x + w - Px(300), y, Px(300), ["Stats", "Abilities"], _statusPage, Px);
		int top = Math.Max(y + 96, barsY + Px(20) + 18);
		int bodyH = y + h - top;
		if (_statusPage == 0)
		{
			var (left, right) = StatusColumns(member.Id);
			int colW = w / 2 - 20;
			int rowH = Px(26);
			void Row(List<(string Label, string Value)> rows, int cx, int i, int ry)
			{
				if (i >= rows.Count)
				{
					return;
				}
				(string label, string value) = rows[i];
				if (value.Length == 0)
				{
					s.Text(label, cx, ry, Style.Accent, Style.Small);
					return;
				}
				int valueW = s.Measure(value, Style.Small);
				string shownLabel = Style.Fit(s, label, colW - Math.Min(valueW, colW / 2) - 40, Style.Small);
				s.Text(shownLabel, cx + 14, ry, Style.Text, Style.Small);
				string shownValue = Style.Fit(s, value, label.Length == 0 ? colW - 40 : colW - s.Measure(shownLabel, Style.Small) - 50, Style.Small);
				s.Text(shownValue, label.Length == 0 ? cx + 28 : cx + colW - 20 - s.Measure(shownValue, Style.Small), ry, label.Length == 0 ? Style.Dim : Style.Text, Style.Small);
			}
			_sub.DrawView(s, x, top, w, bodyH, rowH, Math.Max(left.Count, right.Count), (i, ry, _) =>
			{
				Row(left, x, i, ry);
				Row(right, x + colW + 40, i, ry);
			});
			_probeTop = _sub.Top;
			return;
		}
		IReadOnlyList<AbilityInfo> abilities = _s.MemberAbilities(member.Id);
		if (abilities.Count == 0)
		{
			s.Text("No abilities.", x + 14, top, Style.Dim);
			return;
		}
		int listW = _pickAction is null ? w : w * 3 / 5;
		int kindX = x + listW * 45 / 100;
		int costX = x + listW * 62 / 100;
		int usesX = x + listW * 76 / 100;
		s.Text("Kind", kindX, top, Style.Dim, Style.Small);
		s.Text("Cost", costX, top, Style.Dim, Style.Small);
		s.Text("Uses", usesX, top, Style.Dim, Style.Small);
		int listTop = top + Px(28);
		int descH = Px(56);
		_sub.Draw(s, x, listTop, listW, y + h - listTop - descH, Px(30), abilities.Count, (i, ry, on) =>
		{
			AbilityInfo a = abilities[i];
			Style.ListRow(s, Style.Fit(s, a.Name + (a.Field ? "  *" : ""), kindX - x - 30), x, ry, listW - 14, on, a.Field, height: Px(28));
			s.Text(Words(GameSession.NameOf(NameKind.AbilityKind, (int)a.Kind)), kindX, ry, Style.Dim, Style.Small);
			s.Text(a.Cost > 0 ? $"{a.Cost} TP" : "-", costX, ry, Style.Text, Style.Small);
			s.Text(a.MaxUses >= 0 ? $"{a.Uses}/{a.MaxUses}" : "-", usesX, ry, Style.Text, Style.Small);
		}, _pickAction is null);
		s.Fill(x, y + h - descH, listW - 14, 1, Style.PanelEdge * 0.4f);
		DrawDescription(s, _s.Description(DescriptionKind.Ability, abilities[Math.Clamp(_sub.Cursor, 0, abilities.Count - 1)].Id), x + 14, y + h - descH + Px(8), listW - 40);
		if (_pickAction is null)
		{
			_probeSelected = abilities[_sub.Cursor].Name;
			_probeTop = _sub.Top;
			s.Text("* usable here", x + w - s.Measure("* usable here", Style.Small) - 20, top, Style.Dim, Style.Small);
		}
		else
		{
			int tx = x + listW + 20;
			s.Text($"Use {_s.DisplayName(_pickAction)} on whom?", tx, top, Style.Accent);
			DrawItems(s, _third, tx, listTop + Px(12), w - listW - 20, y + h - listTop - Px(12), lineup.Select(m => $"{m.Name}  HP {m.Hp}/{m.MaxHp}").ToList());
		}
	}
}
