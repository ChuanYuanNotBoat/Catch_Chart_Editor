#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QString>
#include <optional>

class ChartController;
class SelectionController;
class PlaybackController;

class LongRangeSelector : public QWidget
{
    Q_OBJECT
public:
    explicit LongRangeSelector(QWidget *parent = nullptr);

    void setChartController(ChartController *controller);
    void setSelectionController(SelectionController *controller);
    void setPlaybackController(PlaybackController *controller);
    void setTimeDivision(int division);
    int timeDivision() const { return m_timeDivision; }
    void retranslateUi();

    // 范围覆盖层控制
    bool isRangeVisible() const { return m_rangeVisible; }
    void setRangeVisible(bool visible);
    double currentStartBeat() const;
    double currentEndBeat() const;
    bool hasValidRange() const;
    void setStartBeat(double beat);
    void setEndBeat(double beat);

    struct BeatValue
    {
        int integer;
        int numerator;
        int denominator;
    };

    static std::optional<BeatValue> parseBeat(const QString &text);
    static bool isValidBeat(const BeatValue &bv);
    static bool isValidBeat(int integer, int numerator, int denominator);
    static QString formatBeat(const BeatValue &bv);
    static QString formatBeat(int integer, int numerator, int denominator);
    static double beatToDouble(const BeatValue &bv);
    static double beatToDouble(int integer, int numerator, int denominator);

signals:
    void rangeChanged(double startBeat, double endBeat);
    void rangeVisibilityChanged(bool visible);

private slots:
    void onStartTextChanged(const QString &text);
    void onEndTextChanged(const QString &text);
    void onStartNowClicked();
    void onEndNowClicked();
    void onSelectClicked();
    void onShowOverlayToggled(bool checked);
    void onUndoMergeTimeout();
    void onRangeChangeDebounce();

private:
    void setupUi();
    void updateValidationState();
    void autoSwapIfNeeded();
    void adjustBeat(QLineEdit *edit, int delta, bool shiftHeld);
    void fillCurrentTime(QLineEdit *edit);
    void performSelection();
    void saveUndoState(QLineEdit *edit);
    void tryTriggerUndo(QLineEdit *edit);
    void notifyCanvasRangeIfValid();
    bool eventFilter(QObject *obj, QEvent *event) override;

    QLineEdit *m_startEdit;
    QLineEdit *m_endEdit;
    QPushButton *m_startNowBtn;
    QPushButton *m_endNowBtn;
    QPushButton *m_selectBtn;
    QCheckBox *m_showOverlayCheck;

    ChartController *m_chartController = nullptr;
    SelectionController *m_selectionController = nullptr;
    PlaybackController *m_playbackController = nullptr;
    int m_timeDivision = 4;

    // 范围覆盖层状态
    bool m_rangeVisible = true;

    // 撤销合并状态（每个输入框独立）
    QTimer *m_undoMergeTimer;
    QString m_savedStartText; // 定时器启动时保存的 start 框值
    QString m_savedEndText;   // 定时器启动时保存的 end 框值
    QLineEdit *m_lastAdjustedEdit = nullptr; // 最近一次滚轮调整的输入框
    bool m_undoMergeActive = false;

    // 范围变更防抖
    QTimer *m_rangeChangeDebounceTimer;
    double m_lastEmittedStartBeat = -1;
    double m_lastEmittedEndBeat = -1;
};