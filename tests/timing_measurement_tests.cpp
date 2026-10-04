#include "analysis/TimingMeasurement.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

void require(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
int main()
{
    using namespace analysis;
    const auto third = parseBeatSpan("1/3");
    require(third && third->numerator == 1 && third->denominator == 3, "exact third");
    const auto decimal = parseBeatSpan(" 1.125\t");
    require(decimal && decimal->numerator == 9 && decimal->denominator == 8, "exact decimal");
    const auto half = parseBeatSpan(".5");
    require(half && half->numerator == 1 && half->denominator == 2, "leading-dot decimal");
    const auto tiny = parseBeatSpan("0.000000001");
    require(tiny && tiny->denominator == 1000000000, "decimal precision");
    for (const auto text : {"", "0", "-1", "1/0", "1/2/3", "nan", "inf", "1e3", "1.2.3", "1/2.0",
                            "0.0000000001", "1000001", "1/1000000001", "99999999999999999999"})
        require(!parseBeatSpan(text), "reject malformed span");
    const auto measured = measureTiming(*third, 17345.125, 17511.791666666667);
    require(measured.valid && std::abs(measured.bpm - 120) < 1e-8, "absolute-time rational measurement");
    const auto single = measureTiming({}, 1000, 1352.941176470588);
    require(single.valid && std::abs(single.bpm - 170) < 1e-9, "one-beat default");
    for (double end : {1000., 999., std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN(), 1000.00001})
        require(!measureTiming({}, 1000, end).valid, "reject reversed/nonfinite/out-of-range BPM");
    require(!measureTiming({1, 0}, 0, 500).valid, "invalid rational");
    require(!measureTiming({}, -1, 500).valid, "start before audio");
    std::cout << "Timing measurement tests passed\n";
}
