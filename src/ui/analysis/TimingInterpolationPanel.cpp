#include "TimingInterpolationPanel.h"
#include "AnalysisCanvas.h"
#include "controller/ChartController.h"
#include "utils/MathUtils.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QFileDialog>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QShowEvent>
#include <QHideEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QString beatText(analysis::BeatTriplet beat)
{
    return QString("[%1,%2,%3]").arg(beat.whole).arg(beat.numerator).arg(beat.denominator);
}
QJsonArray beatJson(analysis::BeatTriplet beat) { return {beat.whole, beat.numerator, beat.denominator}; }
} // namespace
TimingInterpolationPanel::TimingInterpolationPanel(ChartController *chart, AnalysisCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_chart(chart), m_canvas(canvas)
{
    setObjectName("analysis.interpolationPanel");
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("BPM varies linearly with beat. Stored segment BPMs match the exact curve "
                              "duration; difficult regions subdivide locally to stay below the error limit."));
    help->setWordWrap(true);
    layout->addWidget(help);
    m_pick = new QCheckBox(tr("Pick Start on spectrum"));
    m_pick->setObjectName("analysis.interpolationPick");
    layout->addWidget(m_pick);
    auto *form = new QFormLayout;
    auto beatFields = [&](std::array<QSpinBox *, 3> &fields, const QString &prefix) {
        auto *widget = new QWidget;
        auto *row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0);
        const QString names[]{"Whole", "Numerator", "Denominator"};
        for (int i = 0; i < 3; ++i)
        {
            fields[i] = new QSpinBox;
            fields[i]->setRange(i == 2 ? 1 : std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            fields[i]->setValue(i == 2 ? 1 : 0);
            fields[i]->setKeyboardTracking(false);
            fields[i]->setObjectName(prefix + names[i]);
            fields[i]->setToolTip(names[i]);
            row->addWidget(fields[i]);
        }
        return widget;
    };
    form->addRow(tr("Start beat"), beatFields(m_start, "analysis.interpolationStart"));
    form->addRow(tr("End beat"), beatFields(m_end, "analysis.interpolationEnd"));
    auto number = [&](const QString &name, double min, double max, int decimals, double value) {
        auto *spin = new QDoubleSpinBox;
        spin->setObjectName(name);
        spin->setDecimals(decimals);
        spin->setRange(min, max);
        spin->setValue(value);
        spin->setKeyboardTracking(false);
        return spin;
    };
    m_startBpm = number("analysis.interpolationStartBpm", .001, 10000, 6, 120);
    m_endBpm = number("analysis.interpolationEndBpm", .001, 10000, 6, 180);
    form->addRow(tr("Start BPM"), m_startBpm);
    form->addRow(tr("End BPM"), m_endBpm);
    m_mode = new QComboBox;
    m_mode->setObjectName("analysis.interpolationRangeMode");
    m_mode->addItems({tr("Beat length"), tr("Start / End beat"), tr("Duration ms")});
    form->addRow(tr("Range via"), m_mode);
    m_length = new QLineEdit("8");
    m_length->setObjectName("analysis.interpolationLength");
    form->addRow(tr("Beat length"), m_length);
    m_duration = number("analysis.interpolationDuration", .001, 1e12, 3, 2000);
    form->addRow(tr("Duration ms"), m_duration);
    m_gap = new QLineEdit("1");
    m_gap->setObjectName("analysis.interpolationGap");
    m_gap->setToolTip(tr("Maximum spacing in beats; decimal or fraction. Default 1 beat."));
    form->addRow(tr("Maximum gap"), m_gap);
    m_error = number("analysis.interpolationError", .001, 10, 3, 10);
    m_error->setSuffix(tr(" ms"));
    form->addRow(tr("Error below"), m_error);
    layout->addLayout(form);
    m_resume = new QCheckBox(tr("Resume chart BPM at End"));
    m_resume->setObjectName("analysis.interpolationResume");
    m_resume->setChecked(true);
    m_resume->setToolTip(tr("On: restore the original active BPM at End. Off: continue the specified End BPM. "
                           "Later timing points keep their original beat coordinates and BPM."));
    m_preview = new QCheckBox(tr("Preview interpolation"));
    m_preview->setObjectName("analysis.interpolationPreview");
    m_preview->setChecked(true);
    layout->addWidget(m_resume);
    layout->addWidget(m_preview);
    m_summary = new QLabel;
    m_summary->setObjectName("analysis.interpolationSummary");
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_summary);
    m_table = new QTableWidget(0, 4);
    m_table->setObjectName("analysis.interpolationNodes");
    m_table->setHorizontalHeaderLabels({tr("Beat"), tr("Audio ms"), tr("Stored BPM"), tr("Error ms")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setMinimumHeight(120);
    m_table->setMaximumHeight(220);
    layout->addWidget(m_table);
    auto *build = new QPushButton(tr("Rebuild preview"));
    build->setObjectName("analysis.interpolationBuild");
    m_apply = new QPushButton(tr("Apply interpolation…"));
    m_apply->setObjectName("analysis.interpolationApply");
    m_export = new QPushButton(tr("Export interpolation JSON…"));
    m_export->setObjectName("analysis.interpolationExport");
    layout->addWidget(build);
    layout->addWidget(m_apply);
    layout->addWidget(m_export);
    layout->addStretch();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &TimingInterpolationPanel::buildPreview);
    for (auto *spin : {m_startBpm, m_endBpm, m_duration, m_error})
        connect(spin, &QDoubleSpinBox::valueChanged, this, &TimingInterpolationPanel::schedule);
    for (auto *spin : m_start)
        connect(spin, &QSpinBox::valueChanged, this, &TimingInterpolationPanel::schedule);
    for (auto *spin : m_end)
        connect(spin, &QSpinBox::valueChanged, this, &TimingInterpolationPanel::schedule);
    for (auto *edit : {m_length, m_gap})
        connect(edit, &QLineEdit::textChanged, this, &TimingInterpolationPanel::schedule);
    connect(m_mode, &QComboBox::currentIndexChanged, this, &TimingInterpolationPanel::schedule);
    connect(m_resume, &QCheckBox::toggled, this, &TimingInterpolationPanel::schedule);
    connect(m_preview, &QCheckBox::toggled, this, &TimingInterpolationPanel::displayPreview);
    connect(build, &QPushButton::clicked, this, &TimingInterpolationPanel::buildPreview);
    connect(m_apply, &QPushButton::clicked, this, &TimingInterpolationPanel::apply);
    connect(m_export, &QPushButton::clicked, this, &TimingInterpolationPanel::exportJson);
    connect(m_pick, &QCheckBox::toggled, this, [this](bool enabled) {
        m_canvas->setInterpolationStartPicking(enabled);
        if (enabled) { emit pickingStarted(); m_canvas->setFocus(); }
    });
    connect(canvas, &AnalysisCanvas::interpolationStartRequested, this, &TimingInterpolationPanel::setStart);
    connect(canvas, &AnalysisCanvas::interpolationPickCancelled, this, &TimingInterpolationPanel::stopPicking);
    connect(chart, &ChartController::bpmListChanged, this, &TimingInterpolationPanel::invalidate);
    connect(chart, &ChartController::metaDataChanged, this, &TimingInterpolationPanel::invalidate);
    connect(chart, &ChartController::notesChanged, this, &TimingInterpolationPanel::invalidate);
    connect(chart, &ChartController::chartLoaded, this, [this] {
        invalidate();
        if (!m_chart->chart()->bpmList().isEmpty())
        {
            const auto &point = m_chart->chart()->bpmList().first();
            setStart(point.beatNum, point.numerator, point.denominator);
        }
    });
    const auto &list = chart->chart()->bpmList();
    if (!list.isEmpty())
        setStart(list.first().beatNum, list.first().numerator, list.first().denominator);
    schedule();
}
analysis::BeatTriplet TimingInterpolationPanel::readBeat(const std::array<QSpinBox *, 3> &fields) const
{
    return {fields[0]->value(), fields[1]->value(), fields[2]->value()};
}
void TimingInterpolationPanel::setStart(int b, int n, int d)
{
    const int values[]{b, n, d};
    for (int i = 0; i < 3; ++i) { const QSignalBlocker blocker(m_start[i]); m_start[i]->setValue(values[i]); }
    stopPicking();
    schedule();
}
void TimingInterpolationPanel::stopPicking() { m_pick->setChecked(false); }
void TimingInterpolationPanel::invalidate()
{
    stopPicking();
    m_timer->stop();
    m_result = {};
    m_proposal.clear();
    m_storedBpms.clear();
    m_table->setRowCount(0);
    m_failure = tr("Chart changed. Rebuild preview before applying.");
    m_summary->setText(m_failure);
    m_apply->setEnabled(false);
    m_export->setEnabled(false);
    m_canvas->clearInterpolationPreview();
}
void TimingInterpolationPanel::schedule()
{
    m_apply->setEnabled(false);
    m_export->setEnabled(false);
    m_canvas->clearInterpolationPreview();
    m_summary->setText(tr("Updating preview…"));
    m_timer->start();
}
void TimingInterpolationPanel::buildPreview()
{
    m_timer->stop();
    m_failure.clear(); m_proposal.clear(); m_storedBpms.clear(); m_result = {}; m_cacheError = 0; m_durationResidual = 0; m_replaced = 0;
    m_revision = m_chart->revision();
    m_options.start = readBeat(m_start);
    m_options.startBpm = m_startBpm->value(); m_options.endBpm = m_endBpm->value();
    m_options.maximumErrorMilliseconds = m_error->value();
    const auto gap = analysis::parseBeatSpan(m_gap->text().toStdString());
    const int mode = m_mode->currentIndex();
    for (auto *spin : m_end) spin->setEnabled(mode == 1);
    m_length->setEnabled(mode == 0); m_duration->setEnabled(mode == 2);
    const auto span = mode == 0 ? analysis::parseBeatSpan(m_length->text().toStdString())
                      : mode == 1 ? analysis::subtractBeats(readBeat(m_end), m_options.start)
                                  : analysis::spanForDuration(m_options.startBpm, m_options.endBpm, m_duration->value());
    const auto &original = m_chart->chart()->bpmList();
    bool validOriginal = !original.isEmpty();
    for (int i = 0; i < original.size(); ++i)
        if (!original[i].position().isValid() || original[i].position() < BeatPosition(0, 0, 1)
            || !std::isfinite(original[i].bpm) || original[i].bpm <= 0 || original[i].bpm > 10000
            || (i && original[i - 1].position() >= original[i].position()))
            validOriginal = false;
    if (!gap || !span || original.isEmpty() || !m_options.start.position().isValid()
        || m_options.start.position() < original.first().position())
        m_failure = tr("Enter valid beats, a positive length/gap and Start at or after the first timing.");
    else if (!validOriginal)
        m_failure = tr("Existing timing contains duplicate/invalid points. Resolve them before applying.");
    else
    {
        m_options.length = *span; m_options.maximumGap = *gap;
        m_result = analysis::interpolateTiming(m_options);
        if (!m_result.valid()) m_failure = QString::fromStdString(m_result.error);
    }
    if (m_failure.isEmpty())
    {
        auto endpoint = m_result.nodes.back().beat;
        if (mode == 1) endpoint = readBeat(m_end);
        m_result.nodes.back().beat = endpoint;
        double resumeBpm = original.first().bpm;
        for (const auto &point : original)
        {
            if (point.position() <= endpoint.position()) resumeBpm = point.bpm;
            if (point.position() < m_options.start.position() || point.position() > endpoint.position())
                m_proposal.append(point);
            else ++m_replaced;
        }
        for (std::size_t i = 0; i < m_result.nodes.size(); ++i)
        {
            auto &node = m_result.nodes[i];
            const auto existing = std::lower_bound(original.cbegin(), original.cend(), node.beat.position(),
                [](const auto &point, const auto &position) { return point.position() < position; });
            if (existing != original.cend() && existing->position() == node.beat.position())
                node.beat = {existing->beatNum, existing->numerator, existing->denominator};
            double bpm = i + 1 == m_result.nodes.size() && m_resume->isChecked() ? resumeBpm : node.bpm;
            m_storedBpms.append(bpm);
            m_proposal.append(BpmEntry(node.beat.whole, node.beat.numerator, node.beat.denominator, bpm));
        }
        std::stable_sort(m_proposal.begin(), m_proposal.end(), [](const auto &a, const auto &b) {
            return a.position() < b.position();
        });
        if (m_proposal.size() > 100000) m_failure = tr("The complete timing list exceeds 100000 points.");
        m_startMs = m_canvas->timeAtBeat(m_options.start.position().toDouble());
        const auto cache = MathUtils::buildBpmTimeCache(m_proposal, m_chart->chart()->meta().offset);
        for (const auto &node : m_result.nodes)
        {
            const double actual = MathUtils::beatToMs(node.beat.whole, node.beat.numerator, node.beat.denominator, cache);
            const double ideal = m_startMs + node.idealMilliseconds;
            if (!std::isfinite(actual) || !std::isfinite(ideal))
            {
                m_cacheError = std::numeric_limits<double>::infinity();
                break;
            }
            m_cacheError = qMax(m_cacheError, qAbs(actual - ideal));
        }
        if (mode == 2) m_durationResidual = m_result.durationMilliseconds - m_duration->value();
        for (int i = 0; i < m_proposal.size(); ++i)
            if (!m_proposal[i].position().isValid() || !std::isfinite(m_proposal[i].bpm) || m_proposal[i].bpm <= 0
                || m_proposal[i].bpm > 10000 || (i && m_proposal[i - 1].position() >= m_proposal[i].position()))
                m_failure = tr("Existing timing contains duplicate/invalid points. Resolve them before applying.");
        if (!std::isfinite(m_cacheError) || m_cacheError + m_result.maximumErrorMilliseconds
                                               + std::abs(m_durationResidual) >= m_error->value())
            m_failure = tr("Stored chart timing or duration rounding exceeds the requested error bound.");
    }
    m_table->setRowCount(0);
    if (m_failure.isEmpty())
    {
        const int rows = qMin(200, int(m_result.nodes.size()));
        m_table->setRowCount(rows);
        for (int i = 0; i < rows; ++i)
        {
            const auto &node = m_result.nodes[std::size_t(i)];
            const double stored = m_storedBpms.value(i, node.bpm);
            const QString cells[]{beatText(node.beat), QString::number(m_startMs + node.idealMilliseconds, 'f', 3),
                                  QString::number(stored, 'f', 6), QString::number(node.errorMilliseconds, 'f', 6)};
            for (int c = 0; c < 4; ++c) m_table->setItem(i, c, new QTableWidgetItem(cells[c]));
        }
        m_summary->setText(tr("%1 → %2 · %3 segments\nCurve %4 → %5 BPM · %6 ms\n"
                             "Maximum interior error %7 ms; node drift %8 ms; duration residual %9 ms.\n"
                             "Replace %10 timing points in range. %11 Notes keep their beat coordinates.\n"
                             "At End: %12. Later timing beats/BPM stay unchanged; audio times may move.%13")
            .arg(beatText(m_result.nodes.front().beat), beatText(m_result.nodes.back().beat))
            .arg(m_result.nodes.size() - 1).arg(m_options.startBpm, 0, 'f', 3).arg(m_options.endBpm, 0, 'f', 3)
            .arg(m_result.durationMilliseconds, 0, 'f', 3).arg(m_result.maximumErrorMilliseconds, 0, 'f', 6)
            .arg(m_cacheError, 0, 'f', 6).arg(m_durationResidual, 0, 'f', 6).arg(m_replaced)
            .arg(m_chart->chart()->notes().size()).arg(m_resume->isChecked() ? tr("resume chart BPM") : tr("continue End BPM"))
            .arg(m_result.nodes.size() > 200 ? tr("\nTable shows the first 200 points; JSON includes every point.") : QString()));
    }
    else m_summary->setText(m_failure);
    m_apply->setEnabled(m_failure.isEmpty()); m_export->setEnabled(m_failure.isEmpty());
    displayPreview();
}
void TimingInterpolationPanel::displayPreview()
{
    if (!isVisible() || !m_preview->isChecked() || !m_failure.isEmpty() || !m_result.valid()
        || m_timer->isActive() || m_revision != m_chart->revision())
        m_canvas->clearInterpolationPreview();
    else m_canvas->setInterpolationPreview(m_result, m_startMs);
}
void TimingInterpolationPanel::apply()
{
    if (m_revision != m_chart->revision() || m_timer->isActive()) { invalidate(); return; }
    if (!m_apply->isEnabled() || !m_failure.isEmpty()) return;
    const auto revision = m_revision;
    const auto proposal = m_proposal;
    const auto answer = QMessageBox::question(this, tr("Apply BPM interpolation"),
        tr("Replace %1 timing points with %2 points in %3 → %4?\n%5 Notes keep their beat coordinates, "
           "but audio times may change. Apply as one undoable edit?")
            .arg(m_replaced).arg(m_result.nodes.size()).arg(beatText(m_result.nodes.front().beat),
                                                         beatText(m_result.nodes.back().beat))
            .arg(m_chart->chart()->notes().size()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes && revision == m_chart->revision())
        m_chart->replaceBpmList(tr("Interpolate BPM"), proposal);
    else if (revision != m_chart->revision()) invalidate();
}
QJsonObject TimingInterpolationPanel::diagnostics() const
{
    QJsonArray nodes;
    for (std::size_t i = 0; i < m_result.nodes.size(); ++i)
    {
        const auto &node = m_result.nodes[i];
        const double stored = m_storedBpms.value(int(i), node.bpm);
        nodes.append(QJsonObject{{"beat", beatJson(node.beat)}, {"relativeBeat", node.relativeBeat},
                                 {"idealAudioMilliseconds", m_startMs + node.idealMilliseconds},
                                 {"effectiveBpm", node.bpm}, {"storedBpm", stored}, {"errorMilliseconds", node.errorMilliseconds}});
    }
    return {{"kind", "manual beat-linear BPM interpolation"}, {"ready", m_apply->isEnabled()},
             {"sourceRevision", double(m_revision)}, {"rangeMode", m_mode->currentIndex()},
             {"startBeat", beatJson(m_options.start)}, {"startBpm", m_options.startBpm}, {"endBpm", m_options.endBpm},
             {"spanNumerator", double(m_options.length.numerator)}, {"spanDenominator", double(m_options.length.denominator)},
             {"maximumGapBeats", m_options.maximumGap.beats()}, {"errorLimitMilliseconds", m_options.maximumErrorMilliseconds},
             {"durationMilliseconds", m_result.durationMilliseconds}, {"durationResidualMilliseconds", m_durationResidual},
             {"maximumInteriorErrorMilliseconds", m_result.maximumErrorMilliseconds}, {"maximumNodeDriftMilliseconds", m_cacheError},
             {"resumeChartBpmAtEnd", m_resume->isChecked()}, {"replacedTimingPoints", m_replaced},
             {"failure", m_failure}, {"nodes", nodes}};
}
void TimingInterpolationPanel::exportJson()
{
    if (!m_export->isEnabled() || m_revision != m_chart->revision() || m_timer->isActive()) return;
    const auto snapshot = diagnostics();
    const auto path = QFileDialog::getSaveFileName(this, tr("Export interpolation JSON"), "bpm-interpolation.json", tr("JSON (*.json)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const auto bytes = QJsonDocument(snapshot).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        m_summary->setText(tr("Export failed: %1").arg(file.errorString()));
}
void TimingInterpolationPanel::showEvent(QShowEvent *event) { QWidget::showEvent(event); schedule(); }
void TimingInterpolationPanel::hideEvent(QHideEvent *event)
{
    stopPicking(); m_canvas->clearInterpolationPreview(); QWidget::hideEvent(event);
}
