using System.Globalization;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateShop(ActionInput input)
	{
		if (input.Pressed(GameAction.PagePrev) || input.Pressed(GameAction.PageNext))
		{
			_tab = 1 - _tab;
			_list.Reset();
		}
		if (input.Back)
		{
			Report(_s.CloseMenu());
			return;
		}
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		var list = _tab == 0 ? shop.Stock.ToList() : _s.Inventory().Where(i => i.Price >= 0).ToList();
		_list.Move(input, list.Count);
		if (input.Confirm && list.Count > 0)
		{
			Report(_tab == 0 ? _s.Buy(list[_list.Cursor].Id) : _s.Sell(list[_list.Cursor].Id));
		}
	}

	private void DrawShop(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int x = 80;
		int w = s.Width - 160;
		Style.Window(s, x, 50, w, s.Height - 100, T("ui.shop.title"));
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		Widgets.Tabs(s, x + 24, 76, w - 220, [T("ui.shop.buy"), T("ui.shop.sell")], _tab, Px);
		string money = T("ui.common.money", _s.WorldInfo().Money);
		s.Text(money, x + w - 40 - s.Measure(money), 81, Style.Text);
		s.Image(AreaTiles(art).Stall, x + w - 120, 120, 80, 80, Color.White);
		int priceX = x + 560;
		int countX = x + 680;
		int headY = 76 + Px(52);
		s.Text(T(_tab == 0 ? "ui.shop.price" : "ui.shop.each"), priceX, headY, Style.Dim, Style.Small);
		s.Text(T("ui.shop.have"), countX, headY, Style.Dim, Style.Small);
		List<InventoryEntry> entries = _tab == 0 ? shop.Stock.ToList() : _s.Inventory().Where(i => i.Price >= 0).ToList();
		IReadOnlyList<(string Name, int Price, int Count)> rows = entries.Select(i => (i.Name, _tab == 0 ? i.Price : i.Price * shop.SellPercent / 100, i.Count)).ToList();
		int top = headY + Px(28);
		int listW = countX + 80 - x - 40;
		int descH = Px(80);
		_list.Draw(s, x + 40, top, listW, s.Height - 140 - top - descH, Px(30), rows.Count, (i, y, on) =>
		{
			Style.ListRow(s, Style.Fit(s, rows[i].Name, priceX - x - 100), x + 40, y, listW - 20, on, height: Px(28));
			string price = rows[i].Price.ToString(CultureInfo.InvariantCulture);
			s.Text(price, priceX + 40 - s.Measure(price), y, on ? Style.Accent : Style.Text);
			s.Text(T("ui.common.count", rows[i].Count), countX, y, Style.Dim);
		});
		if (rows.Count > 0)
		{
			_probeSelected = rows[_list.Cursor].Name;
			InventoryEntry selected = entries[Math.Clamp(_list.Cursor, 0, entries.Count - 1)];
			int dy = s.Height - 140 - descH + Px(8);
			s.Fill(x + 40, dy - Px(8), listW - 20, 1, Style.PanelEdge * 0.4f);
			DrawDescription(s, DescriptionOf(selected), x + 54, dy, w - 120);
			if (selected.IsEquipment && LineupMembers().FirstOrDefault() is { } leader && _s.EquipPreview(leader.Id, selected.Id) is { } preview)
			{
				s.Text(Style.Fit(s, $"{BonusText(preview.Bonus)}   " + WearersText(preview.WearerNames), w - 120, Style.Small),
					x + 54, dy + Px(48), Style.Dim, Style.Small);
			}
		}
		else
		{
			s.Text(T(_tab == 0 ? "ui.shop.nothing_for_sale" : "ui.shop.nothing_to_sell"), x + 54, top, Style.Dim);
		}
		DrawToasts(s, x + 24, s.Height - 140 - descH, right: true);
		Widgets.Hints(s, x + 24, s.Height - 50 - Px(36), w - 48,
			T("ui.shop.hint", Hint(GameAction.PagePrev), Hint(GameAction.PageNext), Hint(GameAction.Confirm), Hint(GameAction.Back)));
	}
}
