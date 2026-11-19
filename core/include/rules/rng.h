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

	// The whole generator state: Rng(state()) continues the same stream.
	std::uint64_t state() const { return state_; }
	void restore(std::uint64_t state) { state_ = state; }

private:
	std::uint64_t state_;
};

}
