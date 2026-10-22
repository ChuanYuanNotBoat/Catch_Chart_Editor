#include "ReviewWorkspace.h"
#include "CommentModel.h"
#include "controller/ChartController.h"
#include "controller/CommandRouter.h"
#include "controller/SelectionController.h"
#include "document/ExportPipeline.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/review/IdentityDialog.h"
#include "ui/review/NoteInspector.h"
#include "file/ChartIO.h"
#include "utils/MathUtils.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolButton>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <algorithm>

namespace CceReview
{
namespace
{
QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString beatText(const QJsonValue &value)
{
    const auto a = value.toArray();
    return a.size() == 3 ? QString("%1:%2/%3").arg(a[0].toInt()).arg(a[1].toInt()).arg(a[2].toInt()) : QString();
}
QString summary(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return QObject::tr("—");
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? QObject::tr("Yes") : QObject::tr("No");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 12);
    const auto o = value.toObject();
    if (o.contains("beat"))
    {
        QString text = beatText(o.value("beat"));
        if (o.contains("x"))
            text += QString("  X %1").arg(o.value("x").toInt());
        if (o.contains("endbeat"))
            text += " → " + beatText(o.value("endbeat"));
        if (o.contains("bpm"))
            text += QString("  %1 BPM").arg(o.value("bpm").toDouble());
        if (o.contains("sound"))
            text += "  " + o.value("sound").toString();
        return text;
    }
    if (value.isArray())
        return QObject::tr("%1 items").arg(value.toArray().size());
    return QObject::tr("%1 properties").arg(o.size());
}
QJsonObject eventRecord(const QString &type, const QJsonObject &author, const QJsonObject &detail = {})
{
    return {{"eventId", uuid()},
            {"type", type},
            {"author", author},
            {"detail", detail},
            {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
}
QStringList chooseItems(QWidget *parent, const QString &title, const QStringList &labels, const QStringList &ids)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    QVBoxLayout layout(&dialog);
    QListWidget list;
    list.setSelectionMode(QAbstractItemView::ExtendedSelection);
    list.addItems(labels);
    layout.addWidget(&list);
    list.selectAll();
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    QStringList selected;
    for (auto *item : list.selectedItems())
        selected.append(ids.value(list.row(item)));
    return selected;
}
const QStringList metaFields = {"title",     "titleOrg",       "artist",      "artistOrg", "difficulty", "chartAuthor",
                                "audioFile", "backgroundFile", "previewTime", "firstBpm",  "offset",     "speed"};
QString fieldLabel(const QString &field)
{
    const QStringList labels = {QObject::tr("Title"),           QObject::tr("Original title"),
                                QObject::tr("Artist"),          QObject::tr("Original artist"),
                                QObject::tr("Difficulty name"), QObject::tr("Creator"),
                                QObject::tr("Audio reference"), QObject::tr("Background reference"),
                                QObject::tr("Preview time"),    QObject::tr("First BPM"),
                                QObject::tr("Offset"),          QObject::tr("Fall speed")};
    const int index = metaFields.indexOf(field);
    return index < 0 ? field : labels[index];
}
} // namespace

void ReviewWorkspace::installMenus(CommandRouter *router, QToolBar *toolbar)
{
    if (auto *existing = m_window->findChild<QMenu *>("localReviewMenu"))
        delete existing;
    m_peeking = false;
    auto *menu = m_window->menuBar()->addMenu(tr("Project & Review"));
    menu->setObjectName("localReviewMenu");
    auto action = [&](const QString &text, const QString &id, const QKeySequence &key, auto callback) {
        auto *a = menu->addAction(text);
        a->setObjectName(id);
        a->setAutoRepeat(false);
        connect(a, &QAction::triggered, this, callback);
        router->registerAction(a, id, key,
                               id == "review.peekBase" || id == "review.addComment" ? CommandRouter::Scope::Editor
                                                                                    : CommandRouter::Scope::Window);
        return a;
    };
    auto *reviewEntry = action(tr("Layers, Comments and Review… (Experimental)"), "review.workspace", {}, [this] {
        showPanelPage(1);
    });
    reviewEntry->setIconText(tr("Review"));
    auto *commentEntry = action(tr("Add Comment…"), "review.addComment", QKeySequence("Ctrl+Alt+M"), [this] {
        addCommentDialog();
    });
    commentEntry->setIconText(tr("Comment"));
    if (toolbar)
        installToolbar(toolbar, router, reviewEntry);
    action(tr("Local Identities…"), "review.identities", {}, [this] {
        CceReviewUi::IdentityDialog(&m_identity, m_window).exec();
    });
    menu->addSeparator();
    action(tr("Open CCE Project…"), "review.openProject", {}, [this] {
        const QString path =
            QFileDialog::getOpenFileName(m_window, tr("Open project"), {}, tr("CCE Projects (*.cceproj)"));
        if (path.isEmpty() || !confirmClose())
            return;
        QString error;
        if (!openProject(path, &error))
            report(error);
        else
            showPanel();
    });
    action(tr("Save Project…"), "review.saveProject", {}, [this] {
        saveProjectDialog();
    });
    action(tr("Export Difficulty as .mc…"), "review.exportChart", {}, [this] {
        exportChartDialog(false);
    });
    action(tr("Export Selected Layer as .mc…"), "review.exportLayer", {}, [this] {
        exportChartDialog(true);
    });
    menu->addSeparator();
    action(tr("Create Proposal…"), "review.start", {}, [this] {
        bool ok;
        const QString text =
            QInputDialog::getText(m_window, tr("Create proposal"), tr("Description:"), QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        QString error;
        if (!startProposal(text, &error))
            report(error);
        else
            showPanel();
    });
    action(tr("Import .ccepr…"), "review.import", {}, [this] {
        const QString path =
            QFileDialog::getOpenFileName(m_window, tr("Import proposal"), {}, tr("CCE Review Packages (*.ccepr)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!importProposal(path, &error))
            report(error);
        else
        {
            showPanel();
            setViewMode(1);
        }
    });
    action(tr("Export .ccepr…"), "review.export", {}, [this] {
        const QString path =
            QFileDialog::getSaveFileName(m_window, tr("Export proposal"), {}, tr("CCE Review Packages (*.ccepr)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!exportProposal(path, &error))
            report(error);
    });
    action(tr("Edit Proposal"), "review.editProposal", {}, [this] {
        editProposal();
    });
    auto *peek = action(tr("Peek Base (hold)"), "review.peekBase", QKeySequence(Qt::Key_Backslash), [this] {
        if (!m_revision.isEmpty())
        {
            m_peeking = true;
            setViewMode(m_viewMode);
        }
    });
    Q_UNUSED(peek);
    connect(router, &CommandRouter::commandReleased, menu, [this](const QString &id) {
        if (id == "review.peekBase" && m_peeking)
        {
            m_peeking = false;
            setViewMode(m_viewMode);
        }
    });
    action(tr("Previous Comment"), "review.previousComment", {}, [this] {
        navigate(0, -1);
    });
    action(tr("Next Comment"), "review.nextComment", {}, [this] {
        navigate(0, 1);
    });
    action(tr("Previous Change"), "review.previousChange", {}, [this] {
        navigate(1, -1);
    });
    action(tr("Next Change"), "review.nextChange", {}, [this] {
        navigate(1, 1);
    });
    action(tr("Previous Conflict"), "review.previousConflict", {}, [this] {
        navigate(2, -1);
    });
    action(tr("Next Conflict"), "review.nextConflict", {}, [this] {
        navigate(2, 1);
    });
    const QString recovery =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/local-review/recovery.cceproj";
    action(tr("Recover Local Review Workspace…"), "review.recover", {}, [this, recovery] {
        if (!confirmClose())
            return;
        QString error;
        if (!openProject(recovery, &error))
            report(error);
        else
        {
            m_projectPath.clear();
            m_dirty = true;
            showPanel();
        }
    });
}

void ReviewWorkspace::buildUi()
{
    if (m_panel)
        return;
    auto *dialog = new QDialog(m_window, Qt::Window);
    m_panel = dialog;
    dialog->setObjectName("reviewWorkspaceWindow");
    dialog->setWindowTitle(tr("Review & Layers (Experimental)"));
    dialog->resize(1100, 720);
    auto *layout = new QVBoxLayout(dialog);
    auto *experimental = new QLabel(tr(
        "Experimental: layers and .ccepr review. Use a chart copy and verify the result before replacing your chart."));
    experimental->setObjectName("reviewExperimentalNotice");
    experimental->setWordWrap(true);
    layout->addWidget(experimental);
    auto *top = new QHBoxLayout;
    m_identityLabel = new QLabel;
    auto *identity = new QPushButton(tr("Local Identities…"));
    top->addWidget(m_identityLabel, 1);
    top->addWidget(identity);
    connect(identity, &QPushButton::clicked, this, [this] {
        CceReviewUi::IdentityDialog(&m_identity, m_panel).exec();
    });
    m_viewBox = new QComboBox;
    m_viewBox->addItems({tr("Proposal Only"), tr("Diff Overlay"), tr("Base · Read Only")});
    m_viewBox->setObjectName("reviewViewMode");
    top->addWidget(m_viewBox);
    connect(m_viewBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_refreshing)
            setViewMode(i);
    });
    auto *peek = new QPushButton(tr("Peek Base"));
    top->addWidget(peek);
    connect(peek, &QPushButton::pressed, this, [this] {
        m_peeking = true;
        setViewMode(m_viewMode);
    });
    connect(peek, &QPushButton::released, this, [this] {
        m_peeking = false;
        setViewMode(m_viewMode);
    });
    layout->addLayout(top);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs, 1);
    auto *project = new QWidget;
    auto *pl = new QVBoxLayout(project);
    auto *difficultyBar = new QHBoxLayout;
    m_difficultyBox = new QComboBox;
    m_difficultyBox->setObjectName("reviewDifficulty");
    difficultyBar->addWidget(m_difficultyBox, 1);
    auto button = [this](QHBoxLayout *bar, const QString &text, auto callback) {
        auto *b = new QPushButton(text);
        bar->addWidget(b);
        connect(b, &QPushButton::clicked, this, callback);
        return b;
    };
    button(difficultyBar, tr("New"), [this] {
        changeDifficulty(0);
    });
    button(difficultyBar, tr("Rename"), [this] {
        changeDifficulty(1);
    });
    button(difficultyBar, tr("Duplicate"), [this] {
        changeDifficulty(2);
    });
    button(difficultyBar, tr("Import .mc…"), [this] {
        importDifficulty();
    });
    pl->addLayout(difficultyBar);
    connect(m_difficultyBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_refreshing || i < 0)
            return;
        auto after = m_bundle;
        after.project.activeDifficultyId = m_difficultyBox->itemData(i).toString();
        QString error;
        if (!applyBundle(tr("Switch difficulty"), after, &error))
            report(error);
    });
    m_layersTree = new QTreeWidget;
    m_layersTree->setObjectName("reviewLayers");
    m_layersTree->setHeaderLabels(
        {tr("Layer"), tr("Visible"), tr("Locked"), tr("Export"), tr("Opacity %"), tr("Shared by")});
    m_layersTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    pl->addWidget(m_layersTree, 1);
    connect(m_layersTree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (!m_refreshing)
            m_activeLayerId = selectedLayerId();
    });
    connect(m_layersTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_refreshing || column == 0 || column == 5)
            return;
        const QString projectId = m_bundle.project.projectId;
        const QString difficultyId = m_bundle.project.activeDifficultyId;
        const QString layerId = item->data(0, Qt::UserRole).toString();
        bool valid = true;
        const double value =
            column == 4 ? item->text(4).toDouble(&valid) / 100. : item->checkState(column) == Qt::Checked;
        valid = valid && value >= 0 && value <= 1;
        // setData/checkbox delegates still use the item after emitting this
        // signal. Commit only after that stack unwinds, using values, not items.
        QTimer::singleShot(0, this, [this, projectId, difficultyId, layerId, column, value, valid] {
            if (m_bundle.project.projectId != projectId || !m_bundle.project.difficulties.contains(difficultyId))
                return;
            if (!valid)
            {
                refreshUi();
                return;
            }
            auto after = m_bundle;
            auto &bindings = after.project.difficulties[difficultyId].bindings;
            for (auto &binding : bindings)
                if (binding.layerId == layerId)
                {
                    if (column == 1)
                        binding.visible = value;
                    else if (column == 2)
                        binding.locked = value;
                    else if (column == 3)
                        binding.exportEnabled = value;
                    else
                        binding.opacity = value;
                    QString error;
                    if (!applyBundle(tr("Update layer binding"), after, &error))
                        report(error);
                    return;
                }
        });
    });
    auto *layersBar = new QHBoxLayout;
    auto *layerOrderBar = new QHBoxLayout;
    const QStringList layerButtons = {
        tr("New Layer"), tr("Rename"), tr("Share Existing…"), tr("Make Independent"), tr("Unbind"),
        tr("Up"),        tr("Down")};
    for (int i = 0; i < layerButtons.size(); ++i)
        button(i < 4 ? layersBar : layerOrderBar, layerButtons[i], [this, i] {
            changeLayer(i);
        });
    pl->addLayout(layersBar);
    pl->addLayout(layerOrderBar);
    auto *transfer = new QHBoxLayout;
    button(transfer, tr("Move Selection into Layer"), [this] {
        changeLayer(7);
    });
    button(transfer, tr("Copy Selection into Layer"), [this] {
        changeLayer(8);
    });
    button(transfer, tr("Export Layer…"), [this] {
        exportChartDialog(true);
    });
    pl->addLayout(transfer);
    pl->addWidget(new QLabel(tr("Live references (read only; excluded from statistics and export)")));
    m_referencesList = new QListWidget;
    m_referencesList->setMaximumHeight(100);
    pl->addWidget(m_referencesList);
    auto *refs = new QHBoxLayout;
    button(refs, tr("Add Reference…"), [this] {
        addReference();
    });
    button(refs, tr("Remove Reference"), [this] {
        const int row = m_referencesList->currentRow();
        if (row < 0)
            return;
        auto after = m_bundle;
        auto &list = after.project.difficulties[after.project.activeDifficultyId].references;
        if (row < list.size())
            list.removeAt(row);
        QString error;
        if (!applyBundle(tr("Remove reference"), after, &error))
            report(error);
    });
    pl->addLayout(refs);
    tabs->addTab(project, tr("Layers & Difficulties"));

    auto *changes = new QWidget;
    auto *cl = new QVBoxLayout(changes);
    auto *proposalFiles = new QHBoxLayout;
    const QVector<QPair<QString, QString>> proposalCommands = {{"review.start", tr("Create Proposal…")},
                                                               {"review.import", tr("Import .ccepr…")},
                                                               {"review.export", tr("Export .ccepr…")}};
    for (const auto &command : proposalCommands)
    {
        auto *action = m_window->findChild<QAction *>(command.first);
        auto *entry = button(proposalFiles, command.second, [this, id = command.first] {
            if (auto *current = m_window->findChild<QAction *>(id))
                current->trigger();
        });
        entry->setObjectName(command.first + "Button");
        entry->setEnabled(action);
    }
    cl->addLayout(proposalFiles);
    auto *reviewBar = new QHBoxLayout;
    button(reviewBar, tr("Edit Proposal"), [this] {
        editProposal();
    });
    button(reviewBar, tr("Save Proposal as Difficulty"), [this] {
        QString error;
        if (!saveProposalAsDifficulty(&error))
            report(error);
    });
    button(reviewBar, tr("Rebase"), [this] {
        QString error;
        if (!rebase(&error))
            report(error);
    });
    m_filterBox = new QComboBox;
    m_filterBox->addItems({tr("All Changes"), "Add", "Remove", "Move", "Modify", tr("Conflicts")});
    reviewBar->addWidget(m_filterBox);
    cl->addLayout(reviewBar);
    connect(m_filterBox, &QComboBox::currentIndexChanged, this, &ReviewWorkspace::refreshChanges);
    m_changesTree = new QTreeWidget;
    m_changesTree->setObjectName("reviewChanges");
    m_changesTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_changesTree->setHeaderLabels(
        {tr("Data"), tr("Change"), tr("Before"), tr("After"), tr("Decision"), tr("Apply state")});
    for (int i = 0; i < 4; ++i)
        m_changesTree->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    cl->addWidget(m_changesTree, 1);
    connect(m_changesTree, &QTreeWidget::itemSelectionChanged, this, &ReviewWorkspace::selectChange);
    auto *decisions = new QHBoxLayout;
    auto *resolutionBar = new QHBoxLayout;
    auto *groupReview = button(decisions, tr("Select Dependency Group"), [this] {
        QSet<QString> selected;
        for (auto *item : m_changesTree->selectedItems())
            selected.insert(item->data(0, Qt::UserRole).toString());
        const auto expanded = dependencySelection(m_changes, selected);
        if (m_filterBox)
            m_filterBox->setCurrentIndex(0);
        for (int i = 0; i < m_changesTree->topLevelItemCount(); ++i)
        {
            auto *item = m_changesTree->topLevelItem(i);
            item->setSelected(expanded.contains(item->data(0, Qt::UserRole).toString()));
        }
    });
    groupReview->setObjectName("reviewSelectDependencyGroup");
    button(decisions, tr("Accept"), [this] {
        setDecision("Accepted");
    });
    button(decisions, tr("Reject"), [this] {
        setDecision("Rejected");
    });
    button(decisions, tr("Pending"), [this] {
        setDecision("Pending");
    });
    button(resolutionBar, tr("Link as Move"), [this] {
        linkAsMove();
    });
    button(resolutionBar, tr("Use Proposal"), [this] {
        resolveSelected(true);
    });
    button(resolutionBar, tr("Keep Current"), [this] {
        resolveSelected(false);
    });
    cl->addLayout(decisions);
    cl->addLayout(resolutionBar);
    auto *applyBar = new QHBoxLayout;
    auto *navigationBar = new QHBoxLayout;
    button(applyBar, tr("Apply Selected"), [this] {
        QSet<QString> ids;
        for (auto *item : m_changesTree->selectedItems())
            ids.insert(item->data(0, Qt::UserRole).toString());
        QString error;
        if (!applyChanges(ids, &error))
            report(error);
    });
    button(applyBar, tr("Apply Accepted"), [this] {
        const auto ids = acceptedChangeIds();
        QString error;
        if (!applyChanges(ids, &error))
            report(error);
    });
    button(navigationBar, tr("Previous Change"), [this] {
        navigate(1, -1);
    });
    button(navigationBar, tr("Next Change"), [this] {
        navigate(1, 1);
    });
    button(navigationBar, tr("Next Conflict"), [this] {
        navigate(2, 1);
    });
    cl->addLayout(applyBar);
    cl->addLayout(navigationBar);
    tabs->addTab(changes, tr("Diff & Conflict"));

    auto *commentPage = new QWidget;
    auto *col = new QVBoxLayout(commentPage);
    m_commentFilterBox = new QComboBox(commentPage);
    m_commentFilterBox->setObjectName("reviewCommentFilter");
    m_commentFilterBox->addItem(tr("All comments"), QStringLiteral("All"));
    m_commentFilterBox->addItem(tr("Unresolved"), QStringLiteral("Unresolved"));
    m_commentFilterBox->addItem(tr("Resolved"), QStringLiteral("Resolved"));
    col->addWidget(m_commentFilterBox);
    m_commentsTree = new QTreeWidget;
    m_commentsTree->setObjectName("reviewComments");
    m_commentsTree->setHeaderLabels({tr("Author"), tr("Comment"), tr("State"), tr("Anchor"), tr("Reference status")});
    m_commentsTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    col->addWidget(m_commentsTree, 1);
    connect(m_commentFilterBox, &QComboBox::currentIndexChanged, this, &ReviewWorkspace::refreshComments);
    connect(m_commentsTree, &QTreeWidget::itemDoubleClicked, this, [this] {
        editComment(false);
    });
    auto *anchorType = new QComboBox;
    anchorType->addItems({"TimePoint", "TimePosition", "Region", "Note", "NoteGroup", "BpmEntry", "BpmList"});
    auto *newComment = new QHBoxLayout;
    newComment->addWidget(anchorType);
    button(newComment, tr("Add Comment…"), [this, anchorType] {
        bool ok;
        const QString body = QInputDialog::getMultiLineText(m_panel, tr("Add comment"), tr("Comment:"), {}, &ok);
        if (!ok)
            return;
        QString error;
        if (!addCommentAtSelection(body, anchorType->currentText(), &error))
            report(error);
    });
    button(newComment, tr("Reply…"), [this] {
        const QString id = selectedCommentId();
        if (id.isEmpty())
            return;
        QJsonObject parent;
        for (const auto &v : comments())
            if (v.toObject().value("commentId") == id)
                parent = v.toObject();
        bool ok;
        QString body = QInputDialog::getMultiLineText(m_panel, tr("Reply"), tr("Reply:"), {}, &ok);
        if (!ok)
            return;
        auto list = comments();
        QString error;
        if (!addComment(list, m_identity.activeIdentity().snapshot(), body, parent.value("anchor").toObject(),
                        parent.value("threadId").toString(), nullptr, &error))
        {
            report(error);
            return;
        }
        auto log = events();
        log.append(eventRecord("CreateComment", m_identity.activeIdentity().snapshot()));
        commitReviewState(tr("Reply to comment"), list, log);
    });
    col->addLayout(newComment);
    auto *commentBar = new QHBoxLayout;
    button(commentBar, tr("Edit…"), [this] {
        editComment(false);
    });
    button(commentBar, tr("Delete"), [this] {
        editComment(true);
    });
    button(commentBar, tr("Resolve"), [this] {
        setResolved(true);
    });
    button(commentBar, tr("Reopen"), [this] {
        setResolved(false);
    });
    button(commentBar, tr("Previous"), [this] {
        navigate(0, -1);
    });
    button(commentBar, tr("Next"), [this] {
        navigate(0, 1);
    });
    col->addLayout(commentBar);
    auto *displayBar = new QHBoxLayout;
    const QVector<QPair<QString, QString>> displayStates = {{QStringLiteral("Expanded"), tr("Expanded")},
                                                            {QStringLiteral("Collapsed"), tr("Collapsed")},
                                                            {QStringLiteral("Hidden"), tr("Hidden")}};
    for (const auto &displayState : displayStates)
    {
        const QString state = displayState.first;
        button(displayBar, displayState.second, [this, state] {
            const QString id = selectedCommentId();
            for (const auto &v : comments())
            {
                const auto o = v.toObject();
                if (o.value("commentId") != id)
                    continue;
                QSettings().setValue("local-review/comments/" + o.value("threadId").toString() + "/display", state);
            }
            rebuildCommentCards();
            if (m_canvas)
                m_canvas->update();
        });
    }
    col->addLayout(displayBar);
    tabs->addTab(commentPage, tr("Comments"));
    auto *exportPage = new QScrollArea;
    exportPage->setWidgetResizable(true);
    exportPage->setWidget(createExportConfiguration(exportPage));
    tabs->addTab(exportPage, tr("Export Metadata"));
    m_eventsList = new QListWidget;
    tabs->addTab(m_eventsList, tr("Event Log"));
    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);
    refreshUi();
}

void ReviewWorkspace::showPanel()
{
    buildUi();
    refreshUi();
    m_panel->show();
    m_panel->raise();
    m_panel->activateWindow();
}

void ReviewWorkspace::showComments()
{
    showPanelPage(2);
}

void ReviewWorkspace::showPanelPage(int page)
{
    showPanel();
    if (auto *tabs = m_panel->findChild<QTabWidget *>())
        tabs->setCurrentIndex(page);
}

void ReviewWorkspace::addCommentDialog(const QPointF &position)
{
    if (m_identity.activeIdentity().authorId.isEmpty())
    {
        CceReviewUi::IdentityDialog(&m_identity, m_window).exec();
        if (m_identity.activeIdentity().authorId.isEmpty())
            return;
    }
    bool ok = false;
    const auto body = QInputDialog::getMultiLineText(m_window, tr("Add comment"), tr("Comment:"), {}, &ok);
    if (!ok)
        return;
    const auto count = m_selection ? m_selection->selectedIndices().size() : 0;
    const QString type = position.x() >= 0 ? "TimePosition"
                         : count == 1      ? "Note"
                         : count > 1       ? "NoteGroup"
                                           : "TimePoint";
    QString error;
    if (!addCommentAtSelection(body, type, &error, position))
        report(error);
    else
        showComments();
}
QString ReviewWorkspace::selectedLayerId() const
{
    if (m_layersTree && m_layersTree->currentItem())
        return m_layersTree->currentItem()->data(0, Qt::UserRole).toString();
    return m_activeLayerId;
}
QString ReviewWorkspace::selectedCommentId() const
{
    return m_commentsTree && m_commentsTree->currentItem()
               ? m_commentsTree->currentItem()->data(0, Qt::UserRole).toString()
               : QString();
}

void ReviewWorkspace::refreshUi()
{
    refreshToolbar();
    updateExportFields();
    if (!m_panel || m_refreshing)
        return;
    m_refreshing = true;
    m_identityLabel->setText(
        tr("Author: %1").arg(activeAuthorName().isEmpty() ? tr("No identity selected") : activeAuthorName()));
    m_difficultyBox->clear();
    for (auto it = m_bundle.project.difficulties.cbegin(); it != m_bundle.project.difficulties.cend(); ++it)
        m_difficultyBox->addItem(it->name, it.key());
    m_difficultyBox->setCurrentIndex(m_difficultyBox->findData(m_bundle.project.activeDifficultyId));
    m_viewBox->setCurrentIndex(m_viewMode);
    m_viewBox->setEnabled(!m_revision.isEmpty());
    m_layersTree->clear();
    const auto d = m_bundle.project.difficulties.value(m_bundle.project.activeDifficultyId);
    auto bindings = d.bindings;
    std::stable_sort(bindings.begin(), bindings.end(), [](const auto &a, const auto &b) {
        return a.order < b.order;
    });
    for (const auto &b : bindings)
    {
        auto *item = new QTreeWidgetItem(m_layersTree);
        item->setText(0, m_bundle.project.layers.value(b.layerId).name);
        item->setData(0, Qt::UserRole, b.layerId);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setCheckState(1, b.visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(2, b.locked ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(3, b.exportEnabled ? Qt::Checked : Qt::Unchecked);
        item->setText(4, QString::number(qRound(b.opacity * 100)));
        item->setText(5, QString::number(m_bundle.project.bindingCount(b.layerId)));
        if (b.layerId == m_activeLayerId)
            m_layersTree->setCurrentItem(item);
    }
    if (!m_layersTree->currentItem() && m_layersTree->topLevelItemCount())
    {
        m_layersTree->setCurrentItem(m_layersTree->topLevelItem(0));
        m_activeLayerId = selectedLayerId();
    }
    m_referencesList->clear();
    for (const auto &ref : d.references)
        m_referencesList->addItem(
            m_bundle.project.difficulties.value(ref.difficultyId).name
            + (ref.layerIds.isEmpty() ? tr(" · whole difficulty") : tr(" · %1 layers").arg(ref.layerIds.size())));
    m_eventsList->clear();
    for (const auto &v : events())
    {
        const auto e = v.toObject();
        m_eventsList->addItem(e.value("timestamp").toString() + "  " + e.value("type").toString() + "  "
                              + e.value("author").toObject().value("authorNameSnapshot").toString());
    }
    m_refreshing = false;
    refreshComments();
    refreshChanges();
}

void ReviewWorkspace::refreshChanges()
{
    if (!m_changesTree)
        return;
    const QString selection =
        m_changesTree->currentItem() ? m_changesTree->currentItem()->data(0, Qt::UserRole).toString() : QString();
    const QSignalBlocker block(m_changesTree);
    m_changesTree->clear();
    const auto target = m_editingProposal ? m_targetBundle : m_bundle;
    const auto result = merge(m_revision.value("baseSnapshot").toObject(), snapshot(target), m_changes);
    QHash<QString, QString> states;
    QHash<QString, QJsonObject> decisions;
    for (const auto &item : result.items)
        states[item.change.changeId] = item.state;
    for (const auto &v : m_bundle.review.value("reviewDecisions").toArray())
    {
        const auto d = v.toObject();
        decisions[d.value("changeId").toString()] = d;
    }
    const int filter = m_filterBox ? m_filterBox->currentIndex() : 0;
    const QStringList kinds = {"", "Add", "Remove", "Move", "Modify"};
    for (const auto &c : m_changes)
    {
        const QString state = states.value(c.changeId, "Pending");
        if (filter > 0 && filter < 5 && c.kind != kinds[filter])
            continue;
        if (filter == 5 && state != "Conflict" && state != "Invalid")
            continue;
        auto *item = new QTreeWidgetItem(m_changesTree);
        item->setData(0, Qt::UserRole, c.changeId);
        item->setData(0, Qt::UserRole + 1, state);
        QString dataLabel = c.domain;
        QString field = c.path.section('/', -1).replace("~1", "/").replace("~0", "~");
        if (c.domain == "Metadata")
            dataLabel = fieldLabel(field);
        else if (c.domain == "ExportConfiguration")
            dataLabel = tr("Export Metadata");
        else if (c.domain == "Resource")
            dataLabel = c.path.mid(QString("/resources/").size()).replace("~1", "/").replace("~0", "~");
        item->setText(0, dataLabel);
        item->setToolTip(0, c.path);
        item->setText(1, c.kind);
        if (!c.dependencyGroupId.isEmpty() || !c.dependsOn.isEmpty())
            item->setToolTip(1, tr("Select Dependency Group"));
        const auto describe = [&](const QJsonValue &value) {
            if (c.domain == "ExportConfiguration" && value.isArray())
            {
                QStringList labels;
                for (const auto &field : value.toArray())
                    labels.append(fieldLabel(field.toString()));
                return labels.isEmpty() ? QString("—") : labels.join(", ");
            }
            return summary(value);
        };
        item->setText(2, describe(c.before));
        item->setText(3, describe(c.after));
        const auto decision = decisions.value(c.changeId);
        item->setText(4, decisionMatchesChange(decision, c) ? decision.value("decision").toString() : "Pending");
        item->setText(5, state == "Applied" ? tr("Ready to apply") : state);
        const QColor color = state == "Conflict" || state == "Invalid" ? QColor(180, 135, 0)
                             : c.kind == "Remove"                      ? QColor(205, 65, 70)
                                                                       : QColor(40, 155, 90);
        item->setForeground(1, color);
        if (c.changeId == selection)
            m_changesTree->setCurrentItem(item);
    }
    const auto conflicts = m_revision.value("mergeConflicts").toArray();
    for (int i = 0; i < conflicts.size(); ++i)
    {
        const auto conflict = conflicts[i].toObject();
        auto *item = new QTreeWidgetItem(m_changesTree);
        item->setData(0, Qt::UserRole, QString("conflict:%1").arg(i));
        item->setData(0, Qt::UserRole + 1, "Conflict");
        item->setText(0, tr("Revision conflict"));
        item->setText(1, conflict.value("path").toString());
        item->setText(2, summary(conflict.value("local").toObject().value("after")));
        item->setText(3, summary(conflict.value("incoming").toObject().value("after")));
        item->setText(5, tr("Conflict"));
        item->setForeground(0, QColor(180, 135, 0));
    }
}

void ReviewWorkspace::refreshComments()
{
    if (!m_commentsTree)
        return;
    const QString selected = selectedCommentId();
    const QString filter = m_commentFilterBox ? m_commentFilterBox->currentData().toString() : QStringLiteral("All");
    const QSignalBlocker block(m_commentsTree);
    m_commentsTree->clear();
    for (const auto &v : comments())
    {
        const auto c = v.toObject();
        if (c.value("tombstone").toBool())
            continue;
        const QString state = c.value("state").toString();
        if (filter != QStringLiteral("All") && state != filter)
            continue;
        auto *item = new QTreeWidgetItem(m_commentsTree);
        item->setData(0, Qt::UserRole, c.value("commentId"));
        item->setText(0, c.value("author").toObject().value("authorNameSnapshot").toString());
        item->setText(1, c.value("body").toString());
        item->setText(2, state);
        item->setText(3, c.value("anchor").toObject().value("type").toString());
        const auto referenceState = anchorReferenceState(c.value("anchor").toObject());
        const auto reference = referenceState == "Missing"     ? tr("Target missing")
                               : referenceState == "Ambiguous" ? tr("Ambiguous target")
                               : referenceState == "Modified"  ? tr("Target modified")
                                                               : QString();
        item->setText(4, reference);
        if (!reference.isEmpty())
            item->setForeground(4, QColor(210, 155, 45));
        if (c.value("commentId") == selected)
            m_commentsTree->setCurrentItem(item);
    }
}

void ReviewWorkspace::selectChange()
{
    if (!m_changesTree || !m_changesTree->currentItem())
        return;
    const QString id = m_changesTree->currentItem()->data(0, Qt::UserRole).toString();
    for (const auto &c : m_changes)
        if (c.changeId == id)
        {
            Note before, after;
            if (c.domain == "Note" && CceDocument::decodeNote(c.before.toObject(), before)
                && CceDocument::decodeNote(c.after.toObject(), after))
                m_inspector->setComparison({qMakePair(before, after)});
            else
                m_inspector->clearComparison();
            const auto location = c.after.isObject() ? c.after.toObject() : c.before.toObject();
            if (m_canvas && location.value("beat").isArray())
            {
                const auto b = location.value("beat").toArray();
                m_canvas->setScrollPos(MathUtils::beatToMs(b[0].toInt(), b[1].toInt(), b[2].toInt(),
                                                           m_controller->chart()->bpmList(),
                                                           m_controller->chart()->meta().offset));
            }
            if (m_canvas)
                m_canvas->update();
            break;
        }
}

void ReviewWorkspace::navigate(int kind, int direction)
{
    buildUi();
    auto *tree = kind == 0 ? m_commentsTree : m_changesTree;
    const int count = tree->topLevelItemCount();
    if (!count)
        return;
    int index = tree->indexOfTopLevelItem(tree->currentItem());
    for (int i = 0; i < count; ++i)
    {
        index = (index + direction + count) % count;
        auto *item = tree->topLevelItem(index);
        if (kind == 2 && item->data(0, Qt::UserRole + 1).toString() != "Conflict"
            && item->data(0, Qt::UserRole + 1).toString() != "Invalid")
            continue;
        tree->setCurrentItem(item);
        tree->scrollToItem(item);
        if (kind != 0)
            selectChange();
        else
        {
            const QString id = selectedCommentId();
            for (const auto &v : comments())
            {
                const auto c = v.toObject();
                if (c.value("commentId") != id)
                    continue;
                auto a = c.value("anchor").toObject();
                auto beat = a.value("beat").toArray();
                const auto targets = a.value("targets").toArray();
                if (beat.isEmpty() && !targets.isEmpty())
                    beat = targets.first().toObject().value("note").toObject().value("beat").toArray();
                if (m_canvas && beat.size() == 3)
                    m_canvas->setScrollPos(MathUtils::beatToMs(beat[0].toInt(), beat[1].toInt(), beat[2].toInt(),
                                                               m_controller->chart()->bpmList(),
                                                               m_controller->chart()->meta().offset));
            }
        }
        break;
    }
}

bool ReviewWorkspace::addCommentAtSelection(const QString &body, const QString &type, QString *error,
                                            const QPointF &canvasPosition)
{
    QJsonObject anchor{{"type", type}, {"difficultyId", m_bundle.project.activeDifficultyId}};
    QVector<Note> selected;
    const Chart &anchorChart =
        m_controller->isReadOnly() && !m_revision.isEmpty() ? m_presentationChart : *m_controller->chart();
    if (m_selection)
        for (int i : m_selection->selectedIndices())
            if (i >= 0 && i < anchorChart.notes().size())
                selected.append(portableNote(anchorChart.notes()[i]));
    int b = 0, n = 0, d = 1;
    if (m_canvas)
        MathUtils::quantizeBeatToDivision(
            qMax(0.0, m_canvas->chartYToBeat(canvasPosition.y() >= 0 ? canvasPosition.y() : m_canvas->height() * .8)),
            m_canvas->timeDivision(), b, n, d);
    anchor["beat"] = QJsonArray{b, n, d};
    anchor["x"] =
        canvasPosition.x() >= 0 && m_canvas ? qBound(0, m_canvas->chartCanvasXToLaneX(canvasPosition.x()), 512) : 256;
    if (type == "Note" || type == "NoteGroup")
    {
        if (selected.isEmpty())
        {
            if (error)
                *error = tr("Select one or more notes first.");
            return false;
        }
        if (type == "Note" && selected.size() != 1)
        {
            if (error)
                *error = tr("A Note anchor requires exactly one selected note.");
            return false;
        }
        QJsonArray targets;
        for (const auto &note : selected)
            targets.append(QJsonObject{{"layerId", m_owner.value(note.id)},
                                       {"note", CceDocument::encodeNote(note)},
                                       {"originalNote", CceDocument::encodeNote(note)},
                                       {"state", "Valid"}});
        anchor["targets"] = targets;
    }
    else if (type == "Region")
    {
        double start = b + n / double(d), end = start + 1;
        int minX = 0, maxX = 512;
        if (!selected.isEmpty())
        {
            start = selected.first().getStartBeat();
            end = selected.first().getEndBeat();
            minX = maxX = selected.first().x;
            for (const auto &note : selected)
            {
                start = qMin(start, note.getStartBeat());
                end = qMax(end, note.getEndBeat());
                minX = qMin(minX, note.x);
                maxX = qMax(maxX, note.x);
            }
        }
        MathUtils::floatToBeat(start, b, n, d);
        anchor["beat"] = QJsonArray{b, n, d};
        MathUtils::floatToBeat(end, b, n, d);
        anchor["endBeat"] = QJsonArray{b, n, d};
        anchor["x"] = minX;
        anchor["endX"] = maxX;
    }
    else if (type == "BpmEntry")
    {
        const auto list = anchorChart.bpmList();
        QStringList choices;
        for (const auto &point : list)
            choices.append(QString("%1:%2/%3 · %4 BPM")
                               .arg(point.beatNum)
                               .arg(point.numerator)
                               .arg(point.denominator)
                               .arg(point.bpm));
        bool ok;
        const QString choice = QInputDialog::getItem(m_panel, tr("BPM entry"), tr("Entry:"), choices, 0, false, &ok);
        if (!ok)
            return false;
        const auto point = list.value(choices.indexOf(choice));
        anchor["bpm"] =
            QJsonObject{{"beat", QJsonArray{point.beatNum, point.numerator, point.denominator}}, {"bpm", point.bpm}};
    }
    auto list = comments();
    if (!addComment(list, m_identity.activeIdentity().snapshot(), body, anchor, {}, nullptr, error))
        return false;
    auto log = events();
    log.append(eventRecord("CreateComment", m_identity.activeIdentity().snapshot()));
    m_projectMode = true;
    commitReviewState(tr("Add comment"), list, log);
    return true;
}

void ReviewWorkspace::editComment(bool remove)
{
    const QString id = selectedCommentId();
    if (id.isEmpty())
        return;
    auto list = comments();
    QString error;
    if (remove)
    {
        if (!deleteComment(list, id, m_identity.activeIdentity().authorId, &error))
        {
            report(error);
            return;
        }
    }
    else
    {
        QString body;
        for (const auto &v : list)
            if (v.toObject().value("commentId") == id)
                body = v.toObject().value("body").toString();
        bool ok;
        body = QInputDialog::getMultiLineText(m_panel, tr("Edit comment"), tr("Comment:"), body, &ok);
        if (!ok)
            return;
        if (!CceReview::editComment(list, id, m_identity.activeIdentity().authorId, body, &error))
        {
            report(error);
            return;
        }
    }
    auto log = events();
    log.append(eventRecord(remove ? "DeleteComment" : "EditComment", m_identity.activeIdentity().snapshot(),
                           {{"commentId", id}}));
    commitReviewState(remove ? tr("Delete comment") : tr("Edit comment"), list, log);
}
void ReviewWorkspace::setResolved(bool resolved)
{
    const QString id = selectedCommentId();
    auto list = comments();
    QString thread;
    for (const auto &v : list)
        if (v.toObject().value("commentId") == id)
            thread = v.toObject().value("threadId").toString();
    QString error;
    if (!setThreadResolved(list, thread, resolved, &error))
    {
        report(error);
        return;
    }
    auto log = events();
    log.append(eventRecord(resolved ? "ResolveComment" : "ReopenComment", m_identity.activeIdentity().snapshot(),
                           {{"threadId", thread}}));
    commitReviewState(resolved ? tr("Resolve comment") : tr("Reopen comment"), list, log);
}

void ReviewWorkspace::setDecision(const QString &decision)
{
    if (!m_changesTree)
        return;
    QSet<QString> selected;
    for (auto *item : m_changesTree->selectedItems())
        selected.insert(item->data(0, Qt::UserRole).toString());
    if (selected.isEmpty())
    {
        report(tr("Select a change to review."));
        return;
    }
    auto after = m_bundle;
    QJsonArray decisions;
    for (const auto &v : m_bundle.review.value("reviewDecisions").toArray())
        if (!selected.contains(v.toObject().value("changeId").toString()))
            decisions.append(v);
    for (const auto &change : m_changes)
        if (selected.contains(change.changeId))
            decisions.append(QJsonObject{{"changeId", change.changeId},
                                         {"changeHash", changeFingerprint(change)},
                                         {"decision", decision},
                                         {"author", m_identity.activeIdentity().snapshot()}});
    after.review["reviewDecisions"] = decisions;
    auto log = events();
    log.append(eventRecord(decision == "Accepted"   ? "AcceptChange"
                           : decision == "Rejected" ? "RejectChange"
                                                    : "CreateChange",
                           m_identity.activeIdentity().snapshot()));
    after.review["events"] = log;
    QString error;
    if (!applyBundle(tr("Update review decision"), after, &error))
        report(error);
}

void ReviewWorkspace::linkAsMove()
{
    if (!m_editingProposal || !m_changesTree || m_changesTree->selectedItems().size() != 2)
    {
        report(tr("Select one removal and one addition while editing the proposal."));
        return;
    }
    Change old, added;
    for (auto *item : m_changesTree->selectedItems())
        for (const auto &c : m_changes)
            if (c.changeId == item->data(0, Qt::UserRole).toString())
            {
                if (c.kind == "Remove")
                    old = c;
                if (c.kind == "Add")
                    added = c;
            }
    if (old.changeId.isEmpty() || added.changeId.isEmpty() || old.domain != "Note" || old.path != added.path)
    {
        report(tr("Choose a Note removal and addition from the same layer."));
        return;
    }
    old.kind = "Move";
    old.after = added.after;
    old.moveIntent = true;
    old.operationGroupId = uuid();
    m_intents.append(old);
    addEvent("LinkAsMove");
    markChanged();
    scheduleDiff();
}

void ReviewWorkspace::resolveSelected(bool useProposal)
{
    if (!m_changesTree)
        return;
    auto *selectedItem = m_changesTree->currentItem();
    const QString selectedId = selectedItem ? selectedItem->data(0, Qt::UserRole).toString() : QString();
    if (selectedId.startsWith("conflict:"))
    {
        auto revision = m_revision;
        QString error;
        if (!resolveRevisionConflict(revision, selectedId.mid(9).toInt(), useProposal,
                                     m_identity.activeIdentity().snapshot(), &error))
        {
            report(error);
            return;
        }
        auto after = m_bundle;
        after.review["revision"] = revision;
        after.review["comments"] = revision.value("comments");
        after.review["reviewDecisions"] = revision.value("reviewDecisions");
        after.review["events"] = revision.value("events");
        if (m_editingProposal)
        {
            QVector<Change> changes;
            for (const auto &v : revision.value("changes").toArray())
            {
                Change c;
                if (changeFromJson(v.toObject(), c, nullptr))
                    changes.append(c);
            }
            const auto base = revision.value("baseSnapshot").toObject();
            const auto rebuilt = merge(base, base, changes);
            auto json = rebuilt.result;
            json["activeDifficultyId"] = after.project.activeDifficultyId;
            if (!CceDocument::Project::fromJson(json, after.project, &error))
            {
                report(error);
                return;
            }
        }
        m_applyingReview = true;
        const bool applied = applyBundle(tr("Resolve revision conflict"), after, &error);
        m_applyingReview = false;
        if (!applied)
            report(error);
        else
            initializeProposalSources();
        return;
    }
    if (!useProposal)
    {
        setDecision("Rejected");
        return;
    }
    auto target = m_editingProposal ? m_targetBundle : m_bundle;
    auto selected = m_changesTree->selectedItems();
    QSet<QString> ids;
    for (auto *item : selected)
        ids.insert(item->data(0, Qt::UserRole).toString());
    auto result = merge(m_revision.value("baseSnapshot").toObject(), snapshot(target), m_changes, ids);
    QHash<QString, QJsonValue> resolutions;
    for (const auto &item : result.items)
        if (ids.contains(item.change.changeId) && item.state == "Conflict")
        {
            if (item.current.isArray())
            {
                QStringList labels{tr("Keep existing objects; apply proposal separately")};
                const auto objects = item.current.toArray();
                for (int i = 0; i < objects.size(); ++i)
                    labels.append(QString::number(i + 1) + " · " + summary(objects[i]));
                bool ok = false;
                const auto chosen =
                    QInputDialog::getItem(m_panel, tr("Resolve object conflict"),
                                          tr("Choose the current object to replace or remove:"), labels, 0, false, &ok);
                if (!ok)
                    return;
                const int index = labels.indexOf(chosen) - 1;
                resolutions[item.change.changeId] = index >= 0 ? objects[index] : QJsonValue(QJsonValue::Null);
            }
            else
                resolutions[item.change.changeId] = item.current;
        }
    QString error;
    if (!applyChanges(ids, &error, resolutions))
        report(error);
}

void ReviewWorkspace::changeLayer(int operation)
{
    if (displayIsReadOnly())
    {
        report(tr("Base is read only."));
        return;
    }
    auto after = m_bundle;
    auto &p = after.project;
    auto &d = p.difficulties[p.activeDifficultyId];
    const QString selected = selectedLayerId();
    bool ok = false;
    if (operation == 0)
    {
        const QString name =
            QInputDialog::getText(m_panel, tr("New layer"), tr("Name:"), QLineEdit::Normal, tr("Layer"), &ok);
        if (!ok)
            return;
        m_activeLayerId = p.addLayer(p.activeDifficultyId, name);
    }
    else if (operation == 1)
    {
        if (!p.layers.contains(selected))
            return;
        const QString name = QInputDialog::getText(m_panel, tr("Rename layer"), tr("Name:"), QLineEdit::Normal,
                                                   p.layers[selected].name, &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        p.layers[selected].name = name.trimmed();
    }
    else if (operation == 2)
    {
        QStringList labels, ids;
        for (auto it = p.layers.cbegin(); it != p.layers.cend(); ++it)
        {
            labels.append(it->name);
            ids.append(it.key());
        }
        const auto chosen = chooseItems(m_panel, tr("Share existing layers"), labels, ids);
        if (chosen.isEmpty())
            return;
        for (const QString &id : chosen)
            p.bindLayer(p.activeDifficultyId, id);
    }
    else if (operation == 3)
    {
        const QString id = p.copyAsIndependent(p.activeDifficultyId, selected);
        if (id.isEmpty())
            return;
        m_activeLayerId = id;
    }
    else if (operation == 4)
    {
        if (d.bindings.size() <= 1)
        {
            report(tr("Keep at least one layer in a difficulty."));
            return;
        }
        p.unbindLayer(p.activeDifficultyId, selected);
    }
    else if (operation == 5 || operation == 6)
    {
        auto it = std::find_if(d.bindings.begin(), d.bindings.end(), [&](const auto &b) {
            return b.layerId == selected;
        });
        if (it == d.bindings.end())
            return;
        int i = int(it - d.bindings.begin()), j = i + (operation == 5 ? -1 : 1);
        if (j < 0 || j >= d.bindings.size())
            return;
        d.bindings.swapItemsAt(i, j);
        for (int k = 0; k < d.bindings.size(); ++k)
            d.bindings[k].order = k;
    }
    else if (operation == 7 || operation == 8)
    {
        QVector<Note> selection;
        QSet<QString> ids;
        for (int i : m_selection->selectedIndices())
            if (i >= 0 && i < m_controller->chart()->notes().size())
            {
                selection.append(portableNote(m_controller->chart()->notes()[i]));
                ids.insert(selection.last().id);
            }
        if (selection.isEmpty() || !guardEdit(selection, true))
            return;
        for (auto it = p.layers.begin(); it != p.layers.end(); ++it)
            if (operation == 7)
                it->notes.erase(std::remove_if(it->notes.begin(), it->notes.end(),
                                               [&](const Note &note) {
                                                   return ids.contains(note.id);
                                               }),
                                it->notes.end());
        for (Note note : selection)
        {
            if (operation == 8)
                note.id = Note::generateId();
            p.layers[selected].notes.append(note);
        }
    }
    QString error;
    if (!applyBundle(tr("Edit project layers"), after, &error))
        report(error);
    m_sharedConfirmed.clear();
}

void ReviewWorkspace::changeDifficulty(int operation)
{
    if (displayIsReadOnly())
    {
        report(tr("Base is read only."));
        return;
    }
    auto after = m_bundle;
    auto &p = after.project;
    const auto source = p.difficulties[p.activeDifficultyId];
    bool ok;
    const QString name = QInputDialog::getText(m_panel, tr("Difficulty"), tr("Name:"), QLineEdit::Normal,
                                               operation == 1 ? source.name : source.name + tr(" copy"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    if (operation == 1)
    {
        p.difficulties[p.activeDifficultyId].name = name;
        p.difficulties[p.activeDifficultyId].metadata.difficulty = name;
    }
    else
    {
        auto meta = source.metadata;
        meta.difficulty = name;
        if (!activeAuthorName().isEmpty())
            meta.chartAuthor = activeAuthorName();
        const QString id = p.addDifficulty(name, meta, source.timing, source.oggHash);
        if (operation == 0)
            p.addLayer(id, tr("Base"));
        else
        {
            auto duplicate = source;
            duplicate.difficultyId = id;
            duplicate.name = name;
            duplicate.metadata = meta;
            duplicate.bindings.clear();
            for (const auto &b : source.bindings)
            {
                auto layer = p.layers[b.layerId];
                layer.layerId = uuid();
                for (auto &note : layer.notes)
                    note.id = Note::generateId();
                p.layers[layer.layerId] = layer;
                auto binding = b;
                binding.layerId = layer.layerId;
                duplicate.bindings.append(binding);
            }
            p.difficulties[id] = duplicate;
        }
        p.activeDifficultyId = id;
    }
    QString error;
    if (!applyBundle(tr("Edit project difficulty"), after, &error))
        report(error);
}

void ReviewWorkspace::importDifficulty()
{
    const QString path = QFileDialog::getOpenFileName(m_panel, tr("Import difficulty"), {}, tr("Malody Charts (*.mc)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!importDifficultyFromPath(path, &error))
        report(error);
}

void ReviewWorkspace::addReference()
{
    auto after = m_bundle;
    QString error;
    if (!collectResources(after, &error))
    {
        report(error);
        return;
    }
    QStringList ids, labels;
    for (auto it = after.project.difficulties.cbegin(); it != after.project.difficulties.cend(); ++it)
        if (it.key() != after.project.activeDifficultyId)
        {
            ids.append(it.key());
            labels.append(it->name);
        }
    if (ids.isEmpty())
    {
        report(tr("Import or create another difficulty first."));
        return;
    }
    bool ok;
    const QString name =
        QInputDialog::getItem(m_panel, tr("Reference difficulty"), tr("Source:"), labels, 0, false, &ok);
    if (!ok)
        return;
    const QString source = ids.value(labels.indexOf(name));
    QStringList layerIds, layerNames;
    for (const auto &b : after.project.difficulties[source].bindings)
    {
        layerIds.append(b.layerId);
        layerNames.append(after.project.layers[b.layerId].name);
    }
    const auto chosen =
        chooseItems(m_panel, tr("Reference layers (select all for the whole difficulty)"), layerNames, layerIds);
    if (chosen.isEmpty())
        return;
    if (!after.project.addReference(after.project.activeDifficultyId, source,
                                    chosen.size() == layerIds.size() ? QStringList{} : chosen, &error)
        || !applyBundle(tr("Add live reference"), after, &error))
        report(error);
}

QWidget *ReviewWorkspace::createExportConfiguration(QWidget *parent)
{
    auto *group = new QGroupBox(tr("Export Metadata Configuration"), parent);
    group->setObjectName("exportMetadataConfiguration");
    auto *layout = new QVBoxLayout(group);
    auto *hint =
        new QLabel(tr("Choose fields included in exported .mc files. Stored chart values remain independent."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    for (const QString &field : metaFields)
    {
        auto *check = new QCheckBox(fieldLabel(field), group);
        check->setProperty("metadataField", field);
        check->setObjectName("exportField_" + field);
        m_exportChecks.append(check);
        layout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &ReviewWorkspace::onExportFieldsChanged);
    }
    updateExportFields();
    return group;
}
void ReviewWorkspace::updateExportFields()
{
    const auto fields = m_bundle.project.difficulties.value(m_bundle.project.activeDifficultyId).exportedMetadataFields;
    for (auto *check : m_exportChecks)
    {
        const QSignalBlocker block(check);
        check->setChecked(fields.contains(check->property("metadataField").toString()));
        check->setEnabled(!displayIsReadOnly());
    }
}
void ReviewWorkspace::onExportFieldsChanged()
{
    if (m_refreshing)
        return;
    auto *check = qobject_cast<QCheckBox *>(sender());
    if (!check)
        return;
    auto after = m_bundle;
    auto &fields = after.project.difficulties[after.project.activeDifficultyId].exportedMetadataFields;
    const QString field = check->property("metadataField").toString();
    if (check->isChecked())
        fields.insert(field);
    else
        fields.remove(field);
    QString error;
    if (!applyBundle(tr("Change export metadata configuration"), after, &error))
    {
        report(error);
        updateExportFields();
    }
}

bool ReviewWorkspace::exportChart(const QString &path, const QStringList &layers, QString *error)
{
    auto bundle = m_bundle;
    if (!collectResources(bundle, error))
        return false;
    return CceDocument::exportDifficulty(bundle, bundle.project.activeDifficultyId, path, layers, error);
}
void ReviewWorkspace::exportChartDialog(bool layerOnly)
{
    const QString path = QFileDialog::getSaveFileName(m_panel ? static_cast<QWidget *>(m_panel) : m_window,
                                                      tr("Export chart"), {}, tr("Malody Charts (*.mc)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!exportChart(path, layerOnly ? QStringList{selectedLayerId()} : QStringList{}, &error))
        report(error);
    else
        report(tr("Chart exported."));
}
void ReviewWorkspace::saveProjectDialog()
{
    const QString path =
        QFileDialog::getSaveFileName(m_window, tr("Save CCE project"), m_projectPath, tr("CCE Projects (*.cceproj)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!saveProject(path, &error))
        report(error);
}
bool ReviewWorkspace::confirmClose()
{
    if (!m_projectMode || !m_dirty)
        return true;
    const auto reply =
        QMessageBox::question(m_window, tr("Save project"), tr("Save changes to the local project and review data?"),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (reply == QMessageBox::Cancel)
        return false;
    if (reply == QMessageBox::Discard)
        return true;
    saveProjectDialog();
    return !m_dirty;
}

void ReviewWorkspace::editProposal()
{
    if (m_revision.isEmpty())
    {
        report(tr("Create or import a proposal first."));
        return;
    }
    if (m_editingProposal)
    {
        setViewMode(0);
        return;
    }
    CceDocument::ProjectBundle after;
    QString error;
    if (!proposalBundle(after, &error))
    {
        report(error);
        return;
    }
    m_targetBundle = m_bundle;
    after.review["editingProposal"] = true;
    after.review["targetProject"] = m_targetBundle.project.toJson();
    QJsonObject paths;
    for (auto it = m_targetBundle.resourcePaths.cbegin(); it != m_targetBundle.resourcePaths.cend(); ++it)
        paths[it.key()] = it.value();
    after.review["targetResourcePaths"] = paths;
    m_controller->setReadOnly(false);
    m_applyingReview = true;
    const bool applied = applyBundle(tr("Edit proposal"), after, &error);
    m_applyingReview = false;
    if (!applied)
    {
        report(error);
        return;
    }
    m_editingProposal = true;
    initializeProposalSources();
    setViewMode(0);
}
} // namespace CceReview
