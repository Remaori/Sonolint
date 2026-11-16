#include "core/analysis/fft.h"

// One thread and no compiler-specific vector code: the same arithmetic on every
// platform, and nothing to link against.
#define POCKETFFT_NO_MULTITHREADING
#define POCKETFFT_NO_VECTORS
#include <pocketfft_hdronly.h>

namespace sonolint
{
namespace
{
constexpr auto kRealStride    = static_cast<std::ptrdiff_t> (sizeof (double));
constexpr auto kComplexStride = static_cast<std::ptrdiff_t> (sizeof (std::complex<double>));
} // namespace

std::vector<std::complex<double>> realFft (std::span<const double> x)
{
    std::vector<std::complex<double>> spectrum (x.size() / 2 + 1);
    pocketfft::r2c<double> ({ x.size() }, { kRealStride }, { kComplexStride }, 0, pocketfft::FORWARD,
                            x.data(), spectrum.data(), 1.0);
    return spectrum;
}

std::vector<double> inverseRealFft (std::span<const std::complex<double>> spectrum, size_t size)
{
    std::vector<double> x (size);
    pocketfft::c2r<double> ({ size }, { kComplexStride }, { kRealStride }, 0, pocketfft::BACKWARD,
                            spectrum.data(), x.data(), 1.0 / (double) size);
    return x;
}
} // namespace sonolint
