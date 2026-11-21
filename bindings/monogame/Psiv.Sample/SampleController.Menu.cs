using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private static readonly string[] MenuTabs = ["Party", "Items", "Equip", "Save", "Quests", "Codex", "Log"];

	private void UpdateMenu(ActionInput input)
	{
		if (_pickMember is null && _pickAction is null)
		{
			if (input.Pressed(GameAction.PagePrev))
			{
				_tab = (_tab + MenuTabs.Length - 1) % MenuTabs.Length;
				_cursor = 0;
			}
			if (input.Pressed(GameAction.PageNext))
			{
				_tab = (_tab + 1) % MenuTabs.Length;
				_cursor = 0;
			}
			if (input.Back)
			{
				Report(_s.CloseMenu());
				return;
			}
		}
		else if (input.Back)
		{
			_pickMember = null;
			_pickAction = null;
			_cursor = 0;
			return;
		}
		List<Member> lineup = _s.Party().Where(m => m.InLineup).ToList();
		switch (MenuTabs[_tab])
		{
		case "Party":
			if (_pickMember is null)
			{
				Move(input, lineup.Count);
				if (input.Confirm && lineup.Count > 0)
				{
					_pickMember = lineup[_cursor].Id;
					_cursor = 0;
				}
			}
			else if (_pickAction is null)
			{
				var techniques = _s.MemberAbilities(_pickMember).Where(a => a.Field).ToList();
				Move(input, techniques.Count);
				if (input.Confirm && techniques.Count > 0)
				{
					AbilityInfo ability = techniques[_cursor];
					if (ability.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
					{
						_pickAction = ability.Id;
						_cursor = 0;
					}
					else
					{
						Report(_s.UseAbility(_pickMember, ability.Id));
					}
				}
			}
			else
			{
				Move(input, lineup.Count);
				if (input.Confirm)
				{
					Report(_s.UseAbility(_pickMember, _pickAction, lineup[_cursor].Id));
					_pickAction = null;
					_cursor = 0;
				}
			}
			break;
		case "Items":
			var items = _s.Inventory().Where(i => !i.IsEquipment).ToList();
			if (_pickAction is null)
			{
				Move(input, items.Count);
				if (input.Confirm && items.Count > 0)
				{
					InventoryEntry item = items[_cursor];
					if (item.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
					{
						_pickAction = item.Id;
						_cursor = 0;
					}
					else
					{
						Report(_s.UseItem(item.Id));
					}
				}
			}
			else
			{
				Move(input, lineup.Count);
				if (input.Confirm)
				{
					Report(_s.UseItem(_pickAction, lineup[_cursor].Id));
					_pickAction = null;
					_cursor = 0;
				}
			}
			break;
		case "Equip":
			if (_pickMember is null)
			{
				Move(input, lineup.Count);
				if (input.Confirm && lineup.Count > 0)
				{
					_pickMember = lineup[_cursor].Id;
					_cursor = 0;
				}
			}
			else
			{
				List<(string Label, Func<PsivResult> Run)> choices = EquipChoices(_pickMember);
				Move(input, choices.Count);
				if (input.Confirm && choices.Count > 0)
				{
					Report(choices[_cursor].Run());
					_cursor = 0;
				}
			}
			break;
		case "Save":
			int slots = _s.GameInfo().ManualSlots;
			Move(input, slots);
			if (input.Confirm)
			{
				Report(_s.SaveManual($"manual_{_cursor}"));
			}
			break;
		}
	}

	private List<(string Label, Func<PsivResult> Run)> EquipChoices(string memberId)
	{
		var choices = new List<(string, Func<PsivResult>)>();
		Member? member = _s.Party().FirstOrDefault(m => m.Id == memberId);
		if (member is null)
		{
			return choices;
		}
		foreach (InventoryEntry piece in _s.Inventory().Where(i => i.IsEquipment))
		{
			choices.Add(($"Equip {piece.Name} ({GameSession.NameOf(NameKind.EquipSlot, (int)piece.Slot)})", () => _s.Equip(memberId, piece.Id)));
		}
		for (int slot = 0; slot < member.Equipment.Count; slot++)
		{
			if (member.Equipment[slot].Length > 0)
			{
				var equipSlot = (EquipSlot)slot;
				choices.Add(($"Remove {member.EquipmentNames[slot]}", () => _s.Unequip(memberId, equipSlot)));
			}
		}
		return choices;
	}

	private void DrawMenu(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		Style.Window(s, 24, 56, s.Width - 48, s.Height - 72, "Menu");
		Style.Tabs(s, 48, 64, MenuTabs, _tab);
		string money = $"Money {_s.WorldInfo().Money}";
		s.Text(money, s.Width - 64 - s.Measure(money), 69, Style.Text);
		List<Member> lineup = _s.Party().Where(m => m.InLineup).ToList();
		int y = 124;
		switch (MenuTabs[_tab])
		{
		case "Party":
			if (_pickMember is null || _pickAction is not null)
			{
				DrawList(s, 56, y, lineup.Select(m => $"{m.Name}  Lv{m.Level}  HP {m.Hp}/{m.MaxHp}  TP {m.Tp}/{m.MaxTp}  EXP {m.Experience}" +
					string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList());
				if (_pickAction is not null)
				{
					s.Text($"Use {_s.DisplayName(_pickAction)} on whom?", 56, s.Height - 104, Style.Accent);
				}
			}
			else
			{
				Member m = lineup.First(x => x.Id == _pickMember);
				s.Text($"{m.Name}: STR {m.Strength}  MEN {m.Mental}  AGI {m.Agility}  DEX {m.Dexterity}  DEF {m.Defense}", 56, y, Style.Text);
				s.Text("Equipment: " + string.Join(", ", m.EquipmentNames.Where(n => n.Length > 0)), 56, y + 30, Style.Dim, Style.Small);
				var techniques = _s.MemberAbilities(_pickMember).Where(a => a.Field).ToList();
				s.Text(techniques.Count == 0 ? "No techniques to use here." : "Use a technique:", 56, y + 70, Style.Accent);
				DrawList(s, 56, y + 100, techniques.Select(a => $"{a.Name} ({a.Cost} TP)").ToList());
			}
			break;
		case "Items":
			var items = _s.Inventory().Where(i => !i.IsEquipment).ToList();
			DrawList(s, 56, y, items.Select(i => $"{i.Name} x{i.Count}" + (i.Field ? "" : "  (battle only)")).ToList(), _pickAction is null, _pickAction is null ? 0 : 520);
			if (_pickAction is not null)
			{
				DrawList(s, 620, y, lineup.Select(m => $"{m.Name}  HP {m.Hp}/{m.MaxHp}").ToList());
			}
			break;
		case "Equip":
			if (_pickMember is null)
			{
				DrawList(s, 56, y, lineup.Select(m => m.Name + ": " + string.Join(", ", m.EquipmentNames.Where(n => n.Length > 0))).ToList());
			}
			else
			{
				DrawList(s, 56, y, EquipChoices(_pickMember).Select(c => c.Label).ToList());
			}
			break;
		case "Save":
			var saves = _s.Saves() ?? [];
			int slots = _s.GameInfo().ManualSlots;
			DrawList(s, 56, y, Enumerable.Range(0, slots).Select(i =>
			{
				SaveSummary? existing = saves.FirstOrDefault(x => x.Slot == $"manual_{i}");
				return $"manual_{i}  " + (existing is null ? "(empty)" : existing.Damaged ? "(damaged)" : $"{existing.Area} {string.Join(" ", existing.Party)}");
			}).ToList());
			break;
		case "Quests":
			DrawQuestLog(s, 56, y);
			break;
		case "Codex":
			int cy = y;
			foreach (CodexEntry entry in _s.Encyclopedia())
			{
				s.Text(entry.Title, 56, cy, Style.Accent);
				cy += Style.LineHeight;
				foreach (string l in Style.Wrap(s, entry.Text, s.Width - 150, Style.Small))
				{
					s.Text(l, 76, cy, Style.Text, Style.Small);
					cy += 22;
				}
				cy += 10;
			}
			break;
		case "Log":
			DrawTextLog(s, 56, y, s.Height - y - 100);
			break;
		}
		s.Text($"{Hint(GameAction.PagePrev)}/{Hint(GameAction.PageNext)} tab   {Hint(GameAction.Up)}/{Hint(GameAction.Down)} choose   {Hint(GameAction.Confirm)} use   {Hint(GameAction.Back)} back", 56, s.Height - 66, Style.Dim, Style.Small);
	}

	private void DrawQuestLog(IDrawSurface s, int x, int y)
	{
		QuestLog log = _s.QuestLog();
		s.Text("Objective", x, y, Style.Accent);
		s.Text(log.Objective.Length == 0 ? "-" : log.Objective, x + 20, y + 30, Style.Text);
		s.Text("Jobs", x, y + 80, Style.Accent);
		for (int i = 0; i < log.Jobs.Count; i++)
		{
			s.Text($"{log.Jobs[i].Title} ({GameSession.NameOf(NameKind.JobState, (int)log.Jobs[i].State)})", x + 20, y + 110 + i * Style.LineHeight, Style.Text);
		}
	}

	private void DrawTextLog(IDrawSurface s, int x, int y, int height)
	{
		IReadOnlyList<string> pages = _s.TextLog();
		int lines = Math.Max(1, height / 22);
		var visible = pages.Skip(Math.Max(0, pages.Count - lines)).ToList();
		for (int i = 0; i < visible.Count; i++)
		{
			s.Text(visible[i], x, y + i * 22, Style.Text, Style.Small);
		}
	}
}
