#pragma once

#include "CustomWidgets/RightPanel.h"
#include "audio/BpmDetector.h"
#include "file/BpmAuxFiles.h"
#include <QVector>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QCheckBox;
class PlaybackController;

class BPMTimePanel : public RightPanel
{
    Q_OBJECT
public:
    explicit BPMTimePanel(QWidget *parent = nullptr);
    void setChartController(ChartController *controller) override;
    void setSelectionController(SelectionController *controller) override;
    void setPlaybackController(PlaybackController *controller);
    void retranslateUi();

private slots:
    void refreshBpmList();
    void onItemSelected(int row);
    void onAddClicked();
    void onRemoveClicked();
    void onBpmChanged(double);
    void onMeasureBpmClicked();
    void onShowExcludesToggled(bool checked);
    void onAddExcludeClicked();
    void onRemoveExcludeClicked();
    void onBpmItemChanged(QListWidgetItem *item);
    void onExcludeItemChanged(QListWidgetItem *item);

private:
    void setupUi();
    void refreshExcludesList();
    bool isBeatExcluded(int beatNum, int numerator, int denominator,
                        const BpmAuxFiles::BpmExcludesData &excludesData) const;
    double currentChartTimeMs() const;
    QString currentAudioFilePath() const;
    bool measureBpmFromAudio(int durationSeconds,
                             int mode,
                             BpmDetector::DetectionResult &outResult,
                             QString *outError) const;

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
    QLabel *m_excludesLabel;
    QCheckBox *m_showExcludesCheck;
    QListWidget *m_excludesListWidget;
    QPushButton *m_addExcludeBtn;
    QPushButton *m_removeExcludeBtn;
    QWidget *m_excludesContainer;
    int m_selectedIndex;
    BpmAuxFiles::BpmExcludesData m_currentExcludesData;
};
