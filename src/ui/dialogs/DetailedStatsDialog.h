// src/ui/dialogs/DetailedStatsDialog.h - 详细统计弹窗（非模态）
#pragma once

#include "model/ChartStatistics.h"
#include <QDialog>
#include <QVector>

class QGridLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

/**
 * @brief 详细统计弹窗。
 *
 * - 非模态：show() 打开后不阻塞主窗口，可长开并随谱面实时刷新
 *   （宿主调用 setStatistics() 推送快照）。
 * - 预留接口：未来新增统计维度时，通过 addSection()/setSectionContent()
 *   追加自定义分区，无需改动现有布局。
 */
class DetailedStatsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DetailedStatsDialog(QWidget *parent = nullptr);

    void setStatistics(const ChartStatistics &stats);
    ChartStatistics statistics() const { return m_stats; }
    void retranslateUi();

    // ---- 预留接口模块 ----
    // 追加一个空分区，返回分区索引（按添加顺序，0 起始）。
    int addSection(const QString &title);
    // 将自定义内容挂到指定分区（索引为 addSection 的返回值）。
    void setSectionContent(int sectionIndex, QWidget *content);

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void dialogClosed();

private:
    void rebuildBasicRows();
    static QString formatCount(int count);

    QVBoxLayout *m_mainLayout = nullptr;
    QGroupBox *m_basicGroup = nullptr;
    QGridLayout *m_basicLayout = nullptr;
    QLabel *m_totalValue = nullptr;
    QLabel *m_normalValue = nullptr;
    QLabel *m_rainValue = nullptr;
    QLabel *m_rewardValue = nullptr;
    QLabel *m_maxComboValue = nullptr;
    QLabel *m_soundNoteValue = nullptr;
    QLabel *m_notesTotalTitle = nullptr;
    QLabel *m_normalTitle = nullptr;
    QLabel *m_rainTitle = nullptr;
    QLabel *m_rewardTitle = nullptr;
    QLabel *m_maxComboTitle = nullptr;
    QLabel *m_soundNoteTitle = nullptr;
    QPushButton *m_closeButton = nullptr;
    struct ExtraSection
    {
        QGroupBox *group = nullptr;
        QVBoxLayout *layout = nullptr;
    };
    QVector<ExtraSection> m_extraSections;
    ChartStatistics m_stats;
};
