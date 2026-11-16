#include "core/config/text_match.h"

#include <algorithm>

namespace sonolint
{
namespace
{
// Edit distance: how many characters to insert, delete or replace to turn a into b.
size_t editDistance (std::string_view a, std::string_view b)
{
    std::vector<size_t> row (b.size() + 1);

    for (size_t j = 0; j <= b.size(); ++j)
        row[j] = j;

    for (size_t i = 1; i <= a.size(); ++i)
    {
        auto diagonal = row[0];
        row[0] = i;

        for (size_t j = 1; j <= b.size(); ++j)
        {
            const auto above = row[j];
            row[j] = std::min ({ above + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1) });
            diagonal = above;
        }
    }

    return row[b.size()];
}
} // namespace

bool globMatch (std::string_view pattern, std::string_view text)
{
    size_t p = 0, t = 0;
    auto star = std::string_view::npos;   // the last * seen in the pattern
    size_t starText = 0;                  // and how much text it has taken so far

    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            starText = t;
        }
        else if (star != std::string_view::npos)
        {
            p = star + 1;   // let the last * take one more character and try again
            t = ++starText;
        }
        else
        {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*')
        ++p;

    return p == pattern.size();
}

std::string closestMatch (std::string_view name, const std::vector<std::string>& candidates)
{
    std::string best;
    auto bestDistance = std::max<size_t> (2, name.size() / 3) + 1;   // further than this isn't a typo

    for (const auto& candidate : candidates)
    {
        const auto distance = editDistance (name, candidate);

        if (distance < bestDistance)
        {
            best = candidate;
            bestDistance = distance;
        }
    }

    return best;
}

std::string didYouMean (std::string_view name, const std::vector<std::string>& candidates)
{
    const auto match = closestMatch (name, candidates);
    return match.empty() ? "" : "; did you mean \"" + match + "\"?";
}
} // namespace sonolint
