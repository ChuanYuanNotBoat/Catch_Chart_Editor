#pragma once

#include <QList>
#include <QString>
#include <QWidget>
#include <QHash>

class ChartController;
class PlaybackController;
class ChartCanvas;
class QPushButton;
class QDoubleSpinBox;
class QLabel;
class QVBoxLayout;
class QToolButton;


class LeftPanel : public QWidget
{
    Q_OBJECT
public:
    struct PluginQuickAction
    {
        QString pluginId;
        QString pluginDisplayName;
        QString actionId;
        QString title;
        QString tooltip;
        bool checkable = false;
        bool checked = false;
    };

    explicit LeftPanel(QWidget *parent = nullptr);
    void setChartController(ChartController *controller);
    void setPlaybackController(PlaybackController *controller);
    void setChartCanvas(ChartCanvas *canvas);
    void setPluginQuickActions(const QList<PluginQuickAction> &actions);
    void retranslateUi();

    // 统计面板区块（旧模式专用）：挂到左栏底部，可折叠。
    // 新旧模式共用同一个面板实例，切换窗口模式时通过本组方法迁移。
    void attachStatsSection(QWidget *statsSection);
    QWidget *takeStatsSection();

signals:
    void pluginQuickActionTriggered(const QString &pluginId, const QString &actionId);

private slots:
    void onPlayPauseClicked();
    void onZoomInClicked();
    void onZoomOutClicked();
    void onTimeScaleChanged(double scale);

private:
    void setupUi();

    QVBoxLayout *m_layout = nullptr;
    ChartController *m_chartController = nullptr;
    PlaybackController *m_playbackController = nullptr;
    ChartCanvas *m_chartCanvas = nullptr;
    QPushButton *m_playPauseBtn = nullptr;
    QPushButton *m_zoomInBtn = nullptr;
    QPushButton *m_zoomOutBtn = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QDoubleSpinBox *m_timeScaleSpin = nullptr;
    QLabel *m_pluginSectionLabel = nullptr;
    QWidget *m_pluginSectionContainer = nullptr;
    QVBoxLayout *m_pluginButtonsLayout = nullptr;
    QHash<QString, bool> m_pluginSectionExpanded;
    QToolButton *m_statsToggleBtn = nullptr;
    QWidget *m_statsSection = nullptr;
};
