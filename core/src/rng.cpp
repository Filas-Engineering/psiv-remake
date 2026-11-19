#include "rules/rng.h"

namespace rules {

std::uint64_t Rng::next() {
	state_ += 0x9E3779B97F4A7C15ULL;
	std::uint64_t z = state_;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

int Rng::range(int low, int high) {
	if (high <= low) {
		return low;
	}
	const std::uint64_t span = static_cast<std::uint64_t>(high - low) + 1;
	return low + static_cast<int>(next() % span);
}

bool Rng::chance(int percent) {
	if (percent >= 100) {
		return true;
	}
	if (percent <= 0) {
		return false;
	}
	return range(1, 100) <= percent;
}

}
