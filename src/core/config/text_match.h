#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sonolint
{
// True if `text` matches `pattern`, where * stands for any run of characters
// (also none) and ? for exactly one.
bool globMatch (std::string_view pattern, std::string_view text);

// The candidate that `name` is most likely a typo of, or "" when none is close.
std::string closestMatch (std::string_view name, const std::vector<std::string>& candidates);

// "; did you mean "x"?" for the closest candidate, or "" when none is close.
std::string didYouMean (std::string_view name, const std::vector<std::string>& candidates);
} // namespace sonolint
