#include "MainWindow.h"
#include "MainWindowPrivate.h"
#include "app/Application.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "plugin/PluginManager.h"
#include "ui/dialogs/LogSettingsDialog.h"
#include "ui/dialogs/PluginManagerDialog.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/CustomWidgets/RealtimePreviewWidget.h"
#include "ui/LeftPanel.h"
#include "ui/NoteEditPanel.h"
#include "ui/LongRangeSelector.h"
#include "ui/PluginActionPanel.h"
#include "file/ChartIO.h"
#include "utils/Settings.h"
#include "utils/Logger.h"
#include "utils/DiagnosticCollector.h"
#include "model/Skin.h"
#include "model/Note.h"
#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QSpinBox>
#include <QColorDialog>
#include <QPainter>
#include <QTabWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QVariant>
#include <QSlider>
#include <QCoreApplication>
#include <QAction>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QDialog>
#include <QGroupBox>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

namespace
{
    QColor dialogTextColorFor(const QColor &bg)
    {
        const double r = bg.redF();
        const double g = bg.greenF();
        const double b = bg.blueF();
        const double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        return (luminance >= 0.5) ? QColor(20, 20, 20) : QColor(245, 245, 245);
    }

    QString themedDialogCss(const QColor &baseBg)
    {
        const QColor fg = dialogTextColorFor(baseBg);
        const bool darkTheme = (fg.lightness() > 128);
        const QColor panelBg = darkTheme ? baseBg.lighter(108) : baseBg.darker(103);
        const QColor inputBg = darkTheme ? panelBg.lighter(120) : panelBg.darker(105);
        const QColor buttonBg = darkTheme ? panelBg.lighter(132) : panelBg.darker(112);
        const QColor border = darkTheme ? panelBg.lighter(165) : panelBg.darker(145);
        const QColor disabledText = darkTheme ? QColor("#9A9A9A") : QColor("#707070");

        return QString(
                   "QDialog { background-color: %1; color: %2; }"
                   "QLabel, QCheckBox, QGroupBox { color: %2; }"
                   "QLineEdit, QAbstractSpinBox, QComboBox, QTextEdit, QPlainTextEdit {"
                   "  background-color: %3; color: %2; border: 1px solid %4; }"
                   "QPushButton { background-color: %5; color: %2; border: 1px solid %4; padding: 3px 8px; }"
                   "QPushButton:disabled { color: %6; }"
                   "QGroupBox { border: 1px solid %4; margin-top: 8px; padding-top: 10px; }"
                   "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %2; }")
            .arg(panelBg.name(), fg.name(), inputBg.name(), border.name(), buttonBg.name(), disabledText.name());
    }

    QString humanizeActionId(const QString &actionId)
    {
        QStringList parts = actionId.split('_', Qt::SkipEmptyParts);
        for (QString &p : parts)
        {
            if (p.isEmpty())
                continue;
            p[0] = p[0].toUpper();
        }
        return parts.join(' ');
    }

    bool hasReadableGlyph(const QString &text)
    {
        for (const QChar ch : text)
        {
            if (ch.isLetterOrNumber())
                return true;
        }
        return false;
    }

    QString safeActionTitle(const PluginInterface::ToolAction &action)
    {
        const QString raw = action.title.trimmed();
        if (!raw.isEmpty() && raw != "?" && hasReadableGlyph(raw))
            return raw;

        const QString fallback = humanizeActionId(action.actionId.trimmed());
        if (!fallback.isEmpty())
            return fallback;
        return QObject::tr("Plugin Action");
    }

    QString firstCanvasInteractionPluginId(PluginManager *pm)
    {
        if (!pm)
            return QString();
        const QVector<PluginInterface *> plugins = pm->plugins();
        for (PluginInterface *p : plugins)
        {
            if (!p)
                continue;
            if (p->pluginId().trimmed() == "builtin.note_chain_assist" &&
                p->hasCapability(PluginInterface::kCapabilityCanvasInteraction))
            {
                return p->pluginId();
            }
        }
        for (PluginInterface *p : plugins)
        {
            if (!p)
                continue;
            if (!p->hasCapability(PluginInterface::kCapabilityCanvasInteraction))
                continue;
            return p->pluginId();
        }
        return QString();
    }

    void seedCurveSidecarFromSourceIfMissing(const QString &workingChartPath, const QString &sourceChartPath)
    {
        if (workingChartPath.trimmed().isEmpty() || sourceChartPath.trimmed().isEmpty())
            return;

        const QFileInfo workingInfo(workingChartPath);
        const QFileInfo sourceInfo(sourceChartPath);
        if (!workingInfo.exists() || !sourceInfo.exists())
            return;

        const QString workingCurve = QDir(workingInfo.absoluteDir().filePath(".mcce-plugin"))
                                         .filePath(workingInfo.completeBaseName() + ".curve_tbd.json");
        if (QFileInfo::exists(workingCurve))
            return;

        const QString sourceCurve = QDir(sourceInfo.absoluteDir().filePath(".mcce-plugin"))
                                        .filePath(sourceInfo.completeBaseName() + ".curve_tbd.json");
        if (!QFileInfo::exists(sourceCurve))
            return;

        QDir().mkpath(QFileInfo(workingCurve).absolutePath());
        QFile::remove(workingCurve);
        QFile::copy(sourceCurve, workingCurve);
    }

    void enrichContextWithSidecarPaths(QVariantMap *context, const QString &chartPath, const QString &sourceChartPath = QString())
    {
        if (!context)
            return;
        if (chartPath.trimmed().isEmpty())
            return;

        const QFileInfo fi(chartPath);
        if (!fi.exists())
            return;

        seedCurveSidecarFromSourceIfMissing(chartPath, sourceChartPath);

        const QString sidecarDir = fi.absoluteDir().filePath(".mcce-plugin");
        const QString chartStem = fi.completeBaseName();
        context->insert("sidecar_dir", sidecarDir);
        context->insert("curve_project_path", QDir(sidecarDir).filePath(chartStem + ".curve_tbd.json"));

        QVariantList styleLibraryPaths;
        styleLibraryPaths.append(QDir(sidecarDir).filePath("styles"));
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!appData.isEmpty())
            styleLibraryPaths.append(QDir(appData).filePath("plugin_styles"));
        context->insert("style_library_paths", styleLibraryPaths);
    }
}

void MainWindow::populatePluginToolsMenu()
{
    if (!d->pluginToolsMenu)
        return;

    d->pluginToolsMenu->clear();
    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginManager())
    {
        QAction *none = d->pluginToolsMenu->addAction(tr("(Plugin manager unavailable)"));
        none->setEnabled(false);
        return;
    }

    const QList<PluginManager::ToolActionEntry> entries = app->pluginManager()->toolActions();
    if (entries.isEmpty())
    {
        QAction *none = d->pluginToolsMenu->addAction(tr("(No plugin actions)"));
        none->setEnabled(false);
        return;
    }

    bool added = false;
    for (const PluginManager::ToolActionEntry &entry : entries)
    {
        if (entry.pluginId.trimmed() == "tool.note_chain_assist.cpp" &&
            entry.action.actionId.trimmed() == "open_note_chain_assist")
        {
            continue;
        }

        const QString title = safeActionTitle(entry.action);
        const QString text = QString("%1  [%2]").arg(title, entry.pluginDisplayName);
        QAction *act = d->pluginToolsMenu->addAction(text);
        if (!entry.action.description.isEmpty())
            act->setToolTip(entry.action.description);

        QVariantMap payload;
        payload.insert("plugin_id", entry.pluginId);
        payload.insert("action_id", entry.action.actionId);
        payload.insert("title", title);
        payload.insert("confirm_message", entry.action.confirmMessage);
        payload.insert("host_action", entry.action.hostAction);
        payload.insert("scope_selector", entry.action.scopeSelector);
        payload.insert("requires_undo_snapshot", entry.action.requiresUndoSnapshot);
        payload.insert("placement", entry.action.placement);
        payload.insert("checkable", entry.action.checkable);
        payload.insert("checked", entry.action.checked);
        payload.insert("sync_plugin_tool_mode_with_checked", entry.action.syncPluginToolModeWithChecked);
        act->setData(payload);
        connect(act, &QAction::triggered, this, &MainWindow::triggerPluginToolAction);
        added = true;
    }

    if (!added)
    {
        QAction *none = d->pluginToolsMenu->addAction(tr("(No menu actions)"));
        none->setEnabled(false);
    }
}

void MainWindow::populatePluginPanelsMenu()
{
    if (!d->pluginPanelsMenu)
        return;

    d->pluginPanelsMenu->clear();
    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginManager())
    {
        QAction *none = d->pluginPanelsMenu->addAction(tr("(Plugin manager unavailable)"));
        none->setEnabled(false);
        return;
    }

    const QList<PluginManager::FloatingPanelEntry> entries = app->pluginManager()->floatingPanels();
    QAction *toolPanelAction = d->pluginPanelsMenu->addAction(tr("Plugin Tools"));
    connect(toolPanelAction, &QAction::triggered, this, [this]()
            { showDockPanel(d->pluginToolsDock); });
    if (!entries.isEmpty())
        d->pluginPanelsMenu->addSeparator();

    for (const PluginManager::FloatingPanelEntry &entry : entries)
    {
        const QString text = QString("%1  [%2]").arg(entry.panel.title, entry.pluginDisplayName);
        QAction *act = d->pluginPanelsMenu->addAction(text);
        if (!entry.panel.description.isEmpty())
            act->setToolTip(entry.panel.description);

        QVariantMap payload;
        payload.insert("plugin_id", entry.pluginId);
        payload.insert("panel_id", entry.panel.panelId);
        payload.insert("title", entry.panel.title);
        act->setData(payload);
        connect(act, &QAction::triggered, this, &MainWindow::triggerPluginPanelAction);
    }
}

void MainWindow::refreshPluginUiExtensions()
{
    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginManager())
    {
        Logger::warn("refreshPluginUiExtensions skipped: plugin manager unavailable.");
        return;
    }

    d->pluginActionMeta.clear();
    d->batchEditDisabledActions.clear();
    for (QAction *a : d->pluginToolbarActions)
    {
        if (d->pluginToolBar && a)
            d->pluginToolBar->removeAction(a);
        delete a;
    }
    d->pluginToolbarActions.clear();

    QList<LeftPanel::PluginQuickAction> sidebarActions;
    QList<NoteEditPanel::PluginPlacementAction> notePanelActions;
    QList<PluginActionPanel::Action> panelActions;
    const QList<PluginManager::ToolActionEntry> entries = app->pluginManager()->toolActions();
    Logger::info(QString("refreshPluginUiExtensions: discovered %1 plugin tool actions.").arg(entries.size()));
    for (const PluginManager::ToolActionEntry &entry : entries)
    {
        // Hide legacy note-chain bootstrap action when old plugin variants coexist.
        if (entry.pluginId.trimmed() == "tool.note_chain_assist.cpp" &&
            entry.action.actionId.trimmed() == "open_note_chain_assist")
        {
            Logger::info("Skipping legacy action tool.note_chain_assist.cpp::open_note_chain_assist");
            continue;
        }

        QVariantMap meta;
        meta.insert("plugin_id", entry.pluginId);
        meta.insert("action_id", entry.action.actionId);
        const QString title = safeActionTitle(entry.action);
        meta.insert("title", title);
        meta.insert("confirm_message", entry.action.confirmMessage);
        meta.insert("host_action", entry.action.hostAction);
        meta.insert("scope_selector", entry.action.scopeSelector);
        meta.insert("requires_undo_snapshot", entry.action.requiresUndoSnapshot);
        meta.insert("placement", entry.action.placement);
        meta.insert("checkable", entry.action.checkable);
        meta.insert("checked", entry.action.checked);
        meta.insert("sync_plugin_tool_mode_with_checked", entry.action.syncPluginToolModeWithChecked);

        const QString key = entry.pluginId + "::" + entry.action.actionId;
        d->pluginActionMeta.insert(key, meta);

        PluginActionPanel::Action panelAction;
        panelAction.pluginDisplayName = entry.pluginDisplayName;
        panelAction.description = entry.action.description;
        panelAction.meta = meta;
        panelActions.append(panelAction);

        const QString placement = entry.action.placement.toLower();
        if (placement == QString(PluginInterface::kPlacementTopToolbar) && d->pluginToolBar)
        {
            QAction *act = d->pluginToolBar->addAction(title);
            if (entry.action.scopeSelector.trimmed().compare(QStringLiteral("note_range"), Qt::CaseInsensitive) == 0)
                act->setToolTip(tr("Open dockable controls for %1").arg(title));
            else if (!entry.action.description.isEmpty())
                act->setToolTip(entry.action.description);
            act->setCheckable(entry.action.checkable);
            if (entry.action.checkable)
                act->setChecked(entry.action.checked);
            act->setData(meta);
            if (entry.action.scopeSelector.trimmed().compare(QStringLiteral("note_range"), Qt::CaseInsensitive) == 0)
            {
                connect(act, &QAction::triggered, this,
                        [this, pluginId = entry.pluginId, actionId = entry.action.actionId]()
                        {
                            showDockPanel(d->pluginToolsDock);
                            if (d->pluginActionPanel)
                                d->pluginActionPanel->focusAction(pluginId, actionId);
                        });
            }
            else
            {
                connect(act, &QAction::triggered, this, &MainWindow::triggerPluginToolAction);
            }
            d->pluginToolbarActions.append(act);
        }
        else if (placement == QString(PluginInterface::kPlacementLeftSidebar))
        {
            LeftPanel::PluginQuickAction qa;
            qa.pluginId = entry.pluginId;
            qa.pluginDisplayName = entry.pluginDisplayName;
            qa.actionId = entry.action.actionId;
            qa.title = title;
            qa.tooltip = entry.action.description;
            qa.checkable = entry.action.checkable;
            qa.checked = entry.action.checked;
            sidebarActions.append(qa);
        }
        else if (placement == QString(PluginInterface::kPlacementRightNotePanel))
        {
            if (entry.action.actionId.trimmed() == "toggle_anchor_placement")
                continue;
            NoteEditPanel::PluginPlacementAction qa;
            qa.pluginId = entry.pluginId;
            qa.actionId = entry.action.actionId;
            qa.title = title;
            qa.tooltip = entry.action.description;
            qa.checkable = entry.action.checkable;
            qa.checked = entry.action.checked;
            notePanelActions.append(qa);
        }
    }

    if (d->leftPanel)
        d->leftPanel->setPluginQuickActions(sidebarActions);
    if (d->notePanel)
        d->notePanel->setPluginPlacementActions(notePanelActions);
    if (d->pluginActionPanel)
        d->pluginActionPanel->setActions(panelActions);

    const QString interactionPluginId = firstCanvasInteractionPluginId(app->pluginManager());
    const bool hasInteractionPlugin = !interactionPluginId.isEmpty();
    if (d->pluginToolModeAction)
    {
        d->pluginToolModeAction->setEnabled(hasInteractionPlugin);
        if (!hasInteractionPlugin && d->pluginToolModeAction->isChecked())
            d->pluginToolModeAction->setChecked(false);
        else if (hasInteractionPlugin && d->canvas)
        {
            const bool checked = d->canvas->isPluginToolModeActive();
            if (d->pluginToolModeAction->isChecked() != checked)
            {
                const QSignalBlocker blocker(d->pluginToolModeAction);
                d->pluginToolModeAction->setChecked(checked);
            }
        }
    }
    if (d->pluginToolModeToolbarAction)
    {
        d->pluginToolModeToolbarAction->setEnabled(hasInteractionPlugin);
        if (!hasInteractionPlugin && d->pluginToolModeToolbarAction->isChecked())
            d->pluginToolModeToolbarAction->setChecked(false);
        else if (hasInteractionPlugin && d->canvas)
        {
            const bool checked = d->canvas->isPluginToolModeActive();
            if (d->pluginToolModeToolbarAction->isChecked() != checked)
            {
                const QSignalBlocker blocker(d->pluginToolModeToolbarAction);
                d->pluginToolModeToolbarAction->setChecked(checked);
            }
        }
    }
    if (!interactionPluginId.isEmpty())
        d->pluginToolModePluginId = interactionPluginId;
    else
        d->pluginToolModePluginId.clear();
}

void MainWindow::triggerPluginToolAction()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;
    runPluginActionWithMeta(action->data().toMap());
}

void MainWindow::triggerPluginQuickAction(const QString &pluginId, const QString &actionId)
{
    const QString key = pluginId + "::" + actionId;
    if (!d->pluginActionMeta.contains(key))
        return;
    runPluginActionWithMeta(d->pluginActionMeta.value(key));
}

void MainWindow::togglePluginEnhancedToolMode(bool enabled)
{
    if (!d->canvas)
        return;

    // Keep menu toggle and toolbar toggle in sync without recursion.
    if (auto *act = d->pluginToolModeAction)
    {
        if (act->isChecked() != enabled)
        {
            const QSignalBlocker blocker(act);
            act->setChecked(enabled);
        }
    }
    if (auto *act = d->pluginToolModeToolbarAction)
    {
        if (act->isChecked() != enabled)
        {
            const QSignalBlocker blocker(act);
            act->setChecked(enabled);
        }
    }

    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginManager())
    {
        if (d->pluginToolModeAction)
            d->pluginToolModeAction->setChecked(false);
        if (d->pluginToolModeToolbarAction)
            d->pluginToolModeToolbarAction->setChecked(false);
        return;
    }

    QString pluginId = d->pluginToolModePluginId.trimmed();
    if (pluginId.isEmpty())
        pluginId = firstCanvasInteractionPluginId(app->pluginManager());
    if (enabled && pluginId.isEmpty())
    {
        statusBar()->showMessage(tr("No plugin supports canvas interaction."), 2500);
        if (d->pluginToolModeAction)
            d->pluginToolModeAction->setChecked(false);
        if (d->pluginToolModeToolbarAction)
            d->pluginToolModeToolbarAction->setChecked(false);
        return;
    }

    d->pluginToolModePluginId = pluginId;
    d->canvas->setPluginToolMode(enabled, pluginId);
    if (d->notePanel)
    {
        if (enabled)
        {
            d->notePanel->setModeFromHost(NoteEditPanel::PlaceAnchorMode);
            d->canvas->setMode(ChartCanvas::AnchorPlace);
        }
        else if (d->notePanel->currentMode() == NoteEditPanel::PlaceAnchorMode)
        {
            d->notePanel->setModeFromHost(NoteEditPanel::PlaceNoteMode);
            d->canvas->setMode(ChartCanvas::PlaceNote);
        }
    }
    statusBar()->showMessage(
        enabled ? tr("Plugin enhanced tool mode ON")
                : tr("Plugin enhanced tool mode OFF"),
        2500);
}

bool MainWindow::runPluginActionWithMeta(const QVariantMap &meta)
{
    const QString pluginId = meta.value("plugin_id").toString();
    const QString actionId = meta.value("action_id").toString();
    const QString actionTitle = meta.value("title").toString();
    const QString confirmMessage = meta.value("confirm_message").toString();
    const QString hostAction = meta.value("host_action").toString().trimmed().toLower();
    const QString scopeSelector = meta.value("scope_selector").toString().trimmed().toLower();
    const bool requiresUndo = meta.value("requires_undo_snapshot", true).toBool();
    const bool syncToolModeWithChecked = meta.value("sync_plugin_tool_mode_with_checked", false).toBool();
    if (pluginId.isEmpty() || actionId.isEmpty())
        return false;

    const auto applyPostActionPluginModeSync = [this, pluginId, actionId, syncToolModeWithChecked, meta]()
    {
        if (!syncToolModeWithChecked)
            return;
        if (!d->canvas)
            return;
        const QString key = pluginId + "::" + actionId;
        const QVariantMap refreshedMeta = d->pluginActionMeta.value(key, meta);
        if (!refreshedMeta.value("checkable", false).toBool())
            return;
        const bool checked = refreshedMeta.value("checked", false).toBool();
        d->pluginToolModePluginId = pluginId;
        togglePluginEnhancedToolMode(checked);
    };

    if (!confirmMessage.isEmpty())
    {
        QMessageBox::StandardButton confirm = QMessageBox::warning(
            this,
            tr("Confirm Plugin Action"),
            confirmMessage,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (confirm != QMessageBox::Yes)
            return false;
    }

    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginManager() || !d->chartController)
        return false;

    if (hostAction == "undo")
    {
        undo();
        statusBar()->showMessage(tr("Plugin action completed: %1").arg(actionTitle), 2500);
        refreshPluginUiExtensions();
        applyPostActionPluginModeSync();
        return true;
    }
    if (hostAction == "redo")
    {
        redo();
        statusBar()->showMessage(tr("Plugin action completed: %1").arg(actionTitle), 2500);
        refreshPluginUiExtensions();
        applyPostActionPluginModeSync();
        return true;
    }

    const QString chartPath = d->workingChartPath.isEmpty() ? d->chartController->chartFilePath() : d->workingChartPath;
    const QString sourceChartPath = d->sourceChartPath.isEmpty() ? d->currentChartPath : d->sourceChartPath;
    if (chartPath.isEmpty())
    {
        QMessageBox::information(this, tr("No Chart"), tr("Please open a chart first."));
        return false;
    }

    const bool hasProvidedActionContext = meta.contains(QStringLiteral("action_context"));
    QVariantMap actionContext = meta.value(QStringLiteral("action_context")).toMap();
    if (scopeSelector == QLatin1String("note_range") && !hasProvidedActionContext)
    {
        QDialog scopeDialog(this);
        scopeDialog.setWindowTitle(tr("Format Note Colors"));
        scopeDialog.setStyleSheet(themedDialogCss(Settings::instance().backgroundColor()));

        QVBoxLayout *scopeLayout = new QVBoxLayout(&scopeDialog);
        QFormLayout *scopeForm = new QFormLayout;
        QComboBox *scopeCombo = new QComboBox(&scopeDialog);
        const int selectedCount = d->selectionController
                                      ? d->selectionController->selectedIndices().size()
                                      : 0;
        if (selectedCount > 0)
            scopeCombo->addItem(tr("Selected Notes (%1)").arg(selectedCount), QStringLiteral("selected"));
        scopeCombo->addItem(tr("Beat Range"), QStringLiteral("range"));
        scopeCombo->addItem(tr("Entire Chart"), QStringLiteral("all"));
        scopeForm->addRow(tr("Format Scope:"), scopeCombo);
        scopeLayout->addLayout(scopeForm);

        LongRangeSelector *rangeEditor = new LongRangeSelector(&scopeDialog);
        rangeEditor->setInputOnlyMode(tr("Beat Range"));
        rangeEditor->setChartController(d->chartController);
        rangeEditor->setPlaybackController(d->playbackController);
        if (d->canvas)
        {
            rangeEditor->setTimeDivision(d->canvas->timeDivision());
            rangeEditor->setStartBeat(d->canvas->scrollBeat());
            rangeEditor->setEndBeat(d->canvas->scrollBeat() + d->canvas->visibleBeatRange());
        }
        scopeLayout->addWidget(rangeEditor);

        QLabel *scopeHint = new QLabel(
            tr("Only Normal and Rain note start colors are formatted. Sound notes are not changed."),
            &scopeDialog);
        scopeHint->setWordWrap(true);
        scopeLayout->addWidget(scopeHint);

        QDialogButtonBox *scopeButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &scopeDialog);
        if (QPushButton *okButton = scopeButtons->button(QDialogButtonBox::Ok))
            okButton->setText(tr("Format"));
        connect(scopeButtons, &QDialogButtonBox::accepted, &scopeDialog, &QDialog::accept);
        connect(scopeButtons, &QDialogButtonBox::rejected, &scopeDialog, &QDialog::reject);
        scopeLayout->addWidget(scopeButtons);

        const auto updateRangeVisibility = [scopeCombo, rangeEditor]() {
            rangeEditor->setVisible(scopeCombo->currentData().toString() == QLatin1String("range"));
        };
        connect(scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                &scopeDialog, [updateRangeVisibility](int) { updateRangeVisibility(); });
        updateRangeVisibility();

        if (scopeDialog.exec() != QDialog::Accepted)
            return false;

        const QString selectedScope = scopeCombo->currentData().toString();
        actionContext.insert(QStringLiteral("format_scope"), selectedScope);
        if (selectedScope == QLatin1String("range"))
        {
            if (!rangeEditor->hasValidRange())
            {
                QMessageBox::warning(this, tr("Format Note Colors"), tr("Enter a valid beat range."));
                return false;
            }
            double startBeat = rangeEditor->currentStartBeat();
            double endBeat = rangeEditor->currentEndBeat();
            if (startBeat > endBeat)
                std::swap(startBeat, endBeat);
            actionContext.insert(QStringLiteral("range_start_beat"), startBeat);
            actionContext.insert(QStringLiteral("range_end_beat"), endBeat);
        }
    }

    QVariantMap context;
    if (!d->workingChartPath.isEmpty())
    {
        if (!d->chartController->saveChart(d->workingChartPath))
        {
            QMessageBox::warning(this, tr("Plugin Action"), tr("Failed to sync working copy before plugin action."));
            return false;
        }
    }
    context.insert("chart_path", chartPath);
    context.insert("chart_path_native", QDir::toNativeSeparators(chartPath));
    context.insert("chart_path_canonical", QFileInfo(chartPath).canonicalFilePath());
    if (!sourceChartPath.isEmpty())
        context.insert("chart_path_source", sourceChartPath);
    enrichContextWithSidecarPaths(&context, chartPath, sourceChartPath);
    if (d->canvas)
    {
        const QVariantMap canvasContext = d->canvas->pluginCanvasActionContext();
        for (auto it = canvasContext.constBegin(); it != canvasContext.constEnd(); ++it)
            context.insert(it.key(), it.value());
    }
    for (auto it = actionContext.constBegin(); it != actionContext.constEnd(); ++it)
        context.insert(it.key(), it.value());
    context.insert("action_title", actionTitle);
    Logger::info(QString("Running plugin action: plugin=%1 action=%2 path=%3")
                     .arg(pluginId)
                     .arg(actionId)
                     .arg(chartPath));

    const QString actionKey = pluginId + "::" + actionId;
    PluginInterface::BatchEdit batchEdit;
    const bool batchEditAvailable =
        app->pluginManager()->supportsHostBatchEdit(pluginId) &&
        !d->batchEditDisabledActions.contains(actionKey);
    if (batchEditAvailable &&
        app->pluginManager()->buildToolActionBatchEdit(pluginId, actionId, context, &batchEdit))
    {
        const bool ok = d->chartController->applyBatchEdit(
            tr("Plugin Action: %1").arg(actionTitle),
            batchEdit.notesToAdd,
            batchEdit.notesToRemove,
            batchEdit.notesToMove);
        if (!ok)
        {
            QMessageBox::warning(this, tr("Plugin Action"), tr("Plugin batch edit is empty or invalid: %1").arg(actionTitle));
            return false;
        }
        statusBar()->showMessage(tr("Plugin action completed: %1").arg(actionTitle), 2500);
        refreshPluginUiExtensions();
        applyPostActionPluginModeSync();
        return true;
    }
    else if (batchEditAvailable)
    {
        d->batchEditDisabledActions.insert(actionKey);
        Logger::warn(QString("Plugin action batch-edit disabled for this session after failure: %1")
                         .arg(actionKey));
    }

    if (!app->pluginManager()->runToolAction(pluginId, actionId, context))
    {
        Logger::warn(QString("Plugin action returned false: plugin=%1 action=%2 path=%3")
                         .arg(pluginId)
                         .arg(actionId)
                         .arg(chartPath));
        QMessageBox::warning(this, tr("Plugin Action"), tr("Plugin action failed: %1").arg(actionTitle));
        return false;
    }

    Chart mutated;
    if (!ChartIO::load(chartPath, mutated, false))
    {
        QMessageBox::warning(this, tr("Plugin Action"), tr("Plugin action finished, but failed to reload chart."));
        return false;
    }

    if (requiresUndo)
        d->chartController->applyExternalChartMutation(tr("Plugin Action: %1").arg(actionTitle), mutated);
    else
        d->chartController->loadChart(chartPath);

    statusBar()->showMessage(tr("Plugin action completed: %1").arg(actionTitle), 2500);
    refreshPluginUiExtensions();
    applyPostActionPluginModeSync();
    return true;
}

void MainWindow::closePluginPanels(const QString &reasonText)
{
    if (d->pluginPanelDocks.isEmpty())
        return;

    const auto docks = d->pluginPanelDocks;
    for (auto it = docks.constBegin(); it != docks.constEnd(); ++it)
    {
        if (it.value())
            it.value()->closeDockWidget();
    }
    d->pluginPanelDocks.clear();

    if (!reasonText.isEmpty())
        statusBar()->showMessage(reasonText, 2000);
}

void MainWindow::triggerPluginPanelAction()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QVariantMap meta = action->data().toMap();
    const QString pluginId = meta.value("plugin_id").toString();
    const QString panelId = meta.value("panel_id").toString();
    const QString title = meta.value("title").toString();
    if (pluginId.isEmpty() || panelId.isEmpty())
        return;

    const QString key = pluginId + "::" + panelId;
    if (d->pluginPanelDocks.contains(key) && d->pluginPanelDocks[key])
    {
        ads::CDockWidget *existing = d->pluginPanelDocks[key];
        existing->toggleView(true);
        existing->setAsCurrentTab();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginManager())
        return;

    QVariantMap context;
    const QString chartPath = d->workingChartPath.isEmpty() ? (d->chartController ? d->chartController->chartFilePath() : QString())
                                                            : d->workingChartPath;
    if (!chartPath.isEmpty())
    {
        context.insert("chart_path", chartPath);
        context.insert("chart_path_native", QDir::toNativeSeparators(chartPath));
        context.insert("chart_path_canonical", QFileInfo(chartPath).canonicalFilePath());
    }
    if (!d->sourceChartPath.isEmpty())
        context.insert("chart_path_source", d->sourceChartPath);
    enrichContextWithSidecarPaths(&context, chartPath, d->sourceChartPath);
    const QVariantMap workspaceConfig = app->pluginManager()->panelWorkspaceConfig(pluginId, context);
    if (!workspaceConfig.isEmpty())
        context.insert("workspace", workspaceConfig);

    if (!d->dockManager)
        return;

    const QString panelTitle = title.isEmpty() ? tr("Plugin Panel") : title;
    ads::CDockWidget *dock = new ads::CDockWidget(d->dockManager, panelTitle);
    dock->setObjectName(QStringLiteral("dock.plugin.%1.%2").arg(pluginId, panelId));
    dock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    dock->setFeature(ads::CDockWidget::DockWidgetForceCloseWithArea, true);
    if (workspaceConfig.value("default_layout").toString().toLower() == "advanced")
        dock->resize(680, 520);
    else if (workspaceConfig.value("default_layout").toString().toLower() == "dual")
        dock->resize(580, 460);

    QWidget *panel = app->pluginManager()->createFloatingPanel(pluginId, panelId, dock, context);
    if (!panel)
    {
        delete dock;
        QMessageBox::warning(this, tr("Plugin Panel"), tr("Failed to create plugin panel."));
        return;
    }

    panel->setStyleSheet(themedDialogCss(Settings::instance().backgroundColor()));
    dock->setWidget(panel, ads::CDockWidget::AutoScrollArea);
    if (d->pluginToolsDock && d->pluginToolsDock->dockAreaWidget())
        d->dockManager->addDockWidgetTabToArea(dock, d->pluginToolsDock->dockAreaWidget());
    else if (d->notePanelDock && d->notePanelDock->dockAreaWidget())
        d->dockManager->addDockWidgetTabToArea(dock, d->notePanelDock->dockAreaWidget());
    else
        d->dockManager->addDockWidget(ads::RightDockWidgetArea, dock);

    d->pluginPanelDocks.insert(key, dock);
    connect(dock, &QObject::destroyed, this, [this, key]()
            { d->pluginPanelDocks.remove(key); });
    dock->setAsCurrentTab();
}

void MainWindow::openPluginManager()
{
    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginManager())
    {
        QMessageBox::warning(this, tr("Plugin Manager"), tr("Plugin manager is not available."));
        return;
    }

    PluginManagerDialog dialog(app->pluginManager(), this);
    connect(&dialog, &PluginManagerDialog::pluginsReloading, this, [this]()
            {
                closePluginPanels(tr("Plugin panels were closed after plugin reload."));
                if (d->leftPanel)
                    d->leftPanel->setPluginQuickActions({});
                if (d->notePanel)
                    d->notePanel->setPluginPlacementActions({}); });
    connect(&dialog, &PluginManagerDialog::pluginsReloaded, this, [this, app]()
            {
                QString chartPath = d->workingChartPath;
                if (chartPath.trimmed().isEmpty() && d->chartController)
                    chartPath = d->chartController->chartFilePath();

                chartPath = chartPath.trimmed();
                if (!chartPath.isEmpty())
                {
                    app->pluginManager()->notifyChartLoaded(chartPath);
                    app->pluginManager()->notifyChartChanged();
                }
                refreshPluginUiExtensions(); });
    dialog.exec();
    refreshPluginUiExtensions();
}
void MainWindow::openLogSettings()
{
    Logger::info("Log settings dialog opened");
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Log Settings"));
    dialog.setMinimumSize(400, 300);
    dialog.setStyleSheet(themedDialogCss(Settings::instance().backgroundColor()));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QCheckBox *jsonLoggingCheck = new QCheckBox(tr("Enable JSON Logging"));
    jsonLoggingCheck->setChecked(Logger::isJsonLoggingEnabled());
    layout->addWidget(jsonLoggingCheck);

    QCheckBox *verboseCheck = new QCheckBox(tr("Enable Verbose Logging"));
    verboseCheck->setChecked(Logger::isVerbose());
    layout->addWidget(verboseCheck);

    QCheckBox *qtFilterCheck = new QCheckBox(tr("Enable Qt Noise Filter"));
    qtFilterCheck->setChecked(Settings::instance().qtMessageFilterEnabled());
    layout->addWidget(qtFilterCheck);

    QLabel *categoryLabel = new QLabel(tr("Qt Categories (comma-separated):"));
    QLineEdit *categoryEdit = new QLineEdit;
    categoryEdit->setText(Settings::instance().qtMessageFilterCategories().join(","));
    categoryEdit->setPlaceholderText("qt.multimedia.ffmpeg,qt.qpa.fonts");
    layout->addWidget(categoryLabel);
    layout->addWidget(categoryEdit);

    QLabel *prefixLabel = new QLabel(tr("Message Prefixes (comma-separated):"));
    QLineEdit *prefixEdit = new QLineEdit;
    prefixEdit->setText(Settings::instance().qtMessageFilterPrefixes().join(","));
    prefixEdit->setPlaceholderText("QFont::setPixelSize,QWindowsWindow::setGeometry");
    layout->addWidget(prefixLabel);
    layout->addWidget(prefixEdit);

    QHBoxLayout *logPathLayout = new QHBoxLayout;
    QLabel *pathLabel = new QLabel(tr("Log File:"));
    QLineEdit *pathEdit = new QLineEdit;
    pathEdit->setText(Logger::logFilePath());
    pathEdit->setReadOnly(true);
    QPushButton *openLogBtn = new QPushButton(tr("Open Log Folder"));
    connect(openLogBtn, &QPushButton::clicked, [this]()
            {
        QString logDir = QFileInfo(Logger::logFilePath()).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
        Logger::debug("Opened log folder"); });
    logPathLayout->addWidget(pathLabel);
    logPathLayout->addWidget(pathEdit);
    logPathLayout->addWidget(openLogBtn);
    layout->addLayout(logPathLayout);

    QHBoxLayout *jsonPathLayout = new QHBoxLayout;
    QLabel *jsonPathLabel = new QLabel(tr("JSON Log File:"));
    QLineEdit *jsonPathEdit = new QLineEdit;
    jsonPathEdit->setText(Logger::jsonLogFilePath());
    jsonPathEdit->setReadOnly(true);
    jsonPathLayout->addWidget(jsonPathLabel);
    jsonPathLayout->addWidget(jsonPathEdit);
    layout->addLayout(jsonPathLayout);

    layout->addStretch();

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
            {
        const auto parseCsv = [](const QString &text) -> QStringList
        {
            QStringList out;
            const QStringList parts = text.split(',', Qt::SkipEmptyParts);
            for (const QString &part : parts)
            {
                const QString trimmed = part.trimmed();
                if (!trimmed.isEmpty() && !out.contains(trimmed))
                    out.append(trimmed);
            }
            return out;
        };

        const QStringList categories = parseCsv(categoryEdit->text());
        const QStringList prefixes = parseCsv(prefixEdit->text());
        Logger::setJsonLoggingEnabled(jsonLoggingCheck->isChecked());
        Logger::setVerbose(verboseCheck->isChecked());
        Logger::setQtMessageFilterEnabled(qtFilterCheck->isChecked());
        Logger::setQtMessageFilterCategories(categories);
        Logger::setQtMessageFilterPrefixes(prefixes);
        Settings::instance().setQtMessageFilterEnabled(qtFilterCheck->isChecked());
        Settings::instance().setQtMessageFilterCategories(categories);
        Settings::instance().setQtMessageFilterPrefixes(prefixes);
        Logger::info(QString("Log settings changed - JSON: %1, Verbose: %2, Qt filter: %3")
                     .arg(jsonLoggingCheck->isChecked() ? "enabled" : "disabled")
                     .arg(verboseCheck->isChecked() ? "enabled" : "disabled")
                     .arg(qtFilterCheck->isChecked() ? "enabled" : "disabled"));
        dialog.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::openSessionSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Session Settings"));
    dialog.setMinimumSize(460, 260);
    dialog.setStyleSheet(themedDialogCss(Settings::instance().backgroundColor()));

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QGroupBox *sessionGroup = new QGroupBox(tr("Editing Session"), &dialog);
    QFormLayout *sessionLayout = new QFormLayout(sessionGroup);

    QCheckBox *autoSaveCheck = new QCheckBox(tr("Enable Auto Save"), sessionGroup);
    autoSaveCheck->setChecked(Settings::instance().autoSaveEnabled());
    QSpinBox *autoSaveIntervalSpin = new QSpinBox(sessionGroup);
    autoSaveIntervalSpin->setRange(15, 3600);
    autoSaveIntervalSpin->setSuffix(tr(" s"));
    autoSaveIntervalSpin->setValue(Settings::instance().autoSaveIntervalSec());
    autoSaveIntervalSpin->setEnabled(autoSaveCheck->isChecked());
    connect(autoSaveCheck, &QCheckBox::toggled, autoSaveIntervalSpin, &QWidget::setEnabled);
    QCheckBox *audioCorrectionCheck = new QCheckBox(tr("Enable Audio Correction (Testing)"), sessionGroup);
    audioCorrectionCheck->setChecked(Settings::instance().audioCorrectionEnabled());
    QComboBox *fpsCapCombo = new QComboBox(sessionGroup);
    fpsCapCombo->addItem(tr("Lock 60 FPS"), 60);
    fpsCapCombo->addItem(tr("Lock 90 FPS"), 90);
    fpsCapCombo->addItem(tr("Lock 120 FPS"), 120);
    fpsCapCombo->addItem(tr("Unlimited"), 0);
    {
        const int currentCap = Settings::instance().playbackFrameRateCap();
        const int idx = fpsCapCombo->findData(currentCap);
        fpsCapCombo->setCurrentIndex(idx >= 0 ? idx : 2);
    }

    sessionLayout->addRow(autoSaveCheck);
    sessionLayout->addRow(tr("Auto Save Interval:"), autoSaveIntervalSpin);
    sessionLayout->addRow(audioCorrectionCheck);
    sessionLayout->addRow(tr("Playback FPS Cap:"), fpsCapCombo);
    layout->addWidget(sessionGroup);

    QGroupBox *viewGroup = new QGroupBox(tr("Grid Beat Number"), &dialog);
    QVBoxLayout *viewGroupLayout = new QVBoxLayout(viewGroup);

    QLabel *previewLabel = new QLabel(viewGroup);
    previewLabel->setFixedHeight(60);
    previewLabel->setMinimumWidth(200);
    previewLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    previewLabel->setStyleSheet(QString("background-color: %1; border: 1px solid #555;")
                                    .arg(Settings::instance().backgroundColor().name()));

    QSpinBox *beatFontSizeSpin = new QSpinBox(viewGroup);
    beatFontSizeSpin->setRange(6, 24);
    beatFontSizeSpin->setValue(Settings::instance().beatNumberFontSize());
    beatFontSizeSpin->setSuffix(tr(" pt"));
    beatFontSizeSpin->setToolTip(tr("Font size for beat numbers on the left side of the grid"));

    auto updatePreviewPixmap = [previewLabel, beatFontSizeSpin]()
    {
        const int w = previewLabel->width();
        const int h = previewLabel->height();
        if (w <= 0 || h <= 0)
            return;
        QPixmap pix(w, h);
        pix.fill(Settings::instance().backgroundColor());
        QPainter p(&pix);
        const int midY = h / 2;
        const QColor lineColor(180, 180, 180);
        const QColor textColor(80, 80, 80);

        QFont font;
        font.setPointSize(beatFontSizeSpin->value());
        QFontMetrics fm(font);
        const QString sampleText = "221";
        const int textW = fm.horizontalAdvance(sampleText);
        const int padding = 2;

        p.setPen(QPen(lineColor, 2));
        p.drawLine(textW + padding, midY, w, midY);

        p.setPen(textColor);
        p.setFont(font);
        const int textY = midY + fm.ascent() / 2;
        p.drawText(0, textY, sampleText);
        p.end();
        previewLabel->setPixmap(pix);
    };

    QHBoxLayout *previewRow = new QHBoxLayout;
    previewRow->addWidget(new QLabel(tr("Size:"), viewGroup));
    previewRow->addWidget(beatFontSizeSpin);
    previewRow->addStretch();
    viewGroupLayout->addLayout(previewRow);
    viewGroupLayout->addWidget(previewLabel);
    layout->addWidget(viewGroup);

    QObject::connect(beatFontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                     [updatePreviewPixmap](int /*value*/) { updatePreviewPixmap(); });
    // Delay preview init until after dialog layout
    QTimer::singleShot(0, &dialog, [updatePreviewPixmap]() { updatePreviewPixmap(); });
    layout->addStretch();

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
            {
        Settings::instance().setAutoSaveEnabled(autoSaveCheck->isChecked());
        Settings::instance().setAutoSaveIntervalSec(autoSaveIntervalSpin->value());
        Settings::instance().setAudioCorrectionEnabled(audioCorrectionCheck->isChecked());
        const int fpsCap = fpsCapCombo->currentData().toInt();
        Settings::instance().setPlaybackFrameRateCap(fpsCap);
        Settings::instance().setBeatNumberFontSize(beatFontSizeSpin->value());
        if (d->playbackController && d->playbackController->audioPlayer())
            d->playbackController->audioPlayer()->setAudioCorrectionEnabled(audioCorrectionCheck->isChecked());
        if (d->playbackController)
            d->playbackController->setFrameRateCap(fpsCap);
        setupAutoSaveTimer();
        if (d->canvas)
            d->canvas->update();
        statusBar()->showMessage(tr("Session settings updated"), 2000);
        dialog.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::exportDiagnosticsReport()
{
    Logger::info("Diagnostics report export requested");
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Diagnostics Report"),
                                                    Settings::instance().lastOpenPath(),
                                                    tr("Text Files (*.txt);;JSON Files (*.json);;All Files (*.*)"));
    if (fileName.isEmpty())
    {
        Logger::debug("Export diagnostics cancelled");
        return;
    }

    try
    {
        DiagnosticCollector &collector = DiagnosticCollector::instance();
        DiagnosticCollector::DiagnosticReport report = collector.generateReport();

        if (fileName.endsWith(".json"))
        {
            QJsonDocument doc = collector.toJsonDocument();
            QFile file(fileName);
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(doc.toJson());
                file.close();
                Logger::info("Diagnostics report exported to JSON: " + fileName);
                QMessageBox::information(this, tr("Export Successful"),
                                         tr("Diagnostics report exported to:\n%1").arg(fileName));
            }
            else
            {
                Logger::error("Failed to open file for writing: " + fileName);
                QMessageBox::warning(this, tr("Export Failed"), tr("Failed to open file for writing."));
            }
        }
        else
        {
            QFile file(fileName);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                QTextStream stream(&file);
                stream << report.toFormattedString();
                file.close();
                Logger::info("Diagnostics report exported to text: " + fileName);
                QMessageBox::information(this, tr("Export Successful"),
                                         tr("Diagnostics report exported to:\n%1").arg(fileName));
            }
            else
            {
                Logger::error("Failed to open file for writing: " + fileName);
                QMessageBox::warning(this, tr("Export Failed"), tr("Failed to open file for writing."));
            }
        }
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("Exception during diagnostics export: %1").arg(e.what()));
        QMessageBox::critical(this, tr("Error"), tr("Exception during export:\n%1").arg(e.what()));
    }
}

void MainWindow::adjustNoteSize()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Note Size"));
    dialog.setMinimumSize(400, 300);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QHBoxLayout *sizeLayout = new QHBoxLayout;
    QLabel *label = new QLabel(tr("Size (pixels):"));
    QSpinBox *sizeSpin = new QSpinBox;
    sizeSpin->setRange(8, 64);
    sizeSpin->setValue(Settings::instance().noteSize());
    sizeLayout->addWidget(label);
    sizeLayout->addWidget(sizeSpin);
    layout->addLayout(sizeLayout);

    QLabel *remarkLabel = new QLabel(tr("Note: This setting affects fallback circle notes. When a skin is loaded, note size is controlled by skin calibration."));
    remarkLabel->setWordWrap(true);
    remarkLabel->setProperty("hintText", true);
    layout->addWidget(remarkLabel);

    QLabel *previewLabel = new QLabel;
    previewLabel->setFixedSize(128, 128);
    previewLabel->setStyleSheet("border: 1px solid gray; background: white;");
    previewLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(previewLabel, 0, Qt::AlignCenter);

    auto updatePreview = [previewLabel, sizeSpin, this]()
    {
        int sz = sizeSpin->value();
        QPixmap pix(128, 128);
        pix.fill(Qt::white);
        QPainter painter(&pix);
        if (d->skin && d->skin->isValid())
        {
            const QPixmap *notePix = d->skin->getNotePixmap(2);
            if (notePix && !notePix->isNull())
            {
                double scale = d->skin->getNoteScale(2);
                int scaledW = qMax(1, qRound(notePix->width() * scale));
                int scaledH = qMax(1, qRound(notePix->height() * scale));
                QPixmap scaled = notePix->scaled(scaledW, scaledH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter.drawPixmap((128 - scaled.width()) / 2, (128 - scaled.height()) / 2, scaled);
            }
            else
            {
                painter.setBrush(Qt::lightGray);
                painter.setPen(Qt::NoPen);
                painter.drawEllipse((128 - sz) / 2, (128 - sz) / 2, sz, sz);
            }
        }
        else
        {
            painter.setBrush(Qt::lightGray);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse((128 - sz) / 2, (128 - sz) / 2, sz, sz);
        }
        previewLabel->setPixmap(pix);
    };
    updatePreview();
    connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), updatePreview);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() == QDialog::Accepted)
    {
        int newSize = sizeSpin->value();
        Settings::instance().setNoteSize(newSize);
        d->canvas->setNoteSize(newSize);
        if (d->previewWidget)
            d->previewWidget->setNoteSize(newSize);
        Logger::info(QString("Note size set to %1").arg(newSize));
    }
}

void MainWindow::calibrateSkin()
{
    if (!d->skin)
    {
        QMessageBox::information(this, tr("No Skin"), tr("No skin loaded, cannot calibrate."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Calibrate Skin: %1").arg(d->skin->title()));
    dialog.setMinimumSize(600, 400);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QTabWidget *tabs = new QTabWidget;
    QStringList typeNames = {"1/1", "1/2", "1/4", "1/8/16/32", "1/3/6/12/24", "Rain"};
    for (int i = 0; i <= 5; ++i)
    {
        QWidget *page = new QWidget;
        QVBoxLayout *pageLayout = new QVBoxLayout(page);

        QHBoxLayout *scaleLayout = new QHBoxLayout;
        QLabel *scaleLabel = new QLabel(tr("Scale Factor:"));
        QDoubleSpinBox *scaleSpin = new QDoubleSpinBox;
        scaleSpin->setRange(0.2, 3.0);
        scaleSpin->setSingleStep(0.05);
        scaleSpin->setValue(d->skin->getNoteScale(i));
        scaleLayout->addWidget(scaleLabel);
        scaleLayout->addWidget(scaleSpin);
        pageLayout->addLayout(scaleLayout);

        QLabel *previewLabel = new QLabel;
        previewLabel->setFixedSize(128, 128);
        previewLabel->setStyleSheet("border: 1px solid gray; background: white;");
        previewLabel->setAlignment(Qt::AlignCenter);
        pageLayout->addWidget(previewLabel, 0, Qt::AlignCenter);

        auto updatePreview = [previewLabel, scaleSpin, this, i]()
        {
            double scale = scaleSpin->value();
            QPixmap pix(128, 128);
            pix.fill(Qt::white);
            QPainter painter(&pix);
            const QPixmap *notePix = d->skin->getNotePixmap(i);
            if (notePix && !notePix->isNull())
            {
                int scaledW = notePix->width() * scale;
                int scaledH = notePix->height() * scale;
                QPixmap scaled = notePix->scaled(scaledW, scaledH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter.drawPixmap((128 - scaled.width()) / 2, (128 - scaled.height()) / 2, scaled);
            }
            else
            {
                painter.setBrush(Qt::lightGray);
                painter.drawEllipse(20, 20, 88, 88);
            }
            previewLabel->setPixmap(pix);
        };
        updatePreview();
        connect(scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), updatePreview);

        pageLayout->addStretch();
        tabs->addTab(page, typeNames[i]);

        scaleSpin->setProperty("noteType", i);
        page->setProperty("scaleSpin", QVariant::fromValue(scaleSpin));
    }
    layout->addWidget(tabs);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
            {
        for (int i = 0; i <= 5; ++i) {
            QWidget* page = tabs->widget(i);
            QDoubleSpinBox* scaleSpin = page->property("scaleSpin").value<QDoubleSpinBox*>();
            if (scaleSpin) {
                d->skin->setNoteScale(i, scaleSpin->value());
            }
        }
        d->skin->saveConfig();
        d->canvas->update();
        Logger::info("Skin calibration saved");
        dialog.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::configureOutline()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Outline Settings"));
    QFormLayout form(&dialog);

    QSpinBox *widthSpin = new QSpinBox;
    widthSpin->setRange(1, 8);
    widthSpin->setValue(Settings::instance().outlineWidth());
    form.addRow(tr("Outline Width (px):"), widthSpin);

    QPushButton *colorBtn = new QPushButton;
    QColor outlineColor = Settings::instance().outlineColor();
    colorBtn->setStyleSheet(QString("background-color: %1").arg(outlineColor.name()));
    connect(colorBtn, &QPushButton::clicked, [&]()
            {
        QColor newColor = QColorDialog::getColor(outlineColor, &dialog);
        if (newColor.isValid()) {
            outlineColor = newColor;
            colorBtn->setStyleSheet(QString("background-color: %1").arg(outlineColor.name()));
        } });
    form.addRow(tr("Outline Color:"), colorBtn);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(buttons);

    if (dialog.exec() == QDialog::Accepted)
    {
        Settings::instance().setOutlineWidth(widthSpin->value());
        Settings::instance().setOutlineColor(outlineColor);
        d->canvas->update();
        Logger::info("Outline settings updated");
    }
}

void MainWindow::adjustNoteSoundVolume()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Note Sound Volume"));
    dialog.setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *valueLabel = new QLabel;
    valueLabel->setAlignment(Qt::AlignCenter);
    QSlider *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 200);
    slider->setValue(Settings::instance().noteSoundVolume());
    slider->setFixedSize(420, 26);
    slider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "height: 10px;"
        "border: 1px solid #666;"
        "background: #d8d8d8;"
        "border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "background: #444;"
        "width: 18px;"
        "margin: -5px 0;"
        "border-radius: 2px;"
        "}");

    auto refreshLabel = [valueLabel, slider]()
    { valueLabel->setText(QObject::tr("Volume: %1%").arg(slider->value())); };
    refreshLabel();
    connect(slider, &QSlider::valueChanged, this, [this, refreshLabel, slider]()
            {
        refreshLabel();
        if (d->canvas)
            d->canvas->setNoteSoundVolume(slider->value()); });

    layout->addWidget(valueLabel);
    layout->addWidget(slider, 0, Qt::AlignCenter);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const int originalVolume = Settings::instance().noteSoundVolume();
    if (dialog.exec() == QDialog::Accepted)
    {
        Settings::instance().setNoteSoundVolume(slider->value());
        Logger::info(QString("Note sound volume set to %1%").arg(slider->value()));
    }
    else if (d->canvas)
    {
        d->canvas->setNoteSoundVolume(originalVolume);
    }
}
