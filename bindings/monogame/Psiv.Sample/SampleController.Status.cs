using System.Globalization;

using Psiv.Session;

namespace Psiv.Sample;

// The menu's Status page: pick a member, then their stats or their abilities; field abilities can be used from here.
public sealed partial class SampleController
{
	// 0: stats, 1: abilities.
	private int _statusPage;

	private static string Signed(int value) => value.ToString("+0;-0;0", CultureInfo.InvariantCulture);

	private string BonusText(StatBonus b)
	{
		var parts = new List<string>();
		void Add(string stat, int value)
		{
			if (value != 0)
			{
				parts.Add($"{T("ui.stat_short." + stat)} {Signed(value)}");
			}
		}
		Add("hp", b.MaxHp);
		Add("tp", b.MaxTp);
		Add("str", b.Strength);
		Add("men", b.Mental);
		Add("agi", b.Agility);
		Add("dex", b.Dexterity);
		Add("def", b.Defense);
		Add("crit", b.Critical);
		Add("eva", b.Evasion);
		if (_s.GameInfo().MagicDefense)
		{
			Add("mdef", b.MagicDefense);
		}
		return parts.Count == 0 ? T("ui.status.no_bonus") : ListText(parts);
	}

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
			if (ability.Travel is { } kind)
			{
				PickTravel(kind, _pickMember, ability.Id);
			}
			else if (ability.Field && ability.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
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
		left.Add((T("ui.status.stats"), ""));
		left.Add((T("ui.stat.strength"), Stat(m.Strength, b => b.Strength)));
		left.Add((T("ui.stat.mental"), Stat(m.Mental, b => b.Mental)));
		left.Add((T("ui.stat.agility"), Stat(m.Agility, b => b.Agility)));
		left.Add((T("ui.stat.dexterity"), Stat(m.Dexterity, b => b.Dexterity)));
		left.Add((T("ui.stat.defense"), Stat(m.Defense, b => b.Defense)));
		left.Add((T("ui.stat.critical"), Stat(d.Critical, b => b.Critical)));
		left.Add((T("ui.stat.evasion"), Stat(d.Evasion, b => b.Evasion)));
		if (_s.GameInfo().MagicDefense)
		{
			left.Add((T("ui.stat.magic_defense"), Stat(d.MagicDefense, b => b.MagicDefense)));
		}
		left.Add((T("ui.status.attack_element"), ElementText(d.AttackElement)));
		left.Add((T("ui.status.affinities"), ""));
		if (d.AffinityList is not { Count: > 0 })
		{
			left.Add((T("ui.status.none"), "-"));
		}
		int divisor = _s.GameInfo().AffinityDivisor;
		foreach (Affinity a in d.AffinityList ?? [])
		{
			left.Add((a.Name, $"{a.Percent * 100 / Math.Max(1, divisor)}%"));
		}
		left.Add((T("ui.status.resistances"), ""));
		if (d.StatusResists.Count == 0)
		{
			left.Add((T("ui.status.none"), "-"));
		}
		foreach (StatusResist r in d.StatusResists)
		{
			left.Add((r.Name, $"{r.Percent}%"));
		}
		right.Add((T("ui.status.equipment"), ""));
		foreach (int slot in EquipPositions.Select(p => (int)p))
		{
			string piece = m.EquipmentNames[slot].Length > 0 ? m.EquipmentNames[slot] : "-";
			right.Add((T(NameKind.EquipSlot, slot), piece));
			if (m.Equipment[slot].Length > 0 && slot < d.EquipmentBonus.Count)
			{
				right.Add(("", BonusText(d.EquipmentBonus[slot])));
			}
		}
		right.Add((T("ui.status.statuses"), ""));
		if (m.Statuses.Count == 0)
		{
			right.Add((T("ui.status.none"), "-"));
		}
		foreach (ActiveStatus st in m.Statuses)
		{
			right.Add((st.Name, st.Remaining > 0 ? T("ui.status.remaining", st.Remaining) : "-"));
		}
		return (left, right);
	}

	private List<(string Label, string Value)> StatusRows(string memberId)
	{
		var (left, right) = StatusColumns(memberId);
		return left.Count >= right.Count ? left : right;
	}

	private void DrawStatus(IDrawSurface s, IDrawSurface t, Art art, int x, int y, int w, int h)
	{
		List<Member> lineup = LineupMembers();
		UiContext data = UiArea(x, y, w, h);
		if (_pickMember is null)
		{
			Ui.Draw("menu_status", s, t, data
				.Set("view", "members")
				.Set("members", lineup.Select(m => $"{MemberRow(m)}  {T("ui.status.exp", m.Experience)}" + string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList())
				.Set("list", _list));
			return;
		}
		Member? member = lineup.FirstOrDefault(m => m.Id == _pickMember);
		MemberDetail? detail = _s.MemberDetail(_pickMember);
		if (member is null || detail is null)
		{
			return;
		}
		data.Set("view", _statusPage == 0 ? "stats" : "abilities")
			.Set("portrait", art.Portrait("", member.Id))
			.Set("name", member.Name)
			.Set("level", member.Level)
			.Set("experience", member.Experience)
			.Set("experience_next", detail.ExperienceToNext)
			.Set("hp", member.Hp)
			.Set("max_hp", member.MaxHp)
			.Set("tp", member.Tp)
			.Set("max_tp", member.MaxTp)
			.Set("pages", new List<string> { T("ui.status.stats"), T("ui.status.abilities") })
			.Set("page", _statusPage)
			.Set("sub_list", _sub);
		if (_statusPage == 0)
		{
			var (left, right) = StatusColumns(member.Id);
			UiContext Cell(List<(string Label, string Value)> rows, int i) => i < rows.Count
				? new UiContext(data).Set("shown", true).Set("label", rows[i].Label).Set("value", rows[i].Value)
				: new UiContext(data).Set("shown", false);
			Ui.Draw("menu_status", s, t, data.Set("stat_rows", Enumerable.Range(0, Math.Max(left.Count, right.Count))
				.Select(i => new UiContext(data).Set("cells", new List<UiContext> { Cell(left, i), Cell(right, i) })).ToList()));
			_probeTop = _sub.Top;
			return;
		}
		IReadOnlyList<AbilityInfo> abilities = _s.MemberAbilities(member.Id);
		string description = "";
		if (abilities.Count > 0)
		{
			description = _s.Description(DescriptionKind.Ability, abilities[Math.Clamp(_sub.Cursor, 0, abilities.Count - 1)].Id) ?? "";
			_probeDescription = description.Length == 0 ? "-" : description;
		}
		Ui.Draw("menu_status", s, t, data
			.Set("picking", _pickAction is not null)
			.Set("abilities", abilities.Select(a => new UiContext(data)
				.Set("text", a.Name)
				.Set("label", a.Name + (a.Field ? "  *" : ""))
				.Set("usable", a.Field)
				.Set("kind", T(NameKind.AbilityKind, (int)a.Kind))
				.Set("cost", a.Cost > 0 ? T("ui.common.tp_cost", a.Cost) : "-")
				.Set("uses", a.MaxUses >= 0 ? $"{a.Uses}/{a.MaxUses}" : "-")).ToList())
			.Set("description", description)
			.Set("target_name", _pickAction is null ? "" : _s.DisplayName(_pickAction))
			.Set("targets", _pickAction is null ? new List<string>() : lineup.Select(m => T("ui.status.member_hp", m.Name, m.Hp, m.MaxHp)).ToList())
			.Set("target_list", _third));
	}
}
