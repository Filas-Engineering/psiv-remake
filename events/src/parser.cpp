#include "parser.h"

#include "events/language.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <set>

namespace events {

namespace {

struct Context {
	// "together" or "race" inside a parallel block, else null.
	const char* parallel = nullptr;
	bool in_sequence = false;
	// Inside an event marked `background`.
	bool background = false;
};

// The condition test a word starts, other than `except`; null for none.
const ConditionInfo* condition_test(const std::string& word, TermTest except) {
	for (const ConditionInfo& c : condition_tests()) {
		if (!c.word.empty() && c.word == word && c.test != except) {
			return &c;
		}
	}
	return nullptr;
}

bool one_of(const std::string& word, std::initializer_list<const char*> words) {
	for (const char* w : words) {
		if (word == w) {
			return true;
		}
	}
	return false;
}

std::string describe_token(const Token& token) {
	if (token.kind == TokenKind::string) {
		return "string \"" + token.text + "\"";
	}
	return std::string(token_kind_name(token.kind)) + " '" + token.text + "'";
}

// Reads the arguments of one line. Only the first problem on a line is
// reported; after that every read fails quietly.
class Args {
public:
	Args(const SourceLine& line, std::size_t start, std::string command, const std::string& file, std::vector<Diagnostic>& diagnostics)
		: line_(line), pos_(start), command_(std::move(command)), file_(file), diagnostics_(diagnostics) {}

	bool failed() const { return failed_; }
	bool done() const { return pos_ >= line_.tokens.size(); }

	const Token* peek(std::size_t ahead = 0) const { return pos_ + ahead >= line_.tokens.size() ? nullptr : &line_.tokens[pos_ + ahead]; }

	// The rendered message starts with the command, so the line's problem reads in context.
	void error(const char* key, std::vector<std::string> params) {
		if (!failed_) {
			Diagnostic d = diagnostic(Severity::error, file_, line_.number, key, std::move(params));
			d.message = command_ + ": " + d.message;
			diagnostics_.push_back(std::move(d));
		}
		failed_ = true;
	}

	void expected(const std::string& what) {
		if (done()) {
			error("script.expected_but_line_ended", {what});
		} else {
			error("script.expected_found", {what, std::string(describe_token(line_.tokens[pos_]))});
		}
	}

	bool keyword(const char* word) {
		if (failed_ || done()) {
			return false;
		}
		const Token& t = line_.tokens[pos_];
		if (t.kind == TokenKind::word && t.text == word) {
			++pos_;
			return true;
		}
		return false;
	}

	bool expect_keyword(const char* word) {
		if (keyword(word)) {
			return true;
		}
		expected(std::string("'") + word + "'");
		return false;
	}

	bool name(std::string& out, const std::string& what) {
		if (failed_) {
			return false;
		}
		if (done() || line_.tokens[pos_].kind != TokenKind::word) {
			expected(what);
			return false;
		}
		out = line_.tokens[pos_++].text;
		return true;
	}

	bool number(double& out, const std::string& what) {
		if (failed_) {
			return false;
		}
		if (done() || (line_.tokens[pos_].kind != TokenKind::integer && line_.tokens[pos_].kind != TokenKind::real)) {
			expected(what);
			return false;
		}
		out = line_.tokens[pos_++].real;
		return true;
	}

	bool next_is_number() const {
		return !failed_ && !done() && (line_.tokens[pos_].kind == TokenKind::integer || line_.tokens[pos_].kind == TokenKind::real);
	}

	bool integer(std::int64_t& out, const std::string& what) {
		if (failed_) {
			return false;
		}
		if (done() || line_.tokens[pos_].kind != TokenKind::integer) {
			expected(what);
			return false;
		}
		out = line_.tokens[pos_++].integer;
		return true;
	}

	bool op(std::string& out, const std::string& what) {
		if (failed_) {
			return false;
		}
		if (done() || line_.tokens[pos_].kind != TokenKind::op) {
			expected(what);
			return false;
		}
		out = line_.tokens[pos_++].text;
		return true;
	}

	bool next_is_op() const {
		return !failed_ && !done() && line_.tokens[pos_].kind == TokenKind::op;
	}

	bool next_is_text() const {
		return !failed_ && !done() && (line_.tokens[pos_].kind == TokenKind::string || line_.tokens[pos_].kind == TokenKind::text_id);
	}

	bool text(Text& out, const std::string& what) {
		if (failed_) {
			return false;
		}
		if (!next_is_text()) {
			expected(what);
			return false;
		}
		const Token& t = line_.tokens[pos_++];
		out.is_id = t.kind == TokenKind::text_id;
		out.value = t.text;
		return true;
	}

	bool finish() {
		if (failed_) {
			return false;
		}
		if (!done()) {
			error("script.unexpected", {std::string(describe_token(line_.tokens[pos_]))});
			return false;
		}
		return true;
	}

private:
	const SourceLine& line_;
	std::size_t pos_;
	std::string command_;
	const std::string& file_;
	std::vector<Diagnostic>& diagnostics_;
	bool failed_ = false;
};

const char* const text_what = "text (a quoted string or @string.id)";

class Parser {
public:
	Parser(const std::string& file, int file_index, const std::vector<SourceLine>& lines, std::vector<Diagnostic>& diagnostics)
		: file_(file), file_index_(file_index), lines_(lines), diagnostics_(diagnostics) {}

	std::vector<EventDecl> run() {
		while (pos_ < lines_.size()) {
			std::string h = head();
			if (h == "event") {
				parse_event();
			} else if (h == "end") {
				error(line().number, "script.end_without_open_block", {});
				++pos_;
			} else {
				error(line().number, "script.expected_event_at_top_level", {std::string(describe_token(line().tokens[0]))});
				++pos_;
			}
		}
		return std::move(events_);
	}

private:
	const SourceLine& line() const { return lines_[pos_]; }

	std::string head() const {
		const Token& t = lines_[pos_].tokens[0];
		return t.kind == TokenKind::word ? t.text : std::string();
	}

	// `end game` is a command, not the end of a block.
	bool ends_game() const {
		const std::vector<Token>& t = lines_[pos_].tokens;
		return head() == "end" && t.size() > 1 && t[1].kind == TokenKind::word && t[1].text == "game";
	}

	void error(int number, const char* key, std::vector<std::string> params) {
		diagnostics_.push_back(diagnostic(Severity::error, file_, number, key, std::move(params)));
	}

	Args args(std::size_t start = 1) {
		return Args(line(), start, head(), file_, diagnostics_);
	}

	Stmt simple(Op op) {
		Stmt s;
		s.line = line().number;
		s.ins.op = op;
		s.ins.file = file_index_;
		s.ins.source_line = line().number;
		return s;
	}

	// Returns the stop word that ended the block, or "" at end of file or at
	// an `event` line (which always means the block was never closed).
	std::string parse_block(const Context& ctx, std::initializer_list<const char*> stops, std::vector<Stmt>& out) {
		while (pos_ < lines_.size()) {
			std::string h = head();
			if (h == "event") {
				return "";
			}
			if (one_of(h, stops) && !ends_game()) {
				return h;
			}
			parse_statement(ctx, out);
		}
		return "";
	}

	void unterminated(const char* what, int opened) {
		int at = pos_ < lines_.size() ? line().number : (lines_.empty() ? opened : lines_.back().number);
		error(at, "script.unterminated_block_opened_at_line", {what, std::to_string(opened)});
	}

	bool close_block(const std::string& stop, const char* what, int opened) {
		if (stop == "end") {
			Args a = args();
			a.finish();
			++pos_;
			return true;
		}
		unterminated(what, opened);
		return false;
	}

	void parse_event() {
		EventDecl ev;
		ev.file = file_index_;
		ev.line = line().number;
		Args a = args();
		bool marked = false;
		if (a.name(ev.name, "an event name")) {
			if (a.keyword("skippable")) {
				ev.skippable = true;
				marked = true;
			} else if (a.keyword("unskippable")) {
				marked = true;
			} else if (a.keyword("background")) {
				ev.background = true;
				marked = true;
			}
			if (!marked && !a.failed()) {
				error(ev.line, "script.event_marked_skippable_unskippable", {ev.name});
			} else {
				a.finish();
			}
		}
		++pos_;
		Context ctx;
		ctx.background = ev.background;
		std::string stop = parse_block(ctx, {"on", "end"}, ev.body);
		if (stop == "on") {
			// Older scripts describe a skip's effect here; a skip now plays the
			// rest of the event at once, so the block is parsed and dropped.
			int skip_line = line().number;
			Args s = args();
			s.expect_keyword("skip");
			s.finish();
			diagnostics_.push_back(diagnostic(Severity::warning, file_, skip_line, "script.on_skip_ignored_skip_now", {}));
			++pos_;
			std::vector<Stmt> ignored;
			stop = parse_block(Context(), {"end"}, ignored);
		}
		if (stop == "end") {
			Args e = args();
			e.finish();
			++pos_;
		} else {
			int at = pos_ < lines_.size() ? line().number : (lines_.empty() ? ev.line : lines_.back().number);
			error(at, "script.event_opened_at_line_missing", {ev.name, std::to_string(ev.line)});
		}
		events_.push_back(std::move(ev));
	}

	bool parse_condition(Args& a, Condition& cond) {
		const char* joiner = nullptr;
		while (true) {
			Term term;
			if (a.keyword("not")) {
				term.negate = true;
			}
			// `vehicle`, `aboard`, `party` and `item` followed by a name test the game; alone they are still flag names.
			const Token* test = a.peek();
			const Token* subject = a.peek(1);
			if (test && subject && test->kind == TokenKind::word && condition_test(test->text, TermTest::none) &&
				condition_test(test->text, TermTest::none)->test == TermTest::money && subject->kind != TokenKind::word) {
				term.test = TermTest::money;
				if (!parse_value_test(a, term)) {
					return false;
				}
				term.is_flag = false;
			} else if (test && subject && test->kind == TokenKind::word && subject->kind == TokenKind::word && condition_test(test->text, TermTest::money) &&
				!one_of(subject->text, {"and", "or"})) {
				term.test = condition_test(test->text, TermTest::money)->test;
				const char* what = term.test == TermTest::member || term.test == TermTest::leader ? "a member name"
					: term.test == TermTest::holds                                               ? "an item name"
					: term.test == TermTest::music                                               ? "a track name"
					: term.test == TermTest::area                                                ? "an area name"
					: term.test == TermTest::tile                                                ? "floor, water, soft, barrier or counter"
																								 : "a vehicle name";
				a.keyword(test->text.c_str());
				if (!a.name(term.name, what)) {
					return false;
				}
				if (term.test == TermTest::tile && !one_of(term.name, {"floor", "water", "soft", "barrier", "counter"})) {
					a.expected(what);
					return false;
				}
			} else if (!parse_value_test(a, term)) {
				return false;
			}
			cond.terms.push_back(term);
			if (a.done()) {
				return true;
			}
			const char* next = a.keyword("and") ? "and" : (a.keyword("or") ? "or" : nullptr);
			if (!next) {
				a.expected("'and', 'or' or the end of the line");
				return false;
			}
			if (joiner && std::strcmp(joiner, next) != 0) {
				a.error("script.mixing_one_condition_supported_split", {});
				return false;
			}
			joiner = next;
			cond.join = std::strcmp(next, "and") == 0 ? Join::all : Join::any;
		}
	}

	bool parse_value_test(Args& a, Term& term) {
		if (!a.name(term.name, "a flag or variable name")) {
			return false;
		}
		if (one_of(term.name, {"and", "or", "not"})) {
			a.error("script.cannot_used_as_flag_variable", {term.name});
			return false;
		}
		if (!a.next_is_op()) {
			return true;
		}
		std::string op;
		a.op(op, "a comparison");
		term.is_flag = false;
		if (op == "==") {
			term.compare = Compare::eq;
		} else if (op == "!=") {
			term.compare = Compare::ne;
		} else if (op == "<") {
			term.compare = Compare::lt;
		} else if (op == "<=") {
			term.compare = Compare::le;
		} else if (op == ">") {
			term.compare = Compare::gt;
		} else if (op == ">=") {
			term.compare = Compare::ge;
		} else {
			a.error("script.comparison_use", {op});
			return false;
		}
		return a.integer(term.value, "an integer to compare with");
	}

	void parse_if(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::jump_unless);
		s.kind = StmtKind::if_chain;
		int opened = s.line;
		Branch first;
		first.line = s.line;
		first.present = true;
		first.has_condition = true;
		Args a = args();
		parse_condition(a, first.condition);
		++pos_;
		std::string stop = parse_block(ctx, {"else", "end"}, first.body);
		s.branches.push_back(std::move(first));
		int else_line = 0;
		while (stop == "else") {
			Branch b;
			b.line = line().number;
			b.present = true;
			bool is_elif = line().tokens.size() > 1 && line().tokens[1].kind == TokenKind::word && line().tokens[1].text == "if";
			if (else_line) {
				error(b.line, "script.unreachable_follows_else_at_line", {std::string((is_elif ? "else if" : "else")), std::to_string(else_line)});
			}
			if (is_elif) {
				b.has_condition = true;
				Args c = Args(line(), 2, "else if", file_, diagnostics_);
				if (parse_condition(c, b.condition)) {
					std::string text = describe(b.condition);
					for (const Branch& earlier : s.branches) {
						if (earlier.has_condition && describe(earlier.condition) == text) {
							error(b.line, "script.unreachable_else_if_was_already", {text, std::to_string(earlier.line)});
							break;
						}
					}
				}
			} else {
				Args c = args();
				c.finish();
				if (!else_line) {
					else_line = b.line;
				}
			}
			++pos_;
			stop = parse_block(ctx, {"else", "end"}, b.body);
			s.branches.push_back(std::move(b));
		}
		close_block(stop, "if", opened);
		out.push_back(std::move(s));
	}

	void parse_while(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::jump_unless);
		s.kind = StmtKind::while_loop;
		Args a = args();
		parse_condition(a, s.ins.condition);
		++pos_;
		Branch body;
		body.present = true;
		body.line = s.line;
		std::string stop = parse_block(ctx, {"end"}, body.body);
		s.branches.push_back(std::move(body));
		close_block(stop, "while", s.line);
		out.push_back(std::move(s));
	}

	void reject_leading(const std::vector<Stmt>& stray, const char* first_section) {
		if (!stray.empty()) {
			error(stray.front().line, "script.commands_come_after_first", {first_section});
		}
	}

	// The optional `into <name>` that records the answer (EVT-006).
	void into(Args& a, Stmt& s, const char* what) {
		if (a.keyword("into")) {
			a.name(s.ins.name, what);
		}
	}

	// `choice clear`: a simple command, not a block.
	bool choice_clear() {
		const SourceLine& l = line();
		return l.tokens.size() == 2 && l.tokens[1].kind == TokenKind::word && l.tokens[1].text == "clear";
	}

	void parse_choice(const Context& ctx, std::vector<Stmt>& out) {
		if (choice_clear()) {
			out.push_back(simple(Op::choice_clear));
			++pos_;
			return;
		}
		Stmt s = simple(Op::choice);
		s.kind = StmtKind::choice;
		Args a = args();
		if (a.next_is_text()) {
			a.text(s.ins.choice.prompt, text_what);
		}
		if (a.keyword("header")) {
			a.text(s.ins.choice.header, "header text (a quoted string or @string.id)");
		}
		s.ins.choice.keep = a.keyword("keep");
		into(a, s, "a variable name after 'into'");
		a.finish();
		++pos_;
		std::vector<Stmt> stray;
		std::string stop = parse_block(ctx, {"option", "end"}, stray);
		reject_leading(stray, "option");
		while (stop == "option") {
			Branch b;
			b.line = line().number;
			b.present = true;
			Text label;
			Args o = args();
			o.text(label, "option text (a quoted string or @string.id)");
			OptionState state;
			if (o.keyword("state") && o.name(state.word, "a state word after 'state'") && o.keyword("if")) {
				state.has_condition = parse_condition(o, state.condition);
			}
			o.finish();
			if (!state.word.empty()) {
				s.ins.option_states.resize(s.ins.choice.options.size());
				s.ins.option_states.push_back(state);
			} else if (!s.ins.option_states.empty()) {
				s.ins.option_states.emplace_back();
			}
			s.ins.choice.options.push_back(label);
			s.ins.option_lines.push_back(b.line);
			++pos_;
			stop = parse_block(ctx, {"option", "end"}, b.body);
			s.branches.push_back(std::move(b));
		}
		if (!s.ins.option_states.empty()) {
			s.ins.option_states.resize(s.ins.choice.options.size());
		}
		if (close_block(stop, "choice", s.line) && s.branches.size() < 2) {
			error(s.line, "script.choice_needs_at_least_two", {});
		}
		out.push_back(std::move(s));
	}

	// Shared by `ask` (yes, no) and `battle` (win, lose, flee): a block whose
	// sections each appear at most once, in any order.
	void parse_sections(const Context& ctx, Stmt& s, std::initializer_list<const char*> names, const char* what) {
		s.branches.resize(names.size());
		std::vector<Stmt> stray;
		std::string stop;
		if (names.size() == 2) {
			stop = parse_block(ctx, {"yes", "no", "end"}, stray);
		} else {
			stop = parse_block(ctx, {"win", "lose", "flee", "end"}, stray);
		}
		reject_leading(stray, *names.begin());
		while (stop != "end" && !stop.empty()) {
			std::size_t index = 0;
			for (const char* n : names) {
				if (stop == n) {
					break;
				}
				++index;
			}
			Branch& b = s.branches[index];
			if (b.present) {
				error(line().number, "script.duplicate_section_first_one_at", {stop, std::to_string(b.line)});
			}
			Branch fresh;
			fresh.present = true;
			fresh.line = line().number;
			Args a = args();
			a.finish();
			++pos_;
			if (names.size() == 2) {
				stop = parse_block(ctx, {"yes", "no", "end"}, fresh.body);
			} else {
				stop = parse_block(ctx, {"win", "lose", "flee", "end"}, fresh.body);
			}
			if (!b.present) {
				b = std::move(fresh);
			}
		}
		close_block(stop, what, s.line);
	}

	void parse_ask(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::choice);
		s.kind = StmtKind::choice;
		s.ins.choice.yes_no = true;
		Args a = args();
		a.text(s.ins.choice.prompt, "a question (a quoted string or @string.id)");
		into(a, s, "a flag name after 'into'");
		a.finish();
		++pos_;
		parse_sections(ctx, s, {"yes", "no"}, "ask");
		out.push_back(std::move(s));
	}

	void parse_battle(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::battle);
		s.kind = StmtKind::battle;
		Args a = args();
		a.name(s.ins.battle.encounter, "an encounter name");
		if (a.keyword("aboard")) {
			a.name(s.ins.battle.vehicle, "a vehicle name after 'aboard'");
		}
		if (a.keyword("opening")) {
			std::string opening;
			if (a.name(opening, "'normal', 'party_first' or 'enemy_first' after 'opening'")) {
				if (opening == "party_first") {
					s.ins.battle.opening = BattleOpening::party_first;
				} else if (opening == "enemy_first") {
					s.ins.battle.opening = BattleOpening::enemy_first;
				} else if (opening != "normal") {
					a.error("script.expected_normal_party_first_enemy", {opening});
				}
			}
		}
		if (a.keyword("until")) {
			std::int64_t rounds = 0;
			if (a.keyword("survive")) {
				if (a.integer(rounds, "a number of rounds after 'survive'")) {
					if (rounds < 1 || rounds > 999) {
						a.error("script.rounds_survive_1_999_found", {std::to_string(rounds)});
					}
					s.ins.battle.end = BattleEndKind::survive_rounds;
					s.ins.battle.end_rounds = static_cast<int>(rounds);
				}
			} else if (a.keyword("defeat")) {
				if (a.name(s.ins.battle.end_enemy, "an enemy name after 'defeat'")) {
					s.ins.battle.end = BattleEndKind::enemy_defeated;
				}
			} else {
				a.expected("'survive <rounds>' or 'defeat <enemy>' after 'until'");
			}
		}
		a.finish();
		++pos_;
		parse_sections(ctx, s, {"win", "lose", "flee"}, "battle");
		if (!s.branches[0].present) {
			error(s.line, "script.battle_needs_win_section", {});
		}
		s.ins.battle.defeat_handled = s.branches[1].present;
		s.ins.battle.escape_allowed = s.branches[2].present;
		out.push_back(std::move(s));
	}

	void parse_together(const Context& ctx, std::vector<Stmt>& out, bool race) {
		const char* what = race ? "race" : "together";
		Stmt s = simple(Op::fork);
		s.kind = StmtKind::together;
		s.ins.number = race ? 1 : 0;
		Args a = args();
		a.finish();
		++pos_;
		Context inner = ctx;
		inner.parallel = what;
		std::vector<Stmt> items;
		std::string stop = parse_block(inner, {"end"}, items);
		for (Stmt& item : items) {
			Branch b;
			b.present = true;
			b.line = item.line;
			if (item.kind == StmtKind::sequence) {
				b.body = std::move(item.branches[0].body);
			} else {
				b.body.push_back(std::move(item));
			}
			s.branches.push_back(std::move(b));
		}
		if (close_block(stop, what, s.line) && s.branches.empty()) {
			error(s.line, "script.block_empty", {std::string(what)});
		}
		out.push_back(std::move(s));
	}

	void parse_sequence(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::fork);
		s.kind = StmtKind::sequence;
		Args a = args();
		a.finish();
		++pos_;
		Context inner = ctx;
		inner.in_sequence = true;
		Branch b;
		b.present = true;
		b.line = s.line;
		std::string stop = parse_block(inner, {"end"}, b.body);
		s.branches.push_back(std::move(b));
		close_block(stop, "sequence", s.line);
		out.push_back(std::move(s));
	}

	// Trailing options shared by host actions.
	void action_options(const Context& ctx, Args& a, Stmt& s, bool speed, bool strength, bool loop, bool pathfind = false) {
		while (!a.failed() && !a.done()) {
			if (a.keyword("nowait")) {
				if (ctx.parallel) {
					a.error("script.nowait_has_effect_inside_every", {ctx.parallel});
				}
				s.ins.wait = false;
			} else if (speed && a.keyword("speed")) {
				a.number(s.ins.action.speed, "a speed");
			} else if (strength && a.keyword("strength")) {
				a.number(s.ins.action.strength, "a strength");
			} else if (pathfind && a.keyword("pathfind")) {
				s.ins.action.pathfind = true;
			} else if (loop && a.keyword("loop")) {
				s.ins.action.loop = true;
				s.ins.wait = false;
			} else {
				a.finish();
			}
		}
	}

	Stmt action(ActionType type) {
		Stmt s = simple(Op::action);
		s.ins.action.type = type;
		return s;
	}

	void parse_statement(const Context& ctx, std::vector<Stmt>& out) {
		const SourceLine& l = line();
		std::string cmd = head();
		if (cmd.empty()) {
			error(l.number, "script.expected_command_found", {std::string(describe_token(l.tokens[0]))});
			++pos_;
			return;
		}

		if (cmd == "else") {
			error(l.number, "script.else_without_matching_if", {});
			++pos_;
			return;
		}
		if (cmd == "option") {
			error(l.number, "script.option_outside_choice_block", {});
			++pos_;
			return;
		}
		if (cmd == "yes" || cmd == "no") {
			error(l.number, "script.outside_ask_block", {cmd});
			++pos_;
			return;
		}
		if (cmd == "win" || cmd == "lose" || cmd == "flee") {
			error(l.number, "script.outside_battle_block", {cmd});
			++pos_;
			return;
		}
		if (cmd == "on") {
			error(l.number, "script.on_skip_directly_close_event", {});
			++pos_;
			return;
		}

		const CommandInfo* command = script_command(cmd);
		if (!command) {
			error(l.number, "script.unknown_command", {cmd});
			++pos_;
			return;
		}

		if (ctx.background && !command->background) {
			error(l.number, "script.background_not_allowed", {cmd});
			skip_command(ctx);
			return;
		}
		const bool areaMusic = cmd == "music" && l.tokens.size() > 1 && l.tokens[1].text == "area";
		if (ctx.parallel && (areaMusic || !command->parallel)) {
			error(l.number, "script.allowed_inside_only_actor_camera", {cmd, ctx.parallel});
			skip_command(ctx);
			return;
		}
		if (cmd == "sequence" && (!ctx.parallel || ctx.in_sequence)) {
			error(l.number, "script.sequence_only_allowed_directly_inside", {});
			skip_command(ctx);
			return;
		}

		if (cmd == "if") {
			parse_if(ctx, out);
		} else if (cmd == "while") {
			parse_while(ctx, out);
		} else if (cmd == "choice") {
			parse_choice(ctx, out);
		} else if (cmd == "ask") {
			parse_ask(ctx, out);
		} else if (cmd == "battle") {
			parse_battle(ctx, out);
		} else if (cmd == "together" || cmd == "race") {
			parse_together(ctx, out, cmd == "race");
		} else if (cmd == "sequence") {
			parse_sequence(ctx, out);
		} else {
			parse_simple(ctx, cmd, out);
			++pos_;
		}
	}

	// After rejecting a block command, consume its block so its `end` does
	// not close the wrong parent.
	void skip_command(const Context& ctx) {
		std::string cmd = head();
		if (one_of(cmd, {"if", "while", "choice", "ask", "battle", "together", "race", "sequence"}) && !(cmd == "choice" && choice_clear())) {
			std::vector<Stmt> discard;
			Context quiet = ctx;
			quiet.parallel = nullptr;
			quiet.in_sequence = false;
			if (cmd == "if") {
				parse_if(quiet, discard);
			} else if (cmd == "while") {
				parse_while(quiet, discard);
			} else if (cmd == "choice") {
				parse_choice(quiet, discard);
			} else if (cmd == "ask") {
				parse_ask(quiet, discard);
			} else if (cmd == "battle") {
				parse_battle(quiet, discard);
			} else if (cmd == "together" || cmd == "race") {
				parse_together(quiet, discard, cmd == "race");
			} else {
				quiet.parallel = "together";
				parse_sequence(quiet, discard);
			}
			return;
		}
		++pos_;
	}

	void parse_simple(const Context& ctx, const std::string& cmd, std::vector<Stmt>& out) {
		Args a = args();
		Stmt s;
		if (cmd == "say") {
			s = simple(Op::say);
			a.name(s.ins.dialogue.speaker, "a speaker name");
			a.text(s.ins.dialogue.text, text_what);
			if (a.keyword("as")) {
				a.name(s.ins.dialogue.portrait, "a portrait name after 'as'");
			}
		} else if (cmd == "narrate") {
			s = simple(Op::say);
			a.text(s.ins.dialogue.text, text_what);
		} else if (cmd == "set" || cmd == "clear") {
			s = simple(cmd == "set" ? Op::set_flag : Op::clear_flag);
			a.name(s.ins.name, "a flag name");
		} else if (cmd == "var") {
			s = simple(Op::var_set);
			a.name(s.ins.name, "a variable name");
			std::string op;
			if (a.op(op, "'=', '+=' or '-='")) {
				if (op == "+=" || op == "-=") {
					s.ins.op = Op::var_add;
				} else if (op != "=") {
					a.error("script.expected_found_2", {op});
				}
			}
			if (a.integer(s.ins.number, "an integer value") && op == "-=") {
				s.ins.number = -s.ins.number;
			}
		} else if (cmd == "give" || cmd == "take") {
			s = simple(cmd == "give" ? Op::give : Op::take);
			a.name(s.ins.name, "an item name");
			s.ins.number = 1;
			if (!a.done() && a.integer(s.ins.number, "an item count") && s.ins.number < 1) {
				a.error("script.item_count_at_least_1", {});
			}
		} else if (cmd == "party") {
			s = simple(Op::party_add);
			const bool save = a.keyword("save");
			if (save || a.keyword("restore")) {
				s.ins.op = save ? Op::party_save : Op::party_restore;
				s.ins.name = "default";
				if (!a.done()) {
					a.name(s.ins.name, "a party snapshot name");
				}
			} else if (a.keyword("swap")) {
				s.ins.op = Op::party_swap;
				PartySwap& swap = s.ins.swap;
				if (a.next_is_number()) {
					std::int64_t slot = 0;
					if (a.integer(slot, "a lineup slot") && slot < 1) {
						a.error("script.party_slot_at_least_1", {});
					}
					swap.slot = static_cast<int>(std::min<std::int64_t>(slot, 99));
				} else {
					a.name(swap.replaced, "a lineup slot or a party member name");
				}
				a.name(swap.member, "a party member name");
				if (a.keyword("equipment")) {
					if (a.keyword("keep")) {
						swap.equipment = SwapEquipment::keep;
					} else if (a.keyword("bag")) {
						swap.equipment = SwapEquipment::bag;
					} else {
						a.expected("'keep' or 'bag'");
					}
				}
				swap.silent = a.keyword("silent");
			} else if (a.keyword("order")) {
				s.ins.op = Op::party_order;
				do {
					std::string member;
					if (a.name(member, "a party member name")) {
						if (std::find(s.ins.names.begin(), s.ins.names.end(), member) != s.ins.names.end()) {
							a.error("script.party_order_twice", {member});
						}
						s.ins.names.push_back(member);
					}
				} while (!a.failed() && !a.done());
			} else {
				if (a.keyword("remove")) {
					s.ins.op = Op::party_remove;
				} else {
					a.expect_keyword("add");
				}
				a.name(s.ins.name, "a party member name");
				s.ins.number = a.keyword("silent") ? 1 : 0;
			}
		} else if (cmd == "lock" || cmd == "unlock") {
			s = simple(cmd == "lock" ? Op::lock_player : Op::unlock_player);
			a.expect_keyword("player");
		} else if (cmd == "wait") {
			s = simple(Op::wait);
			if (a.number(s.ins.seconds, "a duration in seconds") && s.ins.seconds < 0) {
				a.error("script.duration_cannot_negative", {});
			}
		} else if (cmd == "sync") {
			s = simple(Op::sync);
		} else if (cmd == "call") {
			s = simple(Op::call);
			a.name(s.ins.name, "an event name");
		} else if (cmd == "return") {
			s = simple(Op::ret);
		} else if (cmd == "label" || cmd == "goto") {
			s = simple(Op::jump);
			s.kind = cmd == "label" ? StmtKind::label : StmtKind::go_to;
			a.name(s.ins.name, "a label name");
		} else if (cmd == "move" || cmd == "place") {
			s = action(cmd == "move" ? ActionType::move : ActionType::place_actor);
			a.name(s.ins.action.subject, "an actor name");
			if (cmd == "move" && a.keyword("via")) {
				do {
					std::string marker;
					if (a.name(marker, "a marker name")) {
						s.ins.action.waypoints.push_back(marker);
					}
				} while (!a.failed() && !a.keyword("to"));
			} else if (cmd == "move") {
				a.expect_keyword("to");
			}
			if (a.next_is_number()) {
				s.ins.action.has_position = true;
				a.number(s.ins.action.position.x, "an x coordinate");
				a.number(s.ins.action.position.y, "a y coordinate");
				a.number(s.ins.action.position.z, "a z coordinate");
			} else {
				a.name(s.ins.action.target, "a marker name or x y z coordinates");
			}
			if (cmd == "place" && a.keyword("facing")) {
				if (a.name(s.ins.action.facing, "north, east, south or west") && !is_direction(s.ins.action.facing)) {
					a.error("script.expected_found", {"north, east, south or west", "name '" + s.ins.action.facing + "'"});
				}
			}
			action_options(ctx, a, s, cmd == "move", false, false, cmd == "move");
		} else if (cmd == "remove") {
			s = simple(Op::remove_actor);
			a.name(s.ins.name, "an actor name");
			if (a.keyword("until")) {
				a.expect_keyword("reload");
				s.ins.number = 1;
			}
		} else if (cmd == "face") {
			s = action(ActionType::face);
			a.name(s.ins.action.subject, "an actor name");
			a.name(s.ins.action.target, "an actor or marker to face");
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "anim") {
			s = action(ActionType::animate);
			a.name(s.ins.action.subject, "an actor name");
			a.name(s.ins.action.target, "an animation name");
			if (const StandardAnimation* standard = standard_animation(s.ins.action.target)) {
				s.ins.action.seconds = standard->seconds;
			}
			action_options(ctx, a, s, false, false, true);
		} else if (cmd == "show" || cmd == "hide") {
			s = action(cmd == "show" ? ActionType::show_actor : ActionType::hide_actor);
			a.name(s.ins.action.subject, "an actor name");
			if (a.keyword("with") && a.name(s.ins.action.target, "an animation name after 'with'")) {
				if (const StandardAnimation* standard = standard_animation(s.ins.action.target)) {
					s.ins.action.seconds = standard->seconds;
					const AnimationEnd wrong = cmd == "show" ? AnimationEnd::hidden : AnimationEnd::shown;
					if (standard->end == wrong) {
						a.error("script.transition_wrong_way", {s.ins.action.target, cmd});
					}
				}
			}
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "spawn") {
			s = simple(Op::spawn);
			a.name(s.ins.name, "a background event name");
		} else if (cmd == "picture") {
			s = simple(Op::picture);
			if (a.keyword("show")) {
				a.name(s.ins.name, "a picture name");
			} else if (a.keyword("hide")) {
				s.ins.number = 1;
				if (!a.done()) {
					a.name(s.ins.name, "a picture name");
				}
			} else if (a.keyword("clear")) {
				s.ins.number = 2;
			} else {
				a.expected("'show', 'hide' or 'clear'");
			}
		} else if (cmd == "look") {
			s = simple(Op::actor_look);
			a.name(s.ins.name, "an actor name");
			if (a.keyword("enemy")) {
				s.ins.action.subject = "enemy";
				a.name(s.ins.action.target, "an enemy name");
			} else if (!a.keyword("default")) {
				a.name(s.ins.action.target, "a look name, 'enemy <enemy>' or 'default'");
			}
		} else if (cmd == "panel") {
			s = action(ActionType::panel_show);
			if (a.keyword("show")) {
				a.name(s.ins.action.subject, "a panel id");
				a.expect_keyword("camera");
				a.name(s.ins.action.target, "a camera name");
				a.expect_keyword("layout");
				a.name(s.ins.action.slot, "a layout slot");
			} else if (a.keyword("hide")) {
				s.ins.action.type = ActionType::panel_hide;
				a.name(s.ins.action.subject, "a panel id");
			} else if (a.keyword("clear")) {
				s.ins.action.type = ActionType::panel_clear;
			} else {
				a.expected("'show', 'hide' or 'clear'");
			}
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "camera") {
			s = action(ActionType::camera_cut);
			if (a.keyword("cut")) {
				a.name(s.ins.action.subject, "a camera name");
			} else if (a.keyword("move")) {
				s.ins.action.type = ActionType::camera_move;
				a.name(s.ins.action.subject, "a camera name");
				a.number(s.ins.action.seconds, "a blend time in seconds");
			} else if (a.keyword("to")) {
				s.ins.action.type = ActionType::camera_to;
				if (a.next_is_number()) {
					s.ins.action.has_position = true;
					if (a.number(s.ins.action.position.x, "a cell's x") && a.number(s.ins.action.position.y, "a cell's y") &&
						(s.ins.action.position.x != std::floor(s.ins.action.position.x) || s.ins.action.position.y != std::floor(s.ins.action.position.y) ||
							s.ins.action.position.x < 0 || s.ins.action.position.y < 0)) {
						a.error("script.camera_cell", {});
					}
				} else {
					a.name(s.ins.action.target, "a marker or a cell's x and y");
				}
				if (a.next_is_number()) {
					a.number(s.ins.action.seconds, "a move time in seconds");
				}
			} else if (a.keyword("reset")) {
				s.ins.action.type = ActionType::camera_reset;
				if (a.next_is_number()) {
					a.number(s.ins.action.seconds, "a move time in seconds");
				}
			} else {
				a.expected("'cut', 'move', 'to' or 'reset'");
			}
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "fade") {
			s = action(ActionType::fade_out);
			if (a.keyword("in")) {
				s.ins.action.type = ActionType::fade_in;
			} else {
				a.expect_keyword("out");
			}
			s.ins.action.seconds = 0.5;
			if (a.next_is_number()) {
				a.number(s.ins.action.seconds, "a duration in seconds");
			}
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "shake") {
			s = action(ActionType::shake);
			s.ins.action.strength = 1;
			a.number(s.ins.action.seconds, "a duration in seconds");
			action_options(ctx, a, s, false, true, false);
		} else if (cmd == "music" && a.keyword("area")) {
			s = simple(Op::area_music);
			if (!a.keyword("default")) {
				a.name(s.ins.name, "a track name or 'default'");
			}
		} else if (cmd == "music") {
			s = action(ActionType::music_play);
			if (a.keyword("play")) {
				a.name(s.ins.action.subject, "a track name");
				if (a.keyword("cut")) {
					s.ins.action.fade_ms = 0;
				} else if (a.keyword("fade")) {
					double ms = 0;
					if (a.number(ms, "a fade time in milliseconds") && (ms < 1 || ms > 60000 || ms != std::floor(ms))) {
						a.error("script.music_fade_ms", {});
					}
					s.ins.action.fade_ms = static_cast<int>(ms);
				}
			} else if (a.keyword("stop")) {
				s.ins.action.type = ActionType::music_stop;
			} else if (a.keyword("fade")) {
				s.ins.action.type = ActionType::music_fade;
				a.number(s.ins.action.seconds, "a fade time in seconds");
			} else {
				a.expected("'play', 'stop' or 'fade'");
			}
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "ambient") {
			s = action(ActionType::ambient_play);
			if (a.keyword("play")) {
				a.name(s.ins.action.subject, "an ambient sound name");
			} else if (a.keyword("stop")) {
				s.ins.action.type = ActionType::ambient_stop;
			} else {
				a.expected("'play' or 'stop'");
			}
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "shop") {
			s = simple(Op::shop);
			a.name(s.ins.name, "a shop name");
		} else if (cmd == "rest") {
			s = simple(Op::rest);
			if (!a.done()) {
				a.name(s.ins.name, "a rest point name");
			}
		} else if (cmd == "save") {
			s = simple(Op::save_prompt);
			a.expect_keyword("prompt");
		} else if (cmd == "name") {
			s = simple(Op::name_entry);
			a.expect_keyword("entry");
			a.name(s.ins.name, "a party member name");
		} else if (cmd == "register" || cmd == "unregister") {
			s = simple(Op::register_town);
			s.ins.number = cmd == "unregister" ? 1 : 0;
			a.expect_keyword("town");
			a.name(s.ins.name, "a map name");
		} else if (cmd == "credits") {
			s = simple(Op::credits);
			a.name(s.ins.name, "a credits name");
		} else if (cmd == "end") {
			s = simple(Op::end_game);
			a.expect_keyword("game");
		} else if (cmd == "sound") {
			s = action(ActionType::sound);
			a.name(s.ins.action.subject, "a sound cue name");
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "warp") {
			s = action(ActionType::warp);
			a.name(s.ins.action.subject, "a map name");
			a.name(s.ins.action.target, "a marker name");
			if (a.keyword("followers")) {
				static const std::set<std::string> placements = {"behind", "down", "left", "right", "stacked", "up"};
				if (a.name(s.ins.action.followers, "stacked, behind, up, down, left or right") && !placements.count(s.ins.action.followers)) {
					a.error("script.expected_found", {"stacked, behind, up, down, left or right", "name '" + s.ins.action.followers + "'"});
				}
			}
		} else if (cmd == "learn") {
			s = simple(Op::learn);
			a.name(s.ins.name, "a party member name");
			a.name(s.ins.action.subject, "an ability name");
		} else if (cmd == "money") {
			s = simple(Op::money);
			const bool take = a.keyword("take");
			if (!take && !a.keyword("give")) {
				a.expected("'give' or 'take'");
			} else if (a.integer(s.ins.number, "an amount of money") && s.ins.number < 1) {
				a.error("script.money_at_least_1", {});
			} else if (take) {
				s.ins.number = -s.ins.number;
			}
		} else if (cmd == "equip" || cmd == "unequip") {
			s = simple(cmd == "equip" ? Op::equip : Op::unequip);
			a.name(s.ins.name, "a party member name");
			a.name(s.ins.action.subject, "an equipment name");
		} else if (cmd == "follower") {
			s = simple(Op::follower);
			if (!a.keyword("none")) {
				a.name(s.ins.name, "an actor name or 'none'");
			}
		} else if (cmd == "vehicle") {
			s = simple(Op::vehicle_give);
			if (a.keyword("place")) {
				s.ins.op = Op::vehicle_place;
			} else if (a.keyword("remove")) {
				s.ins.op = Op::vehicle_remove;
			} else if (!a.keyword("give")) {
				a.expected("'give', 'place' or 'remove'");
			}
			a.name(s.ins.name, "a vehicle name");
			if (s.ins.op != Op::vehicle_remove) {
				a.name(s.ins.action.subject, "a map name");
				a.name(s.ins.action.target, "a marker name");
			}
		}
		a.finish();
		if (!a.failed()) {
			out.push_back(std::move(s));
		}
	}

	const std::string& file_;
	int file_index_;
	const std::vector<SourceLine>& lines_;
	std::vector<Diagnostic>& diagnostics_;
	std::size_t pos_ = 0;
	std::vector<EventDecl> events_;
};

}

std::vector<EventDecl> parse(const std::string& file, int file_index, const std::vector<SourceLine>& lines, std::vector<Diagnostic>& diagnostics) {
	return Parser(file, file_index, lines, diagnostics).run();
}

}
