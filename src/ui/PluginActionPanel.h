#pragma once

#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QWidget>

class ChartController;
class SelectionController;
class PlaybackController;
class LongRangeSelector;
class QAbstractButton;
class QComboBox;
class QLabel;
class QVBoxLayout;

class PluginActionPanel final : public QWidget
{
    Q_OBJECT
public:
    struct Action
    {
        QString pluginDisplayName;
        QString description;
        QVariantMap meta;
    };

    explicit PluginActionPanel(QWidget *parent = nullptr);

    void setActions(const QList<Action> &actions);
    void setChartController(ChartController *controller);
    void setSelectionController(SelectionController *controller);
    void setPlaybackController(PlaybackController *controller);
    void setViewportRange(int timeDivision, double startBeat, double endBeat);
    void focusAction(const QString &pluginId, const QString &actionId);
    void retranslateUi();

signals:
    void actionRequested(const QVariantMap &meta, const QVariantMap &actionContext);

private:
    struct ScopeControls
    {
        QPointer<QComboBox> combo;
        QPointer<LongRangeSelector> range;
        QPointer<QLabel> validation;
    };

    void rebuildUi();
    void clearUi();
    void updateSelectionCount(int count);
    QString actionKey(const QVariantMap &meta) const;

    QVBoxLayout *m_layout = nullptr;
    QList<Action> m_actions;
    QList<ScopeControls> m_scopeControls;
    QHash<QString, QPointer<QAbstractButton>> m_actionButtons;
    ChartController *m_chartController = nullptr;
    SelectionController *m_selectionController = nullptr;
    PlaybackController *m_playbackController = nullptr;
    int m_selectedCount = 0;
    int m_timeDivision = 4;
    double m_rangeStart = 0.0;
    double m_rangeEnd = 4.0;
};
