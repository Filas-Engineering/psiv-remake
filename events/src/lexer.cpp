#include "lexer.h"

#include <cctype>
#include <cstdlib>

namespace events {

namespace {

bool is_word_start(char c) {
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_word_char(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
}

bool is_digit(char c) {
	return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool lex_line(const std::string& text, std::vector<Token>& tokens, DiagnosticMessage& error) {
	std::size_t i = 0;
	while (i < text.size()) {
		char c = text[i];
		if (c == ' ' || c == '\t' || c == '\r') {
			++i;
			continue;
		}
		if (c == '#') {
			break;
		}
		Token token;
		if (c == '"') {
			token.kind = TokenKind::string;
			++i;
			bool closed = false;
			while (i < text.size()) {
				char d = text[i++];
				if (d == '"') {
					closed = true;
					break;
				}
				if (d == '\\') {
					if (i >= text.size()) {
						break;
					}
					char e = text[i++];
					if (e == 'n') {
						token.text += '\n';
					} else if (e == '"' || e == '\\') {
						token.text += e;
					} else {
						error = {"script.unknown_escape_in_string", {std::string(1, e)}};
						return false;
					}
					continue;
				}
				token.text += d;
			}
			if (!closed) {
				error = {"script.unterminated_string", {}};
				return false;
			}
		} else if (c == '@') {
			token.kind = TokenKind::text_id;
			++i;
			std::size_t start = i;
			while (i < text.size() && is_word_char(text[i])) {
				++i;
			}
			if (i == start) {
				error = {"script.expected_string_id", {}};
				return false;
			}
			token.text = text.substr(start, i - start);
		} else if (is_digit(c) || (c == '-' && i + 1 < text.size() && is_digit(text[i + 1]))) {
			std::size_t start = i;
			++i;
			bool real = false;
			while (i < text.size() && (is_digit(text[i]) || text[i] == '.')) {
				if (text[i] == '.') {
					if (real) {
						error = {"script.malformed_number", {}};
						return false;
					}
					real = true;
				}
				++i;
			}
			if (i < text.size() && is_word_char(text[i])) {
				error = {"script.malformed_number_named", {text.substr(start, i - start + 1)}};
				return false;
			}
			token.text = text.substr(start, i - start);
			if (real) {
				token.kind = TokenKind::real;
				token.real = std::strtod(token.text.c_str(), nullptr);
			} else {
				token.kind = TokenKind::integer;
				token.integer = std::strtoll(token.text.c_str(), nullptr, 10);
				token.real = static_cast<double>(token.integer);
			}
		} else if (is_word_start(c)) {
			token.kind = TokenKind::word;
			std::size_t start = i;
			while (i < text.size() && is_word_char(text[i])) {
				++i;
			}
			token.text = text.substr(start, i - start);
		} else if (c == '=' || c == '!' || c == '<' || c == '>' || c == '+' || c == '-') {
			token.kind = TokenKind::op;
			bool has_eq = i + 1 < text.size() && text[i + 1] == '=';
			if ((c == '!' || c == '+' || c == '-') && !has_eq) {
				error = {"script.unexpected_character", {std::string(1, c)}};
				return false;
			}
			token.text = text.substr(i, has_eq ? 2 : 1);
			i += has_eq ? 2 : 1;
		} else {
			error = {"script.unexpected_character", {std::string(1, c)}};
			return false;
		}
		tokens.push_back(token);
	}
	return true;
}

}

std::vector<SourceLine> tokenize(const std::string& file, const std::string& text, std::vector<Diagnostic>& diagnostics) {
	std::vector<SourceLine> lines;
	std::size_t start = 0;
	int number = 0;
	while (start <= text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string::npos) {
			end = text.size();
		}
		++number;
		SourceLine line;
		line.number = number;
		DiagnosticMessage error;
		if (!lex_line(text.substr(start, end - start), line.tokens, error)) {
			diagnostics.push_back(diagnostic(Severity::error, file, number, error));
		} else if (!line.tokens.empty()) {
			lines.push_back(line);
		}
		start = end + 1;
	}
	return lines;
}

const char* token_kind_name(TokenKind kind) {
	switch (kind) {
	case TokenKind::word:
		return "name";
	case TokenKind::integer:
		return "integer";
	case TokenKind::real:
		return "number";
	case TokenKind::string:
		return "string";
	case TokenKind::text_id:
		return "string ID";
	case TokenKind::op:
		return "operator";
	}
	return "token";
}

}
