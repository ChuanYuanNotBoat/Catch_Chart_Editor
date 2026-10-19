#pragma once

#include "document/ProjectStorage.h"
#include "identity/LocalIdentityStore.h"
#include "review/ReviewCore.h"
#include "render/RainVisibilityIndex.h"
#include <QObject>
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>
#include <QColor>
#include <QRectF>
#include <functional>
#include <cmath>
#include <limits>

class ChartController;
class SelectionController;
class ChartCanvas;
class CommandRouter;
class QMainWindow;
class QDialog;
class QComboBox;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QLabel;
class QPainter;
class QWidget;
class QCheckBox;
class QToolBar;
class QAction;
class QPlainTextEdit;
class QMouseEvent;
namespace CceReviewUi
{
class NoteInspector;
}

namespace CceReview
{
// Coordinates the local document and the review protocol; MainWindow only
// supplies existing editor services and menu/workspace ownership.
class ReviewWorkspace final : public QObject
{
    Q_OBJECT
public:
    using DiffRunner =
        std::function<QVector<Change>(const QJsonObject &, const QJsonObject &, const QVector<Change> &)>;
    ReviewWorkspace(ChartController *controller, SelectionController *selection, ChartCanvas *canvas,
                    QMainWindow *window, const QString &identityPath = QString(), DiffRunner diffRunner = {});
    ~ReviewWorkspace() override;
    void installMenus(CommandRouter *router, QToolBar *toolbar = nullptr);
    QWidget *inspector() const;
    QWidget *createExportConfiguration(QWidget *parent);
    QString activeAuthorName() const;
    bool projectMode() const
    {
        return m_projectMode;
    }
    bool dirty() const
    {
        return m_dirty;
    }
    const CceDocument::Project &project() const
    {
        return m_bundle.project;
    }
    QJsonObject revision() const
    {
        return m_revision;
    }
    QJsonArray comments() const;
    CceIdentity::LocalIdentityStore *identities()
    {
        return &m_identity;
    }
    bool setProject(const CceDocument::Project &project, QString *error = nullptr);
    bool openProject(const QString &path, QString *error = nullptr);
    bool saveProject(const QString &path, QString *error = nullptr);
    bool exportChart(const QString &path, const QStringList &layerIds = {}, QString *error = nullptr);
    bool importDifficultyFromPath(const QString &path, QString *error = nullptr);
    bool startProposal(const QString &description, QString *error = nullptr);
    bool importProposal(const QString &path, QString *error = nullptr);
    bool exportProposal(const QString &path, QString *error = nullptr);
    bool saveProposalAsDifficulty(QString *error = nullptr);
    bool applyChanges(const QSet<QString> &changeIds, QString *error = nullptr,
                      const QHash<QString, QJsonValue> &resolutions = {});
    bool rebase(QString *error = nullptr);
    bool addCommentAtSelection(const QString &body, const QString &anchorType, QString *error = nullptr,
                               const QPointF &canvasPosition = QPointF(-1, -1));
    void showPanel();
    void saveProjectDialog();
    bool confirmClose();
    void setViewMode(int mode);
    bool displayIsReadOnly() const
    {
        return m_viewMode == 2 || m_peeking;
    }
signals:
    void workspaceChanged();
    void projectOpened(const QString &path);
    void statusMessage(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct IntentSource
    {
        QJsonObject before;
        QString layerId, changeId, groupId;
        bool added = false;
    };
    struct PaintedNote
    {
        Note note;
        QColor color;
        qreal opacity = .5;
        QString groupId;
        double projectedStart = std::numeric_limits<double>::quiet_NaN();
        double projectedEnd = std::numeric_limits<double>::quiet_NaN();
        double firstBeat() const
        {
            return std::isnan(projectedStart) ? note.getStartBeat() : projectedStart;
        }
        double lastBeat() const
        {
            return std::isnan(projectedEnd) ? note.getEndBeat() : projectedEnd;
        }
    };
    struct CommentCard
    {
        QString commentId, threadId, author, body, referenceStatus;
        double beat = 0, laneX = -1;
        QPointF offset;
        QVector<QPointF> beforeTargets, afterTargets;
        QRectF region;
        bool hasRegion = false;
        bool expanded = true, resolved = false;
    };
    struct MoveBounds
    {
        QRectF before, after; // lane X and beat coordinates, projected during paint
        int count = 0;
    };
    void chartLoaded();
    void chartChanged();
    void recordOperation(const QVector<Note> &removed, const QVector<Note> &added,
                         const QList<QPair<Note, Note>> &moved, const QString &group);
    bool guardEdit(const QVector<Note> &notes, bool adding);
    bool applyBundle(const QString &name, const CceDocument::ProjectBundle &after, QString *error = nullptr);
    void restoreBundle(const CceDocument::ProjectBundle &bundle);
    void initializeProposalSources();
    bool proposalBundle(CceDocument::ProjectBundle &bundle, QString *error = nullptr) const;
    void markChanged();
    void scheduleDiff();
    void startDiff();
    void rebuildPresentation();
    void rebuildCommentCards();
    void paintOverlay(QPainter *painter);
    void buildUi();
    void refreshUi();
    void refreshChanges();
    void refreshComments();
    void selectChange();
    void navigate(int kind, int direction);
    void editComment(bool remove);
    void addCommentDialog(const QPointF &canvasPosition = QPointF(-1, -1));
    void showComments();
    void showPanelPage(int page);
    void installToolbar(QToolBar *toolbar, CommandRouter *router, QAction *review);
    void refreshToolbar();
    bool selectToolbarChanges();
    void setResolved(bool resolved);
    void addEvent(const QString &type, const QJsonObject &detail = {});
    void commitReviewState(const QString &name, const QJsonArray &comments, const QJsonArray &events);
    void setDecision(const QString &decision);
    void linkAsMove();
    void resolveSelected(bool useProposal);
    void changeLayer(int operation);
    void changeDifficulty(int operation);
    void addReference();
    void importDifficulty();
    void editProposal();
    void exportChartDialog(bool layerOnly);
    void updateExportFields();
    void onExportFieldsChanged();
    void persistRecovery();
    bool collectResources(CceDocument::ProjectBundle &bundle, QString *error) const;
    bool prepareRevision(QJsonObject &revision, CceDocument::ProjectBundle &bundle, QString *error) const;
    Chart portableChart(const Chart &chart);
    Note portableNote(const Note &note);
    Chart activeChart(const CceDocument::ProjectBundle &bundle) const;
    QJsonObject snapshot() const;
    QJsonObject snapshot(const CceDocument::ProjectBundle &bundle) const;
    QVector<Change> intents() const;
    QVector<Change> currentChanges() const;
    QSet<QString> acceptedChangeIds() const;
    QString resourceRoot() const;
    QString resourceCacheRoot(const CceDocument::ProjectBundle &bundle) const;
    QString selectedLayerId() const;
    QString selectedCommentId() const;
    QJsonArray events() const;
    void report(const QString &message);

    ChartController *m_controller;
    SelectionController *m_selection;
    ChartCanvas *m_canvas;
    QMainWindow *m_window;
    CceIdentity::LocalIdentityStore m_identity;
    CceDocument::ProjectBundle m_bundle;
    CceDocument::ProjectBundle m_targetBundle;
    CceDocument::Project m_baseProject;
    QJsonObject m_revision;
    QVector<Change> m_changes;
    QHash<QString, IntentSource> m_sources;
    QVector<Change> m_intents;
    QHash<QString, Change> m_intentMap;
    QVector<Change> m_pendingOperations;
    QHash<QString, QString> m_resourceSources;
    QSet<QString> m_sharedConfirmed;
    QHash<QString, QString> m_owner;
    Chart m_lastChart;
    QString m_lastChartPath, m_projectPath, m_activeLayerId, m_cacheRoot;
    bool m_restoring = false, m_refreshing = false, m_projectMode = false, m_dirty = false;
    bool m_pendingDiff = false, m_peeking = false;
    bool m_editingProposal = false;
    bool m_applyingReview = false;
    int m_viewMode = 0;
    quint64 m_generation = 0, m_diffGeneration = 0;
    DiffRunner m_diffRunner;
    QTimer m_diffTimer, m_recoveryTimer;
    QFutureWatcher<QVector<Change>> m_diffWatcher;
    QFutureWatcher<QString> m_recoveryWatcher;
    bool m_pendingRecovery = false;
    QVector<PaintedNote> m_overlayNotes;
    QVector<MoveBounds> m_moveBounds;
    RainVisibilityIndex::IntervalIndex m_moveBoundsIndex;
    QHash<QByteArray, QVector<int>> m_overlayByNote;
    QHash<QString, QVector<int>> m_overlayByGroup;
    Chart m_presentationChart;
    RainVisibilityIndex::IntervalIndex m_presentationRainIndex;
    RainVisibilityIndex::IntervalIndex m_overlayRainIndex;
    QVector<CommentCard> m_commentCards;
    RainVisibilityIndex::IntervalIndex m_commentCardIndex;
    QHash<QString, QRectF> m_commentRects;
    QString m_draggedComment;
    QPointF m_dragStart, m_dragOffset;
    QPointer<QDialog> m_panel;
    QComboBox *m_difficultyBox = nullptr, *m_viewBox = nullptr, *m_filterBox = nullptr, *m_commentFilterBox = nullptr;
    QTreeWidget *m_layersTree = nullptr, *m_changesTree = nullptr, *m_commentsTree = nullptr;
    QListWidget *m_referencesList = nullptr, *m_eventsList = nullptr;
    QLabel *m_identityLabel = nullptr, *m_statusLabel = nullptr;
    QPointer<CceReviewUi::NoteInspector> m_inspector;
    QVector<QCheckBox *> m_exportChecks;
    QVector<QPointer<QAction>> m_editorToolbarActions, m_toolbarOwnedActions;
    QPointer<QAction> m_layerVisibleAction, m_layerLockedAction;
};
} // namespace CceReview
