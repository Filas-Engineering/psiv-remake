using Psiv.Session;

namespace Psiv.Sample;

// The menu's Equip page: a member, a slot, then the pieces that fit it or removing what is worn. The session reports
// the worn pieces' bonuses but not those of pieces in the bag, so a piece's change is shown once it is on.
public sealed partial class SampleController
{
	private int? _equipSlot;
	// The member's stats before and after the last change made here.
	private (string Title, List<(string Label, int Before, int After)> Rows)? _equipChange;

	private static readonly string[] CompareLabels = ["Max HP", "Max TP", "Strength", "Mental", "Agility", "Dexterity", "Defense", "Critical", "Evasion"];

	private int[]? Totals(string memberId)
	{
		Member? m = _s.Party().FirstOrDefault(x => x.Id == memberId);
		MemberDetail? d = _s.MemberDetail(memberId);
		return m is null || d is null ? null : [m.MaxHp, m.MaxTp, m.Strength, m.Mental, m.Agility, m.Dexterity, m.Defense, d.Critical, d.Evasion];
	}

	private static int[] Values(StatBonus b) => [b.MaxHp, b.MaxTp, b.Strength, b.Mental, b.Agility, b.Dexterity, b.Defense, b.Critical, b.Evasion];

	// What can go in the slot: the bag's pieces for it, then taking off the one worn.
	private List<(string Label, string? Piece)> EquipChoices(string memberId, int slot)
	{
		var choices = _s.Inventory().Where(i => i.IsEquipment && (int)i.Slot == slot).Select(i => ($"{i.Name} x{i.Count}", (string?)i.Id)).ToList();
		Member? m = _s.Party().FirstOrDefault(x => x.Id == memberId);
		if (m is not null && m.Equipment[slot].Length > 0)
		{
			choices.Add(($"Remove {m.EquipmentNames[slot]}", null));
		}
		return choices;
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
		List<(string Label, string? Piece)> choices = EquipChoices(_pickMember, slot);
		_third.Move(input, choices.Count);
		if (input.Back)
		{
			_equipSlot = null;
		}
		else if (input.Confirm && choices.Count > 0)
		{
			(string label, string? piece) = choices[_third.Cursor];
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
			DrawItems(s, _list, x, y, w, h, lineup.Select(m => m.Name + ": " + string.Join(", ", m.EquipmentNames.Where(n => n.Length > 0))).ToList());
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
			$"{Words(GameSession.NameOf(NameKind.EquipSlot, slot))}: " + (member.Equipment[slot].Length > 0 ? member.EquipmentNames[slot] : "-")).ToList(), _equipSlot is null);
		int[] totals = Totals(member.Id)!;
		int rowH = Px(24);
		if (_equipSlot is not { } chosen)
		{
			int slot = _sub.Cursor;
			s.Text("Worn here", rx, y, Style.Accent);
			s.Text(Style.Fit(s, member.Equipment[slot].Length > 0 ? $"{member.EquipmentNames[slot]}: {BonusText(detail.EquipmentBonus[slot])}" : "Nothing", rw, Style.Small),
				rx, y + Px(36), Style.Text, Style.Small);
			if (_equipChange is { } change)
			{
				s.Text(Style.Fit(s, $"Last change: {change.Title}", rw), rx, y + Px(80), Style.Accent);
				Widgets.Compare(s, rx, y + Px(116), rw, rowH, change.Rows.Count > 0 ? change.Rows : [("No stat changed", 0, 0)]);
			}
			return;
		}
		int py = y + Px(36) + Px(30) * slots.Count + Px(20);
		List<(string Label, string? Piece)> choices = EquipChoices(member.Id, chosen);
		s.Text($"For the {Words(GameSession.NameOf(NameKind.EquipSlot, chosen))} slot", x, py, Style.Accent);
		if (choices.Count == 0)
		{
			s.Text("Nothing in the bag fits here.", x + 14, py + Px(36), Style.Dim);
		}
		DrawItems(s, _third, x, py + Px(36), listW, y + h - py - Px(36), choices.Select(c => c.Label).ToList());
		if (choices.Count == 0)
		{
			return;
		}
		(string label, string? piece) = choices[_third.Cursor];
		int[] worn = Values(detail.EquipmentBonus[chosen]);
		s.Text("Worn now", rx, y, Style.Accent);
		s.Text(Style.Fit(s, member.Equipment[chosen].Length > 0 ? $"{member.EquipmentNames[chosen]}: {BonusText(detail.EquipmentBonus[chosen])}" : "Nothing", rw, Style.Small),
			rx, y + Px(32), Style.Text, Style.Small);
		if (piece is null)
		{
			s.Text("After removing it", rx, y + Px(70), Style.Accent);
			var rows = CompareLabels.Select((l, i) => (l, totals[i], totals[i] - worn[i])).Where(r => r.Item2 != r.Item3).ToList();
			Widgets.Compare(s, rx, y + Px(104), rw, rowH, rows.Count > 0 ? rows : [("No stat changes", 0, 0)]);
		}
		else
		{
			s.Text(Style.Fit(s, label, rw), rx, y + Px(70), Style.Accent);
			int ly = y + Px(104);
			foreach (string l in Style.Wrap(s, "The session does not report this piece's bonuses; the change shows here once it is on.", rw, Style.Small))
			{
				s.Text(l, rx, ly, Style.Dim, Style.Small);
				ly += rowH;
			}
		}
	}
}
