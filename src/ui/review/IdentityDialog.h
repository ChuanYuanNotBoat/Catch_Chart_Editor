#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

namespace CceIdentity
{
class LocalIdentityStore;
}

namespace CceReviewUi
{
class IdentityDialog : public QDialog
{
    Q_OBJECT

public:
    explicit IdentityDialog(CceIdentity::LocalIdentityStore *store, QWidget *parent = nullptr);

private slots:
    void refresh();
    void createIdentity();
    void renameSelected();
    void removeSelected();
    void setSelectedAsActive();

private:
    QString selectedIdentityId() const;
    void showError(const QString &error);

    CceIdentity::LocalIdentityStore *m_store = nullptr;
    QListWidget *m_identityList = nullptr;
    QLabel *m_currentIdentity = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_activateButton = nullptr;
};
} // namespace CceReviewUi
