#include "support.h"

#include "rules/event.h"
#include "rules/record.h"

#include <cstdio>
#include <cstdlib>

// Plays seeded random battles and prints each battle record and its event stream, so ACC-BAT-15 can
// compare the bytes this build produces with another build's.
int main(int argc, char** argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: %s REPO_ROOT COUNT\n", argv[0]);
		return 2;
	}
	const acc::Context context(argv[1]);
	const rules::Database& db = support::data(context);
	rules::ComboLog log;
	const int count = std::atoi(argv[2]);
	for (int seed = 1; seed <= count; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, static_cast<std::uint64_t>(seed), log);
		std::printf("== battle %d\n%s-- events\n%s", seed, rules::serializeRecord(played.record).c_str(), rules::serializeEvents(played.events).c_str());
	}
	return 0;
}
