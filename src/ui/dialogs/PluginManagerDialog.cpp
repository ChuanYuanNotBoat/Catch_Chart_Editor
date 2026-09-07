#include "PluginManagerDialog.h"
#include "utils/NativeWindowTheme.h"
#include "utils/Settings.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QEventLoop>

namespace
{
    enum Column
    {
        ColEnabled = 0,
        ColActive,
        ColName,
        ColId,
        ColVersion,
        ColAuthor,
        ColCount
    };
}

PluginManagerDialog::PluginManagerDialog(PluginManager *pluginManager, QWidget *parent)
    : QDialog(parent), m_pluginManager(pluginManager)
{
    setWindowTitle(tr("Plugin Manager"));
    resize(980, 600);
    setStyleSheet(NativeWindowTheme::dialogStyleSheet(Settings::instance().backgroundColor()));

    QVBoxLayout *root = new QVBoxLayout(this);
    m_summaryLabel = new QLabel(this);
    root->addWidget(m_summaryLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Enabled"), tr("Active"), tr("Name"), tr("Plugin ID"), tr("Version"), tr("Author")});
    m_table->horizontalHeader()->setSectionResizeMode(ColEnabled, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColActive, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColId, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColVersion, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColAuthor, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 2);

    m_details = new QTextEdit(this);
    m_details->setReadOnly(true);
    root->addWidget(m_details, 1);

    QHBoxLayout *buttonsLayout = new QHBoxLayout;
    buttonsLayout->addStretch();
    m_openFolderButton = new QPushButton(tr("Open Plugins Folder"), this);
    m_reloadSelectedButton = new QPushButton(tr("Reload Selected Plugin"), this);
    m_reloadButton = new QPushButton(tr("Reload Plugins"), this);
    QPushButton *closeButton = new QPushButton(tr("Close"), this);
    buttonsLayout->addWidget(m_openFolderButton);
    buttonsLayout->addWidget(m_reloadSelectedButton);
    buttonsLayout->addWidget(m_reloadButton);
    buttonsLayout->addWidget(closeButton);
    root->addLayout(buttonsLayout);

    connect(m_reloadButton, &QPushButton::clicked, this, &PluginManagerDialog::reloadPlugins);
    connect(m_reloadSelectedButton, &QPushButton::clicked, this, &PluginManagerDialog::reloadSelectedPlugin);
    connect(m_openFolderButton, &QPushButton::clicked, this, &PluginManagerDialog::openPluginsFolder);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &PluginManagerDialog::updateDetails);
    connect(m_table, &QTableWidget::itemChanged, this, &PluginManagerDialog::onItemChanged);

    rebuildTable();
}

void PluginManagerDialog::reloadPlugins()
{
    if (!m_pluginManager)
        return;

    emit pluginsReloading();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_pluginManager->reloadPlugins();
    rebuildTable();
    emit pluginsReloaded();
}

void PluginManagerDialog::reloadSelectedPlugin()
{
    if (!m_pluginManager)
        return;

    const QString targetPluginId = selectedPluginId();
    if (targetPluginId.isEmpty())
        return;

    emit pluginsReloading();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    m_pluginManager->reloadPlugins();
    rebuildTable();

    for (int row = 0; row < m_infos.size(); ++row)
    {
        if (m_infos[row].pluginId == targetPluginId)
        {
            m_table->selectRow(row);
            break;
        }
    }
    emit pluginsReloaded();
}

void PluginManagerDialog::openPluginsFolder()
{
    QString path;
    if (m_pluginManager)
        path = m_pluginManager->pluginsDir();
    if (path.isEmpty())
        path = QCoreApplication::applicationDirPath() + "/plugins";

    if (!QDir(path).exists())
    {
        if (!QDir().mkpath(path))
        {
            QMessageBox::warning(this, tr("Plugin Manager"), tr("Failed to create plugin directory:\n%1").arg(path));
            return;
        }
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
    {
        QMessageBox::warning(this, tr("Plugin Manager"), tr("Failed to open plugin directory:\n%1").arg(path));
    }
}

void PluginManagerDialog::updateDetails()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_infos.size())
    {
        if (m_reloadSelectedButton)
            m_reloadSelectedButton->setEnabled(false);
        m_details->clear();
        return;
    }

    if (m_reloadSelectedButton)
        m_reloadSelectedButton->setEnabled(true);

    const PluginManager::PluginInfo &info = m_infos[row];
    QString details;
    details += tr("Name: %1\n").arg(safeText(info.displayName));
    details += tr("Plugin ID: %1\n").arg(safeText(info.pluginId));
    details += tr("Version: %1\n").arg(safeText(info.version));
    details += tr("Author: %1\n").arg(safeText(info.author));
    details += tr("Enabled: %1\n").arg(info.enabled ? tr("Yes") : tr("No"));
    details += tr("Active: %1\n").arg(info.active ? tr("Yes") : tr("No"));
    details += tr("Source: %1\n").arg(safeText(info.sourcePath));
    details += tr("Capabilities: %1\n").arg(info.capabilities.join(", "));
    if (!info.loadError.isEmpty())
        details += tr("Last Load Status: %1\n").arg(info.loadError);
    details += "\n";
    details += tr("Description:\n%1").arg(safeText(info.description));
    m_details->setPlainText(details);
}

void PluginManagerDialog::onItemChanged(QTableWidgetItem *item)
{
    if (m_updatingTable || !m_pluginManager || !item)
        return;
    if (item->column() != ColEnabled)
        return;

    const QString pluginId = item->data(Qt::UserRole).toString();
    if (pluginId.isEmpty())
        return;
    const bool enabled = (item->checkState() == Qt::Checked);
    m_pluginManager->setPluginEnabled(pluginId, enabled);
}

void PluginManagerDialog::rebuildTable()
{
    if (!m_pluginManager)
    {
        m_summaryLabel->setText(tr("Plugin manager is not available."));
        m_table->setRowCount(0);
        m_details->clear();
        if (m_reloadSelectedButton)
            m_reloadSelectedButton->setEnabled(false);
        return;
    }

    m_infos = m_pluginManager->pluginInfos();
    const QStringList disabled = m_pluginManager->disabledPluginIds();
    QSet<QString> knownIds;
    for (const PluginManager::PluginInfo &info : m_infos)
        knownIds.insert(info.pluginId);
    for (const QString &id : disabled)
    {
        if (knownIds.contains(id))
            continue;
        PluginManager::PluginInfo missing;
        missing.pluginId = id;
        missing.displayName = id;
        missing.enabled = false;
        missing.active = false;
        missing.loadError = tr("Disabled by user (plugin file not currently discovered).");
        m_infos.append(missing);
    }

    int activeCount = 0;
    for (const PluginManager::PluginInfo &info : m_infos)
    {
        if (info.active)
            ++activeCount;
    }

    m_summaryLabel->setText(tr("Plugins: %1 total, %2 active. Toggle Enabled and click Reload Plugins to apply.")
                                .arg(m_infos.size())
                                .arg(activeCount));

    m_updatingTable = true;
    m_table->clearContents();
    m_table->setRowCount(m_infos.size());
    for (int row = 0; row < m_infos.size(); ++row)
    {
        const PluginManager::PluginInfo &info = m_infos[row];

        QTableWidgetItem *enabledItem = new QTableWidgetItem;
        enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        enabledItem->setCheckState(info.enabled ? Qt::Checked : Qt::Unchecked);
        enabledItem->setData(Qt::UserRole, info.pluginId);
        m_table->setItem(row, ColEnabled, enabledItem);

        QTableWidgetItem *activeItem = new QTableWidgetItem(info.active ? tr("Loaded") : tr("Inactive"));
        activeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColActive, activeItem);

        QTableWidgetItem *nameItem = new QTableWidgetItem(safeText(info.displayName));
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColName, nameItem);

        QTableWidgetItem *idItem = new QTableWidgetItem(safeText(info.pluginId));
        idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColId, idItem);

        QTableWidgetItem *versionItem = new QTableWidgetItem(safeText(info.version));
        versionItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColVersion, versionItem);

        QTableWidgetItem *authorItem = new QTableWidgetItem(safeText(info.author));
        authorItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(row, ColAuthor, authorItem);
    }
    m_updatingTable = false;

    if (!m_infos.isEmpty())
        m_table->selectRow(0);
    else
    {
        m_details->clear();
        if (m_reloadSelectedButton)
            m_reloadSelectedButton->setEnabled(false);
    }
}

QString PluginManagerDialog::safeText(const QString &value) const
{
    if (value.isEmpty())
        return tr("(empty)");
    return value;
}

QString PluginManagerDialog::selectedPluginId() const
{
    const int row = m_table ? m_table->currentRow() : -1;
    if (row < 0 || row >= m_infos.size())
        return QString();
    return m_infos[row].pluginId;
}
