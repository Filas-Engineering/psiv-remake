using Psiv.Session;

namespace Psiv.Sample;

// The menu's Equip page: a member, a slot, then the bag's pieces for it (those the member cannot wear marked) or removing
// what is worn. Browsing a piece shows the session's comparison: the piece's bonuses and the member's stats before and
// after, as a battle would use them.
public sealed partial class SampleController
{
	private int? _equipSlot;
	// The member's stats before and after the last change made here.
	private (string Title, List<(string Label, int Before, int After)> Rows)? _equipChange;

	private string[] CompareLabels =>
		[.. new[] { "max_hp", "max_tp", "strength", "mental", "agility", "dexterity", "defense", "critical", "evasion" }.Select(stat => T("ui.stat." + stat))];

	private int[]? Totals(string memberId)
	{
		Member? m = _s.Party().FirstOrDefault(x => x.Id == memberId);
		MemberDetail? d = _s.MemberDetail(memberId);
		return m is null || d is null ? null : [m.MaxHp, m.MaxTp, m.Strength, m.Mental, m.Agility, m.Dexterity, m.Defense, d.Critical, d.Evasion];
	}

	private static int[] Values(StatBonus b) => [b.MaxHp, b.MaxTp, b.Strength, b.Mental, b.Agility, b.Dexterity, b.Defense, b.Critical, b.Evasion];

	// What can go in the slot: the bag's pieces for it, marked when the member cannot wear them, then taking off the one worn.
	private List<(string Label, string? Piece, bool Wearable)> EquipChoices(string memberId, int slot)
	{
		var choices = _s.Inventory().Where(i => i.IsEquipment && (int)i.Slot == slot).Select(i =>
		{
			bool wearable = _s.EquipPreview(memberId, i.Id)?.CanWear ?? false;
			return (T("ui.common.item_count", i.Name, i.Count) + (wearable ? "" : "  " + T("ui.equip.cannot_wear_mark")), (string?)i.Id, wearable);
		}).ToList();
		Member? m = _s.Party().FirstOrDefault(x => x.Id == memberId);
		if (m is not null && m.Equipment[slot].Length > 0)
		{
			choices.Add((T("ui.equip.remove", m.EquipmentNames[slot]), null, true));
		}
		return choices;
	}

	// The comparison of a piece on the member: its own effects in words, who can wear it, then each stat and effect that
	// would change, before and after.
	private void DrawPreview(IDrawSurface s, EquipPreview p, int x, int y, int w)
	{
		int rowH = Px(24);
		DrawDescription(s, _s.Description(DescriptionKind.Equipment, p.Piece), x, y, w);
		int ly = y + Px(50);
		var effects = new List<string> { BonusText(p.Bonus) };
		if (p.Element != Element.None)
		{
			effects.Add(T("ui.equip.element_attacks", T(NameKind.Element, (int)p.Element)));
		}
		if (p.AttackName.Length > 0)
		{
			effects.Add(T("ui.equip.attacks_with", p.AttackName));
		}
		effects.AddRange(p.GrantedNames.Select(n => T("ui.equip.grants", n)));
		effects.AddRange(p.PieceResists.Select(r => T("ui.equip.resists", r.Name, r.Percent)));
		foreach (string l in Style.Wrap(s, ListText(effects), w, Style.Small))
		{
			s.Text(l, x, ly, Style.Text, Style.Small);
			ly += rowH;
		}
		s.Text(Style.Fit(s, WearersText(p.WearerNames), w, Style.Small), x, ly, Style.Dim, Style.Small);
		ly += rowH + Px(6);
		if (!p.CanWear)
		{
			s.Text(Style.Fit(s, p.Refusal?.Text ?? T("ui.equip.cannot_wear"), w, Style.Small), x, ly, Style.Bad, Style.Small);
			_probeCompare = "cannot wear";
			return;
		}
		s.Text(Style.Fit(s, p.ReplacesName.Length > 0 ? T("ui.equip.with_it_instead", p.ReplacesName) : T("ui.equip.with_it"), w), x, ly, Style.Accent);
		ly += Px(32);
		int[] before = Values(p.Before.Stats);
		int[] after = Values(p.After.Stats);
		var rows = CompareLabels.Select((l, i) => (l, before[i], after[i])).Where(r => r.Item2 != r.Item3).ToList();
		Widgets.Compare(s, x, ly, w, rowH, rows.Count > 0 ? rows : [(T("ui.equip.no_changes"), 0, 0)]);
		_probeCompare = rows.Count == 0 ? "none" : string.Join(", ", rows.Select(r => $"{r.Item1} {r.Item2}>{r.Item3}"));
		ly += rowH * Math.Max(1, rows.Count) + Px(6);
		var notes = new List<string>();
		if (p.Before.AttackElement != p.After.AttackElement)
		{
			notes.Add(T("ui.equip.element_change", T(NameKind.Element, (int)p.Before.AttackElement), T(NameKind.Element, (int)p.After.AttackElement)));
		}
		if (p.Before.BasicAttack != p.After.BasicAttack)
		{
			notes.Add(T("ui.equip.attack_change", p.Before.BasicAttackName, p.After.BasicAttackName));
		}
		notes.AddRange(p.After.AbilityNames.Except(p.Before.AbilityNames).Select(n => T("ui.equip.learns", n)));
		notes.AddRange(p.Before.AbilityNames.Except(p.After.AbilityNames).Select(n => T("ui.equip.loses", n)));
		foreach (string status in p.Before.StatusResists.Select(r => r.Name).Union(p.After.StatusResists.Select(r => r.Name)))
		{
			int was = p.Before.StatusResists.FirstOrDefault(r => r.Name == status)?.Percent ?? 0;
			int now = p.After.StatusResists.FirstOrDefault(r => r.Name == status)?.Percent ?? 0;
			if (was != now)
			{
				notes.Add(T("ui.equip.resistance_change", status, was, now));
			}
		}
		foreach (string note in notes)
		{
			s.Text(Style.Fit(s, note, w, Style.Small), x, ly, Style.Text, Style.Small);
			ly += rowH;
		}
	}

	private void UpdateEquip(ActionInput input)
	{
		List<Member> lineup = LineupMembers();
		if (_pickMember is null)
		{
			_list.Move(input, lineup.Count);
			if (input.Confirm && lineup.Count > 0)
			{
				_pickMember = lineup[_list.Cursor].Id;
				_sub.Reset();
				_equipChange = null;
			}
			return;
		}
		if (_equipSlot is not { } slot)
		{
			_sub.Move(input, Enum.GetValues<EquipSlot>().Length);
			if (input.Back)
			{
				_pickMember = null;
			}
			else if (input.Confirm)
			{
				_equipSlot = _sub.Cursor;
				_third.Reset();
			}
			return;
		}
		List<(string Label, string? Piece, bool Wearable)> choices = EquipChoices(_pickMember, slot);
		_third.Move(input, choices.Count);
		if (input.Back)
		{
			_equipSlot = null;
		}
		else if (input.Confirm && choices.Count > 0)
		{
			(string label, string? piece, _) = choices[_third.Cursor];
			int[]? before = Totals(_pickMember);
			PsivResult result = piece is null ? _s.Unequip(_pickMember, (EquipSlot)slot) : _s.Equip(_pickMember, piece);
			Report(result);
			if (result.Ok && before is not null && Totals(_pickMember) is { } after)
			{
				_equipChange = (label, CompareLabels.Select((l, i) => (l, before[i], after[i])).Where(r => r.Item2 != r.Item3).ToList());
				_equipSlot = null;
			}
		}
	}

	private void DrawEquip(IDrawSurface s, int x, int y, int w, int h)
	{
		List<Member> lineup = LineupMembers();
		if (_pickMember is null)
		{
			DrawItems(s, _list, x, y, w, h, lineup.Select(m => m.Name + ": " + ListText(m.EquipmentNames.Where(n => n.Length > 0))).ToList());
			return;
		}
		Member? member = lineup.FirstOrDefault(m => m.Id == _pickMember);
		MemberDetail? detail = _s.MemberDetail(_pickMember);
		if (member is null || detail is null)
		{
			return;
		}
		int listW = w / 2 - 10;
		int rx = x + listW + 30;
		int rw = w - listW - 30;
		s.Text(member.Name, x, y, Style.Accent);
		var slots = Enum.GetValues<EquipSlot>().Select(slot => (int)slot).ToList();
		DrawItems(s, _sub, x, y + Px(36), listW, Px(30) * slots.Count, slots.Select(slot =>
			$"{T(NameKind.EquipSlot, slot)}: " + (member.Equipment[slot].Length > 0 ? member.EquipmentNames[slot] : "-")).ToList(), _equipSlot is null);
		int[] totals = Totals(member.Id)!;
		int rowH = Px(24);
		if (_equipSlot is not { } chosen)
		{
			int slot = _sub.Cursor;
			s.Text(T("ui.equip.worn_here"), rx, y, Style.Accent);
			s.Text(Style.Fit(s, member.Equipment[slot].Length > 0 ? $"{member.EquipmentNames[slot]}: {BonusText(detail.EquipmentBonus[slot])}" : T("ui.equip.nothing"), rw, Style.Small),
				rx, y + Px(36), Style.Text, Style.Small);
			if (_equipChange is { } change)
			{
				s.Text(Style.Fit(s, T("ui.equip.last_change", change.Title), rw), rx, y + Px(80), Style.Accent);
				Widgets.Compare(s, rx, y + Px(116), rw, rowH, change.Rows.Count > 0 ? change.Rows : [(T("ui.equip.no_change_made"), 0, 0)]);
			}
			return;
		}
		int py = y + Px(36) + Px(30) * slots.Count + Px(20);
		List<(string Label, string? Piece, bool Wearable)> choices = EquipChoices(member.Id, chosen);
		s.Text(T("ui.equip.for_slot", T(NameKind.EquipSlot, chosen)), x, py, Style.Accent);
		if (choices.Count == 0)
		{
			s.Text(T("ui.equip.nothing_fits"), x + 14, py + Px(36), Style.Dim);
		}
		DrawItems(s, _third, x, py + Px(36), listW, y + h - py - Px(36), choices.Select(c => c.Label).ToList(), enabled: choices.Select(c => c.Wearable).ToList());
		if (choices.Count == 0)
		{
			return;
		}
		(string label, string? piece, _) = choices[_third.Cursor];
		int[] worn = Values(detail.EquipmentBonus[chosen]);
		s.Text(T("ui.equip.worn_now"), rx, y, Style.Accent);
		s.Text(Style.Fit(s, member.Equipment[chosen].Length > 0 ? $"{member.EquipmentNames[chosen]}: {BonusText(detail.EquipmentBonus[chosen])}" : T("ui.equip.nothing"), rw, Style.Small),
			rx, y + Px(32), Style.Text, Style.Small);
		if (piece is null)
		{
			s.Text(T("ui.equip.after_removing"), rx, y + Px(70), Style.Accent);
			var rows = CompareLabels.Select((l, i) => (l, totals[i], totals[i] - worn[i])).Where(r => r.Item2 != r.Item3).ToList();
			Widgets.Compare(s, rx, y + Px(104), rw, rowH, rows.Count > 0 ? rows : [(T("ui.equip.no_changes"), 0, 0)]);
		}
		else if (_s.EquipPreview(member.Id, piece) is { } preview)
		{
			s.Text(Style.Fit(s, preview.PieceName, rw), rx, y + Px(70), Style.Accent);
			DrawPreview(s, preview, rx, y + Px(104), rw);
		}
	}
}
