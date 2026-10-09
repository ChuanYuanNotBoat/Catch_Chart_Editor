#include "TimingProposal.h"
#include "controller/ChartController.h"
#include "utils/MathUtils.h"
#include <QMessageBox>
#include <cmath>
namespace analysis
{
QString validateTimingProposal(const QVector<BpmEntry> &entries)
{
    if (entries.isEmpty() || entries.size() > 100000)
        return QStringLiteral("Invalid timing list size");
    for (qsizetype i = 0; i < entries.size(); ++i)
        if (!entries[i].position().isValid() || entries[i].position().toDouble() < 0 || !std::isfinite(entries[i].bpm)
            || entries[i].bpm <= 0 || entries[i].bpm > 10000
            || (i && entries[i - 1].position() >= entries[i].position()))
            return QStringLiteral(
                "Timing entries must have valid exact beats, positive BPM and strictly increasing positions");
    return {};
}
bool confirmTimingProposal(QWidget *parent, ChartController *chart, const QString &title, const QString &description,
                           const QVector<BpmEntry> &entries, quint64 revision, std::function<bool()> sourceStillCurrent,
                           bool confirmEmptyChart)
{
    if (revision != chart->revision() || !validateTimingProposal(entries).isEmpty()
        || (sourceStillCurrent && !sourceStillCurrent()))
        return false;
    if (!confirmEmptyChart && chart->chart()->notes().isEmpty())
        return chart->replaceBpmList(title, entries);
    const auto before = MathUtils::buildBpmTimeCache(chart->chart()->bpmList(), chart->chart()->meta().offset);
    const auto after = MathUtils::buildBpmTimeCache(entries, chart->chart()->meta().offset);
    double maximum = 0;
    int affected = 0;
    for (const auto &note : chart->chart()->notes())
    {
        double difference = qAbs(MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, before)
                                 - MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, after));
        if (note.isRainNote())
            difference =
                qMax(difference,
                     qAbs(MathUtils::beatToMs(note.endBeatNum, note.endNumerator, note.endDenominator, before)
                          - MathUtils::beatToMs(note.endBeatNum, note.endNumerator, note.endDenominator, after)));
        maximum = qMax(maximum, difference);
        if (difference > .001)
            ++affected;
    }
    const auto answer = QMessageBox::question(
        parent, title,
        description + QStringLiteral("\n\n")
            + QObject::tr("%1 notes may move in audio time (maximum %2 ms). Their beat coordinates and chart offset "
                          "stay unchanged. Apply as one undoable timing edit?")
                  .arg(affected)
                  .arg(maximum, 0, 'f', 3),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes || revision != chart->revision() || (sourceStillCurrent && !sourceStillCurrent()))
        return false;
    return chart->replaceBpmList(title, entries);
}
} // namespace analysis
