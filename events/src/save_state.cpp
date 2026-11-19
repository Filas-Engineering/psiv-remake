#include "events/save_state.h"

#include <cstdio>
#include <sstream>

namespace events {

namespace {

const char* const header = "events-save";
const int version = 1;

std::string real(double value) {
	char buffer[40];
	std::snprintf(buffer, sizeof buffer, "%.17g", value);
	return buffer;
}

void write_thread(std::ostringstream& out, const SavedThread& t) {
	out << ' ' << t.pc << ' ' << static_cast<int>(t.wait) << ' ' << real(t.timer);
}

bool read_thread(std::istringstream& in, SavedThread& t) {
	int wait = 0;
	if (!(in >> t.pc >> wait >> t.timer) || wait < 0 || wait > static_cast<int>(WaitKind::background)) {
		return false;
	}
	t.wait = static_cast<WaitKind>(wait);
	return true;
}

bool read_count(std::istringstream& in, std::size_t& count) {
	long long n = 0;
	if (!(in >> n) || n < 0 || n > 1000000) {
		return false;
	}
	count = static_cast<std::size_t>(n);
	return true;
}

}

std::string save_state_to_text(const SaveState& state) {
	std::ostringstream out;
	out << header << ' ' << version << ' ' << state.fingerprint << ' ' << static_cast<int>(state.status) << ' ' << state.skip_depth;
	out << ' ' << state.frames.size();
	for (const SavedFrame& f : state.frames) {
		out << ' ' << f.event << ' ' << f.return_pc;
	}
	write_thread(out, state.main);
	out << ' ' << state.children.size();
	for (const SavedThread& t : state.children) {
		write_thread(out, t);
	}
	out << ' ' << state.background.size();
	for (const SavedBackground& b : state.background) {
		out << ' ' << b.pc << ' ' << b.depth;
	}
	return out.str();
}

bool save_state_from_text(const std::string& text, SaveState& state) {
	std::istringstream in(text);
	std::string word;
	int file_version = 0;
	int status = 0;
	SaveState s;
	if (!(in >> word >> file_version) || word != header || file_version != version) {
		return false;
	}
	if (!(in >> s.fingerprint >> status >> s.skip_depth) || status < 0 || status > static_cast<int>(Status::error)) {
		return false;
	}
	s.status = static_cast<Status>(status);
	std::size_t count = 0;
	if (!read_count(in, count)) {
		return false;
	}
	s.frames.resize(count);
	for (SavedFrame& f : s.frames) {
		if (!(in >> f.event >> f.return_pc)) {
			return false;
		}
	}
	if (!read_thread(in, s.main) || !read_count(in, count)) {
		return false;
	}
	s.children.resize(count);
	for (SavedThread& t : s.children) {
		if (!read_thread(in, t)) {
			return false;
		}
	}
	if (!read_count(in, count)) {
		return false;
	}
	s.background.resize(count);
	for (SavedBackground& b : s.background) {
		if (!(in >> b.pc >> b.depth)) {
			return false;
		}
	}
	if (in >> word) {
		return false;
	}
	state = s;
	return true;
}

}
