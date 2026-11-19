#include "parser.h"

#include <cstring>
#include <initializer_list>

namespace events {

namespace {

struct Context {
	bool in_together = false;
	bool in_sequence = false;
	bool in_skip = false;
};

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

	const Token* peek() const { return done() ? nullptr : &line_.tokens[pos_]; }

	void error(const std::string& message) {
		if (!failed_) {
			diagnostics_.push_back({Severity::error, file_, line_.number, command_ + ": " + message});
		}
		failed_ = true;
	}

	void expected(const std::string& what) {
		if (done()) {
			error("expected " + what + ", but the line ended");
		} else {
			error("expected " + what + ", found " + describe_token(line_.tokens[pos_]));
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
			error("unexpected " + describe_token(line_.tokens[pos_]));
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
				error(line().number, "'end' without an open block");
				++pos_;
			} else {
				error(line().number, "expected 'event' at the top level, found " + describe_token(line().tokens[0]));
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

	void error(int number, const std::string& message) {
		diagnostics_.push_back({Severity::error, file_, number, message});
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
			if (one_of(h, stops)) {
				return h;
			}
			parse_statement(ctx, out);
		}
		return "";
	}

	void unterminated(const char* what, int opened) {
		int at = pos_ < lines_.size() ? line().number : (lines_.empty() ? opened : lines_.back().number);
		error(at, std::string("unterminated '") + what + "' block opened at line " + std::to_string(opened) + ": missing 'end'");
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
			}
			if (!marked && !a.failed()) {
				error(ev.line, "event '" + ev.name + "' must be marked 'skippable' or 'unskippable'");
			} else {
				a.finish();
			}
		}
		++pos_;
		Context ctx;
		std::string stop = parse_block(ctx, {"on", "end"}, ev.body);
		if (stop == "on") {
			ev.skip_line = line().number;
			Args s = args();
			s.expect_keyword("skip");
			s.finish();
			if (!ev.skippable) {
				error(ev.skip_line, "'on skip' in event '" + ev.name + "', which is not skippable");
			}
			ev.has_skip = true;
			++pos_;
			Context skip_ctx;
			skip_ctx.in_skip = true;
			stop = parse_block(skip_ctx, {"end"}, ev.on_skip);
		}
		if (stop == "end") {
			Args e = args();
			e.finish();
			++pos_;
		} else {
			int at = pos_ < lines_.size() ? line().number : (lines_.empty() ? ev.line : lines_.back().number);
			error(at, "event '" + ev.name + "' opened at line " + std::to_string(ev.line) + " is missing 'end'");
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
			if (!a.name(term.name, "a flag or variable name")) {
				return false;
			}
			if (one_of(term.name, {"and", "or", "not"})) {
				a.error("'" + term.name + "' cannot be used as a flag or variable name");
				return false;
			}
			if (a.next_is_op()) {
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
					a.error("'" + op + "' is not a comparison; use ==, !=, <, <=, > or >=");
					return false;
				}
				if (!a.integer(term.value, "an integer to compare with")) {
					return false;
				}
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
				a.error("mixing 'and' and 'or' in one condition is not supported; split it into nested ifs");
				return false;
			}
			joiner = next;
			cond.join = std::strcmp(next, "and") == 0 ? Join::all : Join::any;
		}
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
				error(b.line, std::string("unreachable '") + (is_elif ? "else if" : "else") + "': it follows the 'else' at line " + std::to_string(else_line));
			}
			if (is_elif) {
				b.has_condition = true;
				Args c = Args(line(), 2, "else if", file_, diagnostics_);
				if (parse_condition(c, b.condition)) {
					std::string text = describe(b.condition);
					for (const Branch& earlier : s.branches) {
						if (earlier.has_condition && describe(earlier.condition) == text) {
							error(b.line, "unreachable 'else if': '" + text + "' was already tested at line " + std::to_string(earlier.line));
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
			error(stray.front().line, std::string("commands must come after the first '") + first_section + "'");
		}
	}

	void parse_choice(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::choice);
		s.kind = StmtKind::choice;
		Args a = args();
		if (a.next_is_text()) {
			a.text(s.ins.choice.prompt, text_what);
		}
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
			o.finish();
			s.ins.choice.options.push_back(label);
			++pos_;
			stop = parse_block(ctx, {"option", "end"}, b.body);
			s.branches.push_back(std::move(b));
		}
		if (close_block(stop, "choice", s.line) && s.branches.size() < 2) {
			error(s.line, "choice: needs at least two options");
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
				error(line().number, "duplicate '" + stop + "' section; the first one is at line " + std::to_string(b.line));
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
		a.finish();
		++pos_;
		parse_sections(ctx, s, {"yes", "no"}, "ask");
		out.push_back(std::move(s));
	}

	void parse_battle(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::battle);
		s.kind = StmtKind::battle;
		s.ins.action.type = ActionType::battle;
		Args a = args();
		a.name(s.ins.action.subject, "an encounter name");
		a.finish();
		++pos_;
		parse_sections(ctx, s, {"win", "lose", "flee"}, "battle");
		out.push_back(std::move(s));
	}

	void parse_together(const Context& ctx, std::vector<Stmt>& out) {
		Stmt s = simple(Op::fork);
		s.kind = StmtKind::together;
		Args a = args();
		a.finish();
		++pos_;
		Context inner = ctx;
		inner.in_together = true;
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
		if (close_block(stop, "together", s.line) && s.branches.empty()) {
			error(s.line, "together: the block is empty");
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
	void action_options(const Context& ctx, Args& a, Stmt& s, bool speed, bool strength, bool loop) {
		while (!a.failed() && !a.done()) {
			if (a.keyword("nowait")) {
				if (ctx.in_together) {
					a.error("'nowait' has no effect inside 'together'; every branch already runs in parallel");
				}
				s.ins.wait = false;
			} else if (speed && a.keyword("speed")) {
				a.number(s.ins.action.speed, "a speed");
			} else if (strength && a.keyword("strength")) {
				a.number(s.ins.action.strength, "a strength");
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
			error(l.number, "expected a command, found " + describe_token(l.tokens[0]));
			++pos_;
			return;
		}

		if (cmd == "else") {
			error(l.number, "'else' without a matching 'if'");
			++pos_;
			return;
		}
		if (cmd == "option") {
			error(l.number, "'option' outside of a 'choice' block");
			++pos_;
			return;
		}
		if (cmd == "yes" || cmd == "no") {
			error(l.number, "'" + cmd + "' outside of an 'ask' block");
			++pos_;
			return;
		}
		if (cmd == "win" || cmd == "lose" || cmd == "flee") {
			error(l.number, "'" + cmd + "' outside of a 'battle' block");
			++pos_;
			return;
		}
		if (cmd == "on") {
			error(l.number, "'on skip' must directly close an event body and may appear only once");
			++pos_;
			return;
		}

		static const std::initializer_list<const char*> known = {
			"anim", "ask", "battle", "call", "camera", "choice", "clear", "face", "fade", "give", "goto", "hide", "if",
			"label", "lock", "move", "music", "narrate", "panel", "party", "return", "say", "sequence", "set", "shake",
			"show", "sound", "sync", "take", "together", "unlock", "var", "wait", "warp", "while",
		};
		if (!one_of(cmd, known)) {
			error(l.number, "unknown command '" + cmd + "'");
			++pos_;
			return;
		}

		if (ctx.in_together && !one_of(cmd, {"anim", "camera", "face", "fade", "hide", "move", "music", "panel", "sequence", "shake", "show", "sound", "wait"})) {
			error(l.number, "'" + cmd + "' is not allowed inside 'together'; only actor, camera, panel, screen and audio commands and 'wait' run in parallel");
			skip_command(ctx);
			return;
		}
		if (cmd == "sequence" && (!ctx.in_together || ctx.in_sequence)) {
			error(l.number, "'sequence' is only allowed directly inside 'together'");
			skip_command(ctx);
			return;
		}
		if (ctx.in_skip && one_of(cmd, {"ask", "battle", "call", "choice", "goto", "label", "narrate", "say", "together"})) {
			error(l.number, "'" + cmd + "' is not allowed in 'on skip'; it must only apply the scene's state changes");
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
		} else if (cmd == "together") {
			parse_together(ctx, out);
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
		if (one_of(cmd, {"if", "while", "choice", "ask", "battle", "together", "sequence"})) {
			std::vector<Stmt> discard;
			Context quiet = ctx;
			quiet.in_together = false;
			quiet.in_sequence = false;
			quiet.in_skip = false;
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
			} else if (cmd == "together") {
				parse_together(quiet, discard);
			} else {
				quiet.in_together = true;
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
					a.error("expected '=', '+=' or '-=', found '" + op + "'");
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
				a.error("the item count must be at least 1");
			}
		} else if (cmd == "party") {
			s = simple(Op::party_add);
			if (a.keyword("remove")) {
				s.ins.op = Op::party_remove;
			} else {
				a.expect_keyword("add");
			}
			a.name(s.ins.name, "a party member name");
		} else if (cmd == "lock" || cmd == "unlock") {
			s = simple(cmd == "lock" ? Op::lock_player : Op::unlock_player);
			a.expect_keyword("player");
		} else if (cmd == "wait") {
			s = simple(Op::wait);
			if (a.number(s.ins.seconds, "a duration in seconds") && s.ins.seconds < 0) {
				a.error("the duration cannot be negative");
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
		} else if (cmd == "move") {
			s = action(ActionType::move);
			a.name(s.ins.action.subject, "an actor name");
			a.expect_keyword("to");
			if (a.next_is_number()) {
				s.ins.action.has_position = true;
				a.number(s.ins.action.position.x, "an x coordinate");
				a.number(s.ins.action.position.y, "a y coordinate");
				a.number(s.ins.action.position.z, "a z coordinate");
			} else {
				a.name(s.ins.action.target, "a marker name or x y z coordinates");
			}
			action_options(ctx, a, s, true, false, false);
		} else if (cmd == "face") {
			s = action(ActionType::face);
			a.name(s.ins.action.subject, "an actor name");
			a.name(s.ins.action.target, "an actor or marker to face");
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "anim") {
			s = action(ActionType::animate);
			a.name(s.ins.action.subject, "an actor name");
			a.name(s.ins.action.target, "an animation name");
			action_options(ctx, a, s, false, false, true);
		} else if (cmd == "show" || cmd == "hide") {
			s = action(cmd == "show" ? ActionType::show_actor : ActionType::hide_actor);
			a.name(s.ins.action.subject, "an actor name");
			action_options(ctx, a, s, false, false, false);
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
			} else {
				a.expected("'cut' or 'move'");
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
		} else if (cmd == "music") {
			s = action(ActionType::music_play);
			if (a.keyword("play")) {
				a.name(s.ins.action.subject, "a track name");
			} else if (a.keyword("stop")) {
				s.ins.action.type = ActionType::music_stop;
			} else if (a.keyword("fade")) {
				s.ins.action.type = ActionType::music_fade;
				a.number(s.ins.action.seconds, "a fade time in seconds");
			} else {
				a.expected("'play', 'stop' or 'fade'");
			}
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "sound") {
			s = action(ActionType::sound);
			a.name(s.ins.action.subject, "a sound cue name");
			action_options(ctx, a, s, false, false, false);
		} else if (cmd == "warp") {
			s = action(ActionType::warp);
			a.name(s.ins.action.subject, "a map name");
			a.name(s.ins.action.target, "a marker name");
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
