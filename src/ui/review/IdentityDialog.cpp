#include "IdentityDialog.h"

#include "identity/LocalIdentityStore.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace CceReviewUi
{
IdentityDialog::IdentityDialog(CceIdentity::LocalIdentityStore *store, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setObjectName(QStringLiteral("reviewIdentityDialog"));
    setWindowTitle(tr("Local identities"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    m_currentIdentity = new QLabel(this);
    m_currentIdentity->setObjectName(QStringLiteral("reviewCurrentIdentity"));
    layout->addWidget(m_currentIdentity);

    m_identityList = new QListWidget(this);
    m_identityList->setObjectName(QStringLiteral("reviewIdentityList"));
    layout->addWidget(m_identityList);

    auto *actions = new QHBoxLayout;
    auto *createButton = new QPushButton(tr("Create"), this);
    createButton->setObjectName(QStringLiteral("reviewIdentityCreateButton"));
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_renameButton->setObjectName(QStringLiteral("reviewIdentityRenameButton"));
    m_removeButton = new QPushButton(tr("Delete"), this);
    m_removeButton->setObjectName(QStringLiteral("reviewIdentityDeleteButton"));
    m_activateButton = new QPushButton(tr("Set as current"), this);
    m_activateButton->setObjectName(QStringLiteral("reviewIdentityActivateButton"));
    actions->addWidget(createButton);
    actions->addWidget(m_renameButton);
    actions->addWidget(m_removeButton);
    actions->addWidget(m_activateButton);
    layout->addLayout(actions);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("reviewIdentityError"));
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b42318"));
    m_errorLabel->setWordWrap(true);
    layout->addWidget(m_errorLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("reviewIdentityDialogButtons"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    connect(createButton, &QPushButton::clicked, this, &IdentityDialog::createIdentity);
    connect(m_renameButton, &QPushButton::clicked, this, &IdentityDialog::renameSelected);
    connect(m_removeButton, &QPushButton::clicked, this, &IdentityDialog::removeSelected);
    connect(m_activateButton, &QPushButton::clicked, this, &IdentityDialog::setSelectedAsActive);
    connect(m_identityList, &QListWidget::currentRowChanged, this, [this] {
        const QString selectedId = selectedIdentityId();
        const bool hasSelection = !selectedId.isEmpty();
        m_renameButton->setEnabled(hasSelection);
        m_removeButton->setEnabled(hasSelection);
        m_activateButton->setEnabled(hasSelection && m_store && selectedId != m_store->activeIdentity().authorId);
    });
    if (m_store)
        connect(m_store, &CceIdentity::LocalIdentityStore::changed, this, &IdentityDialog::refresh);
    refresh();
}

QString IdentityDialog::selectedIdentityId() const
{
    const QListWidgetItem *item = m_identityList ? m_identityList->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void IdentityDialog::showError(const QString &error)
{
    if (m_errorLabel)
        m_errorLabel->setText(error);
}

void IdentityDialog::refresh()
{
    if (!m_identityList)
        return;
    const QString selectedId = selectedIdentityId();
    m_identityList->clear();
    if (m_store)
    {
        const auto active = m_store->activeIdentity();
        m_currentIdentity->setText(active.authorId.isEmpty() ? tr("Current identity: none")
                                                             : tr("Current identity: %1").arg(active.authorName));
        for (const auto &identity : m_store->identities())
        {
            auto *item = new QListWidgetItem(identity.authorName, m_identityList);
            item->setData(Qt::UserRole, identity.authorId);
            if (identity.authorId == active.authorId)
                item->setText(tr("%1 (current)").arg(identity.authorName));
            if (identity.authorId == selectedId)
                m_identityList->setCurrentItem(item);
        }
    }
    else
    {
        m_currentIdentity->setText(tr("Identity store is unavailable."));
    }
    if (m_identityList->currentRow() < 0 && m_identityList->count() > 0)
        m_identityList->setCurrentRow(0);
    const bool hasSelection = !selectedIdentityId().isEmpty();
    m_renameButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);
    m_activateButton->setEnabled(hasSelection && m_store && selectedIdentityId() != m_store->activeIdentity().authorId);
}

void IdentityDialog::createIdentity()
{
    if (!m_store)
        return;
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, tr("Create identity"), tr("Name:"), QLineEdit::Normal, QString(), &accepted);
    if (!accepted)
        return;
    QString error;
    if (!m_store->createIdentity(name, &error))
        showError(error);
    else
        showError(QString());
}

void IdentityDialog::renameSelected()
{
    if (!m_store)
        return;
    const QString id = selectedIdentityId();
    for (const auto &identity : m_store->identities())
    {
        if (identity.authorId != id)
            continue;
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Rename identity"), tr("Name:"), QLineEdit::Normal,
                                                   identity.authorName, &accepted);
        if (!accepted)
            return;
        QString error;
        if (!m_store->renameIdentity(id, name, &error))
            showError(error);
        else
            showError(QString());
        return;
    }
}

void IdentityDialog::removeSelected()
{
    if (!m_store)
        return;
    const QString id = selectedIdentityId();
    QString name;
    for (const auto &identity : m_store->identities())
    {
        if (identity.authorId == id)
        {
            name = identity.authorName;
            break;
        }
    }
    if (name.isEmpty())
        return;
    if (QMessageBox::question(
            this, tr("Delete identity"),
            tr("Delete the local identity '%1'? Historical signatures will remain unchanged.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    QString error;
    if (!m_store->removeIdentity(id, &error))
        showError(error);
    else
        showError(QString());
}

void IdentityDialog::setSelectedAsActive()
{
    if (!m_store)
        return;
    QString error;
    if (!m_store->setActiveIdentity(selectedIdentityId(), &error))
        showError(error);
    else
        showError(QString());
}
} // namespace CceReviewUi
