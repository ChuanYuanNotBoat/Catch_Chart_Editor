#pragma once

#include "CustomWidgets/RightPanel.h"
#include <QButtonGroup>
#include <QList>
#include <QString>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QPushButton;
class QVBoxLayout;
class QLabel;
class QRadioButton;
class QGroupBox;
class QToolButton;
class LongRangeSelector;
class PlaybackController;

class NoteEditPanel : public RightPanel
{
    Q_OBJECT
public:
    enum EditorMode
    {
        PlaceNoteMode = 0,
        PlaceRainMode = 1,
        DeleteMode = 2,
        SelectMode = 3,
        PlaceAnchorMode = 4,
    };

    struct PluginPlacementAction
    {
        QString pluginId;
        QString actionId;
        QString title;
        QString tooltip;
        bool checkable = false;
        bool checked = false;
    };

    explicit NoteEditPanel(QWidget *parent = nullptr);
    void setChartController(ChartController *controller) override;
    void setSelectionController(SelectionController *controller) override;
    void setPlaybackController(PlaybackController *controller);
    void setPluginPlacementActions(const QList<PluginPlacementAction> &actions);
    void retranslateUi();
    void setMirrorAxisValue(int axisX);
    void setModeFromHost(int mode);
    int currentMode() const { return m_currentMode; }
    void setNoteChainControlsVisible(bool visible);
    void syncNoteChainControlsFromEditor(bool anchorPlace, bool curveVisible, bool polyline,
                                         bool noteSnap, bool selAnchors, bool selSegments, bool selNotes);


    LongRangeSelector *longRangeSelector() const { return m_longRangeSelector; }

signals:
    void modeChanged(int mode);
    void timeDivisionChanged(int division);
    void gridDivisionChanged(int division);
    void gridSnapChanged(bool enabled);
    void deleteOnceRequested();
    void copyRequested();
    void mirrorAxisChanged(int axisX);
    void mirrorGuideVisibilityChanged(bool visible);
    void mirrorPreviewVisibilityChanged(bool visible);
    void rangeChanged(double startBeat, double endBeat);
    void rangeVisibilityChanged(bool visible);

    void mirrorFlipRequested();
    void pluginPlacementActionTriggered(const QString &pluginId, const QString &actionId);
    // NoteChain native controls
    void noteChainAnchorPlaceToggled(bool on);
    void noteChainCurveVisibleToggled(bool on);
    void noteChainPolylineModeToggled(bool on);
    void noteChainCommitRequested();
    void noteChainDeleteRequested();
    void noteChainNoteCurveSnapToggled(bool on);
    void noteChainSelectAnchorsToggled(bool on);
    void noteChainSelectSegmentsToggled(bool on);
    void noteChainSelectNotesToggled(bool on);
    void noteChainConnectRequested();
    void noteChainDisconnectRequested();
    void noteChainResetRequested();

private slots:
    void onNoteModeClicked();
    void onRainModeClicked();
    void onDeleteModeClicked();
    void onSelectModeClicked();
    void onGridSettingsClicked();
    // void onGridDivisionChanged(int value);
    void onGridSnapToggled(bool on);
    void onTimeDivisionChanged(int index);
    void onMirrorAxisSpinChanged(int value);

private:
    void setupUi();
    void setMode(int mode);
    void refreshPluginToolsUi();

    ChartController *m_chartController;
    SelectionController *m_selectionController;
    QLabel *m_modeLabel;
    QButtonGroup *m_modeGroup;
    QRadioButton *m_noteRadio;
    QRadioButton *m_rainRadio;
    QRadioButton *m_deleteRadio;
    QRadioButton *m_selectRadio;
    QRadioButton *m_anchorRadio;
    QPushButton *m_deleteOnceButton;
    QToolButton *m_pluginToolsToggleBtn;
    QLabel *m_pluginToolsLabel;
    QWidget *m_pluginToolsContainer;
    QVBoxLayout *m_pluginToolsLayout;
    QLabel *m_timeDivisionLabel;
    QComboBox *m_timeDivisionCombo;
    QCheckBox *m_gridSnapCheck;
    QPushButton *m_gridSettingsBtn;
    QPushButton *m_copyButton;
    QGroupBox *m_mirrorGroup;
    QLabel *m_mirrorAxisLabel;
    QSpinBox *m_mirrorAxisSpin;
    QCheckBox *m_mirrorGuideCheck;
    QCheckBox *m_mirrorPreviewCheck;
    QPushButton *m_mirrorFlipButton;
    LongRangeSelector *m_longRangeSelector;
    int m_currentMode;
    int m_gridDivision;
    bool m_pluginToolsExpanded;

    // NoteChain native controls
    QCheckBox *m_ncAnchorPlaceCheck = nullptr;
    QCheckBox *m_ncCurveVisibleCheck = nullptr;
    QCheckBox *m_ncPolylineModeCheck = nullptr;
    QCheckBox *m_ncNoteCurveSnapCheck = nullptr;
    QCheckBox *m_ncSelectAnchorsCheck = nullptr;
    QCheckBox *m_ncSelectSegmentsCheck = nullptr;
    QCheckBox *m_ncSelectNotesCheck = nullptr;
    QPushButton *m_ncCommitBtn = nullptr;
    QPushButton *m_ncDeleteBtn = nullptr;
    QPushButton *m_ncConnectBtn = nullptr;
    QPushButton *m_ncDisconnectBtn = nullptr;
    QPushButton *m_ncResetBtn = nullptr;
    QWidget *m_ncPlaceholder = nullptr;
    bool m_noteChainControlsVisible = false;
};
