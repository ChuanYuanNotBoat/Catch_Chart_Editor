#include "PluginActionPanel.h"

#include "controller/SelectionController.h"
#include "ui/LongRangeSelector.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QSet>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

PluginActionPanel::PluginActionPanel(QWidget *parent)
    : QWidget(parent), m_layout(new QVBoxLayout(this))
{
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(8);
    rebuildUi();
}

void PluginActionPanel::setActions(const QList<Action> &actions)
{
    m_actions = actions;
    rebuildUi();
}

void PluginActionPanel::setChartController(ChartController *controller)
{
    m_chartController = controller;
    for (const ScopeControls &controls : std::as_const(m_scopeControls))
    {
        if (controls.range)
            controls.range->setChartController(controller);
    }
}

void PluginActionPanel::setSelectionController(SelectionController *controller)
{
    if (m_selectionController)
        disconnect(m_selectionController, nullptr, this, nullptr);
    m_selectionController = controller;
    m_selectedCount = controller ? controller->selectedIndices().size() : 0;
    if (controller)
    {
        connect(controller, &SelectionController::selectionChanged, this,
                [this](const QSet<int> &selection) { updateSelectionCount(selection.size()); });
    }
    updateSelectionCount(m_selectedCount);
}

void PluginActionPanel::setPlaybackController(PlaybackController *controller)
{
    m_playbackController = controller;
    for (const ScopeControls &controls : std::as_const(m_scopeControls))
    {
        if (controls.range)
            controls.range->setPlaybackController(controller);
    }
}

void PluginActionPanel::setViewportRange(int timeDivision, double startBeat, double endBeat)
{
    m_timeDivision = qMax(1, timeDivision);
    m_rangeStart = startBeat;
    m_rangeEnd = qMax(startBeat, endBeat);
    for (const ScopeControls &controls : std::as_const(m_scopeControls))
    {
        if (!controls.range)
            continue;
        controls.range->setTimeDivision(m_timeDivision);
        controls.range->setStartBeat(m_rangeStart);
        controls.range->setEndBeat(m_rangeEnd);
    }
}

void PluginActionPanel::focusAction(const QString &pluginId, const QString &actionId)
{
    const QString key = pluginId + QStringLiteral("::") + actionId;
    if (QAbstractButton *button = m_actionButtons.value(key))
    {
        button->setFocus(Qt::OtherFocusReason);
        button->ensurePolished();
    }
}

void PluginActionPanel::retranslateUi()
{
    rebuildUi();
}

void PluginActionPanel::clearUi()
{
    m_scopeControls.clear();
    m_actionButtons.clear();
    while (QLayoutItem *item = m_layout->takeAt(0))
    {
        if (QWidget *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

QString PluginActionPanel::actionKey(const QVariantMap &meta) const
{
    return meta.value(QStringLiteral("plugin_id")).toString()
        + QStringLiteral("::")
        + meta.value(QStringLiteral("action_id")).toString();
}

void PluginActionPanel::rebuildUi()
{
    clearUi();

    if (m_actions.isEmpty())
    {
        QLabel *empty = new QLabel(tr("No plugin actions available."), this);
        empty->setWordWrap(true);
        m_layout->addWidget(empty);
        m_layout->addStretch();
        return;
    }

    QHash<QString, QVBoxLayout *> groupLayouts;
    for (const Action &action : std::as_const(m_actions))
    {
        const QString pluginId = action.meta.value(QStringLiteral("plugin_id")).toString();
        const QString actionId = action.meta.value(QStringLiteral("action_id")).toString();
        if (pluginId.isEmpty() || actionId.isEmpty())
            continue;

        QVBoxLayout *groupLayout = groupLayouts.value(pluginId, nullptr);
        if (!groupLayout)
        {
            const QString groupTitle = action.pluginDisplayName.isEmpty()
                ? pluginId : action.pluginDisplayName;
            QGroupBox *group = new QGroupBox(groupTitle, this);
            groupLayout = new QVBoxLayout(group);
            groupLayout->setSpacing(6);
            m_layout->addWidget(group);
            groupLayouts.insert(pluginId, groupLayout);
        }

        if (!action.description.isEmpty())
        {
            QLabel *description = new QLabel(action.description, this);
            description->setWordWrap(true);
            groupLayout->addWidget(description);
        }

        const QString scopeSelector = action.meta.value(QStringLiteral("scope_selector"))
                                          .toString().trimmed().toLower();
        if (scopeSelector == QLatin1String("note_range"))
        {
            QLabel *scopeLabel = new QLabel(tr("Format Scope:"), this);
            QComboBox *scopeCombo = new QComboBox(this);
            scopeCombo->addItem(tr("Selected Notes (%1)").arg(m_selectedCount), QStringLiteral("selected"));
            scopeCombo->addItem(tr("Beat Range"), QStringLiteral("range"));
            scopeCombo->addItem(tr("Entire Chart"), QStringLiteral("all"));
            scopeCombo->setCurrentIndex(m_selectedCount > 0 ? 0 : 1);
            groupLayout->addWidget(scopeLabel);
            groupLayout->addWidget(scopeCombo);

            LongRangeSelector *range = new LongRangeSelector(this);
            range->setInputOnlyMode(tr("Beat Range"));
            range->setChartController(m_chartController);
            range->setPlaybackController(m_playbackController);
            range->setTimeDivision(m_timeDivision);
            range->setStartBeat(m_rangeStart);
            range->setEndBeat(m_rangeEnd);
            groupLayout->addWidget(range);

            QLabel *hint = new QLabel(
                tr("Only Normal and Rain note start colors are formatted. Sound notes are not changed."),
                this);
            hint->setWordWrap(true);
            groupLayout->addWidget(hint);

            QLabel *validation = new QLabel(this);
            validation->setWordWrap(true);
            validation->setStyleSheet(QStringLiteral("color: palette(link);"));
            validation->hide();
            groupLayout->addWidget(validation);

            const auto updateRangeVisibility = [scopeCombo, range]() {
                range->setVisible(scopeCombo->currentData().toString() == QLatin1String("range"));
            };
            connect(scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [updateRangeVisibility](int) { updateRangeVisibility(); });
            updateRangeVisibility();

            QPushButton *button = new QPushButton(
                action.meta.value(QStringLiteral("title")).toString(), this);
            groupLayout->addWidget(button);
            m_actionButtons.insert(actionKey(action.meta), button);
            m_scopeControls.append({scopeCombo, range, validation});
            connect(button, &QPushButton::clicked, this,
                    [this, action, scopeCombo, range, validation]() {
                        validation->hide();
                        QVariantMap context;
                        const QString scope = scopeCombo->currentData().toString();
                        if (scope == QLatin1String("selected") && m_selectedCount <= 0)
                        {
                            validation->setText(tr("Select at least one note first."));
                            validation->show();
                            return;
                        }
                        context.insert(QStringLiteral("format_scope"), scope);
                        if (scope == QLatin1String("range"))
                        {
                            if (!range->hasValidRange())
                            {
                                validation->setText(tr("Enter a valid beat range."));
                                validation->show();
                                return;
                            }
                            double startBeat = range->currentStartBeat();
                            double endBeat = range->currentEndBeat();
                            if (startBeat > endBeat)
                                std::swap(startBeat, endBeat);
                            context.insert(QStringLiteral("range_start_beat"), startBeat);
                            context.insert(QStringLiteral("range_end_beat"), endBeat);
                        }
                        emit actionRequested(action.meta, context);
                    });
            continue;
        }

        QAbstractButton *button = nullptr;
        if (action.meta.value(QStringLiteral("checkable"), false).toBool())
        {
            QCheckBox *check = new QCheckBox(action.meta.value(QStringLiteral("title")).toString(), this);
            check->setChecked(action.meta.value(QStringLiteral("checked"), false).toBool());
            button = check;
        }
        else
        {
            button = new QPushButton(action.meta.value(QStringLiteral("title")).toString(), this);
        }
        groupLayout->addWidget(button);
        m_actionButtons.insert(actionKey(action.meta), button);
        connect(button, &QAbstractButton::clicked, this,
                [this, action]() { emit actionRequested(action.meta, QVariantMap{}); });
    }

    m_layout->addStretch();
    updateSelectionCount(m_selectedCount);
}

void PluginActionPanel::updateSelectionCount(int count)
{
    m_selectedCount = qMax(0, count);
    for (const ScopeControls &controls : std::as_const(m_scopeControls))
    {
        if (!controls.combo)
            continue;
        controls.combo->setItemText(0, tr("Selected Notes (%1)").arg(m_selectedCount));
        if (auto *model = qobject_cast<QStandardItemModel *>(controls.combo->model()))
        {
            if (QStandardItem *item = model->item(0))
                item->setEnabled(m_selectedCount > 0);
        }
        if (m_selectedCount == 0 && controls.combo->currentData().toString() == QLatin1String("selected"))
            controls.combo->setCurrentIndex(1);
    }
}
