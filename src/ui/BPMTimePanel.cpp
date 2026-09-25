#include "BPMTimePanel.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "model/BpmEntry.h"
#include "model/Chart.h"
#include "ui/dialogs/BpmMeasureDialog.h"
#include "ui/BpmMeasureUtils.h"
#include "utils/MathUtils.h"
#include "audio/BpmDetector.h"
#include <QFileInfo>
#include <QDir>
#include <QListWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QStringList>
#include <QTextStream>
#include <QSizePolicy>
#include <QtMath>

#include <atomic>
#include <memory>

BPMTimePanel::BPMTimePanel(QWidget *parent)
    : RightPanel(parent), m_chartController(nullptr), m_playbackController(nullptr), m_selectedIndex(-1)
{
    setupUi();
}

void BPMTimePanel::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // BPM 列表
    m_bpmListWidget = new QListWidget(this);
    mainLayout->addWidget(m_bpmListWidget);
    connect(m_bpmListWidget, &QListWidget::currentRowChanged, this, &BPMTimePanel::onItemSelected);

    // 编辑区域
    QHBoxLayout *timeLayout = new QHBoxLayout;
    m_timeLabel = new QLabel(tr("Time:"));
    timeLayout->addWidget(m_timeLabel);
    m_timeEdit = new QLineEdit(this);
    m_timeEdit->setPlaceholderText(tr("e.g. 0:1/1"));
    timeLayout->addWidget(m_timeEdit);
    mainLayout->addLayout(timeLayout);

    QHBoxLayout *bpmLayout = new QHBoxLayout;
    m_bpmLabel = new QLabel(tr("BPM:"));
    bpmLayout->addWidget(m_bpmLabel);
    m_bpmSpin = new QDoubleSpinBox(this);
    m_bpmSpin->setRange(1, 999);
    m_bpmSpin->setDecimals(3);
    m_bpmSpin->setValue(120);
    bpmLayout->addWidget(m_bpmSpin);
    mainLayout->addLayout(bpmLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("Add/Update"), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    m_addBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_removeBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    mainLayout->addLayout(btnLayout);

    // Measure BPM button
    m_measureBtn = new QPushButton(tr("Measure BPM..."), this);
    m_measureBtn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    mainLayout->addWidget(m_measureBtn);

    mainLayout->addStretch();

    connect(m_addBtn, &QPushButton::clicked, this, &BPMTimePanel::onAddClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &BPMTimePanel::onRemoveClicked);
    connect(m_measureBtn, &QPushButton::clicked, this, &BPMTimePanel::onMeasureBpmClicked);
    connect(m_bpmSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BPMTimePanel::onBpmChanged);
}

void BPMTimePanel::refreshBpmList()
{
    if (!m_chartController || !m_chartController->chart())
        return;
    m_bpmListWidget->clear();
    const auto &bpmList = m_chartController->chart()->bpmList();
    for (int i = 0; i < bpmList.size(); ++i)
    {
        const BpmEntry &bpm = bpmList[i];
        QString text = QString("%1:%2/%3\t%4")
                           .arg(bpm.beatNum)
                           .arg(bpm.numerator)
                           .arg(bpm.denominator)
                           .arg(bpm.bpm, 0, 'f', 3);
        m_bpmListWidget->addItem(text);
    }
}

void BPMTimePanel::onItemSelected(int row)
{
    if (!m_chartController || !m_chartController->chart())
    {
        m_selectedIndex = -1;
        return;
    }

    if (row < 0)
    {
        m_selectedIndex = -1;
        m_timeEdit->clear();
        m_bpmSpin->setValue(120);
        return;
    }
    m_selectedIndex = row;
    const auto &bpmList = m_chartController->chart()->bpmList();
    if (row < bpmList.size())
    {
        const BpmEntry &bpm = bpmList[row];
        m_timeEdit->setText(QString("%1:%2/%3").arg(bpm.beatNum).arg(bpm.numerator).arg(bpm.denominator));
        m_bpmSpin->setValue(bpm.bpm);
    }
}

void BPMTimePanel::onAddClicked()
{
    if (!m_chartController)
        return;
    // 解析时间
    QString timeStr = m_timeEdit->text();
    int beat = 0, num = 1, den = 1;
    if (timeStr.contains(':'))
    {
        QStringList parts = timeStr.split(':');
        if (parts.size() >= 2)
        {
            beat = parts[0].toInt();
            QString fraction = parts[1];
            if (fraction.contains('/'))
            {
                QStringList fracParts = fraction.split('/');
                if (fracParts.size() == 2)
                {
                    num = fracParts[0].toInt();
                    den = fracParts[1].toInt();
                }
            }
            else
            {
                num = fraction.toInt();
                den = 1;
            }
        }
    }
    BpmEntry newBpm(beat, num, den, m_bpmSpin->value());
    if (m_selectedIndex >= 0)
    {
        m_chartController->updateBpm(m_selectedIndex, newBpm);
        m_selectedIndex = -1;
    }
    else
    {
        m_chartController->addBpm(newBpm);
    }
    refreshBpmList();
    m_timeEdit->clear();
    m_bpmSpin->setValue(120);
}

void BPMTimePanel::onRemoveClicked()
{
    if (!m_chartController)
        return;
    if (m_selectedIndex >= 0)
    {
        m_chartController->removeBpm(m_selectedIndex);
        m_selectedIndex = -1;
        refreshBpmList();
    }
}

void BPMTimePanel::onBpmChanged(double)
{
    // 可实时预览，但暂时不做
}

QString BPMTimePanel::buildMeasurementDetails(const BpmDetector::DetectionResult &result,
                                              bool fromStart) const
{
    const auto percent = [](double value)
    {
        return QStringLiteral("%1%").arg(QString::number(qBound(0.0, value, 1.0) * 100.0, 'f', 0));
    };
    const auto ranges = [this](const auto &regions)
    {
        constexpr int kMaximumVisibleRanges = 3;
        QStringList values;
        const int visibleCount = qMin(kMaximumVisibleRanges, static_cast<int>(regions.size()));
        for (int i = 0; i < visibleCount; ++i)
        {
            values.append(tr("%1-%2 s")
                              .arg(QString::number(regions[i].startSeconds, 'f', 1),
                                   QString::number(regions[i].endSeconds, 'f', 1)));
        }
        if (regions.size() > visibleCount)
            values.append(tr("+%1 more").arg(regions.size() - visibleCount));
        return values.isEmpty() ? tr("none") : values.join(QStringLiteral(", "));
    };
    const auto rhythmSummary = [this](const QVector<AutoTiming2RhythmProfile> &profiles)
    {
        constexpr int kMaximumVisibleProfiles = 4;
        constexpr int kMaximumVisibleLayers = 3;
        QStringList profileValues;
        const int visibleProfiles = qMin(kMaximumVisibleProfiles, static_cast<int>(profiles.size()));
        for (int i = 0; i < visibleProfiles; ++i)
        {
            const AutoTiming2RhythmProfile &profile = profiles[i];
            QStringList layerValues;
            const int visibleLayers = qMin(kMaximumVisibleLayers, static_cast<int>(profile.layers.size()));
            for (int j = 0; j < visibleLayers; ++j)
            {
                const AutoTiming2RhythmLayer &layer = profile.layers[j];
                if (layer.role == QStringLiteral("subdivision_candidate"))
                {
                    layerValues.append(tr("x%1 subdivision candidate")
                                           .arg(layer.ratioNumerator > 0
                                                    ? QString::number(layer.ratioNumerator)
                                                    : QString::number(layer.relativeRate, 'f', 2)));
                }
                else if (layer.role == QStringLiteral("polyrhythm_candidate") &&
                         layer.ratioNumerator > 0 && layer.ratioDenominator > 0)
                {
                    layerValues.append(tr("%1:%2 polyrhythm candidate")
                                           .arg(layer.ratioNumerator)
                                           .arg(layer.ratioDenominator));
                }
                else if (layer.role == QStringLiteral("texture_candidate"))
                {
                    layerValues.append(tr("x%1 texture candidate")
                                           .arg(QString::number(layer.relativeRate, 'f', 2)));
                }
                else
                {
                    layerValues.append(tr("x%1 unresolved rhythm candidate")
                                           .arg(QString::number(layer.relativeRate, 'f', 2)));
                }
            }
            if (profile.layers.size() > visibleLayers)
                layerValues.append(tr("+%1 more").arg(profile.layers.size() - visibleLayers));
            profileValues.append(tr("%1-%2 s: %3")
                                     .arg(QString::number(profile.startSeconds, 'f', 1),
                                          QString::number(profile.endSeconds, 'f', 1),
                                          layerValues.isEmpty() ? tr("unresolved")
                                                                : layerValues.join(QStringLiteral(" + "))));
        }
        if (profiles.size() > visibleProfiles)
            profileValues.append(tr("+%1 more profiles").arg(profiles.size() - visibleProfiles));
        return profileValues.isEmpty() ? tr("none")
                                       : profileValues.join(QStringLiteral("; "));
    };

    QString details;
    QTextStream stream(&details);
    stream << tr("Mode: ")
           << (fromStart ? tr("From Song Start") : tr("From Current Time")) << "\n";
    stream << tr("Analysis starts at audio time: ")
           << QString::number(result.analysisStartMs / 1000.0, 'f', 1) << tr(" s") << "\n\n";

    if (result.hasLegacyResult())
    {
        stream << tr("Legacy BPM: ") << QString::number(result.bpm, 'f', 3) << "\n";
        if (fromStart)
            stream << tr("Legacy offset: ")
                   << QString::number(result.estimatedOffsetMs, 'f', 1) << tr(" ms") << "\n";
    }
    else
    {
        stream << tr("Legacy BPM: unavailable") << "\n";
        if (!result.legacyError.isEmpty())
            stream << tr("Legacy reason: ") << result.legacyError << "\n";
    }

    stream << "\n";
    switch (result.analysisStatus)
    {
    case BpmDetector::AnalysisStatus::Succeeded:
    {
        const BpmMeasureUtils::Recommendation recommendation =
            BpmMeasureUtils::selectRecommendation(result);
        if (recommendation.available)
        {
            stream << tr("AutoTiming 2 recommendation: ")
                   << QString::number(recommendation.bpm, 'f', 3) << tr(" BPM") << "\n";
            stream << tr("Evidence: ")
                   << (recommendation.quality == BpmMeasureUtils::EvidenceQuality::Supported
                           ? tr("supported")
                           : tr("uncertain"))
                   << "\n";

            if (!recommendation.familyBpms.isEmpty())
            {
                QStringList familyValues;
                for (double bpm : recommendation.familyBpms)
                    familyValues.append(QString::number(bpm, 'f', 2));
                stream << tr("Power-of-two tempo family: ")
                       << familyValues.join(QStringLiteral(" / ")) << tr(" BPM") << "\n";
            }
            else if (recommendation.familyReferenceBpm > 0.0)
            {
                stream << tr("Power-of-two tempo family reference: ")
                       << QString::number(recommendation.familyReferenceBpm, 'f', 2)
                       << tr(" BPM") << "\n";
            }
        }
        else
        {
            stream << tr("AutoTiming 2 recommendation: no stable candidate") << "\n";
            stream << tr("Evidence: uncertain") << "\n";
        }

        const AutoTiming2Confidence &confidence = result.analysis.confidence;
        stream << tr("Confidence - overall: %1, tempo: %2, family: %3")
                      .arg(percent(confidence.overall),
                           percent(confidence.tempo),
                           percent(confidence.tempoFamily))
               << "\n";
        stream << tr("Reliable coverage: ") << percent(confidence.reliableCoverage) << "\n";
        stream << tr("Reliable sections: ") << ranges(result.analysis.anchors) << "\n";
        stream << tr("Uncertain sections: ") << ranges(result.analysis.uncertainRegions) << "\n";
        stream << tr("Phase-refined tracker points: %1 (stable-grid corrections: %2)")
                      .arg(result.analysis.multiScalePhaseRefinedCount)
                      .arg(result.analysis.stableGridRegularizedCount)
               << "\n";
        stream << tr("Rhythm candidates (diagnostic only): ")
               << rhythmSummary(result.analysis.rhythmProfiles) << "\n";
        break;
    }
    case BpmDetector::AnalysisStatus::Failed:
        stream << tr("AutoTiming 2: unavailable") << "\n";
        if (!result.analysisError.isEmpty())
            stream << tr("AutoTiming 2 reason: ") << result.analysisError << "\n";
        break;
    case BpmDetector::AnalysisStatus::Cancelled:
        stream << tr("AutoTiming 2: cancelled") << "\n";
        break;
    case BpmDetector::AnalysisStatus::NotRequested:
        stream << tr("AutoTiming 2: not run") << "\n";
        break;
    }
    return details.trimmed();
}

void BPMTimePanel::onMeasureBpmClicked()
{
    if (!m_chartController || !m_chartController->chart())
        return;

    struct MeasureSession
    {
        QPointer<ChartController> controller;
        QString chartPath;
        QString audioPath;
        quint64 chartRevision = 0;
        quint64 requestId = 0;
        std::shared_ptr<std::atomic<bool>> cancelFlag;
        double targetChartTimeMs = 0.0;
        int targetOffsetMs = 0;
        bool measuredFromStart = true;
        bool hasCompletedResult = false;
        BpmMeasureUtils::TimingMapProposal timingMap;
    };

    const double chartMs = currentChartTimeMs();
    const int offsetMs = m_chartController->chart()->meta().offset;
    const double audioMs = qMax(0.0, chartMs + static_cast<double>(offsetMs));
    const QString timeStr = tr("%1 ms").arg(QString::number(audioMs, 'f', 0));
    BpmMeasureDialog dialog(this);
    dialog.setCurrentTimeText(timeStr);
    dialog.setStatusText(tr("Ready to measure."));

    const auto session = std::make_shared<MeasureSession>();
    session->controller = m_chartController;
    session->chartPath = m_chartController->chartFilePath();
    session->audioPath = currentAudioFilePath();
    session->chartRevision = m_chartController->revision();

    connect(&dialog,
            &QDialog::finished,
            &dialog,
            [session](int)
            {
                ++session->requestId;
                if (session->cancelFlag)
                    session->cancelFlag->store(true);
            });

    connect(&dialog,
            &BpmMeasureDialog::measureRequested,
            this,
            [this, &dialog, session](int durationSeconds, int mode)
            {
        dialog.setStatusText(tr("Measuring audio..."));

        const QPointer<BpmMeasureDialog> dialogGuard(&dialog);
        const bool fromStart = mode == static_cast<int>(BpmMeasureDialog::MeasureMode::FromStart);
        const QString audioPath = currentAudioFilePath();
        const bool sessionCurrent = session->controller &&
                                    m_chartController == session->controller.data() &&
                                    m_chartController->chartFilePath() == session->chartPath &&
                                    m_chartController->revision() == session->chartRevision &&
                                    audioPath == session->audioPath;
        if (durationSeconds <= 0 || audioPath.isEmpty() || !sessionCurrent)
        {
            dialog.setMeasuring(false);
            dialog.setStatusText(tr("Measurement failed."));
            QMessageBox::warning(this,
                                 tr("Measurement Failed"),
                                 !sessionCurrent
                                     ? tr("The chart changed. Reopen Measure BPM and try again.")
                                     : durationSeconds <= 0
                                           ? tr("Duration must be greater than 0.")
                                           : tr("No audio file is linked to this chart."));
            return;
        }

        if (session->cancelFlag)
            session->cancelFlag->store(true);
        session->cancelFlag = std::make_shared<std::atomic<bool>>(false);
        session->hasCompletedResult = false;
        const std::shared_ptr<std::atomic<bool>> cancelFlag = session->cancelFlag;
        const quint64 requestId = ++session->requestId;
        const double requestChartMs = fromStart ? 0.0 : currentChartTimeMs();
        const int requestOffsetMs = m_chartController->chart()->meta().offset;
        const double audioStartMs = qMax(0.0,
                                         requestChartMs + static_cast<double>(requestOffsetMs));
        dialog.setCurrentTimeText(tr("%1 ms").arg(QString::number(audioStartMs, 'f', 0)));
        BpmDetector::analyzeFromFileDetailedAsync(
            &dialog,
            audioPath,
            audioStartMs,
            durationSeconds * 1000.0,
            [this,
             dialogGuard,
             session,
             requestId,
             fromStart,
             requestChartMs,
             requestOffsetMs](bool pipelineCompleted,
                              BpmDetector::DetectionResult result,
                              const QString &sharedError)
            {
                if (!dialogGuard || requestId != session->requestId)
                    return;
                BpmMeasureDialog &dialog = *dialogGuard;

                const bool sessionCurrent = session->controller &&
                                            m_chartController == session->controller.data() &&
                                            m_chartController->chartFilePath() == session->chartPath &&
                                            m_chartController->revision() == session->chartRevision &&
                                            currentAudioFilePath() == session->audioPath;
                if (!sessionCurrent)
                {
                    dialog.setMeasuring(false);
                    dialog.setStatusText(tr("Chart changed; result discarded."));
                    dialog.setResultDetailsText(tr("The chart or linked audio changed while analysis was running. Measure again."));
                    return;
                }

                if (!pipelineCompleted)
                {
                    dialog.setMeasuring(false);
                    dialog.setStatusText(tr("Measurement failed."));
                    QMessageBox::warning(
                        this,
                        tr("Measurement Failed"),
                        sharedError.isEmpty() ? tr("Failed to prepare audio for BPM measurement.") : sharedError);
                    return;
                }

                session->targetChartTimeMs = requestChartMs;
                session->targetOffsetMs = requestOffsetMs;
                session->measuredFromStart = fromStart;
                session->hasCompletedResult = true;
                session->timingMap = BpmMeasureUtils::TimingMapProposal{};

                if (result.hasLegacyResult())
                {
                    dialog.setMeasuredBpm(result.bpm);
                    if (fromStart)
                        dialog.setMeasuredOffset(qRound(result.estimatedOffsetMs));
                }
                else
                {
                    dialog.setLegacyUnavailable();
                }

                const BpmMeasureUtils::Recommendation recommendation =
                    BpmMeasureUtils::selectRecommendation(result);
                if (recommendation.available)
                {
                    const QString qualifier =
                        recommendation.quality == BpmMeasureUtils::EvidenceQuality::Supported
                            ? tr("(supported)")
                            : tr("(uncertain)");
                    dialog.setAutoTimingSuggestion(recommendation.bpm, qualifier);
                }
                else if (result.analysisStatus == BpmDetector::AnalysisStatus::Succeeded)
                {
                    dialog.setAutoTimingUnavailable(tr("No stable recommendation"));
                }
                else if (result.analysisStatus == BpmDetector::AnalysisStatus::Cancelled)
                {
                    dialog.setAutoTimingUnavailable(tr("Cancelled"));
                }
                else
                {
                    dialog.setAutoTimingUnavailable(tr("Unavailable"));
                }

                if (result.hasAnalysis())
                {
                    session->timingMap = BpmMeasureUtils::buildTimingMapProposal(
                        result.analysis,
                        m_chartController->chart()->bpmList(),
                        requestOffsetMs);
                }
                if (session->timingMap.available && session->timingMap.hasTempoChange)
                {
                    dialog.setAutoTimingMapSuggestion(
                        tr("%1 core-generated BPM points from %2 phase anchors; range %3-%4 s; detected-anchor fit %5 ms; interpolation bound %6 ms. Existing timing before the first anchor is preserved.")
                            .arg(session->timingMap.generatedEntryCount)
                            .arg(session->timingMap.sourceAnchorCount)
                            .arg(session->timingMap.sourceStartSeconds, 0, 'f', 2)
                            .arg(session->timingMap.sourceEndSeconds, 0, 'f', 2)
                            .arg(session->timingMap.maximumAnchorResidualMs, 0, 'f', 3)
                            .arg(session->timingMap.maximumModelErrorMs, 0, 'f', 3));
                }
                else
                {
                    dialog.setAutoTimingMapUnavailable(
                        session->timingMap.unavailableReason.isEmpty()
                            ? tr("No variable-tempo map detected")
                            : session->timingMap.unavailableReason);
                }

                BpmMeasureUtils::MultiplierHint multiplierHint;
                if (result.hasLegacyResult() && recommendation.available)
                    multiplierHint = BpmMeasureUtils::findMultiplierHint(result.bpm, recommendation.bpm);
                if (multiplierHint)
                {
                    const QString hintText =
                        multiplierHint.relation == BpmMeasureUtils::MultiplierRelation::PowerOfTwo
                            ? tr("AutoTiming 2 matches measured BPM x%1 (a power-of-two tempo relation). You can use the existing x%1 button.")
                                  .arg(multiplierHint.factor)
                            : tr("AutoTiming 2 is numerically close to measured BPM x%1. Use the existing x%1 button only if that grid is intended.")
                                  .arg(multiplierHint.factor);
                    dialog.setMultiplierHint(multiplierHint.factor, hintText);
                }
                else
                {
                    dialog.setMultiplierHint(0, QString());
                }

                dialog.setResultDetailsText(buildMeasurementDetails(result, fromStart));
                if (result.hasLegacyResult() && recommendation.available)
                    dialog.setStatusText(tr("Measurement complete. Review the AutoTiming 2 suggestion before applying."));
                else if (result.hasLegacyResult())
                    dialog.setStatusText(tr("Legacy measurement complete; AutoTiming 2 has no usable suggestion."));
                else if (recommendation.available)
                    dialog.setStatusText(tr("Legacy measurement unavailable; AutoTiming 2 suggestion is ready for review."));
                else
                    dialog.setStatusText(tr("Analysis complete, but no reliable BPM result was found."));
                dialog.setMeasurementComplete();
            },
            AutoTiming2Options{},
            cancelFlag);
            });

    if (dialog.exec() == QDialog::Accepted)
    {
        const bool sessionCurrent = session->controller &&
                                    m_chartController == session->controller.data() &&
                                    m_chartController->chartFilePath() == session->chartPath &&
                                    m_chartController->revision() == session->chartRevision &&
                                    currentAudioFilePath() == session->audioPath;
        if (!sessionCurrent || !session->hasCompletedResult)
        {
            QMessageBox::warning(this,
                                 tr("Measurement Expired"),
                                 tr("The chart changed after measurement. No timing data was written."));
            return;
        }

        if (dialog.applyAutoTimingMap())
        {
            const BpmMeasureUtils::TimingMapProposal &proposal = session->timingMap;
            if (!proposal.available || !proposal.hasTempoChange || proposal.bpmList.isEmpty())
            {
                QMessageBox::warning(
                    this,
                    tr("Timing Map Unavailable"),
                    tr("The AutoTiming 2 timing-map proposal is no longer available. No timing data was written."));
                return;
            }

            const QMessageBox::StandardButton mapReply = QMessageBox::question(
                this,
                tr("Apply AutoTiming 2 BPM Map"),
                tr("Replace BPM entries from beat %1 onward with %2 generated timing points?\n\n"
                   "The map follows detected pulse anchors through %3-%4 s and keeps earlier BPM entries. "
                   "It is audio-derived, not chart ground truth, and can be undone as one action.")
                    .arg(proposal.firstAnchorBeat, 0, 'f', 4)
                    .arg(proposal.generatedEntryCount)
                    .arg(proposal.sourceStartSeconds, 0, 'f', 2)
                    .arg(proposal.sourceEndSeconds, 0, 'f', 2),
                QMessageBox::Yes | QMessageBox::No);
            if (mapReply != QMessageBox::Yes)
                return;

            Chart mutated = *m_chartController->chart();
            mutated.bpmList() = proposal.bpmList;
            if (!mutated.bpmList().isEmpty())
                mutated.meta().firstBpm = mutated.bpmList().first().bpm;
            if (!m_chartController->applyExternalChartMutation(
                    tr("Apply AutoTiming 2 BPM map"),
                    mutated))
            {
                QMessageBox::warning(
                    this,
                    tr("Timing Map Failed"),
                    tr("The BPM map could not be applied. No timing data was written."));
                return;
            }
            refreshBpmList();
            return;
        }

        const double measuredBpm = dialog.finalBpm();
        const bool fromStart = session->measuredFromStart;
        const BpmEntry newBpm = BpmMeasureUtils::makeTargetEntry(
            fromStart,
            session->targetChartTimeMs,
            m_chartController->chart()->bpmList(),
            session->targetOffsetMs,
            measuredBpm);

        // Ask user if they want to write this BPM at current time
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Write BPM"),
            fromStart
                ? tr("Write measured BPM %1 at chart start (0:0/1)?").arg(QString::number(measuredBpm, 'f', 2))
                : tr("Write measured BPM %1 at current time?").arg(QString::number(measuredBpm, 'f', 2)),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes)
        {
            m_chartController->addBpm(newBpm);
            refreshBpmList();
        }

        // Apply offset if checkbox was checked in dialog
        if (fromStart && dialog.applyOffset())
        {
            const int newOffset = dialog.finalOffset();
            MetaData meta = m_chartController->chart()->meta();
            meta.offset = newOffset;
            m_chartController->setMetaData(meta);
        }
    }
}

double BPMTimePanel::currentChartTimeMs() const
{
    if (!m_playbackController)
        return 0.0;
    return m_playbackController->currentTime();
}

QString BPMTimePanel::currentAudioFilePath() const
{
    if (!m_chartController || !m_chartController->chart())
        return QString();
    const QString chartPath = m_chartController->chartFilePath();
    if (chartPath.isEmpty())
        return QString();
    const QString audioRel = m_chartController->chart()->meta().audioFile.trimmed();
    if (audioRel.isEmpty())
        return QString();
    return QFileInfo(chartPath).absoluteDir().absoluteFilePath(audioRel);
}

void BPMTimePanel::setChartController(ChartController *controller)
{
    if (m_chartController)
    {
        disconnect(m_chartController, &ChartController::bpmListChanged, this, &BPMTimePanel::refreshBpmList);
    }

    m_chartController = controller;
    if (!m_chartController)
        return;

    connect(m_chartController, &ChartController::bpmListChanged, this, &BPMTimePanel::refreshBpmList, Qt::UniqueConnection);
    refreshBpmList();
}

void BPMTimePanel::setSelectionController(SelectionController *controller)
{
    Q_UNUSED(controller);
}

void BPMTimePanel::setPlaybackController(PlaybackController *controller)
{
    m_playbackController = controller;
}

void BPMTimePanel::retranslateUi()
{
    if (m_timeLabel)
        m_timeLabel->setText(tr("Time:"));
    if (m_bpmLabel)
        m_bpmLabel->setText(tr("BPM:"));
    if (m_addBtn)
        m_addBtn->setText(tr("Add/Update"));
    if (m_removeBtn)
        m_removeBtn->setText(tr("Remove"));
    if (m_measureBtn)
        m_measureBtn->setText(tr("Measure BPM..."));
    if (m_timeEdit)
        m_timeEdit->setPlaceholderText(tr("e.g. 0:1/1"));
}
