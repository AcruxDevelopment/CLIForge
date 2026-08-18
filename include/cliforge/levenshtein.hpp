#pragma once
//
// levenshtein.hpp
//
// Plain edit-distance + a small "closest matches" helper, used to power
// the "did you mean...?" suggestions for unknown commands, flags and
// options.
//
#include <algorithm>
#include <string>
#include <vector>

namespace cliforge::detail {

inline std::size_t levenshtein(const std::string& a, const std::string& b) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::size_t> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = j;
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= m; ++j) {
            std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

// Returns up to `limit` candidates from `pool` that are close to `query`,
// ordered by distance. Anything farther than a generous relative
// threshold is dropped so we never suggest something wildly unrelated.
inline std::vector<std::string> closestMatches(const std::string& query,
                                                 const std::vector<std::string>& pool,
                                                 std::size_t limit = 3) {
    std::vector<std::pair<std::size_t, std::string>> scored;
    scored.reserve(pool.size());
    for (const auto& candidate : pool) {
        std::size_t d = levenshtein(query, candidate);
        // A prefix relationship (e.g. "--arch" for "--architectures") is a
        // very strong signal -- treat it as near-identical even though its
        // raw edit distance is large, so common abbreviations still surface.
        if (query.size() >= 2 && candidate.size() > query.size() &&
            candidate.compare(0, query.size(), query) == 0) {
            d = std::min<std::size_t>(d, 1);
        }
        scored.emplace_back(d, candidate);
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    std::size_t threshold = std::max<std::size_t>(3, query.size() / 2);
    std::vector<std::string> out;
    for (auto& [dist, name] : scored) {
        if (out.size() >= limit) break;
        if (dist <= threshold) out.push_back(name);
    }
    return out;
}

}  // namespace cliforge::detail
