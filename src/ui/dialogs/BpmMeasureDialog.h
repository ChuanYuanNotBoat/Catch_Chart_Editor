#pragma once

#include <QDialog>
#include <QDoubleSpinBox>

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
    double finalBpm() const;
    int measureDurationSeconds() const { return m_measureDuration; }
    
    void setCurrentTimeText(const QString &text);
    void setMeasuredBpm(double bpm);
    void setResultDetailsText(const QString &text);
    void setMeasuring(bool measuring);
    void setStatusText(const QString &text);
    int durationSeconds() const;
    MeasureMode mode() const;

signals:
    void measureRequested(int durationSeconds, int mode);

private slots:
    void onMeasureClicked();
    void onOkClicked();
    void onQuickMultiply(int factor);
    void onUndoQuick();

private:
    void setupUi();

    QLabel *m_currentTimeLabel;
    QLabel *m_durationLabel;
    QSpinBox *m_durationSpin;
    QLabel *m_modeLabel;
    QComboBox *m_modeCombo;
    QLabel *m_resultLabel;
    QLineEdit *m_resultEdit;
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

    QShortcut *m_undoShortcut;
    
    double m_measuredBpm;
    double m_lastFinalBpm; // for undo
    int m_measureDuration;
};