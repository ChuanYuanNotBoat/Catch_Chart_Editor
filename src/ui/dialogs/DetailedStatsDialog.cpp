#include "DetailedStatsDialog.h"

#include "utils/NativeWindowTheme.h"
#include "utils/Settings.h"

#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

DetailedStatsDialog::DetailedStatsDialog(QWidget *parent)
    : QDialog(parent)
{
    // 非模态：不调用 setModal/exec，配合 show() 使用。
    setStyleSheet(NativeWindowTheme::dialogStyleSheet(Settings::instance().backgroundColor()));
    setWindowTitle(tr("Detailed Chart Statistics"));
    resize(420, 320);

    m_mainLayout = new QVBoxLayout(this);

    m_basicGroup = new QGroupBox(this);
    m_basicLayout = new QGridLayout(m_basicGroup);
    m_basicLayout->setHorizontalSpacing(12);
    m_basicLayout->setVerticalSpacing(6);

    const auto addRow = [this](int row, QLabel **title, QLabel **value)
    {
        *title = new QLabel(m_basicGroup);
        *value = new QLabel(m_basicGroup);
        (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_basicLayout->addWidget(*title, row, 0);
        m_basicLayout->addWidget(*value, row, 1);
    };

    addRow(0, &m_notesTotalTitle, &m_totalValue);
    addRow(1, &m_normalTitle, &m_normalValue);
    addRow(2, &m_rainTitle, &m_rainValue);
    addRow(3, &m_rewardTitle, &m_rewardValue);
    addRow(4, &m_soundNoteTitle, &m_soundNoteValue);
    addRow(5, &m_maxComboTitle, &m_maxComboValue);
    m_mainLayout->addWidget(m_basicGroup);

    m_mainLayout->addStretch();

    QHBoxLayout *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    m_closeButton = new QPushButton(this);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    buttonRow->addWidget(m_closeButton);
    m_mainLayout->addLayout(buttonRow);

    retranslateUi();
    rebuildBasicRows();
}

void DetailedStatsDialog::setStatistics(const ChartStatistics &stats)
{
    if (stats == m_stats)
        return;
    m_stats = stats;
    rebuildBasicRows();
}

void DetailedStatsDialog::retranslateUi()
{
    setWindowTitle(tr("Detailed Chart Statistics"));
    if (m_basicGroup)
        m_basicGroup->setTitle(tr("Overview"));
    if (m_notesTotalTitle)
        m_notesTotalTitle->setText(tr("Total Notes"));
    if (m_normalTitle)
        m_normalTitle->setText(tr("Normal Notes"));
    if (m_rainTitle)
        m_rainTitle->setText(tr("Rain Count"));
    if (m_rewardTitle)
        m_rewardTitle->setText(tr("Reward Notes"));
    if (m_soundNoteTitle)
        m_soundNoteTitle->setText(tr("Audio Notes (excluded)"));
    if (m_maxComboTitle)
        m_maxComboTitle->setText(tr("Max Combo"));
    if (m_closeButton)
        m_closeButton->setText(tr("Close"));
}

int DetailedStatsDialog::addSection(const QString &title)
{
    ExtraSection section;
    section.group = new QGroupBox(title, this);
    section.layout = new QVBoxLayout(section.group);
    section.layout->setContentsMargins(8, 12, 8, 8);
    // 插入到弹性占位之前，关闭按钮保持在底部。
    m_mainLayout->insertWidget(m_mainLayout->count() - 2, section.group);
    m_extraSections.append(section);
    return m_extraSections.size() - 1;
}

void DetailedStatsDialog::setSectionContent(int sectionIndex, QWidget *content)
{
    if (sectionIndex < 0 || sectionIndex >= m_extraSections.size() || !content)
        return;
    content->setParent(m_extraSections[sectionIndex].group);
    m_extraSections[sectionIndex].layout->addWidget(content);
    content->show();
}

void DetailedStatsDialog::closeEvent(QCloseEvent *event)
{
    QDialog::closeEvent(event);
    emit dialogClosed();
}

void DetailedStatsDialog::rebuildBasicRows()
{
    if (m_totalValue)
        m_totalValue->setText(formatCount(m_stats.totalNotes));
    if (m_normalValue)
        m_normalValue->setText(formatCount(m_stats.normalCount));
    if (m_rainValue)
        m_rainValue->setText(formatCount(m_stats.rainCount));
    if (m_rewardValue)
        m_rewardValue->setText(formatCount(m_stats.rewardCount));
    if (m_soundNoteValue)
        m_soundNoteValue->setText(formatCount(m_stats.soundCount));
    if (m_maxComboValue)
        m_maxComboValue->setText(formatCount(m_stats.maxCombo));
}

QString DetailedStatsDialog::formatCount(int count)
{
    return QString::number(count);
}
