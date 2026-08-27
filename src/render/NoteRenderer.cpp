#include "NoteRenderer.h"
#include "model/Skin.h"
#include "render/BeatDivisionColor.h"
#include "render/HyperfruitDetector.h"
#include "utils/Settings.h"
#include <QPainter>
#include <QPen>
#include "utils/Logger.h"

NoteRenderer::NoteRenderer()
    : m_skin(nullptr), m_showColors(true), m_hyperfruitEnabled(true),
      m_hyperfruitDetector(nullptr), m_noteSize(16),
      m_outlineWidth(1), m_outlineColor(Qt::black),
      m_cachedSkinPtr(nullptr)
{
    refreshSettings();
}

void NoteRenderer::setSkin(const Skin *skin)
{
    m_skin = skin;
    invalidateSkinPixmapCache();
}

void NoteRenderer::setShowColors(bool show)
{
    m_showColors = show;
}

void NoteRenderer::setHyperfruitEnabled(bool enabled)
{
    m_hyperfruitEnabled = enabled;
}

void NoteRenderer::setHyperfruitDetector(HyperfruitDetector *detector)
{
    m_hyperfruitDetector = detector;
}

void NoteRenderer::setHyperfruitIndices(const QSet<int> &indices)
{
    m_hyperfruitIndices = indices;
}

void NoteRenderer::setNoteSize(int size)
{
    if (m_noteSize == size)
        return;
    m_noteSize = size;
    // Keep cache coherent if skin scaling policy changes in future.
    invalidateSkinPixmapCache();
}

void NoteRenderer::refreshSettings()
{
    m_outlineWidth = Settings::instance().outlineWidth();
    m_outlineColor = Settings::instance().outlineColor();
}

int NoteRenderer::getNoteSize() const
{
    return m_noteSize;
}

void NoteRenderer::drawNote(QPainter &painter, const Note &note, const QPointF &pos, bool selected, int index) const
{
    int noteType = 0;
    if (note.type != NoteType::RAIN)
    {
        if (note.denominator == 1)
            noteType = 0; // 1/1 whole note uses the dedicated 1/1 skin (red in color mode)
        else if (note.denominator == 2)
            noteType = 1;
        else if (note.denominator == 4)
            noteType = 2;
        else if (note.denominator == 8 || note.denominator == 16 || note.denominator == 32)
            noteType = 3;
        else if (note.denominator == 3 || note.denominator == 6 || note.denominator == 12 || note.denominator == 24)
            noteType = 4;
        else if (note.denominator == 288)
            noteType = 5;
        else
            noteType = 5;
    }
    else
    {
        noteType = 5;
    }

    const QPixmap *notePix = nullptr;
    if (m_skin && m_skin->isValid() && note.type != NoteType::RAIN)
    {
        notePix = cachedSkinPixmapForType(noteType);
    }

    int drawW = (!notePix || notePix->isNull()) ? m_noteSize : notePix->width();
    int drawH = (!notePix || notePix->isNull()) ? m_noteSize : notePix->height();
    QRectF rect(pos.x() - drawW / 2, pos.y() - drawH / 2, drawW, drawH);

    int outlineWidth;
    QColor outlineColor;
    calculateOutline(note, selected, index, outlineWidth, outlineColor);

    if (notePix && !notePix->isNull())
    {
        painter.drawPixmap(rect.toRect(), *notePix);
    }
    else if (m_showColors && !m_skin)
    {
        painter.setBrush(BeatDivisionColor::noteColorForDivision(note.denominator, note.numerator));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect);
    }
    else
    {
        painter.setBrush(Qt::lightGray);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect);
    }

    if (outlineWidth > 0)
    {
        painter.setPen(QPen(outlineColor, outlineWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rect);
    }

    if (selected)
    {
        drawSelectionHighlight(painter, rect);
    }
}

void NoteRenderer::drawRain(QPainter &painter, const Note &note, const QRectF &rect, bool selected) const
{
    if (!validateRect(rect))
    {
        Logger::warn(QString("Invalid rectangle in drawRain: x=%1, y=%2, w=%3, h=%4")
                         .arg(rect.x())
                         .arg(rect.y())
                         .arg(rect.width())
                         .arg(rect.height()));
        return;
    }

    painter.setBrush(QColor(0, 0, 255, 100));
    painter.setPen(Qt::NoPen);
    painter.drawRect(rect);

    int outlineWidth;
    QColor outlineColor;
    calculateOutline(note, selected, -1, outlineWidth, outlineColor);

    if (outlineWidth > 0)
    {
        painter.setPen(QPen(outlineColor, outlineWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
    }

    if (selected)
    {
        drawSelectionHighlight(painter, rect);
    }
}

void NoteRenderer::calculateOutline(const Note &note, bool selected, int index, int &outlineWidth, QColor &outlineColor) const
{
    if (selected)
    {
        outlineWidth = 2;
        outlineColor = QColor(255, 165, 0);
    }
    else if (m_hyperfruitEnabled && m_hyperfruitIndices.contains(index))
    {
        outlineWidth = 4;
        outlineColor = Qt::red;
    }
    else
    {
        outlineWidth = m_outlineWidth;
        outlineColor = m_outlineColor;
    }
}

void NoteRenderer::drawSelectionHighlight(QPainter &painter, const QRectF &rect) const
{
    painter.setBrush(QColor(255, 255, 0, 80));
    painter.setPen(Qt::NoPen);
    painter.drawRect(rect);
}

bool NoteRenderer::validateRect(const QRectF &rect) const
{
    if (rect.width() <= 0 || rect.height() <= 0)
        return false;
    if (!std::isfinite(rect.x()) || !std::isfinite(rect.y()) ||
        !std::isfinite(rect.width()) || !std::isfinite(rect.height()))
        return false;
    return true;
}

const QPixmap *NoteRenderer::cachedSkinPixmapForType(int noteType) const
{
    if (!m_skin || !m_skin->isValid())
        return nullptr;

    if (m_cachedSkinPtr != m_skin)
        invalidateSkinPixmapCache();

    const auto cachedIt = m_cachedScaledSkinPixmaps.constFind(noteType);
    if (cachedIt != m_cachedScaledSkinPixmaps.constEnd())
    {
        if (!cachedIt.value().isNull())
            return &cachedIt.value();
        return nullptr;
    }

    const QPixmap *basePix = m_skin->getNotePixmap(noteType);
    if (!basePix || basePix->isNull())
    {
        m_cachedScaledSkinPixmaps.insert(noteType, QPixmap());
        return nullptr;
    }

    const double scale = m_skin->getNoteScale(noteType);
    const int scaledW = qMax(1, qRound(basePix->width() * scale));
    const int scaledH = qMax(1, qRound(basePix->height() * scale));
    const QPixmap scaled = basePix->scaled(scaledW, scaledH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    auto inserted = m_cachedScaledSkinPixmaps.insert(noteType, scaled);
    if (inserted.value().isNull())
        return nullptr;
    return &inserted.value();
}

void NoteRenderer::invalidateSkinPixmapCache() const
{
    m_cachedSkinPtr = m_skin;
    m_cachedScaledSkinPixmaps.clear();
}
