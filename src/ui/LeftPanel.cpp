#include "LeftPanel.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

LeftPanel::LeftPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void LeftPanel::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_playPauseBtn = new QPushButton(tr("Play"), this);
    layout->addWidget(m_playPauseBtn);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &LeftPanel::onPlayPauseClicked);

    QHBoxLayout *zoomLayout = new QHBoxLayout;
    m_zoomLabel = new QLabel(tr("Zoom:"), this);
    m_zoomOutBtn = new QPushButton(tr("-"), this);
    m_zoomOutBtn->setFixedWidth(30);
    m_zoomInBtn = new QPushButton(tr("+"), this);
    m_zoomInBtn->setFixedWidth(30);

    m_timeScaleSpin = new QDoubleSpinBox(this);
    m_timeScaleSpin->setRange(0.2, 10.0);
    m_timeScaleSpin->setSingleStep(0.1);
    m_timeScaleSpin->setDecimals(2);
    m_timeScaleSpin->setValue(2.25);
    m_timeScaleSpin->setSuffix(tr("x"));

    zoomLayout->addWidget(m_zoomLabel);
    zoomLayout->addWidget(m_zoomOutBtn);
    zoomLayout->addWidget(m_timeScaleSpin);
    zoomLayout->addWidget(m_zoomInBtn);
    layout->addLayout(zoomLayout);

    connect(m_zoomInBtn, &QPushButton::clicked, this, &LeftPanel::onZoomInClicked);
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &LeftPanel::onZoomOutClicked);
    connect(m_timeScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &LeftPanel::onTimeScaleChanged);

    m_pluginSectionLabel = new QLabel(tr("Plugin Shortcuts"), this);
    m_pluginSectionContainer = new QWidget(this);
    m_pluginButtonsLayout = new QVBoxLayout(m_pluginSectionContainer);
    m_pluginButtonsLayout->setContentsMargins(0, 0, 0, 0);
    m_pluginButtonsLayout->setSpacing(6);
    m_pluginSectionLabel->setVisible(false);
    m_pluginSectionContainer->setVisible(false);
    layout->addWidget(m_pluginSectionLabel);
    layout->addWidget(m_pluginSectionContainer);

    layout->addStretch();
}

void LeftPanel::onPlayPauseClicked()
{
    if (!m_playbackController)
        return;
    if (m_playbackController->state() == PlaybackController::Playing)
    {
        m_playbackController->pause();
        m_playPauseBtn->setText(tr("Play"));
    }
    else
    {
        m_playbackController->play();
        m_playPauseBtn->setText(tr("Pause"));
    }
}

void LeftPanel::onZoomInClicked()
{
    if (!m_chartCanvas)
        return;
    m_chartCanvas->setTimeScale(m_chartCanvas->timeScale() * 1.2);
    m_timeScaleSpin->blockSignals(true);
    m_timeScaleSpin->setValue(m_chartCanvas->timeScale());
    m_timeScaleSpin->blockSignals(false);
}

void LeftPanel::onZoomOutClicked()
{
    if (!m_chartCanvas)
        return;
    m_chartCanvas->setTimeScale(m_chartCanvas->timeScale() / 1.2);
    m_timeScaleSpin->blockSignals(true);
    m_timeScaleSpin->setValue(m_chartCanvas->timeScale());
    m_timeScaleSpin->blockSignals(false);
}

void LeftPanel::onTimeScaleChanged(double scale)
{
    if (!m_chartCanvas)
        return;
    m_chartCanvas->setTimeScale(scale);
}

void LeftPanel::setChartController(ChartController *controller)
{
    m_chartController = controller;
}

void LeftPanel::setPlaybackController(PlaybackController *controller)
{
    m_playbackController = controller;
    connect(controller, &PlaybackController::stateChanged, this, [this](PlaybackController::State state)
            { m_playPauseBtn->setText(state == PlaybackController::Playing ? tr("Pause") : tr("Play")); });
}

void LeftPanel::setChartCanvas(ChartCanvas *canvas)
{
    m_chartCanvas = canvas;
    if (!canvas)
        return;

    connect(canvas, &ChartCanvas::timeScaleChanged, this, [this](double scale)
            {
                m_timeScaleSpin->blockSignals(true);
                m_timeScaleSpin->setValue(scale);
                m_timeScaleSpin->blockSignals(false); });
    m_timeScaleSpin->setValue(canvas->timeScale());
}

void LeftPanel::setPluginQuickActions(const QList<PluginQuickAction> &actions)
{
    const bool prevUpdates = updatesEnabled();
    setUpdatesEnabled(false);

    while (QLayoutItem *item = m_pluginButtonsLayout->takeAt(0))
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    QList<QString> pluginOrder;
    QHash<QString, QList<PluginQuickAction>> grouped;
    for (const PluginQuickAction &a : actions)
    {
        if (a.pluginId.isEmpty() || a.actionId.isEmpty() || a.title.isEmpty())
            continue;
        if (!grouped.contains(a.pluginId))
            pluginOrder.append(a.pluginId);
        grouped[a.pluginId].append(a);
    }

    for (const QString &pluginId : pluginOrder)
    {
        const QList<PluginQuickAction> pluginActions = grouped.value(pluginId);
        if (pluginActions.isEmpty())
            continue;

        const bool alwaysExpanded = (pluginId.trimmed() == QStringLiteral("builtin.note_color_formatter"));
        bool expanded = alwaysExpanded;
        if (!alwaysExpanded)
        {
            if (m_pluginSectionExpanded.contains(pluginId))
                expanded = m_pluginSectionExpanded.value(pluginId, false);
            else
                expanded = false;
        }
        m_pluginSectionExpanded.insert(pluginId, expanded);

        const QString pluginTitle = pluginActions.first().pluginDisplayName.trimmed().isEmpty()
                                        ? pluginId
                                        : pluginActions.first().pluginDisplayName.trimmed();
        QGroupBox *section = new QGroupBox(pluginTitle, m_pluginSectionContainer);
        section->setCheckable(!alwaysExpanded);
        section->setChecked(expanded);
        QVBoxLayout *sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(8, 8, 8, 8);
        sectionLayout->setSpacing(4);

        QWidget *body = new QWidget(section);
        QVBoxLayout *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        bodyLayout->setSpacing(6);
        body->setVisible(expanded);
        sectionLayout->addWidget(body);

        if (!alwaysExpanded && section->isCheckable())
        {
            connect(section, &QGroupBox::toggled, this, [this, pluginId, body](bool checked)
                    {
                        m_pluginSectionExpanded.insert(pluginId, checked);
                        body->setVisible(checked);
                        updateGeometry(); });
        }

        for (const PluginQuickAction &a : pluginActions)
        {
            QPushButton *btn = new QPushButton(a.title, body);
            if (!a.tooltip.isEmpty())
                btn->setToolTip(a.tooltip);
            btn->setCheckable(a.checkable);
            if (a.checkable)
                btn->setChecked(a.checked);
            btn->setMinimumWidth(0);
            btn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            connect(btn, &QPushButton::clicked, this, [this, a]()
                    { emit pluginQuickActionTriggered(a.pluginId, a.actionId); });
            bodyLayout->addWidget(btn);
        }

        m_pluginButtonsLayout->addWidget(section);
    }

    const bool hasActions = (m_pluginButtonsLayout->count() > 0);
    m_pluginSectionLabel->setVisible(hasActions);
    m_pluginSectionContainer->setVisible(hasActions);

    setUpdatesEnabled(prevUpdates);
    updateGeometry();
    update();
}

void LeftPanel::retranslateUi()
{
    if (!m_playPauseBtn)
        return;

    const bool isPlaying = (m_playbackController && m_playbackController->state() == PlaybackController::Playing);
    m_playPauseBtn->setText(isPlaying ? tr("Pause") : tr("Play"));
    if (m_zoomLabel)
        m_zoomLabel->setText(tr("Zoom:"));
    if (m_pluginSectionLabel)
        m_pluginSectionLabel->setText(tr("Plugin Shortcuts"));
    if (m_timeScaleSpin)
        m_timeScaleSpin->setSuffix(tr("x"));
}
