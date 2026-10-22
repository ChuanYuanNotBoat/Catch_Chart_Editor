#pragma once
#include <QWidget>
class ChartController;
class ChartCanvas;
class PlaybackController;
class BpmCurveCanvas;
class TimingToolsPanel;
class TimingInterpolationPanel;
class QHideEvent;
class QShowEvent;

class BpmAdvancedEditor : public QWidget
{
    Q_OBJECT
public:
    BpmAdvancedEditor(ChartController *, PlaybackController *, ChartCanvas *, QWidget *parent = nullptr);
protected:
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    void reloadShortcuts();
    BpmCurveCanvas *m_canvas;
    TimingToolsPanel *m_measure;
    TimingInterpolationPanel *m_interpolate;
    class CommandRouter *m_router;
};
