// src/ui/ChartStatsPanel.h - 左栏统计面板（新旧两种窗口模式共用）
#pragma once

#include "model/ChartStatistics.h"
#include <QWidget>

class QGroupBox;
class QLabel;
class QPushButton;

/**
 * @brief 谱面统计面板。
 *
 * 展示：全 note 数、常规 note 数量、rain 数量、奖励音符数量、理论 Max Combo。
 * 底部提供“详细统计...”小按钮，宿主通过 detailedStatsRequested() 打开
 * 非模态的 DetailedStatsDialog。
 */
class ChartStatsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ChartStatsPanel(QWidget *parent = nullptr);

    void setStatistics(const ChartStatistics &stats);
    ChartStatistics statistics() const { return m_stats; }
    void retranslateUi();

signals:
    void detailedStatsRequested();

private:
    void updateValues();
    static QString formatCount(int count);

    QGroupBox *m_group = nullptr;
    QLabel *m_totalTitle = nullptr;
    QLabel *m_totalValue = nullptr;
    QLabel *m_normalTitle = nullptr;
    QLabel *m_normalValue = nullptr;
    QLabel *m_rainTitle = nullptr;
    QLabel *m_rainValue = nullptr;
    QLabel *m_rewardTitle = nullptr;
    QLabel *m_rewardValue = nullptr;
    QLabel *m_maxComboTitle = nullptr;
    QLabel *m_maxComboValue = nullptr;
    QPushButton *m_detailsButton = nullptr;
    ChartStatistics m_stats;
};
