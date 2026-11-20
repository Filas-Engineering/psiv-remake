#include "support.h"

#include <cstdio>
#include <cstdlib>
#include <regex>
#include <set>
#include <sstream>
#include <sys/stat.h>

namespace {

const std::set<std::string>& standardHeaders() {
	static const std::set<std::string> headers = {
		"algorithm", "any", "array", "atomic", "bitset", "cassert", "cctype", "cerrno", "cfloat", "charconv", "chrono",
		"cinttypes", "climits", "clocale", "cmath", "complex", "condition_variable", "cstdarg", "cstddef", "cstdint",
		"cstdio", "cstdlib", "cstring", "ctime", "deque", "exception", "filesystem", "fstream", "functional", "initializer_list",
		"iomanip", "ios", "iosfwd", "iostream", "istream", "iterator", "limits", "list", "locale", "map", "memory", "mutex",
		"new", "numeric", "optional", "ostream", "queue", "random", "ratio", "regex", "set", "shared_mutex", "sstream", "stack",
		"stdexcept", "streambuf", "string", "string_view", "system_error", "thread", "tuple", "type_traits", "typeindex",
		"typeinfo", "unordered_map", "unordered_set", "utility", "valarray", "variant", "vector"};
	return headers;
}

// Headers that would let resolution read the clock, the file system, the network or threads (BAT-002).
const std::set<std::string>& forbiddenInResolution() {
	static const std::set<std::string> headers = {"chrono", "ctime", "filesystem", "fstream", "iostream", "cstdio", "thread", "random", "mutex", "future"};
	return headers;
}

// The top-level function each line sits in, by its header line at column 0, so a host-side file read such as
// loadTextTable is told apart from resolution code in the same file.
std::vector<std::string> enclosingFunctions(const std::string& text) {
	const std::regex header(R"(^[A-Za-z].*\b(\w+)\(.*\{\s*$)");
	std::vector<std::string> out;
	std::istringstream in(text);
	std::string line;
	std::string current;
	while (std::getline(in, line)) {
		std::smatch m;
		if (std::regex_search(line, m, header)) {
			current = m[1];
		}
		out.push_back(current);
	}
	return out;
}

bool exists(const std::string& path) {
	struct stat info;
	return stat(path.c_str(), &info) == 0;
}

std::string run(const std::string& command, int& status) {
	std::string output;
	if (FILE* pipe = popen((command + " 2>&1").c_str(), "r")) {
		char buffer[512];
		while (fgets(buffer, sizeof buffer, pipe)) {
			output += buffer;
		}
		status = pclose(pipe);
	} else {
		status = -1;
	}
	return output;
}

}

ACC_CHECK(acc_bat_01, "ACC-BAT-01", "BAT-002") {
	const std::string core = t.path("core");
	std::vector<std::string> sources = support::listFiles(core + "/src", ".cpp");
	std::vector<std::string> headers = support::listFiles(core + "/include/rules", ".h");
	for (const std::string& h : support::listFiles(core + "/src", ".h")) {
		headers.push_back(h);
	}
	t.expect(!sources.empty(), "no sources found under core/src");

	const std::regex include(R"(^\s*#\s*include\s*([<"])([^>"]+)[>"])");
	int scanned = 0;
	std::vector<std::string> files = sources;
	files.insert(files.end(), headers.begin(), headers.end());
	int hostLoads = 0;
	for (const std::string& file : files) {
		const std::string name = file.substr(file.rfind('/') + 1);
		const bool loading = name.rfind("loader", 0) == 0 || name.rfind("json", 0) == 0;
		const std::string text = support::readFile(file);
		bool fileAccessOnlyInLoad = true;
		if (!loading) {
			const std::vector<std::string> functions = enclosingFunctions(text);
			std::istringstream lines(text);
			std::string line;
			for (std::size_t number = 0; std::getline(lines, line); ++number) {
				bool counted = false;
				for (const char* call : {"std::time(", "std::rand(", "srand(", "getenv(", "fopen(", "ifstream", "ofstream", "std::filesystem", "socket("}) {
					if (line.find(call) == std::string::npos || line.find("#include") != std::string::npos) {
						continue;
					}
					const std::string& function = functions[number];
					// std::filesystem names the path a load opens (UTF-8 on every platform).
					const bool fileRead = std::string(call) == "ifstream" || std::string(call) == "fopen(" || std::string(call) == "std::filesystem";
					if (fileRead && function.rfind("load", 0) == 0) {
						hostLoads += counted ? 0 : 1;
						counted = true;
						continue;
					}
					fileAccessOnlyInLoad = false;
					t.expect(false, name + " is battle resolution code and uses " + call + (function.empty() ? "" : " in " + function));
				}
			}
		}
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line)) {
			std::smatch m;
			if (!std::regex_search(line, m, include)) {
				continue;
			}
			++scanned;
			const std::string target = m[2];
			if (m[1] == "<") {
				t.expect(standardHeaders().count(target) == 1, name + " includes <" + target + ">, which is not a standard C++ header");
				if (!loading && !((target == "fstream" || target == "filesystem") && fileAccessOnlyInLoad)) {
					t.expect(forbiddenInResolution().count(target) == 0, name + " is battle resolution code and includes <" + target + ">");
				}
			} else {
				t.expect(exists(core + "/include/" + target) || exists(core + "/src/" + target), name + " includes \"" + target + "\" from outside core/");
			}
		}
	}

	// Compile every source with nothing but core's own folders on the include path.
	const std::string out = t.path("acceptance/build/bat01");
	int status = 0;
	run("mkdir -p '" + out + "'", status);
	int compiled = 0;
	// The Makefile exports CXX; run by hand, the check falls back to clang++.
	const char* fromMake = std::getenv("CXX");
	const std::string compiler = fromMake != nullptr && *fromMake != '\0' ? fromMake : "clang++";
	for (const std::string& source : sources) {
		const std::string name = source.substr(source.rfind('/') + 1);
		const std::string command = compiler + " -std=c++17 -Wall -Wextra -Werror -O0 -I'" + core + "/include' -I'" + core + "/src' -c '" + source + "' -o '" + out + "/" + name + ".o'";
		const std::string output = run(command, status);
		if (t.expect(status == 0, "isolated compile failed for " + name + ": " + output.substr(0, 200))) {
			++compiled;
		}
	}

	// The library's own suite, from core/ because it loads data/.
	const std::string suite = run("cd '" + core + "' && ./build/rules_tests", status);
	t.expect(status == 0, "core suite build/rules_tests failed: " + suite.substr(suite.size() > 300 ? suite.size() - 300 : 0));

	const std::string version = run(compiler + " --version | head -n 1", status);
	t.note(std::to_string(compiled) + " core sources compile with only core/include and core/src on the include path; " +
		std::to_string(scanned) + " includes are standard or core-local; resolution code has no clock, file, network or thread access (" + std::to_string(hostLoads) +
		" file reads sit in host-side load functions outside loader and json sources); core suite passes");
	t.note("compilers covered: " + version.substr(0, version.find('\n')) + " only; other target compilers are not installed on this host");
}
