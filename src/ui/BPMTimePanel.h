#pragma once

#include "CustomWidgets/RightPanel.h"
#include "audio/BpmDetector.h"
#include <QVector>
#include <QPointer>

class QListWidget;
class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class PlaybackController;
class ChartCanvas;
class BpmAdvancedEditor;

class BPMTimePanel : public RightPanel
{
    Q_OBJECT
public:
    explicit BPMTimePanel(QWidget *parent = nullptr);
    void setChartController(ChartController *controller) override;
    void setSelectionController(SelectionController *controller) override;
    void setPlaybackController(PlaybackController *controller);
    void setMainCanvas(ChartCanvas *canvas) { m_mainCanvas = canvas; }
    void retranslateUi();

private slots:
    void refreshBpmList();
    void onItemSelected(int row);
    void onAddClicked();
    void onRemoveClicked();
    void onBpmChanged(double);
    void onMeasureBpmClicked();

private:
    void setupUi();
    double currentChartTimeMs() const;
    QString currentAudioFilePath() const;
    QString buildMeasurementDetails(const BpmDetector::DetectionResult &result,
                                    bool fromStart) const;

    ChartController *m_chartController;
    PlaybackController *m_playbackController;
    QListWidget *m_bpmListWidget;
    QLabel *m_timeLabel;
    QLabel *m_bpmLabel;
    QLineEdit *m_timeEdit;
    QDoubleSpinBox *m_bpmSpin;
    QPushButton *m_addBtn;
    QPushButton *m_removeBtn;
    QPushButton *m_measureBtn;
    QPushButton *m_advancedBtn;
    ChartCanvas *m_mainCanvas = nullptr;
    QPointer<BpmAdvancedEditor> m_advancedEditor;
    int m_selectedIndex;
};
