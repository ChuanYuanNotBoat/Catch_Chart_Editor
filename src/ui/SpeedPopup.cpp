#include "SpeedPopup.h"
#include "utils/NativeWindowTheme.h"
#include "utils/Settings.h"
#include <QHBoxLayout>
#include <QRadioButton>
#include <QButtonGroup>

SpeedPopup::SpeedPopup(QWidget *parent)
    : QWidget(parent), m_currentSpeed(1.0)
{
    // Popup floats above the main window: force theme colors so the radio
    // labels stay readable in dark mode (native style ignores QPalette).
    const auto theme = NativeWindowTheme::themeColorsFor(Settings::instance().backgroundColor());
    setStyleSheet(QString("QWidget { background-color: %1; color: %2; }"
                          "QRadioButton { color: %2; spacing: 4px; padding: 2px; }")
                      .arg(theme.window.name(), theme.text.name()));
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);
    QList<double> speeds = {0.25, 0.5, 0.75, 1.0};
    for (double sp : speeds)
    {
        QRadioButton *btn = new QRadioButton(tr("%1x").arg(sp), this);
        btn->setCheckable(true);
        m_buttonGroup->addButton(btn, static_cast<int>(sp * 100));
        layout->addWidget(btn);
        if (qFuzzyCompare(sp, m_currentSpeed))
            btn->setChecked(true);
    }
    connect(m_buttonGroup, QOverload<int>::of(&QButtonGroup::idClicked), this, &SpeedPopup::onSpeedSelected);
    setWindowFlags(Qt::Popup);
}

void SpeedPopup::setSpeed(double speed)
{
    m_currentSpeed = speed;
    m_buttonGroup->setExclusive(true);
    int id = static_cast<int>(speed * 100);
    if (QAbstractButton *btn = m_buttonGroup->button(id))
    {
        btn->setChecked(true);
    }
    else
    {
        if (QAbstractButton *checked = m_buttonGroup->checkedButton())
            checked->setChecked(false);
    }
}

void SpeedPopup::onSpeedSelected(int id)
{
    m_currentSpeed = id / 100.0;
    emit speedChanged(m_currentSpeed);
    close();
}
