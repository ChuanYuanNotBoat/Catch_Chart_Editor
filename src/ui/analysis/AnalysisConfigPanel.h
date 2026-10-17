#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QHash>
class QComboBox;
class QLabel;
class QPlainTextEdit;
namespace analysis
{
struct ConfigField;
}
class AnalysisConfigPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AnalysisConfigPanel(QWidget *parent = nullptr, const QString &storeOverride = {});
    QJsonObject config() const
    {
        return m_config;
    }
    void setLevel(int level);
    void loadProject(const QString &chartPath);
    void setConfig(const QJsonObject &config);
signals:
    void configChanged();

private:
    void refresh();
    void rebuildPresets(const QString &selected = QStringLiteral("Default"));
    void savePreset(bool duplicate);
    void importProfile();
    void exportProfile();
    void resetSection();
    void selectPreset(const QString &name);
    QJsonObject projectBaseMetadata() const;
    QString storePath() const;
    QJsonObject m_config, m_base, m_profiles, m_project, m_projectBase;
    QString m_projectPath, m_notice, m_storeOverride;
    QComboBox *m_presets, *m_sections;
    QLabel *m_status;
    QPlainTextEdit *m_windows, *m_differences;
    QHash<QString, QWidget *> m_editors, m_labels, m_rows;
    bool m_refreshing = false;
    bool m_storeReadable = true;
    QString m_selectedPreset = QStringLiteral("Default");
    QString m_globalSelectedPreset = QStringLiteral("Default"), m_projectBaseName;
    int m_level = 0;
};
