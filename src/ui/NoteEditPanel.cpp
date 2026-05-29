#include "NoteEditPanel.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "logic/UnreachableDivisionManager.h"
#include "utils/Logger.h"
#include <QtGlobal>
#include <QButtonGroup>
#include <QRadioButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDebug>
#include <QSignalBlocker>

NoteEditPanel::NoteEditPanel(QWidget *parent)
    : RightPanel(parent), m_chartController(nullptr), m_selectionController(nullptr), m_currentMode(0), m_gridDivision(20), m_pluginToolsExpanded(false)
{
    setupUi();
}

void NoteEditPanel::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_modeLabel = new QLabel(tr("Mode:"), this);
    mainLayout->addWidget(m_modeLabel);

    m_modeGroup = new QButtonGroup(this);
    m_noteRadio = new QRadioButton(tr("Place Note"), this);
    m_rainRadio = new QRadioButton(tr("Place Rain"), this);
    m_deleteRadio = new QRadioButton(tr("Delete Mode"), this);
    m_selectRadio = new QRadioButton(tr("Select Mode"), this);
    m_anchorRadio = new QRadioButton(tr("Place Anchor"), this);
    m_deleteOnceButton = new QPushButton(tr("Del"), this);
    m_deleteOnceButton->setFixedWidth(40);
    m_deleteOnceButton->setToolTip(tr("Delete selected notes once"));
    m_noteRadio->setChecked(true);
    m_modeGroup->addButton(m_noteRadio, PlaceNoteMode);
    m_modeGroup->addButton(m_rainRadio, PlaceRainMode);
    m_modeGroup->addButton(m_deleteRadio, DeleteMode);
    m_modeGroup->addButton(m_selectRadio, SelectMode);
    m_modeGroup->addButton(m_anchorRadio, PlaceAnchorMode);
    connect(m_modeGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *button)
            { setMode(m_modeGroup->id(button)); });

    mainLayout->addWidget(m_noteRadio);
    mainLayout->addWidget(m_rainRadio);
    QWidget *deleteRow = new QWidget(this);
    QHBoxLayout *deleteRowLayout = new QHBoxLayout(deleteRow);
    deleteRowLayout->setContentsMargins(0, 0, 0, 0);
    deleteRowLayout->setSpacing(6);
    deleteRowLayout->addWidget(m_deleteRadio);
    deleteRowLayout->addStretch();
    deleteRowLayout->addWidget(m_deleteOnceButton);
    mainLayout->addWidget(deleteRow);
    mainLayout->addWidget(m_selectRadio);
    mainLayout->addWidget(m_anchorRadio);
    connect(m_deleteOnceButton, &QPushButton::clicked, this, &NoteEditPanel::deleteOnceRequested);

    m_pluginToolsToggleBtn = new QToolButton(this);
    m_pluginToolsToggleBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_pluginToolsToggleBtn->setCheckable(true);
    m_pluginToolsToggleBtn->setChecked(false);
    connect(m_pluginToolsToggleBtn, &QToolButton::clicked, this, [this](bool checked)
            {
                m_pluginToolsExpanded = checked;
                refreshPluginToolsUi();
            });
    mainLayout->addWidget(m_pluginToolsToggleBtn);

    m_pluginToolsLabel = new QLabel(tr("Note Placement Tools:"), this);
    m_pluginToolsContainer = new QWidget(this);
    m_pluginToolsLayout = new QVBoxLayout(m_pluginToolsContainer);
    m_pluginToolsLayout->setContentsMargins(0, 0, 0, 0);
    m_pluginToolsLayout->setSpacing(6);
    m_pluginToolsToggleBtn->setVisible(false);
    m_pluginToolsLabel->setVisible(false);
    m_pluginToolsContainer->setVisible(false);
    mainLayout->addWidget(m_pluginToolsLabel);
    mainLayout->addWidget(m_pluginToolsContainer);

    // Copy button.
    m_copyButton = new QPushButton(tr("Copy"), this);
    connect(m_copyButton, &QPushButton::clicked, this, &NoteEditPanel::copyRequested);
    mainLayout->addWidget(m_copyButton);

    m_timeDivisionLabel = new QLabel(tr("Time Division:"), this);
    mainLayout->addWidget(m_timeDivisionLabel);
    m_timeDivisionCombo = new QComboBox(this);
    QStringList divisions = {"1", "2", "3", "4", "6", "8", "12", "16", "24", "32"};
    for (const QString &d : divisions)
        m_timeDivisionCombo->addItem(d);
    m_timeDivisionCombo->setEditable(true);
    m_timeDivisionCombo->setCurrentText("4");
    connect(m_timeDivisionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NoteEditPanel::onTimeDivisionChanged);
    mainLayout->addWidget(m_timeDivisionCombo);

    m_gridSnapCheck = new QCheckBox(tr("Grid Snap"), this);
    m_gridSnapCheck->setChecked(true);
    connect(m_gridSnapCheck, &QCheckBox::toggled, this, &NoteEditPanel::onGridSnapToggled);
    mainLayout->addWidget(m_gridSnapCheck);

    m_gridSettingsBtn = new QPushButton(tr("Grid Settings..."), this);
    connect(m_gridSettingsBtn, &QPushButton::clicked, this, &NoteEditPanel::onGridSettingsClicked);
    mainLayout->addWidget(m_gridSettingsBtn);

    // 不可达分度开关（右侧面板位置2）
    m_showUnreachableDivisions = UnreachableDivisionManager::loadShowUnreachable();
    m_showUnreachableCheck = new QCheckBox(tr("显示不可达分度"), this);
    m_showUnreachableCheck->setChecked(m_showUnreachableDivisions);
    connect(m_showUnreachableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_showUnreachableDivisions = checked;
        UnreachableDivisionManager::saveShowUnreachable(checked);
        emit showUnreachableDivisionsToggled(checked);
    });
    mainLayout->addWidget(m_showUnreachableCheck);

    m_mirrorGroup = new QGroupBox(tr("Mirror Flip"), this);
    QVBoxLayout *mirrorLayout = new QVBoxLayout(m_mirrorGroup);
    QHBoxLayout *axisLayout = new QHBoxLayout;
    m_mirrorAxisLabel = new QLabel(tr("Axis X:"), m_mirrorGroup);
    m_mirrorAxisSpin = new QSpinBox(m_mirrorGroup);
    m_mirrorAxisSpin->setRange(0, 512);
    m_mirrorAxisSpin->setValue(256);
    axisLayout->addWidget(m_mirrorAxisLabel);
    axisLayout->addWidget(m_mirrorAxisSpin, 1);
    mirrorLayout->addLayout(axisLayout);

    m_mirrorGuideCheck = new QCheckBox(tr("Show Guide"), m_mirrorGroup);
    m_mirrorGuideCheck->setChecked(false);
    mirrorLayout->addWidget(m_mirrorGuideCheck);

    m_mirrorPreviewCheck = new QCheckBox(tr("Show Preview"), m_mirrorGroup);
    m_mirrorPreviewCheck->setChecked(false);
    mirrorLayout->addWidget(m_mirrorPreviewCheck);

    m_mirrorFlipButton = new QPushButton(tr("Flip Selected"), m_mirrorGroup);
    mirrorLayout->addWidget(m_mirrorFlipButton);
    mainLayout->addWidget(m_mirrorGroup);

    connect(m_mirrorAxisSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &NoteEditPanel::onMirrorAxisSpinChanged);
    connect(m_mirrorGuideCheck, &QCheckBox::toggled, this, &NoteEditPanel::mirrorGuideVisibilityChanged);
    connect(m_mirrorPreviewCheck, &QCheckBox::toggled, this, &NoteEditPanel::mirrorPreviewVisibilityChanged);
    connect(m_mirrorFlipButton, &QPushButton::clicked, this, &NoteEditPanel::mirrorFlipRequested);

    mainLayout->addStretch();
}

void NoteEditPanel::setMode(int mode)
{
    if (m_currentMode == mode)
        return;
    m_currentMode = mode;
    emit modeChanged(mode);
}

void NoteEditPanel::onNoteModeClicked() { setMode(0); }
void NoteEditPanel::onRainModeClicked() { setMode(1); }
void NoteEditPanel::onDeleteModeClicked() { setMode(2); }
void NoteEditPanel::onSelectModeClicked() { setMode(3); }

void NoteEditPanel::onGridSettingsClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Grid Settings"));
    QFormLayout form(&dialog);

    QCheckBox *snapCheck = new QCheckBox(tr("Enable Grid Snap"));
    snapCheck->setChecked(m_gridSnapCheck->isChecked());

    QSpinBox *divisionSpin = new QSpinBox;
    divisionSpin->setRange(4, 64);
    divisionSpin->setValue(m_gridDivision); // Use current grid division instead of hardcoded default.

    form.addRow(tr("Snap to Grid:"), snapCheck);
    form.addRow(tr("Grid Divisions (4-64):"), divisionSpin);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(buttons);

    if (dialog.exec() == QDialog::Accepted)
    {
        m_gridSnapCheck->setChecked(snapCheck->isChecked());
        int newDivision = divisionSpin->value();
        m_gridDivision = newDivision;
        emit gridDivisionChanged(newDivision);
        Logger::info(QString("[Grid] NoteEditPanel: grid division changed to %1").arg(newDivision));
    }
}

void NoteEditPanel::onGridSnapToggled(bool on)
{
    Logger::info(QString("[Grid] NoteEditPanel::onGridSnapToggled: %1").arg(on));
    emit gridSnapChanged(on);
}

void NoteEditPanel::onTimeDivisionChanged(int index)
{
    Q_UNUSED(index);
    int division = m_timeDivisionCombo->currentText().toInt();
    if (division < 1)
        division = 1;
    if (division > 96)
        division = 96;
    qDebug() << "NoteEditPanel: Time division changed to" << division;
    emit timeDivisionChanged(division);
}

void NoteEditPanel::onMirrorAxisSpinChanged(int value)
{
    emit mirrorAxisChanged(value);
}

void NoteEditPanel::setChartController(ChartController *controller)
{
    m_chartController = controller;
}

void NoteEditPanel::setSelectionController(SelectionController *controller)
{
    m_selectionController = controller;
}

void NoteEditPanel::setModeFromHost(int mode)
{
    QAbstractButton *button = m_modeGroup ? m_modeGroup->button(mode) : nullptr;
    if (!button)
        return;
    const QSignalBlocker blocker(m_modeGroup);
    button->setChecked(true);
    m_currentMode = mode;
}

void NoteEditPanel::setPluginPlacementActions(const QList<PluginPlacementAction> &actions)
{
    if (!m_pluginToolsLayout)
        return;

    while (QLayoutItem *item = m_pluginToolsLayout->takeAt(0))
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (const PluginPlacementAction &a : actions)
    {
        if (a.pluginId.isEmpty() || a.actionId.isEmpty() || a.title.isEmpty())
            continue;

        if (a.checkable)
        {
            QCheckBox *cb = new QCheckBox(a.title, m_pluginToolsContainer);
            cb->setChecked(a.checked);
            if (!a.tooltip.isEmpty())
                cb->setToolTip(a.tooltip);
            connect(cb, &QCheckBox::clicked, this, [this, a](bool)
                    { emit pluginPlacementActionTriggered(a.pluginId, a.actionId); });
            m_pluginToolsLayout->addWidget(cb);
            continue;
        }

        QPushButton *btn = new QPushButton(a.title, m_pluginToolsContainer);
        if (!a.tooltip.isEmpty())
            btn->setToolTip(a.tooltip);
        connect(btn, &QPushButton::clicked, this, [this, a](bool)
                { emit pluginPlacementActionTriggered(a.pluginId, a.actionId); });
        m_pluginToolsLayout->addWidget(btn);
    }

    refreshPluginToolsUi();
}

void NoteEditPanel::refreshPluginToolsUi()
{
    const bool hasActions = (m_pluginToolsLayout && m_pluginToolsLayout->count() > 0);
    if (m_pluginToolsToggleBtn)
    {
        m_pluginToolsToggleBtn->setVisible(hasActions);
        m_pluginToolsToggleBtn->setChecked(m_pluginToolsExpanded);
        const QString arrow = m_pluginToolsExpanded ? QStringLiteral("▼ ") : QStringLiteral("▶ ");
        m_pluginToolsToggleBtn->setText(arrow + tr("Curve Plugin Options"));
    }
    if (m_pluginToolsLabel)
        m_pluginToolsLabel->setVisible(hasActions);
    if (m_pluginToolsContainer)
        m_pluginToolsContainer->setVisible(hasActions && m_pluginToolsExpanded);
}

void NoteEditPanel::setMirrorAxisValue(int axisX)
{
    if (!m_mirrorAxisSpin)
        return;

    const QSignalBlocker blocker(m_mirrorAxisSpin);
    m_mirrorAxisSpin->setValue(axisX);
}

void NoteEditPanel::retranslateUi()
{
    if (m_modeLabel)
        m_modeLabel->setText(tr("Mode:"));
    if (m_noteRadio)
        m_noteRadio->setText(tr("Place Note"));
    if (m_rainRadio)
        m_rainRadio->setText(tr("Place Rain"));
    if (m_deleteRadio)
        m_deleteRadio->setText(tr("Delete Mode"));
    if (m_deleteOnceButton)
    {
        m_deleteOnceButton->setText(tr("Del"));
        m_deleteOnceButton->setToolTip(tr("Delete selected notes once"));
    }
    if (m_selectRadio)
        m_selectRadio->setText(tr("Select Mode"));
    if (m_anchorRadio)
        m_anchorRadio->setText(tr("Place Anchor"));
    if (m_pluginToolsLabel)
        m_pluginToolsLabel->setText(tr("Note Placement Tools:"));
    refreshPluginToolsUi();
    if (m_copyButton)
        m_copyButton->setText(tr("Copy"));
    if (m_timeDivisionLabel)
        m_timeDivisionLabel->setText(tr("Time Division:"));
    if (m_gridSnapCheck)
        m_gridSnapCheck->setText(tr("Grid Snap"));
    if (m_gridSettingsBtn)
        m_gridSettingsBtn->setText(tr("Grid Settings..."));
    if (m_mirrorGroup)
        m_mirrorGroup->setTitle(tr("Mirror Flip"));
    if (m_mirrorAxisLabel)
        m_mirrorAxisLabel->setText(tr("Axis X:"));
    if (m_mirrorGuideCheck)
        m_mirrorGuideCheck->setText(tr("Show Guide"));
    if (m_mirrorPreviewCheck)
        m_mirrorPreviewCheck->setText(tr("Show Preview"));
    if (m_mirrorFlipButton)
        m_mirrorFlipButton->setText(tr("Flip Selected"));
    if (m_showUnreachableCheck)
        m_showUnreachableCheck->setText(tr("显示不可达分度"));
}

void NoteEditPanel::setShowUnreachableDivisions(bool enabled)
{
    if (m_showUnreachableDivisions == enabled)
        return;
    m_showUnreachableDivisions = enabled;
    if (m_showUnreachableCheck)
    {
        const QSignalBlocker blocker(m_showUnreachableCheck);
        m_showUnreachableCheck->setChecked(enabled);
    }
}

