#include "acc.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace acc {

namespace {

constexpr std::size_t kKeptFailures = 5;

std::string join(const std::vector<std::string>& parts) {
	std::string out;
	for (const std::string& part : parts) {
		out += (out.empty() ? "" : "; ") + part;
	}
	return out;
}

// The runner reads one tab-separated line, so the reason must stay on it.
std::string oneLine(std::string text) {
	for (char& c : text) {
		if (c == '\n' || c == '\t' || c == '\r') {
			c = ' ';
		}
	}
	return text;
}

}

std::vector<Check>& registry() {
	static std::vector<Check> checks;
	return checks;
}

bool Context::expect(bool ok, const std::string& what) {
	if (!ok) {
		++failureCount_;
		if (failures_.size() < kKeptFailures) {
			failures_.push_back(what);
		}
	}
	return ok;
}

void Context::gap(const std::string& what) {
	gaps_.push_back(what);
}

void Context::note(const std::string& what) {
	notes_.push_back(what);
}

}

int main(int argc, char** argv) {
	std::string root = ".";
	std::string run;
	bool list = false;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--list") == 0) {
			list = true;
		} else if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
			root = argv[++i];
		} else if (std::strcmp(argv[i], "--run") == 0 && i + 1 < argc) {
			run = argv[++i];
		} else {
			std::fprintf(stderr, "usage: %s --list | --run ID [--root REPO]\n", argv[0]);
			return 2;
		}
	}
	if (list) {
		for (const acc::Check& check : acc::registry()) {
			std::printf("%s\t%s\n", check.id, check.verifies);
		}
		return 0;
	}
	for (const acc::Check& check : acc::registry()) {
		if (run != check.id) {
			continue;
		}
		acc::Context context(root);
		check.run(context);
		std::string status = "PASS";
		std::string reason = acc::join(context.notes());
		if (context.failureCount() > 0) {
			status = "FAIL";
			reason = acc::join(context.failures());
			const std::size_t hidden = static_cast<std::size_t>(context.failureCount()) - context.failures().size();
			if (hidden > 0) {
				reason += "; and " + std::to_string(hidden) + " more failure(s)";
			}
			if (!context.gaps().empty()) {
				reason += "; also not implemented: " + acc::join(context.gaps());
			}
		} else if (!context.gaps().empty()) {
			status = "NOT IMPLEMENTED";
			reason = acc::join(context.gaps());
			if (!context.notes().empty()) {
				reason += "; verified: " + acc::join(context.notes());
			}
		}
		std::printf("%s\t%s\n", status.c_str(), acc::oneLine(reason).c_str());
		return 0;
	}
	std::fprintf(stderr, "no check named '%s'\n", run.c_str());
	return 2;
}
