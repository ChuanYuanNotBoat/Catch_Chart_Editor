#include "MainWindow.h"
#include "MainWindowPrivate.h"
#include "controller/CommandRouter.h"
#include "review/ReviewWorkspace.h"
#include "ui/NoteEditPanel.h"
#include "ui/BPMTimePanel.h"
#include "ui/MetaEditPanel.h"
#include "ui/LeftPanel.h"
#include "ui/WorkbenchLayout.h"
#include "ui/PaneContainer.h"
#include "utils/Settings.h"

#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

void MainWindow::configureClassicSidebars()
{
    if (!d->reviewWorkspace)
        return;
    if (d->mainToolBar)
        d->mainToolBar->show();
    if (d->floatingToolWindowsEnabled || !d->workbenchLayout)
        return;

    auto *workbench = d->workbenchLayout;
    d->reviewWorkspace->setClassicWorkbench(workbench);
    const auto buildHeader = [this, workbench](PaneContainer *container, bool primary) {
        if (primary)
            d->leftPanel->setNavigationControlsHost(nullptr);
        container->setHeaderWidget(nullptr);
        auto *header = new QWidget(container);
        header->setObjectName("classicSidebarHeader");
        auto *layout = new QVBoxLayout(header);
        layout->setContentsMargins(10, 10, 10, 8);
        layout->setSpacing(8);
        auto *title = new QLabel(primary ? tr("Workspace") : tr("Editor"), header);
        title->setObjectName("classicSidebarTitle");
        layout->addWidget(title);

        if (primary)
        {
            auto *commands = new QGridLayout;
            commands->setSpacing(6);
            const QList<QPair<QString, QString>> entries = {
                {QStringLiteral("file.open_chart"), tr("Open chart")},
                {QStringLiteral("file.save"), tr("Save chart")},
                {QStringLiteral("review.openProject"), tr("Open project")},
                {QStringLiteral("review.saveProject"), tr("Save project")},
                {QStringLiteral("file.merge_charts"), tr("Merge charts…")}};
            for (int i = 0; i < entries.size(); ++i)
            {
                auto *button = new QToolButton(header);
                button->setObjectName("classic." + entries[i].first);
                button->setToolButtonStyle(Qt::ToolButtonTextOnly);
                button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                button->setDefaultAction(d->commandRouter->action(entries[i].first));
                button->setText(entries[i].second);
                commands->addWidget(button, i / 2, i % 2, 1, i == 4 ? 2 : 1);
            }
            layout->addLayout(commands);
            d->leftPanel->setNavigationControlsHost(header);
        }

        auto *pages = new QGridLayout;
        pages->setSpacing(4);
        const QList<QPair<QString, QString>> entries = primary
            ? QList<QPair<QString, QString>>{{"navigation", tr("Navigation")}, {"layers", tr("Layers")}}
            : QList<QPair<QString, QString>>{{"note", tr("Note")}, {"bpm", tr("BPM")}, {"meta", tr("Meta")},
                                             {"comments", tr("Comments")}, {"review", tr("Review")}};
        for (int i = 0; i < entries.size(); ++i)
        {
            const auto id = entries[i].first;
            auto *button = new QToolButton(header);
            button->setObjectName("classic.page." + id);
            button->setText(entries[i].second);
            button->setCheckable(true);
            button->setProperty("sidebarPage", true);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            pages->addWidget(button, i / (primary ? 2 : 3), i % (primary ? 2 : 3));
            connect(button, &QToolButton::clicked, header, [this, workbench, id, button] {
                if (id == "note")
                    showEditorPanel(d->notePanel);
                else if (id == "bpm")
                    showEditorPanel(d->bpmPanel);
                else if (id == "meta")
                    showEditorPanel(d->metaPanel);
                else
                    workbench->setPaneVisible(id, true);
                const auto *host = workbench->paneContainerForPane(id);
                const QSignalBlocker block(button);
                button->setChecked(host && host->paneVisible(id));
            });
            const auto sync = [this, workbench, button, id, primary] {
                auto *host = workbench->paneContainerForPane(id);
                const QSignalBlocker block(button);
                button->setChecked(host && host->paneVisible(id));
                if (!primary && button->isChecked())
                {
                    if (auto *tabs = d->mainToolBar->findChild<QTabBar *>("reviewToolbarTabs"))
                    {
                        const QSignalBlocker tabBlock(tabs);
                        tabs->setCurrentIndex(id == "bpm" ? 1 : id == "meta" ? 2
                                              : id == "comments" || id == "review" ? 4 : 0);
                    }
                }
            };
            for (auto *part : {workbench->primarySidebar(), workbench->auxiliarySidebar(), workbench->bottomPanel(),
                               workbench->previewArea()})
                connect(part, &PaneContainer::stateChanged, header, sync);
            sync();
        }
        if (!primary)
        {
            auto *curve = new QToolButton(header);
            curve->setObjectName("classic.curve");
            curve->setDefaultAction(d->curvePanelAction);
            curve->setToolButtonStyle(Qt::ToolButtonTextOnly);
            curve->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            pages->addWidget(curve, 1, 2);
        }
        layout->addLayout(pages);
        container->setHeaderWidget(header);
    };
    buildHeader(workbench->primarySidebar(), true);
    buildHeader(workbench->auxiliarySidebar(), false);
    workbench->primarySidebar()->setMinimumWidth(240);
    workbench->auxiliarySidebar()->setMinimumWidth(320);
}
