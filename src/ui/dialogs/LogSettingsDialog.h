#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>

/**
 * @brief 日志设置对话框
 *
 * 允许用户在运行时动态调整：
 * - 日志级别（DEBUG/INFO/WARN/ERROR）
 * - JSON日志输出
 * - 性能计时
 * - 详细模式
 * - 清除日志
 */
class LogSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogSettingsDialog(QWidget *parent = nullptr);
    ~LogSettingsDialog() = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onVerboseModeChanged(Qt::CheckState state);
    void onJsonLoggingChanged(Qt::CheckState state);
    void onPerformanceTimingChanged(Qt::CheckState state);
    void onPlaybackProbeChanged(Qt::CheckState state);
    void onLogLevelChanged(int index);
    void onClearLogsClicked();
    void onShowLogDialogClicked();
    void onExportDiagnosticsClicked();

private:
    void setupUI();
    void saveSettings();
    void loadSettings();
    void displayDiagnosticInfo();

    // UI组件
    QComboBox *m_logLevelCombo;
    QCheckBox *m_verboseModeCheckbox;
    QCheckBox *m_jsonLoggingCheckbox;
    QCheckBox *m_performanceTimingCheckbox;
    QCheckBox *m_playbackProbeCheckbox;
    QPushButton *m_clearLogsButton;
    QPushButton *m_showLogButton;
    QPushButton *m_exportButton;
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
};
