#include "AnalysisEditor.h"
#include "AnalysisCanvas.h"
#include "TransientPanel.h"
#include "TimingToolsPanel.h"
#include "TimingInterpolationPanel.h"
#include "AnalysisConfigPanel.h"
#include "AnalysisWorkbench.h"
#include "AudioOverview.h"
#include "TimingProposal.h"
#include "analysis/AnalysisConfig.h"
#include "analysis/SpectrumPaging.h"
#include "ui/BpmMeasureUtils.h"
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
#include <QScrollBar>
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
#include <QComboBox>
#include <QApplication>
#include <QElapsedTimer>
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
AnalysisEditor::AnalysisEditor(ChartController *chart, SelectionController *selection, PlaybackController *playback,
                               ChartCanvas *main, LongRangeSelector *range, QWidget *parent,
                               SpectrumRunner spectrumRunner)
    : QWidget(parent, Qt::Window), m_chart(chart), m_selection(selection), m_playback(playback), m_main(main),
      m_range(range)
{
    m_spectrumRunner = spectrumRunner ? std::move(spectrumRunner) : SpectrumService::prepareFileRangeAsync;
    setObjectName("analysis.editor");
    setProperty("cceOwnCommandRouter", true);
    setWindowTitle(tr("CCE — Analysis Editor"));
    resize(1180, 800);
    setMinimumSize(640, 400);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 4);
    root->setSpacing(4);
    auto *header = new QHBoxLayout;
    m_sourceName = new QLabel(tr("Analysis Editor"));
    m_sourceName->setObjectName("analysis.audioName");
    m_sourceName->setTextFormat(Qt::PlainText);
    m_sourceName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(m_sourceName, 1);
    header->addStretch();
    m_level = new QComboBox;
    m_level->setObjectName("analysis.interfaceLevel");
    m_level->addItems({tr("Normal"), tr("Advanced"), tr("Debug")});
    header->addWidget(m_level);
    m_timeView = new QComboBox;
    m_timeView->setObjectName("analysis.timeView");
    m_timeView->addItems({tr("Musical time"), tr("Audio time")});
    header->addWidget(m_timeView);
    m_syncView = new QCheckBox(tr("Sync visible range"));
    m_syncView->setObjectName("analysis.syncView");
    header->addWidget(m_syncView);
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
    m_configPanel = new AnalysisConfigPanel;
    m_work->addWidget(m_canvas);
    m_overview = new AudioOverview(m_canvas);
    m_work->addWidget(m_overview);
    connect(m_overview, &AudioOverview::seekRequested, this, &AnalysisEditor::seek);
    connect(m_playback, &PlaybackController::loopRangeChanged, m_overview, &AudioOverview::setLoop);
    m_overview->setLoop(m_playback->loopStartMs(), m_playback->loopEndMs(), m_playback->loopEnabled());
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
    auto *timingTabs = new QTabWidget;
    timingTabs->setObjectName("analysis.timingModes");
    m_timingTools = new TimingToolsPanel(chart, m_canvas);
    timingTabs->addTab(m_timingTools, tr("Measure"));
    timingTabs->addTab(createTimingPanel(), tr("AutoTiming"));
    m_interpolation = new TimingInterpolationPanel(chart, m_canvas);
    timingTabs->addTab(m_interpolation, tr("Interpolate"));
    timingTabs->setCurrentIndex(1);
    addAnalysisPanel(tr("Timing"), timingTabs);
    connect(timingTabs, &QTabWidget::currentChanged, this, [this](int index) {
        m_timingTools->stopPicking();
        m_interpolation->stopPicking();
        if (index != 1)
            m_candidateGrid->setChecked(false);
    });
    connect(m_timingTools, &TimingToolsPanel::pickingStarted, this, [this] {
        m_playback->pause();
        m_candidateGrid->setChecked(false);
    });
    connect(m_interpolation, &TimingInterpolationPanel::pickingStarted, this, [this] {
        m_playback->pause(); m_candidateGrid->setChecked(false);
    });
    m_transient = new TransientPanel(m_canvas);
    addAnalysisPanel(tr("Transient"), m_transient);
    connect(m_transient, &TransientPanel::seekRequested, this, [this](double ms) {
        m_syncView->setChecked(false);
        m_timeView->setCurrentIndex(1);
        m_zoom->setValue(1);
        seek(ms);
    });
    addAnalysisPanel(tr("Timbre"),
                     placeholder(tr("Future: spectral features, event groups and stereo characteristics.")));
    addAnalysisPanel(tr("Tracks"), placeholder(tr("Future: pseudo tracks, continuity and preferred track.")),
                     true);
    addAnalysisPanel(tr("Generation"),
                     placeholder(tr("Future: density, importance and charting rules. Candidates will use "
                                    "ghost preview before a separate undoable commit.")),
                     true);
    addAnalysisPanel(tr("Diagnostics"), createDiagnosticsPanel(), true);
    connect(m_transient, &TransientPanel::tracksChanged, this, &AnalysisEditor::updateAnalysisDisplay);
    connect(m_canvas, &AnalysisCanvas::analysisObjectSelected, this,
            [this](const QString &trackId, const QString &handle, const QJsonObject &details) {
                m_workbench->selectObject(trackId, handle, details);
                auto table = details.value("sourceTable").toString();
                if (table == "derivedAnchorFit")
                    table = "fit";
                if (table == "windowCandidates" && details.contains("sourceWindowRow"))
                {
                    const auto row = details.value("sourceWindowRow").toInt(-1);
                    auto *windows = m_tables.value("windows");
                    if (windows && row >= 0 && row < windows->rowCount())
                        windows->setCurrentCell(row, 0);
                }
                if (table == "transientPeaks")
                    m_transient->selectPeakAt(details.value("timeSeconds").toDouble());
                else if (auto *widget = m_tables.value(table))
                {
                    int row = details.value("sourceRow").toInt(-1);
                    if (row >= 0 && row < widget->rowCount())
                        widget->setCurrentCell(row, 0);
                    m_workbench->showTable(table);
                }
                const auto start = details.value("startSeconds"), end = details.value("endSeconds");
                if (start.isDouble() && end.isDouble() && end.toDouble() > start.toDouble())
                {
                    m_range->setStartBeat(m_canvas->beatAtTime(start.toDouble() * 1000));
                    m_range->setEndBeat(m_canvas->beatAtTime(end.toDouble() * 1000));
                    m_range->setRangeVisible(true);
                }
            });
    addAnalysisPanel(tr("Configuration"), m_configPanel);
    connect(m_configPanel, &AnalysisConfigPanel::configChanged, this, [this] {
        const auto s = m_configPanel->config().value("stable").toObject();
        QSignalBlocker a(m_preferLocal), b(m_complexSubdivision);
        m_preferLocal->setChecked(s.value("preferLocalTempoEvidence").toBool());
        m_complexSubdivision->setChecked(s.value("enableComplexSubdivisionAnalysis").toBool());
        invalidateTimingConfiguration();
    });
    connect(m_level, &QComboBox::currentIndexChanged, this, &AnalysisEditor::setInterfaceLevel);
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
    auto *deleteAction =
        registerCommand("edit.delete", tr("Delete selected notes"), QKeySequence(Qt::Key_Delete), [this] {
            if (m_canvas->hasFocus())
                m_canvas->deleteSelectedNotes();
        });
    connect(selection, &SelectionController::selectionChanged, this, [this, deleteAction] {
        deleteAction->setEnabled(!m_selection->selectedIndices().isEmpty());
    });
    deleteAction->setEnabled(!m_selection->selectedIndices().isEmpty());
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
        m_timeView->setEnabled(!on);
    });
    connect(m_zoom, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_canvas->musicalView())
            m_canvas->setBeatsPerPixel(value);
        else
            m_canvas->setMillisecondsPerPixel(value);
    });
    connect(m_timeView, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_canvas->setMusicalView(index == 0);
    });
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
    m_spectrumTimer->setInterval(80);
    connect(m_spectrumTimer, &QTimer::timeout, this, &AnalysisEditor::requestSpectrum);
    connect(playback->audioPlayer(), &AudioPlayer::durationChanged, this, &AnalysisEditor::scheduleSpectrum);
    connect(playback->audioPlayer(), &AudioPlayer::loadingStateChanged, this, &AnalysisEditor::scheduleSpectrum);
    connect(m_canvas, &AnalysisCanvas::viewportChanged, this, [this] {
        const QSignalBlocker blocker(m_zoom);
        const QSignalBlocker modeBlocker(m_timeView);
        const bool musical = m_canvas->musicalView();
        m_timeView->setCurrentIndex(musical ? 0 : 1);
        m_zoom->setDecimals(musical ? 4 : 2);
        m_zoom->setRange(musical ? .0001 : .1, musical ? 10. : 100.);
        m_zoom->setSuffix(musical ? tr(" beat / px") : tr(" ms / px"));
        m_zoom->setValue(musical ? m_canvas->beatsPerPixel() : m_canvas->millisecondsPerPixel());
        scheduleSpectrum();
    });
    connect(chart, &ChartController::chartLoaded, this, [this] {
        m_session.clearSource();
        m_configPanel->loadProject(m_chart->chartFilePath());
        ++m_sourceGeneration;
        ++m_spectrumGeneration;
        if (m_spectrumCancel)
            *m_spectrumCancel = true;
        m_canvas->clearSpectrum();
        m_transient->clearSource();
        clearDiagnostics();
        cancelTiming();
        m_playback->setLoopRange(0, 0, false);
        m_sourceIdentity.clear();
        refreshAudioSource();
        updateRange();
    });
    connect(chart, &ChartController::metaDataChanged, this, &AnalysisEditor::refreshAudioSource);
    m_configPanel->loadProject(m_chart->chartFilePath());
    setInterfaceLevel(0);
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
    if (m_overviewCancel)
        *m_overviewCancel = true;
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
    m_timingTools->stopPicking();
    m_interpolation->stopPicking();
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
    m_overview->setRange(m_canvas->timeAtBeat(m_range->currentStartBeat()),
                         m_canvas->timeAtBeat(m_range->currentEndBeat()), m_range->isRangeVisible());
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
    m_sourceName->setText(path.isEmpty() ? tr("Analysis Editor")
                                       : tr("Analysis Editor · %1").arg(info.fileName()));
    m_sourceName->setToolTip(path);
    m_sourceIdentity = identity;
    m_session.clearSource();
    if (m_overviewCancel)
        *m_overviewCancel = true;
    m_overview->setEnvelope({});
    ++m_sourceGeneration;
    ++m_spectrumGeneration;
    if (m_spectrumCancel)
        *m_spectrumCancel = true;
    cancelTiming();
    clearDiagnostics();
    m_canvas->clearSpectrum();
    m_transient->clearSource();
    m_spectrumPages.clear();
    m_spectrumFailureKey.clear();
    m_cachedEofMs = -1;
    scheduleSpectrum();
    requestOverview();
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
    const auto *audio = m_playback->audioPlayer();
    const double audioDuration = audio->isLoaded() ? audio->duration() : -1;
    double eof = audioDuration > 0 ? audioDuration : -1;
    if (m_cachedEofMs >= 0)
        eof = eof >= 0 ? qMin(eof, m_cachedEofMs) : m_cachedEofMs;
    const auto range = analysis::spectrumPageRange(low, high, eof);
    if (!range.valid)
    {
        m_canvas->setStatus({});
        return;
    }
    // Prefetch before either visible edge reaches the page boundary. Larger
    // safety margins are reserved in the next page, so minor scrolls do not
    // continually decode overlapping pages.
    const double span = range.requiredEndMs - range.requiredStartMs;
    const double guard = qMin(qMax(1000., span * .25), (120000. - span) / 4);
    const double guardStart = qMax(0., range.requiredStartMs - guard);
    const double guardEnd = eof >= 0 ? qMin(eof, range.requiredEndMs + guard) : range.requiredEndMs + guard;
    auto covers = [](const analysis::StereoSpectrum &s, double a, double b) {
        return s.valid() && s.startSeconds * 1000 <= a + .1 && (s.startSeconds + s.durationSeconds) * 1000 + .1 >= b;
    };
    bool visibleCovered = covers(m_canvas->spectrum(), range.requiredStartMs, range.requiredEndMs);
    bool guardCovered = covers(m_canvas->spectrum(), guardStart, guardEnd);
    const SpectrumService::Page *selected = nullptr;
    if (!guardCovered)
        for (auto it = m_spectrumPages.crbegin(); it != m_spectrumPages.crend(); ++it)
        {
            if (covers(*it->spectrum, guardStart, guardEnd))
            {
                selected = &*it;
                guardCovered = true;
                break;
            }
            if (!visibleCovered && !selected && covers(*it->spectrum, range.requiredStartMs, range.requiredEndMs))
                selected = &*it;
        }
    if (selected)
    {
        m_canvas->setSpectrumPage(selected->spectrum, selected->raster);
        m_transient->setSpectrum(selected->spectrum);
        visibleCovered = true;
        m_status->setText(tr("Stereo spectrum ready · gutters: peak / RMS"));
    }
    if (visibleCovered)
        m_canvas->setStatus({});
    if (guardCovered)
        return;
    const double start = range.startMs, duration = range.durationMs();
    const QString key = m_sourceIdentity + '|' + QString::number(start) + '|' + QString::number(duration);
    if (key == m_spectrumFailureKey)
        return;
    if (m_spectrumBusy)
    {
        if (!visibleCovered
            && (range.requiredStartMs < m_pendingStart || range.requiredEndMs > m_pendingStart + m_pendingDuration))
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
    if (visibleCovered)
        m_status->setText(tr("Preloading adjacent spectrum…"));
    else
        m_canvas->setStatus(tr("Decoding stereo spectrum…"));
    m_spectrumRunner(this, m_audioPath, start, duration, cancel,
                     [this, generation, source, key, duration, cancel](SpectrumService::Page page) {
                         m_spectrumBusy = false;
                         if (generation != m_spectrumGeneration || source != m_sourceGeneration || cancel->load())
                         {
                             scheduleSpectrum();
                             return;
                         }
                         const bool ready = page.valid();
                         if (ready)
                         {
                             const auto &result = *page.spectrum;
                             if (result.durationSeconds * 1000 + 20 < duration)
                                 m_cachedEofMs = (result.startSeconds + result.durationSeconds) * 1000;
                             m_spectrumPages.append(std::move(page));
                             while (m_spectrumPages.size() > 3)
                                 m_spectrumPages.removeFirst();
                             // Select against the current viewport, which may have moved
                             // while this page was being prepared. An obsolete prefetch
                             // must never replace a page that still covers the screen.
                             requestSpectrum();
                         }
                         else
                         {
                             m_spectrumFailureKey = key;
                             const auto error = page.spectrum ? QString::fromStdString(page.spectrum->error)
                                                              : tr("Invalid spectrum result");
                             m_status->setText(error);
                             if (!m_canvas->spectrum().valid())
                                 m_canvas->setStatus(error);
                         }
                         if (!ready)
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
    m_timingStart->setDecimals(6);
    m_timingStart->setSuffix(tr(" s"));
    m_timingDuration = new QDoubleSpinBox;
    m_timingDuration->setObjectName("analysis.timingDuration");
    m_timingDuration->setRange(0, 86400);
    m_timingDuration->setDecimals(6);
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
    m_complexSubdivision->setObjectName("analysis.complexSubdivision");
    m_complexSubdivision->setToolTip(tr("Raw periodicity evidence is always available. Semantic rhythm "
                                        "profiles require Core's confidence gates."));
    layout->addWidget(m_complexSubdivision);
    m_preferLocal = new QCheckBox(tr("Prefer local tempo evidence"));
    m_preferLocal->setObjectName("analysis.preferLocal");
    layout->addWidget(m_preferLocal);
    auto updateOptions = [this] {
        auto config = m_configPanel->config();
        auto stable = config.value("stable").toObject();
        stable["preferLocalTempoEvidence"] = m_preferLocal->isChecked();
        stable["enableComplexSubdivisionAnalysis"] = m_complexSubdivision->isChecked();
        config["stable"] = stable;
        m_configPanel->setConfig(config);
    };
    connect(m_complexSubdivision, &QCheckBox::toggled, this, updateOptions);
    connect(m_preferLocal, &QCheckBox::toggled, this, updateOptions);
    connect(m_timingStart, &QDoubleSpinBox::valueChanged, this, [this] {
        invalidateTimingConfiguration();
    });
    connect(m_timingDuration, &QDoubleSpinBox::valueChanged, this, [this] {
        invalidateTimingConfiguration();
    });
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
    m_timingTables = tabs;
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
        const auto inspectRow = [this, t, id](int row) {
            const auto *item = t->item(row, 0);
            if (!item)
                return;
            const auto object = item->data(Qt::UserRole).toJsonObject();
            if (id == QLatin1String("windows"))
                selectDiagnosticWindow(object);
            else if (id == QLatin1String("tempoCandidates") || id == QLatin1String("windowCandidates"))
                selectTimingCandidate(object);
        };
        connect(t, &QTableWidget::currentCellChanged, this, [inspectRow](int row, int, int, int) {
            inspectRow(row);
        });
        // Re-selecting a row after inspecting another table must restore its
        // candidate too; the current cell need not have changed.
        connect(t, &QTableWidget::cellClicked, this, [inspectRow](int row, int) {
            inspectRow(row);
        });
        connect(t, &QTableWidget::cellDoubleClicked, this, [this, t, id, tabs, inspectRow](int row, int) {
            inspectRow(row);
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
                m_timeView->setCurrentIndex(1);
                m_zoom->setValue(1);
                seek(v.toDouble() * 1000);
            }
            if (id == QLatin1String("windows"))
                if (m_workbench)
                    m_workbench->showWindowCandidates();
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
    m_modelGrid = new QCheckBox(tr("Preview fitted tempo map"));
    m_modelGrid->setObjectName("analysis.modelGrid");
    m_showTracks = new QCheckBox(tr("Show analysis tracks"));
    m_showTracks->setObjectName("analysis.showTracks");
    m_showTracks->setChecked(true);
    m_applyMap = new QPushButton(tr("Preview and apply reliable tempo changes…"));
    m_applyMap->setObjectName("analysis.applyTempoMap");
    m_applyMap->setEnabled(false);
    m_modelGrid->setEnabled(false);
    layout->addWidget(m_modelGrid);
    layout->addWidget(m_showTracks);
    layout->addWidget(m_applyMap);
    connect(m_modelGrid, &QCheckBox::toggled, this, &AnalysisEditor::updateAnalysisDisplay);
    connect(m_showTracks, &QCheckBox::toggled, this, &AnalysisEditor::updateAnalysisDisplay);
    connect(m_applyMap, &QPushButton::clicked, this, &AnalysisEditor::applyTempoMap);
    connect(m_candidateGrid, &QCheckBox::toggled, this, &AnalysisEditor::updateTimingPreview);
    connect(m_candidateSeek, &QPushButton::clicked, this, [this] {
        const auto pulse = m_selectedCandidate.value("pulseTimeSeconds");
        if (!pulse.isDouble() || !std::isfinite(pulse.toDouble()))
            return;
        m_syncView->setChecked(false);
        m_timeView->setCurrentIndex(1);
        m_zoom->setValue(1);
        seek(pulse.toDouble() * 1000);
    });
    auto *hint =
        new QLabel(tr("Double-click a window to inspect its local candidates and audio. Global "
                      "candidates currently carry tempo only. Scores/costs are diagnostics, not probabilities."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    return panel;
}
QWidget *AnalysisEditor::createDiagnosticsPanel()
{
    m_workbench = new AnalysisWorkbench(m_canvas);
    connect(m_workbench, &AnalysisWorkbench::exportRequested, this, &AnalysisEditor::exportDiagnostics);
    connect(m_workbench, &AnalysisWorkbench::seekRequested, this, &AnalysisEditor::seek);
    connect(m_workbench, &AnalysisWorkbench::rangeRequested, this, [this](double a, double b) {
        m_range->setStartBeat(m_canvas->beatAtTime(a));
        m_range->setEndBeat(m_canvas->beatAtTime(b));
        m_range->setRangeVisible(true);
    });
    connect(m_workbench, &AnalysisWorkbench::hypothesisSelected, this, &AnalysisEditor::inspectHypothesis);
    connect(m_workbench, &AnalysisWorkbench::trackVisibilityChanged, this, &AnalysisEditor::updateAnalysisDisplay);
    connect(m_workbench, &AnalysisWorkbench::evidenceSelected, this, [this](const QJsonObject &record) {
        m_selectedEvidence = record;
        updateAnalysisDisplay();
    });
    while (m_timingTables->count())
    {
        auto *table = qobject_cast<QTableWidget *>(m_timingTables->widget(0));
        const auto label = m_timingTables->tabText(0);
        m_timingTables->removeTab(0);
        m_workbench->adoptTable(table->objectName().mid(QStringLiteral("analysis.table.").size()), label, table);
    }
    m_timingTables->hide();
    m_raw = m_workbench->rawEditor();
    return m_workbench;
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
    {
        auto *locals = m_tables.value("windowCandidates");
        locals->setCurrentCell(0, 0);
        // A hidden table can have a narrow provisional viewport when selection
        // scrolls the first cell into view. Keep the BPM column fully visible.
        locals->horizontalScrollBar()->setValue(0);
        selectTimingCandidate(rows.first().toObject());
    }
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
    const bool hasPulse =
        candidate.value("hasPulseTime").toBool() && pulse.isDouble() && std::isfinite(pulse.toDouble());
    m_candidateSeek->setEnabled(hasPulse);
    const double bpm = candidate.value("bpm").toDouble();
    const double start =
        candidate.value("previewStartSeconds").toDouble(m_diagnostics.value("analysisStartMs").toDouble() / 1000);
    const double end =
        candidate.value("previewEndSeconds").toDouble(start + m_diagnostics.value("durationSeconds").toDouble());
    const bool canPreview = hasPulse && std::isfinite(bpm) && bpm > 0 && bpm <= 10000 && std::isfinite(start)
                            && std::isfinite(end) && start >= 0 && end > start;
    m_candidateGrid->setEnabled(canPreview);
    if (!canPreview)
        m_candidateGrid->setChecked(false);
    if (candidate.isEmpty())
        m_candidateDetails->setText(tr("Select a candidate or an analysis window to inspect its phase."));
    else
    {
        const QString scope = candidate.contains("windowId") ? tr("Window #%1 · %2–%3 s")
                                                                   .arg(cellText(candidate.value("windowId")))
                                                                   .arg(start, 0, 'f', 3)
                                                                   .arg(end, 0, 'f', 3)
                                                             : tr("Global candidate");
        m_candidateDetails->setText(
            tr("%1\n%2 BPM · pulse %3\nPhase confidence %4 · legacy offset %5 ms")
                .arg(scope)
                .arg(bpm, 0, 'f', 5)
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
    const double start = m_selectedCandidate.value("previewStartSeconds")
                             .toDouble(m_diagnostics.value("analysisStartMs").toDouble() / 1000);
    const double end = m_selectedCandidate.value("previewEndSeconds")
                           .toDouble(start + m_diagnostics.value("durationSeconds").toDouble());
    m_canvas->setTimingPreview(m_selectedCandidate.value("bpm").toDouble(),
                               m_selectedCandidate.value("pulseTimeSeconds").toDouble() * 1000, start * 1000,
                               end * 1000);
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
    const auto fit = analysis::anchorFit(result.analysis.tempoMap);
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
    {
        m_tables.value("tempoCandidates")->setCurrentCell(0, 0);
        selectTimingCandidate(m_diagnostics.value("tempoCandidates").toArray().first().toObject());
    }
    m_result = result;
    m_inspectedMap = result.analysis.tempoMap;
    m_inspectedDiagnostics = m_diagnostics;
    m_modelGrid->setEnabled(result.analysis.tempoMap.available);
    m_applyMap->setEnabled(result.hasAnalysis() && result.analysis.valid
                           && BpmMeasureUtils::buildTimingMapProposal(result.analysis, m_chart->chart()->bpmList(),
                                                                      m_chart->chart()->meta().offset)
                                  .available);
    m_workbench->setResult(m_diagnostics);
    updateAnalysisDisplay();
}
void AnalysisEditor::runTiming()
{
    if (m_timingBusy)
        return;
    refreshAudioSource();
    const auto config = m_configPanel->config();
    const double start = m_timingStart->value() * 1000, duration = m_timingDuration->value() * 1000;
    auto errors = analysis::validateRequest(config, start, duration);
    if (m_audioPath.isEmpty() || !QFileInfo::exists(m_audioPath))
        errors << tr("Chart audio is unavailable");
    const double audioDuration = m_playback->audioPlayer()->duration();
    if (audioDuration > 0 && (start >= audioDuration || start + duration > audioDuration + .001))
        errors << tr("Requested interval exceeds the remaining audio. Choose an exact interval within the file.");
    if (!errors.isEmpty())
    {
        m_timingSummary->setText(errors.join('\n'));
        return;
    }
    clearDiagnostics();
    m_timingBusy = true;
    m_runTiming->setEnabled(false);
    m_cancelTiming->setEnabled(true);
    const auto source = m_sourceGeneration;
    const auto request = m_session.begin(m_sourceIdentity, start, duration, config);
    m_timingSummary->setText(
        tr("Analyzing… Cancel discards the result; an active Core calculation finishes in the background."));
    const bool started =
        m_session.dispatch(this, m_audioPath, request, [this, source](analysis::AnalysisCompletion completion) {
            m_timingBusy = false;
            m_runTiming->setEnabled(true);
            m_cancelTiming->setEnabled(false);
            refreshAudioSource();
            if (source != m_sourceGeneration || completion.discarded
                || !m_session.isCurrent(completion.request, m_sourceIdentity))
            {
                m_status->setText(
                    tr("Previous analysis finished and was discarded. Analyze the current configuration when ready."));
                m_timingSummary->setText(m_status->text());
                return;
            }
            showTimingResult(completion.result, completion.error);
            const auto derived = m_diagnostics.value("derivedAnchorFit");
            m_diagnostics = completion.diagnostics;
            m_diagnostics["derivedAnchorFit"] = derived;
            m_diagnostics["audioPath"] = m_audioPath;
            m_diagnostics["actualDurationMs"] = completion.result.analysis.durationSeconds * 1000;
            m_inspectedDiagnostics = m_diagnostics;
            m_raw->setPlainText(QString::fromUtf8(QJsonDocument(m_diagnostics).toJson()));
            m_session.remember(m_diagnostics);
            m_workbench->setResult(m_diagnostics);
        });
    if (!started)
    {
        m_timingBusy = false;
        m_runTiming->setEnabled(true);
        m_cancelTiming->setEnabled(false);
        m_status->setText(tr("The previous analysis is still finishing."));
    }
}
void AnalysisEditor::cancelTiming()
{
    m_session.discard();

    if (m_timingBusy && m_timingSummary)
        m_timingSummary->setText(tr("Result discarded. Waiting for the analysis worker to finish…"));
    if (m_cancelTiming)
        m_cancelTiming->setEnabled(false);
}
void AnalysisEditor::clearDiagnostics()
{
    m_selectedCandidate = {};
    m_result = {};
    m_inspectedMap = {};
    m_inspectedDiagnostics = {};
    m_selectedEvidence = {};
    m_modelGrid->setChecked(false);
    m_modelGrid->setEnabled(false);
    m_applyMap->setEnabled(false);
    m_canvas->setTempoMapPreview({});
    m_canvas->setAnalysisTracks({}, {});
    m_candidateGrid->setChecked(false);
    m_candidateGrid->setEnabled(false);
    m_candidateSeek->setEnabled(false);
    m_canvas->clearTimingPreview();
    m_candidateDetails->setText(tr("Select a candidate or an analysis window to inspect its phase."));
    m_diagnostics = {};
    for (auto *table : m_tables)
        table->setRowCount(0);
    m_raw->clear();
    if (m_workbench)
        m_workbench->setResult({});
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
    s.setValue("musicalView", m_canvas->musicalView());
    s.setValue("beatsPerPixel", m_canvas->beatsPerPixel());
    s.setValue("interfaceLevel", m_interfaceLevel);
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
    m_canvas->setMillisecondsPerPixel(s.value("msPerPixel", 6).toDouble());
    if (s.value("musicalView", !s.contains("msPerPixel")).toBool())
        m_canvas->setBeatsPerPixel(s.value("beatsPerPixel", .012).toDouble());
    m_level->setCurrentIndex(qBound(0, s.value("interfaceLevel", 0).toInt(), 2));
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
    requestOverview();
}
void AnalysisEditor::hideEvent(QHideEvent *event)
{
    saveLayout();
    m_canvas->cancelGesture();
    m_timingTools->stopPicking();
    m_interpolation->stopPicking();
    cancelTiming();
    if (m_spectrumCancel)
        *m_spectrumCancel = true;
    m_spectrumTimer->stop();
    if (m_overviewCancel)
        *m_overviewCancel = true;
    QWidget::hideEvent(event);
}
void AnalysisEditor::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void AnalysisEditor::invalidateTimingConfiguration()
{
    m_session.invalidateConfig();

    if (!m_diagnostics.isEmpty())
    {
        m_diagnostics["resultState"] = "OldConfiguration";
        m_raw->setPlainText(QString::fromUtf8(QJsonDocument(m_diagnostics).toJson()));
        if (m_workbench)
            m_workbench->setResult(m_diagnostics);
        m_timingSummary->setText(tr("Configuration/range changed. These results belong to the previous input settings; "
                                    "analyze again before preview or apply."));
    }
    m_candidateGrid->setChecked(false);
    m_candidateGrid->setEnabled(false);
    m_modelGrid->setChecked(false);
    m_modelGrid->setEnabled(false);
    m_applyMap->setEnabled(false);
    m_canvas->clearTimingPreview();
    m_canvas->setTempoMapPreview({});
}
void AnalysisEditor::setInterfaceLevel(int level)
{
    m_interfaceLevel = qBound(0, level, 2);
    m_canvas->cancelGesture();
    m_timingTools->stopPicking();
    m_interpolation->stopPicking();
    m_configPanel->setLevel(m_interfaceLevel);
    m_workbench->setLevel(m_interfaceLevel);
    m_complexSubdivision->setVisible(level >= 1);
    m_preferLocal->setVisible(level >= 1);
    m_candidateDetails->setVisible(level >= 1);
    m_modelGrid->setVisible(level >= 1);
    m_showTracks->setVisible(level >= 1);
    for (int i = 0; i < m_rightTabs->count(); ++i)
        m_rightTabs->setTabVisible(i, level >= 1);
    m_rightTabs->setVisible(level >= 1);
    m_leftTabs->setTabVisible(1, level >= 1);
    m_leftTabs->setTabVisible(2, level >= 2);
    if (level == 0)
    {
        m_rightPanels->hide();
        m_rightOpen = -1;
    }
    updateAnalysisDisplay();
}
void AnalysisEditor::updateAnalysisDisplay()
{
    const auto data = m_inspectedDiagnostics.isEmpty() ? m_diagnostics : m_inspectedDiagnostics;
    auto tracks = analysis::diagnosticTracks(data);
    for (auto track :
         analysis::diagnosticTracks(m_diagnostics.isEmpty() ? QJsonObject() : m_workbench->pairedBaseline()))
        if (track.id == "tempo")
        {
            track.id = "baselineTempo";
            track.viewGroup = "baselineTempo";
            track.laneGroup = "tempo";
            track.label = tr("Baseline observed tempo");
            tracks.append(track);
        }
    if (!m_selectedEvidence.isEmpty())
    {
        const auto start = m_selectedEvidence.value("startSeconds"), end = m_selectedEvidence.value("endSeconds"),
                   value = m_selectedEvidence.value("observedRateBpm");
        if (start.isDouble() && end.isDouble() && value.isDouble() && std::isfinite(value.toDouble())
            && end.toDouble() > start.toDouble())
        {
            analysis::AnalysisTrack track{"periodicity", tr("Selected periodicity evidence"), "events/min"};
            track.viewGroup = "periodicity";
            track.laneGroup = "periodicity";
            track.provenance = {{"sourceIdentity", m_sourceIdentity},
                                {"timeline", "whole-file audio seconds"},
                                {"method", "Selected raw periodicity evidence"}};
            track.points = {
                {start.toDouble(), value.toDouble(), "periodicity:start", "Evidence", {}, m_selectedEvidence},
                {end.toDouble(), value.toDouble(), "periodicity:end", "Evidence", {}, m_selectedEvidence}};
            tracks.append(track);
        }
    }
    const QJsonObject source{{"sourceIdentity", m_sourceIdentity}};
    if (m_interfaceLevel >= 1 && m_showTracks->isChecked())
    {
        if (m_workbench->trackVisible("spectrumEnvelope"))
            tracks += analysis::spectrumTracks(m_canvas->spectrum(), source);
        if (m_workbench->trackVisible("transientFlux") || m_workbench->trackVisible("transientEvents"))
        {
            auto transient = m_transient->tracks();
            for (auto &track : transient)
                track.provenance["sourceIdentity"] = m_sourceIdentity;
            tracks += transient;
        }
    }
    for (auto &track : tracks)
        track.visible =
            track.visible && m_workbench->trackVisible(track.viewGroup.isEmpty() ? track.id : track.viewGroup);
    m_canvas->setAnalysisTracks(
        m_interfaceLevel >= 1 && m_showTracks->isChecked() ? tracks : QVector<analysis::AnalysisTrack>(), {});
    m_canvas->setTempoMapPreview(m_modelGrid->isEnabled() && m_modelGrid->isChecked() ? m_inspectedMap
                                                                                      : AutoTiming2TempoMap());
    if (!m_diagnostics.isEmpty() && m_interfaceLevel == 0
        && m_diagnostics.value("resultState").toString() != QLatin1String("OldConfiguration"))
    {
        auto recommendation = BpmMeasureUtils::selectRecommendation(m_result);
        QString summary;
        if (recommendation.available)
            summary = tr("Suggested tempo: %1 BPM\n").arg(recommendation.bpm, 0, 'f', 6);
        switch (recommendation.quality)
        {
            case BpmMeasureUtils::EvidenceQuality::Supported:
                summary += tr("Tempo suggestion has supporting evidence.");
                break;
            case BpmMeasureUtils::EvidenceQuality::Uncertain:
                summary += tr("Tempo suggestion is uncertain; inspect coverage and competing candidates.");
                break;
            default:
                summary += tr("No supported tempo suggestion is available.");
                break;
        }
        summary += tr("\n%1 tempo candidates · %2 analysis windows. Preview does not change the chart.")
                       .arg(m_result.analysis.tempoCandidates.size())
                       .arg(m_result.analysis.windowCount);
        if (!m_result.analysisError.isEmpty())
            summary += '\n' + m_result.analysisError;
        if (!m_diagnostics.value("pipelineError").toString().isEmpty())
            summary += '\n' + m_diagnostics.value("pipelineError").toString();
        m_timingSummary->setText(summary);
    }
}
void AnalysisEditor::inspectHypothesis(int index)
{
    m_inspectedDiagnostics = m_diagnostics;
    m_inspectedMap = m_result.analysis.tempoMap;
    if (index >= 0 && index < m_result.analysis.tempoHypotheses.size())
    {
        const auto h = m_diagnostics.value("tempoHypotheses").toArray()[index].toObject();
        m_inspectedDiagnostics["tempoTrack"] = h.value("track");
        m_inspectedDiagnostics["tempoMap"] = h.value("tempoMap");
        m_inspectedMap = m_result.analysis.tempoHypotheses[index].tempoMap;
        m_inspectedDiagnostics["derivedAnchorFit"] = analysis::anchorFit(m_inspectedMap);
    }
    fillTable("tempoTrack", m_inspectedDiagnostics.value("tempoTrack").toArray());
    fillTable("segments", m_inspectedDiagnostics.value("tempoMap").toObject().value("segments").toArray());
    fillTable("fit", m_inspectedDiagnostics.value("derivedAnchorFit").toArray());
    m_modelGrid->setEnabled(m_inspectedMap.available
                            && m_diagnostics.value("resultState").toString() != QLatin1String("OldConfiguration"));
    updateAnalysisDisplay();
}
void AnalysisEditor::requestOverview()
{
    if (!isVisible() || m_overviewBusy || m_overview->hasEnvelope() || m_audioPath.isEmpty()
        || !QFileInfo::exists(m_audioPath))
        return;
    m_overviewBusy = true;
    m_overviewCancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = m_overviewCancel;
    const auto source = m_sourceGeneration;
    m_overview->setStatus(tr("Preparing whole-audio RMS / peak overview…"));
    SpectrumService::analyzeEnergyAsync(this, m_audioPath, cancel,
                                        [this, cancel, source](SpectrumService::EnergyEnvelope result) {
                                            m_overviewBusy = false;
                                            refreshAudioSource();
                                            if (cancel->load() || source != m_sourceGeneration)
                                            {
                                                if (isVisible())
                                                    requestOverview();
                                                return;
                                            }
                                            m_overview->setEnvelope(std::move(result));
                                        });
}
void AnalysisEditor::applyTempoMap()
{
    if (!m_result.hasAnalysis() || !m_result.analysis.valid)
        return;
    if (m_diagnostics.isEmpty() || m_diagnostics.value("resultState").toString() == QLatin1String("OldConfiguration"))
        return;
    refreshAudioSource();
    if (m_diagnostics.isEmpty())
        return;
    const auto proposal = BpmMeasureUtils::buildTimingMapProposal(m_result.analysis, m_chart->chart()->bpmList(),
                                                                  m_chart->chart()->meta().offset);
    if (!proposal.available)
    {
        m_status->setText(proposal.unavailableReason);
        return;
    }
    const auto source = m_sourceIdentity;
    const auto configRevision = m_session.configRevision;
    analysis::confirmTimingProposal(this, m_chart, tr("Apply AutoTiming tempo map"),
                                    tr("Replace only reliable tempo-change intervals from %1 to %2 seconds with %3 "
                                       "generated timing entries. Model error: %4 ms.")
                                        .arg(proposal.sourceStartSeconds)
                                        .arg(proposal.sourceEndSeconds)
                                        .arg(proposal.generatedEntryCount)
                                        .arg(proposal.maximumModelErrorMs),
                                    proposal.bpmList, m_chart->revision(), [this, source, configRevision] {
                                        refreshAudioSource();
                                        return source == m_sourceIdentity && configRevision == m_session.configRevision
                                               && !m_diagnostics.isEmpty();
                                    });
}
