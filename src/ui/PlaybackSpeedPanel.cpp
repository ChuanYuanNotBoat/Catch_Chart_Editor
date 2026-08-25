#include "PlaybackSpeedPanel.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QtGlobal>
#include <iterator>

namespace
{
constexpr double kQuickSpeeds[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
}

PlaybackSpeedPanel::PlaybackSpeedPanel(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    m_group = new QGroupBox(this);
    outerLayout->addWidget(m_group);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_group);
    groupLayout->setSpacing(6);

    m_quickButtonGroup = new QButtonGroup(this);
    m_quickButtonGroup->setExclusive(true);
    QGridLayout *quickLayout = new QGridLayout;
    quickLayout->setContentsMargins(0, 0, 0, 0);
    quickLayout->setHorizontalSpacing(6);
    quickLayout->setVerticalSpacing(6);
    for (int index = 0; index < static_cast<int>(std::size(kQuickSpeeds)); ++index)
    {
        const double quickSpeed = kQuickSpeeds[index];
        QPushButton *button = new QPushButton(formatSpeed(quickSpeed), m_group);
        button->setCheckable(true);
        button->setProperty("playbackSpeed", quickSpeed);
        button->setObjectName(QStringLiteral("playbackSpeedQuickButton%1")
                                  .arg(qRound(quickSpeed * 100.0)));
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        m_quickButtonGroup->addButton(button);
        quickLayout->addWidget(button, index / 3, index % 3);
    }
    groupLayout->addLayout(quickLayout);

    QHBoxLayout *customLayout = new QHBoxLayout;
    customLayout->setContentsMargins(0, 0, 0, 0);
    m_customLabel = new QLabel(m_group);
    m_speedInput = new QDoubleSpinBox(m_group);
    m_speedInput->setObjectName(QStringLiteral("playbackSpeedInput"));
    m_speedInput->setRange(MinimumSpeed, MaximumSpeed);
    m_speedInput->setDecimals(2);
    m_speedInput->setSingleStep(0.05);
    m_speedInput->setSuffix(QStringLiteral("×"));
    m_speedInput->setKeyboardTracking(false);
    m_speedInput->setAccelerated(true);
    m_speedInput->setValue(1.0);
    customLayout->addWidget(m_customLabel);
    customLayout->addWidget(m_speedInput, 1);
    groupLayout->addLayout(customLayout);

    connect(m_quickButtonGroup, &QButtonGroup::buttonClicked, this,
            [this](QAbstractButton *button)
            {
                if (!button)
                    return;
                m_speedInput->setValue(button->property("playbackSpeed").toDouble());
            });
    connect(m_speedInput, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double speed)
            {
                syncQuickButtons(speed);
                emit speedChanged(speed);
            });

    retranslateUi();
    setSpeed(1.0);
}

double PlaybackSpeedPanel::speed() const
{
    return m_speedInput ? m_speedInput->value() : 1.0;
}

void PlaybackSpeedPanel::setSpeed(double speed)
{
    const double boundedSpeed = PlaybackSpeed::sanitize(speed);
    if (m_speedInput)
    {
        const QSignalBlocker blocker(m_speedInput);
        m_speedInput->setValue(boundedSpeed);
    }
    syncQuickButtons(boundedSpeed);
}

void PlaybackSpeedPanel::retranslateUi()
{
    if (m_group)
        m_group->setTitle(tr("Playback Speed"));
    if (m_customLabel)
        m_customLabel->setText(tr("Custom:"));
}

void PlaybackSpeedPanel::syncQuickButtons(double speed)
{
    if (!m_quickButtonGroup)
        return;

    const QSignalBlocker blocker(m_quickButtonGroup);
    m_quickButtonGroup->setExclusive(false);
    for (QAbstractButton *button : m_quickButtonGroup->buttons())
    {
        const double buttonSpeed = button->property("playbackSpeed").toDouble();
        button->setChecked(qAbs(buttonSpeed - speed) < 0.0001);
    }
    m_quickButtonGroup->setExclusive(true);
}

QString PlaybackSpeedPanel::formatSpeed(double speed)
{
    return QStringLiteral("%1×").arg(QString::number(speed, 'g', 3));
}
