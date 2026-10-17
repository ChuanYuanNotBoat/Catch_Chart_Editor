#include "TimingToolsPanel.h"
#include "AnalysisCanvas.h"
#include "TimingProposal.h"
#include <algorithm>
#include "controller/ChartController.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QShowEvent>
#include <QHideEvent>
#include <cmath>

TimingToolsPanel::TimingToolsPanel(ChartController *chart, AnalysisCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_chart(chart), m_canvas(canvas)
{
    setObjectName("analysis.measurePanel");
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("Pick Start on the chart grid, then End freely on the spectrum. "
                              "Drag either marker to adjust. Ctrl + wheel zooms; Tab edits Beat span."));
    help->setWordWrap(true);
    layout->addWidget(help);
    m_pick = new QCheckBox(tr("Pick Start → End"));
    m_pick->setObjectName("analysis.measurePick");
    layout->addWidget(m_pick);
    auto *form = new QFormLayout;
    m_start = new QLabel(tr("Click the spectrum to choose Start"));
    m_start->setObjectName("analysis.measureStart");
    m_start->setWordWrap(true);
    m_start->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Start beat"), m_start);
    m_end = new QDoubleSpinBox;
    m_end->setObjectName("analysis.measureEnd");
    m_end->setDecimals(3);
    m_end->setRange(0, 1e12);
    m_end->setSuffix(tr(" ms"));
    m_end->setKeyboardTracking(false);
    m_end->setEnabled(false);
    form->addRow(tr("End audio time"), m_end);
    m_span = new QLineEdit("1");
    m_span->setObjectName("analysis.measureSpan");
    m_span->setPlaceholderText(tr("1, 0.5, 1/3, 4…"));
    m_span->setToolTip(tr("Measured interval in beats. Positive decimal or fraction; default 1 beat."));
    form->addRow(tr("Beat span"), m_span);
    layout->addLayout(form);
    m_preview = new QCheckBox(tr("Preview measured grid"));
    m_preview->setObjectName("analysis.measurePreview");
    m_preview->setChecked(true);
    layout->addWidget(m_preview);
    m_summary = new QLabel;
    m_summary->setObjectName("analysis.measureSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summary);
    m_effect = new QLabel;
    m_effect->setObjectName("analysis.measureEffect");
    m_effect->setWordWrap(true);
    layout->addWidget(m_effect);
    m_copy = new QPushButton(tr("Copy BPM"));
    m_copy->setObjectName("analysis.measureCopy");
    layout->addWidget(m_copy);
    m_apply = new QPushButton(tr("Apply BPM at Start"));
    m_apply->setObjectName("analysis.measureApply");
    layout->addWidget(m_apply);
    auto *clear = new QPushButton(tr("Clear / measure again"));
    clear->setObjectName("analysis.measureClear");
    layout->addWidget(clear);
    layout->addStretch();
    connect(clear, &QPushButton::clicked, this, &TimingToolsPanel::clearMeasurement);
    connect(m_pick, &QCheckBox::toggled, this, [this](bool enabled) {
        m_canvas->setMeasurementPicking(enabled);
        if (enabled)
        {
            emit pickingStarted();
            m_canvas->setFocus();
        }
    });
    connect(canvas, &AnalysisCanvas::measurementStartRequested, this, &TimingToolsPanel::setStart);
    connect(canvas, &AnalysisCanvas::measurementEndRequested, this, &TimingToolsPanel::setEnd);
    connect(canvas, &AnalysisCanvas::measurementCancelRequested, this, &TimingToolsPanel::clearMeasurement);
    connect(canvas, &AnalysisCanvas::measurementDetailsRequested, this, [this] {
        m_span->setFocus();
        m_span->selectAll();
    });
    connect(m_end, &QDoubleSpinBox::valueChanged, this, &TimingToolsPanel::setEnd);
    connect(m_span, &QLineEdit::textChanged, this, &TimingToolsPanel::refresh);
    connect(m_preview, &QCheckBox::toggled, this, &TimingToolsPanel::refresh);
    connect(m_apply, &QPushButton::clicked, this, &TimingToolsPanel::apply);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        if (m_result.valid)
            QApplication::clipboard()->setText(QString::number(m_result.bpm, 'g', 17));
    });
    connect(chart, &ChartController::chartLoaded, this, &TimingToolsPanel::clearMeasurement);
    connect(chart, &ChartController::bpmListChanged, this, &TimingToolsPanel::clearMeasurement);
    connect(chart, &ChartController::metaDataChanged, this, &TimingToolsPanel::clearMeasurement);
    connect(chart, &ChartController::notesChanged, this, &TimingToolsPanel::refresh);
    refresh();
}
double TimingToolsPanel::startTime() const
{
    return m_startBeat ? m_canvas->timeAtBeat(m_startBeat->position().toDouble()) : 0;
}
int TimingToolsPanel::matchingTiming() const
{
    int found = -1;
    const auto &list = m_chart->chart()->bpmList();
    for (int i = 0; m_startBeat && i < list.size(); ++i)
        if (list[i].position() == m_startBeat->position())
        {
            if (found >= 0)
                return -2;
            found = i;
        }
    return found;
}
void TimingToolsPanel::setStart(int b, int n, int d)
{
    const BpmEntry point(b, n, d, 0);
    if (!point.position().isValid() || point.position().toDouble() < 0)
        return;
    m_startBeat = point; // keep the original triplet, never round-trip through ms
    m_end->setEnabled(true);
    refresh();
}
void TimingToolsPanel::setEnd(double ms)
{
    if (!m_startBeat || !std::isfinite(ms) || ms < 0 || ms > m_end->maximum())
        return;
    m_endMs = ms;
    const QSignalBlocker blocker(m_end);
    m_end->setValue(ms);
    refresh();
}
void TimingToolsPanel::stopPicking()
{
    m_pick->setChecked(false);
}
void TimingToolsPanel::clearMeasurement()
{
    stopPicking();
    m_startBeat.reset();
    m_endMs.reset();
    m_end->setEnabled(false);
    const QSignalBlocker blocker(m_end);
    m_end->setValue(0);
    const QSignalBlocker spanBlocker(m_span);
    m_span->setText("1");
    refresh();
}
void TimingToolsPanel::refresh()
{
    const auto span = analysis::parseBeatSpan(m_span->text().toStdString());
    m_result = span && m_startBeat && m_endMs
                   ? analysis::measureTiming(*span, startTime(), *m_endMs)
                   : analysis::TimingMeasurement{};
    m_start->setText(m_startBeat ? tr("[%1, %2, %3] · %4 ms")
                                     .arg(m_startBeat->beatNum).arg(m_startBeat->numerator)
                                     .arg(m_startBeat->denominator).arg(startTime(), 0, 'f', 3)
                               : tr("Click the spectrum to choose Start"));
    m_summary->setText(!span ? tr("Enter a positive decimal or fraction (up to 9 decimal places).")
                      : !m_startBeat ? tr("Default interval: 1 beat. Enable Pick to begin.")
                      : !m_endMs ? tr("Start selected. Click End freely on the spectrum.")
                      : !m_result.valid ? tr("End must follow Start; BPM must be 0.001–10000.")
                      : tr("Δt %1 ms · %2/%3 beat\n%4 BPM")
                            .arg(m_result.durationMilliseconds, 0, 'f', 3)
                            .arg(span->numerator).arg(span->denominator).arg(m_result.bpm, 0, 'f', 6));
    int match = matchingTiming();
    bool canApply = m_result.valid && match != -2;
    QString effect;
    if (m_startBeat)
    {
        effect = match == -2 ? tr("Multiple timing points share Start. Resolve them before applying.")
                            : match >= 0 ? tr("Update the existing timing at Start; preserve its beat triplet.")
                                         : tr("Add one timing point at Start.");
        if (m_chart->chart()->bpmList().isEmpty()
            || m_startBeat->position() < m_chart->chart()->bpmList().first().position())
        {
            effect += tr("\nStart must be at or after the first chart timing point.");
            canApply = false;
        }
        for (const auto &point : m_chart->chart()->bpmList())
            if (point.position() > m_startBeat->position())
            {
                effect += tr("\nThe next timing at [%1, %2, %3] remains in place.")
                              .arg(point.beatNum).arg(point.numerator).arg(point.denominator);
                if (span && point.position().toDouble() < m_startBeat->position().toDouble() + span->beats() - 1e-9)
                {
                    effect += tr(" The measured interval crosses it; shorten Beat span before applying.");
                    canApply = false;
                }
                break;
            }
        effect += tr("\nApplying changes later audio times, keeps note beats and offset, and is undoable.");
        if (match >= 0 && m_result.valid && m_chart->chart()->bpmList()[match].bpm == m_result.bpm)
            canApply = false;
    }
    m_effect->setText(effect);
    m_apply->setEnabled(canApply);
    m_copy->setEnabled(m_result.valid);
    m_canvas->setMeasurementOverlay(isVisible() && m_startBeat ? std::optional<double>(startTime()) : std::nullopt,
                                   isVisible() ? m_endMs : std::nullopt,
                                   m_result.valid && m_preview->isChecked() ? m_result.bpm : 0,
                                   m_startBeat ? tr("Start [%1,%2,%3]").arg(m_startBeat->beatNum)
                                                  .arg(m_startBeat->numerator).arg(m_startBeat->denominator)
                                               : QString());
}
void TimingToolsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}
void TimingToolsPanel::hideEvent(QHideEvent *event)
{
    stopPicking();
    m_canvas->setMeasurementOverlay({}, {}, 0, {});
    QWidget::hideEvent(event);
}
void TimingToolsPanel::apply()
{
    refresh();
    if (!m_apply->isEnabled())
        return;
    auto entries = m_chart->chart()->bpmList();
    const int match = matchingTiming();
    auto point = match >= 0 ? entries[match] : *m_startBeat;
    point.bpm = m_result.bpm;
    if (match >= 0)
        entries[match] = point;
    else
        entries.append(point);
    std::stable_sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.position() < b.position();
    });
    analysis::confirmTimingProposal(this, m_chart, tr("Apply measured BPM"),
                                    tr("Set %1 BPM at [%2, %3, %4]?")
                                        .arg(m_result.bpm, 0, 'f', 6)
                                        .arg(point.beatNum)
                                        .arg(point.numerator)
                                        .arg(point.denominator),
                                    entries, m_chart->revision(), {}, false);
}
QJsonObject TimingToolsPanel::diagnostics() const
{
    QJsonObject result{{"kind", "manual BPM measurement"}, {"valid", m_result.valid},
                       {"spanInput", m_span->text()}, {"defaultSpanBeats", 1},
                       {"chartRevision", double(m_chart->revision())}};
    if (m_startBeat)
    {
        result["startBeat"] = QJsonArray{m_startBeat->beatNum, m_startBeat->numerator, m_startBeat->denominator};
        result["startAudioMilliseconds"] = startTime();
    }
    if (m_endMs)
        result["endAudioMilliseconds"] = *m_endMs;
    if (m_result.valid)
    {
        result["durationMilliseconds"] = m_result.durationMilliseconds;
        result["bpm"] = m_result.bpm;
    }
    result["applyEnabled"] = m_apply->isEnabled();
    return result;
}
