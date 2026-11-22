using Psiv.Session;

namespace Psiv.Sample;

// The menu's Equip page: a member, a place to wear something (each hand, then the other slots), then the bag's pieces for
// it (those the member cannot wear there marked) or removing what is worn. Browsing a piece shows the session's
// comparison at that place: the piece's bonuses, what else it sends back, and the member's stats before and after, as a
// battle would use them.
public sealed partial class SampleController
{
	private static readonly EquipSlot[] EquipPositions = [EquipSlot.Weapon, EquipSlot.LeftHand, EquipSlot.Head, EquipSlot.Body, EquipSlot.Arms, EquipSlot.Accessory];

	private int? _equipSlot;
	// The member's stats before and after the last change made here.
	private (string Title, List<(string Label, int Before, int After)> Rows)? _equipChange;

	// Magic defense is shown only when the rules use it.
	private bool MagicDefenseShown => _s.GameInfo().MagicDefense;

	private string[] CompareLabels =>
		[.. new[] { "max_hp", "max_tp", "strength", "mental", "agility", "dexterity", "defense", "critical", "evasion", "magic_defense" }
			.Take(MagicDefenseShown ? 10 : 9).Select(stat => T("ui.stat." + stat))];

	private int[]? Totals(string memberId)
	{
		Member? m = _s.Party().FirstOrDefault(x => x.Id == memberId);
		MemberDetail? d = _s.MemberDetail(memberId);
		return m is null || d is null ? null : [m.MaxHp, m.MaxTp, m.Strength, m.Mental, m.Agility, m.Dexterity, m.Defense, d.Critical, d.Evasion, d.MagicDefense];
	}

	private static int[] Values(StatBonus b) => [b.MaxHp, b.MaxTp, b.Strength, b.Mental, b.Agility, b.Dexterity, b.Defense, b.Critical, b.Evasion, b.MagicDefense];

	// The places the page offers, in order: the menu_equip layout's `places`, else the built-in six.
	private List<int> EquipPlaces() => Layout.Screen("menu_equip").Places?.Select(p => (int)Enum.Parse<EquipSlot>(p.Slot.Replace("_", ""), ignoreCase: true)).ToList()
		?? [.. EquipPositions.Select(p => (int)p)];

	private string PlaceLabel(int slot) => Layout.Screen("menu_equip").Places?.FirstOrDefault(p => (int)Enum.Parse<EquipSlot>(p.Slot.Replace("_", ""), ignoreCase: true) == slot).Label is { } key
		? T(key) : T(NameKind.EquipSlot, slot);

	// The page opens on the member list (`members_first`, 1 by default), or on the first member's places, where the tab
	// keys still turn the tabs and the PartyTalk key (the Items page's sort key) goes to the next member.
	private bool EquipMembersFirst => Ui.Param("menu_equip", "members_first", UiData(), 1) != 0;

	private void EquipFirstMember(List<Member> lineup)
	{
		if (_pickMember is null && !EquipMembersFirst && lineup.Count > 0)
		{
			_pickMember = lineup[0].Id;
		}
	}

	private static bool Fits(InventoryEntry piece, int position) => position is (int)EquipSlot.Weapon or (int)EquipSlot.LeftHand
		? piece.Slot is EquipSlot.Weapon or EquipSlot.Shield
		: (int)piece.Slot == position;

	// What can go there: the bag's pieces for it (weapons and shields in either hand), marked when the member cannot wear
	// them there, two-handed ones marked too, then taking off the one worn.
	private List<(string Label, string? Piece, bool Wearable)> EquipChoices(string memberId, int slot)
	{
		var choices = _s.Inventory().Where(i => i.IsEquipment && Fits(i, slot)).Select(i =>
		{
			bool wearable = _s.EquipPreview(memberId, i.Id, (EquipSlot)slot)?.CanWear ?? false;
			string label = T("ui.common.item_count", i.Name, i.Count) + (i.Hands == 2 ? "  " + T("ui.equip.both_hands_mark") : "");
			return (label + (wearable ? "" : "  " + T("ui.equip.cannot_wear_mark")), (string?)i.Id, wearable);
		}).ToList();
		Member? m = _s.Party().FirstOrDefault(x => x.Id == memberId);
		if (m is not null && m.Equipment[slot].Length > 0)
		{
			choices.Add((T("ui.equip.remove", m.EquipmentNames[slot]), null, true));
		}
		return choices;
	}

	private void UpdateEquip(ActionInput input)
	{
		List<Member> lineup = LineupMembers();
		EquipFirstMember(lineup);
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
			List<int> places = EquipPlaces();
			_sub.Move(input, places.Count);
			if (!EquipMembersFirst && input.Pressed(GameAction.PartyTalk) && lineup.Count > 0)
			{
				int at = Math.Max(0, lineup.FindIndex(m => m.Id == _pickMember));
				_pickMember = lineup[(at + 1) % lineup.Count].Id;
				_equipChange = null;
			}
			else if (input.Back)
			{
				_pickMember = null;
			}
			else if (input.Confirm)
			{
				_equipSlot = places[_sub.Cursor];
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
			PsivResult result = piece is null ? _s.Unequip(_pickMember, (EquipSlot)slot) : _s.EquipAt(_pickMember, piece, (EquipSlot)slot);
			Report(result);
			if (result.Ok && before is not null && Totals(_pickMember) is { } after)
			{
				_equipChange = (label, CompareLabels.Select((l, i) => (l, before[i], after[i])).Where(r => r.Item2 != r.Item3).ToList());
				_equipSlot = null;
			}
		}
	}

	private void DrawEquip(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		List<Member> lineup = LineupMembers();
		EquipFirstMember(lineup);
		UiContext data = UiArea(x, y, w, h);
		if (_pickMember is null)
		{
			Ui.Draw("menu_equip", s, t, data
				.Set("view", "members")
				.Set("members", lineup.Select(m => m.Name + ": " + ListText(m.EquipmentNames.Where(n => n.Length > 0))).ToList())
				.Set("list", _list));
			return;
		}
		Member? member = lineup.FirstOrDefault(m => m.Id == _pickMember);
		MemberDetail? detail = _s.MemberDetail(_pickMember);
		if (member is null || detail is null)
		{
			return;
		}
		string WornText(int slot) => member.Equipment[slot].Length > 0 ? $"{member.EquipmentNames[slot]}: {BonusText(detail.EquipmentBonus[slot])}" : T("ui.equip.nothing");
		List<int> slots = EquipPlaces();
		string Row(int slot) => $"{PlaceLabel(slot)}: " + (member.Equipment[slot].Length > 0 ? member.EquipmentNames[slot] : "-");
		data.Set("member_name", member.Name)
			.Set("slot_rows", slots.Select(Row).ToList())
			.Set("places", slots.Select(slot => new UiContext(data).Set("text", Row(slot)).Set("slot", Snake(((EquipSlot)slot).ToString())).Set("label", PlaceLabel(slot))
				.Set("piece", member.EquipmentNames[slot]).Set("worn", member.Equipment[slot].Length > 0)).ToList())
			.Set("member_index", lineup.FindIndex(m => m.Id == member.Id))
			.Set("member_count", lineup.Count)
			.Set("slot_list", _sub);
		int[] totals = Totals(member.Id)!;
		List<(string Label, int Before, int After)> compare = [];
		if (_equipSlot is not { } chosen)
		{
			// With `stats_preview_only` the stats show only while a piece is browsed, not the last change made.
			(string Title, List<(string Label, int Before, int After)> Rows)? shownChange = Ui.Param("menu_equip", "stats_preview_only", data, 0) != 0 ? null : _equipChange;
			data.Set("view", "slots")
				.Set("right", "worn")
				.Set("worn_text", WornText(slots[Math.Clamp(_sub.Cursor, 0, slots.Count - 1)]))
				.Set("has_change", shownChange is not null)
				.Set("change_text", shownChange is { } last ? T("ui.equip.last_change", last.Title) : "");
			if (shownChange is { } change)
			{
				compare = change.Rows.Count > 0 ? change.Rows : [(T("ui.equip.no_change_made"), 0, 0)];
			}
		}
		else
		{
			List<(string Label, string? Piece, bool Wearable)> choices = EquipChoices(member.Id, chosen);
			data.Set("view", "pieces")
				.Set("slot_name", PlaceLabel(chosen))
				.Set("choices", choices.Select(c => new UiContext(data).Set("text", c.Label).Set("enabled", c.Wearable)).ToList())
				.Set("choice_list", _third);
			if (choices.Count > 0)
			{
				string? piece = choices[Math.Clamp(_third.Cursor, 0, choices.Count - 1)].Piece;
				data.Set("right", "piece").Set("worn_text", WornText(chosen)).Set("removing", piece is null);
				if (piece is null)
				{
					int[] worn = Values(detail.EquipmentBonus[chosen]);
					var rows = CompareLabels.Select((l, i) => (l, totals[i], totals[i] - worn[i])).Where(r => r.Item2 != r.Item3).ToList();
					compare = rows.Count > 0 ? rows : [(T("ui.equip.no_changes"), 0, 0)];
				}
				else if (_s.EquipPreview(member.Id, piece, (EquipSlot)chosen) is { } p)
				{
					compare = PreviewData(t, data, p);
				}
			}
		}
		Ui.Draw("menu_equip", s, t, data.Set("compare_rows", compare.Select(r => new UiContext(data).Set("label", r.Label).Set("before", r.Before).Set("after", r.After)).ToList()));
	}

	// The comparison's fields; returns its stat rows.
	private List<(string Label, int Before, int After)> PreviewData(IDrawSurface t, UiContext data, EquipPreview p)
	{
		string description = Description(DescriptionKind.Equipment, p.Piece) ?? "";
		_probeDescription = description.Length == 0 ? "-" : description;
		var effects = new List<string> { BonusText(p.Bonus) };
		if (p.Element != Element.None)
		{
			effects.Add(T("ui.equip.element_attacks", ElementText(p.Element)));
		}
		if (p.AttackName.Length > 0)
		{
			effects.Add(T("ui.equip.attacks_with", p.AttackName));
		}
		effects.AddRange(p.GrantedNames.Select(n => T("ui.equip.grants", n)));
		effects.AddRange(p.PieceResists.Select(r => T("ui.equip.resists", r.Name, r.Percent)));
		string effectsText = ListText(effects);
		data.Set("has_preview", true)
			.Set("piece_name", p.PieceName)
			.Set("description", description)
			.Set("effects", effectsText)
			.Set("wearers", WearersText(p.WearerNames))
			.Set("can_wear", p.CanWear);
		// The lines after the effects stand below however many lines they wrap to at the column's width.
		int width = Ui.WindowRect("menu_equip", "piece", t, data)?.Width ?? 0;
		data.Set("effect_lines", Style.Wrap(t, effectsText, width, Style.Small).Count);
		if (!p.CanWear)
		{
			data.Set("refusal", p.Refusal?.Text ?? T("ui.equip.cannot_wear"));
			_probeCompare = "cannot wear";
			return [];
		}
		string instead = p.AlsoReplacesName.Length > 0 ? (p.ReplacesName.Length > 0 ? ListText([p.ReplacesName, p.AlsoReplacesName]) : p.AlsoReplacesName) : p.ReplacesName;
		int[] before = Values(p.Before.Stats);
		int[] after = Values(p.After.Stats);
		var rows = CompareLabels.Select((l, i) => (l, before[i], after[i])).Where(r => r.Item2 != r.Item3).ToList();
		_probeCompare = rows.Count == 0 ? "none" : string.Join(", ", rows.Select(r => $"{r.Item1} {r.Item2}>{r.Item3}"));
		var notes = new List<string>();
		if (p.Before.AttackElement != p.After.AttackElement)
		{
			notes.Add(T("ui.equip.element_change", ElementText(p.Before.AttackElement), ElementText(p.After.AttackElement)));
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
		data.Set("with_it", instead.Length > 0 ? T("ui.equip.with_it_instead", instead) : T("ui.equip.with_it")).Set("notes", notes);
		return rows.Count > 0 ? rows : [(T("ui.equip.no_changes"), 0, 0)];
	}
}
