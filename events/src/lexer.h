#pragma once

#include "events/diagnostics.h"

#include <cstdint>
#include <string>
#include <vector>

namespace events {

enum class TokenKind { word, integer, real, string, text_id, op };

struct Token {
	TokenKind kind = TokenKind::word;
	std::string text;
	std::int64_t integer = 0;
	double real = 0;
};

struct SourceLine {
	int number = 0;
	std::vector<Token> tokens;
};

// Blank and comment-only lines are dropped. A line with a lexical error is
// reported and dropped too, so the parser never sees half a command.
std::vector<SourceLine> tokenize(const std::string& file, const std::string& text, std::vector<Diagnostic>& diagnostics);

const char* token_kind_name(TokenKind kind);

}
