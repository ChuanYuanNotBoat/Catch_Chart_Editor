#include "AnalysisEditor.h"
#include "AnalysisCanvas.h"
#include "analysis/AutoTimingDiagnostics.h"
#include "audio/SpectrumService.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "controller/CommandRouter.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/LongRangeSelector.h"
#include "utils/Settings.h"
#include <QAction>
#include <QBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTimer>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileDialog>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QHideEvent>
#include <QCloseEvent>
#include <cmath>
namespace
{
QSettings layoutSettings()
{
    return QSettings("CatchEditor", "CatchChartEditor");
}
QWidget *placeholder(const QString &text)
{
    auto *p = new QWidget;
    auto *l = new QVBoxLayout(p);
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    l->addWidget(label);
    l->addStretch();
    return p;
}
QString cellText(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("—");
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 8);
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return v.toString();
}
} // namespace
AnalysisEditor::AnalysisEditor(ChartController *chart, SelectionController *selection,
                               PlaybackController *playback, ChartCanvas *main, LongRangeSelector *range,
                               QWidget *parent)
    : QWidget(parent, Qt::Window), m_chart(chart), m_selection(selection), m_playback(playback), m_main(main),
      m_range(range)
{
    setObjectName("analysis.editor");
    setProperty("cceOwnCommandRouter", true);
    setWindowTitle(tr("CCE — Analysis Editor"));
    resize(1180, 800);
    setMinimumSize(640, 400);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 4);
    root->setSpacing(4);
    auto *header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Analysis Editor")));
    header->addStretch();
    m_syncView = new QCheckBox(tr("Sync visible range"));
    m_syncView->setObjectName("analysis.syncView");
    header->addWidget(m_syncView);
    header->addWidget(new QLabel(tr("ms / px")));
    m_zoom = new QDoubleSpinBox;
    m_zoom->setObjectName("analysis.zoom");
    m_zoom->setRange(0.1, 100);
    m_zoom->setDecimals(2);
    m_zoom->setValue(6);
    m_zoom->setToolTip(tr("Independent vertical zoom. Ctrl + wheel also zooms."));
    header->addWidget(m_zoom);
    root->addLayout(header);
    auto *body = new QHBoxLayout;
    body->setSpacing(2);
    m_leftTabs = new QTabBar;
    m_leftTabs->setObjectName("analysis.leftTabs");
    m_leftTabs->setShape(QTabBar::RoundedWest);
    m_rightTabs = new QTabBar;
    m_rightTabs->setObjectName("analysis.rightTabs");
    m_rightTabs->setShape(QTabBar::RoundedEast);
    for (auto *tabs : {m_leftTabs, m_rightTabs})
    {
        tabs->setExpanding(false);
        tabs->setUsesScrollButtons(true);
        tabs->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    m_panels = new QSplitter;
    m_panels->setObjectName("analysis.panels");
    m_panels->setChildrenCollapsible(false);
    m_leftPanels = new QStackedWidget;
    m_rightPanels = new QStackedWidget;
    m_leftPanels->setMinimumWidth(150);
    m_rightPanels->setMinimumWidth(150);
    m_work = new QSplitter;
    m_work->setObjectName("analysis.work");
    m_work->setChildrenCollapsible(false);
    m_canvas = new AnalysisCanvas(chart, selection, main);
    m_work->addWidget(m_canvas);
    auto *spare = new QWidget;
    spare->setMinimumWidth(0);
    spare->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_work->addWidget(spare);
    m_work->setCollapsible(1, true);
    m_work->setStretchFactor(0, 0);
    m_work->setStretchFactor(1, 1);
    m_panels->addWidget(m_leftPanels);
    m_panels->addWidget(m_work);
    m_panels->addWidget(m_rightPanels);
    m_panels->setStretchFactor(1, 1);
    body->addWidget(m_leftTabs);
    body->addWidget(m_panels, 1);
    body->addWidget(m_rightTabs);
    m_panels->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_work->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addLayout(body, 1);
    addAnalysisPanel(tr("Timing"), createTimingPanel());
    addAnalysisPanel(tr("Transient"),
                     placeholder(tr("Future: onset strength, band transients, detected / rejected events.")));
    addAnalysisPanel(tr("Timbre"),
                     placeholder(tr("Future: spectral features, event groups and stereo characteristics.")));
    addAnalysisPanel(tr("Tracks"), placeholder(tr("Future: pseudo tracks, continuity and preferred track.")),
                     true);
    addAnalysisPanel(tr("Generation"),
                     placeholder(tr("Future: density, importance and charting rules. Candidates will use "
                                    "ghost preview before a separate undoable commit.")),
                     true);
    addAnalysisPanel(tr("Diagnostics"), createDiagnosticsPanel(), true);
    m_leftPanels->hide();
    m_rightPanels->hide();
    connect(m_leftTabs, &QTabBar::tabBarClicked, this, [this](int i) { togglePanel(false, i); });
    connect(m_rightTabs, &QTabBar::tabBarClicked, this, [this](int i) { togglePanel(true, i); });
    auto *bottom = new QWidget;
    bottom->setObjectName("analysis.bottom");
    bottom->setMaximumHeight(38);
    bottom->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *controls = new QHBoxLayout(bottom);
    controls->setContentsMargins(0, 0, 0, 0);
    m_play = new QPushButton(tr("Play"));
    m_play->setObjectName("analysis.play");
    controls->addWidget(m_play);
    m_time = new QLabel;
    m_time->setMinimumWidth(105);
    controls->addWidget(m_time);
    m_loop = new QCheckBox(tr("Loop range"));
    m_loop->setObjectName("analysis.loop");
    controls->addWidget(m_loop);
    auto *undo = new QPushButton(tr("Undo")), *redo = new QPushButton(tr("Redo"));
    controls->addWidget(undo);
    controls->addWidget(redo);
    m_status = new QLabel(tr("Note: left add / right delete · Rain: two clicks · Shift drag: time range"));
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    controls->addWidget(m_status, 1);
    root->addWidget(bottom);
    m_router = new CommandRouter(this);
    auto registerCommand = [this](const QString &id, const QString &text, const QKeySequence &key,
                                  auto callback) {
        auto *action = new QAction(text, this);
        connect(action, &QAction::triggered, this, callback);
        m_router->registerAction(action, id, key);
        return action;
    };
    auto *undoAction =
        registerCommand("edit.undo", tr("Undo"), QKeySequence::Undo, [chart] { chart->undo(); });
    auto *redoAction =
        registerCommand("edit.redo", tr("Redo"), QKeySequence::Redo, [chart] { chart->redo(); });
    auto togglePlay = [this] {
        if (m_playback->state() == PlaybackController::Playing)
            m_playback->pause();
        else
            m_playback->playFromTime(m_main->currentPlayTime());
    };
    auto *playAction =
        registerCommand("playback.play_pause", tr("Play / Pause"), QKeySequence(Qt::Key_Space), togglePlay);
    connect(undo, &QPushButton::clicked, undoAction, &QAction::trigger);
    connect(redo, &QPushButton::clicked, redoAction, &QAction::trigger);
    connect(m_play, &QPushButton::clicked, playAction, &QAction::trigger);
    auto updateUndo = [=] {
        undoAction->setEnabled(chart->canUndo());
        redoAction->setEnabled(chart->canRedo());
        undo->setEnabled(chart->canUndo());
        redo->setEnabled(chart->canRedo());
    };
    connect(chart, &ChartController::undoStateChanged, this, updateUndo);
    updateUndo();
    connect(m_syncView, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setViewSynchronized(on);
        m_zoom->setEnabled(!on);
    });
    connect(m_zoom, &QDoubleSpinBox::valueChanged, m_canvas, &AnalysisCanvas::setMillisecondsPerPixel);
    connect(m_canvas, &AnalysisCanvas::seekRequested, this, &AnalysisEditor::seek);
    connect(m_canvas, &AnalysisCanvas::rangeRequested, this, [this](double a, double b) {
        m_range->setStartBeat(a);
        m_range->setEndBeat(b);
        m_range->setRangeVisible(true);
        updateRange();
    });
    connect(m_canvas, &AnalysisCanvas::statusMessage, m_status, &QLabel::setText);
    connect(m_main, &ChartCanvas::currentTimeChanged, this, &AnalysisEditor::updateTime);
    connect(m_main, &ChartCanvas::scrollPositionChanged, this,
            [this] { updateTime(m_main->currentPlayTime()); });
    connect(playback, &PlaybackController::positionChanged, this, [this](double ms) {
        if (m_playback->state() != PlaybackController::Playing)
        {
            m_main->setScrollPos(ms);
            updateTime(ms);
        }
    });
    connect(playback, &PlaybackController::playbackFrameTick, this, [this](double ms, qint64) {
        if (isVisible())
            updateTime(ms);
    });
    connect(playback, &PlaybackController::stateChanged, this, [this](PlaybackController::State s) {
        m_play->setText(s == PlaybackController::Playing ? tr("Pause") : tr("Play"));
    });
    connect(m_range, &LongRangeSelector::rangeChanged, this, &AnalysisEditor::updateRange);
    connect(m_range, &LongRangeSelector::rangeVisibilityChanged, this, &AnalysisEditor::updateRange);
    connect(m_loop, &QCheckBox::toggled, this, &AnalysisEditor::updateLoopFromRange);
    connect(playback, &PlaybackController::loopRangeChanged, this, [this](double a, double b, bool enabled) {
        const QSignalBlocker blocker(m_loop);
        m_loop->setChecked(enabled);
        m_canvas->setLoop(a, b, enabled);
    });
    m_spectrumTimer = new QTimer(this);
    m_spectrumTimer->setSingleShot(true);
    m_spectrumTimer->setInterval(180);
    connect(m_spectrumTimer, &QTimer::timeout, this, &AnalysisEditor::requestSpectrum);
    connect(m_canvas, &AnalysisCanvas::viewportChanged, this, [this] {
        const QSignalBlocker blocker(m_zoom);
        m_zoom->setValue(m_canvas->millisecondsPerPixel());
        scheduleSpectrum();
    });
    connect(chart, &ChartController::chartLoaded, this, [this] {
        ++m_sourceGeneration;
        ++m_spectrumGeneration;
        if (m_spectrumCancel)
            *m_spectrumCancel = true;
        m_canvas->clearSpectrum();
        clearDiagnostics();
        cancelTiming();
        m_playback->setLoopRange(0, 0, false);
        m_sourceIdentity.clear();
        refreshAudioSource();
        updateRange();
    });
    connect(chart, &ChartController::metaDataChanged, this, &AnalysisEditor::refreshAudioSource);
    refreshAudioSource();
    updateRange();
    updateTime(main->currentPlayTime());
    m_canvas->setLoop(playback->loopStartMs(), playback->loopEndMs(), playback->loopEnabled());
    m_loop->setChecked(playback->loopEnabled());
}
AnalysisEditor::~AnalysisEditor()
{
    saveLayout();
    cancelTiming();
    if (m_spectrumCancel)
        *m_spectrumCancel = true;
}
void AnalysisEditor::addAnalysisPanel(const QString &title, QWidget *panel, bool right)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    auto *tabs = right ? m_rightTabs : m_leftTabs;
    auto *stack = right ? m_rightPanels : m_leftPanels;
    tabs->addTab(title);
    stack->addWidget(scroll);
}
void AnalysisEditor::togglePanel(bool right, int index)
{
    auto *stack = right ? m_rightPanels : m_leftPanels;
    int &open = right ? m_rightOpen : m_leftOpen;
    if (index < 0 || index >= stack->count())
        return;
    if (open == index && stack->isVisible())
    {
        stack->hide();
        open = -1;
    }
    else
    {
        open = index;
        stack->setCurrentIndex(index);
        stack->show();
        auto sizes = m_panels->sizes();
        const int i = right ? 2 : 0;
        if (sizes[i] < 160)
        {
            sizes[i] = 340;
            sizes[1] = qMax(180, sizes[1] - 340);
            m_panels->setSizes(sizes);
        }
    }
}
void AnalysisEditor::updateTime(double ms)
{
    m_canvas->setCurrentTime(ms);
    m_time->setText(QString::number(ms / 1000, 'f', 3) + tr(" s"));
}
void AnalysisEditor::seek(double ms)
{
    if (!std::isfinite(ms))
        return;
    if (m_playback->state() == PlaybackController::Playing)
        m_playback->pause();
    ms = qMax(0.0, ms);
    const auto duration = m_playback->audioPlayer()->duration();
    if (duration > 0)
        ms = qMin(ms, double(duration));
    m_playback->seekTo(ms);
    m_main->setScrollPos(ms);
    updateTime(ms);
}
void AnalysisEditor::updateRange()
{
    m_canvas->setRange(m_range->currentStartBeat(), m_range->currentEndBeat(), m_range->isRangeVisible());
    if (m_loop->isChecked())
        updateLoopFromRange();
}
void AnalysisEditor::updateLoopFromRange()
{
    m_playback->setLoopRange(m_canvas->timeAtBeat(m_range->currentStartBeat()),
                             m_canvas->timeAtBeat(m_range->currentEndBeat()), m_loop->isChecked());
}
void AnalysisEditor::refreshAudioSource()
{
    QString path = m_chart->chart()->audioSourceFullPath();
    if (path.isEmpty())
        path = m_chart->chart()->meta().audioFile;
    if (!path.isEmpty() && QFileInfo(path).isRelative())
        path = QFileInfo(m_chart->chartFilePath()).dir().absoluteFilePath(path);
    const QFileInfo info(path);
    const QString identity = path.isEmpty()
                                 ? QString()
                                 : info.absoluteFilePath() + '|' + QString::number(info.size()) + '|' +
                                       QString::number(info.lastModified().toMSecsSinceEpoch());
    if (identity == m_sourceIdentity)
        return;
    m_audioPath = path;
    m_sourceIdentity = identity;
    ++m_sourceGeneration;
    ++m_spectrumGeneration;
    if (m_spectrumCancel)
        *m_spectrumCancel = true;
    cancelTiming();
    clearDiagnostics();
    m_canvas->clearSpectrum();
    m_spectrumFailureKey.clear();
    m_cachedEofMs = -1;
    scheduleSpectrum();
}
void AnalysisEditor::scheduleSpectrum()
{
    if (isVisible() && !m_spectrumTimer->isActive())
        m_spectrumTimer->start();
}
void AnalysisEditor::requestSpectrum()
{
    if (!isVisible())
        return;
    // Detect replacement of the same audio pathname as well as metadata edits.
    const QString before = m_sourceIdentity;
    refreshAudioSource();
    if (before != m_sourceIdentity)
        return;
    if (m_audioPath.isEmpty() || !QFileInfo::exists(m_audioPath))
    {
        m_canvas->setStatus(tr("Chart audio is unavailable"));
        return;
    }
    const double a = m_canvas->timeAtY(28), b = m_canvas->timeAtY(m_canvas->height());
    const double low = qMax(0.0, qMin(a, b)), high = qMax(low, qMax(a, b));
    if (high - low > 100000)
    {
        m_canvas->setStatus(tr("Zoom in to inspect spectrum (visible range ≤ 100 s)"));
        return;
    }
    const auto &cached = m_canvas->spectrum();
    const double audioDuration = m_playback->audioPlayer()->duration();
    double requiredEnd = audioDuration > 0 ? qMin(high, audioDuration) : high;
    if (m_cachedEofMs >= 0)
        requiredEnd = qMin(requiredEnd, m_cachedEofMs);
    if (cached.valid() && cached.startSeconds * 1000 <= low &&
        (cached.startSeconds + cached.durationSeconds) * 1000 + 20 >= requiredEnd)
    {
        if (m_spectrumBusy && (low < m_pendingStart || high > m_pendingStart + m_pendingDuration))
            if (m_spectrumCancel)
                *m_spectrumCancel = true;
        m_canvas->setStatus({});
        return;
    }
    const double start = qMax(0.0, std::floor(low / 20000) * 20000);
    const double duration = qBound(30000.0, high - start + 1000, 120000.0);
    const QString key = m_sourceIdentity + '|' + QString::number(start) + '|' + QString::number(duration);
    if (key == m_spectrumFailureKey)
        return;
    if (m_spectrumBusy)
    {
        if (low < m_pendingStart || high > m_pendingStart + m_pendingDuration)
        {
            if (m_spectrumCancel)
                *m_spectrumCancel = true;
        }
        return;
    }
    m_spectrumBusy = true;
    m_pendingStart = start;
    m_pendingDuration = duration;
    m_spectrumCancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_spectrumCancel;
    const auto generation = ++m_spectrumGeneration, source = m_sourceGeneration;
    m_canvas->setStatus(tr("Decoding stereo spectrum…"));
    SpectrumService::analyzeFileRangeAsync(
        this, m_audioPath, start, duration, cancel,
        [this, generation, source, key, duration, cancel](analysis::StereoSpectrum result) {
            m_spectrumBusy = false;
            if (generation != m_spectrumGeneration || source != m_sourceGeneration || cancel->load())
            {
                scheduleSpectrum();
                return;
            }
            if (result.valid())
            {
                if (result.durationSeconds * 1000 + 20 < duration)
                    m_cachedEofMs = (result.startSeconds + result.durationSeconds) * 1000;
                m_canvas->setSpectrum(std::move(result));
                m_status->setText(tr("Stereo spectrum ready · gutters: peak / RMS"));
            }
            else
            {
                m_spectrumFailureKey = key;
                m_canvas->setStatus(QString::fromStdString(result.error));
            }
            scheduleSpectrum();
        });
}
QWidget *AnalysisEditor::createTimingPanel()
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    auto *form = new QFormLayout;
    m_timingStart = new QDoubleSpinBox;
    m_timingStart->setObjectName("analysis.timingStart");
    m_timingStart->setRange(0, 86400);
    m_timingStart->setDecimals(3);
    m_timingStart->setSuffix(tr(" s"));
    m_timingDuration = new QDoubleSpinBox;
    m_timingDuration->setObjectName("analysis.timingDuration");
    m_timingDuration->setRange(4, 300);
    m_timingDuration->setValue(60);
    m_timingDuration->setSuffix(tr(" s"));
    form->addRow(tr("Audio start"), m_timingStart);
    form->addRow(tr("Duration"), m_timingDuration);
    layout->addLayout(form);
    auto *ranges = new QHBoxLayout;
    auto *selection = new QPushButton(tr("Use range")), *visible = new QPushButton(tr("Use visible"));
    ranges->addWidget(selection);
    ranges->addWidget(visible);
    layout->addLayout(ranges);
    auto setRange = [this](double a, double b) {
        m_timingStart->setValue(qMax(0.0, qMin(a, b)) / 1000);
        m_timingDuration->setValue(qAbs(b - a) / 1000);
    };
    connect(selection, &QPushButton::clicked, this, [this, setRange] {
        setRange(m_canvas->timeAtBeat(m_range->currentStartBeat()),
                 m_canvas->timeAtBeat(m_range->currentEndBeat()));
    });
    connect(visible, &QPushButton::clicked, this,
            [this, setRange] { setRange(m_canvas->timeAtY(28), m_canvas->timeAtY(m_canvas->height())); });
    m_complexSubdivision = new QCheckBox(tr("Enable experimental subdivision analysis"));
    m_complexSubdivision->setToolTip(tr("Raw periodicity evidence is always available. Semantic rhythm "
                                        "profiles require Core's confidence gates."));
    layout->addWidget(m_complexSubdivision);
    auto *buttons = new QHBoxLayout;
    m_runTiming = new QPushButton(tr("Analyze timing"));
    m_runTiming->setObjectName("analysis.runTiming");
    m_cancelTiming = new QPushButton(tr("Cancel / discard"));
    m_cancelTiming->setEnabled(false);
    buttons->addWidget(m_runTiming);
    buttons->addWidget(m_cancelTiming);
    layout->addLayout(buttons);
    connect(m_runTiming, &QPushButton::clicked, this, &AnalysisEditor::runTiming);
    connect(m_cancelTiming, &QPushButton::clicked, this, &AnalysisEditor::cancelTiming);
    m_timingSummary = new QLabel(tr("Analyze audio to inspect AutoTiming intermediate results."));
    m_timingSummary->setObjectName("analysis.timingSummary");
    m_timingSummary->setMinimumHeight(m_timingSummary->fontMetrics().lineSpacing() * 6);
    m_timingSummary->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_timingSummary->setWordWrap(true);
    m_timingSummary->setTextFormat(Qt::PlainText);
    layout->addWidget(m_timingSummary);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("analysis.timingTables");
    auto table = [this, tabs](const QString &id, const QString &label, const QStringList &fields) {
        auto *t = new QTableWidget(0, fields.size());
        t->setObjectName("analysis.table." + id);
        t->setHorizontalHeaderLabels(fields);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        t->horizontalHeader()->setDefaultSectionSize(116);
        t->verticalHeader()->hide();
        t->setAlternatingRowColors(true);
        for (int i = 0; i < fields.size(); ++i)
            t->horizontalHeaderItem(i)->setData(Qt::UserRole, fields[i]);
        connect(t, &QTableWidget::currentCellChanged, this, [this, t, id](int row, int, int, int) {
            const auto *item = t->item(row, 0);
            if (!item)
                return;
            const auto object = item->data(Qt::UserRole).toJsonObject();
            if (id == QLatin1String("windows"))
                selectDiagnosticWindow(object);
            else if (id == QLatin1String("tempoCandidates") || id == QLatin1String("windowCandidates"))
                selectTimingCandidate(object);
        });
        connect(t, &QTableWidget::cellDoubleClicked, this, [this, t, id, tabs](int row, int) {
            if (!t->item(row, 0))
                return;
            const auto o = t->item(row, 0)->data(Qt::UserRole).toJsonObject();
            auto v = o.value("timeSeconds");
            if (!v.isDouble())
                v = o.value("startSeconds");
            if (!v.isDouble())
                v = o.value("pulseTimeSeconds");
            if (v.isDouble() && std::isfinite(v.toDouble()))
            {
                m_syncView->setChecked(false);
                m_zoom->setValue(1);
                seek(v.toDouble() * 1000);
            }
            if (id == QLatin1String("windows"))
                tabs->setCurrentWidget(m_tables.value("windowCandidates"));
        });
        m_tables.insert(id, t);
        tabs->addTab(t, label);
    };
    table("tempoCandidates", tr("Candidates"),
          {"bpm", "rawBpm", "score", "rawBpmUncertainty", "hasPulseTime", "pulseTimeSeconds",
           "legacyOffsetMilliseconds", "phaseConfidence", "origin", "harmonicFamilyId",
           "supportingWindowIds"});
    table("windows", tr("Windows"),
          {"id", "startSeconds", "endSeconds", "scale", "reliability", "tempoEvidence",
           "crossScaleConsistency", "selectedAsAnchor", "estimatorMessage"});
    table("windowCandidates", tr("Window candidates"),
          {"bpm", "rawBpm", "score", "rawBpmUncertainty", "pulseTimeSeconds", "phaseConfidence",
           "legacyOffsetMilliseconds", "origin", "harmonicRatio", "periodSeconds"});
    table("tempoTrack", tr("Track"),
          {"timeSeconds", "bpm", "pulseTimeSeconds", "confidence", "phaseConfidence", "state",
           "propagationReason", "sourceWindowId"});
    table("segments", tr("Segments"),
          {"startSeconds", "endSeconds", "startBpm", "endBpm", "confidence", "kind", "startBeat", "endBeat"});
    table("fit", tr("Grid fit"),
          {"timeSeconds", "observedBpm", "modelBpm", "residualMilliseconds", "confidence", "phaseConfidence",
           "phaseBeat", "pulseIndex"});
    table("periodicityLayers", tr("Periodicity evidence"),
          {"startSeconds", "endSeconds", "observedRateBpm", "referenceTempoBpm", "relativeRate",
           "ratioNumerator", "ratioDenominator", "confidence", "relation", "phaseConfidence",
           "highRatePulseCoverage"});
    table("rhythmLayers", tr("Subdivision evidence"),
          {"startSeconds", "endSeconds", "observedRateBpm", "relativeRate", "confidence", "role",
           "phaseConfidence", "pulseCoverage", "transientDensityCoverage"});
    table("uncertainRegions", tr("Uncertainty"), {"startSeconds", "endSeconds", "confidence", "reason"});
    tabs->setMinimumHeight(230);
    layout->addWidget(tabs, 1);
    m_candidateDetails = new QLabel(tr("Select a candidate or an analysis window to inspect its phase."));
    m_candidateDetails->setObjectName("analysis.candidateDetails");
    m_candidateDetails->setWordWrap(true);
    m_candidateDetails->setTextFormat(Qt::PlainText);
    m_candidateDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_candidateDetails);
    auto *preview = new QHBoxLayout;
    m_candidateGrid = new QCheckBox(tr("Preview candidate grid"));
    m_candidateGrid->setObjectName("analysis.candidateGrid");
    m_candidateGrid->setEnabled(false);
    m_candidateGrid->setToolTip(tr("Constant BPM reference at the reported absolute pulse. Cyan lines are "
                                   "limited to the analyzed range. Chart timing and placement snap stay unchanged."));
    m_candidateSeek = new QPushButton(tr("Go to pulse"));
    m_candidateSeek->setObjectName("analysis.candidateSeek");
    m_candidateSeek->setEnabled(false);
    preview->addWidget(m_candidateGrid);
    preview->addWidget(m_candidateSeek);
    layout->addLayout(preview);
    connect(m_candidateGrid, &QCheckBox::toggled, this, &AnalysisEditor::updateTimingPreview);
    connect(m_candidateSeek, &QPushButton::clicked, this, [this] {
        const auto pulse = m_selectedCandidate.value("pulseTimeSeconds");
        if (!pulse.isDouble() || !std::isfinite(pulse.toDouble()))
            return;
        m_syncView->setChecked(false);
        m_zoom->setValue(1);
        seek(pulse.toDouble() * 1000);
    });
    auto *hint = new QLabel(tr("Double-click a window to inspect its local candidates and audio. Global "
                               "candidates currently carry tempo only. Scores/costs are diagnostics, not probabilities."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    return panel;
}
QWidget *AnalysisEditor::createDiagnosticsPanel()
{
    auto *panel = new QWidget;
    auto *l = new QVBoxLayout(panel);
    auto *exportButton = new QPushButton(tr("Export diagnostic JSON…"));
    l->addWidget(exportButton);
    connect(exportButton, &QPushButton::clicked, this, &AnalysisEditor::exportDiagnostics);
    m_raw = new QPlainTextEdit;
    m_raw->setObjectName("analysis.rawDiagnostics");
    m_raw->setReadOnly(true);
    m_raw->setLineWrapMode(QPlainTextEdit::NoWrap);
    l->addWidget(m_raw, 1);
    return panel;
}
void AnalysisEditor::fillTable(const QString &id, const QJsonArray &rows)
{
    auto *t = m_tables.value(id);
    if (!t)
        return;
    const QSignalBlocker blocker(t);
    t->clearContents();
    t->setRowCount(rows.size());
    t->setCurrentCell(-1, -1);
    t->clearSelection();
    for (int r = 0; r < rows.size(); ++r)
    {
        const auto o = rows[r].toObject();
        for (int c = 0; c < t->columnCount(); ++c)
        {
            auto *item = new QTableWidgetItem(
                cellText(o.value(t->horizontalHeaderItem(c)->data(Qt::UserRole).toString())));
            if (c == 0)
                item->setData(Qt::UserRole, o);
            t->setItem(r, c, item);
        }
    }
}
void AnalysisEditor::selectDiagnosticWindow(const QJsonObject &window)
{
    QJsonArray rows;
    for (const auto &value : window.value("tempoCandidates").toArray())
    {
        auto candidate = value.toObject();
        candidate["windowId"] = window.value("id");
        candidate["previewStartSeconds"] = window.value("startSeconds");
        candidate["previewEndSeconds"] = window.value("endSeconds");
        rows.append(candidate);
    }
    fillTable("windowCandidates", rows);
    if (!rows.isEmpty())
        m_tables.value("windowCandidates")->setCurrentCell(0, 0);
    else
    {
        selectTimingCandidate({});
        m_candidateDetails->setText(tr("Window #%1 has no tempo candidates.").arg(cellText(window.value("id"))));
    }
}
void AnalysisEditor::selectTimingCandidate(const QJsonObject &candidate)
{
    m_selectedCandidate = candidate;
    const auto pulse = candidate.value("pulseTimeSeconds");
    const bool hasPulse = candidate.value("hasPulseTime").toBool() && pulse.isDouble() &&
                          std::isfinite(pulse.toDouble());
    m_candidateSeek->setEnabled(hasPulse);
    const double bpm = candidate.value("bpm").toDouble();
    const double start = candidate.value("previewStartSeconds").toDouble(m_diagnostics.value("analysisStartMs").toDouble() / 1000);
    const double end = candidate.value("previewEndSeconds").toDouble(start + m_diagnostics.value("durationSeconds").toDouble());
    const bool canPreview = hasPulse && std::isfinite(bpm) && bpm > 0 && bpm <= 10000 &&
                            std::isfinite(start) && std::isfinite(end) && start >= 0 && end > start;
    m_candidateGrid->setEnabled(canPreview);
    if (!canPreview)
        m_candidateGrid->setChecked(false);
    if (candidate.isEmpty())
        m_candidateDetails->setText(tr("Select a candidate or an analysis window to inspect its phase."));
    else
    {
        const QString scope = candidate.contains("windowId")
                                  ? tr("Window #%1 · %2–%3 s").arg(cellText(candidate.value("windowId")))
                                        .arg(start, 0, 'f', 3).arg(end, 0, 'f', 3)
                                  : tr("Global candidate");
        m_candidateDetails->setText(
            tr("%1\n%2 BPM · pulse %3\nPhase confidence %4 · legacy offset %5 ms")
                .arg(scope).arg(bpm, 0, 'f', 5)
                .arg(hasPulse ? tr("%1 s").arg(pulse.toDouble(), 0, 'f', 6) : tr("unavailable (tempo only)"))
                .arg(cellText(candidate.value("phaseConfidence")))
                .arg(cellText(candidate.value("legacyOffsetMilliseconds"))));
    }
    updateTimingPreview();
}
void AnalysisEditor::updateTimingPreview()
{
    if (!m_candidateGrid->isChecked() || !m_candidateGrid->isEnabled())
    {
        m_canvas->clearTimingPreview();
        return;
    }
    const double start = m_selectedCandidate.value("previewStartSeconds").toDouble(
        m_diagnostics.value("analysisStartMs").toDouble() / 1000);
    const double end = m_selectedCandidate.value("previewEndSeconds").toDouble(
        start + m_diagnostics.value("durationSeconds").toDouble());
    m_canvas->setTimingPreview(m_selectedCandidate.value("bpm").toDouble(),
                               m_selectedCandidate.value("pulseTimeSeconds").toDouble() * 1000,
                               start * 1000, end * 1000);
}
void AnalysisEditor::showTimingResult(const BpmDetector::DetectionResult &result,
                                      const QString &pipelineError)
{
    clearDiagnostics();
    m_diagnostics = analysis::timingDiagnostics(result);
    m_diagnostics["pipelineError"] = pipelineError;
    m_diagnostics["audioPath"] = m_audioPath;
    m_diagnostics["sourceIdentity"] = m_sourceIdentity;
    for (const auto &id :
         {"tempoCandidates", "windows", "tempoTrack", "periodicityLayers", "uncertainRegions"})
        fillTable(id, m_diagnostics.value(id).toArray());
    const auto map = m_diagnostics.value("tempoMap").toObject();
    fillTable("segments", map.value("segments").toArray());
    // The core exposes phase anchors and a maximum residual, not per-anchor
    // residuals. Evaluate the published cubic phase model at each anchor.
    QJsonArray fit;
    for (const auto &v : map.value("anchors").toArray())
    {
        auto row = v.toObject();
        const double time = row.value("timeSeconds").toDouble();
        const double beat = row.value("phaseBeat").toDouble();
        for (const auto &sv : map.value("segments").toArray())
        {
            const auto segment = sv.toObject();
            if (beat < segment.value("startBeat").toDouble() || beat > segment.value("endBeat").toDouble())
                continue;
            double lo = 0, hi = 1;
            for (int iteration = 0; iteration < 56; ++iteration)
            {
                const double x = (lo + hi) / 2;
                const double phase = segment.value("startBeat").toDouble() +
                                     x * (segment.value("phaseLinear").toDouble() +
                                          x * (segment.value("phaseQuadratic").toDouble() +
                                               x * segment.value("phaseCubic").toDouble()));
                if (phase < beat)
                    lo = x;
                else
                    hi = x;
            }
            const double start = segment.value("startSeconds").toDouble();
            const double predicted = start + (lo + hi) / 2 * (segment.value("endSeconds").toDouble() - start);
            row["residualMilliseconds"] = (predicted - time) * 1000; // signed diagnostic
            break;
        }
        fit.append(row);
    }
    fillTable("fit", fit);
    m_diagnostics["derivedAnchorFit"] = fit;
    QJsonArray rhythm;
    for (const auto &profile : m_diagnostics.value("rhythmProfiles").toArray())
        for (const auto &layer : profile.toObject().value("layers").toArray())
            rhythm.append(layer);
    fillTable("rhythmLayers", rhythm);
    const auto &a = result.analysis;
    QString summary =
        tr("Legacy: %1 BPM / offset %2 ms\nAnalysis: %3 · %4 windows · confidence %5\nGrid max residual: %6 "
           "ms · BPM list model error: %7 ms")
            .arg(result.hasLegacyResult() ? QString::number(result.bpm, 'f', 3) : tr("unavailable"))
            .arg(result.hasLegacyResult() ? QString::number(result.estimatedOffsetMs, 'f', 3)
                                          : tr("unavailable"))
            .arg(m_diagnostics.value("analysisStatus").toString())
            .arg(a.windowCount)
            .arg(a.confidence.overall, 0, 'f', 3)
            .arg(a.tempoMap.available ? QString::number(a.tempoMap.maximumAnchorResidualMilliseconds, 'f', 4)
                                      : tr("unavailable"))
            .arg(a.tempoMap.bpmListAvailable
                     ? QString::number(a.tempoMap.maximumBpmListModelErrorMilliseconds, 'f', 4)
                     : tr("unavailable"));
    for (const auto &error : {pipelineError, result.legacyError, result.analysisError,
                              a.tempoMap.available ? QString() : a.tempoMap.failureReason,
                              a.tempoMap.bpmListAvailable ? QString() : a.tempoMap.bpmListFailureReason})
        if (!error.isEmpty() && error != QLatin1String("none"))
            summary += '\n' + error;
    m_timingSummary->setText(summary);
    m_raw->setPlainText(QString::fromUtf8(QJsonDocument(m_diagnostics).toJson()));
    if (m_tables.value("tempoCandidates")->rowCount() > 0)
        m_tables.value("tempoCandidates")->setCurrentCell(0, 0);
}
void AnalysisEditor::runTiming()
{
    if (m_timingBusy)
        return;
    refreshAudioSource();
    if (m_audioPath.isEmpty() || !QFileInfo::exists(m_audioPath))
    {
        m_timingSummary->setText(tr("Chart audio is unavailable"));
        return;
    }
    clearDiagnostics();
    m_timingBusy = true;
    m_runTiming->setEnabled(false);
    m_cancelTiming->setEnabled(true);
    m_timingCancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_timingCancel;
    const auto generation = ++m_timingGeneration, source = m_sourceGeneration;
    const double start = m_timingStart->value() * 1000, duration = m_timingDuration->value() * 1000;
    AutoTiming2Options options;
    options.enableComplexSubdivisionAnalysis = m_complexSubdivision->isChecked();
    m_timingSummary->setText(
        tr("Analyzing… Cancel discards the result; an active core calculation finishes in the background."));
    BpmDetector::analyzeFromFileDetailedAsync(
        this, m_audioPath, start, duration,
        [this, source, generation, start, duration, options, cancel](bool, BpmDetector::DetectionResult r,
                                                                     const QString &error) {
            m_timingBusy = false;
            m_runTiming->setEnabled(true);
            m_cancelTiming->setEnabled(false);
            if (source != m_sourceGeneration || generation != m_timingGeneration || cancel->load())
                return;
            showTimingResult(r, error);
            m_diagnostics["requestedStartMs"] = start;
            m_diagnostics["requestedDurationMs"] = duration;
            m_diagnostics["enableComplexSubdivisionAnalysis"] = options.enableComplexSubdivisionAnalysis;
            m_raw->setPlainText(QString::fromUtf8(QJsonDocument(m_diagnostics).toJson()));
        },
        options, cancel);
}
void AnalysisEditor::cancelTiming()
{
    ++m_timingGeneration;
    if (m_timingCancel)
        *m_timingCancel = true;
    if (m_timingBusy && m_timingSummary)
        m_timingSummary->setText(tr("Result discarded. Waiting for the analysis worker to finish…"));
    if (m_cancelTiming)
        m_cancelTiming->setEnabled(false);
}
void AnalysisEditor::clearDiagnostics()
{
    m_selectedCandidate = {};
    m_candidateGrid->setChecked(false);
    m_candidateGrid->setEnabled(false);
    m_candidateSeek->setEnabled(false);
    m_canvas->clearTimingPreview();
    m_candidateDetails->setText(tr("Select a candidate or an analysis window to inspect its phase."));
    m_diagnostics = {};
    for (auto *table : m_tables)
        table->setRowCount(0);
    m_raw->clear();
    m_timingSummary->setText(tr("Analyze audio to inspect AutoTiming intermediate results."));
}
void AnalysisEditor::exportDiagnostics()
{
    if (m_diagnostics.isEmpty())
    {
        m_status->setText(tr("No diagnostic result to export"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export diagnostic JSON"),
                                                      "timing-diagnostics.json", tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const auto data = QJsonDocument(m_diagnostics).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        m_status->setText(tr("Export failed: %1").arg(file.errorString()));
    else
        m_status->setText(tr("Diagnostic JSON exported"));
}
void AnalysisEditor::saveLayout()
{
    if (!m_restored)
        return;
    auto s = layoutSettings();
    s.beginGroup("analysisEditor");
    s.setValue("geometry", saveGeometry());
    s.setValue("work", m_work->saveState());
    s.setValue("panels", m_panels->saveState());
    s.setValue("noteLaneWidth", m_canvas->noteLaneWidth());
    s.remove("spectrumFraction");
    s.setValue("msPerPixel", m_canvas->millisecondsPerPixel());
    s.setValue("syncView", m_syncView->isChecked());
    s.setValue("leftOpen", m_leftOpen);
    s.setValue("rightOpen", m_rightOpen);
}
void AnalysisEditor::restoreLayout()
{
    auto s = layoutSettings();
    s.beginGroup("analysisEditor");
    restoreGeometry(s.value("geometry").toByteArray());
    const int available = qMax(360, m_work->width());
    m_work->setSizes({available / 2, available / 2});
    m_work->restoreState(s.value("work").toByteArray());
    m_panels->restoreState(s.value("panels").toByteArray());
    m_canvas->setNoteLaneWidth(s.value("noteLaneWidth", 56).toInt());
    m_zoom->setValue(s.value("msPerPixel", 6).toDouble());
    m_syncView->setChecked(s.value("syncView", false).toBool());
    const int left = s.value("leftOpen", -1).toInt(), right = s.value("rightOpen", -1).toInt();
    if (left >= 0)
        togglePanel(false, left);
    if (right >= 0)
        togglePanel(true, right);
    m_restored = true;
}
void AnalysisEditor::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_restored)
        restoreLayout();
    QHash<QString, QKeySequence> bindings;
    for (const auto &command : m_router->commands())
        if (Settings::instance().hasShortcut(command.id))
            bindings.insert(command.id, Settings::instance().shortcut(command.id));
    m_router->applyBindings(bindings);
    refreshAudioSource();
    updateRange();
    updateTime(m_main->currentPlayTime());
    m_canvas->setFocus();
    scheduleSpectrum();
}
void AnalysisEditor::hideEvent(QHideEvent *event)
{
    saveLayout();
    m_canvas->cancelGesture();
    cancelTiming();
    if (m_spectrumCancel)
        *m_spectrumCancel = true;
    m_spectrumTimer->stop();
    QWidget::hideEvent(event);
}
void AnalysisEditor::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}
