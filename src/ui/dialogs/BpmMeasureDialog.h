#pragma once

#include <QDialog>
#include <QDoubleSpinBox>
#include <QCheckBox>

class QSpinBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QTextEdit;
class QProgressBar;
class QShortcut;

class BpmMeasureDialog : public QDialog
{
    Q_OBJECT
public:
    enum class MeasureMode
    {
        FromStart = 0,
        FromCurrentTime = 1
    };

    explicit BpmMeasureDialog(QWidget *parent = nullptr);

    double measuredBpm() const { return m_measuredBpm; }
    double autoTimingSuggestionBpm() const { return m_autoTimingSuggestionBpm; }
    double finalBpm() const;
    int finalOffset() const;
    bool applyOffset() const;
    bool applyAutoTimingMap() const;
    int measureDurationSeconds() const { return m_measureDuration; }

    void setCurrentTimeText(const QString &text);
    void setMeasuredBpm(double bpm);
    void setLegacyUnavailable(const QString &text = QString());
    void setAutoTimingSuggestion(double bpm, const QString &qualifier = QString());
    void setAutoTimingUnavailable(const QString &text);
    void setAutoTimingPhaseOffset(int offsetMs);
    void setAutoTimingMapSuggestion(const QString &summary);
    void setAutoTimingMapUnavailable(const QString &text = QString());
    void setMultiplierHint(int factor, const QString &text);
    void setResultDetailsText(const QString &text);
    void setMeasuring(bool measuring);
    void setMeasurementComplete();
    void setStatusText(const QString &text);
    void setMeasuredOffset(int offsetMs);
    int durationSeconds() const;
    MeasureMode mode() const;

signals:
    void measureRequested(int durationSeconds, int mode);

private slots:
    void onMeasureClicked();
    void onOkClicked();
    void onQuickMultiply(int factor);
    void onUndoQuick();
    void onUseAutoTimingSuggestion();
    void onModeChanged(int index);

private:
    void setupUi();
    void resetMeasurementResults();
    void invalidateCompletedMeasurement();
    void updateActionState();
    QPushButton *quickButtonForFactor(int factor) const;

    QLabel *m_currentTimeLabel;
    QLabel *m_durationLabel;
    QSpinBox *m_durationSpin;
    QLabel *m_modeLabel;
    QComboBox *m_modeCombo;
    QLabel *m_resultLabel;
    QLineEdit *m_resultEdit;
    QLabel *m_autoTimingSuggestionLabel;
    QLineEdit *m_autoTimingSuggestionEdit;
    QPushButton *m_useAutoTimingSuggestionBtn;
    QCheckBox *m_applyAutoTimingMapCheck;
    QLabel *m_autoTimingMapSummaryLabel;
    QLabel *m_multiplierHintLabel;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QTextEdit *m_detailsEdit;
    QPushButton *m_measureBtn;
    QPushButton *m_okBtn;
    QPushButton *m_cancelBtn;

    // Quick multiply buttons
    QPushButton *m_x2Btn;
    QPushButton *m_x3Btn;
    QPushButton *m_x4Btn;
    QPushButton *m_x6Btn;
    QPushButton *m_x8Btn;

    // Final BPM to add
    QLabel *m_finalBpmLabel;
    QDoubleSpinBox *m_finalBpmSpin;

    // Final Offset to apply
    QLabel *m_finalOffsetLabel;
    QSpinBox *m_finalOffsetSpin;
    QCheckBox *m_applyOffsetCheck;

    QShortcut *m_undoShortcut;

    double m_measuredBpm;
    double m_autoTimingSuggestionBpm;
    double m_lastFinalBpm; // for undo
    int m_measureDuration;
    bool m_isMeasuring;
    bool m_measurementCompleted;
    bool m_hasAdoptedBpm;
    bool m_hasLegacyOffset;
    bool m_hasAutoTimingPhaseOffset;
    bool m_usingAutoTimingPhaseOffset;
    int m_legacyOffset;
    int m_autoTimingPhaseOffset;
    bool m_hasAutoTimingMap;
};
