#pragma once
#include <QWidget>
#include <QPointer>
#include "analysis/TransientNoteDraft.h"
class ChartController;
class ChartCanvas;
class AnalysisCanvas;
class TransientPanel;
class PlaybackController;
class LongRangeSelector;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

class NoteGenerationPanel : public QWidget
{
    Q_OBJECT
public:
    NoteGenerationPanel(ChartController *, ChartCanvas *, AnalysisCanvas *, TransientPanel *,
                         PlaybackController *, LongRangeSelector *, QWidget *parent = nullptr);
    ~NoteGenerationPanel() override;
    void clearPreview();
    void setSourceIdentity(const QString &identity) { if (identity != m_loadedSource) { invalidate(); m_loadedSource = identity; } }

protected:
    void hideEvent(QHideEvent *) override;

private:
    void preview();
    void apply();
    void syncIncluded();
    void invalidate();
    QString sourceIdentity() const;
    ChartController *m_chart;
    QPointer<ChartCanvas> m_main;
    QPointer<AnalysisCanvas> m_canvas;
    TransientPanel *m_transient;
    PlaybackController *m_playback;
    LongRangeSelector *m_range;
    QComboBox *m_scope, *m_band;
    QSpinBox *m_x;
    QDoubleSpinBox *m_strength, *m_snapError;
    QLabel *m_summary;
    QPushButton *m_preview, *m_apply;
    QTableWidget *m_candidates;
    analysis::TransientNoteDraft m_draft;
    QVector<Note> m_included;
    quint64 m_revision = 0;
    int m_division = 0;
    QString m_source, m_loadedSource;
};
