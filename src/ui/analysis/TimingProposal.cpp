#include "TimingProposal.h"
#include "controller/ChartController.h"
#include "utils/MathUtils.h"
#include <QMessageBox>
#include <cmath>
#include "TimingPreviewJobs.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QPushButton>
#include <QtConcurrent/QtConcurrentRun>
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
void requestTimingProposal(QWidget *parent, ChartController *chart, const QString &title, const QString &description,
                           const QVector<BpmEntry> &entries, quint64 revision, std::function<bool()> sourceStillCurrent,
                           bool confirmEmptyChart)
{
    if (!parent->window()->property("cceNonModalTiming").toBool())
    {
        confirmTimingProposal(parent, chart, title, description, entries, revision, sourceStillCurrent, confirmEmptyChart);
        return;
    }
    if (parent->property("timingProposalPending").toBool() || revision != chart->revision()
        || (sourceStillCurrent && !sourceStillCurrent())) return;
    if (!confirmEmptyChart && chart->chart()->notes().isEmpty())
    { chart->replaceBpmList(title, entries); return; }
    struct Impact { int affected = 0; double maximum = 0; bool valid = false; };
    // Timing and Notes are implicitly shared value snapshots. Workers never
    // access a controller or QWidget and do not copy a full Chart.
    const auto original = chart->chart()->bpmList();
    const auto notes = chart->chart()->notes();
    const int offset = chart->chart()->meta().offset;
    parent->setProperty("timingProposalPending", true);
    auto *watcher = new QFutureWatcher<Impact>(parent);
    QPointer<ChartController> controller(chart);
    QObject::connect(watcher, &QFutureWatcher<Impact>::finished, parent,
        [=] {
            const auto impact = watcher->result();
            watcher->deleteLater();
            parent->setProperty("timingProposalPending", false);
            if (!impact.valid || !controller || !parent->isVisible() || revision != controller->revision()
                || (sourceStillCurrent && !sourceStillCurrent())) return;
            auto *box = new QMessageBox(QMessageBox::Question, title,
                description + QStringLiteral("\n\n")
                    + QObject::tr("%1 notes may move in audio time (maximum %2 ms). Their beat coordinates and chart offset "
                                  "stay unchanged. Apply as one undoable timing edit?").arg(impact.affected).arg(impact.maximum, 0, 'f', 3),
                QMessageBox::Yes | QMessageBox::No, parent);
            box->setObjectName("bpm.applyConfirmation");
            box->setDefaultButton(QMessageBox::No);
            box->setWindowModality(Qt::NonModal);
            box->setAttribute(Qt::WA_DeleteOnClose);
            parent->setProperty("timingProposalPending", true);
            QObject::connect(controller, &ChartController::chartChanged, box, &QDialog::reject);
            QObject::connect(box, &QDialog::finished, parent, [=](int answer) {
                parent->setProperty("timingProposalPending", false);
                if (answer == QMessageBox::Yes && controller && parent->isVisible() && revision == controller->revision()
                    && (!sourceStillCurrent || sourceStillCurrent()))
                    controller->replaceBpmList(title, entries);
            });
            // show(), rather than exec()/open(), leaves all editor windows usable.
            box->show();
        });
    watcher->setFuture(QtConcurrent::run(timingPreviewPool(), [=] {
        Impact impact;
        impact.valid = validateTimingProposal(entries).isEmpty();
        if (!impact.valid) return impact;
        const auto before = MathUtils::buildBpmTimeCache(original, offset);
        const auto after = MathUtils::buildBpmTimeCache(entries, offset);
        for (const auto &note : notes)
        {
            double difference = qAbs(MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, before)
                                     - MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, after));
            if (note.isRainNote())
                difference = qMax(difference, qAbs(MathUtils::beatToMs(note.endBeatNum, note.endNumerator, note.endDenominator, before)
                    - MathUtils::beatToMs(note.endBeatNum, note.endNumerator, note.endDenominator, after)));
            impact.maximum = qMax(impact.maximum, difference);
            if (difference > .001) ++impact.affected;
        }
        return impact;
    }));
}
} // namespace analysis
