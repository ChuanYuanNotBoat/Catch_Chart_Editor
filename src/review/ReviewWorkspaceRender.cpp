#include "ReviewWorkspace.h"
#include "CommentModel.h"

#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "document/DocumentModel.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/review/NoteInspector.h"
#include "utils/MathUtils.h"

#include <QEvent>
#include <QComboBox>
#include <QFontMetrics>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSettings>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QtMath>

#include <algorithm>

namespace CceReview
{
namespace
{
constexpr int ProposalOnly = 0;
constexpr int DiffOverlay = 1;
constexpr int BaseOnly = 2;
constexpr qreal CommentCardWidth = 208.0;
constexpr qreal CommentCardHeight = 54.0;
constexpr qreal MaxCommentOffset = 180.0;

double beatValue(const QJsonValue &value, double fallback = 0.0)
{
    const QJsonArray beat = value.toArray();
    if (beat.size() != 3 || beat.at(2).toInt() <= 0)
        return fallback;
    return beat.at(0).toDouble() + beat.at(1).toDouble() / beat.at(2).toDouble();
}

QString layerFromPath(const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 3 || parts.at(0) != QStringLiteral("layers") || parts.at(2) != QStringLiteral("notes"))
        return {};
    QString layer = parts.at(1);
    layer.replace(QStringLiteral("~1"), QStringLiteral("/"));
    layer.replace(QStringLiteral("~0"), QStringLiteral("~"));
    return layer;
}

bool decodeNoteValue(const QJsonValue &value, Note *note)
{
    return value.isObject() && CceDocument::decodeNote(value.toObject(), *note, nullptr);
}

QColor changeColor(const QString &kind, bool before)
{
    if (kind == QStringLiteral("Conflict"))
        return QColor(255, 193, 7, 220);
    return before ? QColor(235, 72, 72, 220) : QColor(55, 210, 115, 220);
}

QVector<Change> decodeRevisionChanges(const QJsonObject &revision)
{
    QVector<Change> changes;
    const QJsonArray values = revision.value(QStringLiteral("changes")).toArray();
    changes.reserve(values.size());
    for (const QJsonValue &value : values)
    {
        Change change;
        if (value.isObject() && changeFromJson(value.toObject(), change, nullptr))
            changes.append(change);
    }
    return changes;
}

CceDocument::Project proposalFromRevision(const QJsonObject &revision, const QVector<Change> &loadedChanges,
                                          const CceDocument::Project &fallback)
{
    CceDocument::Project proposed;
    const QJsonObject base = revision.value(QStringLiteral("baseSnapshot")).toObject();
    if (base.isEmpty())
        return fallback;
    const QVector<Change> changes = loadedChanges.isEmpty() ? decodeRevisionChanges(revision) : loadedChanges;
    const MergeResult merged = merge(base, base, changes);
    QJsonObject mergedProject = merged.result;
    QString activeId = fallback.activeDifficultyId;
    if (activeId.isEmpty() || !base.value(QStringLiteral("difficulties")).toObject().contains(activeId))
        activeId = base.value(QStringLiteral("difficulties")).toObject().keys().value(0);
    if (!activeId.isEmpty())
        mergedProject.insert(QStringLiteral("activeDifficultyId"), activeId);
    if (!merged.committed || !CceDocument::Project::fromJson(mergedProject, proposed, nullptr))
        return fallback;
    if (proposed.difficulties.contains(fallback.activeDifficultyId))
        proposed.activeDifficultyId = fallback.activeDifficultyId;
    else if (proposed.difficulties.contains(revision.value(QStringLiteral("activeDifficultyId")).toString()))
        proposed.activeDifficultyId = revision.value(QStringLiteral("activeDifficultyId")).toString();
    return proposed;
}

Chart chartForProject(const CceDocument::Project &project)
{
    if (!project.difficulties.contains(project.activeDifficultyId))
        return {};
    return project.compose(project.activeDifficultyId);
}

double referenceBeat(int whole, int numerator, int denominator, const QVector<MathUtils::BpmCacheEntry> &source,
                     const QVector<MathUtils::BpmCacheEntry> &target)
{
    if (source.isEmpty() || target.isEmpty())
        return 0;
    const double time = MathUtils::beatToMs(whole, numerator, denominator, source);
    if (time < target.first().accumulatedMs)
        return 0;
    auto entry = std::upper_bound(target.cbegin(), target.cend(), time, [](double ms, const auto &point) {
        return ms < point.accumulatedMs;
    });
    if (entry != target.cbegin())
        --entry;
    // Reference coordinates are a display projection, never authored triplets.
    // Keeping doubles avoids an expensive rational search for every reference.
    return entry->beatPos + (time - entry->accumulatedMs) * entry->bpm / 60000.;
}

QString commentThreadId(const QJsonObject &comment)
{
    QString thread = comment.value(QStringLiteral("threadId")).toString();
    if (thread.isEmpty())
        thread = comment.value(QStringLiteral("commentId")).toString();
    return thread;
}

QPointF persistedCommentOffset(const QString &threadId)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("local-review/commentOffsets"));
    const QVariant value = settings.value(threadId);
    settings.endGroup();
    return value.isValid() ? value.toPointF() : QPointF();
}

QPointF boundedOffset(QPointF offset)
{
    offset.setX(qBound(-MaxCommentOffset, offset.x(), MaxCommentOffset));
    offset.setY(qBound(-MaxCommentOffset, offset.y(), MaxCommentOffset));
    return offset;
}
} // namespace

void ReviewWorkspace::rebuildPresentation()
{
    if (!m_canvas || !m_controller)
        return;

    const bool hasProposal = !m_revision.isEmpty();
    const bool baseView = hasProposal && (m_viewMode == BaseOnly || m_peeking);
    CceDocument::Project base = m_baseProject;
    if (!m_revision.isEmpty() && base.difficulties.isEmpty())
    {
        QJsonObject snapshot = m_revision.value(QStringLiteral("baseSnapshot")).toObject();
        QString baseDifficulty = m_bundle.project.activeDifficultyId;
        if (!snapshot.value(QStringLiteral("difficulties")).toObject().contains(baseDifficulty))
            baseDifficulty = snapshot.value(QStringLiteral("difficulties")).toObject().keys().value(0);
        if (!baseDifficulty.isEmpty())
            snapshot.insert(QStringLiteral("activeDifficultyId"), baseDifficulty);
        CceDocument::Project::fromJson(snapshot, base, nullptr);
        if (base.difficulties.contains(m_bundle.project.activeDifficultyId))
            base.activeDifficultyId = m_bundle.project.activeDifficultyId;
    }
    if (base.difficulties.isEmpty())
        base = m_bundle.project;

    const CceDocument::Project proposed = m_editingProposal || !hasProposal
                                              ? m_bundle.project
                                              : proposalFromRevision(m_revision, m_changes, m_bundle.project);
    const CceDocument::Project &displayProject = baseView ? base : (!m_editingProposal ? proposed : m_bundle.project);
    const bool useReadOnlyChart = hasProposal && (baseView || !m_editingProposal);

    if (useReadOnlyChart)
    {
        m_presentationChart = chartForProject(displayProject);
        m_canvas->setDisplayChart(&m_presentationChart);
    }
    else
    {
        m_canvas->setDisplayChart(nullptr);
    }
    const bool readOnly = useReadOnlyChart;
    if (m_selection)
        m_selection->setNotes(useReadOnlyChart ? &m_presentationChart.notes() : &m_controller->chart()->notes(),
                              m_generation + 1);
    m_controller->setReadOnly(readOnly);
    if (m_inspector)
    {
        m_inspector->setDisplayNotes(useReadOnlyChart ? &m_presentationChart.notes() : nullptr);
        m_inspector->setReadOnly(readOnly);
    }

    m_owner.clear();
    QHash<QString, qreal> opacityByRuntimeId;
    if (displayProject.difficulties.contains(displayProject.activeDifficultyId))
    {
        const auto &difficulty = displayProject.difficulties[displayProject.activeDifficultyId];
        for (const CceDocument::LayerBinding &binding : difficulty.bindings)
        {
            const auto layer = displayProject.layers.constFind(binding.layerId);
            if (layer == displayProject.layers.cend())
                continue;
            const qreal opacity = binding.visible ? qBound(0.0, binding.opacity, 1.0) : 0.0;
            for (const Note &note : layer->notes)
            {
                m_owner.insert(note.id, binding.layerId);
                opacityByRuntimeId.insert(note.id, opacity);
            }
        }
    }
    m_canvas->setLayerPresentation(opacityByRuntimeId);

    m_overlayNotes.clear();
    m_moveBounds.clear();
    QHash<QString, MoveBounds> moveGroups;
    const bool drawDiff = m_viewMode == DiffOverlay && !baseView && !m_revision.isEmpty();
    if (drawDiff)
    {
        const CceDocument::Project &oldProject = m_editingProposal ? base : m_bundle.project;
        const CceDocument::Project &newProject = m_editingProposal ? m_bundle.project : proposed;
        QSet<QString> activeLayers;
        for (const auto *project : {&oldProject, &newProject})
            for (const auto &binding : project->difficulties.value(displayProject.activeDifficultyId).bindings)
                activeLayers.insert(binding.layerId);
        const QVector<Change> changes = m_editingProposal
                                            ? currentChanges()
                                            : (m_changes.isEmpty() ? decodeRevisionChanges(m_revision) : m_changes);
        QHash<QString, QString> conflictStates;
        if (!m_editingProposal && !changes.isEmpty())
        {
            const MergeResult status =
                merge(m_revision.value(QStringLiteral("baseSnapshot")).toObject(), snapshot(m_bundle), changes);
            for (const MergeItem &item : status.items)
                if (item.state == QStringLiteral("Conflict") || item.state == QStringLiteral("Invalid"))
                    conflictStates.insert(item.change.changeId, QStringLiteral("Conflict"));
        }
        for (const Change &change : changes)
        {
            if (change.domain != QStringLiteral("Note"))
                continue;
            const QString layerId = layerFromPath(change.path);
            if (!activeLayers.contains(layerId))
                continue;
            const QString state = conflictStates.value(
                change.changeId, change.kind == QStringLiteral("Conflict") ? QStringLiteral("Conflict") : change.kind);
            Note beforeNote;
            Note afterNote;
            const bool hasBefore = decodeNoteValue(change.before, &beforeNote);
            const bool hasAfter = decodeNoteValue(change.after, &afterNote);
            if (hasBefore && hasAfter && change.kind == "Move" && !change.operationGroupId.isEmpty())
            {
                auto &bounds = moveGroups[change.operationGroupId];
                const auto extent = [](const Note &note) {
                    return QRectF(QPointF(note.x - 4., note.getStartBeat()),
                                  QPointF(note.x + 4., qMax(note.getStartBeat() + .01, note.getEndBeat())));
                };
                bounds.before = bounds.count ? bounds.before.united(extent(beforeNote)) : extent(beforeNote);
                bounds.after = bounds.count ? bounds.after.united(extent(afterNote)) : extent(afterNote);
                ++bounds.count;
            }
            if (hasBefore
                && (!hasAfter || change.kind == QStringLiteral("Move") || change.kind == QStringLiteral("Modify")))
            {
                m_overlayNotes.append({beforeNote, changeColor(state, true), .68, change.changeId});
            }
            if (hasAfter
                && (!hasBefore || change.kind == QStringLiteral("Move") || change.kind == QStringLiteral("Modify")))
            {
                m_overlayNotes.append({afterNote, changeColor(state, false), .68, change.changeId});
            }
        }
    }

    QVector<int> boundsIndices;
    QVector<double> boundsStarts, boundsEnds;
    for (const auto &bounds : moveGroups)
        if (bounds.count > 1)
        {
            boundsIndices.append(m_moveBounds.size());
            boundsStarts.append(qMin(bounds.before.top(), bounds.after.top()));
            boundsEnds.append(qMax(bounds.before.bottom(), bounds.after.bottom()));
            m_moveBounds.append(bounds);
        }
    m_moveBoundsIndex.build(boundsIndices, boundsStarts, boundsEnds);

    // References are live project layers. Formal bindings win and each layer
    // appears once; incompatible audio hashes are never projected.
    if (displayProject.difficulties.contains(displayProject.activeDifficultyId))
    {
        const auto &target = displayProject.difficulties[displayProject.activeDifficultyId];
        const auto targetTime = MathUtils::buildBpmTimeCache(target.timing, target.metadata.offset);
        QSet<QString> seen;
        for (const auto &binding : target.bindings)
            seen.insert(binding.layerId);
        for (const auto &reference : target.references)
        {
            if (!reference.visible)
                continue;
            const auto sourceIt = displayProject.difficulties.constFind(reference.difficultyId);
            if (sourceIt == displayProject.difficulties.cend() || target.oggHash.isEmpty()
                || sourceIt->oggHash != target.oggHash)
                continue;
            const auto sourceTime = MathUtils::buildBpmTimeCache(sourceIt->timing, sourceIt->metadata.offset);
            QStringList layerIds = reference.layerIds;
            if (layerIds.isEmpty())
            {
                for (const auto &binding : sourceIt->bindings)
                    layerIds.append(binding.layerId);
            }
            for (const QString &layerId : layerIds)
            {
                if (seen.contains(layerId))
                    continue;
                seen.insert(layerId);
                const auto layer = displayProject.layers.constFind(layerId);
                if (layer == displayProject.layers.cend())
                    continue;
                for (const Note &note : layer->notes)
                {
                    const double start =
                        referenceBeat(note.beatNum, note.numerator, note.denominator, sourceTime, targetTime);
                    const double end = note.isRainNote() ? referenceBeat(note.endBeatNum, note.endNumerator,
                                                                         note.endDenominator, sourceTime, targetTime)
                                                         : start;
                    m_overlayNotes.append(
                        {note, QColor(111, 186, 255, 210), qBound(0., reference.opacity, 1.0), QString(), start, end});
                }
            }
        }
    }

    QVector<int> allIndices;
    m_overlayByNote.clear();
    m_overlayByGroup.clear();
    QVector<double> starts, ends;
    allIndices.reserve(m_overlayNotes.size());
    starts.reserve(m_overlayNotes.size());
    ends.reserve(m_overlayNotes.size());
    for (int i = 0; i < m_overlayNotes.size(); ++i)
    {
        const Note &note = m_overlayNotes.at(i).note;
        if (!m_overlayNotes.at(i).groupId.isEmpty())
        {
            m_overlayByNote[canonicalHash(CceDocument::encodeNote(note, true))].append(i);
            m_overlayByGroup[m_overlayNotes.at(i).groupId].append(i);
        }
        allIndices.append(i);
        starts.append(m_overlayNotes.at(i).firstBeat());
        ends.append(m_overlayNotes.at(i).lastBeat());
    }
    m_overlayRainIndex.build(allIndices, starts, ends);

    // Keep the formal presentation index ready for callers that need to query
    // the displayed chart; ChartCanvas also maintains its own native caches.
    QVector<int> rainIndices;
    QVector<double> presentationStarts, presentationEnds;
    const QVector<Note> &presentationNotes = m_presentationChart.notes();
    rainIndices.reserve(presentationNotes.size());
    presentationStarts.reserve(presentationNotes.size());
    presentationEnds.reserve(presentationNotes.size());
    for (int i = 0; i < presentationNotes.size(); ++i)
    {
        const Note &note = presentationNotes.at(i);
        rainIndices.append(i);
        presentationStarts.append(note.getStartBeat());
        presentationEnds.append(note.isRainNote() ? note.getEndBeat() : note.getStartBeat());
    }
    m_presentationRainIndex.build(rainIndices, presentationStarts, presentationEnds);
    rebuildCommentCards();
    m_canvas->update();
}

void ReviewWorkspace::rebuildCommentCards()
{
    m_commentCards.clear();
    QSettings settings;
    QSet<QString> seen;
    QVector<int> indices;
    QVector<double> beats, ends;
    for (const auto &value : comments())
    {
        const auto comment = value.toObject();
        const auto anchor = comment.value("anchor").toObject();
        const auto difficulty = anchor.value("difficultyId").toString();
        if (!difficulty.isEmpty() && difficulty != m_bundle.project.activeDifficultyId)
            continue;
        const auto thread = commentThreadId(comment);
        if (comment.value("tombstone").toBool() || seen.contains(thread))
            continue;
        seen.insert(thread);
        const auto state = settings.value("local-review/comments/" + thread + "/display", "Expanded").toString();
        if (state == "Hidden")
            continue;
        CommentCard card;
        card.commentId = comment.value("commentId").toString();
        card.threadId = thread;
        card.author = comment.value("author").toObject().value("authorNameSnapshot").toString();
        card.body = comment.value("body").toString().simplified();
        card.expanded = state != "Collapsed";
        card.resolved = comment.value("state") == "Resolved";
        card.offset = boundedOffset(persistedCommentOffset(thread));
        const auto referenceState = anchorReferenceState(anchor);
        card.referenceStatus = referenceState == "Missing"     ? tr("Target missing")
                               : referenceState == "Ambiguous" ? tr("Ambiguous target")
                               : referenceState == "Modified"  ? tr("Target modified")
                                                               : QString();
        card.beat = beatValue(anchor.value("beat"));
        if (anchor.value("bpm").isObject())
            card.beat = beatValue(anchor.value("bpm").toObject().value("beat"), card.beat);
        card.laneX = anchor.value("x").toDouble(-1);
        if (anchor.value("type") == "Region")
        {
            const double endBeat = beatValue(anchor.value("endBeat"), card.beat);
            card.region = QRectF(QPointF(card.laneX, card.beat), QPointF(anchor.value("endX").toDouble(512), endBeat))
                              .normalized();
            card.hasRegion = true;
        }
        const auto targets = anchor.value("targets").toArray();
        for (const auto &targetValue : targets)
        {
            const auto target = targetValue.toObject();
            const auto after = target.value("note").toObject();
            const auto before = target.value("originalNote").toObject();
            if (after.contains("beat") && after.contains("x"))
                card.afterTargets.append(QPointF(after.value("x").toDouble(), beatValue(after.value("beat"))));
            if (before.contains("beat") && before.contains("x") && canonicalHash(before) != canonicalHash(after))
                card.beforeTargets.append(QPointF(before.value("x").toDouble(), beatValue(before.value("beat"))));
        }
        if (!targets.isEmpty())
        {
            const auto target = targets.first().toObject().value("note").toObject();
            card.beat = beatValue(target.value("beat"), card.beat);
            card.laneX = target.value("x").toDouble(card.laneX);
        }
        indices.append(m_commentCards.size());
        beats.append(card.beat);
        ends.append(card.hasRegion ? card.region.bottom() : card.beat);
        m_commentCards.append(card);
    }
    m_commentCardIndex.build(indices, beats, ends);
}

void ReviewWorkspace::paintOverlay(QPainter *painter)
{
    if (!painter || !m_canvas)
        return;
    painter->save();
    painter->setClipRect(m_canvas->rect());

    const double firstBeat = m_canvas->scrollBeat();
    const double lastBeat = firstBeat + m_canvas->visibleBeatRange();
    const auto visibleBounds = m_moveBoundsIndex.overlapping(firstBeat, lastBeat);
    for (qsizetype i = visibleBounds.begin; i < visibleBounds.end; ++i)
    {
        const auto &bounds = m_moveBounds.at(m_moveBoundsIndex.entryAt(i).index);
        const auto project = [this](const QRectF &rect) {
            return QRectF(QPointF(m_canvas->chartLaneXToCanvasX(rect.left()), m_canvas->chartBeatToY(rect.top())),
                          QPointF(m_canvas->chartLaneXToCanvasX(rect.right()), m_canvas->chartBeatToY(rect.bottom())))
                .normalized()
                .adjusted(-5, -5, 5, 5);
        };
        const auto before = project(bounds.before), after = project(bounds.after);
        painter->setOpacity(.65);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(235, 72, 72), 1.3, Qt::DashLine));
        painter->drawRect(before);
        painter->setPen(QPen(QColor(55, 210, 115), 1.3, Qt::DashLine));
        painter->drawRect(after);
        painter->setPen(QPen(QColor(255, 220, 90), 1.2, Qt::DashLine));
        painter->drawLine(before.center(), after.center());
    }
    const auto visible = m_overlayRainIndex.overlapping(firstBeat, lastBeat);
    for (qsizetype i = visible.begin; i < visible.end; ++i)
    {
        const auto &entry = m_overlayRainIndex.entryAt(i);
        if (entry.index < 0 || entry.index >= m_overlayNotes.size())
            continue;
        const PaintedNote &item = m_overlayNotes.at(entry.index);
        const Note &note = item.note;
        const double y1 = m_canvas->chartBeatToY(item.firstBeat());
        const double y2 = m_canvas->chartBeatToY(item.lastBeat());
        const double x = m_canvas->chartLaneXToCanvasX(note.x);
        const qreal width = note.isRainNote() ? 12.0 : note.isSound() ? 10.0 : 8.0;
        painter->setPen(QPen(item.color, note.isRainNote() ? 2.4 : 1.2));
        painter->setBrush(item.color);
        if (note.isRainNote())
        {
            const QRectF body(x - width / 2.0, qMin(y1, y2), width, qMax<qreal>(5.0, qAbs(y2 - y1)));
            painter->setOpacity(item.opacity);
            painter->drawRoundedRect(body, width / 2.0, width / 2.0);
            painter->drawEllipse(QPointF(x, y1), 4.0, 4.0);
            painter->drawEllipse(QPointF(x, y2), 4.0, 4.0);
        }
        else
        {
            painter->setOpacity(item.opacity);
            painter->drawEllipse(QPointF(x, y1), width / 2.0, width / 2.0);
        }
    }

    // Draw a short direction cue only for the currently selected/hovered move
    // group. The paired line avoids filling the canvas with long per-note links.
    if (m_selection && !m_overlayByNote.isEmpty())
    {
        const QSet<int> selected = m_selection->selectedIndices();
        if (!selected.isEmpty() && selected.size() <= 32)
        {
            QHash<QString, QPointF> oldByGroup;
            QHash<QString, QPointF> newByGroup;
            QSet<QString> selectedGroups;
            const Chart *nativeChart =
                m_controller ? (m_controller->isReadOnly() ? &m_presentationChart : m_controller->chart()) : nullptr;
            if (nativeChart)
            {
                const QVector<Note> &notes = nativeChart->notes();
                for (int index : selected)
                {
                    if (index < 0 || index >= notes.size())
                        continue;
                    for (int overlayIndex : m_overlayByNote.value(
                             canonicalHash(CceDocument::encodeNote(portableNote(notes.at(index)), true))))
                        selectedGroups.insert(m_overlayNotes.at(overlayIndex).groupId);
                }
            }
            for (const auto &group : selectedGroups)
                for (int index : m_overlayByGroup.value(group))
                {
                    const PaintedNote &item = m_overlayNotes.at(index);
                    const QPointF pos(m_canvas->chartLaneXToCanvasX(item.note.x),
                                      m_canvas->chartBeatToY(item.note.getStartBeat()));
                    if (item.color.red() > item.color.green())
                        oldByGroup[item.groupId] = pos;
                    else
                        newByGroup[item.groupId] = pos;
                }
            for (auto it = oldByGroup.cbegin(); it != oldByGroup.cend(); ++it)
            {
                if (!newByGroup.contains(it.key()))
                    continue;
                painter->setOpacity(.5);
                painter->setPen(QPen(QColor(255, 220, 90, 190), 1.2, Qt::DashLine));
                painter->drawLine(it.value(), newByGroup.value(it.key()));
            }
        }
    }

    m_commentRects.clear();
    if (m_viewMode == BaseOnly || m_peeking)
    {
        const QRectF badge(12.0, 10.0, 164.0, 28.0);
        painter->setOpacity(.98);
        painter->setPen(QPen(QColor(181, 65, 52), 1.0));
        painter->setBrush(QColor(100, 35, 32, 238));
        painter->drawRoundedRect(badge, 4.0, 4.0);
        painter->setPen(Qt::white);
        painter->drawText(badge, Qt::AlignCenter, tr("BASE — READ ONLY"));
    }
    const auto cards = m_commentCardIndex.overlapping(firstBeat - 1, lastBeat + 1);
    for (qsizetype i = cards.begin; i < cards.end; ++i)
    {
        const auto &card = m_commentCards.at(m_commentCardIndex.entryAt(i).index);
        QPointF origin(card.laneX >= 0.0 ? m_canvas->chartLaneXToCanvasX(card.laneX)
                                         : m_canvas->width() - CommentCardWidth - 16.0,
                       m_canvas->chartBeatToY(card.beat));
        QPointF offset = card.offset;
        if (card.threadId == m_draggedComment)
            offset = m_dragOffset;
        offset = boundedOffset(offset);
        const bool expanded = card.expanded;
        if (card.hasRegion)
        {
            const QRectF area(
                QPointF(m_canvas->chartLaneXToCanvasX(card.region.left()), m_canvas->chartBeatToY(card.region.top())),
                QPointF(m_canvas->chartLaneXToCanvasX(card.region.right()),
                        m_canvas->chartBeatToY(card.region.bottom())));
            painter->setOpacity(.45);
            painter->setPen(QPen(QColor(125, 178, 240), 1, Qt::DashLine));
            painter->setBrush(QColor(70, 125, 195, 32));
            painter->drawRect(area.normalized().adjusted(-3, -3, 3, 3));
        }
        const qreal cardHeight = expanded ? CommentCardHeight : 30.0;
        const qreal cardWidth = expanded ? CommentCardWidth : 150.0;
        QRectF rect(origin + offset + QPointF(9.0, -cardHeight / 2.0), QSizeF(cardWidth, cardHeight));
        rect.moveLeft(qBound(4.0, rect.left(), qMax(4.0, m_canvas->width() - rect.width() - 4.0)));
        rect.moveTop(qBound(4.0, rect.top(), qMax(4.0, m_canvas->height() - rect.height() - 4.0)));
        m_commentRects.insert(card.commentId, rect);
        // The thread keeps both positional snapshots; no persistent Note identity is needed.
        const auto connectTargets = [&](const QVector<QPointF> &targets, const QColor &color) {
            painter->setOpacity(.6);
            painter->setPen(QPen(color, 1, Qt::DashLine));
            for (const auto &target : targets.first(qMin<qsizetype>(targets.size(), 32)))
                painter->drawLine(rect.center(), QPointF(m_canvas->chartLaneXToCanvasX(target.x()),
                                                         m_canvas->chartBeatToY(target.y())));
        };
        if (m_viewMode == DiffOverlay && !m_peeking)
            connectTargets(card.beforeTargets, QColor(235, 72, 72));
        connectTargets(card.afterTargets, QColor(55, 210, 115));
        const bool resolved = card.resolved;
        painter->setOpacity(.96);
        painter->setPen(QPen(resolved ? QColor(90, 145, 100) : QColor(80, 112, 150), 1.0));
        painter->setBrush(resolved ? QColor(35, 57, 44, 235) : QColor(34, 45, 61, 238));
        painter->drawRoundedRect(rect, 5.0, 5.0);
        painter->setPen(QColor(235, 239, 246));
        painter->drawText(rect.adjusted(8, 5, -7, expanded ? -28 : -5), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(painter->font())
                              .elidedText(card.referenceStatus.isEmpty()
                                              ? card.author.isEmpty() ? tr("Local comment") : card.author
                                              : card.referenceStatus,
                                          Qt::ElideRight, int(rect.width() - 15)));
        if (expanded)
        {
            painter->setPen(QColor(190, 202, 220));
            painter->drawText(
                rect.adjusted(8, 25, -7, -4), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(painter->font()).elidedText(card.body, Qt::ElideRight, int(rect.width() - 15)));
        }
    }
    painter->restore();
}

bool ReviewWorkspace::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate && (watched == m_window || watched == m_canvas))
    {
        if (m_peeking)
        {
            m_peeking = false;
            rebuildPresentation();
        }
        return QObject::eventFilter(watched, event);
    }
    if (watched != m_canvas || !event)
        return QObject::eventFilter(watched, event);

    auto *mouse = dynamic_cast<QMouseEvent *>(event);
    if (!mouse)
        return QObject::eventFilter(watched, event);
    const QPointF pos = mouse->position();
    if (event->type() == QEvent::MouseButtonDblClick && mouse->button() == Qt::LeftButton)
    {
        for (auto it = m_commentRects.cbegin(); it != m_commentRects.cend(); ++it)
        {
            if (!it.value().contains(pos))
                continue;
            const QString commentId = it.key();
            m_draggedComment.clear();
            showComments();
            if (m_commentFilterBox)
                m_commentFilterBox->setCurrentIndex(0);
            if (m_commentsTree)
            {
                QList<QTreeWidgetItem *> items;
                for (int i = 0; i < m_commentsTree->topLevelItemCount(); ++i)
                    items.append(m_commentsTree->topLevelItem(i));
                while (!items.isEmpty())
                {
                    QTreeWidgetItem *item = items.takeLast();
                    if (item->data(0, Qt::UserRole).toString() == commentId)
                    {
                        m_commentsTree->setCurrentItem(item);
                        break;
                    }
                    for (int child = 0; child < item->childCount(); ++child)
                        items.append(item->child(child));
                }
            }
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton)
    {
        for (auto it = m_commentRects.cbegin(); it != m_commentRects.cend(); ++it)
        {
            if (!it.value().contains(pos))
                continue;
            m_draggedComment = commentThreadId([&] {
                for (const QJsonValue &value : comments())
                    if (value.toObject().value(QStringLiteral("commentId")).toString() == it.key())
                        return value.toObject();
                return QJsonObject{};
            }());
            m_dragStart = pos;
            m_dragOffset = persistedCommentOffset(m_draggedComment);
            m_canvas->update();
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove && !m_draggedComment.isEmpty() && (mouse->buttons() & Qt::LeftButton))
    {
        m_dragOffset = boundedOffset(m_dragOffset + (pos - m_dragStart));
        m_dragStart = pos;
        m_canvas->update();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton && !m_draggedComment.isEmpty())
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("local-review/commentOffsets"));
        settings.setValue(m_draggedComment, boundedOffset(m_dragOffset));
        settings.endGroup();
        m_draggedComment.clear();
        rebuildCommentCards();
        m_canvas->update();
        return true;
    }
    if (m_controller->isReadOnly() && !m_revision.isEmpty() && event->type() == QEvent::MouseButtonPress
        && mouse->button() == Qt::LeftButton && m_selection)
    {
        const double beat = m_canvas->chartYToBeat(pos.y());
        const auto range = m_presentationRainIndex.overlapping(beat - .2, beat + .2);
        int best = -1;
        double distance = 18.;
        for (qsizetype i = range.begin; i < range.end; ++i)
        {
            const int index = m_presentationRainIndex.entryAt(i).index;
            const auto &note = m_presentationChart.notes().at(index);
            const double x = m_canvas->chartLaneXToCanvasX(note.x);
            const double y = m_canvas->chartBeatToY(note.getStartBeat());
            const double end = m_canvas->chartBeatToY(note.getEndBeat());
            const double dy =
                note.isRainNote() ? qMax(0., qMax(qMin(y, end) - pos.y(), pos.y() - qMax(y, end))) : qAbs(y - pos.y());
            const double candidate = qAbs(x - pos.x()) + dy;
            if (candidate < distance)
            {
                distance = candidate;
                best = index;
            }
        }
        m_canvas->setFocus();
        if (best >= 0)
        {
            if (mouse->modifiers().testFlag(Qt::ControlModifier))
                m_selection->toggleSelection(m_selection->selectedIndices(), QSet<int>{best});
            else
                m_selection->select(best);
        }
        else if (!mouse->modifiers().testFlag(Qt::ControlModifier))
            m_selection->clearSelection();
        m_canvas->update();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void ReviewWorkspace::setViewMode(int mode)
{
    m_viewMode = qBound(ProposalOnly, mode, BaseOnly);
    if (m_window)
        m_window->installEventFilter(this);
    if (m_viewBox && m_viewBox->currentIndex() != m_viewMode)
    {
        const QSignalBlocker blocker(m_viewBox);
        m_viewBox->setCurrentIndex(m_viewMode);
    }
    rebuildPresentation();
    if (m_canvas)
        m_canvas->update();
    if (m_viewMode == BaseOnly || m_peeking)
        report(tr("BASE — READ ONLY"));
    else
        report(m_viewMode == DiffOverlay ? tr("Diff Overlay") : tr("Proposal Only"));
}
} // namespace CceReview
