#include "TimingMeasurement.h"
#include <charconv>
#include <cmath>
#include <numeric>

namespace analysis
{
namespace
{
bool integer(std::string_view text, std::int64_t &value)
{
    if (text.empty() || text.size() > 16)
        return false;
    for (char c : text)
        if (c < '0' || c > '9')
            return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}
} // namespace
std::optional<BeatSpan> parseBeatSpan(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    std::int64_t n = 0, d = 1;
    const auto slash = text.find('/'), dot = text.find('.');
    if (slash != std::string_view::npos)
    {
        if (!integer(text.substr(0, slash), n) || !integer(text.substr(slash + 1), d))
            return {};
    }
    else if (dot != std::string_view::npos)
    {
        const auto whole = text.substr(0, dot), fraction = text.substr(dot + 1);
        std::int64_t w = 0, f = 0;
        if (fraction.empty() || fraction.size() > 9 || (!whole.empty() && !integer(whole, w))
            || !integer(fraction, f) || w > 1000000)
            return {};
        for (std::size_t i = 0; i < fraction.size(); ++i)
            d *= 10;
        n = w * d + f;
    }
    else if (!integer(text, n))
        return {};
    if (n <= 0 || d <= 0 || d > 1000000000 || double(n) / double(d) > 1000000)
        return {};
    const auto divisor = std::gcd(n, d);
    return BeatSpan{n / divisor, d / divisor};
}
TimingMeasurement measureTiming(BeatSpan span, double start, double end)
{
    TimingMeasurement result;
    if (span.numerator <= 0 || span.denominator <= 0 || !std::isfinite(start) || !std::isfinite(end)
        || start < 0 || end <= start || !std::isfinite(span.beats()) || span.beats() > 1000000)
        return result;
    result.durationMilliseconds = end - start;
    result.bpm = 60000 * span.beats() / result.durationMilliseconds;
    result.valid = std::isfinite(result.bpm) && result.bpm >= .001 && result.bpm <= 10000;
    return result;
}
} // namespace analysis
