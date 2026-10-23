#include "ReviewWorkspace.h"
#include "CommentModel.h"
#include "ReviewResources.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/review/NoteInspector.h"
#include "file/ChartIO.h"
#include "utils/FileUtils.h"
#include "utils/MathUtils.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QPushButton>
#include <QTreeWidget>
#include <QComboBox>
#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace CceReview
{
namespace
{
QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool failure(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
QString pointerPart(QString s)
{
    return s.replace("~", "~0").replace("/", "~1");
}
QString notePath(const QString &layer)
{
    return "/layers/" + pointerPart(layer) + "/notes";
}
QJsonArray jsonStrings(const QStringList &values)
{
    QJsonArray a;
    for (const auto &s : values)
        a.append(s);
    return a;
}
QString contentKey(const QJsonObject &o)
{
    return QString::fromLatin1(canonicalHash(o));
}
QJsonArray encodeChanges(const QVector<Change> &changes)
{
    QJsonArray a;
    for (const auto &c : changes)
        a.append(changeToJson(c));
    return a;
}
} // namespace

ReviewWorkspace::ReviewWorkspace(ChartController *controller, SelectionController *selection, ChartCanvas *canvas,
                                 QMainWindow *window, const QString &identityPath, DiffRunner diffRunner)
    : QObject(window), m_controller(controller), m_selection(selection), m_canvas(canvas), m_window(window),
      m_identity(identityPath, this), m_diffRunner(std::move(diffRunner))
{
    setObjectName("localReviewWorkspace");
    QString error;
    if (!m_identity.load(&error))
        report(error);
    m_cacheRoot =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("local-review/resources");
    m_diffTimer.setSingleShot(true);
    m_diffTimer.setInterval(100);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_diffTimer, &QTimer::timeout, this, &ReviewWorkspace::startDiff);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &ReviewWorkspace::persistRecovery);
    connect(&m_recoveryWatcher, &QFutureWatcher<QString>::finished, this, [this] {
        const auto error = m_recoveryWatcher.result();
        if (!error.isEmpty())
            report(error);
        if (m_pendingRecovery)
        {
            m_pendingRecovery = false;
            m_recoveryTimer.start();
        }
    });
    connect(&m_diffWatcher, &QFutureWatcher<QVector<Change>>::finished, this, [this] {
        if (m_diffGeneration == m_generation)
        {
            m_changes = m_diffWatcher.result();
            rebuildPresentation();
            refreshChanges();
        }
        if (m_pendingDiff || m_diffGeneration != m_generation)
        {
            m_pendingDiff = false;
            m_diffTimer.start();
        }
    });
    connect(controller, &ChartController::chartLoaded, this, &ReviewWorkspace::chartLoaded);
    connect(controller, &ChartController::chartChangeCommitted, this, [this] {
        chartChanged();
    });
    connect(controller, &ChartController::noteOperationCommitted, this, &ReviewWorkspace::recordOperation);
    connect(controller, &ChartController::historyOperationCommitted, this, [this](bool undo, const QString &action) {
        if (!m_projectMode)
            return;
        addEvent(undo ? "Undo" : "Redo", {{"action", action}});
        markChanged();
        refreshUi();
    });
    connect(controller, &ChartController::undoStateChanged, this, [this] {
        if (m_restoring)
        {
            m_restoring = false;
            m_lastChart = portableChart(*m_controller->chart());
            rebuildPresentation();
            refreshUi();
            scheduleDiff();
        }
    });
    controller->setEditGuard([this](const QVector<Note> &notes, bool adds) {
        return guardEdit(notes, adds);
    });
    if (canvas)
    {
        connect(canvas, &ChartCanvas::paintReviewOverlay, this, &ReviewWorkspace::paintOverlay, Qt::DirectConnection);
        canvas->installEventFilter(this);
        connect(canvas, &ChartCanvas::extendContextMenu, this, [this](QMenu *menu, const QPointF &position) {
            const int hit = m_canvas->commentNoteAt(position);
            QRectF area;
            const bool hasArea = m_canvas->selectedCommentRegion(area);
            const QPointF chartPosition(m_canvas->chartCanvasXToLaneX(position.x()), m_canvas->chartYToBeat(position.y()));
            double begin = 0, end = 0;
            const bool inRange = hasArea ? area.contains(chartPosition)
                : m_canvas->selectedBeatRange(begin, end) && chartPosition.y() >= begin && chartPosition.y() <= end;
            if (hit >= 0 && !inRange && m_selection && !m_selection->selectedIndices().contains(hit))
            {
                m_canvas->clearSelectedCommentRegion();
                m_canvas->clearRangeOverlay();
                m_selection->select(hit);
            }
            const QString type = selectionAnchorType();
            auto *comment = menu->addAction(type == "Region" ? tr("Comment on Selected Range…")
                                            : type == "Note" || type == "NoteGroup"
                                                ? tr("Comment on Selected Notes…") : tr("Add Comment Here…"));
            comment->setObjectName("review.context.addComment");
            connect(comment, &QAction::triggered, this, [this, position, type] {
                addCommentDialog(position, type == "TimePoint" ? "TimePosition" : type);
            });
            if (type == "Note" || type == "NoteGroup")
            {
                auto *region = menu->addAction(tr("Comment on Selection Bounds…"));
                region->setObjectName("review.context.addRegionComment");
                connect(region, &QAction::triggered, this, [this, position] { addCommentDialog(position, "Region"); });
            }
            if (type == "Region" && m_selection && !m_selection->selectedIndices().isEmpty())
            {
                auto *notes = menu->addAction(tr("Comment on Selected Notes…"));
                notes->setObjectName("review.context.addNoteComment");
                connect(notes, &QAction::triggered, this, [this, position] {
                    addCommentDialog(position, m_selection->selectedIndices().size() == 1 ? "Note" : "NoteGroup");
                });
            }
            if (type != "TimePoint")
            {
                auto *point = menu->addAction(tr("Comment at This Position…"));
                connect(point, &QAction::triggered, this, [this, position] { addCommentDialog(position, "TimePosition"); });
            }
            auto *comments = menu->addAction(tr("Show Comments"));
            comments->setObjectName("review.context.comments");
            connect(comments, &QAction::triggered, this, &ReviewWorkspace::showComments);
            auto *review = menu->addAction(tr("Review Changes…"));
            review->setObjectName("review.context.review");
            connect(review, &QAction::triggered, this, &ReviewWorkspace::showPanel);
        });
    }
    m_inspector = new CceReviewUi::NoteInspector(controller, selection, window);
    connect(&m_identity, &CceIdentity::LocalIdentityStore::changed, this, &ReviewWorkspace::refreshUi);
    chartLoaded();
}

ReviewWorkspace::~ReviewWorkspace()
{
    m_diffTimer.stop();
    m_recoveryTimer.stop();
    m_controller->setEditGuard({});
    m_controller->setReadOnly(false);
    if (m_canvas)
    {
        m_canvas->removeEventFilter(this);
        m_canvas->setDisplayChart(nullptr);
    }
    if (m_selection)
        m_selection->setNotes(&m_controller->chart()->notes(), m_controller->revision());
    delete m_panel.data();
    delete m_inspector.data();
    // Workers own value snapshots, never widgets or this coordinator.
}

QWidget *ReviewWorkspace::inspector() const
{
    return m_inspector;
}
QString ReviewWorkspace::activeAuthorName() const
{
    return m_identity.activeIdentity().authorName;
}
QJsonArray ReviewWorkspace::comments() const
{
    return m_bundle.review.value("comments").toArray();
}
QJsonArray ReviewWorkspace::events() const
{
    return m_bundle.review.value("events").toArray();
}
QString ReviewWorkspace::resourceRoot() const
{
    return QFileInfo(m_lastChartPath).absolutePath();
}
QString ReviewWorkspace::resourceCacheRoot(const CceDocument::ProjectBundle &bundle) const
{
    QJsonObject paths;
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
        paths[it.key()] = it.value();
    return QDir(m_cacheRoot).filePath(QString::fromLatin1(canonicalHash(paths)));
}

Chart ReviewWorkspace::portableChart(const Chart &chart)
{
    Chart portable = chart;
    auto resource = [this](const QString &value) {
        if (value.isEmpty())
            return value;
        if (!QDir::isAbsolutePath(value))
        {
            if (!m_resourceSources.contains(value))
                m_resourceSources[value] = QDir(resourceRoot()).filePath(value);
            return value;
        }
        for (auto it = m_resourceSources.cbegin(); it != m_resourceSources.cend(); ++it)
            if (QDir::cleanPath(it.value()) == QDir::cleanPath(value))
                return it.key();
        QString name = QFileInfo(value).fileName();
        if (m_resourceSources.contains(name) && m_resourceSources[name] != value)
            name = uuid().left(8) + "_" + name;
        m_resourceSources[name] = value;
        return name;
    };
    portable.meta().audioFile = resource(chart.meta().audioFile);
    portable.meta().backgroundFile = resource(chart.meta().backgroundFile);
    for (Note &note : portable.notes())
        if (note.isSound())
            note.sound = resource(note.sound);
    return portable;
}

Note ReviewWorkspace::portableNote(const Note &note)
{
    if (!note.isSound())
        return note;
    Chart chart;
    chart.notes().append(note);
    return portableChart(chart).notes().first();
}

void ReviewWorkspace::chartLoaded()
{
    if (m_restoring)
        return;
    m_lastChartPath = m_controller->chartFilePath();
    m_resourceSources.clear();
    m_sources.clear();
    m_intentMap.clear();
    m_pendingOperations.clear();
    Chart chart = portableChart(*m_controller->chart());
    if (chart.bpmList().isEmpty())
        chart.addBpm(BpmEntry(0, 0, 1, 120));
    m_bundle = {};
    m_bundle.project = CceDocument::Project::fromChart(chart);
    m_bundle.review = {{"comments", QJsonArray{}}, {"events", QJsonArray{}}};
    m_baseProject = {};
    m_revision = {};
    m_changes.clear();
    m_targetBundle = {};
    m_editingProposal = false;
    m_projectPath.clear();
    m_projectMode = false;
    m_dirty = false;
    m_viewMode = 0;
    m_peeking = false;
    m_lastChart = chart;
    loadChartReview();
    m_controller->setReadOnly(false);
    if (m_canvas)
        m_canvas->setDisplayChart(nullptr);
    const auto &difficulty = m_bundle.project.difficulties[m_bundle.project.activeDifficultyId];
    m_activeLayerId = difficulty.bindings.isEmpty() ? QString() : difficulty.bindings.first().layerId;
    rebuildPresentation();
    refreshUi();
    ++m_generation;
    emit workspaceChanged();
}

void ReviewWorkspace::chartChanged()
{
    if (m_restoring || m_lastChartPath != m_controller->chartFilePath())
        return;
    Chart portable = portableChart(*m_controller->chart());
    CceDocument::Project before = m_bundle.project;
    QString error;
    if (!m_bundle.project.updateComposedChart(m_bundle.project.activeDifficultyId, m_lastChart, portable, &error,
                                              m_activeLayerId))
    {
        report(tr("Document update failed: %1").arg(error));
        return;
    }
    QJsonObject oldTiming, newTiming;
    for (auto it = before.difficulties.cbegin(); it != before.difficulties.cend(); ++it)
    {
        QJsonArray points;
        for (const auto &point : it->timing)
            points.append(QJsonObject{{"beat", QJsonArray{point.beatNum, point.numerator, point.denominator}},
                                      {"bpm", point.bpm}});
        oldTiming[it.key()] = QJsonObject{{"timing", points}};
    }
    for (auto it = m_bundle.project.difficulties.cbegin(); it != m_bundle.project.difficulties.cend(); ++it)
    {
        QJsonArray points;
        for (const auto &point : it->timing)
            points.append(QJsonObject{{"beat", QJsonArray{point.beatNum, point.numerator, point.denominator}},
                                      {"bpm", point.bpm}});
        newTiming[it.key()] = QJsonObject{{"timing", points}};
    }
    const auto timingChanges = diff(QJsonObject{{"difficulties", oldTiming}}, QJsonObject{{"difficulties", newTiming}});
    m_pendingOperations += timingChanges;
    m_bundle.review["comments"] = relocateComments(comments(), before, m_bundle.project, m_pendingOperations);
    m_pendingOperations.clear();
    m_lastChart = portable;
    markChanged();
    rebuildPresentation();
    refreshUi();
    scheduleDiff();
}

bool ReviewWorkspace::guardEdit(const QVector<Note> &notes, bool adding)
{
    if (m_restoring || m_bundle.project.difficulties.isEmpty())
        return !displayIsReadOnly();
    if (displayIsReadOnly())
        return false;
    const auto &difficulty = m_bundle.project.difficulties[m_bundle.project.activeDifficultyId];
    QSet<QString> affected;
    for (const Note &note : notes)
        if (m_owner.contains(note.id))
            affected.insert(m_owner.value(note.id));
    if (adding)
        affected.insert(m_activeLayerId);
    for (const QString &layerId : affected)
    {
        auto binding = std::find_if(difficulty.bindings.cbegin(), difficulty.bindings.cend(),
                                    [&](const CceDocument::LayerBinding &b) {
                                        return b.layerId == layerId;
                                    });
        if (binding == difficulty.bindings.cend() || binding->locked)
        {
            report(tr("Choose an unlocked layer before editing."));
            return false;
        }
        if (m_bundle.project.bindingCount(layerId) <= 1 || m_sharedConfirmed.contains(layerId))
            continue;
        QMessageBox warning(QMessageBox::Warning, tr("Edit shared layer"),
                            tr("Layer “%1” is shared by %2 difficulties. Editing it affects all of them.")
                                .arg(m_bundle.project.layers[layerId].name)
                                .arg(m_bundle.project.bindingCount(layerId)),
                            QMessageBox::NoButton, m_window);
        auto *proceed = warning.addButton(tr("Edit Shared Layer"), QMessageBox::AcceptRole);
        auto *copy = warning.addButton(tr("Copy as Independent Layer"), QMessageBox::ActionRole);
        warning.addButton(QMessageBox::Cancel);
        warning.exec();
        if (warning.clickedButton() == proceed)
        {
            m_sharedConfirmed.insert(layerId);
            continue;
        }
        if (warning.clickedButton() == copy)
        {
            auto after = m_bundle;
            const QString id = after.project.copyAsIndependent(after.project.activeDifficultyId, layerId);
            if (!id.isEmpty())
            {
                m_activeLayerId = id;
                QTimer::singleShot(0, this, [this, after] {
                    QString error;
                    if (!applyBundle(tr("Make layer independent"), after, &error))
                        report(error);
                });
            }
        }
        return false;
    }
    return true;
}

void ReviewWorkspace::recordOperation(const QVector<Note> &removed, const QVector<Note> &added,
                                      const QList<QPair<Note, Note>> &moved, const QString &group)
{
    if (m_restoring)
        return;
    auto record = [&](const Note *before, const Note *after) {
        const auto encode = [this](const Note &note) {
            return CceDocument::encodeNote(portableNote(note));
        };
        const QString runtimeId = before ? before->id : after->id;
        const QString layer = m_owner.value(runtimeId, m_activeLayerId);
        Change live;
        live.changeId = uuid();
        live.operationGroupId = group;
        live.domain = "Note";
        live.path = notePath(layer);
        live.before = before ? encode(*before) : QJsonValue(QJsonValue::Undefined);
        live.after = after ? encode(*after) : QJsonValue(QJsonValue::Undefined);
        const bool movedPosition = before && after
                                   && (before->beatNum != after->beatNum || before->numerator != after->numerator
                                       || before->denominator != after->denominator
                                       || (!before->isRainNote() && before->x != after->x));
        live.kind = before && after ? (movedPosition ? "Move" : "Modify") : before ? "Remove" : "Add";
        live.moveIntent = movedPosition;
        m_pendingOperations.append(live);
        if (m_revision.isEmpty())
            return;
        if (!m_sources.contains(runtimeId))
        {
            IntentSource source;
            source.layerId = layer;
            source.changeId = uuid();
            source.groupId = group;
            source.added = !before;
            if (before)
                source.before = encode(*before);
            m_sources.insert(runtimeId, source);
        }
        const auto source = m_sources.value(runtimeId);
        Change c;
        c.changeId = source.changeId;
        c.operationGroupId = group;
        c.domain = "Note";
        c.path = notePath(source.layerId);
        c.before = source.added ? QJsonValue(QJsonValue::Undefined) : QJsonValue(source.before);
        c.after = after ? QJsonValue(encode(*after)) : QJsonValue(QJsonValue::Undefined);
        if ((!after && source.added) || (after && !source.added && source.before == c.after.toObject()))
        {
            m_intentMap.remove(c.changeId);
            return;
        }
        const auto next = c.after.toObject();
        const bool positionChanged =
            after && (source.before.value("beat") != next.value("beat") || source.before.value("x") != next.value("x"));
        c.kind = source.added ? "Add" : after ? (positionChanged ? "Move" : "Modify") : "Remove";
        c.moveIntent = !source.added && after && positionChanged;
        m_intentMap[c.changeId] = c;
    };
    for (const auto &note : removed)
        record(&note, nullptr);
    for (const auto &note : added)
        record(nullptr, &note);
    for (const auto &pair : moved)
        record(&pair.first, &pair.second);
    addEvent(
        removed.size() + added.size() + moved.size() > 1 ? "BatchChange"
        : !removed.isEmpty()                             ? "RemoveNote"
        : !added.isEmpty()                               ? "AddNote"
                                                         : "EditNote",
        {{"operationGroupId", group}, {"removed", removed.size()}, {"added", added.size()}, {"edited", moved.size()}});
}

QJsonObject ReviewWorkspace::snapshot(const CceDocument::ProjectBundle &bundle) const
{
    QJsonObject result = bundle.project.toJson();
    QJsonObject resources;
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
        resources[it.key()] = it.value();
    result["resources"] = resources;
    // Active difficulty is editor state, not a proposed chart change.
    result.remove("activeDifficultyId");
    return result;
}
QJsonObject ReviewWorkspace::snapshot() const
{
    return snapshot(m_bundle);
}
QVector<Change> ReviewWorkspace::intents() const
{
    QVector<Change> result = m_intents;
    for (auto it = m_intentMap.cbegin(); it != m_intentMap.cend(); ++it)
        result.append(it.value());
    QSet<QString> ids, fields;
    for (const auto &change : result)
    {
        ids.insert(change.changeId);
        if (change.domain != "Note" && change.domain != "Timing")
            fields.insert(change.path);
    }
    const auto append = [&](const Change &change) {
        const bool field = change.domain != "Note" && change.domain != "Timing";
        if (ids.contains(change.changeId) || (field && fields.contains(change.path)))
            return;
        result.append(change);
        ids.insert(change.changeId);
        if (field)
            fields.insert(change.path);
    };
    for (const auto &change : m_changes)
        append(change);
    for (const auto &value : m_revision.value("changes").toArray())
    {
        Change change;
        if (changeFromJson(value.toObject(), change, nullptr))
            append(change);
    }
    return result;
}
QVector<Change> ReviewWorkspace::currentChanges() const
{
    return m_changes;
}
QSet<QString> ReviewWorkspace::acceptedChangeIds() const
{
    QHash<QString, const Change *> changes;
    for (const auto &change : m_changes)
        changes[change.changeId] = &change;
    QSet<QString> ids;
    for (const auto &value : m_bundle.review.value("reviewDecisions").toArray())
    {
        const auto decision = value.toObject();
        const auto id = decision.value("changeId").toString();
        if (decision.value("decision") == "Accepted" && changes.contains(id)
            && decisionMatchesChange(decision, *changes.value(id)))
            ids.insert(id);
    }
    return ids;
}

void ReviewWorkspace::markChanged()
{
    if (!m_revision.isEmpty())
        m_bundle.review["revision"] = m_revision;
    m_dirty = true;
    ++m_generation;
    if (m_projectMode)
        m_recoveryTimer.start();
    emit workspaceChanged();
}
void ReviewWorkspace::scheduleDiff()
{
    if (m_revision.isEmpty())
        return;
    if (!m_editingProposal)
    {
        rebuildPresentation();
        refreshChanges();
        return;
    }
    if (m_diffWatcher.isRunning())
    {
        m_pendingDiff = true;
        return;
    }
    m_diffTimer.start();
}
void ReviewWorkspace::startDiff()
{
    if (m_revision.isEmpty())
        return;
    if (!m_editingProposal)
        return;
    if (m_diffWatcher.isRunning())
    {
        m_pendingDiff = true;
        return;
    }
    m_diffGeneration = m_generation;
    const auto base = m_revision.value("baseSnapshot").toObject();
    const auto bundle = m_bundle;
    const auto explicitIntents = intents();
    const auto runner = m_diffRunner;
    m_diffWatcher.setFuture(QtConcurrent::run([base, bundle, explicitIntents, runner] {
        QJsonObject proposed = bundle.project.toJson();
        proposed.remove("activeDifficultyId");
        QJsonObject resources;
        for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
            resources[it.key()] = it.value();
        proposed["resources"] = resources;
        return runner ? runner(base, proposed, explicitIntents) : diff(base, proposed, explicitIntents);
    }));
}

Chart ReviewWorkspace::activeChart(const CceDocument::ProjectBundle &bundle) const
{
    Chart chart = bundle.project.compose(bundle.project.activeDifficultyId);
    auto resolved = [&](const QString &logical) {
        if (logical.isEmpty())
            return logical;
        if (bundle.resourcePaths.contains(logical))
            return QDir(resourceCacheRoot(bundle)).filePath(bundle.project.projectId + "/" + logical);
        return m_resourceSources.value(logical, QDir(resourceRoot()).filePath(logical));
    };
    chart.setAudioSourceFullPath(resolved(chart.meta().audioFile));
    // Absolute locations are runtime projections only. portableChart converts
    // them back to the bundle's logical paths before any snapshot is persisted.
    chart.meta().audioFile = resolved(chart.meta().audioFile);
    chart.meta().backgroundFile = resolved(chart.meta().backgroundFile);
    for (auto &note : chart.notes())
        if (note.isSound())
            note.sound = resolved(note.sound);
    return chart;
}

void ReviewWorkspace::restoreBundle(const CceDocument::ProjectBundle &bundle, bool projectMode)
{
    m_restoring = true;
    m_bundle = bundle;
    const auto &bindings = m_bundle.project.difficulties.value(m_bundle.project.activeDifficultyId).bindings;
    if (std::none_of(bindings.cbegin(), bindings.cend(), [this](const auto &binding) {
            return binding.layerId == m_activeLayerId;
        }))
        m_activeLayerId = bindings.isEmpty() ? QString() : bindings.first().layerId;
    m_revision = bundle.review.value("revision").toObject();
    m_editingProposal = bundle.review.value("editingProposal").toBool();
    auto base = m_revision.value("baseSnapshot").toObject();
    base["activeDifficultyId"] = bundle.project.activeDifficultyId;
    if (!base.value("difficulties").toObject().contains(bundle.project.activeDifficultyId))
        base["activeDifficultyId"] = base.value("difficulties").toObject().keys().value(0);
    if (!m_revision.isEmpty())
        CceDocument::Project::fromJson(base, m_baseProject, nullptr);
    m_changes.clear();
    for (const auto &v : m_revision.value("changes").toArray())
    {
        Change c;
        if (changeFromJson(v.toObject(), c, nullptr))
            m_changes.append(c);
    }
    if (bundle.review.value("targetProject").isObject())
    {
        m_targetBundle = bundle;
        CceDocument::Project::fromJson(bundle.review.value("targetProject").toObject(), m_targetBundle.project,
                                       nullptr);
        m_targetBundle.resourcePaths.clear();
        const auto targetPaths = bundle.review.value("targetResourcePaths").toObject();
        for (auto it = targetPaths.begin(); it != targetPaths.end(); ++it)
            m_targetBundle.resourcePaths[it.key()] = it.value().toString();
    }
    const QString root = resourceCacheRoot(bundle) + "/" + bundle.project.projectId;
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
        m_resourceSources[it.key()] = QDir(root).filePath(it.key());
    m_projectMode = projectMode;
    m_dirty = true;
    ++m_generation;
    if (m_projectMode)
        m_recoveryTimer.start();
    emit workspaceChanged();
}
bool ReviewWorkspace::applyBundle(const QString &name, const CceDocument::ProjectBundle &after, QString *error,
                                  bool preserveMode)
{
    if (displayIsReadOnly() && !m_applyingReview && snapshot(after) != snapshot())
        return failure(error, tr("Base is read only."));
    if (!after.project.validate(error))
        return false;
    const auto before = m_bundle;
    if (!after.resourcePaths.isEmpty() && !CceDocument::materializeResources(after, resourceCacheRoot(after), error))
        return false;
    if (!before.resourcePaths.isEmpty() && !CceDocument::materializeResources(before, resourceCacheRoot(before), error))
        return false;
    const Chart chart = activeChart(after);
    const QPointer<ReviewWorkspace> coordinator(this);
    if (!m_controller->applyDocumentMutation(
            name, chart,
            [coordinator, before, after, preserveMode](bool forward) {
                if (coordinator)
                    coordinator->restoreBundle(forward ? after : before,
                                                preserveMode ? coordinator->m_projectMode : true);
            },
            m_applyingReview))
        return failure(error, tr("Base is read only."));
    return true;
}

bool ReviewWorkspace::setProject(const CceDocument::Project &project, QString *error)
{
    auto after = m_bundle;
    after.project = project;
    return applyBundle(tr("Update project"), after, error);
}

bool ReviewWorkspace::importDifficultyFromPath(const QString &path, QString *error)
{
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly) || source.size() > 64 * 1024 * 1024)
        return failure(error, tr("Cannot read chart."));
    const auto document = QJsonDocument::fromJson(source.readAll());
    if (!document.isObject())
        return failure(error, tr("Cannot read chart."));
    QTemporaryDir stage;
    if (!stage.isValid())
        return failure(error, tr("Cannot read chart."));
    auto json = document.object();
    const QDir sourceDir = QFileInfo(path).dir();
    QHash<QString, QString> names;
    const auto stageResource = [&](const QString &raw, QString &name) {
        if (raw.isEmpty())
        {
            name.clear();
            return true;
        }
        const QString absolute = QDir::cleanPath(QDir::isAbsolutePath(raw) ? raw : sourceDir.filePath(raw));
        if (names.contains(absolute))
        {
            name = names.value(absolute);
            return true;
        }
        // Short names also prevent the legacy loader's long-audio-name repair
        // from touching any input resource. All its writes stay in this stage.
        const QFileInfo info(absolute);
        name = uuid().left(12) + "." + info.suffix();
        if (!info.isFile() || info.size() > 128 * 1024 * 1024 || !QFile::copy(absolute, stage.filePath(name)))
            return failure(error, tr("Missing resource: %1").arg(raw));
        names[absolute] = name;
        return true;
    };
    auto metadata = json.value("meta").toObject();
    QString name;
    if (!stageResource(metadata.value("background").toString(), name))
        return false;
    metadata["background"] = name;
    if (metadata.contains("audio"))
    {
        if (!stageResource(metadata.value("audio").toString(), name))
            return false;
        metadata["audio"] = name;
    }
    json["meta"] = metadata;
    auto notes = json.value("note").toArray();
    for (int i = 0; i < notes.size(); ++i)
    {
        auto note = notes[i].toObject();
        if (!note.value("sound").toString().isEmpty())
        {
            if (!stageResource(note.value("sound").toString(), name))
                return false;
            note["sound"] = name;
            notes[i] = note;
        }
    }
    json["note"] = notes;
    const auto stagedPath = stage.filePath("import.mc");
    QFile staged(stagedPath);
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (!staged.open(QIODevice::WriteOnly) || staged.write(bytes) != bytes.size())
        return failure(error, tr("Cannot read chart."));
    staged.close();
    Chart chart;
    if (!ChartIO::load(stagedPath, chart, false))
        return failure(error, tr("Cannot read chart."));
    const auto oldSources = m_resourceSources;
    const auto oldPath = m_lastChartPath;
    m_lastChartPath = stagedPath;
    chart = portableChart(chart);
    m_lastChartPath = oldPath;
    auto after = m_bundle;
    const QString id = after.project.addDifficulty(chart.meta().difficulty, chart.meta(), chart.bpmList());
    const QString layer = after.project.addLayer(id, tr("Base"));
    after.project.layers[layer].notes = chart.notes();
    after.project.activeDifficultyId = id;
    if (!collectResources(after, error) || !applyBundle(tr("Import difficulty"), after, error))
    {
        m_resourceSources = oldSources;
        return false;
    }
    return true;
}

bool ReviewWorkspace::collectResources(CceDocument::ProjectBundle &bundle, QString *error) const
{
    return collectBundleResources(bundle, m_resourceSources, resourceRoot(), error);
}

bool ReviewWorkspace::saveProject(const QString &path, QString *error)
{
    auto bundle = m_bundle;
    auto revision = m_revision;
    if (!revision.isEmpty())
    {
        if (!prepareRevision(revision, bundle, error))
            return false;
        bundle.review["revision"] = revision;
        bundle.review["comments"] = revision.value("comments");
        bundle.review["events"] = revision.value("events");
        bundle.review["reviewDecisions"] = revision.value("reviewDecisions");
    }
    else if (!collectResources(bundle, error))
        return false;
    bundle.review["workingIntents"] = encodeChanges(intents());
    if (!CceDocument::writeProject(path, bundle, error))
        return false;
    m_bundle = bundle;
    m_revision = revision;
    ++m_generation;
    if (!revision.isEmpty())
    {
        m_changes.clear();
        for (const auto &value : revision.value("changes").toArray())
        {
            Change change;
            if (changeFromJson(value.toObject(), change, nullptr))
                m_changes.append(change);
        }
    }
    m_projectPath = path;
    m_projectMode = true;
    m_dirty = false;
    report(tr("Project saved."));
    refreshUi();
    scheduleDiff();
    return true;
}

bool ReviewWorkspace::openProject(const QString &path, QString *error)
{
    CceDocument::ProjectBundle bundle;
    if (!CceDocument::readProject(path, bundle, error))
        return false;
    const auto revision = bundle.review.value("revision").toObject();
    if (!revision.isEmpty() && !validateRevision(revision, error))
        return false;
    const QString root = resourceCacheRoot(bundle);
    if (!CceDocument::materializeResources(bundle, root, error))
        return false;
    m_controller->setReadOnly(false);
    setViewMode(0);
    if (!applyBundle(tr("Open project"), bundle, error))
        return false;
    m_controller->clearUndoHistory();
    m_resourceSources.clear();
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
        m_resourceSources[it.key()] = QDir(root).filePath(bundle.project.projectId + "/" + it.key());
    m_projectPath = path;
    m_projectMode = true;
    m_dirty = false;
    m_revision = revision;
    if (m_editingProposal)
    {
        QVector<Change> savedIntents = intents();
        for (const auto &value : bundle.review.value("workingIntents").toArray())
        {
            Change change;
            if (changeFromJson(value.toObject(), change, nullptr))
                savedIntents.append(change);
        }
        m_changes = diff(m_revision.value("baseSnapshot").toObject(), snapshot(), savedIntents);
    }
    initializeProposalSources();
    refreshUi();
    scheduleDiff();
    emit projectOpened(path);
    return true;
}

void ReviewWorkspace::persistRecovery()
{
    if (!m_projectMode || !m_dirty)
        return;
    if (m_recoveryWatcher.isRunning())
    {
        m_pendingRecovery = true;
        return;
    }
    auto bundle = m_bundle;
    bundle.review["revision"] = m_revision;
    bundle.review["workingIntents"] = encodeChanges(intents());
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/local-review";
    QDir().mkpath(dir);
    const auto sources = m_resourceSources;
    const auto root = resourceRoot();
    m_recoveryWatcher.setFuture(QtConcurrent::run([bundle, sources, root, dir]() mutable {
        QString error;
        if (collectBundleResources(bundle, sources, root, &error))
            CceDocument::writeProject(dir + "/recovery.cceproj", bundle, &error);
        return error;
    }));
}

void ReviewWorkspace::addEvent(const QString &type, const QJsonObject &detail)
{
    auto list = events();
    list.append(QJsonObject{{"eventId", uuid()},
                            {"type", type},
                            {"author", m_identity.activeIdentity().snapshot()},
                            {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                            {"detail", detail}});
    m_bundle.review["events"] = list;
}

void ReviewWorkspace::commitReviewState(const QString &name, const QJsonArray &entries, const QJsonArray &log)
{
    auto after = m_bundle;
    after.review["comments"] = entries;
    after.review["events"] = log;
    after.review["revision"] = m_revision;
    QString error;
    if (!applyBundle(name, after, &error, true))
        report(error);
}

QJsonObject ReviewWorkspace::chartReviewState() const
{
    if (m_projectMode)
        return {};
    const auto &difficulty = m_bundle.project.difficulties.value(m_bundle.project.activeDifficultyId);
    return {{"version", 1}, {"difficultyId", difficulty.difficultyId},
            {"layerId", difficulty.bindings.isEmpty() ? QString() : difficulty.bindings.first().layerId},
            {"review", m_bundle.review}};
}

bool ReviewWorkspace::writeChartReview(const QString &chartPath, const QJsonObject &state, QString *error)
{
    if (state.isEmpty())
        return true;
    const auto stem = QFileInfo(chartPath).completeBaseName();
    const auto folder = QFileInfo(chartPath).absoluteDir().filePath(".mcce-plugin");
    const auto path = QDir(folder).filePath(stem + ".review.json");
    if (QFileInfo::exists(path))
    {
        QFile existing(path);
        if (!existing.open(QIODevice::ReadOnly) || existing.size() > 32 * 1024 * 1024
            || QJsonDocument::fromJson(existing.readAll()).object().value("version").toInt() != 1)
            return failure(error, QObject::tr("Cannot overwrite an unreadable chart comments file: %1").arg(path));
    }
    if (state.value("review").toObject().value("comments").toArray().isEmpty() && !QFileInfo::exists(path))
        return true;
    if (!QDir().mkpath(folder))
        return failure(error, QObject::tr("Cannot create the chart comments folder."));
    QSaveFile file(path);
    const auto bytes = QJsonDocument(state).toJson(QJsonDocument::Indented);
    if (bytes.size() > 32 * 1024 * 1024)
        return failure(error, QObject::tr("Chart comments exceed the supported file size."));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return failure(error, QObject::tr("Cannot save chart comments: %1").arg(path));
    return true;
}

void ReviewWorkspace::loadChartReview()
{
    const auto stem = QFileInfo(m_lastChartPath).completeBaseName();
    const auto path = QFileInfo(m_lastChartPath).absoluteDir().filePath(".mcce-plugin/" + stem + ".review.json");
    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
    {
        report(tr("Cannot read chart comments: %1").arg(path));
        return;
    }
    const auto state = QJsonDocument::fromJson(file.readAll()).object();
    const auto review = state.value("review").toObject();
    if (state.value("version").toInt() != 1 || !review.value("comments").isArray())
    {
        report(tr("Invalid chart comments file: %1").arg(path));
        return;
    }
    const auto difficulty = m_bundle.project.difficulties.value(m_bundle.project.activeDifficultyId);
    const auto layer = difficulty.bindings.isEmpty() ? QString() : difficulty.bindings.first().layerId;
    QJsonArray entries;
    for (const auto &value : review.value("comments").toArray())
    {
        auto comment = value.toObject();
        auto anchor = comment.value("anchor").toObject();
        if (anchor.value("difficultyId") == state.value("difficultyId"))
            anchor["difficultyId"] = difficulty.difficultyId;
        QJsonArray targets;
        for (const auto &targetValue : anchor.value("targets").toArray())
        {
            auto target = targetValue.toObject();
            if (target.value("layerId") == state.value("layerId"))
                target["layerId"] = layer;
            targets.append(target);
        }
        if (anchor.contains("targets"))
            anchor["targets"] = targets;
        comment["anchor"] = anchor;
        entries.append(comment);
    }
    m_bundle.review = review;
    m_bundle.review["comments"] = relocateComments(entries, m_bundle.project, m_bundle.project);
}

bool ReviewWorkspace::startProposal(const QString &description, QString *error)
{
    if (m_identity.activeIdentity().authorId.isEmpty())
        return failure(error, tr("Create or select a local identity first."));
    auto bundle = m_bundle;
    if (!collectResources(bundle, error))
        return false;
    m_bundle = bundle;
    m_baseProject = bundle.project;
    m_targetBundle = bundle;
    m_editingProposal = true;
    m_bundle.review["editingProposal"] = true;
    m_bundle.review["targetProject"] = m_targetBundle.project.toJson();
    QJsonObject targetPaths;
    for (auto it = m_targetBundle.resourcePaths.cbegin(); it != m_targetBundle.resourcePaths.cend(); ++it)
        targetPaths[it.key()] = it.value();
    m_bundle.review["targetResourcePaths"] = targetPaths;
    m_revision = createRevision(snapshot(), snapshot(), m_identity.activeIdentity().snapshot(), description);
    m_sources.clear();
    m_intentMap.clear();
    m_intents.clear();
    for (auto it = m_bundle.project.layers.cbegin(); it != m_bundle.project.layers.cend(); ++it)
        for (const Note &note : it->notes)
            m_sources[note.id] = {CceDocument::encodeNote(note), it.key(), uuid(), QString(), false};
    m_projectMode = true;
    addEvent("CreateProposal");
    markChanged();
    setViewMode(1);
    refreshUi();
    scheduleDiff();
    return true;
}

bool ReviewWorkspace::prepareRevision(QJsonObject &revision, CceDocument::ProjectBundle &bundle, QString *error) const
{
    if (m_revision.isEmpty())
        return failure(error, tr("Start or import a proposal first."));
    if (m_identity.activeIdentity().authorId.isEmpty())
        return failure(error, tr("Select a local identity first."));
    bundle = m_bundle;
    if (!collectResources(bundle, error))
        return false;
    revision = m_revision;
    QJsonObject resources;
    for (auto it = bundle.resources.cbegin(); it != bundle.resources.cend(); ++it)
    {
        QStringList paths;
        for (auto p = bundle.resourcePaths.cbegin(); p != bundle.resourcePaths.cend(); ++p)
            if (p.value() == it.key())
                paths.append(p.key());
        resources[it.key()] =
            QJsonObject{{"dataBase64", QString::fromLatin1(it.value().toBase64())}, {"paths", jsonStrings(paths)}};
    }
    const QJsonObject reviewState{{"comments", comments()},
                                  {"events", events()},
                                  {"reviewDecisions", m_bundle.review.value("reviewDecisions").toArray()},
                                  {"resources", resources}};
    QJsonObject proposed = snapshot(bundle);
    if (!m_editingProposal)
    {
        QVector<Change> changes;
        for (const auto &v : m_revision.value("changes").toArray())
        {
            Change c;
            if (!changeFromJson(v.toObject(), c, error))
                return false;
            changes.append(c);
        }
        auto rebuilt =
            merge(m_revision.value("baseSnapshot").toObject(), m_revision.value("baseSnapshot").toObject(), changes);
        if (!rebuilt.committed)
            return failure(error, tr("Cannot reconstruct the proposal."));
        proposed = rebuilt.result;
    }
    return updateRevision(revision, proposed, m_identity.activeIdentity().snapshot(),
                          m_editingProposal ? intents() : QVector<Change>{}, error, reviewState);
}

bool ReviewWorkspace::exportProposal(const QString &path, QString *error)
{
    CceDocument::ProjectBundle bundle;
    QJsonObject revision;
    if (!prepareRevision(revision, bundle, error) || !writePackage(path, revision, error))
        return false;
    m_bundle = bundle;
    m_revision = revision;
    m_bundle.review["revision"] = revision;
    m_bundle.review["comments"] = revision.value("comments");
    m_bundle.review["events"] = revision.value("events");
    m_bundle.review["reviewDecisions"] = revision.value("reviewDecisions");
    m_changes.clear();
    for (const auto &value : revision.value("changes").toArray())
    {
        Change c;
        if (changeFromJson(value.toObject(), c, nullptr))
            m_changes.append(c);
    }
    markChanged();
    refreshUi();
    rebuildPresentation();
    report(tr("Proposal exported."));
    return true;
}

bool ReviewWorkspace::importProposal(const QString &path, QString *error)
{
    QJsonObject incoming;
    if (!readPackage(path, incoming, error))
        return false;
    QJsonObject merged = incoming;
    QStringList conflicts;
    QString localRevisionId;
    const bool sameProposal = !m_revision.isEmpty() && m_revision.value("proposalId") == incoming.value("proposalId");
    if (sameProposal)
    {
        QJsonObject local;
        CceDocument::ProjectBundle localBundle;
        if (!prepareRevision(local, localBundle, error))
            return false;
        localRevisionId = local.value("revisionId").toString();
        merged = mergeRevisions(local, incoming, &conflicts, error);
        if (merged.isEmpty())
            return false;
    }
    auto after = m_bundle;
    QJsonObject baseJson = merged.value("baseSnapshot").toObject();
    const auto baseDifficulties = baseJson.value("difficulties").toObject();
    const QString baseActive = baseDifficulties.contains(after.project.activeDifficultyId)
                                   ? after.project.activeDifficultyId
                                   : baseDifficulties.keys().value(0);
    baseJson["activeDifficultyId"] = baseActive;
    CceDocument::Project baseProject;
    if (!CceDocument::Project::fromJson(baseJson, baseProject, error))
        return false;
    if (after.project.projectId != baseProject.projectId)
    {
        // A standard .mc has no CCE project/layer IDs. Associate its editable
        // content with the proposal's single-difficulty baseline, never Note IDs.
        if (after.project.difficulties.size() != 1 || after.project.layers.size() != 1
            || baseProject.difficulties.size() != 1 || baseProject.layers.size() != 1)
            return failure(error, tr("Open the corresponding CCE project before importing this proposal."));
        const auto currentChart = after.project.compose(after.project.activeDifficultyId);
        const auto baseChart = baseProject.compose(baseActive);
        auto checked = after;
        const bool resourcesKnown = collectResources(checked, nullptr);
        const QString currentAudio = checked.project.difficulties.value(checked.project.activeDifficultyId).oggHash;
        const QString baseAudio = baseProject.difficulties.value(baseActive).oggHash;
        const bool exactChart = canonicalHash(CceDocument::encodeChart(currentChart, true))
                                == canonicalHash(CceDocument::encodeChart(baseChart, true));
        const bool sameAudio = resourcesKnown && !currentAudio.isEmpty() && currentAudio == baseAudio;
        if (!exactChart && !sameAudio)
            return failure(error, tr("This proposal's base does not match the current chart or audio."));
        after.project = baseProject;
        after.project.layers.begin()->notes = currentChart.notes();
        auto &difficulty = after.project.difficulties[baseActive];
        difficulty.metadata = currentChart.meta();
        difficulty.timing = currentChart.bpmList();
        after.resourcePaths = checked.resourcePaths;
        after.resources = checked.resources;
    }
    const auto resources = merged.value("resources").toObject();
    for (auto it = resources.begin(); it != resources.end(); ++it)
    {
        const auto entry = it.value().toObject();
        after.resources[it.key()] = QByteArray::fromBase64(entry.value("dataBase64").toString().toLatin1());
        // Keep imported resource mappings inside the proposal snapshot until Apply.
    }
    after.review["revision"] = merged;
    after.review["comments"] = sameProposal ? merged.value("comments").toArray()
                                            : mergeComments(comments(), merged.value("comments").toArray(), &conflicts);
    after.review["events"] = merged.value("events").toArray();
    auto log = after.review.value("events").toArray();
    const QString incomingId = incoming.value("revisionId").toString();
    bool alreadyImported = false;
    for (const auto &value : log)
    {
        const auto event = value.toObject();
        alreadyImported |= event.value("type") == "ImportRevision" && event.value("revisionId") == incomingId;
    }
    const auto event = [this](const QString &type, const QString &id) {
        return QJsonObject{{"eventId", uuid()},
                           {"type", type},
                           {"revisionId", id},
                           {"author", m_identity.activeIdentity().snapshot()},
                           {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    };
    if (sameProposal && merged.value("revisionId").toString() != localRevisionId
        && merged.value("revisionId") != incoming.value("revisionId"))
        log.append(event("MergeRevision", merged.value("revisionId").toString()));
    if (!alreadyImported)
        log.append(event("ImportRevision", incomingId));
    after.review["events"] = log;
    after.review["reviewDecisions"] = merged.value("reviewDecisions").toArray();
    after.review["editingProposal"] = false;
    // Import changes the review session only; the current chart is untouched.
    if (!applyBundle(tr("Import proposal revision"), after, error))
        return false;
    m_revision = merged;
    m_projectMode = true;
    m_editingProposal = false;
    m_changes.clear();
    for (const auto &value : merged.value("changes").toArray())
    {
        Change c;
        if (changeFromJson(value.toObject(), c, error))
            m_changes.append(c);
        else
            return false;
    }
    m_baseProject = baseProject;
    if (!conflicts.isEmpty())
        report(tr("Revision conflicts require review: %1").arg(conflicts.join(", ")));
    rebuildPresentation();
    refreshUi();
    return true;
}

bool ReviewWorkspace::applyChanges(const QSet<QString> &selectedIds, QString *error,
                                   const QHash<QString, QJsonValue> &resolutions)
{
    if (selectedIds.isEmpty())
        return failure(error, tr("Select at least one change to apply."));
    const auto changes = currentChanges();
    auto ids = selectedIds;
    QSet<QString> knownIds;
    for (const auto &change : changes)
        knownIds.insert(change.changeId);
    if (!knownIds.contains(ids))
        return failure(error, tr("Select at least one change to apply."));
    const auto required = dependencySelection(changes, ids);
    // File payloads may accompany an explicit edit. Other chart edits require
    // explicit selection through Group Review, never implicit approval.
    for (const auto &change : changes)
        if (required.contains(change.changeId) && change.domain == "Resource")
            ids.insert(change.changeId);
    if (dependencySelection(changes, ids) != ids)
        return failure(error, tr("Select the dependency group before applying these changes."));
    const auto target = m_editingProposal ? m_targetBundle : m_bundle;
    const auto merged = merge(m_revision.value("baseSnapshot").toObject(), snapshot(target), changes, ids, resolutions);
    if (!merged.committed)
        return failure(error, tr("Resolve the selected conflicts before applying."));
    auto json = merged.result;
    json["activeDifficultyId"] = target.project.activeDifficultyId;
    auto after = target;
    after.resources = m_bundle.resources;
    if (!CceDocument::Project::fromJson(json, after.project, error))
        return false;
    const auto resources = json.value("resources").toObject();
    after.resourcePaths.clear();
    for (auto it = resources.begin(); it != resources.end(); ++it)
        after.resourcePaths[it.key()] = it.value().toString();
    // A selected object/metadata edit can require a new file. Carry that file's
    // proposed mapping into the same transaction; never publish a dangling path.
    CceDocument::ProjectBundle proposal;
    if (!proposalBundle(proposal, error))
        return false;
    if (m_editingProposal && !collectResources(proposal, error))
        return false;
    after.resources = proposal.resources;
    QSet<QString> dependencyPaths;
    for (const auto &path : CceDocument::requiredResourcePaths(after.project))
        if (!after.resourcePaths.contains(path))
        {
            if (!proposal.resourcePaths.contains(path))
                return failure(error, tr("Missing resource: %1").arg(path));
            after.resourcePaths[path] = proposal.resourcePaths.value(path);
            dependencyPaths.insert(path);
        }
    for (auto it = after.project.difficulties.begin(); it != after.project.difficulties.end(); ++it)
        it->oggHash = after.resourcePaths.value(it->metadata.audioFile);
    const QString root = resourceCacheRoot(after);
    if (!CceDocument::materializeResources(after, root, error))
        return false;
    QVector<Change> selectedChanges;
    for (const auto &c : changes)
        if (ids.contains(c.changeId))
        {
            auto applied = c;
            if (resolutions.contains(c.changeId))
                applied.before = resolutions.value(c.changeId);
            selectedChanges.append(applied);
        }
    after.review["comments"] = relocateComments(comments(), target.project, after.project, selectedChanges);
    QSet<QString> dependencyChangePaths;
    for (const auto &path : dependencyPaths)
        dependencyChangePaths.insert("/resources/" + pointerPart(path));
    QJsonArray applied;
    QSet<QString> replacedApplyStates = ids;
    for (const auto &change : changes)
        if (change.domain == "Resource" && dependencyChangePaths.contains(change.path))
            replacedApplyStates.insert(change.changeId);
    for (const auto &value : m_bundle.review.value("applyStates").toArray())
        if (!replacedApplyStates.contains(value.toObject().value("changeId").toString()))
            applied.append(value);
    for (const auto &item : merged.items)
        if (ids.contains(item.change.changeId))
            applied.append(QJsonObject{{"changeId", item.change.changeId}, {"state", item.state}});
    for (const auto &change : changes)
        if (change.domain == "Resource" && dependencyChangePaths.contains(change.path))
            applied.append(QJsonObject{{"changeId", change.changeId}, {"state", "Applied"}});
    after.review["applyStates"] = applied;
    auto decisions = m_bundle.review.value("reviewDecisions").toArray();
    QHash<QString, QString> resolutionHashes;
    for (const auto &change : changes)
        if (resolutions.contains(change.changeId))
            resolutionHashes[change.changeId] = changeFingerprint(change);
    QJsonArray remaining;
    for (const auto &v : decisions)
        if (!resolutions.contains(v.toObject().value("changeId").toString()))
            remaining.append(v);
    for (auto it = resolutions.cbegin(); it != resolutions.cend(); ++it)
    {
        remaining.append(QJsonObject{{"changeId", it.key()},
                                     {"changeHash", resolutionHashes.value(it.key())},
                                     {"decision", "Accepted"},
                                     {"resolutionValue", it.value()},
                                     {"author", m_identity.activeIdentity().snapshot()}});
    }
    after.review["reviewDecisions"] = remaining;
    after.review["revision"] = m_revision;
    auto log = events();
    log.append(QJsonObject{{"eventId", uuid()},
                           {"type", "ApplyChange"},
                           {"author", m_identity.activeIdentity().snapshot()},
                           {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    after.review["events"] = log;
    after.review["editingProposal"] = false;
    const bool wasReadOnly = m_controller->isReadOnly();
    m_controller->setReadOnly(false);
    m_applyingReview = true;
    const bool committed = applyBundle(tr("Apply reviewed changes"), after, error);
    m_applyingReview = false;
    if (!committed)
    {
        m_controller->setReadOnly(wasReadOnly);
        return false;
    }
    for (auto it = after.resourcePaths.cbegin(); it != after.resourcePaths.cend(); ++it)
        m_resourceSources[it.key()] = QDir(root).filePath(after.project.projectId + "/" + it.key());
    report(tr("Reviewed changes applied as one undoable operation."));
    return true;
}

bool ReviewWorkspace::rebase(QString *error)
{
    if (m_revision.isEmpty())
        return failure(error, tr("No proposal to rebase."));
    const auto target = m_editingProposal ? m_targetBundle : m_bundle;
    CceDocument::ProjectBundle oldProposal;
    if (!proposalBundle(oldProposal, error))
        return false;
    const auto merged = merge(m_revision.value("baseSnapshot").toObject(), snapshot(target), m_changes);
    if (!merged.committed)
        return failure(error, tr("Resolve conflicts before rebasing."));
    auto next = m_revision;
    if (!rebaseRevision(next, snapshot(target), merged.result, m_identity.activeIdentity().snapshot(), intents(),
                        error))
        return false;
    auto after = m_bundle;
    auto proposalJson = merged.result;
    proposalJson["activeDifficultyId"] = after.project.activeDifficultyId;
    CceDocument::Project newProposal;
    if (!CceDocument::Project::fromJson(proposalJson, newProposal, error))
        return false;
    next["comments"] = relocateComments(comments(), oldProposal.project, newProposal);
    after.review["revision"] = next;
    after.review["events"] = next.value("events");
    after.review["comments"] = next.value("comments");
    after.review["reviewDecisions"] = next.value("reviewDecisions");
    if (m_editingProposal)
    {
        auto json = merged.result;
        json["activeDifficultyId"] = after.project.activeDifficultyId;
        if (!CceDocument::Project::fromJson(json, after.project, error))
            return false;
        after.review["targetProject"] = target.project.toJson();
    }
    if (!applyBundle(tr("Rebase proposal"), after, error))
        return false;
    m_revision = next;
    m_baseProject = target.project;
    initializeProposalSources();
    markChanged();
    scheduleDiff();
    return true;
}

bool ReviewWorkspace::proposalBundle(CceDocument::ProjectBundle &bundle, QString *error) const
{
    bundle = m_bundle;
    if (m_editingProposal)
        return true;
    const auto base = m_revision.value("baseSnapshot").toObject();
    const auto result = merge(base, base, m_changes);
    if (!result.committed)
        return failure(error, tr("Cannot reconstruct the proposal."));
    auto json = result.result;
    const auto difficulties = json.value("difficulties").toObject();
    json["activeDifficultyId"] = difficulties.contains(m_bundle.project.activeDifficultyId)
                                     ? m_bundle.project.activeDifficultyId
                                     : difficulties.keys().value(0);
    if (!CceDocument::Project::fromJson(json, bundle.project, error))
        return false;
    bundle.resourcePaths.clear();
    const auto paths = json.value("resources").toObject();
    for (auto it = paths.begin(); it != paths.end(); ++it)
        bundle.resourcePaths[it.key()] = it.value().toString();
    return true;
}

bool ReviewWorkspace::saveProposalAsDifficulty(QString *error)
{
    if (m_revision.isEmpty())
        return failure(error, tr("No proposal to save."));
    CceDocument::ProjectBundle proposal;
    if (!proposalBundle(proposal, error) || (m_editingProposal && !collectResources(proposal, error)))
        return false;
    auto after = m_editingProposal ? m_targetBundle : m_bundle;
    for (auto it = proposal.resources.cbegin(); it != proposal.resources.cend(); ++it)
        after.resources[it.key()] = it.value();
    // A proposal may replace a file at the same logical path. Give its copy a
    // new alias so existing difficulties retain their original resource bytes.
    QHash<QString, QString> aliases;
    for (auto it = proposal.resourcePaths.cbegin(); it != proposal.resourcePaths.cend(); ++it)
    {
        QString path = it.key();
        if (after.resourcePaths.contains(path) && after.resourcePaths.value(path) != it.value())
        {
            const QFileInfo info(path);
            const QString prefix = info.path() == "." ? QString() : info.path() + "/";
            path = prefix + it.value().left(12) + "_" + info.fileName();
            while (after.resourcePaths.contains(path) && after.resourcePaths.value(path) != it.value())
                path = prefix + uuid() + "_" + info.fileName();
        }
        aliases[it.key()] = path;
        after.resourcePaths[path] = it.value();
    }
    auto source = proposal.project.difficulties.value(proposal.project.activeDifficultyId);
    source.difficultyId = uuid();
    source.name += tr(" (proposal)");
    source.metadata.audioFile = aliases.value(source.metadata.audioFile, source.metadata.audioFile);
    source.metadata.backgroundFile = aliases.value(source.metadata.backgroundFile, source.metadata.backgroundFile);
    source.oggHash = after.resourcePaths.value(source.metadata.audioFile);
    for (auto &binding : source.bindings)
    {
        auto layer = proposal.project.layers.value(binding.layerId);
        layer.layerId = uuid();
        for (auto &note : layer.notes)
        {
            note.id = Note::generateId();
            if (note.isSound())
                note.sound = aliases.value(note.sound, note.sound);
        }
        after.project.layers[layer.layerId] = layer;
        binding.layerId = layer.layerId;
    }
    // References remain live only when their source exists with compatible audio.
    source.references.erase(std::remove_if(source.references.begin(), source.references.end(),
                                           [&](const auto &ref) {
                                               return !after.project.difficulties.contains(ref.difficultyId)
                                                      || after.project.difficulties.value(ref.difficultyId).oggHash
                                                             != source.oggHash;
                                           }),
                            source.references.end());
    after.project.difficulties[source.difficultyId] = source;
    after.project.activeDifficultyId = source.difficultyId;
    after.review = {}; // Ordinary difficulty, without proposal history or comments.
    const bool wasReadOnly = m_controller->isReadOnly();
    m_controller->setReadOnly(false);
    m_applyingReview = true;
    const bool committed = applyBundle(tr("Save proposal as difficulty"), after, error);
    m_applyingReview = false;
    if (!committed)
        m_controller->setReadOnly(wasReadOnly);
    else
        setViewMode(0);
    return committed;
}

void ReviewWorkspace::initializeProposalSources()
{
    m_sources.clear();
    m_intentMap.clear();
    m_intents.clear();
    if (!m_editingProposal)
        return;
    QHash<QString, QVector<Change>> byAfter;
    for (const auto &c : m_changes)
        if (c.domain == "Note" && !c.after.isUndefined())
            byAfter[c.path + contentKey(c.after.toObject())].append(c);
    for (auto it = m_bundle.project.layers.cbegin(); it != m_bundle.project.layers.cend(); ++it)
        for (const auto &note : it->notes)
        {
            const auto encoded = CceDocument::encodeNote(note);
            IntentSource source{encoded, it.key(), uuid(), {}, false};
            auto &matches = byAfter[notePath(it.key()) + contentKey(encoded)];
            if (!matches.isEmpty())
            {
                const auto c = matches.takeLast();
                source.before = c.before.toObject();
                source.added = c.before.isUndefined();
                source.changeId = c.changeId;
                source.groupId = c.operationGroupId;
                m_intentMap[c.changeId] = c;
            }
            m_sources[note.id] = source;
        }
}

void ReviewWorkspace::report(const QString &message)
{
    if (m_statusLabel)
        m_statusLabel->setText(message);
    emit statusMessage(message);
}
} // namespace CceReview
