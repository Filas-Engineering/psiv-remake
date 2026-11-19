#include "events/save_state.h"

#include <cstdio>
#include <sstream>

namespace events {

namespace {

const char* const header = "events-save";
const int version = 2;
// Stands in for an empty word, which the space-separated form cannot hold.
const char* const empty_word = "-";

std::string real(double value) {
	char buffer[40];
	std::snprintf(buffer, sizeof buffer, "%.17g", value);
	return buffer;
}

std::string word(const std::string& value) {
	return value.empty() ? empty_word : value;
}

std::string unword(const std::string& value) {
	return value == empty_word ? std::string() : value;
}

// The world snapshot is opaque and may hold spaces or any byte.
std::string to_hex(const std::string& bytes) {
	static const char digits[] = "0123456789abcdef";
	std::string out;
	for (unsigned char c : bytes) {
		out += digits[c >> 4];
		out += digits[c & 15];
	}
	return out;
}

bool from_hex(const std::string& hex, std::string& bytes) {
	if (hex.size() % 2 != 0) {
		return false;
	}
	auto value = [](char c) {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}
		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}
		return -1;
	};
	bytes.clear();
	for (std::size_t i = 0; i < hex.size(); i += 2) {
		int hi = value(hex[i]);
		int lo = value(hex[i + 1]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		bytes += static_cast<char>(hi * 16 + lo);
	}
	return true;
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

void write_changes(std::ostringstream& out, const std::vector<SavedChange>& changes) {
	out << ' ' << changes.size();
	for (const SavedChange& c : changes) {
		out << ' ' << static_cast<int>(c.kind) << ' ' << word(c.name) << ' ' << c.depth << ' ' << c.amount;
	}
}

bool read_changes(std::istringstream& in, std::vector<SavedChange>& changes) {
	std::size_t count = 0;
	if (!read_count(in, count)) {
		return false;
	}
	changes.resize(count);
	for (SavedChange& c : changes) {
		int kind = 0;
		std::string name;
		if (!(in >> kind >> name >> c.depth >> c.amount) || kind < 0 || kind > static_cast<int>(ChangeKind::var)) {
			return false;
		}
		c.kind = static_cast<ChangeKind>(kind);
		c.name = unword(name);
	}
	return true;
}

}

std::string save_state_to_text(const SaveState& state) {
	std::ostringstream out;
	out << header << ' ' << version << ' ' << state.fingerprint << ' ' << static_cast<int>(state.status) << ' ' << state.skip_depth;
	out << ' ' << word(state.event) << ' ' << word(to_hex(state.start_world));
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
	write_changes(out, state.applied);
	write_changes(out, state.skip_credit);
	return out.str();
}

bool save_state_from_text(const std::string& text, SaveState& state) {
	std::istringstream in(text);
	std::string w;
	int file_version = 0;
	int status = 0;
	SaveState s;
	if (!(in >> w >> file_version) || w != header || file_version != version) {
		return false;
	}
	if (!(in >> s.fingerprint >> status >> s.skip_depth) || status < 0 || status > static_cast<int>(Status::error)) {
		return false;
	}
	s.status = static_cast<Status>(status);
	std::string event;
	std::string world;
	if (!(in >> event >> world) || !from_hex(unword(world), s.start_world)) {
		return false;
	}
	s.event = unword(event);
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
	if (!read_changes(in, s.applied) || !read_changes(in, s.skip_credit)) {
		return false;
	}
	if (in >> w) {
		return false;
	}
	state = s;
	return true;
}

}
