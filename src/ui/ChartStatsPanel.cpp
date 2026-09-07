#include "ChartStatsPanel.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace
{
// 与 NoteEditPanel 的操作按钮保持一致的紧凑尺寸策略。
void keepActionButtonCompact(QPushButton *button)
{
    if (button)
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
}
}

ChartStatsPanel::ChartStatsPanel(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    m_group = new QGroupBox(this);
    outerLayout->addWidget(m_group);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_group);
    groupLayout->setSpacing(6);

    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);

    const auto addRow = [this, grid](int row, QLabel **title, QLabel **value)
    {
        *title = new QLabel(m_group);
        *value = new QLabel(m_group);
        (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(*title, row, 0);
        grid->addWidget(*value, row, 1);
    };

    addRow(0, &m_totalTitle, &m_totalValue);
    addRow(1, &m_normalTitle, &m_normalValue);
    addRow(2, &m_rainTitle, &m_rainValue);
    addRow(3, &m_rewardTitle, &m_rewardValue);
    addRow(4, &m_maxComboTitle, &m_maxComboValue);
    groupLayout->addLayout(grid);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_detailsButton = new QPushButton(m_group);
    keepActionButtonCompact(m_detailsButton);
    connect(m_detailsButton, &QPushButton::clicked, this,
            &ChartStatsPanel::detailedStatsRequested);
    buttonLayout->addWidget(m_detailsButton);
    buttonLayout->addStretch();
    groupLayout->addLayout(buttonLayout);

    retranslateUi();
    updateValues();
}

void ChartStatsPanel::setStatistics(const ChartStatistics &stats)
{
    if (stats == m_stats)
        return;
    m_stats = stats;
    updateValues();
}

void ChartStatsPanel::retranslateUi()
{
    if (m_group)
        m_group->setTitle(tr("Chart Statistics"));
    if (m_totalTitle)
        m_totalTitle->setText(tr("Total Notes"));
    if (m_normalTitle)
        m_normalTitle->setText(tr("Normal Notes"));
    if (m_rainTitle)
        m_rainTitle->setText(tr("Rain Count"));
    if (m_rewardTitle)
        m_rewardTitle->setText(tr("Reward Notes"));
    if (m_maxComboTitle)
        m_maxComboTitle->setText(tr("Max Combo"));
    if (m_detailsButton)
        m_detailsButton->setText(tr("Detailed Stats..."));
    updateValues();
}

void ChartStatsPanel::updateValues()
{
    if (m_totalValue)
        m_totalValue->setText(formatCount(m_stats.totalNotes));
    if (m_normalValue)
        m_normalValue->setText(formatCount(m_stats.normalCount));
    if (m_rainValue)
        m_rainValue->setText(formatCount(m_stats.rainCount));
    if (m_rewardValue)
        m_rewardValue->setText(formatCount(m_stats.rewardCount));
    if (m_maxComboValue)
        m_maxComboValue->setText(formatCount(m_stats.maxCombo));
}

QString ChartStatsPanel::formatCount(int count)
{
    return QString::number(count);
}
