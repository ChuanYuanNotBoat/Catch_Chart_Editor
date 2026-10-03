#pragma once

#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>

// Exact comparison boundary for legacy [whole, numerator, denominator] beats.
// Normalization is local to this value: the source triplet (and its colour /
// subdivision meaning) is never rewritten. No Qt or compiler-specific integers.
class BeatPosition
{
public:
    BeatPosition(int whole, int numerator, int denominator) noexcept
        : m_whole(whole), m_numerator(numerator), m_denominator(denominator)
    {
        if (!isValid())
            return;

        m_whole += m_numerator / m_denominator;
        m_numerator %= m_denominator;
        if (m_numerator < 0)
        {
            --m_whole;
            m_numerator += m_denominator;
        }
        const auto divisor = std::gcd(m_numerator, m_denominator);
        m_numerator /= divisor;
        m_denominator /= divisor;
    }

    bool isValid() const noexcept { return m_denominator > 0; }

    // Canonical projection keeps equivalent triplets identical at the UI/audio
    // boundary, even when evaluating raw whole + improper fraction would round
    // differently. Logical comparisons still use compare(), never this value.
    double toDouble() const noexcept
    {
        if (!isValid())
            return std::numeric_limits<double>::quiet_NaN();
        return static_cast<double>(m_whole) +
               static_cast<double>(m_numerator) / static_cast<double>(m_denominator);
    }

    int compare(const BeatPosition &other) const noexcept
    {
        // Malformed in-memory data must still have a strict ordering for sort.
        // It sorts before valid beats; validation remains the caller's job.
        if (isValid() != other.isValid())
            return isValid() ? 1 : -1;
        if (!isValid())
        {
            const auto left = std::tie(m_whole, m_numerator, m_denominator);
            const auto right = std::tie(other.m_whole, other.m_numerator, other.m_denominator);
            return left < right ? -1 : (right < left ? 1 : 0);
        }
        if (m_whole != other.m_whole)
            return m_whole < other.m_whole ? -1 : 1;

        // Proper fractional parts are below INT_MAX, so both products fit int64.
        // Comparing the full improper numerators instead would need wider types.
        const auto left = m_numerator * other.m_denominator;
        const auto right = other.m_numerator * m_denominator;
        return left < right ? -1 : (left > right ? 1 : 0);
    }

    bool operator==(const BeatPosition &other) const noexcept { return compare(other) == 0; }
    bool operator!=(const BeatPosition &other) const noexcept { return compare(other) != 0; }
    bool operator<(const BeatPosition &other) const noexcept { return compare(other) < 0; }
    bool operator>(const BeatPosition &other) const noexcept { return compare(other) > 0; }
    bool operator<=(const BeatPosition &other) const noexcept { return compare(other) <= 0; }
    bool operator>=(const BeatPosition &other) const noexcept { return compare(other) >= 0; }

private:
    std::int64_t m_whole;
    std::int64_t m_numerator;
    std::int64_t m_denominator;
};
