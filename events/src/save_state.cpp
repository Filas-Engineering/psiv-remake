#include "events/save_state.h"

#include <cstdio>
#include <sstream>

namespace events {

namespace {

const char* const header = "events-save";
// Version 4 adds each actor's route (`via`, `pathfind`) and `placed`; a state that uses none of them is written as
// version 3, so older builds still read it.
const int version = 4;
const int oldest_version = 3;
const char* const checksum_mark = " sum ";
// Stands in for an empty word, which the space-separated form cannot hold.
const char* const empty_word = "-";

std::uint64_t fnv1a(const std::string& data) {
	std::uint64_t hash = 14695981039346656037ull;
	for (unsigned char c : data) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	return hash;
}

std::string hex64(std::uint64_t value) {
	char buffer[20];
	std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
	return buffer;
}

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

bool read_word(std::istringstream& in, std::string& out) {
	std::string w;
	if (!(in >> w)) {
		return false;
	}
	out = unword(w);
	return true;
}

bool read_bool(std::istringstream& in, bool& out) {
	int value = 0;
	if (!(in >> value) || value < 0 || value > 1) {
		return false;
	}
	out = value == 1;
	return true;
}

bool uses_routes(const PresentationState& p) {
	for (const ActorState& a : p.actors) {
		if (!a.via.empty() || a.pathfind || a.placed) {
			return true;
		}
	}
	return false;
}

void write_presentation(std::ostringstream& out, const PresentationState& p, int file_version) {
	out << ' ' << p.music_set << ' ' << word(p.music) << ' ' << p.ambient_set << ' ' << word(p.ambient);
	out << ' ' << p.fade_set << ' ' << p.faded_out << ' ' << word(p.camera);
	out << ' ' << p.panels.size();
	for (const PanelState& panel : p.panels) {
		out << ' ' << word(panel.panel) << ' ' << word(panel.camera) << ' ' << word(panel.slot);
	}
	out << ' ' << p.actors.size();
	for (const ActorState& a : p.actors) {
		out << ' ' << word(a.actor) << ' ' << a.visible << ' ' << word(a.marker) << ' ' << a.has_position << ' ' << real(a.position.x) << ' '
			<< real(a.position.y) << ' ' << real(a.position.z) << ' ' << word(a.facing) << ' ' << word(a.pose);
		if (file_version >= 4) {
			out << ' ' << a.via.size();
			for (const std::string& w : a.via) {
				out << ' ' << word(w);
			}
			out << ' ' << a.pathfind << ' ' << a.placed;
		}
	}
}

bool read_presentation(std::istringstream& in, PresentationState& p, int file_version) {
	if (!read_bool(in, p.music_set) || !read_word(in, p.music) || !read_bool(in, p.ambient_set) || !read_word(in, p.ambient) ||
		!read_bool(in, p.fade_set) || !read_bool(in, p.faded_out) || !read_word(in, p.camera)) {
		return false;
	}
	std::size_t count = 0;
	if (!read_count(in, count)) {
		return false;
	}
	p.panels.resize(count);
	for (PanelState& panel : p.panels) {
		if (!read_word(in, panel.panel) || !read_word(in, panel.camera) || !read_word(in, panel.slot)) {
			return false;
		}
	}
	if (!read_count(in, count)) {
		return false;
	}
	p.actors.resize(count);
	for (ActorState& a : p.actors) {
		if (!read_word(in, a.actor) || !(in >> a.visible) || a.visible < -1 || a.visible > 1 || !read_word(in, a.marker) || !read_bool(in, a.has_position) ||
			!(in >> a.position.x >> a.position.y >> a.position.z) || !read_word(in, a.facing) || !read_word(in, a.pose)) {
			return false;
		}
		if (file_version >= 4) {
			std::size_t via = 0;
			if (!read_count(in, via)) {
				return false;
			}
			a.via.resize(via);
			for (std::string& w : a.via) {
				if (!read_word(in, w)) {
					return false;
				}
			}
			if (!read_bool(in, a.pathfind) || !read_bool(in, a.placed)) {
				return false;
			}
		}
	}
	return true;
}

}

bool ActorState::operator==(const ActorState& other) const {
	return actor == other.actor && visible == other.visible && marker == other.marker && has_position == other.has_position &&
		position.x == other.position.x && position.y == other.position.y && position.z == other.position.z && via == other.via &&
		pathfind == other.pathfind && placed == other.placed && facing == other.facing && pose == other.pose;
}

bool PanelState::operator==(const PanelState& other) const {
	return panel == other.panel && camera == other.camera && slot == other.slot;
}

bool PresentationState::operator==(const PresentationState& other) const {
	return music_set == other.music_set && music == other.music && ambient_set == other.ambient_set && ambient == other.ambient &&
		fade_set == other.fade_set && faded_out == other.faded_out && camera == other.camera && panels == other.panels && actors == other.actors;
}

std::string save_state_to_text(const SaveState& state) {
	std::ostringstream out;
	const int file_version = uses_routes(state.presentation) ? version : oldest_version;
	out << header << ' ' << file_version << ' ' << state.fingerprint << ' ' << static_cast<int>(state.status) << ' ' << state.skip_depth;
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
	write_presentation(out, state.presentation, file_version);
	out << ' ' << state.locals.size();
	for (const auto& local : state.locals) {
		out << ' ' << word(local.first) << ' ' << local.second;
	}
	const std::string body = out.str();
	return body + checksum_mark + hex64(fnv1a(body));
}

bool save_state_from_text(const std::string& text, SaveState& state) {
	const std::size_t mark = text.rfind(checksum_mark);
	if (mark == std::string::npos) {
		return false;
	}
	const std::string body = text.substr(0, mark);
	if (text.substr(mark + std::string(checksum_mark).size()) != hex64(fnv1a(body))) {
		return false;
	}
	std::istringstream in(body);
	std::string w;
	int file_version = 0;
	int status = 0;
	SaveState s;
	if (!(in >> w >> file_version) || w != header || file_version < oldest_version || file_version > version) {
		return false;
	}
	if (!(in >> s.fingerprint >> status >> s.skip_depth) || status < 0 || status > static_cast<int>(Status::error) || s.skip_depth < -1) {
		return false;
	}
	s.status = static_cast<Status>(status);
	std::string world;
	if (!read_word(in, s.event) || !(in >> world) || !from_hex(unword(world), s.start_world)) {
		return false;
	}
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
	if (!read_presentation(in, s.presentation, file_version) || !read_count(in, count)) {
		return false;
	}
	for (std::size_t i = 0; i < count; ++i) {
		std::string name;
		std::int64_t value = 0;
		if (!read_word(in, name) || !(in >> value)) {
			return false;
		}
		s.locals[name] = value;
	}
	if (in >> w) {
		return false;
	}
	state = s;
	return true;
}

}
