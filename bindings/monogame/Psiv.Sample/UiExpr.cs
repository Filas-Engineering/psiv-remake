using System.Globalization;

namespace Psiv.Sample;

// A value of the layout language: a whole number, a decimal, a truth value or a string.
public readonly record struct UiValue(object Raw)
{
	public static readonly UiValue Zero = new(0L);
	public static readonly UiValue Empty = new("");

	public static UiValue Of(object? value) => value switch
	{
		null => Empty,
		int i => new((long)i),
		long l => new(l),
		float f => new((double)f),
		double d => new(d),
		bool b => new(b),
		string s => new(s),
		UiValue v => v,
		_ => new(value),
	};

	public bool IsNumber => Raw is long or double;

	public long Int => Raw switch
	{
		long l => l,
		double d => (long)d,
		bool b => b ? 1 : 0,
		string s => long.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out long n) ? n : 0,
		_ => 0,
	};

	public double Number => Raw switch
	{
		long l => l,
		double d => d,
		bool b => b ? 1 : 0,
		_ => 0,
	};

	public bool Truth => Raw switch
	{
		bool b => b,
		long l => l != 0,
		double d => d != 0,
		string s => s.Length > 0,
		System.Collections.ICollection c => c.Count > 0,
		null => false,
		_ => true,
	};

	public string Text => Raw switch
	{
		string s => s,
		long l => l.ToString(CultureInfo.InvariantCulture),
		double d => d.ToString(CultureInfo.InvariantCulture),
		bool b => b ? "true" : "false",
		_ => Raw?.ToString() ?? "",
	};
}

// What an expression reads its names and window references from while it is evaluated.
public interface IUiScope
{
	// A field, a built-in variable (W, H, i...) or a window's edge (`list.bottom`); false when nothing has that name.
	bool TryGet(string name, out UiValue value);
	// Whole pixels at the profile's text size: t(30) is 30 at 100% and 45 at 150%.
	int TextScaled(int value);
	// The width of text at a size on the screen's text-size surface, for windows sized by their content.
	int Measure(string text, int size);
	// The same on the surface at 100% text, for elements drawn with `scaled: false`.
	int MeasureUnscaled(string text, int size);
}

// The expression language of layout files: numbers, 'strings', names, + - * / %, comparisons, && || !, ?:, and the
// functions t(n), min, max, abs, clamp, len, measure(text, size), measure_unscaled(text, size), fit(text, width, size) and if. Whole
// numbers divide as C# does (toward zero) so a layout can say
// exactly what the code it replaces computed.
public sealed class UiExpr
{
	private readonly Node _root;

	public string Source { get; }

	// Every name the expression reads, for the check.
	public IReadOnlyList<string> Names { get; }

	private UiExpr(string source, Node root, List<string> names)
	{
		Source = source;
		_root = root;
		Names = names;
	}

	public static UiExpr Constant(long value) => new(value.ToString(CultureInfo.InvariantCulture), new Lit(new UiValue(value)), []);

	// Null with `error` set when the text does not parse.
	public static UiExpr? Parse(string source, out string? error)
	{
		var names = new List<string>();
		try
		{
			var p = new Parser(source, names);
			Node root = p.Expression();
			p.End();
			error = null;
			return new UiExpr(source, root, names);
		}
		catch (FormatException e)
		{
			error = e.Message;
			return null;
		}
	}

	public UiValue Eval(IUiScope scope) => _root.Eval(scope);

	public int Int(IUiScope scope) => (int)Eval(scope).Int;

	public override string ToString() => Source;

	private abstract class Node
	{
		public abstract UiValue Eval(IUiScope scope);
	}

	private sealed class Lit(UiValue value) : Node
	{
		public override UiValue Eval(IUiScope scope) => value;
	}

	private sealed class Name(string name) : Node
	{
		public override UiValue Eval(IUiScope scope) => scope.TryGet(name, out UiValue v) ? v : UiValue.Zero;
	}

	private sealed class Unary(char op, Node operand) : Node
	{
		public override UiValue Eval(IUiScope scope)
		{
			UiValue v = operand.Eval(scope);
			return op == '!' ? new(!v.Truth) : v.Raw is double d ? new(-d) : new(-v.Int);
		}
	}

	private sealed class Ternary(Node test, Node yes, Node no) : Node
	{
		public override UiValue Eval(IUiScope scope) => test.Eval(scope).Truth ? yes.Eval(scope) : no.Eval(scope);
	}

	private sealed class Binary(string op, Node left, Node right) : Node
	{
		public override UiValue Eval(IUiScope scope)
		{
			if (op == "&&")
			{
				return new(left.Eval(scope).Truth && right.Eval(scope).Truth);
			}
			if (op == "||")
			{
				return new(left.Eval(scope).Truth || right.Eval(scope).Truth);
			}
			UiValue a = left.Eval(scope), b = right.Eval(scope);
			if (op is "==" or "!=")
			{
				bool same = a.IsNumber && b.IsNumber ? a.Number == b.Number : a.Raw is bool || b.Raw is bool ? a.Truth == b.Truth : a.Text == b.Text;
				return new(op == "==" ? same : !same);
			}
			if (op == "+" && (a.Raw is string || b.Raw is string))
			{
				return new(a.Text + b.Text);
			}
			bool real = a.Raw is double || b.Raw is double;
			return op switch
			{
				"<" => new(a.Number < b.Number),
				"<=" => new(a.Number <= b.Number),
				">" => new(a.Number > b.Number),
				">=" => new(a.Number >= b.Number),
				"+" => real ? new(a.Number + b.Number) : new(a.Int + b.Int),
				"-" => real ? new(a.Number - b.Number) : new(a.Int - b.Int),
				"*" => real ? new(a.Number * b.Number) : new(a.Int * b.Int),
				// Division by zero gives 0: a layout can never stop the screen.
				"/" => real ? new(b.Number == 0 ? 0 : a.Number / b.Number) : new(b.Int == 0 ? 0 : a.Int / b.Int),
				"%" => real ? new(b.Number == 0 ? 0 : a.Number % b.Number) : new(b.Int == 0 ? 0 : a.Int % b.Int),
				_ => UiValue.Zero,
			};
		}
	}

	private sealed class Call(string name, List<Node> args) : Node
	{
		public override UiValue Eval(IUiScope scope)
		{
			UiValue Arg(int i) => i < args.Count ? args[i].Eval(scope) : UiValue.Zero;
			switch (name)
			{
			case "t":
				return new((long)scope.TextScaled((int)Arg(0).Int));
			case "if":
				return Arg(0).Truth ? Arg(1) : Arg(2);
			case "measure":
				return new((long)scope.Measure(Arg(0).Text, (int)Arg(1).Int));
			case "measure_unscaled":
				return new((long)scope.MeasureUnscaled(Arg(0).Text, (int)Arg(1).Int));
			case "len":
				return new((long)(Arg(0).Raw switch { string s => s.Length, System.Collections.ICollection c => c.Count, _ => 0 }));
			case "abs":
				return Arg(0).Raw is double d ? new(Math.Abs(d)) : new(Math.Abs(Arg(0).Int));
			case "fit":
				return new(Fit(scope, Arg(0).Text, (int)Arg(1).Int, (int)Arg(2).Int));
			case "starts_with":
				return new(Arg(0).Text.StartsWith(Arg(1).Text, StringComparison.Ordinal));
			}
			List<UiValue> values = [.. args.Select(a => a.Eval(scope))];
			bool real = values.Any(v => v.Raw is double);
			switch (name)
			{
			case "min":
				return real ? new(values.Min(v => v.Number)) : new(values.Min(v => v.Int));
			case "max":
				return real ? new(values.Max(v => v.Number)) : new(values.Max(v => v.Int));
			case "clamp":
				return real ? new(Math.Clamp(values[0].Number, values[1].Number, Math.Max(values[1].Number, values[2].Number)))
					: new(Math.Clamp(values[0].Int, values[1].Int, Math.Max(values[1].Int, values[2].Int)));
			}
			return UiValue.Zero;
		}

		// The text cut to fit the width with an ellipsis, as Style.Fit cuts it.
		private static string Fit(IUiScope scope, string text, int width, int size)
		{
			if (scope.Measure(text, size) <= width)
			{
				return text;
			}
			while (text.Length > 0 && scope.Measure(text + "...", size) > width)
			{
				text = text[..^1];
			}
			return text + "...";
		}
	}

	public static readonly Dictionary<string, int> Arity = new() { ["t"] = 1, ["if"] = 3, ["len"] = 1, ["abs"] = 1, ["min"] = -2, ["max"] = -2, ["clamp"] = 3, ["measure"] = 2, ["measure_unscaled"] = 2, ["fit"] = 3, ["starts_with"] = 2 };

	private sealed class Parser(string text, List<string> names)
	{
		private int _at;

		private FormatException Error(string what) => new($"{what} at character {_at + 1} of '{text}'");

		private void Skip()
		{
			while (_at < text.Length && char.IsWhiteSpace(text[_at]))
			{
				_at++;
			}
		}

		private bool Eat(string token)
		{
			Skip();
			if (string.CompareOrdinal(text, _at, token, 0, token.Length) == 0)
			{
				// "<" must not take the first character of "<=", nor "!" that of "!=".
				if (token.Length == 1 && _at + 1 < text.Length && "<>!=".Contains(token[0]) && text[_at + 1] == '=')
				{
					return false;
				}
				_at += token.Length;
				return true;
			}
			return false;
		}

		public void End()
		{
			Skip();
			if (_at < text.Length)
			{
				throw Error($"unexpected '{text[_at]}'");
			}
		}

		public Node Expression()
		{
			Node test = Or();
			if (Eat("?"))
			{
				Node yes = Expression();
				if (!Eat(":"))
				{
					throw Error("expected ':'");
				}
				return new Ternary(test, yes, Expression());
			}
			return test;
		}

		private Node Or()
		{
			Node n = And();
			while (Eat("||"))
			{
				n = new Binary("||", n, And());
			}
			return n;
		}

		private Node And()
		{
			Node n = Compare();
			while (Eat("&&"))
			{
				n = new Binary("&&", n, Compare());
			}
			return n;
		}

		private Node Compare()
		{
			Node n = Add();
			foreach (string op in new[] { "==", "!=", "<=", ">=", "<", ">" })
			{
				if (Eat(op))
				{
					return new Binary(op, n, Add());
				}
			}
			return n;
		}

		private Node Add()
		{
			Node n = Mul();
			while (true)
			{
				if (Eat("+"))
				{
					n = new Binary("+", n, Mul());
				}
				else if (Eat("-"))
				{
					n = new Binary("-", n, Mul());
				}
				else
				{
					return n;
				}
			}
		}

		private Node Mul()
		{
			Node n = UnaryNode();
			while (true)
			{
				if (Eat("*"))
				{
					n = new Binary("*", n, UnaryNode());
				}
				else if (Eat("/"))
				{
					n = new Binary("/", n, UnaryNode());
				}
				else if (Eat("%"))
				{
					n = new Binary("%", n, UnaryNode());
				}
				else
				{
					return n;
				}
			}
		}

		private Node UnaryNode()
		{
			if (Eat("!"))
			{
				return new Unary('!', UnaryNode());
			}
			if (Eat("-"))
			{
				return new Unary('-', UnaryNode());
			}
			return Primary();
		}

		private Node Primary()
		{
			Skip();
			if (_at >= text.Length)
			{
				throw Error("unexpected end");
			}
			char c = text[_at];
			if (c == '(')
			{
				_at++;
				Node inner = Expression();
				if (!Eat(")"))
				{
					throw Error("expected ')'");
				}
				return inner;
			}
			if (c == '\'')
			{
				int end = text.IndexOf('\'', _at + 1);
				if (end < 0)
				{
					throw Error("unclosed string");
				}
				string s = text[(_at + 1)..end];
				_at = end + 1;
				return new Lit(new UiValue(s));
			}
			if (char.IsDigit(c) || (c == '.' && _at + 1 < text.Length && char.IsDigit(text[_at + 1])))
			{
				int start = _at;
				while (_at < text.Length && (char.IsDigit(text[_at]) || text[_at] == '.'))
				{
					_at++;
				}
				string number = text[start.._at];
				if (number.Contains('.'))
				{
					return double.TryParse(number, NumberStyles.Float, CultureInfo.InvariantCulture, out double d) ? new Lit(new UiValue(d)) : throw Error($"bad number '{number}'");
				}
				return long.TryParse(number, NumberStyles.Integer, CultureInfo.InvariantCulture, out long l) ? new Lit(new UiValue(l)) : throw Error($"bad number '{number}'");
			}
			if (char.IsLetter(c) || c == '_')
			{
				int start = _at;
				while (_at < text.Length && (char.IsLetterOrDigit(text[_at]) || text[_at] is '_' or '.'))
				{
					_at++;
				}
				string name = text[start.._at];
				if (name is "true" or "false")
				{
					return new Lit(new UiValue(name == "true"));
				}
				if (Eat("("))
				{
					if (!Arity.TryGetValue(name, out int arity))
					{
						throw Error($"unknown function '{name}'");
					}
					var args = new List<Node>();
					if (!Eat(")"))
					{
						do
						{
							args.Add(Expression());
						}
						while (Eat(","));
						if (!Eat(")"))
						{
							throw Error("expected ')'");
						}
					}
					if (arity > 0 ? args.Count != arity : args.Count < -arity)
					{
						throw Error($"{name} takes {(arity > 0 ? arity.ToString(CultureInfo.InvariantCulture) : $"at least {-arity}")} argument{(Math.Abs(arity) == 1 ? "" : "s")}");
					}
					return new Call(name, args);
				}
				names.Add(name);
				return new Name(name);
			}
			throw Error($"unexpected '{c}'");
		}
	}
}
