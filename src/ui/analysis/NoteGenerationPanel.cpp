#include "NoteGenerationPanel.h"
#include "AnalysisCanvas.h"
#include "TransientPanel.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/LongRangeSelector.h"
#include <QBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QSignalBlocker>
#include <QHideEvent>
#include <QFontMetrics>
#include <cmath>

NoteGenerationPanel::NoteGenerationPanel(ChartController *chart, ChartCanvas *main, AnalysisCanvas *canvas,
    TransientPanel *transient, PlaybackController *playback, LongRangeSelector *range, QWidget *parent)
    : QWidget(parent), m_chart(chart), m_main(main), m_canvas(canvas), m_transient(transient),
      m_playback(playback), m_range(range)
{
    setObjectName("analysis.generationPanel");
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Build ordinary-note drafts from detected transients. Review the ghosts in both editors, then add the checked notes. X layout remains a manual charting choice."));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_scope = new QComboBox;
    m_scope->setObjectName("analysis.generationScope");
    m_scope->addItems({tr("Visible interval"), tr("Selected range"), tr("Analyzed page")});
    m_band = new QComboBox;
    m_band->setObjectName("analysis.generationBand");
    m_band->addItems({tr("All detected peaks"), tr("Low-dominant peaks"), tr("Mid-dominant peaks"), tr("High-dominant peaks")});
    m_x = new QSpinBox;
    m_x->setObjectName("analysis.generationX");
    m_x->setRange(0, 512);
    m_x->setValue(256);
    m_strength = new QDoubleSpinBox;
    m_strength->setObjectName("analysis.generationStrength");
    m_strength->setRange(0, 1);
    m_strength->setSingleStep(.05);
    m_strength->setToolTip(tr("Fraction of the strongest detected peak in this range."));
    m_snapError = new QDoubleSpinBox;
    m_snapError->setObjectName("analysis.generationSnapError");
    m_snapError->setRange(0, 1000);
    m_snapError->setValue(40);
    m_snapError->setSuffix(tr(" ms"));
    m_snapError->setToolTip(tr("Peaks farther from the current chart grid are skipped."));
    form->addRow(tr("Range"), m_scope);
    form->addRow(tr("Source"), m_band);
    form->addRow(tr("X position"), m_x);
    form->addRow(tr("Relative strength"), m_strength);
    form->addRow(tr("Maximum snap distance"), m_snapError);
    layout->addLayout(form);
    m_preview = new QPushButton(tr("Build draft preview"));
    m_preview->setObjectName("analysis.generationPreview");
    layout->addWidget(m_preview);
    m_summary = new QLabel(tr("Wait for transient analysis, then build a draft."));
    m_summary->setObjectName("analysis.generationSummary");
    m_summary->setWordWrap(true);
    m_summary->setMinimumHeight(fontMetrics().lineSpacing() * 3);
    layout->addWidget(m_summary);
    m_candidates = new QTableWidget(0, 3);
    m_candidates->setObjectName("analysis.generationCandidates");
    m_candidates->setHorizontalHeaderLabels({tr("Keep / audio s"), tr("Chart beat"), tr("Snap ms")});
    m_candidates->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_candidates->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_candidates->verticalHeader()->hide();
    layout->addWidget(m_candidates, 1);
    m_apply = new QPushButton(tr("Add checked notes (one undo)"));
    m_apply->setObjectName("analysis.generationApply");
    m_apply->setEnabled(false);
    layout->addWidget(m_apply);
    auto *cancel = new QPushButton(tr("Discard draft"));
    cancel->setObjectName("analysis.generationDiscard");
    layout->addWidget(cancel);
    connect(m_preview, &QPushButton::clicked, this, &NoteGenerationPanel::preview);
    connect(m_apply, &QPushButton::clicked, this, &NoteGenerationPanel::apply);
    connect(cancel, &QPushButton::clicked, this, &NoteGenerationPanel::clearPreview);
    connect(m_candidates, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == 0) syncIncluded();
    });
    for (auto *combo : {m_scope, m_band})
        connect(combo, &QComboBox::currentIndexChanged, this, &NoteGenerationPanel::invalidate);
    connect(m_x, &QSpinBox::valueChanged, this, &NoteGenerationPanel::invalidate);
    connect(m_strength, &QDoubleSpinBox::valueChanged, this, &NoteGenerationPanel::invalidate);
    connect(m_snapError, &QDoubleSpinBox::valueChanged, this, &NoteGenerationPanel::invalidate);
    connect(chart, &ChartController::chartChangeCommitted, this, &NoteGenerationPanel::invalidate);
    connect(chart, &ChartController::chartLoaded, this, &NoteGenerationPanel::invalidate);
    connect(main, &ChartCanvas::timeDivisionChanged, this, &NoteGenerationPanel::invalidate);
    connect(main, &ChartCanvas::analysisNoteDraftCancelled, this, &NoteGenerationPanel::clearPreview);
    connect(canvas, &AnalysisCanvas::noteDraftCancelled, this, &NoteGenerationPanel::clearPreview);
    connect(range, &LongRangeSelector::rangeChanged, this, [this] { if (m_scope->currentIndex() == 1) invalidate(); });
    connect(transient, &TransientPanel::tracksChanged, this, [this] {
        invalidate();
        m_preview->setEnabled(m_transient->result().valid() && !m_transient->busy());
    });
    m_preview->setEnabled(transient->result().valid() && !transient->busy());
}
NoteGenerationPanel::~NoteGenerationPanel() { clearPreview(); }
void NoteGenerationPanel::hideEvent(QHideEvent *event) { clearPreview(); QWidget::hideEvent(event); }
QString NoteGenerationPanel::sourceIdentity() const
{
    QString path = m_chart->chart()->audioSourceFullPath();
    if (path.isEmpty()) path = m_chart->chart()->meta().audioFile;
    if (QFileInfo(path).isRelative()) path = QFileInfo(m_chart->chartFilePath()).dir().absoluteFilePath(path);
    const QFileInfo file(path);
    return file.isFile() ? file.absoluteFilePath() + '|' + QString::number(file.size()) + '|'
        + QString::number(file.lastModified().toMSecsSinceEpoch()) : QString();
}
void NoteGenerationPanel::clearPreview()
{
    m_draft = {};
    m_included.clear();
    m_source.clear();
    if (m_main) m_main->setAnalysisNoteDraft({});
    if (m_canvas) m_canvas->setNoteDraft({});
    const QSignalBlocker blocker(m_candidates);
    m_candidates->setRowCount(0);
    m_apply->setEnabled(false);
    m_summary->setText(tr("Build a draft to review before adding notes."));
}
void NoteGenerationPanel::invalidate()
{
    const bool hadDraft = m_draft.valid();
    clearPreview();
    if (hadDraft) m_summary->setText(tr("The chart, source or analysis changed. Build a new preview."));
}
void NoteGenerationPanel::preview()
{
    clearPreview();
    m_playback->pause();
    const auto spectrum = m_transient->sourceSpectrum();
    if (!m_transient->result().valid() || m_transient->busy() || !spectrum || m_loadedSource.isEmpty()
        || m_loadedSource != sourceIdentity())
    { m_summary->setText(tr("Wait for transient analysis, then build a draft.")); return; }
    analysis::TransientNoteDraftOptions options;
    options.startSeconds = spectrum->startSeconds;
    options.endSeconds = spectrum->startSeconds + spectrum->durationSeconds;
    if (m_scope->currentIndex() == 0)
    {
        const double a = m_canvas->timeAtY(28) / 1000, b = m_canvas->timeAtY(m_canvas->height()) / 1000;
        options.startSeconds = qMax(0., qMin(a, b)); options.endSeconds = qMax(a, b);
    }
    else if (m_scope->currentIndex() == 1)
    {
        if (!m_range->hasValidRange())
        { m_summary->setText(tr("Select a non-empty time range first.")); return; }
        options.startSeconds = m_canvas->timeAtBeat(m_range->currentStartBeat()) / 1000;
        options.endSeconds = m_canvas->timeAtBeat(m_range->currentEndBeat()) / 1000;
    }
    if (options.startSeconds < spectrum->startSeconds - 1e-8
        || options.endSeconds > spectrum->startSeconds + spectrum->durationSeconds + 1e-8)
    { m_summary->setText(tr("The range is not fully analyzed. Load that interval or choose Analyzed page.")); return; }
    options.timeDivision = m_main->timeDivision(); options.x = m_x->value(); options.dominantBand = m_band->currentIndex();
    options.minimumRelativeStrength = m_strength->value(); options.maximumSnapErrorMs = m_snapError->value();
    m_revision = m_chart->revision(); m_source = sourceIdentity(); m_division = options.timeDivision;
    m_draft = analysis::buildTransientNoteDraft(m_transient->result(), options,
        MathUtils::buildBpmTimeCache(m_chart->chart()->bpmList(), m_chart->chart()->meta().offset), m_chart->chart()->notes());
    if (m_draft.error != analysis::NoteDraftError::None)
    {
        m_summary->setText(m_draft.error == analysis::NoteDraftError::TooManyNotes
            ? tr("More than 4096 notes. Narrow the range or raise the strength filter.")
            : tr("The analysis range or chart timing is invalid."));
        return;
    }
    {
        const QSignalBlocker blocker(m_candidates);
        m_candidates->setRowCount(m_draft.notes.size());
        for (int i = 0; i < m_draft.notes.size(); ++i)
        {
            const auto &n = m_draft.notes[i];
            auto *keep = new QTableWidgetItem(QString::number(m_draft.sourceSeconds[i], 'f', 3));
            keep->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            keep->setCheckState(Qt::Checked);
            m_candidates->setItem(i, 0, keep);
            m_candidates->setItem(i, 1, new QTableWidgetItem(QString("[%1,%2,%3]").arg(n.beatNum).arg(n.numerator).arg(n.denominator)));
            m_candidates->setItem(i, 2, new QTableWidgetItem(QString::number(m_draft.snapErrorsMs[i], 'f', 2)));
        }
    }
    m_summary->setText(tr("Frozen range %1–%2 s · %3 notes · filtered %4 · off-grid %5 · occupied %6 · merged %7. Grid: 1/%8.")
        .arg(options.startSeconds, 0, 'f', 3).arg(options.endSeconds, 0, 'f', 3).arg(m_draft.notes.size())
        .arg(m_draft.filtered).arg(m_draft.offGrid).arg(m_draft.occupied).arg(m_draft.merged).arg(m_division));
    syncIncluded();
}
void NoteGenerationPanel::syncIncluded()
{
    m_included.clear();
    for (int i = 0; i < m_draft.notes.size(); ++i)
        if (m_candidates->item(i, 0)->checkState() == Qt::Checked) m_included.append(m_draft.notes[i]);
    if (m_main) m_main->setAnalysisNoteDraft(m_included);
    if (m_canvas) m_canvas->setNoteDraft(m_included);
    m_apply->setEnabled(!m_included.isEmpty());
    m_apply->setText(tr("Add %1 checked notes (one undo)").arg(m_included.size()));
}
void NoteGenerationPanel::apply()
{
    if (!m_draft.valid() || m_included.isEmpty()) return;
    if (m_revision != m_chart->revision() || m_source != sourceIdentity() || m_division != m_main->timeDivision())
    { invalidate(); return; }
    const auto notes = m_included;
    clearPreview();
    if (!m_chart->applyBatchEdit(tr("Generate notes from transients"), notes, {}, {}))
    { m_summary->setText(tr("The draft could not be added. Build a new preview.")); return; }
    m_summary->setText(tr("Added %1 notes. Undo removes this batch.").arg(notes.size()));
}
