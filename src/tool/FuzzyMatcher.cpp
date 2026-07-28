#include "tool/FuzzyMatcher.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

std::optional<std::string> FuzzyMatcher::bestMatch(
    const std::string &input,
    const std::vector<std::string> &candidates,
    std::size_t maxDistance)
{
    std::optional<std::string> best;
    std::size_t bestDistance = std::numeric_limits<std::size_t>::max();

    for (const auto &candidate : candidates)
    {
        const std::size_t distance = editDistance(input, candidate);
        if (distance < bestDistance || (distance == bestDistance && best.has_value() && candidate.size() > best->size()))
        {
            bestDistance = distance;
            best = candidate;
        }
    }

    if (bestDistance <= maxDistance)
    {
        return best;
    }

    return std::nullopt;
}

std::size_t FuzzyMatcher::editDistance(const std::string &left, const std::string &right)
{
    std::vector<std::size_t> previous(right.size() + 1);
    std::vector<std::size_t> current(right.size() + 1);

    for (std::size_t i = 0; i <= right.size(); ++i)
    {
        previous[i] = i;
    }

    for (std::size_t i = 1; i <= left.size(); ++i)
    {
        current[0] = i;
        for (std::size_t j = 1; j <= right.size(); ++j)
        {
            const std::size_t replaceCost = left[i - 1] == right[j - 1] ? 0 : 1;
            current[j] = std::min({previous[j] + 1,
                                   current[j - 1] + 1,
                                   previous[j - 1] + replaceCost});
        }
        previous.swap(current);
    }

    return previous[right.size()];
}
