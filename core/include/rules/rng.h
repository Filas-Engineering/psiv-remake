#pragma once

#include <cstdint>

namespace rules {

// SplitMix64. Small, fast and fully determined by the seed.
class Rng {
public:
	explicit Rng(std::uint64_t seed) : state_(seed) {}

	std::uint64_t next();
	int range(int low, int high);
	bool chance(int percent);

private:
	std::uint64_t state_;
};

}
