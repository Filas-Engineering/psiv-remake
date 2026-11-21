using System.Globalization;

using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// The sample's own words come from the game's text tables (keys under ui.), in the profile's text language.
public sealed partial class SampleController
{
	// Keys with a word for a name; letters, digits and function keys show as themselves, and other keys by their MonoGame name.
	private static readonly HashSet<Keys> NamedKeys =
	[
		Keys.Back, Keys.Delete, Keys.Down, Keys.End, Keys.Enter, Keys.Escape, Keys.Home, Keys.Insert, Keys.Left, Keys.LeftAlt, Keys.LeftControl,
		Keys.LeftShift, Keys.PageDown, Keys.PageUp, Keys.Right, Keys.RightAlt, Keys.RightControl, Keys.RightShift, Keys.Space, Keys.Tab, Keys.Up,
	];

	// Called with each key the text table of the profile's language lacks; the key itself is shown in its place.
	public Action<string>? MissingText { get; set; }

	// The bitmap font the surface was given last, null when none (a run without bitmap fonts never gives one).
	private BitmapFont? _bitmapFont;
	// The dialogue box's lines as the last frame wrapped them: how many, and each one's measured width.
	private string _probeWrapped = "-";

	// Text in the asset directory's bitmap font for the text language and `role` from now on, if it gives one. Given every
	// time once a font was in use, since the developer previewer draws through the same surface with its own.
	private void UseTextRole(IDrawSurface s, TextRole role)
	{
		BitmapFont? font = _art?.BitmapFontFor(_s.Profile().TextLanguage, role);
		if (font is not null || _bitmapFont is not null)
		{
			s.UseBitmapFont(font);
		}
		_bitmapFont = font;
	}

	// The step from one line of wrapped text to the next at a text size: the bitmap font's line height when one draws the
	// text, else the sample's own.
	private int LineStep(int size) => _bitmapFont?.LineStep((int)MathF.Round(size * _scale)) ?? Px(Style.LineHeight);

	// The bitmap font for each role in the text language, as `ROLE FILE` or `ROLE -`.
	private string BitmapFontProbe() => string.Join(", ", Enum.GetValues<TextRole>().Select(r =>
		$"{Snake(r.ToString())} {_art?.BitmapFontFor(_s.Profile().TextLanguage, r)?.Key ?? "-"}"));

	private string T(string key, params object[] parameters)
	{
		string text = _s.FormatText(key, [.. parameters.Select(p => Convert.ToString(p, CultureInfo.InvariantCulture) ?? "")]);
		if (text == key)
		{
			MissingText?.Invoke(key);
		}
		return text;
	}

	// The library's name of an enumeration value in the text language: Element 1 is heat, so ui.element.heat.
	private string T(NameKind kind, int value) => T($"ui.{Snake(kind.ToString())}.{GameSession.NameOf(kind, value)}");

	// A built-in element's word from the sample's table; an element the data declares by the game's own name for it.
	private string ElementText(Element element) => element <= Element.Holy ? T(NameKind.Element, (int)element)
		: _s.Elements().FirstOrDefault(e => e.Index == element)?.Name ?? element.ToString();

	private static string Snake(string name) => string.Concat(name.Select((c, i) => (i > 0 && char.IsUpper(c) ? "_" : "") + char.ToLowerInvariant(c)));

	private string KeyText(Keys key) => NamedKeys.Contains(key) ? T("ui.key." + Snake(key.ToString())) : Bindings.KeyName(key);

	private string ButtonText(Buttons button) => T("ui.button." + Snake(button.ToString()));

	private string ActionText(GameAction action) => T("ui.action." + Snake(action.ToString()));

	private string AreaKindText(AreaKind kind) => T(NameKind.AreaKind, (int)kind);

	private (string Space, string Erase, string Done) PadLabels() => (T("ui.name_pad.space"), T("ui.name_pad.erase"), T("ui.name_pad.done"));

	private string ListText(IEnumerable<string> items) => string.Join(T("ui.common.separator"), items);

	private string WearersText(IReadOnlyList<string> names) => T("ui.equip.can_wear", names.Count == 0 ? T("ui.equip.nobody") : ListText(names));

	private string MemberRow(Member m) => T("ui.menu.member_row", m.Name, m.Level, m.Hp, m.MaxHp, m.Tp, m.MaxTp);

	private string BindingsText(IEnumerable<string> names) => ListText(names) is { Length: > 0 } joined ? joined : "-";

	private string OnOffText(bool on) => T(on ? "ui.common.on" : "ui.common.off");
}
