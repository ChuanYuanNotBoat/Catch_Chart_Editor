#include "TimingInterpolationPanel.h"
#include "TimingProposal.h"
#include "TimingEditorCanvas.h"
#include "TimingPreviewJobs.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QPointer>
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
TimingInterpolationPanel::TimingInterpolationPanel(ChartController *chart, TimingEditorCanvas *canvas, QWidget *parent)
    : QWidget(parent), m_chart(chart), m_canvas(canvas)
{
    setObjectName("analysis.interpolationPanel");
    auto *layout = new QVBoxLayout(this);
    auto *help = new QLabel(tr("BPM varies linearly with beat. Stored segment BPMs match the exact curve "
                              "duration; difficult regions subdivide locally to stay below the error limit."));
    help->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    help->setWordWrap(true);
    layout->addWidget(help);
    m_pick = new QCheckBox(tr("Pick Start in timing view"));
    m_pick->setObjectName("analysis.interpolationPick");
    layout->addWidget(m_pick);
    auto *form = new QFormLayout;
    auto beatFields = [&](std::array<QSpinBox *, 3> &fields, const QString &prefix) {
        auto *widget = new QWidget;
        widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0);
        const QString names[]{"Whole", "Numerator", "Denominator"};
        for (int i = 0; i < 3; ++i)
        {
            fields[i] = new QSpinBox;
            fields[i]->setRange(i == 2 ? 1 : std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            fields[i]->setValue(i == 2 ? 1 : 0);
            fields[i]->setKeyboardTracking(false);
            fields[i]->setMinimumWidth(0);
            fields[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
    m_watcher = new QFutureWatcher<analysis::InterpolationPreviewResult>(this);
    connect(m_watcher, &QFutureWatcher<analysis::InterpolationPreviewResult>::finished, this, [this] {
        m_busy = false;
        if (m_activeGeneration == m_generation && m_revision == m_chart->revision()
            && isVisible() && !m_cancel->load())
        {
            auto result = m_watcher->result();
            m_options = result.options; m_result = std::move(result.result);
            m_proposal = std::move(result.proposal); m_storedBpms = std::move(result.storedBpms);
            m_startMs = result.startMs; m_cacheError = result.cacheError;
            m_durationResidual = result.durationResidual; m_replaced = result.replaced;
            m_failure = result.failure;
            finishPreview();
        }
        if (m_pendingBuild && !m_timer->isActive() && isVisible()) buildPreview();
    });
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
    connect(canvas, &TimingEditorCanvas::interpolationStartRequested, this, &TimingInterpolationPanel::setStart);
    connect(canvas, &TimingEditorCanvas::interpolationPickCancelled, this, &TimingInterpolationPanel::stopPicking);
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
TimingInterpolationPanel::~TimingInterpolationPanel()
{
    if (m_cancel) m_cancel->store(true);
}
void TimingInterpolationPanel::cancelPreview()
{
    invalidate();
    m_summary->setText(tr("Preview cancelled. Rebuild preview to continue."));
}
void TimingInterpolationPanel::invalidate()
{
    if (auto *confirmation = findChild<QMessageBox *>("bpm.applyConfirmation")) confirmation->reject();
    ++m_generation;
    if (m_cancel) m_cancel->store(true);
    m_pendingBuild = false;
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
    if (auto *confirmation = findChild<QMessageBox *>("bpm.applyConfirmation")) confirmation->reject();
    ++m_generation;
    if (m_cancel) m_cancel->store(true);
    m_apply->setEnabled(false);
    m_export->setEnabled(false);
    m_canvas->clearInterpolationPreview();
    m_summary->setText(tr("Updating preview…"));
    if (isVisible()) m_timer->start();
}
void TimingInterpolationPanel::buildPreview()
{
    m_timer->stop();
    ++m_generation;
    if (m_cancel) m_cancel->store(true);
    m_apply->setEnabled(false); m_export->setEnabled(false);
    m_canvas->clearInterpolationPreview();
    if (!isVisible()) return;
    if (m_busy) { m_pendingBuild = true; return; }
    m_pendingBuild = false;
    m_revision = m_chart->revision();
    analysis::InterpolationPreviewRequest request;
    request.options.start = readBeat(m_start);
    request.options.startBpm = m_startBpm->value(); request.options.endBpm = m_endBpm->value();
    request.options.maximumErrorMilliseconds = m_error->value();
    request.gap = analysis::parseBeatSpan(m_gap->text().toStdString());
    request.mode = m_mode->currentIndex();
    request.end = readBeat(m_end);
    request.duration = m_duration->value(); request.resume = m_resume->isChecked();
    for (auto *spin : m_end) spin->setEnabled(request.mode == 1);
    m_length->setEnabled(request.mode == 0); m_duration->setEnabled(request.mode == 2);
    request.span = request.mode == 0 ? analysis::parseBeatSpan(m_length->text().toStdString())
                   : request.mode == 1 ? analysis::subtractBeats(request.end, request.options.start)
                                       : analysis::spanForDuration(request.options.startBpm, request.options.endBpm, request.duration);
    request.original = m_chart->chart()->bpmList();
    request.offset = m_chart->chart()->meta().offset;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_activeGeneration = m_generation;
    m_busy = true;
    m_summary->setText(tr("Updating preview…"));
    m_watcher->setFuture(QtConcurrent::run(timingPreviewPool(), [request = std::move(request), cancel = m_cancel] {
        return analysis::buildInterpolationPreview(request, cancel);
    }));
}
void TimingInterpolationPanel::finishPreview()
{
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
        || m_timer->isActive() || m_busy || m_revision != m_chart->revision())
        m_canvas->clearInterpolationPreview();
    else
    {
        m_canvas->setInterpolationCurve(m_options, m_storedBpms);
        m_canvas->setInterpolationPreview(m_result, m_startMs);
    }
}
void TimingInterpolationPanel::apply()
{
    if (m_revision != m_chart->revision() || m_timer->isActive() || m_busy) { invalidate(); return; }
    if (!m_apply->isEnabled() || !m_failure.isEmpty()) return;
    const auto revision = m_revision;
    const auto proposal = m_proposal;
    const auto generation = m_generation;
    QPointer<TimingInterpolationPanel> guard(this);
    analysis::requestTimingProposal(
        this, m_chart, tr("Apply BPM interpolation"),
        tr("Replace %1 timing points with %2 points in %3 → %4?")
            .arg(m_replaced)
            .arg(m_result.nodes.size())
            .arg(beatText(m_result.nodes.front().beat), beatText(m_result.nodes.back().beat)),
        proposal, revision, [guard, generation] {
            return guard && guard->isVisible() && guard->m_generation == generation;
        });
    if (revision != m_chart->revision())
        invalidate();
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
    if (!m_export->isEnabled() || m_revision != m_chart->revision() || m_timer->isActive() || m_busy) return;
    const auto snapshot = diagnostics();
    if (window()->property("cceNonModalTiming").toBool())
    {
        auto *dialog = new QFileDialog(this, tr("Export interpolation JSON"), "bpm-interpolation.json", tr("JSON (*.json)"));
        dialog->setObjectName("bpm.exportDialog");
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setDefaultSuffix("json");
        dialog->setOption(QFileDialog::DontUseNativeDialog);
        dialog->setWindowModality(Qt::NonModal);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &QFileDialog::fileSelected, this, [this, snapshot](const QString &path) {
            auto *watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
                const auto error = watcher->result();
                watcher->deleteLater();
                if (!error.isEmpty()) m_summary->setText(tr("Export failed: %1").arg(error));
            });
            watcher->setFuture(QtConcurrent::run(timingPreviewPool(), [snapshot, path] {
                const auto bytes = QJsonDocument(snapshot).toJson();
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                    return file.errorString();
                return QString();
            }));
        });
        dialog->show();
        return;
    }
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
    invalidate(); QWidget::hideEvent(event);
}
