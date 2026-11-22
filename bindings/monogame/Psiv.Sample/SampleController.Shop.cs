using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	// The shop layout's `steps` 1: a buy or sell choice, the list, a quantity with its total and a confirm, then the result;
	// Back goes a step back, and leaves from the choice. Without it the two tabs as before.
	private enum ShopStep { Choose, List, Quantity, Result }

	private ShopStep _shopStep;
	private int _shopChoice;
	private int _shopCount = 1;
	// The last trade's outcome in the steps (the session's notice or refusal) and whether it went through.
	private string _shopResult = "";
	private bool _shopResultOk;

	private bool ShopSteps => Ui.Param("shop", "steps", UiData(), 0) != 0;

	// A layout that shows `result_text` takes the trades' outcomes there instead of the notices.
	private bool ShopResultShown => Layout.Screen("shop").Reads("result_text");

	private List<InventoryEntry> ShopRows(ShopInfo shop) => _tab == 0 ? shop.Stock.ToList() : _s.Inventory().Where(i => i.Price >= 0).ToList();

	private int ShopPrice(ShopInfo shop, InventoryEntry entry) => _tab == 0 ? entry.Price : entry.Price * shop.SellPercent / 100;

	// The most of the row one trade may take: what the money buys (at least 1, so a refusal can show), or what is held.
	private int ShopMost(ShopInfo shop, InventoryEntry entry) =>
		Math.Clamp(_tab == 0 ? _s.WorldInfo().Money / Math.Max(1, entry.Price) : entry.Count, 1, 99);

	private void UpdateShop(ActionInput input)
	{
		ShopInfo? shop = _s.Shop();
		if (ShopSteps)
		{
			UpdateShopSteps(input, shop);
			return;
		}
		if (input.Pressed(GameAction.PagePrev) || input.Pressed(GameAction.PageNext))
		{
			_tab = 1 - _tab;
			_list.Reset();
		}
		if (input.Back)
		{
			_keeperPoint = null;
			Report(_s.CloseMenu());
			return;
		}
		KeeperOpened(shop?.Id, "welcome");
		if (shop is null)
		{
			return;
		}
		var list = ShopRows(shop);
		_list.Move(input, list.Count);
		if (input.Confirm && list.Count > 0)
		{
			PsivResult traded = _tab == 0 ? _s.Buy(list[_list.Cursor].Id) : _s.Sell(list[_list.Cursor].Id);
			RememberTrade(shop, list[_list.Cursor], 1, traded);
			KeeperAnswered(traded);
			ShopReport(traded);
		}
	}

	// A refusal to the layout's `result_text` when it shows one, else to the notices; a trade's own notice follows the
	// same way (TakeNotices).
	private void ShopReport(PsivResult traded)
	{
		_shopResult = "";
		_shopResultOk = traded.Ok;
		if (traded.Error is { } error && ShopResultShown)
		{
			_shopResult = error.Text;
			_log("notice: " + error.Text);
			_refused = true;
			return;
		}
		Report(traded);
	}

	private void UpdateShopSteps(ActionInput input, ShopInfo? shop)
	{
		if (shop is null || shop.Id != _keeperPoint)
		{
			_shopStep = ShopStep.Choose;
			_shopChoice = 0;
		}
		KeeperOpened(shop?.Id, "welcome");
		if (shop is null)
		{
			return;
		}
		List<InventoryEntry> rows = ShopRows(shop);
		switch (_shopStep)
		{
		case ShopStep.Choose:
			if (input.Back)
			{
				_keeperPoint = null;
				Report(_s.CloseMenu());
				return;
			}
			if (input.Pressed(GameAction.Up) || input.Pressed(GameAction.Down))
			{
				_shopChoice = 1 - _shopChoice;
			}
			if (input.Confirm)
			{
				_tab = _shopChoice;
				_list.Reset();
				_shopStep = ShopStep.List;
			}
			break;
		case ShopStep.List:
			if (input.Back)
			{
				_shopStep = ShopStep.Choose;
				_keeperMoment = "welcome";
				return;
			}
			_list.Move(input, rows.Count);
			if (input.Confirm && rows.Count > 0)
			{
				_shopCount = 1;
				_shopStep = ShopStep.Quantity;
				_keeperMoment = "confirm";
			}
			break;
		case ShopStep.Quantity when rows.Count == 0:
			_shopStep = ShopStep.List;
			break;
		case ShopStep.Quantity:
			InventoryEntry entry = rows[Math.Clamp(_list.Cursor, 0, rows.Count - 1)];
			int most = ShopMost(shop, entry);
			if (input.Back)
			{
				_shopStep = ShopStep.List;
				_keeperMoment = "welcome";
				return;
			}
			int by = input.Pressed(GameAction.Right) || input.Pressed(GameAction.Up) ? 1 : input.Pressed(GameAction.Left) || input.Pressed(GameAction.Down) ? -1 : 0;
			_shopCount = (_shopCount - 1 + by + most) % most + 1;
			if (input.Confirm)
			{
				PsivResult traded = _tab == 0 ? _s.Buy(entry.Id, _shopCount) : _s.Sell(entry.Id, _shopCount);
				RememberTrade(shop, entry, _shopCount, traded);
				KeeperAnswered(traded);
				ShopReport(traded);
				_shopStep = ShopStep.Result;
			}
			break;
		case ShopStep.Result:
			if (input.Confirm || input.Back)
			{
				_shopStep = ShopStep.List;
				_keeperMoment = "welcome";
				_list.Move(input, ShopRows(shop).Count);
			}
			break;
		}
	}

	private void DrawShop(IDrawSurface s, IDrawSurface t, Art art)
	{
		ShopInfo? shop = _s.Shop();
		KeeperOpened(shop?.Id, "welcome");
		UiContext ctx = UiData()
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art, "shop")))
			.Set("title", shop is { } open ? T(NameKind.ShopKind, (int)open.Kind) : T("ui.shop.title"))
			.Set("open", shop is not null)
			.Set("tab", _tab)
			.Set("money_value", _s.WorldInfo().Money)
			.Set("keeper", shop is null ? -1 : Keeper(art, shop.Id))
			.Set("voice_line", "");
		List<InventoryEntry> entries = shop is null ? [] : ShopRows(shop);
		if (shop is not null)
		{
			ctx.Set("tabs", new List<string> { T("ui.shop.buy"), T("ui.shop.sell") })
				.Set("money", T("ui.common.money", _s.WorldInfo().Money))
				.Set("stall", AreaTiles(art).Stall)
				.Set("hint", T("ui.shop.hint", Hint(GameAction.PagePrev), Hint(GameAction.PageNext), Hint(GameAction.Confirm), Hint(GameAction.Back)))
				.Set("toasts", (UiSlot)((x, y, _, _) => DrawToasts(t, x, y, right: true)));
		}
		ctx.Set("rows", entries.Select(i => new UiContext(ctx).Set("text", i.Name).Set("price", ShopPrice(shop!, i)).Set("have", i.Count)).ToList())
			.Set("list", _list)
			.Set("compare", "");
		InventoryEntry? at = entries.Count > 0 ? entries[Math.Clamp(_list.Cursor, 0, entries.Count - 1)] : null;
		bool steps = ShopSteps;
		ctx.Set("voice_line", shop is null ? "" : ShopVoiceLine(shop, at, steps ? _shopCount : 1));
		ctx.Set("step", !steps ? "tabs" : _shopStep.ToString().ToLowerInvariant())
			.Set("choices", new List<string> { T("ui.shop.buy"), T("ui.shop.sell") })
			.Set("choice_cursor", _shopChoice)
			.Set("quantity", steps ? _shopCount : 1)
			.Set("quantity_max", shop is not null && at is not null ? ShopMost(shop, at) : 0)
			.Set("unit_price", shop is not null && at is not null ? ShopPrice(shop, at) : 0)
			.Set("total", shop is not null && at is not null ? ShopPrice(shop, at) * (steps ? _shopCount : 1) : 0)
			.Set("result_text", _shopResult)
			.Set("result_ok", _shopResultOk);
		if (entries.Count > 0)
		{
			InventoryEntry selected = entries[Math.Clamp(_list.Cursor, 0, entries.Count - 1)];
			SetDescription(ctx, DescriptionOf(selected));
			if (selected.IsEquipment && LineupMembers().FirstOrDefault() is { } leader && _s.EquipPreview(leader.Id, selected.Id) is { } preview)
			{
				ctx.Set("compare", $"{BonusText(preview.Bonus)}   " + WearersText(preview.WearerNames));
			}
		}
		Ui.Draw("shop", s, t, ctx);
		if (entries.Count > 0)
		{
			_probeSelected = entries[_list.Cursor].Name;
		}
	}

	// The portrait the assets' `keepers` give a shop or rest point, -1 without one.
	private int Keeper(Art art, string point)
	{
		string? keeper = _assets?.Manifest.Keepers.GetValueOrDefault(point);
		_probeKeeper = keeper ?? "-";
		return keeper is null ? -1 : art.Portrait(keeper, keeper);
	}

	// The keeper the last shop or inn frame showed, `-` for none.
	private string _probeKeeper = "-";

	// The shop or rest point open and what its keeper says now: `welcome` (a shop opening), `confirm` (a rest point
	// opening), `thanks` after a trade, `cannot_afford` after a refusal for money.
	private string? _keeperPoint;
	private string _keeperMoment = "welcome";

	private void KeeperOpened(string? point, string moment)
	{
		if (point != _keeperPoint)
		{
			_keeperPoint = point;
			_keeperMoment = moment;
		}
	}

	private void KeeperAnswered(PsivResult result) =>
		_keeperMoment = result.Ok ? "thanks" : result.Error!.Key == "game.refuse.no_money" ? "cannot_afford" : _keeperMoment;

	// The keeper's line, the first key the tables have of: VOICE.MOMENT_SIDE and VOICE.MOMENT when the point has a voice,
	// then ui.KIND.MOMENT_SIDE and ui.KIND.MOMENT. SIDE (`buy` or `sell`) is given for a shop's `confirm` and `thanks`.
	private string VoiceLine(string voice, string kind, string moment, string side, params object[] parameters)
	{
		string[] values = [.. parameters.Select(p => Convert.ToString(p, System.Globalization.CultureInfo.InvariantCulture) ?? "")];
		IEnumerable<string> prefixes = voice.Length > 0 ? [voice, $"ui.{kind}"] : [$"ui.{kind}"];
		foreach (string prefix in prefixes)
		{
			foreach (string key in side.Length > 0 ? new[] { $"{prefix}.{moment}_{side}", $"{prefix}.{moment}" } : [$"{prefix}.{moment}"])
			{
				if (_s.FormatText(key, values) is var text && text != key)
				{
					_probeVoice = key;
					return text;
				}
			}
		}
		_probeVoice = $"ui.{kind}.{moment}";
		return T($"ui.{kind}.{moment}", parameters);
	}

	// A shop keeper's line, with {0} the shop's kind word, {1} the item's name, {2} the quantity, {3} the total price and
	// {4} the price of one: after a trade those of the trade, while confirming those of the row and count chosen, empty
	// when there are none (the welcome).
	private string ShopVoiceLine(ShopInfo shop, InventoryEntry? at, int count)
	{
		string side = _keeperMoment is "confirm" or "thanks" ? (_tab == 0 ? "buy" : "sell") : "";
		object[] values = _keeperMoment == "thanks" && _voiceTrade is { } traded ? [T(NameKind.ShopKind, (int)shop.Kind), traded.Name, traded.Count, traded.Count * traded.Unit, traded.Unit]
			: _keeperMoment == "confirm" && at is not null ? [T(NameKind.ShopKind, (int)shop.Kind), at.Name, count, ShopPrice(shop, at) * count, ShopPrice(shop, at)]
			: [T(NameKind.ShopKind, (int)shop.Kind), "", "", "", ""];
		return VoiceLine(shop.Voice, "shop", _keeperMoment, side, values);
	}

	// The last trade, for the keeper's thanks: the item's name, the count and the price of one.
	private (string Name, int Count, int Unit)? _voiceTrade;

	private void RememberTrade(ShopInfo shop, InventoryEntry entry, int count, PsivResult traded) =>
		_voiceTrade = traded.Ok ? (entry.Name, count, ShopPrice(shop, entry)) : _voiceTrade;

	// The text key of the keeper's line the last shop or inn frame showed.
	private string _probeVoice = "-";

	// The description fields of a highlighted row, at most two small lines; the session gives "" when the data has none.
	private UiContext SetDescription(UiContext ctx, string? text)
	{
		_probeDescription = string.IsNullOrEmpty(text) ? "-" : text;
		return ctx.Set("description", string.IsNullOrEmpty(text) ? T("ui.common.no_description") : text).Set("description_missing", string.IsNullOrEmpty(text));
	}
}
