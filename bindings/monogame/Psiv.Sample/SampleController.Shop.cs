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
		Style.Window(s, x, 50, w, s.Height - 100, "Shop");
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		Widgets.Tabs(s, x + 24, 76, w - 220, ["Buy", "Sell"], _tab, Px);
		string money = $"Money {_s.WorldInfo().Money}";
		s.Text(money, x + w - 40 - s.Measure(money), 81, Style.Text);
		s.Image(art.Stall, x + w - 120, 120, 80, 80, Color.White);
		int priceX = x + 560;
		int countX = x + 680;
		int headY = 76 + Px(52);
		s.Text(_tab == 0 ? "Price" : "Each", priceX, headY, Style.Dim, Style.Small);
		s.Text("Have", countX, headY, Style.Dim, Style.Small);
		IReadOnlyList<(string Name, int Price, int Count)> rows = _tab == 0
			? shop.Stock.Select(i => (i.Name, i.Price, i.Count)).ToList()
			: _s.Inventory().Where(i => i.Price >= 0).Select(i => (i.Name, i.Price * shop.SellPercent / 100, i.Count)).ToList();
		int top = headY + Px(28);
		int listW = countX + 80 - x - 40;
		_list.Draw(s, x + 40, top, listW, s.Height - 140 - top, Px(30), rows.Count, (i, y, on) =>
		{
			Style.ListRow(s, Style.Fit(s, rows[i].Name, priceX - x - 100), x + 40, y, listW - 20, on, height: Px(28));
			string price = rows[i].Price.ToString(CultureInfo.InvariantCulture);
			s.Text(price, priceX + 40 - s.Measure(price), y, on ? Style.Accent : Style.Text);
			s.Text($"x{rows[i].Count}", countX, y, Style.Dim);
		});
		if (rows.Count > 0)
		{
			_probeSelected = rows[_list.Cursor].Name;
		}
		else
		{
			s.Text(_tab == 0 ? "Nothing for sale." : "Nothing to sell.", x + 54, top, Style.Dim);
		}
		DrawToasts(s, x + 24, s.Height - 56 - Px(36));
		Widgets.Hints(s, x + 24, s.Height - 50 - Px(36), w - 48,
			$"{Hint(GameAction.PagePrev)}/{Hint(GameAction.PageNext)} buy or sell   {Hint(GameAction.Confirm)} one   {Hint(GameAction.Back)} leave");
	}
}
