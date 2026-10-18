#include "TimingInterpolationPreview.h"
#include "utils/MathUtils.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>

namespace analysis
{
InterpolationPreviewResult buildInterpolationPreview(const InterpolationPreviewRequest &request,
                                                     const std::shared_ptr<std::atomic<bool>> &cancel)
{
    InterpolationPreviewResult out;
    out.options = request.options;
    auto &options = out.options;
    const auto &original = request.original;
    auto text = [](const char *source) { return QCoreApplication::translate("TimingInterpolationPanel", source); };
    if (cancel->load()) return out;
    if (!request.gap || !request.span || original.isEmpty() || !options.start.position().isValid()
        || options.start.position() < original.first().position())
        out.failure = text("Enter valid beats, a positive length/gap and Start at or after the first timing.");
    else
        for (int i = 0; i < original.size(); ++i)
            if (!original[i].position().isValid() || original[i].position() < BeatPosition(0, 0, 1)
                || !std::isfinite(original[i].bpm) || original[i].bpm <= 0 || original[i].bpm > 10000
                || (i && original[i - 1].position() >= original[i].position()))
            {
                out.failure = text("Existing timing contains duplicate/invalid points. Resolve them before applying.");
                break;
            }
    if (!out.failure.isEmpty() || cancel->load()) return out;
    options.length = *request.span; options.maximumGap = *request.gap;
    out.result = interpolateTiming(options);
    if (!out.result.valid()) { out.failure = QString::fromStdString(out.result.error); return out; }
    if (cancel->load()) return out;
    auto endpoint = out.result.nodes.back().beat;
    if (request.mode == 1) endpoint = request.end;
    out.result.nodes.back().beat = endpoint;
    double resumeBpm = original.first().bpm;
    for (const auto &point : original)
    {
        if (point.position() <= endpoint.position()) resumeBpm = point.bpm;
        if (point.position() < options.start.position() || point.position() > endpoint.position())
            out.proposal.append(point);
        else ++out.replaced;
    }
    for (std::size_t i = 0; i < out.result.nodes.size(); ++i)
    {
        if ((i & 255) == 0 && cancel->load()) return out;
        auto &node = out.result.nodes[i];
        const auto existing = std::lower_bound(original.cbegin(), original.cend(), node.beat.position(),
            [](const auto &point, const auto &position) { return point.position() < position; });
        if (existing != original.cend() && existing->position() == node.beat.position())
            node.beat = {existing->beatNum, existing->numerator, existing->denominator};
        const double bpm = i + 1 == out.result.nodes.size() && request.resume ? resumeBpm : node.bpm;
        out.storedBpms.append(bpm);
        out.proposal.append(BpmEntry(node.beat.whole, node.beat.numerator, node.beat.denominator, bpm));
    }
    std::stable_sort(out.proposal.begin(), out.proposal.end(), [](const auto &a, const auto &b) {
        return a.position() < b.position();
    });
    if (out.proposal.size() > 100000) out.failure = text("The complete timing list exceeds 100000 points.");
    const auto before = MathUtils::buildBpmTimeCache(original, request.offset);
    out.startMs = MathUtils::beatToMs(options.start.whole, options.start.numerator, options.start.denominator, before);
    const auto cache = MathUtils::buildBpmTimeCache(out.proposal, request.offset);
    for (const auto &node : out.result.nodes)
    {
        if (cancel->load()) return out;
        const double actual = MathUtils::beatToMs(node.beat.whole, node.beat.numerator, node.beat.denominator, cache);
        const double ideal = out.startMs + node.idealMilliseconds;
        if (!std::isfinite(actual) || !std::isfinite(ideal))
        { out.cacheError = std::numeric_limits<double>::infinity(); break; }
        out.cacheError = qMax(out.cacheError, qAbs(actual - ideal));
    }
    if (request.mode == 2) out.durationResidual = out.result.durationMilliseconds - request.duration;
    for (int i = 0; i < out.proposal.size(); ++i)
        if (!out.proposal[i].position().isValid() || !std::isfinite(out.proposal[i].bpm) || out.proposal[i].bpm <= 0
            || out.proposal[i].bpm > 10000 || (i && out.proposal[i - 1].position() >= out.proposal[i].position()))
            out.failure = text("Existing timing contains duplicate/invalid points. Resolve them before applying.");
    if (!std::isfinite(out.cacheError) || out.cacheError + out.result.maximumErrorMilliseconds
                                            + std::abs(out.durationResidual) >= options.maximumErrorMilliseconds)
        out.failure = text("Stored chart timing or duration rounding exceeds the requested error bound.");
    return out;
}
}
