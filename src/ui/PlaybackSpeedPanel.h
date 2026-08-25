#pragma once

#include "utils/PlaybackSpeed.h"
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;

class PlaybackSpeedPanel : public QWidget
{
    Q_OBJECT
public:
    static constexpr double MinimumSpeed = PlaybackSpeed::Minimum;
    static constexpr double MaximumSpeed = PlaybackSpeed::Maximum;

    explicit PlaybackSpeedPanel(QWidget *parent = nullptr);

    double speed() const;
    void setSpeed(double speed);
    void retranslateUi();

signals:
    void speedChanged(double speed);

private:
    void syncQuickButtons(double speed);
    static QString formatSpeed(double speed);

    QGroupBox *m_group = nullptr;
    QLabel *m_customLabel = nullptr;
    QDoubleSpinBox *m_speedInput = nullptr;
    QButtonGroup *m_quickButtonGroup = nullptr;
};
