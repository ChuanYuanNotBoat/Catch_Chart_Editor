#include "SettingsTransferDialog.h"

#include "utils/NativeWindowTheme.h"
#include "utils/Settings.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace
{
constexpr qint64 kMaximumTransferFileBytes = 8 * 1024 * 1024;

QString initialTransferDirectory()
{
    const QString lastOpenPath = Settings::instance().lastOpenPath();
    if (!lastOpenPath.isEmpty() && QDir(lastOpenPath).exists())
        return lastOpenPath;

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}
}

SettingsTransferDialog::SettingsTransferDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings Backup and Transfer"));
    setMinimumWidth(560);
    setStyleSheet(NativeWindowTheme::dialogStyleSheet(Settings::instance().backgroundColor()));

    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
        tr("Export all saved editor settings as a portable text bundle, or replace the current settings from one. Chart files are never included."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *formatHint = new QLabel(
        tr("The bundle uses fixed Malody Catch Editor markers around a Base64 payload. It may contain local paths and window layout data."),
        this);
    formatHint->setWordWrap(true);
    layout->addWidget(formatHint);

    auto *exportGroup = new QGroupBox(tr("Export"), this);
    auto *exportLayout = new QHBoxLayout(exportGroup);
    auto *copyButton = new QPushButton(tr("Copy to Clipboard"), exportGroup);
    auto *saveButton = new QPushButton(tr("Save as TXT..."), exportGroup);
    exportLayout->addWidget(copyButton);
    exportLayout->addWidget(saveButton);
    exportLayout->addStretch();
    layout->addWidget(exportGroup);

    auto *importGroup = new QGroupBox(tr("Import"), this);
    auto *importLayout = new QHBoxLayout(importGroup);
    auto *clipboardButton = new QPushButton(tr("Import from Clipboard"), importGroup);
    auto *fileButton = new QPushButton(tr("Import from TXT..."), importGroup);
    importLayout->addWidget(clipboardButton);
    importLayout->addWidget(fileButton);
    importLayout->addStretch();
    layout->addWidget(importGroup);

    auto *warning = new QLabel(
        tr("Import validates the complete bundle before replacing any settings. Restart the editor afterward so every imported setting takes effect."),
        this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(copyButton, &QPushButton::clicked, this,
            &SettingsTransferDialog::copySettingsToClipboard);
    connect(saveButton, &QPushButton::clicked, this,
            &SettingsTransferDialog::exportSettingsToFile);
    connect(clipboardButton, &QPushButton::clicked, this,
            &SettingsTransferDialog::importSettingsFromClipboard);
    connect(fileButton, &QPushButton::clicked, this,
            &SettingsTransferDialog::importSettingsFromFile);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool SettingsTransferDialog::settingsImported() const
{
    return m_settingsImported;
}

void SettingsTransferDialog::copySettingsToClipboard()
{
    QString error;
    const QString text = Settings::instance().exportTransferText(&error);
    if (text.isEmpty())
    {
        QMessageBox::critical(this, tr("Export Settings"), error);
        return;
    }

    QApplication::clipboard()->setText(text);
    QMessageBox::information(this, tr("Export Settings"),
                             tr("Settings copied to the clipboard."));
}

void SettingsTransferDialog::exportSettingsToFile()
{
    QString error;
    const QString text = Settings::instance().exportTransferText(&error);
    if (text.isEmpty())
    {
        QMessageBox::critical(this, tr("Export Settings"), error);
        return;
    }

    const QString suggestedName = QStringLiteral("MCCE-settings-%1.txt")
                                      .arg(QDateTime::currentDateTime().toString(
                                          QStringLiteral("yyyyMMdd-HHmmss")));
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export Settings"), QDir(initialTransferDirectory()).filePath(suggestedName),
        tr("Text Files (*.txt);;All Files (*.*)"));
    if (fileName.isEmpty())
        return;
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".txt");

    QSaveFile file(fileName);
    const QByteArray bytes = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        QMessageBox::critical(this, tr("Export Settings"),
                              tr("Could not write the settings file: %1").arg(file.errorString()));
        return;
    }

    QMessageBox::information(this, tr("Export Settings"),
                             tr("Settings exported successfully."));
}

void SettingsTransferDialog::importSettingsFromClipboard()
{
    const QString text = QApplication::clipboard()->text();
    if (text.trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Import Settings"),
                             tr("The clipboard does not contain settings text."));
        return;
    }
    importSettingsText(text, tr("the clipboard"));
}

void SettingsTransferDialog::importSettingsFromFile()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("Import Settings"), initialTransferDirectory(),
        tr("Text Files (*.txt);;All Files (*.*)"));
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, tr("Import Settings"),
                              tr("Could not read the settings file: %1").arg(file.errorString()));
        return;
    }
    if (file.size() > kMaximumTransferFileBytes)
    {
        QMessageBox::critical(this, tr("Import Settings"),
                              tr("The settings file is too large."));
        return;
    }

    importSettingsText(QString::fromUtf8(file.readAll()), QFileInfo(fileName).fileName());
}

void SettingsTransferDialog::importSettingsText(const QString &text,
                                                const QString &sourceDescription)
{
    const auto answer = QMessageBox::question(
        this, tr("Import Settings"),
        tr("Import settings from %1?\n\nThis replaces all current editor settings. Open chart data is not affected.")
            .arg(sourceDescription),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    int importedCount = 0;
    if (!Settings::instance().importTransferText(text, &error, &importedCount))
    {
        QMessageBox::critical(this, tr("Import Settings"), error);
        return;
    }

    m_settingsImported = true;
    QMessageBox::information(
        this, tr("Import Settings"),
        tr("Imported %1 settings. Restart the editor to apply every imported setting.")
            .arg(importedCount));
    accept();
}
