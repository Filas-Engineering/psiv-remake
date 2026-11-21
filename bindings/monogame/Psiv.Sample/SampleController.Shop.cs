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
			_cursor = 0;
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
		Move(input, list.Count);
		if (input.Confirm && list.Count > 0)
		{
			Report(_tab == 0 ? _s.Buy(list[_cursor].Id) : _s.Sell(list[_cursor].Id));
		}
	}

	private void DrawShop(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int x = 80;
		int w = s.Width - 160;
		Style.Window(s, x, 50, w, s.Height - 100, "Shop");
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		Style.Tabs(s, x + 24, 76, ["Buy", "Sell"], _tab);
		string money = $"Money {_s.WorldInfo().Money}";
		s.Text(money, x + w - 40 - s.Measure(money), 81, Style.Text);
		s.Image(art.Stall, x + w - 120, 120, 80, 80, Color.White);
		int priceX = x + 560;
		int countX = x + 680;
		s.Text(_tab == 0 ? "Price" : "Each", priceX, 128, Style.Dim, Style.Small);
		s.Text("Have", countX, 128, Style.Dim, Style.Small);
		IReadOnlyList<(string Name, int Price, int Count)> rows = _tab == 0
			? shop.Stock.Select(i => (i.Name, i.Price, i.Count)).ToList()
			: _s.Inventory().Where(i => i.Price >= 0).Select(i => (i.Name, i.Price * shop.SellPercent / 100, i.Count)).ToList();
		for (int i = 0; i < rows.Count; i++)
		{
			int y = 156 + i * 30;
			bool on = i == _cursor;
			Style.ListRow(s, rows[i].Name, x + 40, y, countX + 60 - x - 40, on);
			string price = rows[i].Price.ToString(System.Globalization.CultureInfo.InvariantCulture);
			s.Text(price, priceX + 40 - s.Measure(price), y, on ? Style.Accent : Style.Text);
			s.Text($"x{rows[i].Count}", countX, y, Style.Dim);
		}
		if (rows.Count == 0)
		{
			s.Text(_tab == 0 ? "Nothing for sale." : "Nothing to sell.", x + 54, 156, Style.Dim);
		}
		DrawToasts(s, x + 24, s.Height - 92);
		s.Text($"{Hint(GameAction.PagePrev)}/{Hint(GameAction.PageNext)} buy or sell   {Hint(GameAction.Confirm)} one   {Hint(GameAction.Back)} leave", x + 24, s.Height - 86, Style.Dim, Style.Small);
	}
}
