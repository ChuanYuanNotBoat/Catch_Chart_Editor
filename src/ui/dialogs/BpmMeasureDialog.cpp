#include "BpmMeasureDialog.h"
#include "utils/NativeWindowTheme.h"
#include "utils/Settings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QTextEdit>
#include <QProgressBar>
#include <QShortcut>
#include <QKeySequence>
#include <QCheckBox>

BpmMeasureDialog::BpmMeasureDialog(QWidget *parent)
    : QDialog(parent), m_measuredBpm(0.0), m_lastFinalBpm(0.0), m_measureDuration(120)
{
    setStyleSheet(NativeWindowTheme::dialogStyleSheet(Settings::instance().backgroundColor()));
    setupUi();
}

void BpmMeasureDialog::setupUi()
{
    setWindowTitle(tr("Measure BPM"));
    setMinimumWidth(500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Current time display
    QHBoxLayout *timeLayout = new QHBoxLayout;
    m_currentTimeLabel = new QLabel(tr("Current Time:"), this);
    QLabel *timeValue = new QLabel(tr("Not set"), this);
    timeLayout->addWidget(m_currentTimeLabel);
    timeLayout->addWidget(timeValue);
    timeLayout->addStretch();
    mainLayout->addLayout(timeLayout);

    // Measure duration
    QHBoxLayout *durationLayout = new QHBoxLayout;
    m_durationLabel = new QLabel(tr("Measure Duration (seconds):"), this);
    m_durationSpin = new QSpinBox(this);
    m_durationSpin->setRange(1, 120);
    m_durationSpin->setValue(120);
    m_durationSpin->setSuffix(" s");
    durationLayout->addWidget(m_durationLabel);
    durationLayout->addWidget(m_durationSpin);
    durationLayout->addStretch();
    mainLayout->addLayout(durationLayout);

    // Measure mode
    QHBoxLayout *modeLayout = new QHBoxLayout;
    m_modeLabel = new QLabel(tr("Measure Mode:"), this);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("From Song Start"), static_cast<int>(MeasureMode::FromStart));
    m_modeCombo->addItem(tr("From Current Time"), static_cast<int>(MeasureMode::FromCurrentTime));
    m_modeCombo->setCurrentIndex(0);
    modeLayout->addWidget(m_modeLabel);
    modeLayout->addWidget(m_modeCombo);
    modeLayout->addStretch();
    mainLayout->addLayout(modeLayout);

    // Result display with quick multiply buttons
    QHBoxLayout *resultLayout = new QHBoxLayout;
    m_resultLabel = new QLabel(tr("Measured BPM:"), this);
    m_resultEdit = new QLineEdit(this);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setText(tr("Click 'Measure' to start"));
    resultLayout->addWidget(m_resultLabel);
    resultLayout->addWidget(m_resultEdit);

    m_x2Btn = new QPushButton(tr("x2"), this);
    m_x2Btn->setMaximumWidth(40);
    m_x3Btn = new QPushButton(tr("x3"), this);
    m_x3Btn->setMaximumWidth(40);
    m_x4Btn = new QPushButton(tr("x4"), this);
    m_x4Btn->setMaximumWidth(40);
    m_x6Btn = new QPushButton(tr("x6"), this);
    m_x6Btn->setMaximumWidth(40);
    m_x8Btn = new QPushButton(tr("x8"), this);
    m_x8Btn->setMaximumWidth(40);
    resultLayout->addWidget(m_x2Btn);
    resultLayout->addWidget(m_x3Btn);
    resultLayout->addWidget(m_x4Btn);
    resultLayout->addWidget(m_x6Btn);
    resultLayout->addWidget(m_x8Btn);
    mainLayout->addLayout(resultLayout);

    m_statusLabel = new QLabel(tr("Ready."), this);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    m_detailsEdit = new QTextEdit(this);
    m_detailsEdit->setReadOnly(true);
    m_detailsEdit->setMinimumHeight(150);
    m_detailsEdit->setPlainText(tr("No segment data yet."));
    mainLayout->addWidget(m_detailsEdit);

    // Measure button
    m_measureBtn = new QPushButton(tr("Measure"), this);
    mainLayout->addWidget(m_measureBtn);

    // Final BPM to add (at bottom)
    QHBoxLayout *finalLayout = new QHBoxLayout;
    m_finalBpmLabel = new QLabel(tr("BPM to Add:"), this);
    m_finalBpmSpin = new QDoubleSpinBox(this);
    m_finalBpmSpin->setRange(1, 999);
    m_finalBpmSpin->setDecimals(3);
    m_finalBpmSpin->setValue(0);
    finalLayout->addWidget(m_finalBpmLabel);
    finalLayout->addWidget(m_finalBpmSpin);
    mainLayout->addLayout(finalLayout);

    // Final Offset to apply
    QHBoxLayout *offsetLayout = new QHBoxLayout;
    m_finalOffsetLabel = new QLabel(tr("Offset to Apply:"), this);
    m_finalOffsetSpin = new QSpinBox(this);
    m_finalOffsetSpin->setRange(-9999, 9999);
    m_finalOffsetSpin->setValue(0);
    m_finalOffsetSpin->setSuffix(" ms");
    m_applyOffsetCheck = new QCheckBox(tr("Apply offset"), this);
    m_applyOffsetCheck->setChecked(false);
    offsetLayout->addWidget(m_finalOffsetLabel);
    offsetLayout->addWidget(m_finalOffsetSpin);
    offsetLayout->addWidget(m_applyOffsetCheck);
    mainLayout->addLayout(offsetLayout);

    // Initially disable offset controls (will be enabled when FromStart mode is selected/measured)
    m_finalOffsetSpin->setEnabled(false);
    m_applyOffsetCheck->setEnabled(false);

    // Button box
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okBtn = buttonBox->button(QDialogButtonBox::Ok);
    m_cancelBtn = buttonBox->button(QDialogButtonBox::Cancel);
    m_okBtn->setEnabled(false); // Disable until measurement is done
    mainLayout->addWidget(buttonBox);

    // Connections
    connect(m_measureBtn, &QPushButton::clicked, this, &BpmMeasureDialog::onMeasureClicked);
    connect(m_okBtn, &QPushButton::clicked, this, &BpmMeasureDialog::onOkClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_x2Btn, &QPushButton::clicked, this, [this]()
            { onQuickMultiply(2); });
    connect(m_x3Btn, &QPushButton::clicked, this, [this]()
            { onQuickMultiply(3); });
    connect(m_x4Btn, &QPushButton::clicked, this, [this]()
            { onQuickMultiply(4); });
    connect(m_x6Btn, &QPushButton::clicked, this, [this]()
            { onQuickMultiply(6); });
    connect(m_x8Btn, &QPushButton::clicked, this, [this]()
            { onQuickMultiply(8); });

    // Ctrl+Z undo shortcut
    m_undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(m_undoShortcut, &QShortcut::activated, this, &BpmMeasureDialog::onUndoQuick);

    // Mode change signal for enabling/disabling offset controls
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BpmMeasureDialog::onModeChanged);

    mainLayout->addStretch();
}

void BpmMeasureDialog::setCurrentTimeText(const QString &text)
{
    // Find the time value label (second widget in timeLayout)
    if (layout() && layout()->itemAt(0) && layout()->itemAt(0)->layout())
    {
        QLayoutItem *item = layout()->itemAt(0)->layout()->itemAt(1);
        if (item && item->widget())
        {
            QLabel *label = qobject_cast<QLabel *>(item->widget());
            if (label)
            {
                label->setText(text);
            }
        }
    }
}

void BpmMeasureDialog::setMeasuredBpm(double bpm)
{
    m_measuredBpm = bpm;
    m_measureDuration = m_durationSpin->value();
    m_resultEdit->setText(QString::number(bpm, 'f', 2));
    m_okBtn->setEnabled(bpm > 0);

    // Auto-fill final BPM with measured value
    m_lastFinalBpm = m_finalBpmSpin->value();
    m_finalBpmSpin->setValue(bpm);
}

void BpmMeasureDialog::setResultDetailsText(const QString &text)
{
    if (m_detailsEdit)
        m_detailsEdit->setPlainText(text.isEmpty() ? tr("No segment data yet.") : text);
}

void BpmMeasureDialog::setMeasuring(bool measuring)
{
    if (m_measureBtn)
        m_measureBtn->setEnabled(!measuring);
    if (m_durationSpin)
        m_durationSpin->setEnabled(!measuring);
    if (m_modeCombo)
        m_modeCombo->setEnabled(!measuring);
    if (m_okBtn)
        m_okBtn->setEnabled(!measuring && m_measuredBpm > 0.0);
    if (m_progressBar)
    {
        if (measuring)
        {
            m_progressBar->setRange(0, 0);
        }
        else
        {
            m_progressBar->setRange(0, 1);
            m_progressBar->setValue(1);
        }
    }
}

void BpmMeasureDialog::setStatusText(const QString &text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text.isEmpty() ? tr("Ready.") : text);
}

int BpmMeasureDialog::durationSeconds() const
{
    return m_durationSpin ? m_durationSpin->value() : 10;
}

BpmMeasureDialog::MeasureMode BpmMeasureDialog::mode() const
{
    if (!m_modeCombo)
        return MeasureMode::FromCurrentTime;
    return static_cast<MeasureMode>(m_modeCombo->currentData().toInt());
}

double BpmMeasureDialog::finalBpm() const
{
    return m_finalBpmSpin ? m_finalBpmSpin->value() : 0.0;
}

int BpmMeasureDialog::finalOffset() const
{
    return m_finalOffsetSpin ? m_finalOffsetSpin->value() : 0;
}

bool BpmMeasureDialog::applyOffset() const
{
    return m_applyOffsetCheck ? m_applyOffsetCheck->isChecked() : false;
}

void BpmMeasureDialog::setMeasuredOffset(int offsetMs)
{
    if (m_finalOffsetSpin)
    {
        m_finalOffsetSpin->setValue(offsetMs);
        m_applyOffsetCheck->setChecked(true);
    }
}

void BpmMeasureDialog::onMeasureClicked()
{
    // Emit signal to request measurement from parent
    emit measureRequested(m_durationSpin->value(), static_cast<int>(mode()));
}

void BpmMeasureDialog::onOkClicked()
{
    if (m_measuredBpm > 0)
    {
        accept();
    }
}

void BpmMeasureDialog::onQuickMultiply(int factor)
{
    m_lastFinalBpm = m_finalBpmSpin->value();
    m_finalBpmSpin->setValue(m_measuredBpm * factor);
}

void BpmMeasureDialog::onUndoQuick()
{
    m_finalBpmSpin->setValue(m_lastFinalBpm);
}

void BpmMeasureDialog::onModeChanged(int index)
{
    // Mode 0 = FromStart -> offset controls enabled
    // Mode 1 = FromCurrentTime -> offset controls disabled
    bool isFromStart = (index == 0);
    if (m_finalOffsetSpin)
        m_finalOffsetSpin->setEnabled(isFromStart);
    if (m_applyOffsetCheck)
    {
        m_applyOffsetCheck->setEnabled(isFromStart);
        if (!isFromStart)
            m_applyOffsetCheck->setChecked(false);
    }
}
