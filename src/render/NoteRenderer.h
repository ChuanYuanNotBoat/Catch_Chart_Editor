#pragma once

#include <QPainter>
#include <QSet>
#include <QHash>
#include "model/Note.h"

class Skin;
class HyperfruitDetector;

class NoteRenderer
{
public:
    NoteRenderer();

    void setSkin(const Skin *skin);
    void setShowColors(bool show);
    void setHyperfruitEnabled(bool enabled);
    void setHyperfruitDetector(HyperfruitDetector *detector);
    void setHyperfruitIndices(const QSet<int> &indices);
    void setNoteSize(int size);
    void refreshSettings();
    int getNoteSize() const;

    void drawNote(QPainter &painter, const Note &note, const QPointF &pos, bool selected, int index) const;
    void drawRain(QPainter &painter, const Note &note, const QRectF &rect, bool selected) const;

private:
    void calculateOutline(const Note &note, bool selected, int index, int &outlineWidth, QColor &outlineColor) const;
    void drawSelectionHighlight(QPainter &painter, const QRectF &rect) const;
    bool validateRect(const QRectF &rect) const;
    const QPixmap *cachedSkinPixmapForType(int noteType) const;
    void invalidateSkinPixmapCache() const;

private:
    const Skin *m_skin;
    bool m_showColors;
    bool m_hyperfruitEnabled;
    HyperfruitDetector *m_hyperfruitDetector;
    QSet<int> m_hyperfruitIndices;
    int m_noteSize;
    int m_outlineWidth;
    QColor m_outlineColor;
    mutable const Skin *m_cachedSkinPtr;
    mutable QHash<int, QPixmap> m_cachedScaledSkinPixmaps;
};
