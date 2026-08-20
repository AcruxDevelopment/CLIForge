#pragma once

// Plain edit-distance + a small "closest matches" helper, used to power
// the "did you mean...?" suggestions for unknown commands, flags and
// options.

#include <algorithm>
#include <string>
#include <vector>

namespace cliforge::detail
{
	std::size_t levenshtein(const std::string& a, const std::string& b);

	// Returns up to `limit` candidates from `pool` that are close to `query`,
	// ordered by distance. Anything farther than a generous relative
	// threshold is dropped so we never suggest something wildly unrelated.
	std::vector<std::string> closestMatches(const std::string& query,
											const std::vector<std::string>& pool,
											std::size_t limit = 3);
}
