#include "text_util.h"

#include <cctype>

namespace events {

bool is_name(const std::string& text) {
	if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_')) {
		return false;
	}
	for (char c : text) {
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) {
			return false;
		}
	}
	return true;
}

std::string trim(const std::string& text) {
	std::size_t begin = text.find_first_not_of(" \t\r");
	if (begin == std::string::npos) {
		return "";
	}
	std::size_t end = text.find_last_not_of(" \t\r");
	return text.substr(begin, end - begin + 1);
}

std::vector<std::string> split_lines(const std::string& text) {
	std::vector<std::string> lines;
	std::size_t start = 0;
	while (start <= text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string::npos) {
			end = text.size();
		}
		lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}

std::vector<int> event_of_pc(const Program& program) {
	std::vector<int> owner(program.code.size(), -1);
	for (std::size_t i = 0; i < program.events.size(); ++i) {
		int begin = program.events[i].entry;
		int end = i + 1 < program.events.size() ? program.events[i + 1].entry : static_cast<int>(program.code.size());
		for (int pc = begin; pc >= 0 && pc < end; ++pc) {
			owner[pc] = static_cast<int>(i);
		}
	}
	return owner;
}

}
