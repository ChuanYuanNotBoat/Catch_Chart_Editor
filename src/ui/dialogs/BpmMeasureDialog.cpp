#include "BpmMeasureDialog.h"
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

BpmMeasureDialog::BpmMeasureDialog(QWidget *parent)
    : QDialog(parent), m_measuredBpm(0.0), m_measureDuration(120)
{
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

    // Result display
    QHBoxLayout *resultLayout = new QHBoxLayout;
    m_resultLabel = new QLabel(tr("Measured BPM:"), this);
    m_resultEdit = new QLineEdit(this);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setText(tr("Click 'Measure' to start"));
    resultLayout->addWidget(m_resultLabel);
    resultLayout->addWidget(m_resultEdit);
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

    mainLayout->addStretch();
}

void BpmMeasureDialog::setCurrentTimeText(const QString &text)
{
    // Find the time value label (second widget in timeLayout)
    if (layout() && layout()->itemAt(0) && layout()->itemAt(0)->layout()) {
        QLayoutItem *item = layout()->itemAt(0)->layout()->itemAt(1);
        if (item && item->widget()) {
            QLabel *label = qobject_cast<QLabel*>(item->widget());
            if (label) {
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

void BpmMeasureDialog::onMeasureClicked()
{
    // Emit signal to request measurement from parent
    emit measureRequested(m_durationSpin->value(), static_cast<int>(mode()));
}

void BpmMeasureDialog::onOkClicked()
{
    if (m_measuredBpm > 0) {
        accept();
    }
}
