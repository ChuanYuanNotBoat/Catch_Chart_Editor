#include "BPMTimePanel.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "model/BpmEntry.h"
#include "model/Chart.h"
#include "ui/dialogs/BpmMeasureDialog.h"
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
#include <QStringList>
#include <QTextStream>
#include <QApplication>
#include <QtMath>

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
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    mainLayout->addLayout(btnLayout);

    // Measure BPM button
    m_measureBtn = new QPushButton(tr("Measure BPM..."), this);
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

void BPMTimePanel::onMeasureBpmClicked()
{
    if (!m_chartController || !m_chartController->chart())
        return;

    const double chartMs = currentChartTimeMs();
    const int offsetMs = m_chartController->chart()->meta().offset;
    const double audioMs = qMax(0.0, chartMs + static_cast<double>(offsetMs));
    const QString timeStr = tr("%1 ms").arg(QString::number(audioMs, 'f', 0));
    BpmMeasureDialog dialog(this);
    dialog.setCurrentTimeText(timeStr);
    dialog.setStatusText(tr("Ready to measure."));
    connect(&dialog, &BpmMeasureDialog::measureRequested, this, [this, &dialog](int durationSeconds, int mode)
            {
        dialog.setMeasuring(true);
        dialog.setStatusText(tr("Measuring audio..."));
        QApplication::processEvents();

        QString err;
        BpmDetector::DetectionResult result;
        if (!measureBpmFromAudio(durationSeconds, mode, result, &err))
        {
            dialog.setMeasuring(false);
            dialog.setStatusText(tr("Measurement failed."));
            QMessageBox::warning(this, tr("Measurement Failed"),
                err.isEmpty() ? tr("Failed to measure BPM from audio.") : err);
            return;
        }
        dialog.setMeasuredBpm(result.bpm);

        QVector<double> validBpms;
        for (const auto &seg : result.segments)
        {
            if (seg.valid)
                validBpms.append(seg.bpm);
        }
        double uncertainty = 0.0;
        if (validBpms.size() > 1)
        {
            double mean = 0.0;
            for (double v : validBpms)
                mean += v;
            mean /= static_cast<double>(validBpms.size());
            double var = 0.0;
            for (double v : validBpms)
            {
                const double d = v - mean;
                var += d * d;
            }
            var /= static_cast<double>(validBpms.size());
            uncertainty = qSqrt(var);
        }

        QString details;
        QTextStream s(&details);
        s << tr("Mode: ") << (mode == static_cast<int>(BpmMeasureDialog::MeasureMode::FromStart)
                                  ? tr("From Song Start") : tr("From Current Time")) << "\n";
        s << tr("Estimated BPM: ") << QString::number(result.bpm, 'f', 3) << "\n";
        s << tr("Uncertainty (segment stddev): ") << QString::number(uncertainty, 'f', 4) << "\n";
        if (mode == static_cast<int>(BpmMeasureDialog::MeasureMode::FromStart))
            s << tr("Estimated offset: ") << QString::number(result.estimatedOffsetMs, 'f', 1) << tr(" ms") << "\n";
        s << tr("Segments:") << "\n";
        for (int i = 0; i < result.segments.size(); ++i)
        {
            const auto &seg = result.segments[i];
            s << QString("#%1 ").arg(i + 1)
              << "[" << QString::number(seg.startMs, 'f', 0) << ", "
              << QString::number(seg.startMs + seg.durationMs, 'f', 0) << "]ms ";
            if (seg.valid)
                s << tr("bpm=") << QString::number(seg.bpm, 'f', 3) << tr(", score=") << QString::number(seg.score, 'f', 3);
            else
                s << tr("invalid");
            s << "\n";
        }
        dialog.setResultDetailsText(details);
        dialog.setStatusText(tr("Measurement complete."));
        dialog.setMeasuring(false);

        // If FromStart mode, fill the offset into the dialog's offset spinbox
        if (mode == static_cast<int>(BpmMeasureDialog::MeasureMode::FromStart))
        {
            const int measuredOffset = qRound(result.estimatedOffsetMs);
            dialog.setMeasuredOffset(measuredOffset);
        } });

    if (dialog.exec() == QDialog::Accepted)
    {
        const double measuredBpm = dialog.finalBpm();
        const bool fromStart = dialog.mode() == BpmMeasureDialog::MeasureMode::FromStart;
        int beat = 0, num = 0, den = 1;
        if (fromStart)
        {
            beat = 0;
            num = 1;
            den = 1;
        }
        else
        {
            MathUtils::msToBeat(chartMs, m_chartController->chart()->bpmList(), offsetMs, beat, num, den);
        }

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
            BpmEntry newBpm(beat, num, den, measuredBpm);
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

bool BPMTimePanel::measureBpmFromAudio(int durationSeconds,
                                       int mode,
                                       BpmDetector::DetectionResult &outResult,
                                       QString *outError) const
{
    outResult = BpmDetector::DetectionResult();
    if (durationSeconds <= 0)
    {
        if (outError)
            *outError = tr("Duration must be greater than 0.");
        return false;
    }
    const QString audioPath = currentAudioFilePath();
    if (audioPath.isEmpty())
    {
        if (outError)
            *outError = tr("No audio file is linked to this chart.");
        return false;
    }

    const bool fromStart = (mode == static_cast<int>(BpmMeasureDialog::MeasureMode::FromStart));
    const double chartMs = fromStart ? 0.0 : currentChartTimeMs();
    const int offsetMs = (m_chartController && m_chartController->chart()) ? m_chartController->chart()->meta().offset : 0;
    const double audioStartMs = qMax(0.0, chartMs + static_cast<double>(offsetMs));

    QString err;
    if (!BpmDetector::detectFromFileDetailed(audioPath, audioStartMs, durationSeconds * 1000.0, outResult, &err))
    {
        if (outError)
            *outError = err.isEmpty() ? tr("Audio BPM detection failed.") : err;
        return false;
    }
    return outResult.bpm > 0.0;
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