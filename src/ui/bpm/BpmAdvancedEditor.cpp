#include "BpmAdvancedEditor.h"
#include "BpmCurveCanvas.h"
#include "ui/analysis/TimingToolsPanel.h"
#include "ui/analysis/TimingInterpolationPanel.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "controller/CommandRouter.h"
#include "utils/Settings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QAction>
#include <QShowEvent>
#include <QHideEvent>
#include <QScreen>

BpmAdvancedEditor::BpmAdvancedEditor(ChartController *chart, PlaybackController *playback, ChartCanvas *main,
                                     QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName("bpm.advancedEditor");
    setWindowTitle(tr("Advanced BPM editing"));
    setAttribute(Qt::WA_DeleteOnClose);
    setProperty("cceOwnCommandRouter", true);
    setProperty("cceNonModalTiming", true);
    setMinimumSize(720, 480);
    resize(1000, 780);
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    auto *root = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("The curve follows the main editor view. Click a timing point to inspect and seek; "
                               "middle-click seeks freely. Wheel scrolls; Ctrl + wheel zooms. Preview does not edit the chart."));
    hint->setWordWrap(true);
    root->addWidget(hint);
    auto *split = new QSplitter;
    m_canvas = new BpmCurveCanvas(chart, main, split);
    auto *tabs = new QTabWidget(split);
    tabs->setObjectName("bpm.tools");
    tabs->setMinimumWidth(300);
    auto *details = new QWidget;
    auto *detailLayout = new QVBoxLayout(details);
    auto *selected = new QLabel(tr("Select a chart timing point to inspect its exact beat and BPM."));
    selected->setObjectName("bpm.selectedTiming");
    selected->setWordWrap(true);
    selected->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(selected);
    detailLayout->addStretch();
    tabs->addTab(details, tr("Curve"));
    m_measure = new TimingToolsPanel(chart, m_canvas);
    m_interpolate = new TimingInterpolationPanel(chart, m_canvas);
    // Keep the error/impact report readable; the outer scroll area handles the
    // additional height instead of squeezing a multi-line report into one row.
    auto *report = m_interpolate->findChild<QLabel *>("analysis.interpolationSummary");
    report->setMinimumHeight(report->fontMetrics().lineSpacing() * 12);
    auto addTool = [&](QWidget *tool, const QString &title) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(tool);
        tabs->addTab(scroll, title);
    };
    addTool(m_measure, tr("Manual measurement"));
    addTool(m_interpolate, tr("Interpolation"));
    split->setStretchFactor(0, 3); split->setStretchFactor(1, 2);
    split->setSizes({500, 480});
    root->addWidget(split, 1);
    auto *buttons = new QHBoxLayout;
    auto *undo = new QPushButton(tr("Undo"));
    auto *redo = new QPushButton(tr("Redo"));
    buttons->addWidget(undo); buttons->addWidget(redo); buttons->addStretch();
    auto *close = new QPushButton(tr("Close"));
    buttons->addWidget(close); root->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(m_canvas, &BpmCurveCanvas::timingSelected, this, [chart, selected](int index) {
        const auto &point = chart->chart()->bpmList()[index];
        selected->setText(tr("Beat [%1, %2, %3]\n%4 BPM")
            .arg(point.beatNum).arg(point.numerator).arg(point.denominator).arg(point.bpm, 0, 'g', 15));
    });
    const auto clearSelection = [selected] { selected->setText(tr("Select a chart timing point to inspect its exact beat and BPM.")); };
    connect(chart, &ChartController::bpmListChanged, this, clearSelection);
    connect(chart, &ChartController::chartLoaded, this, clearSelection);
    if (playback)
    {
        connect(m_canvas, &BpmCurveCanvas::seekRequested, this, [playback, main](double audioMs) {
            playback->seekTo(audioMs); main->setScrollPos(audioMs);
        });
        connect(m_measure, &TimingToolsPanel::pickingStarted, playback, &PlaybackController::pause);
        connect(m_interpolate, &TimingInterpolationPanel::pickingStarted, playback, &PlaybackController::pause);
    }
    m_router = new CommandRouter(this);
    auto command = [this](const QString &id, const QString &title, const QKeySequence &key, auto callback) {
        auto *action = new QAction(title, this);
        connect(action, &QAction::triggered, this, callback);
        m_router->registerAction(action, id, key);
        return action;
    };
    auto *undoAction = command("edit.undo", tr("Undo"), QKeySequence::Undo, [chart] { chart->undo(); });
    auto *redoAction = command("edit.redo", tr("Redo"), QKeySequence::Redo, [chart] { chart->redo(); });
    command("edit.cancel", tr("Cancel preview"), QKeySequence(Qt::Key_Escape), [this] {
        m_canvas->cancelPicking(); m_interpolate->cancelPreview();
    });
    if (playback)
        command("playback.play_pause", tr("Play / Pause"), QKeySequence(Qt::Key_Space), [playback, main] {
            if (playback->state() == PlaybackController::Playing) playback->pause();
            else playback->playFromTime(main->currentPlayTime());
        });
    connect(undo, &QPushButton::clicked, undoAction, &QAction::trigger);
    connect(redo, &QPushButton::clicked, redoAction, &QAction::trigger);
    auto refreshUndo = [chart, undo, redo, undoAction, redoAction] {
        undoAction->setEnabled(chart->canUndo()); redoAction->setEnabled(chart->canRedo());
        undo->setEnabled(chart->canUndo()); redo->setEnabled(chart->canRedo());
    };
    connect(chart, &ChartController::undoStateChanged, this, refreshUndo);
    refreshUndo(); reloadShortcuts();
}
void BpmAdvancedEditor::reloadShortcuts()
{
    QHash<QString, QKeySequence> bindings;
    for (const auto &command : m_router->commands())
        if (Settings::instance().hasShortcut(command.id)) bindings.insert(command.id, Settings::instance().shortcut(command.id));
    m_router->applyBindings(bindings);
}
void BpmAdvancedEditor::showEvent(QShowEvent *event) { QWidget::showEvent(event); reloadShortcuts(); }
void BpmAdvancedEditor::hideEvent(QHideEvent *event)
{
    m_canvas->cancelPicking(); m_canvas->clearInterpolationPreview();
    QWidget::hideEvent(event);
}
