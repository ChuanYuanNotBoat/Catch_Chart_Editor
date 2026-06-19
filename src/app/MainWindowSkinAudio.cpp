#include "MainWindow.h"
#include "MainWindowPrivate.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/CustomWidgets/RealtimePreviewWidget.h"
#include "file/SkinIO.h"
#include "model/Skin.h"
#include "utils/Settings.h"
#include "utils/Logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QActionGroup>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QSet>
#include <QDateTime>
#include <algorithm>

namespace
{
    QStringList skinBaseDirs()
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        QStringList candidates;
        candidates << (appDir + "/skins")
                   << (appDir + "/resources/default_skin");

        QStringList result;
        for (const QString &dir : candidates)
        {
            if (QDir(dir).exists() && !result.contains(dir))
                result.append(dir);
        }
        return result;
    }

    QString resolveSkinPathByName(const QString &skinName)
    {
        for (const QString &baseDir : skinBaseDirs())
        {
            const QString fullPath = baseDir + "/" + skinName;
            if (QDir(fullPath).exists())
                return fullPath;
        }
        return QString();
    }

    struct CachedSkinEntry
    {
        QString name;
        QString path;
        QString displayName;
    };

    QString skinDirsFingerprint()
    {
        QStringList parts;
        const QStringList dirs = skinBaseDirs();
        for (const QString &dir : dirs)
        {
            const QFileInfo fi(dir);
            parts.append(fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath());
            parts.append(QString::number(fi.lastModified().toMSecsSinceEpoch()));
        }
        return parts.join("||");
    }

    QVector<CachedSkinEntry> querySkinEntries(bool *cacheHit = nullptr)
    {
        static bool initialized = false;
        static QString fingerprint;
        static QVector<CachedSkinEntry> cache;

        const QString newFingerprint = skinDirsFingerprint();
        if (initialized && newFingerprint == fingerprint)
        {
            if (cacheHit)
                *cacheHit = true;
            return cache;
        }

        QVector<CachedSkinEntry> entries;
        QSet<QString> seenNames;
        for (const QString &baseDir : skinBaseDirs())
        {
            for (const QString &skinName : SkinIO::getSkinList(baseDir))
            {
                if (seenNames.contains(skinName))
                    continue;
                seenNames.insert(skinName);

                const QString path = baseDir + "/" + skinName;
                entries.append({skinName, path, SkinIO::getSkinDisplayName(path)});
            }
        }

        cache = entries;
        fingerprint = newFingerprint;
        initialized = true;
        if (cacheHit)
            *cacheHit = false;
        return cache;
    }

    QStringList noteSoundBaseDirs()
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        QStringList candidates;
        candidates << (appDir + "/note_sounds")
                   << (appDir + "/resources/note_sounds");

        QStringList result;
        for (const QString &dir : candidates)
        {
            if (QDir(dir).exists() && !result.contains(dir))
                result.append(dir);
        }
        return result;
    }
} // namespace

void MainWindow::changeNoteSound(const QString &soundPath)
{
    Settings::instance().setNoteSoundPath(soundPath);
    if (d->canvas)
    {
        d->canvas->setNoteSoundFile(soundPath);
        d->canvas->setNoteSoundEnabled(!soundPath.isEmpty());
    }

    if (soundPath.isEmpty())
        statusBar()->showMessage(tr("Note sound disabled."), 2000);
    else
        statusBar()->showMessage(tr("Note sound set: %1").arg(QFileInfo(soundPath).fileName()), 2000);
}

void MainWindow::populateNoteSoundMenu()
{
    if (!d->noteSoundMenu)
        return;

    d->noteSoundMenu->clear();
    QActionGroup *group = new QActionGroup(d->noteSoundMenu);
    group->setExclusive(true);

    const QString currentPath = Settings::instance().noteSoundPath();
    bool hasCurrentPathInMenu = currentPath.isEmpty();

    QAction *noneAction = d->noteSoundMenu->addAction(tr("None (No Note Sound)"));
    noneAction->setCheckable(true);
    noneAction->setActionGroup(group);
    noneAction->setChecked(currentPath.isEmpty());
    connect(noneAction, &QAction::triggered, this, [this]()
            { changeNoteSound(QString()); });

    d->noteSoundMenu->addSeparator();

    QStringList filters;
    filters << "*.wav" << "*.ogg" << "*.mp3" << "*.flac" << "*.m4a";
    QVector<QFileInfo> soundEntries;
    QSet<QString> seenCanonical;
    for (const QString &baseDir : noteSoundBaseDirs())
    {
        QDir dir(baseDir);
        const QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &fi : files)
        {
            const QString canonical = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
            if (seenCanonical.contains(canonical))
                continue;
            seenCanonical.insert(canonical);
            soundEntries.append(fi);
        }
    }

    if (soundEntries.isEmpty())
    {
        QAction *emptyAction = d->noteSoundMenu->addAction(tr("No sound files found"));
        emptyAction->setEnabled(false);
        return;
    }

    std::sort(soundEntries.begin(), soundEntries.end(), [](const QFileInfo &a, const QFileInfo &b)
              { return a.fileName().toLower() < b.fileName().toLower(); });

    for (const QFileInfo &fi : soundEntries)
    {
        const QString absPath = fi.absoluteFilePath();
        if (!currentPath.isEmpty() && QFileInfo(currentPath).absoluteFilePath() == absPath)
            hasCurrentPathInMenu = true;
        QAction *act = d->noteSoundMenu->addAction(fi.fileName());
        act->setCheckable(true);
        act->setActionGroup(group);
        act->setChecked(!currentPath.isEmpty() && QFileInfo(currentPath).absoluteFilePath() == absPath);
        connect(act, &QAction::triggered, this, [this, absPath]()
                { changeNoteSound(absPath); });
    }

    if (!hasCurrentPathInMenu)
        noneAction->setChecked(true);
}
void MainWindow::populateSkinMenu()
{
    if (!d->skinMenu)
        return;

    d->skinMenu->clear();
    bool cacheHit = false;
    const QVector<CachedSkinEntry> entries = querySkinEntries(&cacheHit);

    if (entries.isEmpty())
    {
        d->skinMenu->addAction(tr("No skins found"))->setEnabled(false);
        Logger::warn("No skin directories found");
        return;
    }

    QString currentSkin = Settings::instance().currentSkin();
    for (const CachedSkinEntry &entry : entries)
    {
        QAction *action = d->skinMenu->addAction(entry.displayName);
        action->setData(entry.name);
        action->setCheckable(true);
        if (entry.name == currentSkin)
        {
            action->setChecked(true);
        }
        connect(action, &QAction::triggered, this, [this, entry]()
                { changeSkin(entry.name); });
    }
    Logger::debug(QString("Populated skin menu with %1 skins (%2)").arg(entries.size()).arg(cacheHit ? "cache hit" : "cache miss"));
}

void MainWindow::changeSkin(const QString &skinName)
{
    Logger::info(QString("Changing skin to %1").arg(skinName));

    QString skinPath = resolveSkinPathByName(skinName);
    if (skinPath.isEmpty())
    {
        Logger::error(QString("Skin path not found for %1").arg(skinName));
        QMessageBox::warning(this, tr("Skin Error"), tr("Failed to locate skin: %1").arg(skinName));
        populateSkinMenu();
        return;
    }

    Skin *newSkin = new Skin();
    if (SkinIO::loadSkin(skinPath, *newSkin))
    {
        if (d->skin)
            delete d->skin;
        d->skin = newSkin;
        Settings::instance().setCurrentSkin(skinName);
        d->canvas->setSkin(d->skin);
        d->canvas->update();
        Logger::info(QString("Skin changed to %1").arg(skinName));
    }
    else
    {
        Logger::error(QString("Failed to load skin %1").arg(skinName));
        delete newSkin;
        QMessageBox::warning(this, tr("Skin Error"), tr("Failed to load skin: %1").arg(skinName));
    }
    populateSkinMenu();
}

void MainWindow::setSkin(Skin *skin)
{
    if (d->skin == skin)
        return;
    if (d->skin)
        delete d->skin;
    d->skin = skin;
    if (d->canvas)
        d->canvas->setSkin(skin);
    Logger::debug("Skin set externally");
}
