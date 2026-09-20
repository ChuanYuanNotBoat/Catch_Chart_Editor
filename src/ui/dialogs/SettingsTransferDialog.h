#pragma once

#include <QDialog>

class SettingsTransferDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsTransferDialog(QWidget *parent = nullptr);

    bool settingsImported() const;

private:
    void copySettingsToClipboard();
    void exportSettingsToFile();
    void importSettingsFromClipboard();
    void importSettingsFromFile();
    void importSettingsText(const QString &text, const QString &sourceDescription);

    bool m_settingsImported = false;
};
