#pragma once

#include <stdexcept>
#include <string>

namespace sonolint
{
// The values are the process exit codes. 0 (everything passed) and 1 (failures
// according to --fail-on) are results of a run, not errors.
enum class ErrorCode
{
    config = 2,       // bad sonolint.yml, or bad command-line usage
    pluginLoad = 3,   // the plugin could not be loaded or instantiated
    internal = 4      // a bug in Sonolint
};

class Error : public std::runtime_error
{
public:
    Error (ErrorCode errorCode, const std::string& message)
        : std::runtime_error (message), code (errorCode)
    {
    }

    ErrorCode code;
};
} // namespace sonolint
