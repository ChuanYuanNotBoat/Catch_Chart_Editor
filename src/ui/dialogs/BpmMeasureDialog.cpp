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

#include <limits>

BpmMeasureDialog::BpmMeasureDialog(QWidget *parent)
    : QDialog(parent),
      m_measuredBpm(0.0),
      m_autoTimingSuggestionBpm(0.0),
      m_lastFinalBpm(0.0),
      m_measureDuration(120),
      m_isMeasuring(false),
      m_measurementCompleted(false),
      m_hasAdoptedBpm(false),
      m_hasLegacyOffset(false),
      m_hasAutoTimingPhaseOffset(false),
      m_usingAutoTimingPhaseOffset(false),
      m_legacyOffset(0),
      m_autoTimingPhaseOffset(0),
      m_hasAutoTimingMap(false)
{
    setStyleSheet(NativeWindowTheme::dialogStyleSheet(Settings::instance().backgroundColor()));
    setupUi();
}

void BpmMeasureDialog::setupUi()
{
    setWindowTitle(tr("Measure BPM"));
    setMinimumWidth(560);

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
    m_durationSpin->setObjectName(QStringLiteral("measureDurationSpin"));
    m_durationSpin->setRange(1, std::numeric_limits<int>::max());
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
    m_modeCombo->setObjectName(QStringLiteral("measureModeCombo"));
    m_modeCombo->addItem(tr("From Song Start"), static_cast<int>(MeasureMode::FromStart));
    m_modeCombo->addItem(tr("From Current Time"), static_cast<int>(MeasureMode::FromCurrentTime));
    m_modeCombo->setCurrentIndex(0);
    modeLayout->addWidget(m_modeLabel);
    modeLayout->addWidget(m_modeCombo);
    modeLayout->addStretch();
    mainLayout->addLayout(modeLayout);

    // Result display with quick multiply buttons
    QHBoxLayout *resultLayout = new QHBoxLayout;
    m_resultLabel = new QLabel(tr("Measured BPM (legacy):"), this);
    m_resultEdit = new QLineEdit(this);
    m_resultEdit->setObjectName(QStringLiteral("legacyMeasuredBpmEdit"));
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setText(tr("Click 'Measure' to start"));
    resultLayout->addWidget(m_resultLabel);
    resultLayout->addWidget(m_resultEdit);

    m_x2Btn = new QPushButton(tr("x2"), this);
    m_x2Btn->setObjectName(QStringLiteral("multiply2Button"));
    m_x2Btn->setMaximumWidth(40);
    m_x3Btn = new QPushButton(tr("x3"), this);
    m_x3Btn->setObjectName(QStringLiteral("multiply3Button"));
    m_x3Btn->setMaximumWidth(40);
    m_x4Btn = new QPushButton(tr("x4"), this);
    m_x4Btn->setObjectName(QStringLiteral("multiply4Button"));
    m_x4Btn->setMaximumWidth(40);
    m_x6Btn = new QPushButton(tr("x6"), this);
    m_x6Btn->setObjectName(QStringLiteral("multiply6Button"));
    m_x6Btn->setMaximumWidth(40);
    m_x8Btn = new QPushButton(tr("x8"), this);
    m_x8Btn->setObjectName(QStringLiteral("multiply8Button"));
    m_x8Btn->setMaximumWidth(40);
    resultLayout->addWidget(m_x2Btn);
    resultLayout->addWidget(m_x3Btn);
    resultLayout->addWidget(m_x4Btn);
    resultLayout->addWidget(m_x6Btn);
    resultLayout->addWidget(m_x8Btn);
    mainLayout->addLayout(resultLayout);

    // AutoTiming 2 is deliberately separate from the legacy result. It can be
    // copied into "BPM to Add" only through this explicit user action.
    QHBoxLayout *suggestionLayout = new QHBoxLayout;
    m_autoTimingSuggestionLabel = new QLabel(tr("AutoTiming 2 suggestion:"), this);
    m_autoTimingSuggestionEdit = new QLineEdit(this);
    m_autoTimingSuggestionEdit->setObjectName(QStringLiteral("autoTiming2SuggestionEdit"));
    m_autoTimingSuggestionEdit->setReadOnly(true);
    m_autoTimingSuggestionEdit->setText(tr("Not analyzed yet"));
    m_useAutoTimingSuggestionBtn = new QPushButton(tr("Use suggestion"), this);
    m_useAutoTimingSuggestionBtn->setObjectName(QStringLiteral("useAutoTiming2SuggestionButton"));
    m_useAutoTimingSuggestionBtn->setEnabled(false);
    suggestionLayout->addWidget(m_autoTimingSuggestionLabel);
    suggestionLayout->addWidget(m_autoTimingSuggestionEdit, 1);
    suggestionLayout->addWidget(m_useAutoTimingSuggestionBtn);
    mainLayout->addLayout(suggestionLayout);

    QHBoxLayout *timingMapLayout = new QHBoxLayout;
    m_applyAutoTimingMapCheck = new QCheckBox(tr("Apply AutoTiming 2 BPM map"), this);
    m_applyAutoTimingMapCheck->setObjectName(QStringLiteral("applyAutoTiming2MapCheck"));
    m_applyAutoTimingMapCheck->setEnabled(false);
    m_autoTimingMapSummaryLabel = new QLabel(tr("No timing map available"), this);
    m_autoTimingMapSummaryLabel->setObjectName(QStringLiteral("autoTiming2MapSummaryLabel"));
    m_autoTimingMapSummaryLabel->setWordWrap(true);
    timingMapLayout->addWidget(m_applyAutoTimingMapCheck);
    timingMapLayout->addWidget(m_autoTimingMapSummaryLabel, 1);
    mainLayout->addLayout(timingMapLayout);

    m_multiplierHintLabel = new QLabel(this);
    m_multiplierHintLabel->setObjectName(QStringLiteral("multiplierHintLabel"));
    m_multiplierHintLabel->setWordWrap(true);
    m_multiplierHintLabel->hide();
    mainLayout->addWidget(m_multiplierHintLabel);

    m_statusLabel = new QLabel(tr("Ready."), this);
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    m_detailsEdit = new QTextEdit(this);
    m_detailsEdit->setObjectName(QStringLiteral("measurementDetailsEdit"));
    m_detailsEdit->setReadOnly(true);
    m_detailsEdit->setMinimumHeight(150);
    m_detailsEdit->setPlainText(tr("No analysis yet."));
    mainLayout->addWidget(m_detailsEdit);

    // Measure button
    m_measureBtn = new QPushButton(tr("Measure"), this);
    mainLayout->addWidget(m_measureBtn);

    // Final BPM to add (at bottom)
    QHBoxLayout *finalLayout = new QHBoxLayout;
    m_finalBpmLabel = new QLabel(tr("BPM to Add:"), this);
    m_finalBpmSpin = new QDoubleSpinBox(this);
    m_finalBpmSpin->setObjectName(QStringLiteral("bpmToAddSpin"));
    m_finalBpmSpin->setRange(0, 999);
    m_finalBpmSpin->setDecimals(3);
    m_finalBpmSpin->setValue(0);
    m_finalBpmSpin->setSpecialValueText(tr("Not set"));
    finalLayout->addWidget(m_finalBpmLabel);
    finalLayout->addWidget(m_finalBpmSpin);
    mainLayout->addLayout(finalLayout);

    // Final Offset to apply
    QHBoxLayout *offsetLayout = new QHBoxLayout;
    m_finalOffsetLabel = new QLabel(tr("Offset to Apply:"), this);
    m_finalOffsetSpin = new QSpinBox(this);
    m_finalOffsetSpin->setObjectName(QStringLiteral("offsetToApplySpin"));
    m_finalOffsetSpin->setRange(-9999, 9999);
    m_finalOffsetSpin->setValue(0);
    m_finalOffsetSpin->setSuffix(" ms");
    m_applyOffsetCheck = new QCheckBox(tr("Apply offset"), this);
    m_applyOffsetCheck->setObjectName(QStringLiteral("applyLegacyOffsetCheck"));
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
    connect(m_useAutoTimingSuggestionBtn,
            &QPushButton::clicked,
            this,
            &BpmMeasureDialog::onUseAutoTimingSuggestion);
    connect(m_applyAutoTimingMapCheck,
            &QCheckBox::toggled,
            this,
            [this](bool)
            {
                updateActionState();
            });

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
    connect(m_durationSpin,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            [this](int)
            {
                invalidateCompletedMeasurement();
            });
    connect(m_finalBpmSpin,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            [this](double)
            {
                updateActionState();
            });

    mainLayout->addStretch();
    updateActionState();
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
    if (!(bpm > 0.0))
    {
        setLegacyUnavailable();
        return;
    }
    m_measuredBpm = bpm;
    m_measureDuration = m_durationSpin->value();
    m_resultEdit->setText(QString::number(bpm, 'f', 2));

    // Auto-fill final BPM with measured value
    m_lastFinalBpm = m_finalBpmSpin->value();
    m_finalBpmSpin->setValue(bpm);
    m_hasAdoptedBpm = true;
    updateActionState();
}

void BpmMeasureDialog::setLegacyUnavailable(const QString &text)
{
    m_measuredBpm = 0.0;
    if (m_resultEdit)
        m_resultEdit->setText(text.isEmpty() ? tr("Unavailable") : text);
    m_hasLegacyOffset = false;
    if (m_applyOffsetCheck)
        m_applyOffsetCheck->setChecked(false);
    updateActionState();
}

void BpmMeasureDialog::setAutoTimingSuggestion(double bpm, const QString &qualifier)
{
    if (!(bpm > 0.0))
    {
        setAutoTimingUnavailable(tr("No stable recommendation"));
        return;
    }

    m_autoTimingSuggestionBpm = bpm;
    QString text = QString::number(bpm, 'f', 3) + tr(" BPM");
    if (!qualifier.isEmpty())
        text += QStringLiteral(" ") + qualifier;
    if (m_autoTimingSuggestionEdit)
        m_autoTimingSuggestionEdit->setText(text);
    updateActionState();
}

void BpmMeasureDialog::setAutoTimingUnavailable(const QString &text)
{
    m_autoTimingSuggestionBpm = 0.0;
    if (m_autoTimingSuggestionEdit)
        m_autoTimingSuggestionEdit->setText(text.isEmpty() ? tr("Unavailable") : text);
    updateActionState();
}

void BpmMeasureDialog::setAutoTimingPhaseOffset(int offsetMs)
{
    m_autoTimingPhaseOffset = offsetMs;
    m_hasAutoTimingPhaseOffset = true;
    updateActionState();
}

void BpmMeasureDialog::setAutoTimingMapSuggestion(const QString &summary)
{
    m_hasAutoTimingMap = true;
    if (m_applyAutoTimingMapCheck)
    {
        m_applyAutoTimingMapCheck->setChecked(false);
        m_applyAutoTimingMapCheck->setEnabled(!m_isMeasuring);
    }
    if (m_autoTimingMapSummaryLabel)
        m_autoTimingMapSummaryLabel->setText(summary);
    updateActionState();
}

void BpmMeasureDialog::setAutoTimingMapUnavailable(const QString &text)
{
    m_hasAutoTimingMap = false;
    if (m_applyAutoTimingMapCheck)
    {
        m_applyAutoTimingMapCheck->setChecked(false);
        m_applyAutoTimingMapCheck->setEnabled(false);
    }
    if (m_autoTimingMapSummaryLabel)
    {
        m_autoTimingMapSummaryLabel->setText(
            text.isEmpty() ? tr("No variable-tempo map detected") : text);
    }
    updateActionState();
}

void BpmMeasureDialog::setMultiplierHint(int factor, const QString &text)
{
    const int factors[] = {2, 3, 4, 6, 8};
    for (int value : factors)
    {
        if (QPushButton *button = quickButtonForFactor(value))
            button->setToolTip(QString());
    }

    if (!m_multiplierHintLabel)
        return;
    if (factor <= 0 || text.isEmpty())
    {
        m_multiplierHintLabel->clear();
        m_multiplierHintLabel->hide();
        return;
    }

    m_multiplierHintLabel->setText(text);
    m_multiplierHintLabel->show();
    if (QPushButton *button = quickButtonForFactor(factor))
        button->setToolTip(text);
}

void BpmMeasureDialog::setResultDetailsText(const QString &text)
{
    if (m_detailsEdit)
        m_detailsEdit->setPlainText(text.isEmpty() ? tr("No analysis yet.") : text);
}

void BpmMeasureDialog::resetMeasurementResults()
{
    m_measuredBpm = 0.0;
    m_autoTimingSuggestionBpm = 0.0;
    m_measurementCompleted = false;
    m_hasAdoptedBpm = false;
    m_hasLegacyOffset = false;
    m_hasAutoTimingPhaseOffset = false;
    m_usingAutoTimingPhaseOffset = false;
    m_legacyOffset = 0;
    m_autoTimingPhaseOffset = 0;
    m_hasAutoTimingMap = false;
    m_lastFinalBpm = 0.0;

    if (m_resultEdit)
        m_resultEdit->setText(tr("Measuring..."));
    if (m_autoTimingSuggestionEdit)
        m_autoTimingSuggestionEdit->setText(tr("Analyzing..."));
    if (m_applyAutoTimingMapCheck)
    {
        m_applyAutoTimingMapCheck->setChecked(false);
        m_applyAutoTimingMapCheck->setEnabled(false);
    }
    if (m_autoTimingMapSummaryLabel)
        m_autoTimingMapSummaryLabel->setText(tr("Analyzing tempo track..."));
    if (m_finalBpmSpin)
        m_finalBpmSpin->setValue(0.0);
    if (m_finalOffsetSpin)
        m_finalOffsetSpin->setValue(0);
    if (m_applyOffsetCheck)
        m_applyOffsetCheck->setChecked(false);
    if (m_detailsEdit)
        m_detailsEdit->setPlainText(tr("Analyzing legacy timing and AutoTiming 2 evidence..."));
    setMultiplierHint(0, QString());
}

void BpmMeasureDialog::invalidateCompletedMeasurement()
{
    if (m_isMeasuring || !m_measurementCompleted)
        return;

    resetMeasurementResults();
    if (m_resultEdit)
        m_resultEdit->setText(tr("Click 'Measure' to start"));
    if (m_autoTimingSuggestionEdit)
        m_autoTimingSuggestionEdit->setText(tr("Not analyzed yet"));
    if (m_autoTimingMapSummaryLabel)
        m_autoTimingMapSummaryLabel->setText(tr("No timing map available"));
    if (m_detailsEdit)
        m_detailsEdit->setPlainText(tr("Measurement settings changed. Measure again."));
    if (m_progressBar)
    {
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(0);
    }
    setStatusText(tr("Measurement settings changed. Measure again."));
    updateActionState();
}

QPushButton *BpmMeasureDialog::quickButtonForFactor(int factor) const
{
    switch (factor)
    {
    case 2:
        return m_x2Btn;
    case 3:
        return m_x3Btn;
    case 4:
        return m_x4Btn;
    case 6:
        return m_x6Btn;
    case 8:
        return m_x8Btn;
    default:
        return nullptr;
    }
}

void BpmMeasureDialog::updateActionState()
{
    const bool hasLegacyBpm = m_measuredBpm > 0.0;
    const bool canUseSuggestion = m_autoTimingSuggestionBpm > 0.0;
    const bool usingTimingMap = m_hasAutoTimingMap &&
                                m_applyAutoTimingMapCheck &&
                                m_applyAutoTimingMapCheck->isChecked();
    const bool canConfirm = !m_isMeasuring &&
                            m_measurementCompleted &&
                            (usingTimingMap ||
                             (m_hasAdoptedBpm &&
                              m_finalBpmSpin &&
                              m_finalBpmSpin->value() > 0.0));

    if (m_okBtn)
        m_okBtn->setEnabled(canConfirm);
    if (m_useAutoTimingSuggestionBtn)
        m_useAutoTimingSuggestionBtn->setEnabled(
            !m_isMeasuring && canUseSuggestion && !usingTimingMap);
    if (m_applyAutoTimingMapCheck)
        m_applyAutoTimingMapCheck->setEnabled(!m_isMeasuring && m_hasAutoTimingMap);
    if (m_finalBpmSpin)
        m_finalBpmSpin->setEnabled(!m_isMeasuring && !usingTimingMap);

    const bool quickEnabled = !m_isMeasuring && hasLegacyBpm && !usingTimingMap;
    const int factors[] = {2, 3, 4, 6, 8};
    for (int factor : factors)
    {
        if (QPushButton *button = quickButtonForFactor(factor))
            button->setEnabled(quickEnabled);
    }

    const bool hasSelectedOffset = m_usingAutoTimingPhaseOffset
                                       ? m_hasAutoTimingPhaseOffset
                                       : m_hasLegacyOffset;
    const bool offsetEnabled = !m_isMeasuring &&
                               mode() == MeasureMode::FromStart &&
                               hasSelectedOffset &&
                               !usingTimingMap;
    if (m_finalOffsetSpin)
        m_finalOffsetSpin->setEnabled(offsetEnabled);
    if (m_applyOffsetCheck)
        m_applyOffsetCheck->setEnabled(offsetEnabled);
}

void BpmMeasureDialog::setMeasuring(bool measuring)
{
    m_isMeasuring = measuring;
    if (m_measureBtn)
        m_measureBtn->setEnabled(!measuring);
    if (m_durationSpin)
        m_durationSpin->setEnabled(!measuring);
    if (m_modeCombo)
        m_modeCombo->setEnabled(!measuring);
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
    updateActionState();
}

void BpmMeasureDialog::setMeasurementComplete()
{
    m_measureDuration = m_durationSpin ? m_durationSpin->value() : m_measureDuration;
    m_measurementCompleted = true;
    setMeasuring(false);
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
    return !applyAutoTimingMap() &&
           m_applyOffsetCheck &&
           m_applyOffsetCheck->isChecked();
}

bool BpmMeasureDialog::applyAutoTimingMap() const
{
    return m_hasAutoTimingMap &&
           m_applyAutoTimingMapCheck &&
           m_applyAutoTimingMapCheck->isChecked();
}

void BpmMeasureDialog::setMeasuredOffset(int offsetMs)
{
    if (m_finalOffsetSpin)
    {
        m_finalOffsetSpin->setValue(offsetMs);
        m_legacyOffset = offsetMs;
        m_hasLegacyOffset = true;
        m_usingAutoTimingPhaseOffset = false;
        m_applyOffsetCheck->setChecked(true);
        updateActionState();
    }
}

void BpmMeasureDialog::onMeasureClicked()
{
    resetMeasurementResults();
    setMeasuring(true);
    emit measureRequested(m_durationSpin->value(), static_cast<int>(mode()));
}

void BpmMeasureDialog::onOkClicked()
{
    if (m_measurementCompleted &&
        (applyAutoTimingMap() || (m_hasAdoptedBpm && finalBpm() > 0.0)))
    {
        accept();
    }
}

void BpmMeasureDialog::onQuickMultiply(int factor)
{
    if (!(m_measuredBpm > 0.0))
        return;
    m_lastFinalBpm = m_finalBpmSpin->value();
    m_finalBpmSpin->setValue(m_measuredBpm * factor);
    m_hasAdoptedBpm = true;
    m_usingAutoTimingPhaseOffset = false;
    if (m_hasLegacyOffset)
    {
        m_finalOffsetSpin->setValue(m_legacyOffset);
        m_applyOffsetCheck->setChecked(true);
    }
    updateActionState();
}

void BpmMeasureDialog::onUndoQuick()
{
    m_finalBpmSpin->setValue(m_lastFinalBpm);
    updateActionState();
}

void BpmMeasureDialog::onUseAutoTimingSuggestion()
{
    if (!(m_autoTimingSuggestionBpm > 0.0))
        return;
    m_lastFinalBpm = m_finalBpmSpin->value();
    m_finalBpmSpin->setValue(m_autoTimingSuggestionBpm);
    m_hasAdoptedBpm = true;
    if (mode() == MeasureMode::FromStart && m_hasAutoTimingPhaseOffset)
    {
        m_usingAutoTimingPhaseOffset = true;
        m_finalOffsetSpin->setValue(m_autoTimingPhaseOffset);
        m_applyOffsetCheck->setChecked(true);
    }
    updateActionState();
}

void BpmMeasureDialog::onModeChanged(int index)
{
    invalidateCompletedMeasurement();
    const bool isFromStart = index == 0;
    if (m_applyOffsetCheck && !isFromStart)
    {
        m_applyOffsetCheck->setChecked(false);
    }
    updateActionState();
}
