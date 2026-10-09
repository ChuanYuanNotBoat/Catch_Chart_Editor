#include "AnalysisWorkbench.h"
#include "AnalysisCanvas.h"
#include "analysis/AnalysisConfig.h"
#include <QBoxLayout>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QGridLayout>
#include <QSettings>
#include <algorithm>
#include <cmath>
namespace
{
QString text(const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined())
        return QStringLiteral("unavailable");
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isBool())
        return v.toBool() ? "true" : "false";
    return v.isString() ? v.toString() : QString::number(v.toDouble(), 'g', 10);
}
} // namespace
AnalysisWorkbench::AnalysisWorkbench(AnalysisCanvas *canvas, QWidget *parent) : QWidget(parent)
{
    setObjectName("analysis.workbench");
    auto *root = new QVBoxLayout(this);
    auto *buttons = new QHBoxLayout;
    auto *import = new QPushButton(tr("Load record…")), *exportButton = new QPushButton(tr("Export diagnostics…"));
    buttons->addWidget(import);
    buttons->addWidget(exportButton);
    root->addLayout(buttons);
    auto *comparisonExport = new QPushButton(tr("Export comparison…"));
    root->addWidget(comparisonExport);
    m_objectDetails = new QPlainTextEdit;
    m_objectDetails->setReadOnly(true);
    m_objectDetails->setMaximumHeight(140);
    m_objectDetails->setObjectName("analysis.selectedObjectDetails");
    root->addWidget(m_objectDetails);
    connect(comparisonExport, &QPushButton::clicked, this, [this] {
        if (m_record.isEmpty())
        {
            m_referenceStatus->setText(tr("Load a baseline first."));
            return;
        }
        auto path = QFileDialog::getSaveFileName(this, tr("Export local comparison"), "analysis-comparison.json",
                                                 tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!analysis::saveJson(path, comparisonRecord(), &error))
            m_referenceStatus->setText(error);
    });
    connect(exportButton, &QPushButton::clicked, this, &AnalysisWorkbench::exportRequested);
    connect(import, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("Load local analysis record"), {}, tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!loadRecord(path, &error))
            m_referenceStatus->setText(error);
    });
    m_tabs = new QTabWidget;
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setObjectName("analysis.debugTabs");
    root->addWidget(m_tabs, 1);
    for (int i = 0; i < 3; ++i)
    {
        auto *page = new QWidget;
        auto *layout = new QVBoxLayout(page);
        m_groups[i] = new QTabWidget;
        if (i == 0)
        {
            m_overview = new QLabel;
            m_overview->setObjectName("analysis.overviewSummary");
            m_overview->setWordWrap(true);
            m_overview->setTextFormat(Qt::PlainText);
            layout->addWidget(m_overview);
            m_compare = new QLabel;
            m_compare->setWordWrap(true);
            m_compare->setTextFormat(Qt::PlainText);
            layout->addWidget(m_compare);
            m_hypotheses = new QComboBox;
            m_hypotheses->setObjectName("analysis.hypotheses");
            layout->addWidget(m_hypotheses);
            connect(m_hypotheses, &QComboBox::currentIndexChanged, this, [this](int index) {
                emit hypothesisSelected(index - 1);
            });
        }
        else if (i == 1)
        {
            auto *hint =
                new QLabel(tr("Curves follow the spectrum time axis. Observation/model residuals are separate from "
                              "reference accuracy. Orange regions: uncertainty; green grid: fitted model."));
            hint->setWordWrap(true);
            layout->addWidget(hint);
            auto *legend = new QGridLayout;
            const QStringList ids{"tempo",         "confidence",      "phase",          "residual",
                                  "periodicity",   "uncertainty",     "baselineTempo",  "spectrumEnvelope",
                                  "transientFlux", "transientEvents", "candidateEvents"};
            const QStringList labels{tr("Observed tempo (BPM)"),
                                     tr("Confidence (raw)"),
                                     tr("Phase confidence (raw)"),
                                     tr("Observation / model residual (ms)"),
                                     tr("Periodicity (events/min)"),
                                     tr("Uncertainty regions"),
                                     tr("Baseline tempo (same input)"),
                                     tr("Spectrum RMS / peak"),
                                     tr("Transient flux"),
                                     tr("Transient peaks (accepted / rejected)"),
                                     tr("Candidate pulses")};
            QSettings settings("CatchEditor", "CatchChartEditor");
            for (int j = 0; j < ids.size(); ++j)
            {
                auto *check = new QCheckBox(labels[j]);
                check->setObjectName("analysis.track." + ids[j]);
                check->setChecked(settings.value("analysisEditor/tracks/" + ids[j], j < 6).toBool());
                m_trackChecks[ids[j]] = check;
                legend->addWidget(check, j, 0);
                connect(check, &QCheckBox::toggled, this, [this, id = ids[j]](bool visible) {
                    QSettings s("CatchEditor", "CatchChartEditor");
                    s.setValue("analysisEditor/tracks/" + id, visible);
                    emit trackVisibilityChanged();
                });
            }
            layout->addLayout(legend);
        }
        else
        {
            m_evidence = new QPlainTextEdit;
            m_evidence->setObjectName("analysis.windowEvidence");
            m_evidence->setReadOnly(true);
            m_evidence->setMaximumHeight(180);
            layout->addWidget(m_evidence);
        }
        layout->addWidget(m_groups[i], 1);
        m_tabs->addTab(page, i == 0 ? tr("Overview") : i == 1 ? tr("Tempo") : tr("Rhythm"));
        m_tabs->setTabToolTip(i, i == 0   ? tr("Overview / Compare")
                                 : i == 1 ? tr("Tempo / Phase")
                                          : tr("Rhythm / Evidence"));
    }
    auto *references = new QWidget;
    auto *refLayout = new QVBoxLayout(references);
    m_referenceStatus = new QLabel(
        tr("Load existing records. No tests run when this page opens. Unannotated audio has no accuracy PASS."));
    m_referenceStatus->setWordWrap(true);
    m_referenceStatus->setTextFormat(Qt::PlainText);
    refLayout->addWidget(m_referenceStatus);
    m_tests = new QTableWidget(0, 6);
    m_tests->setObjectName("analysis.referenceTests");
    m_tests->setHorizontalHeaderLabels({tr("Case / Mode"), tr("Recorded status"), tr("Reference type"),
                                        tr("Strict max"), tr("Phase max (ms)"), tr("Coverage / reason")});
    m_tests->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tests->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tests->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    refLayout->addWidget(m_tests, 1);
    m_raw = new QPlainTextEdit;
    m_raw->setObjectName("analysis.rawDiagnostics");
    m_raw->setReadOnly(true);
    m_raw->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *records = new QTabWidget;
    records->addTab(m_raw, tr("Current diagnostics"));
    m_loadedRecord = new QPlainTextEdit;
    m_loadedRecord->setReadOnly(true);
    m_loadedRecord->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_loadedRecord->setObjectName("analysis.loadedRecord");
    records->addTab(m_loadedRecord, tr("Loaded record"));
    refLayout->addWidget(records, 1);
    m_tabs->addTab(references, tr("Reference"));
    m_tabs->setTabToolTip(3, tr("Reference / Tests"));
    connect(m_tests, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto *item = m_tests->item(row, 0);
        if (!item)
            return;
        const auto record = item->data(Qt::UserRole).toJsonObject();
        const auto hash = record.value("encodedSha256").toString(record.value("encoded_sha256").toString());
        if (hash.isEmpty() || hash != m_result.value("encodedSha256").toString())
        {
            m_referenceStatus->setText(
                tr("This case is not paired with the current audio hash. Audio navigation is unavailable."));
            return;
        }
        const auto start = record.value("startSeconds"), end = record.value("endSeconds");
        if (start.isDouble() && std::isfinite(start.toDouble()))
        {
            emit seekRequested(start.toDouble() * 1000);
            if (end.isDouble() && end.toDouble() > start.toDouble())
                emit rangeRequested(start.toDouble() * 1000, end.toDouble() * 1000);
        }
    });
    connect(canvas, &AnalysisCanvas::viewportChanged, this, qOverload<>(&QWidget::update));
    setLevel(0);
}
void AnalysisWorkbench::adoptTable(const QString &id, const QString &label, QTableWidget *table)
{
    int group =
        (id == "tempoCandidates"                                                                            ? 0
         : id == "windows" || id == "windowCandidates" || id == "periodicityLayers" || id == "rhythmLayers" ? 2
                                                                                                            : 1);
    m_tables[id] = table;
    m_groups[group]->addTab(table, label);
    if (id == "periodicityLayers" || id == "rhythmLayers")
        connect(table, &QTableWidget::currentCellChanged, this, [this, table](int row, int, int, int) {
            if (table->item(row, 0))
                emit evidenceSelected(table->item(row, 0)->data(Qt::UserRole).toJsonObject());
        });
    if (id == "windows")
        connect(table, &QTableWidget::currentCellChanged, this, [this, table](int row, int, int, int) {
            if (!table->item(row, 0))
                return;
            const auto w = table->item(row, 0)->data(Qt::UserRole).toJsonObject();
            m_evidence->setPlainText(
                QString::fromUtf8(QJsonDocument(QJsonObject{{"windowId", w.value("id")},
                                                            {"signalMetrics", w.value("signalMetrics")},
                                                            {"evidenceReasons", w.value("evidenceReasons")},
                                                            {"rawRhythmCandidates", w.value("rawRhythmCandidates")},
                                                            {"reliability", w.value("reliability")},
                                                            {"estimatorMessage", w.value("estimatorMessage")}})
                                      .toJson()));
        });
}
void AnalysisWorkbench::showWindowCandidates()
{
    showTable("windowCandidates");
}
bool AnalysisWorkbench::trackVisible(const QString &id) const
{
    const auto *check = m_trackChecks.value(id);
    return !check || check->isChecked();
}
void AnalysisWorkbench::showTable(const QString &id)
{
    auto *table = m_tables.value(id);
    if (!table)
        return;
    for (int i = 0; i < 3; ++i)
        if (m_groups[i]->indexOf(table) >= 0)
        {
            m_tabs->setCurrentIndex(i);
            m_groups[i]->setCurrentWidget(table);
            return;
        }
}
QWidget *AnalysisWorkbench::currentDiagnosticTable() const
{
    int i = m_tabs->currentIndex();
    return i >= 0 && i < 3 ? m_groups[i]->currentWidget() : nullptr;
}
void AnalysisWorkbench::setLevel(int level)
{
    m_objectDetails->setVisible(level >= 2 && !m_selectedObject.isEmpty());
    m_tabs->setTabVisible(3, level >= 2);
    m_evidence->setVisible(level >= 2);
    if (level < 2 && m_tabs->currentIndex() == 3)
        m_tabs->setCurrentIndex(0);
}
void AnalysisWorkbench::selectObject(const QString &trackId, const QString &handle, const QJsonObject &details)
{
    m_selectedObject = details;
    m_selectedObject["trackId"] = trackId;
    m_selectedObject["handle"] = handle;
    m_objectDetails->setPlainText(QString::fromUtf8(QJsonDocument(m_selectedObject).toJson()));
    m_objectDetails->setVisible(m_tabs->isTabVisible(3));
}
void AnalysisWorkbench::setResult(const QJsonObject &result)
{
    if (result.isEmpty())
    {
        m_selectedObject = {};
        m_objectDetails->clear();
        m_objectDetails->hide();
    }
    m_result = result;
    QSignalBlocker block(m_hypotheses);
    m_hypotheses->clear();
    m_hypotheses->addItem(tr("Final selected result"));
    for (auto v : result.value("tempoHypotheses").toArray())
    {
        auto h = v.toObject();
        m_hypotheses->addItem(text(h.value("kind"))
                              + (h.value("selected").toBool() ? tr(" (selected by Core)") : tr(" (alternative)")));
    }
    auto map = result.value("tempoMap").toObject();
    QVector<double> residuals;
    for (auto v : result.value("derivedAnchorFit").toArray())
    {
        auto r = v.toObject().value("residualMilliseconds");
        if (r.isDouble() && std::isfinite(r.toDouble()))
            residuals.append(qAbs(r.toDouble()));
    }
    std::sort(residuals.begin(), residuals.end());
    auto quantile = [&](double q) {
        if (residuals.isEmpty())
            return tr("unavailable");
        const double position = q * (residuals.size() - 1);
        const auto lower = qsizetype(std::floor(position)), upper = qsizetype(std::ceil(position));
        return QString::number(residuals[lower] + (position - lower) * (residuals[upper] - residuals[lower]), 'g', 8);
    };
    m_overview->setText(
        result.isEmpty()
            ? tr("Analyze audio to inspect results.")
            : tr("Legacy BPM: %1 · Legacy offset: %2 ms\nAnalysis: %3 · Core: %4\nRequested: %5 ms + %6 ms · Actual: "
                 "%7 ms + %8 s\nInput: %9\nConfig hash: %10\nObservation/model |residual| P50 / P90 / max: %11 / %12 / "
                 "%13 ms\nBPM list/model error: %14 ms · not reference accuracy")
                  .arg(text(result.value("legacyBpm")), text(result.value("legacyOffsetMilliseconds")),
                       text(result.value("analysisStatus")), text(result.value("algorithmVersion")),
                       text(result.value("requestedStartMs")), text(result.value("requestedDurationMs")),
                       text(result.value("analysisStartMs")), text(result.value("durationSeconds")),
                       text(result.value("analysisPcmSha256")).left(16), text(result.value("configHash")).left(16),
                       quantile(.5), quantile(.9), quantile(1),
                       text(map.value("bpmListAvailable").toBool() ? map.value("maximumBpmListModelErrorMilliseconds")
                                                                   : QJsonValue())));
    if (!result.isEmpty())
        m_overview->setText(tr("Result state: %1\n").arg(result.value("resultState").toString("Ready"))
                            + m_overview->text());
    if (result.isEmpty())
        m_evidence->clear();
    updateCompare();
}
QJsonObject AnalysisWorkbench::pairedBaseline() const
{
    const auto current = m_result.isEmpty() ? m_storedCurrent : m_result;
    const auto hash = current.value("analysisPcmSha256").toString();
    return !hash.isEmpty() && hash == m_record.value("analysisPcmSha256").toString()
                   && current.value("analysisStartMs").isDouble() && current.value("analysisFrameCount").toDouble() > 0
                   && current.value("analysisSampleRate").toDouble() > 0
                   && !current.value("analysisPcmEncoding").toString().isEmpty()
                   && current.value("analysisStartMs") == m_record.value("analysisStartMs")
                   && current.value("analysisFrameCount") == m_record.value("analysisFrameCount")
                   && current.value("analysisSampleRate") == m_record.value("analysisSampleRate")
                   && current.value("analysisPcmEncoding") == m_record.value("analysisPcmEncoding")
               ? m_record
               : QJsonObject();
}
void AnalysisWorkbench::updateCompare()
{
    if (m_record.isEmpty())
    {
        m_compare->setText(tr("Load a same-input baseline to compare. Scores and costs are not probabilities."));
        return;
    }
    const auto current = m_result.isEmpty() ? m_storedCurrent : m_result;
    const bool paired = !pairedBaseline().isEmpty();
    auto a = current.value("tempoMap").toObject(), b = m_record.value("tempoMap").toObject();
    QString message = paired
                          ? tr("Same decoded input verified; options and Core versions are recorded independently.\n")
                          : tr("Inputs are not verified identical. Browsing only; accuracy comparison unavailable.\n");
    if (paired && a.value("available").toBool() && b.value("available").toBool())
    {
        double start = qMax(a.value("startSeconds").toDouble(), b.value("startSeconds").toDouble()),
               end = qMin(a.value("endSeconds").toDouble(), b.value("endSeconds").toDouble());
        message += tr("Current coverage: %1–%2 s; baseline: %3–%4 s\nCommon interval: %5; unmatched edges "
                      "retained.\nCurrent config: %6; baseline config: %7\nCurrent Core: %8; baseline Core: %9")
                       .arg(text(a.value("startSeconds")), text(a.value("endSeconds")), text(b.value("startSeconds")),
                            text(b.value("endSeconds")),
                            end > start ? QStringLiteral("%1–%2 s").arg(start).arg(end) : tr("none"),
                            text(current.value("configHash")), text(m_record.value("configHash")),
                            text(current.value("algorithmVersion")), text(m_record.value("algorithmVersion")));
    }
    m_compare->setText(message);
}
bool AnalysisWorkbench::loadRecord(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
    {
        if (error)
            *error = tr("Cannot read record (maximum 32 MiB).");
        return false;
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error)
            *error = tr("Invalid record JSON.");
        return false;
    }
    auto data = doc.object();
    if (data.contains("schemaVersion") && data.value("schemaVersion").toInt() != 1)
    {
        if (error)
            *error = tr("Unsupported diagnostics schema; current results unchanged.");
        return false;
    }
    QJsonObject storedCurrent;
    if (data.value("kind").toString() == "comparison")
    {
        if (!data.value("current").isObject() || !data.value("baseline").isObject())
        {
            if (error)
                *error = tr("Incomplete comparison record.");
            return false;
        }
        storedCurrent = data.value("current").toObject();
        data = data.value("baseline").toObject();
        for (const auto &entry : {data, storedCurrent})
            if (entry.contains("schemaVersion") && entry.value("schemaVersion").toInt() != 1)
            {
                if (error)
                    *error = tr("Unsupported nested diagnostics schema; records unchanged.");
                return false;
            }
    }
    QJsonArray rows = data.value("cases").toArray();
    if (rows.isEmpty())
        rows = data.value("results").toArray();
    if (rows.isEmpty())
        rows = data.value("rows").toArray();
    if (rows.isEmpty() && !data.contains("analysisStatus"))
    {
        if (error)
            *error = tr("Expected diagnostics or a corpus/benchmark summary.");
        return false;
    }
    QJsonArray expanded;
    for (auto v : rows)
    {
        auto row = v.toObject(), runs = row.value("runs").toObject();
        if (runs.isEmpty())
            expanded.append(row);
        else
            for (auto it = runs.begin(); it != runs.end(); ++it)
            {
                auto copy = row;
                copy.remove("runs");
                copy["mode"] = it.key();
                copy["recordedRun"] = it.value();
                copy["encodedSha256"] = row.value("audio_sha256");
                expanded.append(copy);
            }
    }
    rows = expanded;
    m_record = data;
    m_storedCurrent = storedCurrent;
    m_loadedRecord->setPlainText(QString::fromUtf8(doc.toJson()));
    m_tests->setRowCount(0);
    const qsizetype shown = qMin(rows.size(), qsizetype(2000));
    for (qsizetype i = 0; i < shown; ++i)
    {
        auto v = rows[i];
        auto r = v.toObject();
        int row = m_tests->rowCount();
        m_tests->insertRow(row);
        QString status = r.value("accuracy_status")
                             .toString(r.value("reference_status").toString(r.value("status").toString("UNANNOTATED")));
        QString type = r.value("reference_type").toString(data.value("reference_type").toString("audio_only"));
        QStringList cells{r.value("case").toString(r.value("case_id").toString()) + " / " + r.value("mode").toString(),
                          status,
                          type,
                          text(r.value("strict_relative_error_max")),
                          text(r.value("phase_error_max_ms")),
                          text(r.value("coverage")) + " " + r.value("reason").toString() + " "
                              + text(r.value("map_integrity_issues"))};
        for (int c = 0; c < cells.size(); ++c)
        {
            auto *item = new QTableWidgetItem(cells[c]);
            if (c == 0)
                item->setData(Qt::UserRole, r);
            m_tests->setItem(row, c, item);
        }
    }
    m_referenceStatus->setText(
        tr("Showing %1 of %2 existing records. Statuses are recorded results, not a new verification. Source/reference "
           "pairing is required before accuracy or navigation. No test was started.")
            .arg(shown)
            .arg(rows.size()));
    updateCompare();
    emit trackVisibilityChanged();
    return true;
}
QJsonObject AnalysisWorkbench::comparisonRecord() const
{
    return {{"schemaVersion", 1},
            {"kind", "comparison"},
            {"current", m_result.isEmpty() ? m_storedCurrent : m_result},
            {"baseline", m_record},
            {"comparisonExplanation", m_compare->text()},
            {"verification", "existing local records; no new tests"}};
}
