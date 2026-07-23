// MainWindow.cpp - Main window implementation.
#include "MainWindow.h"
#include "MainWindowPrivate.h"
#include "app/Application.h"
#include "plugin/PluginManager.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "ui/CustomWidgets/RealtimePreviewWidget.h"
#include "ui/DensityCurve.h"
#include "ui/NoteEditPanel.h"
#include "ui/BPMTimePanel.h"
#include "ui/MetaEditPanel.h"
#include "ui/LeftPanel.h"
#include "ui/dialogs/LogSettingsDialog.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "audio/AudioPlayer.h"
#include "utils/Settings.h"
#include "utils/Translator.h"
#include "utils/DiagnosticCollector.h"
#include "file/SkinIO.h"
#include "file/ProjectIO.h"
#include "file/ChartIO.h"
#include "file/ChartFileSystem.h"
#include "model/Skin.h"
#include "utils/Logger.h"
#include "utils/MathUtils.h"
#include <QMenuBar>
#include <QToolBar>
#include <QSplitter>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QSlider>
#include <QSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QColorDialog>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QRadioButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QDesktopServices>
#include <QUrl>
#include <QSysInfo>
#include <QGroupBox>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QInputDialog>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QProgressDialog>
#include <QThread>
#include <QTreeWidget>
#include <QSet>
#include <QTimer>
#include <QTextBrowser>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStringConverter>
#include <QDateTime>
#include <QElapsedTimer>
#include <QUuid>
#include <QDirIterator>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace
{
    PluginManager *activePluginManager()
    {
        auto *app = qobject_cast<Application *>(QCoreApplication::instance());
        return app ? app->pluginManager() : nullptr;
    }

    QColor sidebarTextColorFor(const QColor &bg)
    {
        const double r = bg.redF();
        const double g = bg.greenF();
        const double b = bg.blueF();
        const double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        return (luminance >= 0.5) ? QColor(20, 20, 20) : QColor(245, 245, 245);
    }

    bool isModifierKey(int key)
    {
        return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta;
    }

    int modifierCount(Qt::KeyboardModifiers mods)
    {
        int count = 0;
        if (mods.testFlag(Qt::ControlModifier))
            ++count;
        if (mods.testFlag(Qt::AltModifier))
            ++count;
        if (mods.testFlag(Qt::ShiftModifier))
            ++count;
        if (mods.testFlag(Qt::MetaModifier))
            ++count;
        return count;
    }

    QString modifiersPreviewText(Qt::KeyboardModifiers mods)
    {
        QStringList parts;
        if (mods.testFlag(Qt::ControlModifier))
            parts << QObject::tr("Ctrl");
        if (mods.testFlag(Qt::AltModifier))
            parts << QObject::tr("Alt");
        if (mods.testFlag(Qt::ShiftModifier))
            parts << QObject::tr("Shift");
        if (mods.testFlag(Qt::MetaModifier))
            parts << QObject::tr("Meta");

        if (parts.isEmpty())
            return QString();
        return parts.join("+") + "+...";
    }

    bool extractSemver(const QString &text, int &major, int &minor, int &patch)
    {
        const QRegularExpression re("(\\d+)\\.(\\d+)\\.(\\d+)");
        const QRegularExpressionMatch match = re.match(text);
        if (!match.hasMatch())
            return false;

        bool ok1 = false;
        bool ok2 = false;
        bool ok3 = false;
        major = match.captured(1).toInt(&ok1);
        minor = match.captured(2).toInt(&ok2);
        patch = match.captured(3).toInt(&ok3);
        return ok1 && ok2 && ok3;
    }

    QString firstLocalMczPathFromMimeData(const QMimeData *mimeData)
    {
        if (!mimeData || !mimeData->hasUrls())
            return QString();

        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls)
        {
            if (!url.isLocalFile())
                continue;

            const QString path = url.toLocalFile();
            if (QFileInfo(path).suffix().compare(QStringLiteral("mcz"), Qt::CaseInsensitive) == 0)
                return path;
        }
        return QString();
    }

    int compareSemver(const QString &current, const QString &latest)
    {
        int cMaj = 0, cMin = 0, cPat = 0;
        int lMaj = 0, lMin = 0, lPat = 0;
        if (!extractSemver(current, cMaj, cMin, cPat) || !extractSemver(latest, lMaj, lMin, lPat))
            return 0;

        if (cMaj != lMaj)
            return (cMaj < lMaj) ? -1 : 1;
        if (cMin != lMin)
            return (cMin < lMin) ? -1 : 1;
        if (cPat != lPat)
            return (cPat < lPat) ? -1 : 1;
        return 0;
    }

    QString loadUtf8TextFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();

        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        return in.readAll();
    }

    QString loadDocText(const QStringList &candidatePaths, QString *resolvedPath = nullptr)
    {
        for (const QString &path : candidatePaths)
        {
            const QString text = loadUtf8TextFile(path);
            if (!text.trimmed().isEmpty())
            {
                if (resolvedPath)
                    *resolvedPath = path;
                return text;
            }
        }
        if (resolvedPath)
            resolvedPath->clear();
        return QString();
    }

    struct HistorySection
    {
        QString title;
        QStringList lines;
    };

    struct HistoryPrefixGroup
    {
        QString key;
        QString label;
        QList<HistorySection> sections;
    };

    QList<HistorySection> parseHistorySections(const QString &historyText)
    {
        QList<HistorySection> sections;
        HistorySection current;
        const QStringList rawLines = historyText.split('\n');
        for (QString line : rawLines)
        {
            line = line.trimmed();
            if (line.isEmpty())
                continue;

            if (line.startsWith("## "))
            {
                if (!current.title.isEmpty() || !current.lines.isEmpty())
                    sections.push_back(current);
                current = HistorySection{};
                current.title = line.mid(3).trimmed();
                continue;
            }

            if (current.title.isEmpty())
                current.title = QObject::tr("History");
            current.lines.push_back(line);
        }

        if (!current.title.isEmpty() || !current.lines.isEmpty())
            sections.push_back(current);
        return sections;
    }

    QString historyPrefixFromTitle(const QString &title)
    {
        const QString text = title.trimmed();
        if (text.isEmpty())
            return QString();

        const QRegularExpression re("^\\[?([A-Za-z][A-Za-z0-9_-]*)\\]?");
        const QRegularExpressionMatch m = re.match(text);
        if (!m.hasMatch())
            return QString();
        return m.captured(1).trimmed();
    }

    QString normalizedPrefixLabel(const QString &prefix)
    {
        if (prefix.isEmpty())
            return QObject::tr("Other");
        const QString lower = prefix.toLower();
        return lower.left(1).toUpper() + lower.mid(1);
    }

    QString sanitizeFileStem(QString stem)
    {
        stem = stem.trimmed();
        if (stem.isEmpty())
            return QString();

        stem.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        stem.replace(QRegularExpression("\\s+"), " ");
        while (stem.endsWith(' ') || stem.endsWith('.'))
            stem.chop(1);
        return stem.trimmed();
    }

    QList<HistoryPrefixGroup> groupHistorySectionsByPrefix(const QList<HistorySection> &sections)
    {
        QList<HistoryPrefixGroup> groups;
        for (const HistorySection &section : sections)
        {
            const QString rawPrefix = historyPrefixFromTitle(section.title);
            const QString label = normalizedPrefixLabel(rawPrefix);
            const QString key = rawPrefix.isEmpty() ? QString("__other__") : rawPrefix.toCaseFolded();

            int index = -1;
            for (int i = 0; i < groups.size(); ++i)
            {
                if (groups[i].key == key)
                {
                    index = i;
                    break;
                }
            }

            if (index < 0)
            {
                HistoryPrefixGroup group;
                group.key = key;
                group.label = label;
                groups.push_back(group);
                index = groups.size() - 1;
            }

            groups[index].sections.push_back(section);
        }
        return groups;
    }

    void setBrowserContentFromDoc(QTextBrowser *browser,
                                  const QStringList &candidatePaths,
                                  const QString &fallbackMarkdown)
    {
        if (!browser)
            return;

        QString resolvedPath;
        const QString docText = loadDocText(candidatePaths, &resolvedPath);
        if (!docText.isEmpty())
        {
            const QString lower = resolvedPath.toLower();
            if (lower.endsWith(".md") || lower.endsWith(".markdown") || lower.endsWith(".txt"))
                browser->setMarkdown(docText);
            else
                browser->setPlainText(docText);
            return;
        }
        browser->setMarkdown(fallbackMarkdown);
    }

    class ShortcutCaptureEdit final : public QLineEdit
    {
    public:
        explicit ShortcutCaptureEdit(QWidget *parent = nullptr) : QLineEdit(parent)
        {
            // Keep editable so the built-in clear button ('x') remains clickable.
            // We fully control text via key handlers below.
            setReadOnly(false);
            setClearButtonEnabled(true);
            setContextMenuPolicy(Qt::NoContextMenu);
            setDragEnabled(false);
            connect(this, &QLineEdit::textChanged, this, [this](const QString &text)
                    {
            if (text.isEmpty())
                m_sequence = QKeySequence(); });
        }

        QKeySequence keySequence() const
        {
            return m_sequence;
        }

        void setKeySequence(const QKeySequence &seq)
        {
            int k1 = seq.count() > 0 ? seq[0] : 0;
            int k2 = seq.count() > 1 ? seq[1] : 0;
            int k3 = seq.count() > 2 ? seq[2] : 0;
            int k4 = seq.count() > 3 ? seq[3] : 0;
            m_sequence = QKeySequence(k1, k2, k3, k4);
            m_blockedChordAttempt = false;
            refreshText();
        }

    protected:
        void keyPressEvent(QKeyEvent *event) override
        {
            if (!event || event->isAutoRepeat())
                return;
            if (m_blockedChordAttempt)
                return;

            const int key = event->key();
            const Qt::KeyboardModifiers mods = event->modifiers();

            if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) && mods == Qt::NoModifier)
            {
                setKeySequence(QKeySequence());
                return;
            }

            if (isModifierKey(key))
            {
                m_hasModifierPreview = true;
                const QString preview = modifiersPreviewText(mods);
                if (m_sequence.isEmpty())
                    setText(preview);
                else
                    setText(m_sequence.toString(QKeySequence::PortableText) + ", " + preview);
                return;
            }

            const int comboKeyCount = modifierCount(mods) + 1;
            if (comboKeyCount > 2)
            {
                m_blockedChordAttempt = true;
                return;
            }

            appendChord(key | mods);
            m_hasModifierPreview = false;
        }

        void mouseDoubleClickEvent(QMouseEvent *event) override
        {
            // Do not start inline text editing behavior.
            QLineEdit::mouseDoubleClickEvent(event);
            deselect();
        }

        void keyReleaseEvent(QKeyEvent *event) override
        {
            if (!event || event->isAutoRepeat())
                return;

            if (m_blockedChordAttempt && QApplication::keyboardModifiers() == Qt::NoModifier)
            {
                m_blockedChordAttempt = false;
                refreshText();
            }

            if (m_hasModifierPreview && QApplication::keyboardModifiers() == Qt::NoModifier)
            {
                refreshText();
                m_hasModifierPreview = false;
            }
        }

        void focusOutEvent(QFocusEvent *event) override
        {
            QLineEdit::focusOutEvent(event);
            if (m_hasModifierPreview)
            {
                refreshText();
                m_hasModifierPreview = false;
            }
        }

    private:
        void appendChord(int chord)
        {
            if (chord == 0)
                return;

            int keys[4] = {0, 0, 0, 0};
            const int count = qMin(m_sequence.count(), 4);
            for (int i = 0; i < count; ++i)
                keys[i] = m_sequence[i];

            if (count < 4)
                keys[count] = chord;
            else
                keys[3] = chord;

            m_sequence = QKeySequence(keys[0], keys[1], keys[2], keys[3]);
            refreshText();
        }

        void refreshText()
        {
            setText(m_sequence.toString(QKeySequence::PortableText));
        }

        QKeySequence m_sequence;
        bool m_hasModifierPreview = false;
        bool m_blockedChordAttempt = false;
    };

    QString sessionWorkingCopyRootDir()
    {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        return QDir(base).filePath("session_working_copies");
    }

    QString recoveryManifestPath()
    {
        return QDir(sessionWorkingCopyRootDir()).filePath("recovery.json");
    }

    struct RecoverySessionState
    {
        QString sourcePath;
        QString workingPath;
        bool modified = false;
    };

    bool writeRecoveryState(const RecoverySessionState &state)
    {
        if (!QDir().mkpath(sessionWorkingCopyRootDir()))
            return false;

        const QJsonObject obj{
            {"source_path", state.sourcePath},
            {"working_path", state.workingPath},
            {"modified", state.modified},
            {"updated_at_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
        QFile file(recoveryManifestPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;
        const QJsonDocument doc(obj);
        return file.write(doc.toJson(QJsonDocument::Indented)) > 0;
    }

    bool readRecoveryState(RecoverySessionState *state)
    {
        if (!state)
            return false;
        *state = RecoverySessionState{};

        QFile file(recoveryManifestPath());
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return false;

        const QJsonObject obj = doc.object();
        state->sourcePath = obj.value("source_path").toString();
        state->workingPath = obj.value("working_path").toString();
        state->modified = obj.value("modified").toBool(false);
        return !state->workingPath.isEmpty();
    }

    void removeRecoveryState()
    {
        QFile::remove(recoveryManifestPath());
    }

    QString workingSessionDirFromWorkingPath(const QString &workingPath)
    {
        if (workingPath.isEmpty())
            return QString();

        const QString baseRoot = QDir(sessionWorkingCopyRootDir()).absolutePath();
        const QString workingDir = QFileInfo(workingPath).absoluteDir().absolutePath();
        const QString rel = QDir(baseRoot).relativeFilePath(workingDir);
        if (rel.isEmpty() || rel.startsWith(".."))
            return workingDir;

        const QString firstSegment = rel.section('/', 0, 0);
        if (firstSegment.isEmpty() || firstSegment == ".")
            return workingDir;
        return QDir(baseRoot).filePath(firstSegment);
    }

    void removePathRecursively(const QString &path)
    {
        if (path.isEmpty())
            return;

        const QFileInfo fi(path);
        if (!fi.exists() && !fi.isSymLink())
            return;

        if (fi.isDir() && !fi.isSymLink())
        {
            QDir(path).removeRecursively();
            return;
        }

        QFile::remove(path);
    }

    struct CopyProgressState
    {
        std::atomic<qint64> totalFiles{0};
        std::atomic<qint64> copiedFiles{0};
        std::atomic<qint64> totalBytes{0};
        std::atomic<qint64> copiedBytes{0};
        std::atomic<qint64> maxSingleFileMs{0};
        std::atomic<bool> cancelRequested{false};
    };

    bool copyDirectoryRecursively(const QString &sourceDirPath,
                                  const QString &targetDirPath,
                                  QString *errorOut,
                                  CopyProgressState *progress)
    {
        if (errorOut)
            errorOut->clear();

        const QDir sourceDir(sourceDirPath);
        if (!sourceDir.exists())
        {
            if (errorOut)
                *errorOut = QObject::tr("Source directory does not exist:\n%1").arg(sourceDirPath);
            return false;
        }

        if (!QDir().mkpath(targetDirPath))
        {
            if (errorOut)
                *errorOut = QObject::tr("Failed to create working directory:\n%1").arg(targetDirPath);
            return false;
        }

        QVector<QFileInfo> directories;
        QVector<QFileInfo> files;
        qint64 totalBytes = 0;

        QDirIterator scanIt(sourceDirPath,
                            QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
        while (scanIt.hasNext())
        {
            scanIt.next();
            const QFileInfo entry = scanIt.fileInfo();
            if (entry.isDir() && !entry.isSymLink())
            {
                directories.append(entry);
                continue;
            }
            files.append(entry);
            totalBytes += qMax<qint64>(0, entry.size());
        }

        if (progress)
        {
            progress->totalFiles.store(files.size());
            progress->totalBytes.store(totalBytes);
            progress->copiedFiles.store(0);
            progress->copiedBytes.store(0);
            progress->maxSingleFileMs.store(0);
        }

        for (const QFileInfo &entry : directories)
        {
            if (progress && progress->cancelRequested.load())
            {
                if (errorOut)
                    *errorOut = QObject::tr("Copy cancelled by user.");
                return false;
            }

            const QString relPath = sourceDir.relativeFilePath(entry.absoluteFilePath());
            const QString targetPath = QDir(targetDirPath).filePath(relPath);
            if (!QDir().mkpath(targetPath))
            {
                if (errorOut)
                    *errorOut = QObject::tr("Failed to create working subdirectory:\n%1").arg(targetPath);
                return false;
            }
        }

        for (const QFileInfo &entry : files)
        {
            if (progress && progress->cancelRequested.load())
            {
                if (errorOut)
                    *errorOut = QObject::tr("Copy cancelled by user.");
                return false;
            }

            const QString relPath = sourceDir.relativeFilePath(entry.absoluteFilePath());
            const QString targetPath = QDir(targetDirPath).filePath(relPath);
            if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()))
            {
                if (errorOut)
                    *errorOut = QObject::tr("Failed to prepare working file path:\n%1").arg(targetPath);
                return false;
            }

            QElapsedTimer fileTimer;
            fileTimer.start();
            QFile::remove(targetPath);
            if (!QFile::copy(entry.absoluteFilePath(), targetPath))
            {
                if (errorOut)
                    *errorOut = QObject::tr("Failed to copy required file:\n%1").arg(entry.absoluteFilePath());
                return false;
            }

            if (progress)
            {
                progress->copiedFiles.fetch_add(1);
                progress->copiedBytes.fetch_add(qMax<qint64>(0, entry.size()));
                const qint64 fileMs = fileTimer.elapsed();
                qint64 expected = progress->maxSingleFileMs.load();
                while (fileMs > expected && !progress->maxSingleFileMs.compare_exchange_weak(expected, fileMs))
                {
                }
            }
        }

        return true;
    }

    QString chartSongTitleFromFile(const QString &chartPath)
    {
        QFile file(chartPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return QString();

        const QJsonObject root = doc.object();
        const QJsonObject meta = root.value("meta").toObject();
        const QJsonObject song = meta.value("song").toObject();

        QString title = song.value("title").toString().trimmed();
        if (title.isEmpty())
            title = meta.value("title").toString().trimmed();
        return title;
    }

    QStringList collectReferencedResources(const QString &chartPath)
    {
        QStringList resources;
        QFile file(chartPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return resources;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return resources;

        const auto appendIfPresent = [&resources](const QJsonObject &obj, const char *key)
        {
            const QString value = obj.value(QString::fromUtf8(key)).toString().trimmed();
            if (!value.isEmpty())
                resources.append(value);
        };

        const QJsonObject root = doc.object();
        const QJsonObject meta = root.value("meta").toObject();
        appendIfPresent(meta, "background");
        appendIfPresent(meta, "audio");

        const QJsonArray notes = root.value("note").toArray();
        for (const QJsonValue &v : notes)
        {
            if (!v.isObject())
                continue;
            appendIfPresent(v.toObject(), "sound");
        }

        resources.removeDuplicates();
        return resources;
    }

    bool isPathInsideRoot(const QString &rootPath, const QString &targetPath)
    {
        const QString root = QDir::cleanPath(rootPath);
        const QString target = QDir::cleanPath(targetPath);
        const QString prefix = root.endsWith('/') ? root : (root + '/');
        return target == root || target.startsWith(prefix, Qt::CaseInsensitive);
    }

    void copyReferencedExternalResources(const QString &sourceChartPath, const QString &workingChartPath)
    {
        if (sourceChartPath.isEmpty() || workingChartPath.isEmpty())
            return;

        const QString sourceChartDir = QFileInfo(sourceChartPath).absoluteDir().absolutePath();
        const QString workingChartDir = QFileInfo(workingChartPath).absoluteDir().absolutePath();
        const QString sessionRoot = workingSessionDirFromWorkingPath(workingChartPath);
        const QStringList resources = collectReferencedResources(sourceChartPath);
        for (const QString &resource : resources)
        {
            if (resource.isEmpty() || QDir::isAbsolutePath(resource))
                continue;

            const QString sourceAbs = QDir::cleanPath(QDir(sourceChartDir).absoluteFilePath(resource));
            const QFileInfo sourceFi(sourceAbs);
            if (!sourceFi.exists())
                continue;

            const QString targetAbs = QDir::cleanPath(QDir(workingChartDir).absoluteFilePath(resource));
            if (!isPathInsideRoot(sessionRoot, targetAbs))
            {
                Logger::warn(QString("Skip copying referenced resource outside working session root: %1").arg(resource));
                continue;
            }

            if (sourceFi.isDir() && !sourceFi.isSymLink())
            {
                QString copyError;
                if (!copyDirectoryRecursively(sourceAbs, targetAbs, &copyError, nullptr))
                {
                    Logger::warn(QString("Failed to copy referenced resource directory: %1 (%2)").arg(resource, copyError));
                }
                continue;
            }

            QDir().mkpath(QFileInfo(targetAbs).absolutePath());
            QFile::remove(targetAbs);
            if (!QFile::copy(sourceAbs, targetAbs))
            {
                Logger::warn(QString("Failed to copy referenced resource file: %1").arg(resource));
            }
        }
    }

    void syncSidecarDirectoryForChart(const QString &sourceChartPath, const QString &targetChartPath)
    {
        if (sourceChartPath.isEmpty() || targetChartPath.isEmpty())
            return;

        const QString sourceDir = QFileInfo(sourceChartPath).absoluteDir().absolutePath();
        const QString targetDir = QFileInfo(targetChartPath).absoluteDir().absolutePath();
        if (sourceDir.isEmpty() || targetDir.isEmpty())
            return;
        if (QDir::cleanPath(sourceDir) == QDir::cleanPath(targetDir))
            return;

        const QString sourceSidecar = QDir(sourceDir).filePath(".mcce-plugin");
        if (!QDir(sourceSidecar).exists())
            return;

        const QString targetSidecar = QDir(targetDir).filePath(".mcce-plugin");
        QString copyError;
        if (!copyDirectoryRecursively(sourceSidecar, targetSidecar, &copyError, nullptr))
        {
            Logger::warn(QString("Failed to sync sidecar directory: %1 -> %2 (%3)")
                             .arg(sourceSidecar, targetSidecar, copyError));
        }
    }

    void syncAllKnownSidecars(const QString &sourceChartPath, const QString &targetChartPath)
    {
        if (sourceChartPath.isEmpty() || targetChartPath.isEmpty())
            return;

        Logger::debug(QString("syncAllKnownSidecars - Syncing known sidecars from %1 to %2")
                          .arg(sourceChartPath, targetChartPath));

        // 从 ChartFileSystem 注册表获取所有已注册的 sidecar 扩展名
        QVector<ChartFileSystem::RegisteredTypeInfo> registeredTypes = ChartFileSystem::ChartFileSystemRegistry::registeredFileTypes();
        QStringList knownSidecarExtensions;
        for (const ChartFileSystem::RegisteredTypeInfo &type : registeredTypes)
        {
            // 只处理 sidecar 文件（包含 .json 的扩展名）
            if (type.extension.contains(".json"))
            {
                knownSidecarExtensions << type.extension;
            }
        }

        QFileInfo sourceFi(sourceChartPath);
        QFileInfo targetFi(targetChartPath);
        QString sourceDir = sourceFi.absoluteDir().absolutePath();
        QString targetDir = targetFi.absoluteDir().absolutePath();
        const QString sourceChartStem = ChartFileSystem::ChartFileSystemRegistry::chartIdentifierForPath(sourceChartPath);
        const QString targetChartStem = ChartFileSystem::ChartFileSystemRegistry::chartIdentifierForPath(targetChartPath);

        QString sourceSidecarDir = QDir(sourceDir).filePath(".mcce-plugin");
        QString targetSidecarDir = QDir(targetDir).filePath(".mcce-plugin");

        // 确保目标 sidecar 目录存在
        if (!QDir(targetSidecarDir).exists())
        {
            QDir(targetSidecarDir).mkpath(".");
        }

        // 遍历已知 sidecar 文件并复制
        for (const QString &ext : knownSidecarExtensions)
        {
            QString sourceFile = QDir(sourceSidecarDir).filePath(sourceChartStem + "." + ext);
            QString targetFile = QDir(targetSidecarDir).filePath(targetChartStem + "." + ext);

            if (QFile::exists(sourceFile))
            {
                QFile::remove(targetFile);
                if (QFile::copy(sourceFile, targetFile))
                {
                    Logger::debug(QString("syncAllKnownSidecars - Copied sidecar: %1").arg(ext));
                }
                else
                {
                    Logger::warn(QString("syncAllKnownSidecars - Failed to copy sidecar: %1").arg(ext));
                }
            }
        }

        Logger::debug("syncAllKnownSidecars - Known sidecars synced");
    }

    void syncReferencedResourcesForSavedChart(const QString &workingChartPath, const QString &savedChartPath)
    {
        if (workingChartPath.isEmpty() || savedChartPath.isEmpty())
            return;
        if (QDir::cleanPath(workingChartPath) == QDir::cleanPath(savedChartPath))
            return;

        copyReferencedExternalResources(workingChartPath, savedChartPath);
    }

    void cleanupSessionWorkingCopies(const QString &preserveWorkingPath)
    {
        QDir dir(sessionWorkingCopyRootDir());
        if (!dir.exists())
            return;

        const QString preservedDir = workingSessionDirFromWorkingPath(preserveWorkingPath);
        const QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        for (const QFileInfo &fi : entries)
        {
            if (fi.fileName().compare("recovery.json", Qt::CaseInsensitive) == 0)
                continue;
            const QString abs = fi.absoluteFilePath();
            if (!preservedDir.isEmpty() && abs == preservedDir)
                continue;
            removePathRecursively(abs);
        }
    }

    QString buildWorkingCopyPath(const QString &sourcePath, QString *workingSessionDirOut)
    {
        if (workingSessionDirOut)
            workingSessionDirOut->clear();
        const QString fileName = QFileInfo(sourcePath).fileName();
        const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
        const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString sourceDirName = QFileInfo(sourcePath).absoluteDir().dirName();
        const QString sessionName = QString("%1_%2_%3")
                                        .arg(stamp,
                                             uid,
                                             sourceDirName.isEmpty() ? QStringLiteral("chart_session") : sourceDirName);
        const QString sessionDir = QDir(sessionWorkingCopyRootDir()).filePath(sessionName);
        const QString chartDirName = sourceDirName.isEmpty() ? QStringLiteral("chart_dir") : sourceDirName;
        const QString chartDir = QDir(sessionDir).filePath(chartDirName);
        if (workingSessionDirOut)
            *workingSessionDirOut = sessionDir;
        return QDir(chartDir).filePath(fileName);
    }

    bool createWorkingCopyFromSource(const QString &sourcePath, QString *workingPathOut, QString *errorOut)
    {
        if (workingPathOut)
            workingPathOut->clear();
        if (errorOut)
            errorOut->clear();

        if (sourcePath.isEmpty())
        {
            if (errorOut)
                *errorOut = QObject::tr("Source chart path is empty.");
            return false;
        }

        const QString rootDir = sessionWorkingCopyRootDir();
        if (!QDir().mkpath(rootDir))
        {
            if (errorOut)
                *errorOut = QObject::tr("Failed to create working copy directory:\n%1").arg(rootDir);
            return false;
        }

        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
        {
            if (errorOut)
                *errorOut = QObject::tr("Source chart does not exist:\n%1").arg(sourcePath);
            return false;
        }

        QString workingSessionDir;
        const QString workingPath = buildWorkingCopyPath(sourcePath, &workingSessionDir);
        removePathRecursively(workingSessionDir);
        if (!copyDirectoryRecursively(sourceInfo.absolutePath(), QFileInfo(workingPath).absoluteDir().absolutePath(), errorOut, nullptr))
        {
            removePathRecursively(workingSessionDir);
            return false;
        }
        if (!QFile::exists(workingPath))
        {
            if (errorOut)
                *errorOut = QObject::tr("Working copy chart file is missing:\n%1").arg(workingPath);
            return false;
        }

        copyReferencedExternalResources(sourcePath, workingPath);
        syncSidecarDirectoryForChart(sourcePath, workingPath);

        if (workingPathOut)
            *workingPathOut = workingPath;
        return true;
    }

    bool createWorkingCopyFromSourceWithProgress(QWidget *parent,
                                                 const QString &sourcePath,
                                                 QString *workingPathOut,
                                                 QString *errorOut)
    {
        if (workingPathOut)
            workingPathOut->clear();
        if (errorOut)
            errorOut->clear();

        if (sourcePath.isEmpty())
        {
            if (errorOut)
                *errorOut = QObject::tr("Source chart path is empty.");
            return false;
        }

        const QString rootDir = sessionWorkingCopyRootDir();
        if (!QDir().mkpath(rootDir))
        {
            if (errorOut)
                *errorOut = QObject::tr("Failed to create working copy directory:\n%1").arg(rootDir);
            return false;
        }

        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
        {
            if (errorOut)
                *errorOut = QObject::tr("Source chart does not exist:\n%1").arg(sourcePath);
            return false;
        }

        QString workingSessionDir;
        const QString workingPath = buildWorkingCopyPath(sourcePath, &workingSessionDir);
        removePathRecursively(workingSessionDir);

        CopyProgressState progress;
        std::atomic<bool> finished{false};
        std::atomic<bool> success{false};
        QString asyncError;

        QElapsedTimer timer;
        timer.start();
        QThread *worker = QThread::create([&]()
                                          {
        QString localError;
        const bool copied = copyDirectoryRecursively(sourceInfo.absolutePath(),
                                                     QFileInfo(workingPath).absoluteDir().absolutePath(),
                                                     &localError,
                                                     &progress);
        if (copied)
        {
            copyReferencedExternalResources(sourcePath, workingPath);
            syncSidecarDirectoryForChart(sourcePath, workingPath);
            if (!QFile::exists(workingPath))
                localError = QObject::tr("Working copy chart file is missing:\n%1").arg(workingPath);
        }

        if (!localError.isEmpty())
            asyncError = localError;
        success.store(localError.isEmpty() && copied);
        finished.store(true); });
        worker->start();

        QProgressDialog progressDialog(QObject::tr("Preparing working copy..."),
                                       QObject::tr("Cancel"),
                                       0,
                                       100,
                                       parent);
        progressDialog.setWindowModality(Qt::ApplicationModal);
        progressDialog.setAutoClose(false);
        progressDialog.setAutoReset(false);
        progressDialog.show();

        while (!finished.load())
        {
            const qint64 totalFiles = qMax<qint64>(1, progress.totalFiles.load());
            const qint64 copiedFiles = qBound<qint64>(0, progress.copiedFiles.load(), totalFiles);
            const qint64 copiedBytes = qMax<qint64>(0, progress.copiedBytes.load());
            const qint64 totalBytes = qMax<qint64>(0, progress.totalBytes.load());
            const int percent = static_cast<int>((copiedFiles * 100) / totalFiles);
            progressDialog.setValue(qBound(0, percent, 100));
            progressDialog.setLabelText(QObject::tr("Preparing working copy...\n%1/%2 files, %3/%4 MB")
                                            .arg(copiedFiles)
                                            .arg(totalFiles)
                                            .arg(QString::number(copiedBytes / 1024.0 / 1024.0, 'f', 1))
                                            .arg(QString::number(totalBytes / 1024.0 / 1024.0, 'f', 1)));

            if (progressDialog.wasCanceled())
                progress.cancelRequested.store(true);

            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(15);
        }

        worker->wait();
        delete worker;
        progressDialog.setValue(100);

        const qint64 elapsedMs = timer.elapsed();
        Logger::logStructured(Logger::INFO,
                              QString("Working copy completed in %1 ms").arg(elapsedMs),
                              "WorkingCopy",
                              QMap<QString, QString>{
                                  {"source_path", sourcePath},
                                  {"working_path", workingPath},
                                  {"elapsed_ms", QString::number(elapsedMs)},
                                  {"files_total", QString::number(progress.totalFiles.load())},
                                  {"files_copied", QString::number(progress.copiedFiles.load())},
                                  {"bytes_total", QString::number(progress.totalBytes.load())},
                                  {"bytes_copied", QString::number(progress.copiedBytes.load())},
                                  {"max_file_ms", QString::number(progress.maxSingleFileMs.load())},
                                  {"success", success.load() ? "true" : "false"},
                              });

        if (progress.cancelRequested.load())
        {
            removePathRecursively(workingSessionDir);
            if (errorOut)
                *errorOut = QObject::tr("Copy cancelled by user.");
            return false;
        }

        if (!success.load())
        {
            removePathRecursively(workingSessionDir);
            if (errorOut)
                *errorOut = asyncError.isEmpty()
                                ? QObject::tr("Failed to create working copy.")
                                : asyncError;
            return false;
        }

        if (workingPathOut)
            *workingPathOut = workingPath;
        return true;
    }

    bool loadWorkingChartWithProgress(QWidget *parent,
                                      ChartController *chartController,
                                      const QString &workingChartPath,
                                      QString *errorOut)
    {
        if (errorOut)
            errorOut->clear();
        if (!chartController)
        {
            if (errorOut)
                *errorOut = QObject::tr("Chart controller is not available.");
            return false;
        }

        std::atomic<bool> finished{false};
        std::atomic<bool> success{false};
        QString asyncError;
        Chart loadedChart;

        QElapsedTimer timer;
        timer.start();
        QThread *worker = QThread::create([&]()
                                          {
        QString localError;
        Chart parsedChart;
        const bool loaded = ChartIO::load(workingChartPath, parsedChart, false);
        if (!loaded)
        {
            localError = QObject::tr("Failed to parse chart data:\n%1").arg(workingChartPath);
        }
        else
        {
            loadedChart = std::move(parsedChart);
        }

        if (!localError.isEmpty())
            asyncError = localError;
        success.store(localError.isEmpty() && loaded);
        finished.store(true); });
        worker->start();

        QProgressDialog progressDialog(QObject::tr("Loading chart data..."),
                                       QString(),
                                       0,
                                       0,
                                       parent);
        progressDialog.setWindowModality(Qt::ApplicationModal);
        progressDialog.setCancelButton(nullptr);
        progressDialog.setMinimumDuration(0);
        progressDialog.show();

        while (!finished.load())
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(15);
        }

        worker->wait();
        delete worker;

        const qint64 elapsedMs = timer.elapsed();
        Logger::logStructured(Logger::INFO,
                              QString("Working chart parsed in %1 ms").arg(elapsedMs),
                              "WorkingCopy",
                              QMap<QString, QString>{
                                  {"working_path", workingChartPath},
                                  {"elapsed_ms", QString::number(elapsedMs)},
                                  {"notes", success.load() ? QString::number(loadedChart.notes().size()) : QString("0")},
                                  {"success", success.load() ? "true" : "false"},
                              });

        if (!success.load())
        {
            if (errorOut)
                *errorOut = asyncError.isEmpty()
                                ? QObject::tr("Failed to parse chart data.")
                                : asyncError;
            return false;
        }

        if (!chartController->loadChartFromData(workingChartPath, std::move(loadedChart)))
        {
            if (errorOut)
                *errorOut = QObject::tr("Failed to apply loaded chart.");
            return false;
        }
        return true;
    }
}

MainWindow::MainWindow(ChartController *chartCtrl,
                       SelectionController *selCtrl,
                       PlaybackController *playCtrl,
                       Skin *skin,
                       QWidget *parent)
    : QMainWindow(parent), d(new Private)
{
    Logger::info("MainWindow constructor called");

    d->chartController = chartCtrl;
    d->selectionController = selCtrl;
    d->playbackController = playCtrl;
    d->skin = skin;
    d->canvas = nullptr;
    d->rightDensityBar = nullptr;
    d->splitter = nullptr;
    d->rightPanelContainer = nullptr;
    d->currentRightPanel = nullptr;
    d->notePanel = nullptr;
    d->bpmPanel = nullptr;
    d->metaPanel = nullptr;
    d->leftPanel = nullptr;
    d->undoAction = nullptr;
    d->redoAction = nullptr;
    d->colorAction = nullptr;
    d->timelineDivisionColorAction = nullptr;
    d->timelineDivisionColorSettingsAction = nullptr;
    d->hyperfruitAction = nullptr;
    d->verticalFlipAction = nullptr;
    d->playAction = nullptr;
    d->speedActionGroup = nullptr;
    d->skinMenu = nullptr;
    d->noteSoundMenu = nullptr;
    d->helpMenu = nullptr;
    d->pluginsMenu = nullptr;
    d->pluginToolsMenu = nullptr;
    d->pluginPanelsMenu = nullptr;
    d->pluginToolModeAction = nullptr;
    d->pluginToolModeToolbarAction = nullptr;
    d->pluginManagerToolbarAction = nullptr;
    d->mainToolBar = nullptr;
    d->pluginToolBar = nullptr;
    d->languageMenu = nullptr;
    d->languageActionGroup = nullptr;
    d->noteSoundVolumeAction = nullptr;
    d->notePanelAction = nullptr;
    d->bpmPanelAction = nullptr;
    d->metaPanelAction = nullptr;
    d->checkUpdatesAction = nullptr;
    d->aboutAction = nullptr;
    d->currentChartPath.clear();
    d->sourceChartPath.clear();
    d->workingChartPath.clear();
    d->isModified = false;
    d->autoSaveTimer = nullptr;
    d->pluginToolModePluginId.clear();

    setAcceptDrops(true);

    setupUi();
    createCentralArea();
    createMenus();
    setupAutoSaveTimer();

    connect(d->chartController, &ChartController::chartChanged, this, [this]()
            {
        d->isModified = true;
        d->canvas->update();
        if (d->undoAction)
            d->undoAction->setEnabled(true);
        if (d->redoAction)
            d->redoAction->setEnabled(true);
        if (d->selectionController) {
            d->selectionController->setNotes(&(d->chartController->chart()->notes()));
            d->selectionController->updateSelectionFromNotes();
        }

        // Detect resource file changes (e.g. undo/redo on meta) and reload.
        if (d->chartController && d->chartController->chart() && d->playbackController && d->playbackController->audioPlayer())
        {
            const MetaData &meta = d->chartController->chart()->meta();
            const QString chartPath = d->workingChartPath.isEmpty()
                                          ? (d->chartController->chartFilePath())
                                          : d->workingChartPath;

            if (meta.audioFile != d->lastLoadedAudioFile)
            {
                d->lastLoadedAudioFile = meta.audioFile;
                if (!chartPath.isEmpty() && !meta.audioFile.isEmpty())
                {
                    const QString chartDir = QFileInfo(chartPath).absolutePath();
                    const QString audioPath = QDir(chartDir).filePath(meta.audioFile);
                    if (QFile::exists(audioPath))
                    {
                        d->playbackController->audioPlayer()->load(audioPath);
                        updatePlaybackAvailability(d->playbackController->audioPlayer()->canPlay());
                        Logger::info(QString("chartChanged - Reloaded audio after meta change: %1").arg(audioPath));
                    }
                }
            }

            if (meta.backgroundFile != d->lastLoadedBackgroundFile)
            {
                d->lastLoadedBackgroundFile = meta.backgroundFile;
                if (d->canvas)
                    d->canvas->refreshBackground();
            }
        }

        persistRecoveryState(); });
    connect(d->chartController, &ChartController::errorOccurred, this, [this](const QString &msg)
            {
        statusBar()->showMessage(msg, 3000);
        Logger::error("ChartController error: " + msg); });
    connect(d->playbackController, &PlaybackController::errorOccurred, this, [this](const QString &msg)
            {
        statusBar()->showMessage(msg, 3000);
        Logger::error("PlaybackController error: " + msg); });
    if (d->playbackController && d->playbackController->audioPlayer())
    {
        connect(d->playbackController->audioPlayer(),
                &AudioPlayer::errorOccurred,
                this,
                [this](const QString &error)
                {
                    updatePlaybackAvailability(false);
                    statusBar()->showMessage(error, 5000);
                    QMessageBox::warning(this, tr("Audio Load Error"), error);
                });
        connect(d->playbackController->audioPlayer(),
                &AudioPlayer::loadingStateChanged,
                this,
                [this](AudioPlayer::LoadingState state)
                {
                    updatePlaybackAvailability(state == AudioPlayer::LoadingState::Loaded);
                });
        updatePlaybackAvailability(d->playbackController->audioPlayer()->canPlay());
    }
    connect(d->playbackController, &PlaybackController::speedChanged, this, [this](double speed)
            {
        if (!d->speedActionGroup)
            return;
        for (QAction *action : d->speedActionGroup->actions())
        {
            const double actionSpeed = action->data().toDouble();
            action->setChecked(qFuzzyCompare(actionSpeed, speed));
        } });
    connect(d->leftPanel, &LeftPanel::pluginQuickActionTriggered, this, &MainWindow::triggerPluginQuickAction);
    QTimer::singleShot(0, this, [this]()
                       {
        if (PluginManager *pm = activePluginManager())
        {
            connect(pm, &PluginManager::pluginsChanged, this, [this, pm]()
                    {
                        refreshPluginUiExtensions();

                        if (!d->chartController)
                            return;
                        const QString chartPath = d->chartController->chartFilePath().trimmed();
                        if (chartPath.isEmpty())
                            return;

                        // After plugin reload, replay chart context so stateful plugins
                        // can rebuild their runtime actions/panels consistently.
                        pm->notifyChartLoaded(chartPath);
                        pm->notifyChartChanged();
                        refreshPluginUiExtensions();
                    });
            refreshPluginUiExtensions();
        } });
    QTimer::singleShot(0, this, [this]()
                       { tryRecoverPreviousSession(); });

    Logger::info("MainWindow constructor finished");
}

MainWindow::~MainWindow()
{
    clearWorkingCopySession(true);
    delete d->skin;
    delete d;
    Logger::info("MainWindow destroyed");
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("Catch Chart Editor"));
    resize(1200, 800);
    Logger::debug("MainWindow UI setup completed");
}

void MainWindow::createMenus()
{
    Logger::debug("Creating menus...");
    menuBar()->clear();
    d->shortcutActions.clear();
    d->shortcutDefaults.clear();
    d->shortcutActionOrder.clear();
    if (d->languageActionGroup)
    {
        delete d->languageActionGroup;
        d->languageActionGroup = nullptr;
    }
    if (d->speedActionGroup)
    {
        delete d->speedActionGroup;
        d->speedActionGroup = nullptr;
    }

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *openAction = fileMenu->addAction(tr("&Open Chart..."), this, &MainWindow::openChart);
    registerShortcutAction(openAction, "file.open_chart", QKeySequence::Open);
    QAction *openFolderAction = fileMenu->addAction(tr("Open &Folder..."), this, &MainWindow::openFolder);
    QAction *openImportedAction = fileMenu->addAction(tr("Open &Imported Charts..."), this, &MainWindow::openImportedLibrary);
    registerShortcutAction(openImportedAction, "file.open_imported_charts", QKeySequence(tr("Ctrl+Shift+O")));
    QAction *saveAction = fileMenu->addAction(tr("&Save"), this, &MainWindow::saveChart);
    registerShortcutAction(saveAction, "file.save", QKeySequence::Save);
    QAction *saveAsAction = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveChartAs);
    fileMenu->addSeparator();
    QAction *exportAction = fileMenu->addAction(tr("&Export .mcz..."), this, &MainWindow::exportMcz);
    QAction *exportPureAction = fileMenu->addAction(tr("Export .mcz (&Pure)..."), this, &MainWindow::exportMczPure);
    fileMenu->addSeparator();
    QAction *switchDifficultyAction = fileMenu->addAction(tr("Switch &Difficulty..."), this, &MainWindow::switchDifficulty);
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    registerShortcutAction(exitAction, "file.exit", QKeySequence::Quit);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    d->undoAction = editMenu->addAction(tr("&Undo"), this, &MainWindow::undo);
    registerShortcutAction(d->undoAction, "edit.undo", QKeySequence::Undo);
    d->undoAction->setEnabled(true);
    d->redoAction = editMenu->addAction(tr("&Redo"), this, &MainWindow::redo);
    registerShortcutAction(d->redoAction, "edit.redo", QKeySequence::Redo);
    d->redoAction->setEnabled(true);
    editMenu->addSeparator();
    QAction *copyAction = editMenu->addAction(tr("&Copy"));
    connect(copyAction, &QAction::triggered, d->canvas, &ChartCanvas::handleCopy);
    registerShortcutAction(copyAction, "edit.copy", QKeySequence::Copy);
    QAction *pasteAction = editMenu->addAction(tr("&Paste"), d->canvas, &ChartCanvas::paste);
    registerShortcutAction(pasteAction, "edit.paste", QKeySequence::Paste);
    editMenu->addSeparator();
    d->deleteAction = editMenu->addAction(tr("&Delete"));
    registerShortcutAction(d->deleteAction, "edit.delete", QKeySequence::Delete);
    connect(d->deleteAction, &QAction::triggered, this, [this]()
            {
        if (d->canvas && d->canvas->triggerPluginDeleteSelection()) {
            Logger::debug("Deleted plugin selection via menu");
            return;
        }
        if (d->selectionController && !d->selectionController->selectedIndices().isEmpty()) {
            QSet<int> selected = d->selectionController->selectedIndices();
            const auto& notes = d->chartController->chart()->notes();
            QList<int> sorted = selected.values();
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());
            QVector<Note> notesToDelete;
            for (int idx : sorted) {
                if (idx >= 0 && idx < notes.size()) {
                    notesToDelete.append(notes[idx]);
                }
            }
            if (!notesToDelete.isEmpty()) {
                d->chartController->removeNotes(notesToDelete);
            }
            d->selectionController->clearSelection();
            Logger::debug("Deleted selected notes via menu");
         } });

    // Paste option: use 288-division conversion.
    editMenu->addSeparator();
    QAction *paste288Action = editMenu->addAction(tr("Paste with 288 Division"));
    paste288Action->setCheckable(true);
    paste288Action->setChecked(Settings::instance().pasteUse288Division());
    connect(paste288Action, &QAction::toggled, this, &MainWindow::togglePaste288Division);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    d->colorAction = viewMenu->addAction(tr("&Color Notes"));
    d->colorAction->setCheckable(true);
    d->colorAction->setChecked(Settings::instance().colorNoteEnabled());
    connect(d->colorAction, &QAction::toggled, this, &MainWindow::toggleColorMode);
    d->timelineDivisionColorAction = viewMenu->addAction(tr("Color Timeline Divisions"));
    d->timelineDivisionColorAction->setCheckable(true);
    d->timelineDivisionColorAction->setChecked(Settings::instance().timelineDivisionColorEnabled());
    connect(d->timelineDivisionColorAction, &QAction::toggled, this, &MainWindow::toggleTimelineDivisionColorMode);
    d->timelineDivisionColorSettingsAction = viewMenu->addAction(tr("Timeline Division Color Advanced Settings..."));
    connect(d->timelineDivisionColorSettingsAction, &QAction::triggered, this, &MainWindow::openTimelineDivisionColorSettings);
    d->hyperfruitAction = viewMenu->addAction(tr("&Hyperfruit Outline"));
    d->hyperfruitAction->setCheckable(true);
    d->hyperfruitAction->setChecked(Settings::instance().hyperfruitOutlineEnabled());
    connect(d->hyperfruitAction, &QAction::toggled, this, &MainWindow::toggleHyperfruitMode);
    d->verticalFlipAction = viewMenu->addAction(tr("&Vertical Flip"));
    d->verticalFlipAction->setCheckable(true);
    d->verticalFlipAction->setChecked(Settings::instance().verticalFlip());
    connect(d->verticalFlipAction, &QAction::toggled, this, &MainWindow::toggleVerticalFlip);

    // Background image toggle.
    QAction *bgImageAction = viewMenu->addAction(tr("Show Background Image"));
    bgImageAction->setCheckable(true);
    bgImageAction->setChecked(Settings::instance().backgroundImageEnabled());
    connect(bgImageAction, &QAction::toggled, this, [this](bool on)
            {
    Settings::instance().setBackgroundImageEnabled(on);
    if (d->canvas) d->canvas->refreshBackground(); });
    QAction *bgBrightnessAction = viewMenu->addAction(tr("Background Image Brightness..."));
    connect(bgBrightnessAction, &QAction::triggered, this, [this]()
            {
    bool ok = false;
    const int current = Settings::instance().backgroundImageBrightness();
    const int brightness = QInputDialog::getInt(
        this,
        tr("Background Image Brightness"),
        tr("Brightness (%):"),
        current,
        0,
        200,
        5,
        &ok);
    if (!ok)
        return;

    Settings::instance().setBackgroundImageBrightness(brightness);
    if (d->canvas)
        d->canvas->refreshBackground();
    statusBar()->showMessage(tr("Background image brightness: %1%").arg(brightness), 2000); });

    QMenu *bgColorMenu = viewMenu->addMenu(tr("Background Color"));
    bgColorMenu->addAction(tr("Black"), [this]()
                           {
    // Use a softer dark tone to keep UI readable and avoid pure-black harshness.
    Settings::instance().setBackgroundColor(QColor(24, 26, 30));
    applySidebarTheme();
    if (d->canvas) d->canvas->refreshBackground(); });
    bgColorMenu->addAction(tr("White"), [this]()
                           {
    Settings::instance().setBackgroundColor(Qt::white);
    applySidebarTheme();
    if (d->canvas) d->canvas->refreshBackground(); });
    bgColorMenu->addAction(tr("Gray"), [this]()
                           {
    Settings::instance().setBackgroundColor(QColor(40, 40, 40));
    applySidebarTheme();
    if (d->canvas) d->canvas->refreshBackground(); });
    bgColorMenu->addAction(tr("Custom..."), [this]()
                           {
    const QColor current = Settings::instance().backgroundColor();
    const QColor picked = QColorDialog::getColor(current, this, tr("Select Background Color"));
    if (!picked.isValid())
        return;
    Settings::instance().setBackgroundColor(picked);
    applySidebarTheme();
    if (d->canvas) d->canvas->refreshBackground(); });

    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    d->noteSizeAction = settingsMenu->addAction(tr("Note Size..."));
    connect(d->noteSizeAction, &QAction::triggered, this, &MainWindow::adjustNoteSize);
    d->calibrateSkinAction = settingsMenu->addAction(tr("Calibrate Skin..."));
    connect(d->calibrateSkinAction, &QAction::triggered, this, &MainWindow::calibrateSkin);
    d->outlineAction = settingsMenu->addAction(tr("Outline Settings..."));
    connect(d->outlineAction, &QAction::triggered, this, &MainWindow::configureOutline);
    d->noteSoundVolumeAction = settingsMenu->addAction(tr("Note Sound Volume..."));
    connect(d->noteSoundVolumeAction, &QAction::triggered, this, &MainWindow::adjustNoteSoundVolume);
    QAction *sessionSettingsAction = settingsMenu->addAction(tr("Session Settings..."));
    connect(sessionSettingsAction, &QAction::triggered, this, &MainWindow::openSessionSettings);
    settingsMenu->addSeparator();
    d->skinMenu = settingsMenu->addMenu(tr("&Skin"));
    populateSkinMenu();
    d->noteSoundMenu = settingsMenu->addMenu(tr("Note &Sound"));
    populateNoteSoundMenu();
    settingsMenu->addSeparator();
    QAction *shortcutSettingsAction = settingsMenu->addAction(tr("Keyboard Shortcuts..."));
    connect(shortcutSettingsAction, &QAction::triggered, this, &MainWindow::configureShortcuts);
    settingsMenu->addSeparator();
    d->languageMenu = settingsMenu->addMenu(tr("Language"));
    d->languageActionGroup = new QActionGroup(this);
    d->languageActionGroup->setExclusive(true);

    const QString currentLanguage = Settings::instance().language();
    const auto langs = Translator::instance().availableLanguages();
    for (auto it = langs.cbegin(); it != langs.cend(); ++it)
    {
        QAction *act = d->languageMenu->addAction(it.value());
        act->setCheckable(true);
        act->setData(it.key());
        act->setActionGroup(d->languageActionGroup);
        act->setChecked(it.key() == currentLanguage);
        connect(act, &QAction::triggered, this, &MainWindow::changeLanguage);
    }

    QMenu *playMenu = menuBar()->addMenu(tr("&Playback"));
    d->playAction = playMenu->addAction(tr("&Play/Pause"), this, &MainWindow::togglePlayback);
    d->playAction->setEnabled(d->audioPlaybackReady);
    registerShortcutAction(d->playAction, "playback.play_pause", QKeySequence(Qt::Key_Space));
    QAction *markJerkAction = playMenu->addAction(tr("Mark Playback Jerk"));
    connect(markJerkAction, &QAction::triggered, this, [this]()
            {
        if (d->canvas)
            d->canvas->recordManualJerkMark(); });
    registerShortcutAction(markJerkAction, "playback.mark_manual_jerk", QKeySequence(Qt::Key_F8));
    markJerkAction->setShortcutContext(Qt::ApplicationShortcut);
    playMenu->addSeparator();
    QMenu *speedMenu = playMenu->addMenu(tr("&Speed"));
    d->speedActionGroup = new QActionGroup(this);
    d->speedActionGroup->setExclusive(true);
    for (double sp : {0.25, 0.5, 0.75, 1.0})
    {
        QAction *act = speedMenu->addAction(tr("%1x").arg(sp), [this, sp]()
                                            {
            d->playbackController->setSpeed(sp);
            Settings::instance().setPlaybackSpeed(sp);
            Logger::info(QString("Playback speed set to %1x").arg(sp)); });
        act->setCheckable(true);
        act->setData(sp);
        act->setActionGroup(d->speedActionGroup);
        act->setChecked(qFuzzyCompare(sp, Settings::instance().playbackSpeed()));
    }
    QMenu *fpsCapMenu = playMenu->addMenu(tr("Playback FPS Cap"));
    QActionGroup *fpsCapGroup = new QActionGroup(this);
    fpsCapGroup->setExclusive(true);
    const QList<QPair<QString, int>> fpsCapOptions = {
        {tr("Lock 60 FPS"), 60},
        {tr("Lock 90 FPS"), 90},
        {tr("Lock 120 FPS"), 120},
        {tr("Unlimited"), 0},
    };
    const int currentFpsCap = Settings::instance().playbackFrameRateCap();
    for (const auto &option : fpsCapOptions)
    {
        QAction *capAction = fpsCapMenu->addAction(option.first);
        capAction->setCheckable(true);
        capAction->setData(option.second);
        capAction->setActionGroup(fpsCapGroup);
        capAction->setChecked(option.second == currentFpsCap);
        connect(capAction, &QAction::triggered, this, [this, capAction]()
                {
                    const int fpsCap = capAction->data().toInt();
                    Settings::instance().setPlaybackFrameRateCap(fpsCap);
                    if (d->playbackController)
                        d->playbackController->setFrameRateCap(fpsCap);
                    const QString capText = (fpsCap <= 0) ? tr("Unlimited") : QString::number(fpsCap);
                    statusBar()->showMessage(tr("Playback FPS cap: %1").arg(capText), 2000); });
    }
    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    d->pluginsMenu = menuBar()->addMenu(tr("&Plugins"));
    QAction *pluginManagerAction = d->pluginsMenu->addAction(tr("&Plugin Manager..."));
    connect(pluginManagerAction, &QAction::triggered, this, &MainWindow::openPluginManager);
    d->pluginToolsMenu = d->pluginsMenu->addMenu(tr("Plugin &Actions"));
    connect(d->pluginToolsMenu, &QMenu::aboutToShow, this, &MainWindow::populatePluginToolsMenu);
    d->pluginPanelsMenu = d->pluginsMenu->addMenu(tr("Plugin &Panels"));
    connect(d->pluginPanelsMenu, &QMenu::aboutToShow, this, &MainWindow::populatePluginPanelsMenu);
    d->pluginToolModeAction = d->pluginsMenu->addAction(tr("Plugin Enhanced Tool Mode"));
    d->pluginToolModeAction->setCheckable(true);
    d->pluginToolModeAction->setEnabled(false);
    connect(d->pluginToolModeAction, &QAction::toggled, this, &MainWindow::togglePluginEnhancedToolMode);

    QMenu *overlayMenu = d->pluginsMenu->addMenu(tr("Plugin Overlay Elements"));
    auto addOverlayToggle = [this, overlayMenu](const QString &key, const QString &label, bool defaultValue)
    {
        QAction *act = overlayMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(defaultValue);
        connect(act, &QAction::toggled, this, [this, key](bool on)
                {
            if (!d->canvas)
                return;
            QVariantMap toggles;
            toggles.insert(key, on);
            d->canvas->setPluginOverlayToggles(toggles); });
    };
    addOverlayToggle("overlay_enabled", tr("Enable Overlay"), true);
    addOverlayToggle("preview", tr("Preview Notes"), true);
    addOverlayToggle("control_points", tr("Control Points"), true);
    addOverlayToggle("handles", tr("Handles"), true);
    addOverlayToggle("sample_points", tr("Sample Points"), true);
    addOverlayToggle("labels", tr("Labels"), true);

    d->pluginsMenu->addSeparator();
    QAction *gridAction = toolsMenu->addAction(tr("&Grid Settings..."), d->canvas, &ChartCanvas::showGridSettings);
    toolsMenu->addSeparator();
    QAction *logSettingsAction = toolsMenu->addAction(tr("&Log Settings..."));
    connect(logSettingsAction, &QAction::triggered, this, &MainWindow::openLogSettings);
    QAction *exportDiagAction = toolsMenu->addAction(tr("&Export Diagnostics Report..."));
    connect(exportDiagAction, &QAction::triggered, this, &MainWindow::exportDiagnosticsReport);
    d->helpMenu = menuBar()->addMenu(tr("&Help"));
    d->checkUpdatesAction = d->helpMenu->addAction(tr("Check for Updates..."), this, &MainWindow::checkForUpdates);
    d->helpMenu->addSeparator();
    d->helpDocAction = d->helpMenu->addAction(tr("Help Documentation..."), this, &MainWindow::showHelpPage);
    d->aboutAction = d->helpMenu->addAction(tr("About..."), this, &MainWindow::showAboutPage);
    d->versionAction = d->helpMenu->addAction(tr("Version Information..."), this, &MainWindow::showVersionPage);
    d->logsAction = d->helpMenu->addAction(tr("Logs..."), this, &MainWindow::showLogsPage);
    applySidebarTheme();

    Logger::debug("Menus created");
}

void MainWindow::registerShortcutAction(QAction *action, const QString &actionId, const QKeySequence &defaultShortcut)
{
    if (!action || actionId.isEmpty())
        return;

    d->shortcutActions.insert(actionId, action);
    d->shortcutDefaults.insert(actionId, defaultShortcut);
    if (!d->shortcutActionOrder.contains(actionId))
        d->shortcutActionOrder.append(actionId);

    const QKeySequence saved = Settings::instance().shortcut(actionId);
    action->setShortcut(saved.isEmpty() ? defaultShortcut : saved);
}

void MainWindow::configureShortcuts()
{
    if (d->shortcutActionOrder.isEmpty())
    {
        QMessageBox::information(this, tr("Keyboard Shortcuts"), tr("No configurable shortcuts are available."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Keyboard Shortcuts"));
    dialog.setMinimumWidth(520);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Rebind shortcuts. Clear a field to disable a shortcut."), &dialog));
    QLabel *limitHint = new QLabel(tr("Note: currently only 2-key combos using Shift/Ctrl are reliably supported. More complex combos and multi-main-key single-step bindings are not supported yet."), &dialog);
    limitHint->setWordWrap(true);
    layout->addWidget(limitHint);

    QFormLayout *form = new QFormLayout();
    QHash<QString, ShortcutCaptureEdit *> editors;

    for (const QString &actionId : d->shortcutActionOrder)
    {
        QAction *action = d->shortcutActions.value(actionId, nullptr);
        if (!action)
            continue;

        QWidget *row = new QWidget(&dialog);
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        ShortcutCaptureEdit *edit = new ShortcutCaptureEdit(row);
        edit->setKeySequence(action->shortcut());

        QPushButton *resetBtn = new QPushButton(tr("Reset"), row);
        connect(resetBtn, &QPushButton::clicked, this, [this, actionId, edit]()
                { edit->setKeySequence(d->shortcutDefaults.value(actionId)); });

        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(resetBtn);

        QString label = action->text();
        label.remove('&');
        form->addRow(label, row);
        editors.insert(actionId, edit);
    }

    layout->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *resetAllBtn = buttons->addButton(tr("Reset All"), QDialogButtonBox::ResetRole);
    connect(resetAllBtn, &QPushButton::clicked, this, [this, &editors]()
            {
        for (auto it = editors.constBegin(); it != editors.constEnd(); ++it)
            it.value()->setKeySequence(d->shortcutDefaults.value(it.key())); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog]()
            { dialog.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QHash<QString, QString> usedByShortcut;
    for (const QString &actionId : d->shortcutActionOrder)
    {
        ShortcutCaptureEdit *edit = editors.value(actionId, nullptr);
        if (!edit)
            continue;

        const QString portable = edit->keySequence().toString(QKeySequence::PortableText);
        if (portable.isEmpty())
            continue;
        if (usedByShortcut.contains(portable) && usedByShortcut.value(portable) != actionId)
        {
            QMessageBox::warning(
                this,
                tr("Keyboard Shortcuts"),
                tr("Shortcut conflict detected. Please assign unique shortcuts."));
            return;
        }
        usedByShortcut.insert(portable, actionId);
    }

    for (const QString &actionId : d->shortcutActionOrder)
    {
        QAction *action = d->shortcutActions.value(actionId, nullptr);
        ShortcutCaptureEdit *edit = editors.value(actionId, nullptr);
        if (!action || !edit)
            continue;

        const QKeySequence seq = edit->keySequence();
        Settings::instance().setShortcut(actionId, seq);
        action->setShortcut(seq);
    }

    statusBar()->showMessage(tr("Keyboard shortcuts updated."), 2500);
}

void MainWindow::createCentralArea()
{
    Logger::debug("Creating central area...");

    d->leftPanel = new LeftPanel(this);
    d->leftPanel->setObjectName("leftPanelRoot");
    d->leftPanel->setAttribute(Qt::WA_StyledBackground, true);
    d->leftPanel->setChartController(d->chartController);
    d->leftPanel->setPlaybackController(d->playbackController);

    d->canvas = new ChartCanvas(this);
    d->canvas->setChartController(d->chartController);
    d->canvas->setSelectionController(d->selectionController);
    d->canvas->setPlaybackController(d->playbackController);
    d->canvas->setColorMode(Settings::instance().colorNoteEnabled());
    d->canvas->setHyperfruitEnabled(Settings::instance().hyperfruitOutlineEnabled());
    if (d->skin)
        d->canvas->setSkin(d->skin);
    d->canvas->setNoteSize(Settings::instance().noteSize());
    d->canvas->setNoteSoundVolume(Settings::instance().noteSoundVolume());
    QString noteSoundPath = Settings::instance().noteSoundPath();
    if (!noteSoundPath.isEmpty() && !QFile::exists(noteSoundPath))
    {
        noteSoundPath.clear();
        Settings::instance().setNoteSoundPath(QString());
    }
    d->canvas->setNoteSoundFile(noteSoundPath);
    d->canvas->setNoteSoundEnabled(!noteSoundPath.isEmpty());

    d->previewWidget = new RealtimePreviewWidget(this);
    d->previewWidget->setChartController(d->chartController);
    d->previewWidget->setPlaybackController(d->playbackController);
    d->previewWidget->setColorMode(Settings::instance().colorNoteEnabled());
    d->previewWidget->setHyperfruitEnabled(Settings::instance().hyperfruitOutlineEnabled());
    d->previewWidget->setNoteSize(Settings::instance().noteSize());
    d->previewWidget->setCurrentTimeMs(d->canvas->currentPlayTime());

    connect(d->canvas, &ChartCanvas::statusMessage, this, [this](const QString &msg)
            { statusBar()->showMessage(msg, 2000); });
    connect(d->canvas, &ChartCanvas::scrollPositionChanged, this, [this](double)
            {
        if (!d->previewWidget || !d->canvas)
            return;
        if (d->playbackController && d->playbackController->state() == PlaybackController::Playing)
            return;
        d->previewWidget->setCurrentTimeMs(d->canvas->currentPlayTime()); });

    d->leftPanel->setChartCanvas(d->canvas);

    d->rightDensityBar = new DensityCurve(this);
    d->rightDensityBar->setChartController(d->chartController);
    d->rightDensityBar->setPlaybackController(d->playbackController);
    d->rightDensityBar->setCanvas(d->canvas);

    QWidget *canvasContainer = new QWidget(this);
    QHBoxLayout *canvasLayout = new QHBoxLayout(canvasContainer);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);
    canvasLayout->addWidget(d->canvas, 1);
    canvasLayout->addWidget(d->rightDensityBar, 0);

    connect(d->rightDensityBar, &DensityCurve::seekGestureStarted, this, [this]()
            {
        d->densitySeekGestureActive = true;
        d->densityPendingSeekMs = std::numeric_limits<double>::quiet_NaN();
        if (d->playbackController && d->playbackController->state() == PlaybackController::Playing)
        {
            Logger::debug("Playback paused due to density bar drag interaction");
            d->playbackController->pause();
        } });

    const auto clampSeekMs = [this](double targetTimeMs) -> double
    {
        double clamped = qMax(0.0, targetTimeMs);
        if (d->playbackController && d->playbackController->audioPlayer())
        {
            const qint64 duration = d->playbackController->audioPlayer()->duration();
            if (duration > 0)
                clamped = qBound(0.0, clamped, static_cast<double>(duration));
        }
        return clamped;
    };

    connect(d->rightDensityBar, &DensityCurve::seekPreviewRequested, this, [this, clampSeekMs](double targetTimeMs)
            {
        if (!d->canvas)
            return;
        const double clamped = clampSeekMs(targetTimeMs);
        d->densityPendingSeekMs = clamped;
        // Preview path: update visual playhead only; defer real seek to gesture end.
        d->canvas->setScrollPos(clamped); });

    connect(d->rightDensityBar, &DensityCurve::seekRequested, this, [this, clampSeekMs](double targetTimeMs)
            {
        if (!d->playbackController || !d->canvas)
            return;
        const double clamped = clampSeekMs(targetTimeMs);
        d->densityPendingSeekMs = clamped;
        d->canvas->setScrollPos(clamped);
        // Fallback for non-gesture calls.
        if (!d->densitySeekGestureActive)
            d->playbackController->seekTo(clamped); });

    connect(d->rightDensityBar, &DensityCurve::seekGestureFinished, this, [this]()
            {
        if (d->playbackController && std::isfinite(d->densityPendingSeekMs))
            d->playbackController->seekTo(d->densityPendingSeekMs);
        d->densitySeekGestureActive = false; });

    d->rightPanelContainer = new QWidget(this);
    d->rightPanelContainer->setObjectName("rightPanelRoot");
    d->rightPanelContainer->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *rightLayout = new QVBoxLayout(d->rightPanelContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    d->notePanel = new NoteEditPanel(d->rightPanelContainer);
    d->bpmPanel = new BPMTimePanel(d->rightPanelContainer);
    d->metaPanel = new MetaEditPanel(d->rightPanelContainer);
    d->currentRightPanel = d->notePanel;
    rightLayout->addWidget(d->notePanel);
    rightLayout->addWidget(d->bpmPanel);
    rightLayout->addWidget(d->metaPanel);
    d->bpmPanel->setVisible(false);
    d->metaPanel->setVisible(false);

    d->notePanel->setChartController(d->chartController);
    d->notePanel->setSelectionController(d->selectionController);
    d->bpmPanel->setChartController(d->chartController);
    d->bpmPanel->setPlaybackController(d->playbackController);
    d->metaPanel->setChartController(d->chartController);
    connect(d->metaPanel, &MetaEditPanel::backgroundResourceChanged, this,
            [this](const QString &fileName)
            {
                if (d->canvas)
                    d->canvas->refreshBackground();
                Q_UNUSED(fileName);
            });

    connect(d->metaPanel, &MetaEditPanel::audioFileChanged, this,
            [this](const QString &fileName)
            {
                if (!d->playbackController || !d->playbackController->audioPlayer() || fileName.isEmpty())
                    return;

                const QString chartPath = d->workingChartPath.isEmpty()
                                              ? (d->chartController ? d->chartController->chartFilePath() : QString())
                                              : d->workingChartPath;
                if (chartPath.isEmpty())
                    return;

                const QString chartDir = QFileInfo(chartPath).absolutePath();
                const QString audioPath = QDir(chartDir).filePath(fileName);

                if (!QFile::exists(audioPath))
                {
                    const QString msg = tr("Audio file not found: %1").arg(audioPath);
                    statusBar()->showMessage(msg, 5000);
                    Logger::warn("MetaEditPanel::audioFileChanged - " + msg);
                    return;
                }

                d->playbackController->audioPlayer()->load(audioPath);
                updatePlaybackAvailability(d->playbackController->audioPlayer()->canPlay());
                statusBar()->showMessage(tr("Audio reloaded: %1").arg(fileName), 3000);
                Logger::info(QString("MetaEditPanel::audioFileChanged - Reloaded audio: %1").arg(audioPath));
            });

    // Connect NoteEditPanel signals.
    connect(d->notePanel, &NoteEditPanel::timeDivisionChanged, d->canvas, &ChartCanvas::setTimeDivision);
    connect(d->notePanel, &NoteEditPanel::gridDivisionChanged, d->canvas, &ChartCanvas::setGridDivision);
    connect(d->notePanel, &NoteEditPanel::gridSnapChanged, d->canvas, [this](bool on)
            {
        Logger::info(QString("[Grid] MainWindow::gridSnapChanged signal received: %1").arg(on));
        d->canvas->setGridSnap(on); });
    connect(d->notePanel, &NoteEditPanel::modeChanged, d->canvas, [this](int mode)
            {
        if (mode == NoteEditPanel::PlaceAnchorMode)
        {
            d->canvas->setMode(ChartCanvas::AnchorPlace);
            togglePluginEnhancedToolMode(true);
            if (!d->canvas->isPluginToolModeActive())
            {
                d->notePanel->setModeFromHost(NoteEditPanel::PlaceNoteMode);
                d->canvas->setMode(ChartCanvas::PlaceNote);
            }
            return;
        }

        if (d->canvas->isPluginToolModeActive())
            togglePluginEnhancedToolMode(false);
        d->canvas->setMode(static_cast<ChartCanvas::Mode>(mode)); });
    connect(d->notePanel, &NoteEditPanel::copyRequested, d->canvas, &ChartCanvas::handleCopy);
    connect(d->notePanel, &NoteEditPanel::deleteOnceRequested, this, [this]()
            {
        if (d->deleteAction)
            d->deleteAction->trigger(); });
    connect(d->notePanel, &NoteEditPanel::mirrorAxisChanged, d->canvas, &ChartCanvas::setMirrorAxisX);
    connect(d->notePanel, &NoteEditPanel::mirrorGuideVisibilityChanged, d->canvas, &ChartCanvas::setMirrorGuideVisible);
    connect(d->notePanel, &NoteEditPanel::mirrorPreviewVisibilityChanged, d->canvas, &ChartCanvas::setMirrorPreviewVisible);
    connect(d->notePanel, &NoteEditPanel::mirrorFlipRequested, d->canvas, &ChartCanvas::flipSelectedNotes);
    connect(d->notePanel, &NoteEditPanel::pluginPlacementActionTriggered, this, &MainWindow::triggerPluginQuickAction);
    connect(d->canvas, &ChartCanvas::mirrorAxisChanged, d->notePanel, &NoteEditPanel::setMirrorAxisValue);

    d->splitter = new QSplitter(Qt::Horizontal, this);
    d->splitter->addWidget(d->leftPanel);
    d->splitter->addWidget(d->previewWidget);
    d->splitter->addWidget(canvasContainer);
    d->splitter->addWidget(d->rightPanelContainer);
    d->splitter->setSizes({150, 200, 700, 300});
    setCentralWidget(d->splitter);
    d->mainToolBar = addToolBar(tr("Tools"));
    d->notePanelAction = d->mainToolBar->addAction(tr("Note"), [this]()
                                                   { showEditorPanel(d->notePanel); });
    d->bpmPanelAction = d->mainToolBar->addAction(tr("BPM"), [this]()
                                                  { showEditorPanel(d->bpmPanel); });
    d->metaPanelAction = d->mainToolBar->addAction(tr("Meta"), [this]()
                                                   { showEditorPanel(d->metaPanel); });
    addToolBarBreak(Qt::TopToolBarArea);
    d->pluginToolBar = addToolBar(tr("Plugins"));
    d->pluginManagerToolbarAction = d->pluginToolBar->addAction(tr("Plugins"), this, &MainWindow::openPluginManager);
    d->pluginToolModeToolbarAction = d->pluginToolBar->addAction(tr("Launch Curve Tool"));
    d->pluginToolModeToolbarAction->setCheckable(true);
    d->pluginToolModeToolbarAction->setEnabled(false);
    connect(d->pluginToolModeToolbarAction, &QAction::toggled, this, &MainWindow::togglePluginEnhancedToolMode);
    showEditorPanel(d->notePanel);
    applySidebarTheme();

    Logger::debug("Central area created with LeftPanel.");
}

// ==================== beatmap root path ====================
QString MainWindow::beatmapRootPath() const
{
    return Settings::instance().defaultBeatmapPath();
}

void MainWindow::persistRecoveryState()
{
    if (d->workingChartPath.isEmpty() || !d->isModified)
    {
        removeRecoveryState();
        return;
    }

    RecoverySessionState state;
    state.sourcePath = d->sourceChartPath;
    state.workingPath = d->workingChartPath;
    state.modified = d->isModified;
    writeRecoveryState(state);
}

void MainWindow::clearWorkingCopySession(bool removeWorkingFile)
{
    if (removeWorkingFile && !d->workingChartPath.isEmpty())
        removePathRecursively(workingSessionDirFromWorkingPath(d->workingChartPath));
    d->workingChartPath.clear();
    d->sourceChartPath.clear();
    if (d->canvas)
        d->canvas->setSourceChartPath(QString());
    removeRecoveryState();
    cleanupSessionWorkingCopies(QString());
}

void MainWindow::setupAutoSaveTimer()
{
    if (d->autoSaveTimer)
    {
        d->autoSaveTimer->stop();
        d->autoSaveTimer->deleteLater();
    }
    d->autoSaveTimer = new QTimer(this);
    d->autoSaveTimer->setTimerType(Qt::CoarseTimer);
    d->autoSaveTimer->setInterval(Settings::instance().autoSaveIntervalSec() * 1000);
    connect(d->autoSaveTimer, &QTimer::timeout, this, [this]()
            { performAutoSaveTick(); });
    if (Settings::instance().autoSaveEnabled())
        d->autoSaveTimer->start();
}

void MainWindow::performAutoSaveTick()
{
    if (!Settings::instance().autoSaveEnabled())
        return;
    if (!d->isModified || !d->chartController)
        return;

    QString sourcePath = d->sourceChartPath;
    if (sourcePath.isEmpty())
        sourcePath = d->currentChartPath;
    if (sourcePath.isEmpty())
        return;

    if (!d->chartController->saveChart(sourcePath))
    {
        Logger::warn(QString("Auto-save failed: %1").arg(sourcePath));
        return;
    }

    d->sourceChartPath = sourcePath;
    d->currentChartPath = sourcePath;
    if (d->canvas)
        d->canvas->setSourceChartPath(sourcePath);
    d->isModified = false;
    if (!d->workingChartPath.isEmpty())
        d->chartController->saveChart(d->workingChartPath);
    syncReferencedResourcesForSavedChart(d->workingChartPath, sourcePath);
    syncSidecarDirectoryForChart(d->workingChartPath, sourcePath);
    if (!d->workingChartPath.isEmpty())
        d->chartController->saveChart(d->workingChartPath);
    persistRecoveryState();
    statusBar()->showMessage(tr("Auto-saved: %1").arg(sourcePath), 1200);
}

void MainWindow::tryRecoverPreviousSession()
{
    RecoverySessionState state;
    if (!readRecoveryState(&state))
    {
        cleanupSessionWorkingCopies(QString());
        return;
    }
    if (!state.modified)
    {
        removeRecoveryState();
        cleanupSessionWorkingCopies(QString());
        return;
    }
    if (!QFile::exists(state.workingPath))
    {
        removeRecoveryState();
        cleanupSessionWorkingCopies(QString());
        return;
    }

    QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("Recover Unsaved Session"),
        tr("Detected that the previous session may not have exited normally.\n"
           "Unsaved edits were found in a recovery working copy.\n"
           "Do you want to recover them now?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (choice != QMessageBox::Yes)
    {
        removePathRecursively(workingSessionDirFromWorkingPath(state.workingPath));
        removeRecoveryState();
        cleanupSessionWorkingCopies(QString());
        return;
    }

    if (!d->chartController->loadChart(state.workingPath))
    {
        QMessageBox::warning(this, tr("Recovery Failed"), tr("Failed to load the recovery working copy."));
        removePathRecursively(workingSessionDirFromWorkingPath(state.workingPath));
        removeRecoveryState();
        cleanupSessionWorkingCopies(QString());
        return;
    }

    d->sourceChartPath = state.sourcePath;
    d->workingChartPath = state.workingPath;
    d->currentChartPath = state.sourcePath;
    if (d->canvas)
        d->canvas->setSourceChartPath(state.sourcePath);

    // 恢复音频加载（正常打开流程在 loadChartFile 中完成，崩溃恢复路径缺失此步骤）
    updatePlaybackAvailability(false);
    if (d->chartController && d->chartController->chart())
    {
        const MetaData &recoveryMeta = d->chartController->chart()->meta();
        const QString audioFile = recoveryMeta.audioFile;
        if (!audioFile.isEmpty())
        {
            const QString sourceDir = QFileInfo(state.sourcePath).absolutePath();
            const QString audioPath = sourceDir + "/" + audioFile;
            if (QFile::exists(audioPath))
            {
                d->playbackController->audioPlayer()->load(audioPath);
            }
            else
            {
                const QString msg = tr("Audio file not found: %1").arg(audioPath);
                statusBar()->showMessage(msg, 5000);
            }
        }
        d->lastLoadedAudioFile = recoveryMeta.audioFile;
        d->lastLoadedBackgroundFile = recoveryMeta.backgroundFile;
    }

    d->isModified = true;
    d->canvas->update();
    statusBar()->showMessage(tr("Recovered unsaved session"), 3000);
    cleanupSessionWorkingCopies(d->workingChartPath);
    persistRecoveryState();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmSaveIfModified(tr("Closing the application will end this editing session.")))
    {
        event->ignore();
        return;
    }

    clearWorkingCopySession(true);
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (firstLocalMczPathFromMimeData(event ? event->mimeData() : nullptr).isEmpty())
    {
        if (event)
            event->ignore();
        return;
    }

    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (firstLocalMczPathFromMimeData(event ? event->mimeData() : nullptr).isEmpty())
    {
        if (event)
            event->ignore();
        return;
    }

    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QString mczPath = firstLocalMczPathFromMimeData(event ? event->mimeData() : nullptr);
    if (mczPath.isEmpty())
    {
        if (event)
            event->ignore();
        return;
    }

    event->acceptProposedAction();
    statusBar()->showMessage(tr("Importing MCZ: %1").arg(QFileInfo(mczPath).fileName()), 2000);
    loadChartFile(mczPath);
}

bool MainWindow::confirmSaveIfModified(const QString &reasonText)
{
    if (!d->isModified)
        return true;

    QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        tr("Unsaved Changes"),
        tr("Current chart has unsaved changes.\n%1\nDo you want to save before continuing?")
            .arg(reasonText),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Cancel)
        return false;

    if (choice == QMessageBox::Discard)
    {
        if (PluginManager *pm = activePluginManager())
            pm->notifyHostDiscardChanges(reasonText);
        return true;
    }

    QString savePath = d->sourceChartPath;
    if (savePath.isEmpty())
        savePath = d->currentChartPath;
    if (savePath.isEmpty())
    {
        savePath = QFileDialog::getSaveFileName(
            this,
            tr("Save Chart As"),
            Settings::instance().lastOpenPath(),
            tr("Malody Catch Chart (*.mc);;All Files (*.*)"));
        if (savePath.isEmpty())
            return false;
    }

    if (!d->chartController->saveChart(savePath))
    {
        QMessageBox::critical(this, tr("Error"), tr("Failed to save chart."));
        return false;
    }

    d->sourceChartPath = savePath;
    d->currentChartPath = savePath;
    if (d->canvas)
        d->canvas->setSourceChartPath(savePath);
    Settings::instance().setLastOpenPath(QFileInfo(savePath).absolutePath());
    d->isModified = false;
    if (!d->workingChartPath.isEmpty())
        d->chartController->saveChart(d->workingChartPath);
    syncReferencedResourcesForSavedChart(d->workingChartPath, savePath);
    syncSidecarDirectoryForChart(d->workingChartPath, savePath);
    if (!d->workingChartPath.isEmpty())
        d->chartController->saveChart(d->workingChartPath);
    else
        createWorkingCopyFromSource(savePath, &d->workingChartPath, nullptr);
    persistRecoveryState();
    statusBar()->showMessage(tr("Saved: %1").arg(savePath), 2000);
    if (PluginManager *pm = activePluginManager())
        pm->notifyChartSaved(savePath);
    return true;
}

// ==================== Open chart file (.mc/.mcz) ====================
void MainWindow::openChart()
{
    QString startDir = Settings::instance().lastProjectPath();
    if (startDir.isEmpty() || !QDir(startDir).exists())
    {
        startDir = beatmapRootPath();
    }

    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Chart"), startDir,
                                                    tr("Malody Catch Chart (*.mc *.mcz);;All Files (*.*)"));
    if (fileName.isEmpty())
        return;

    loadChartFile(fileName);
}

// ==================== Open folder ====================
void MainWindow::openFolder()
{
    QString startDir = Settings::instance().lastProjectPath();
    if (startDir.isEmpty() || !QDir(startDir).exists())
    {
        startDir = beatmapRootPath();
    }

    QString dirPath = QFileDialog::getExistingDirectory(this, tr("Open Folder"), startDir);
    if (dirPath.isEmpty())
        return;

    Settings::instance().setLastProjectPath(dirPath);
    Logger::info(QString("Opening folder: %1").arg(dirPath));

    QList<QPair<QString, QString>> charts = ProjectIO::findChartsInDirectory(dirPath);
    if (charts.isEmpty())
    {
        QMessageBox::information(this, tr("No Charts"), tr("No .mc files found in the selected folder."));
        return;
    }

    QString selectedPath = selectChartFromFolder(dirPath, charts, tr("Select Chart in Folder"));
    if (selectedPath.isEmpty())
        return;

    loadChartFile(selectedPath);
}

void MainWindow::openImportedLibrary()
{
    const QString beatmapDir = beatmapRootPath();
    QDir().mkpath(beatmapDir);

    const QString selectedPath = selectChartFromLibrary(beatmapDir);
    if (selectedPath.isEmpty())
        return;

    Settings::instance().setLastProjectPath(beatmapDir);
    loadChartFile(selectedPath);
}

// ==================== Load chart core logic ====================
void MainWindow::loadChartFile(const QString &filePath)
{
    Logger::info(QString("Loading chart file: %1").arg(filePath));
    if (!confirmSaveIfModified(tr("Opening another chart will replace the current one in editor.")))
        return;
    clearWorkingCopySession(true);

    QString actualChartPath = filePath;
    QFileInfo fi(filePath);
    if (fi.suffix().toLower() == "mcz")
    {
        QString beatmapDir = beatmapRootPath();
        const QString baseName = fi.completeBaseName();
        const QString defaultTargetDir = beatmapDir + "/" + baseName;
        QString targetDir = defaultTargetDir;

        QDir().mkpath(beatmapDir);

        const bool defaultDirExists = QDir(defaultTargetDir).exists();
        const bool hasExistingImportedCharts = !ProjectIO::findChartsInDirectory(defaultTargetDir).isEmpty();
        if (defaultDirExists && hasExistingImportedCharts)
        {
            QMessageBox chooser(this);
            chooser.setIcon(QMessageBox::Question);
            chooser.setWindowTitle(tr("Chart Already Imported"));
            chooser.setText(tr("This song appears to be imported already."));
            chooser.setInformativeText(tr("Open an imported chart from the local library, or import this MCZ again into a new folder?"));

            QPushButton *openImportedBtn = chooser.addButton(tr("Open Imported"), QMessageBox::AcceptRole);
            QPushButton *reimportBtn = chooser.addButton(tr("Import Again"), QMessageBox::ActionRole);
            chooser.addButton(QMessageBox::Cancel);
            chooser.setDefaultButton(openImportedBtn);
            chooser.exec();

            if (chooser.clickedButton() == openImportedBtn)
            {
                const QString selectedChart = selectChartFromLibrary(beatmapDir, baseName);
                if (selectedChart.isEmpty())
                    return;
                actualChartPath = selectedChart;
                Settings::instance().setLastProjectPath(beatmapDir);
            }
            else if (chooser.clickedButton() == reimportBtn)
            {
                // 避免重复导入同名 MCZ 时目录冲突：自动追加序号。
                for (int i = 2; QDir(targetDir).exists(); ++i)
                {
                    targetDir = QString("%1/%2 (%3)").arg(beatmapDir, baseName).arg(i);
                }
            }
            else
            {
                return;
            }
        }

        if (actualChartPath == filePath)
        {
            QString extractedDir;
            if (!ProjectIO::extractMcz(filePath, targetDir, extractedDir))
            {
                QMessageBox::critical(this, tr("Error"), tr("Failed to extract MCZ file."));
                return;
            }

            QList<QPair<QString, QString>> charts = ProjectIO::findChartsInDirectory(extractedDir);
            if (charts.isEmpty())
            {
                QMessageBox::critical(this, tr("Error"), tr("No .mc files found in the extracted content."));
                return;
            }

            actualChartPath = selectChartFromList(charts, tr("Select Chart from MCZ"));
            if (actualChartPath.isEmpty())
                return;

            Settings::instance().setLastProjectPath(beatmapDir);
        }
    }

    closePluginPanels(tr("Plugin panels were closed after chart switch."));

    QString workingChartPath;
    QString workingCopyError;
    if (!createWorkingCopyFromSourceWithProgress(this, actualChartPath, &workingChartPath, &workingCopyError))
    {
        QMessageBox::critical(this, tr("Error"), workingCopyError);
        return;
    }

    QString loadChartError;
    if (!loadWorkingChartWithProgress(this, d->chartController, workingChartPath, &loadChartError))
    {
        removePathRecursively(workingSessionDirFromWorkingPath(workingChartPath));
        QMessageBox::critical(this,
                              tr("Error"),
                              loadChartError.isEmpty() ? tr("Failed to load chart.") : loadChartError);
        return;
    }

    d->sourceChartPath = actualChartPath;
    d->workingChartPath = workingChartPath;
    d->currentChartPath = actualChartPath;
    if (d->canvas)
        d->canvas->setSourceChartPath(actualChartPath);
    Settings::instance().setLastOpenPath(QFileInfo(actualChartPath).absolutePath());

    if (QFileInfo(filePath).suffix().toLower() != "mcz")
    {
        Settings::instance().setLastProjectPath(QFileInfo(actualChartPath).absolutePath());
    }

    QString chartDir = QFileInfo(actualChartPath).absolutePath();
    const MetaData &loadedMeta = d->chartController->chart()->meta();
    QString audioFile = loadedMeta.audioFile;
    updatePlaybackAvailability(false);
    if (!audioFile.isEmpty())
    {
        QString audioPath = chartDir + "/" + audioFile;
        if (QFile::exists(audioPath))
        {
            d->playbackController->audioPlayer()->load(audioPath);
        }
        else
        {
            const QString msg = tr("Audio file not found: %1").arg(audioPath);
            statusBar()->showMessage(msg, 5000);
            QMessageBox::warning(this, tr("Audio Load Error"), msg);
        }
    }

    // Initialize resource cache for change detection.
    d->lastLoadedAudioFile = loadedMeta.audioFile;
    d->lastLoadedBackgroundFile = loadedMeta.backgroundFile;

    d->canvas->update();
    d->isModified = false;

    persistRecoveryState();
    statusBar()->showMessage(tr("Loaded: %1").arg(QFileInfo(actualChartPath).fileName()), 3000);
}

QString MainWindow::selectChartFromList(const QList<QPair<QString, QString>> &charts, const QString &title)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(350);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Select a chart:")));

    QListWidget *list = new QListWidget();
    for (const auto &chart : charts)
    {
        QString display = chart.second;
        QListWidgetItem *item = new QListWidgetItem(display, list);
        item->setData(Qt::UserRole, chart.first);
        item->setToolTip(chart.first);
    }
    layout->addWidget(list);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted || list->currentItem() == nullptr)
        return QString();

    return list->currentItem()->data(Qt::UserRole).toString();
}

QString MainWindow::selectChartFromFolder(const QString &rootDir,
                                          const QList<QPair<QString, QString>> &charts,
                                          const QString &title)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(620, 460);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Select a chart (grouped by song):")));

    QTreeWidget *tree = new QTreeWidget(&dialog);
    tree->setColumnCount(2);
    tree->setHeaderLabels(QStringList() << tr("Song / Folder / Chart") << tr("Difficulty"));
    tree->setRootIsDecorated(true);
    tree->setTextElideMode(Qt::ElideMiddle);
    constexpr int kPickerDifficultyColumnWidth = 150;
    constexpr int kPickerPrimaryMinWidth = 420;
    if (QHeaderView *header = tree->header())
    {
        header->setSectionResizeMode(0, QHeaderView::Interactive);
        header->setSectionResizeMode(1, QHeaderView::Fixed);
        header->setStretchLastSection(false);
    }
    tree->setColumnWidth(1, kPickerDifficultyColumnWidth);
    const int primaryMaxWidth = qMax(kPickerPrimaryMinWidth, dialog.width() - kPickerDifficultyColumnWidth - 72);
    const int savedPrimaryWidth = Settings::instance().chartPickerPrimaryColumnWidth();
    tree->setColumnWidth(0, qBound(kPickerPrimaryMinWidth, savedPrimaryWidth, primaryMaxWidth));
    layout->addWidget(tree);

    QHash<QString, QTreeWidgetItem *> songItems;
    QHash<QString, QTreeWidgetItem *> folderItems;
    QTreeWidgetItem *firstChartItem = nullptr;

    const QDir root(rootDir);
    for (const auto &chart : charts)
    {
        const QString chartPath = chart.first;
        const QString difficulty = chart.second;
        const QFileInfo chartInfo(chartPath);
        const QString relDir = root.relativeFilePath(chartInfo.absolutePath());
        const QString folderLabel = (relDir == "." || relDir.isEmpty()) ? tr("(Root)") : relDir;

        QString songTitle = chartSongTitleFromFile(chartPath);
        if (songTitle.isEmpty())
        {
            const QString fallback = chartInfo.absoluteDir().dirName();
            songTitle = fallback.isEmpty() ? chartInfo.completeBaseName() : fallback;
        }

        QTreeWidgetItem *songItem = songItems.value(songTitle, nullptr);
        if (!songItem)
        {
            songItem = new QTreeWidgetItem(tree);
            songItem->setText(0, songTitle);
            songItem->setExpanded(true);
            songItems.insert(songTitle, songItem);
        }

        const QString folderKey = songTitle + QStringLiteral("||") + folderLabel;
        QTreeWidgetItem *folderItem = folderItems.value(folderKey, nullptr);
        if (!folderItem)
        {
            folderItem = new QTreeWidgetItem(songItem);
            folderItem->setText(0, folderLabel);
            folderItem->setExpanded(true);
            folderItems.insert(folderKey, folderItem);
        }

        QTreeWidgetItem *chartItem = new QTreeWidgetItem(folderItem);
        chartItem->setText(0, chartInfo.fileName());
        chartItem->setText(1, difficulty);
        chartItem->setData(0, Qt::UserRole, chartPath);
        chartItem->setToolTip(0, chartPath);
        if (!firstChartItem)
            firstChartItem = chartItem;
    }

    if (tree->topLevelItemCount() == 0)
    {
        QMessageBox::information(this, tr("No Charts"), tr("No .mc files found in the selected folder."));
        return QString();
    }

    tree->expandToDepth(1);
    if (firstChartItem)
        tree->setCurrentItem(firstChartItem);

    connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&dialog](QTreeWidgetItem *item, int)
            {
        if (!item->data(0, Qt::UserRole).toString().isEmpty())
            dialog.accept(); });

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const int dialogResult = dialog.exec();
    Settings::instance().setChartPickerPrimaryColumnWidth(tree->columnWidth(0));
    if (dialogResult != QDialog::Accepted || tree->currentItem() == nullptr)
        return QString();

    const QString selectedPath = tree->currentItem()->data(0, Qt::UserRole).toString();
    if (selectedPath.isEmpty())
    {
        QMessageBox::information(this, tr("Select Chart"), tr("Please select a chart item, not a song or folder group."));
        return QString();
    }

    return selectedPath;
}

QString MainWindow::selectChartFromLibrary(const QString &libraryRoot, const QString &preferredSong)
{
    QDir rootDir(libraryRoot);
    if (!rootDir.exists())
        return QString();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Imported Chart Library"));
    dialog.resize(560, 420);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Select a chart from imported songs:")));

    QTreeWidget *tree = new QTreeWidget(&dialog);
    tree->setColumnCount(2);
    tree->setHeaderLabels(QStringList() << tr("Song / Chart") << tr("Difficulty"));
    tree->setRootIsDecorated(true);
    tree->setTextElideMode(Qt::ElideMiddle);
    constexpr int kPickerDifficultyColumnWidth = 150;
    constexpr int kPickerPrimaryMinWidth = 420;
    if (QHeaderView *header = tree->header())
    {
        header->setSectionResizeMode(0, QHeaderView::Interactive);
        header->setSectionResizeMode(1, QHeaderView::Fixed);
        header->setStretchLastSection(false);
    }
    tree->setColumnWidth(1, kPickerDifficultyColumnWidth);
    const int primaryMaxWidth = qMax(kPickerPrimaryMinWidth, dialog.width() - kPickerDifficultyColumnWidth - 72);
    const int savedPrimaryWidth = Settings::instance().chartPickerPrimaryColumnWidth();
    tree->setColumnWidth(0, qBound(kPickerPrimaryMinWidth, savedPrimaryWidth, primaryMaxWidth));
    layout->addWidget(tree);

    QStringList songDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &songName : songDirs)
    {
        const QString songPath = rootDir.absoluteFilePath(songName);
        const QList<QPair<QString, QString>> charts = ProjectIO::findChartsInDirectory(songPath);
        if (charts.isEmpty())
            continue;

        QTreeWidgetItem *songItem = new QTreeWidgetItem(tree);
        songItem->setText(0, songName);
        songItem->setExpanded(songName == preferredSong);

        for (const auto &chart : charts)
        {
            QTreeWidgetItem *chartItem = new QTreeWidgetItem(songItem);
            chartItem->setText(0, QFileInfo(chart.first).fileName());
            chartItem->setText(1, chart.second);
            chartItem->setData(0, Qt::UserRole, chart.first);
            chartItem->setToolTip(0, chart.first);
        }

        if (songName == preferredSong && songItem->childCount() > 0)
            tree->setCurrentItem(songItem->child(0));
    }

    if (tree->topLevelItemCount() == 0)
    {
        QMessageBox::information(this, tr("No Charts"), tr("No imported .mc files were found in the local library."));
        return QString();
    }

    tree->expandToDepth(0);
    connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&dialog](QTreeWidgetItem *item, int)
            {
        if (!item->data(0, Qt::UserRole).toString().isEmpty())
            dialog.accept(); });

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    const int dialogResult = dialog.exec();
    Settings::instance().setChartPickerPrimaryColumnWidth(tree->columnWidth(0));
    if (dialogResult != QDialog::Accepted || tree->currentItem() == nullptr)
        return QString();

    const QString selectedPath = tree->currentItem()->data(0, Qt::UserRole).toString();
    if (selectedPath.isEmpty())
    {
        QMessageBox::information(this, tr("Select Chart"), tr("Please select a chart item, not a song folder."));
        return QString();
    }

    return selectedPath;
}

void MainWindow::switchDifficulty()
{
    if (!d->chartController || !d->chartController->chart())
    {
        QMessageBox::information(this, tr("No Chart"), tr("No chart is currently open."));
        return;
    }

    QString currentDir = QFileInfo(d->currentChartPath).absolutePath();
    QList<QPair<QString, QString>> charts = ProjectIO::findChartsInDirectory(currentDir);
    if (charts.size() <= 1)
    {
        QMessageBox::information(this, tr("No Other Charts"), tr("No other difficulties found in this directory."));
        return;
    }

    QList<QPair<QString, QString>> otherCharts;
    for (const auto &chart : charts)
    {
        if (chart.first != d->currentChartPath)
            otherCharts.append(chart);
    }
    if (otherCharts.isEmpty())
    {
        QMessageBox::information(this, tr("No Other Charts"), tr("No other difficulties found."));
        return;
    }

    QString newPath = selectChartFromList(otherCharts, tr("Switch Difficulty"));
    if (newPath.isEmpty())
        return;

    loadChartFile(newPath);
}

void MainWindow::saveChart()
{
    Logger::info("Save chart requested");
    QString currentPath = d->sourceChartPath;
    if (currentPath.isEmpty())
        currentPath = d->currentChartPath;
    if (currentPath.isEmpty())
    {
        currentPath = QFileDialog::getSaveFileName(
            this,
            tr("Save Chart As"),
            Settings::instance().lastOpenPath(),
            tr("Malody Catch Chart (*.mc);;All Files (*.*)"));
        if (currentPath.isEmpty())
        {
            Logger::debug("Save cancelled (empty current path and user cancelled Save As)");
            return;
        }
    }

    if (d->chartController->saveChart(currentPath))
    {
        d->sourceChartPath = currentPath;
        d->currentChartPath = currentPath;
        if (d->canvas)
            d->canvas->setSourceChartPath(currentPath);
        Settings::instance().setLastOpenPath(QFileInfo(currentPath).absolutePath());
        d->isModified = false;
        if (!d->workingChartPath.isEmpty())
            d->chartController->saveChart(d->workingChartPath);
        syncReferencedResourcesForSavedChart(d->workingChartPath, currentPath);
        syncSidecarDirectoryForChart(d->workingChartPath, currentPath);
        if (!d->workingChartPath.isEmpty())
            d->chartController->saveChart(d->workingChartPath);
        else
            createWorkingCopyFromSource(currentPath, &d->workingChartPath, nullptr);
        persistRecoveryState();
        statusBar()->showMessage(tr("Saved: %1").arg(currentPath), 2000);
        Logger::info("Chart saved: " + currentPath);
        if (PluginManager *pm = activePluginManager())
            pm->notifyChartSaved(currentPath);
    }
    else
    {
        Logger::error("Failed to save chart: " + currentPath);
        QMessageBox::critical(this, tr("Error"), tr("Failed to save chart."));
    }
}

void MainWindow::saveChartAs()
{
    Logger::info("Save chart as requested");
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Chart As"), Settings::instance().lastOpenPath(),
                                                    tr("Malody Catch Chart (*.mc);;All Files (*.*)"));
    if (fileName.isEmpty())
    {
        Logger::debug("Save as cancelled");
        return;
    }
    if (d->chartController->saveChart(fileName))
    {
        d->sourceChartPath = fileName;
        d->currentChartPath = fileName;
        if (d->canvas)
            d->canvas->setSourceChartPath(fileName);
        d->isModified = false;
        if (!d->workingChartPath.isEmpty())
            d->chartController->saveChart(d->workingChartPath);
        syncReferencedResourcesForSavedChart(d->workingChartPath, fileName);
        syncSidecarDirectoryForChart(d->workingChartPath, fileName);
        if (!d->workingChartPath.isEmpty())
            d->chartController->saveChart(d->workingChartPath);
        else
            createWorkingCopyFromSource(fileName, &d->workingChartPath, nullptr);
        persistRecoveryState();
        statusBar()->showMessage(tr("Saved: %1").arg(fileName), 2000);
        Logger::info("Chart saved as: " + fileName);
        if (PluginManager *pm = activePluginManager())
            pm->notifyChartSaved(fileName);
    }
    else
    {
        Logger::error("Failed to save chart as: " + fileName);
        QMessageBox::critical(this, tr("Error"), tr("Failed to save chart."));
    }
}

void MainWindow::exportMcz()
{
    exportMczInternal(false);
}

void MainWindow::exportMczPure()
{
    exportMczInternal(true);
}

void MainWindow::exportMczInternal(bool pureMode)
{
    Logger::info(pureMode ? "Export .mcz (pure) requested"
                          : "Export .mcz requested");

    if (d->currentChartPath.isEmpty())
    {
        QMessageBox::information(this, tr("No Chart"), tr("Please open a chart first before exporting."));
        return;
    }

    QString suggestedStem;
    if (d->chartController && d->chartController->chart())
        suggestedStem = sanitizeFileStem(d->chartController->chart()->meta().title);
    if (suggestedStem.isEmpty())
        suggestedStem = sanitizeFileStem(QFileInfo(d->currentChartPath).completeBaseName());
    if (suggestedStem.isEmpty())
        suggestedStem = "chart";

    QString initialDir;
    const QString lastPath = Settings::instance().lastOpenPath();
    if (!lastPath.isEmpty())
    {
        const QFileInfo lastInfo(lastPath);
        if (lastInfo.exists() && lastInfo.isDir())
            initialDir = lastInfo.absoluteFilePath();
        else if (!lastInfo.absolutePath().isEmpty())
            initialDir = lastInfo.absolutePath();
    }
    if (initialDir.isEmpty())
        initialDir = QFileInfo(d->currentChartPath).absolutePath();

    const QString initialPath = QDir(initialDir).filePath(suggestedStem + ".mcz");
    const QString dialogTitle = pureMode ? tr("Export .mcz (Pure)") : tr("Export .mcz");
    QString fileName = QFileDialog::getSaveFileName(this, dialogTitle, initialPath,
                                                    tr("Malody Catch Pack (*.mcz);;All Files (*.*)"));
    if (fileName.isEmpty())
    {
        Logger::debug(pureMode ? "Export .mcz (pure) cancelled" : "Export .mcz cancelled");
        return;
    }

    try
    {
        Logger::info(QString("MainWindow::exportMczInternal - Exporting to: %1 (mode=%2)")
                         .arg(fileName, pureMode ? "pure" : "full"));

        const bool ok = pureMode
                            ? ProjectIO::exportToMczPure(fileName, d->currentChartPath)
                            : ProjectIO::exportToMcz(fileName, d->currentChartPath);
        if (ok)
        {
            statusBar()->showMessage(tr("Exported: %1").arg(fileName), 3000);
            Logger::info(QString("MainWindow::exportMczInternal - Successfully exported to: %1").arg(fileName));
            QMessageBox::information(this, tr("Success"), tr("Chart exported successfully to:\n%1").arg(fileName));
        }
        else
        {
            Logger::error(QString("MainWindow::exportMczInternal - Failed to export to: %1").arg(fileName));
            QMessageBox::critical(this, tr("Error"), tr("Failed to export chart to MCZ format."));
        }
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("MainWindow::exportMczInternal - Exception: %1").arg(e.what()));
        QMessageBox::critical(this, tr("Error"), tr("Exception during export: %1").arg(e.what()));
    }
    catch (...)
    {
        Logger::error("MainWindow::exportMczInternal - Unknown exception");
        QMessageBox::critical(this, tr("Error"), tr("Unknown exception during export."));
    }
}

// ==================== Undo / Redo ====================
void MainWindow::undo()
{
    if (d->chartController)
    {
        Logger::debug("Undo triggered");
        const QString actionText = d->chartController->nextUndoActionText();
        d->chartController->undo();
        if (PluginManager *pm = activePluginManager())
            pm->notifyHostUndo(actionText);
    }
}

void MainWindow::redo()
{
    if (d->chartController)
    {
        Logger::debug("Redo triggered");
        const QString actionText = d->chartController->nextRedoActionText();
        d->chartController->redo();
        if (PluginManager *pm = activePluginManager())
            pm->notifyHostRedo(actionText);
    }
}

void MainWindow::toggleColorMode(bool on)
{
    Logger::info(QString("Color mode toggled to %1").arg(on));
    Settings::instance().setColorNoteEnabled(on);
    d->canvas->setColorMode(on);
    if (d->previewWidget)
        d->previewWidget->setColorMode(on);
}

void MainWindow::toggleTimelineDivisionColorMode(bool on)
{
    Logger::info(QString("Timeline division color mode toggled to %1").arg(on));
    Settings::instance().setTimelineDivisionColorEnabled(on);
    if (d->canvas)
        d->canvas->update();
}

void MainWindow::openTimelineDivisionColorSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Timeline Division Color Advanced Settings"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    const QColor baseBg = Settings::instance().backgroundColor();
    const QColor fg = sidebarTextColorFor(baseBg);
    const bool darkTheme = (fg.lightness() > 128);
    const QColor panelBg = darkTheme ? baseBg.lighter(108) : baseBg.darker(103);
    const QColor panelInputBg = darkTheme ? panelBg.lighter(120) : panelBg.darker(105);
    const QColor panelButtonBg = darkTheme ? panelBg.lighter(132) : panelBg.darker(112);
    const QColor panelBorder = darkTheme ? panelBg.lighter(165) : panelBg.darker(145);
    const QColor selectionText = darkTheme ? QColor(20, 20, 20) : QColor(245, 245, 245);
    dialog.setStyleSheet(QString(
                             "QDialog { background-color: %1; color: %2; }"
                             "QLabel, QCheckBox, QRadioButton, QGroupBox { color: %2; }"
                             "QGroupBox { border: 1px solid %4; margin-top: 8px; padding-top: 8px; }"
                             "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %2; }"
                             "QLineEdit, QComboBox, QListWidget { background-color: %3; color: %2; border: 1px solid %4; }"
                             "QListWidget::item:selected { background-color: %5; color: %6; }"
                             "QPushButton { background-color: %5; color: %2; border: 1px solid %4; padding: 3px 8px; }")
                             .arg(panelBg.name(), fg.name(), panelInputBg.name(),
                                  panelBorder.name(), panelButtonBg.name(), selectionText.name()));

    QCheckBox *enableCheck = new QCheckBox(tr("Enable Timeline Division Coloring"), &dialog);
    enableCheck->setChecked(Settings::instance().timelineDivisionColorEnabled());
    layout->addWidget(enableCheck);

    QFormLayout *form = new QFormLayout;
    QComboBox *presetCombo = new QComboBox(&dialog);
    presetCombo->addItem(tr("Custom"), "custom");
    presetCombo->addItem(tr("Classic"), "classic");
    presetCombo->addItem(tr("All"), "all");
    const QString preset = Settings::instance().timelineDivisionColorPreset().toLower();
    const int presetIndex = qMax(0, presetCombo->findData(preset));
    presetCombo->setCurrentIndex(presetIndex);
    form->addRow(tr("Preset:"), presetCombo);
    layout->addLayout(form);

    QGroupBox *customGroup = new QGroupBox(tr("Custom Rules"), &dialog);
    QVBoxLayout *customLayout = new QVBoxLayout(customGroup);

    QLabel *commonLabel = new QLabel(tr("Common divisions:"), customGroup);
    customLayout->addWidget(commonLabel);

    QWidget *commonWrap = new QWidget(customGroup);
    QHBoxLayout *commonLayout = new QHBoxLayout(commonWrap);
    commonLayout->setContentsMargins(0, 0, 0, 0);
    commonLayout->setSpacing(8);

    const QList<int> commonDivisions = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
    const QList<int> savedCustom = Settings::instance().timelineDivisionColorCustomDivisions();
    QHash<int, QCheckBox *> commonChecks;
    for (int div : commonDivisions)
    {
        QCheckBox *cb = new QCheckBox(QString("/%1").arg(div), commonWrap);
        cb->setChecked(savedCustom.contains(div));
        commonChecks.insert(div, cb);
        commonLayout->addWidget(cb);
    }
    commonLayout->addStretch(1);
    customLayout->addWidget(commonWrap);

    QLabel *extraLabel = new QLabel(tr("Extra divisions (manual):"), customGroup);
    customLayout->addWidget(extraLabel);

    QListWidget *extraList = new QListWidget(customGroup);
    for (int div : savedCustom)
    {
        if (!commonDivisions.contains(div))
            extraList->addItem(QString::number(div));
    }
    customLayout->addWidget(extraList);

    QHBoxLayout *addRow = new QHBoxLayout;
    QLineEdit *addEdit = new QLineEdit(customGroup);
    addEdit->setPlaceholderText(tr("Enter denominator, e.g. 48"));
    QPushButton *addBtn = new QPushButton(tr("Add"), customGroup);
    QPushButton *removeBtn = new QPushButton(tr("Remove Selected"), customGroup);
    addRow->addWidget(addEdit, 1);
    addRow->addWidget(addBtn);
    addRow->addWidget(removeBtn);
    customLayout->addLayout(addRow);

    layout->addWidget(customGroup);

    auto refreshCustomEnabled = [presetCombo, customGroup]()
    {
        const QString p = presetCombo->currentData().toString().toLower();
        customGroup->setEnabled(p == "custom");
    };
    refreshCustomEnabled();
    connect(presetCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [refreshCustomEnabled](int)
            { refreshCustomEnabled(); });

    connect(addBtn, &QPushButton::clicked, &dialog, [this, addEdit, extraList, commonDivisions]()
            {
        bool ok = false;
        const int v = addEdit->text().trimmed().toInt(&ok);
        if (!ok || v <= 0)
        {
            QMessageBox::warning(this, tr("Invalid Division"), tr("Please enter a positive integer denominator."));
            return;
        }
        if (commonDivisions.contains(v))
        {
            QMessageBox::information(this, tr("Already In Common List"), tr("This division is already in common rules. Please use its checkbox."));
            return;
        }
        for (int i = 0; i < extraList->count(); ++i)
        {
            if (extraList->item(i)->text().toInt() == v)
                return;
        }
        extraList->addItem(QString::number(v));
        addEdit->clear(); });

    connect(removeBtn, &QPushButton::clicked, &dialog, [extraList]()
            { qDeleteAll(extraList->selectedItems()); });

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const bool enabled = enableCheck->isChecked();
    const QString selectedPreset = presetCombo->currentData().toString().toLower();
    QList<int> customRules;
    for (int div : commonDivisions)
    {
        QCheckBox *cb = commonChecks.value(div, nullptr);
        if (cb && cb->isChecked())
            customRules.append(div);
    }
    for (int i = 0; i < extraList->count(); ++i)
    {
        bool ok = false;
        const int v = extraList->item(i)->text().toInt(&ok);
        if (ok && v > 0)
            customRules.append(v);
    }

    Settings::instance().setTimelineDivisionColorEnabled(enabled);
    Settings::instance().setTimelineDivisionColorPreset(selectedPreset);
    Settings::instance().setTimelineDivisionColorCustomDivisions(customRules);

    if (d->timelineDivisionColorAction)
        d->timelineDivisionColorAction->setChecked(enabled);
    if (d->canvas)
        d->canvas->update();
}

void MainWindow::toggleHyperfruitMode(bool on)
{
    Logger::info(QString("Hyperfruit mode toggled to %1").arg(on));
    Settings::instance().setHyperfruitOutlineEnabled(on);
    d->canvas->setHyperfruitEnabled(on);
    if (d->previewWidget)
        d->previewWidget->setHyperfruitEnabled(on);
}

void MainWindow::toggleVerticalFlip(bool flipped)
{
    Logger::info(QString("Vertical flip toggled to %1").arg(flipped));
    Settings::instance().setVerticalFlip(flipped);
    d->canvas->setVerticalFlip(flipped);
}

void MainWindow::updatePlaybackAvailability(bool canPlay)
{
    d->audioPlaybackReady = canPlay;
    if (d->playAction)
        d->playAction->setEnabled(canPlay);
}

void MainWindow::togglePlayback()
{
    if (!d->audioPlaybackReady)
    {
        statusBar()->showMessage(tr("Audio is not ready. Please reload a valid audio file."), 3000);
        return;
    }
    if (d->playbackController->state() == PlaybackController::Playing)
    {
        d->playbackController->pause();
        Logger::debug("Playback paused");
    }
    else
    {
        double startTime = d->canvas->currentPlayTime();
        const Chart *chart = d->chartController->chart();
        if (chart)
        {
            const QVector<BpmEntry> &bpmList = chart->bpmList();
            int offset = chart->meta().offset;
            int timeDivision = d->canvas ? d->canvas->timeDivision() : 4;
            startTime = MathUtils::snapTimeToGrid(startTime, bpmList, offset, timeDivision);
        }
        d->playbackController->playFromTime(startTime);
        if (d->playbackController->state() == PlaybackController::Playing)
            Logger::debug(QString("Playback started from %1ms").arg(startTime));
        else
            Logger::warn(QString("Playback start request rejected at %1ms").arg(startTime));
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Catch Chart Editor"));
    createMenus();
    if (d->mainToolBar)
        d->mainToolBar->setWindowTitle(tr("Tools"));
    if (d->pluginToolBar)
        d->pluginToolBar->setWindowTitle(tr("Plugins"));
    if (d->notePanelAction)
        d->notePanelAction->setText(tr("Note"));
    if (d->bpmPanelAction)
        d->bpmPanelAction->setText(tr("BPM"));
    if (d->metaPanelAction)
        d->metaPanelAction->setText(tr("Meta"));
    if (d->pluginManagerToolbarAction)
        d->pluginManagerToolbarAction->setText(tr("Plugins"));
    if (d->pluginToolModeToolbarAction)
        d->pluginToolModeToolbarAction->setText(tr("Launch Curve Tool"));
    if (d->leftPanel)
        d->leftPanel->retranslateUi();
    if (d->notePanel)
        d->notePanel->retranslateUi();
    if (d->bpmPanel)
        d->bpmPanel->retranslateUi();
    if (d->metaPanel)
        d->metaPanel->retranslateUi();
    applySidebarTheme();
    Logger::debug("UI retranslated");
}

void MainWindow::showEditorPanel(QWidget *panel)
{
    if (!panel)
        return;

    d->notePanel->setVisible(panel == d->notePanel);
    d->bpmPanel->setVisible(panel == d->bpmPanel);
    d->metaPanel->setVisible(panel == d->metaPanel);
    if (panel == d->notePanel)
        d->currentRightPanel = d->notePanel;
    else if (panel == d->bpmPanel)
        d->currentRightPanel = d->bpmPanel;
    else if (panel == d->metaPanel)
        d->currentRightPanel = d->metaPanel;

    if (d->rightPanelContainer)
        d->rightPanelContainer->setVisible(true);
}

// ==================== Paste 288 division option slot ====================
void MainWindow::togglePaste288Division(bool enabled)
{
    Settings::instance().setPasteUse288Division(enabled);
    Logger::info(QString("Paste 288 division: %1").arg(enabled ? "enabled" : "disabled"));
}

void MainWindow::changeLanguage()
{
    QAction *act = qobject_cast<QAction *>(sender());
    if (!act)
        return;

    const QString languageCode = act->data().toString();
    const QString languageName = act->text();
    if (languageCode.isEmpty() || languageCode == Settings::instance().language())
        return;

    if (!Translator::instance().setLanguage(languageCode))
    {
        QMessageBox::warning(this, tr("Language"), tr("Failed to load language pack: %1").arg(languageCode));
        return;
    }

    Settings::instance().setLanguage(languageCode);

    QTimer::singleShot(0, this, [this]()
                       {
        if (PluginManager *pm = activePluginManager())
        {
            pm->reloadPlugins();
            refreshPluginUiExtensions();
        } });

    statusBar()->showMessage(tr("Language changed to %1").arg(languageName), 2000);
}

void MainWindow::checkForUpdates()
{
    statusBar()->showMessage(tr("Checking for updates..."), 2000);

    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.github.com/repos/ChuanYuanNotBoat/Malody_Catch_Editor/releases/latest"));
    request.setHeader(QNetworkRequest::UserAgentHeader, "CatchChartEditor Update Checker");
    request.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]()
            {
        const QString currentVersion = QCoreApplication::applicationVersion();
        if (reply->error() != QNetworkReply::NoError)
        {
            QMessageBox::warning(this,
                                 tr("Check for Updates"),
                                 tr("Update check failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            manager->deleteLater();
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject obj = doc.object();
        const QString latestTag = obj.value("tag_name").toString();
        const QString latestName = obj.value("name").toString();
        const QString latestVersion = latestTag.isEmpty() ? latestName : latestTag;
        const QString releaseUrl = obj.value("html_url").toString("https://github.com/ChuanYuanNotBoat/Malody_Catch_Editor/releases/latest");

        const int cmp = compareSemver(currentVersion, latestVersion);
        if (cmp < 0)
        {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Information);
            box.setWindowTitle(tr("Update Available"));
            box.setText(tr("A newer version is available."));
            box.setInformativeText(tr("Current: %1\nLatest: %2\n\nOpen release page?")
                                       .arg(currentVersion, latestVersion));
            QPushButton *openButton = box.addButton(tr("Open Release Page"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() == openButton)
                QDesktopServices::openUrl(QUrl(releaseUrl));
        }
        else if (cmp == 0)
        {
            QMessageBox::information(this,
                                     tr("Check for Updates"),
                                     tr("You are using the latest version.\nCurrent: %1").arg(currentVersion));
        }
        else
        {
            QMessageBox::information(this,
                                     tr("Check for Updates"),
                                     tr("Current version appears newer than latest release.\nCurrent: %1\nLatest: %2")
                                         .arg(currentVersion, latestVersion));
        }

        reply->deleteLater();
        manager->deleteLater(); });
}

void MainWindow::showHelpPage()
{
    showInfoCenter(0);
}

void MainWindow::showAboutPage()
{
    showInfoCenter(1);
}

void MainWindow::showVersionPage()
{
    showInfoCenter(2);
}

void MainWindow::showLogsPage()
{
    showInfoCenter(4);
}

void MainWindow::showInfoCenter(int initialTab)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Help Center"));
    dialog.resize(840, 600);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QTabWidget *tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs, 1);

    const QString docsDir = QCoreApplication::applicationDirPath() + "/docs";

    // Help tab: user-facing documentation.
    QTextBrowser *helpBrowser = new QTextBrowser(&dialog);
    helpBrowser->setOpenExternalLinks(true);
    setBrowserContentFromDoc(
        helpBrowser,
        QStringList{docsDir + "/help.md"},
        tr("# Help Documentation\n\n"
           "Create a `docs/help.md` file to customize this page.\n\n"
           "Quick start:\n"
           "1. Open a `.mc` chart.\n"
           "2. Choose edit mode in the right panel.\n"
           "3. Edit notes and save/export `.mcz`."));
    tabs->addTab(helpBrowser, tr("Help"));

    // About tab: keep it flexible for long-form "mixed" content.
    QTextBrowser *aboutBrowser = new QTextBrowser(&dialog);
    aboutBrowser->setOpenExternalLinks(true);
    setBrowserContentFromDoc(
        aboutBrowser,
        QStringList{docsDir + "/about.md"},
        tr("# About\n\n"
           "Create a `docs/about.md` file to customize this page."));
    tabs->addTab(aboutBrowser, tr("About"));

    // Version tab: show build/runtime version information.
    QTextBrowser *versionBrowser = new QTextBrowser(&dialog);
    versionBrowser->setOpenExternalLinks(true);
    const QString versionDoc = loadDocText(QStringList{docsDir + "/version.md"});
    const QString historyDoc = loadDocText(QStringList{
        docsDir + "/history.md",
        docsDir + "/changelog.md"});
    QString versionMarkdown = QString("# %1\n\n"
                                      "- **%2** %3\n"
                                      "- **%4** %5\n"
                                      "- **%6** %7\n"
                                      "- **%8** %9\n\n")
                                  .arg(QCoreApplication::applicationName(),
                                       tr("Application Version:"),
                                       QCoreApplication::applicationVersion(),
                                       tr("Qt Runtime:"),
                                       qVersion(),
                                       tr("Build ABI:"),
                                       QSysInfo::buildAbi(),
                                       tr("Operating System:"),
                                       QSysInfo::prettyProductName());
    if (!versionDoc.trimmed().isEmpty())
    {
        versionMarkdown += tr("## Version Notes\n\n");
        versionMarkdown += versionDoc;
        versionMarkdown += "\n\n";
    }
    versionMarkdown += tr("## History Updates\n\n"
                          "Open the **History** tab for collapsible long update notes.");
    versionBrowser->setMarkdown(versionMarkdown);
    tabs->addTab(versionBrowser, tr("Version"));

    // History tab: collapsible long update notes.
    QWidget *historyTab = new QWidget(&dialog);
    QVBoxLayout *historyLayout = new QVBoxLayout(historyTab);
    QLabel *historyHint = new QLabel(tr("Long update notes are grouped by prefix and version and can be collapsed."), historyTab);
    historyHint->setWordWrap(true);
    historyLayout->addWidget(historyHint);

    QTreeWidget *historyTree = new QTreeWidget(historyTab);
    historyTree->setHeaderHidden(true);
    historyLayout->addWidget(historyTree, 1);

    QHBoxLayout *historyButtons = new QHBoxLayout;
    QPushButton *expandAllBtn = new QPushButton(tr("Expand All"), historyTab);
    QPushButton *collapseAllBtn = new QPushButton(tr("Collapse All"), historyTab);
    historyButtons->addWidget(expandAllBtn);
    historyButtons->addWidget(collapseAllBtn);
    historyButtons->addStretch(1);
    historyLayout->addLayout(historyButtons);

    const QList<HistorySection> sections = parseHistorySections(historyDoc);
    const QList<HistoryPrefixGroup> groups = groupHistorySectionsByPrefix(sections);
    if (groups.isEmpty())
    {
        auto *emptyItem = new QTreeWidgetItem(historyTree);
        emptyItem->setText(0, tr("No history document found. Put one in `docs/history.md` or `docs/changelog.md`."));
    }
    else
    {
        for (int g = 0; g < groups.size(); ++g)
        {
            const HistoryPrefixGroup &group = groups[g];
            auto *groupItem = new QTreeWidgetItem(historyTree);
            groupItem->setText(0, group.label);

            for (int i = 0; i < group.sections.size(); ++i)
            {
                const HistorySection &section = group.sections[i];
                auto *sectionItem = new QTreeWidgetItem(groupItem);
                sectionItem->setText(0, section.title);
                for (const QString &line : section.lines)
                {
                    auto *lineItem = new QTreeWidgetItem(sectionItem);
                    lineItem->setText(0, line);
                }
                sectionItem->setExpanded(g == 0 && i == 0);
            }
            groupItem->setExpanded(g == 0);
        }
    }

    connect(expandAllBtn, &QPushButton::clicked, historyTree, &QTreeWidget::expandAll);
    connect(collapseAllBtn, &QPushButton::clicked, historyTree, &QTreeWidget::collapseAll);
    tabs->addTab(historyTab, tr("History"));

    // Logs tab: quick log access and overview.
    QWidget *logTab = new QWidget(&dialog);
    QVBoxLayout *logLayout = new QVBoxLayout(logTab);
    QLabel *logHint = new QLabel(tr("Logs are generated in the application 'logs' directory."), logTab);
    logHint->setWordWrap(true);
    logLayout->addWidget(logHint);

    QTableWidget *logTable = new QTableWidget(logTab);
    logTable->setColumnCount(3);
    logTable->setHorizontalHeaderLabels({tr("File"), tr("Size (KB)"), tr("Modified")});
    logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    logLayout->addWidget(logTable, 1);

    QHBoxLayout *logButtons = new QHBoxLayout;
    QPushButton *refreshBtn = new QPushButton(tr("Refresh"), logTab);
    QPushButton *openSelectedBtn = new QPushButton(tr("Open Selected Log"), logTab);
    QPushButton *openCurrentBtn = new QPushButton(tr("Open Current Log"), logTab);
    QPushButton *openDirBtn = new QPushButton(tr("Open Log Folder"), logTab);
    logButtons->addWidget(refreshBtn);
    logButtons->addWidget(openSelectedBtn);
    logButtons->addWidget(openCurrentBtn);
    logButtons->addWidget(openDirBtn);
    logButtons->addStretch(1);
    logLayout->addLayout(logButtons);
    tabs->addTab(logTab, tr("Logs"));

    auto logsDirPath = [this]() -> QString
    {
        const QString currentLog = Logger::logFilePath();
        if (!currentLog.isEmpty())
            return QFileInfo(currentLog).absolutePath();
        return QCoreApplication::applicationDirPath() + "/logs";
    };

    auto refreshLogs = [logTable, logsDirPath]()
    {
        const QDir dir(logsDirPath());
        const QFileInfoList files = dir.entryInfoList(
            QStringList() << "*.log" << "*.jsonl",
            QDir::Files | QDir::NoSymLinks,
            QDir::Time);

        logTable->setRowCount(files.size());
        for (int i = 0; i < files.size(); ++i)
        {
            const QFileInfo &fi = files[i];
            auto *nameItem = new QTableWidgetItem(fi.fileName());
            nameItem->setData(Qt::UserRole, fi.absoluteFilePath());
            auto *sizeItem = new QTableWidgetItem(QString::number(fi.size() / 1024.0, 'f', 1));
            auto *timeItem = new QTableWidgetItem(fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"));
            logTable->setItem(i, 0, nameItem);
            logTable->setItem(i, 1, sizeItem);
            logTable->setItem(i, 2, timeItem);
        }
        if (logTable->rowCount() > 0)
            logTable->selectRow(0);
    };

    connect(refreshBtn, &QPushButton::clicked, &dialog, refreshLogs);
    connect(openSelectedBtn, &QPushButton::clicked, &dialog, [this, logTable]()
            {
        const int row = logTable->currentRow();
        if (row < 0 || !logTable->item(row, 0))
            return;
        const QString path = logTable->item(row, 0)->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else
            QMessageBox::information(this, tr("Logs"), tr("No log file selected.")); });
    connect(openCurrentBtn, &QPushButton::clicked, &dialog, [this]()
            {
        const QString currentLog = Logger::logFilePath();
        if (currentLog.isEmpty())
        {
            QMessageBox::information(this, tr("Logs"), tr("Current log file is not available yet."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(currentLog)); });
    connect(openDirBtn, &QPushButton::clicked, &dialog, [this, logsDirPath]()
            {
        const QString dir = logsDirPath();
        if (!QFileInfo::exists(dir))
        {
            QMessageBox::information(this, tr("Logs"), tr("Log folder does not exist yet."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); });

    refreshLogs();
    if (initialTab >= 0 && initialTab < tabs->count())
        tabs->setCurrentIndex(initialTab);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::applySidebarTheme()
{
    const QColor bg = Settings::instance().backgroundColor();
    const QColor fg = sidebarTextColorFor(bg);
    const bool darkTheme = (fg.lightness() > 128);
    const QColor panelBg = darkTheme ? bg.lighter(108) : bg.darker(103);
    const QColor panelInputBg = darkTheme ? panelBg.lighter(120) : panelBg.darker(105);
    const QColor panelButtonBg = darkTheme ? panelBg.lighter(132) : panelBg.darker(112);
    const QColor panelButtonHoverBg = darkTheme ? panelButtonBg.lighter(120) : panelButtonBg.lighter(108);
    const QColor panelButtonPressedBg = darkTheme ? panelButtonBg.darker(118) : panelButtonBg.darker(118);
    const QColor panelBorder = darkTheme ? panelBg.lighter(165) : panelBg.darker(145);
    const QColor panelDisabledText = darkTheme ? QColor("#9A9A9A") : QColor("#707070");

    auto applyPanelStyle = [&](QWidget *panel, const QString &rootName)
    {
        if (!panel)
            return;

        const QString css = QString(
                                "QWidget#%9 { background-color: %1; color: %2; border: 1px solid %4; }"
                                "QLabel, QCheckBox, QRadioButton, QGroupBox { color: %2; }"
                                "QGroupBox { border: 1px solid %4; border-radius: 6px; margin-top: 8px; padding-top: 10px; }"
                                "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %2; }"
                                "QLineEdit, QAbstractSpinBox, QComboBox, QListWidget, QTextEdit, QPlainTextEdit {"
                                "  background-color: %3; color: %2; border: 1px solid %4; }"
                                "QAbstractItemView { background-color: %3; color: %2; border: 1px solid %4; selection-background-color: %5; selection-color: %6; }"
                                "QPushButton { background-color: %5; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
                                "QPushButton:hover { background-color: %7; }"
                                "QPushButton:pressed { background-color: %8; }"
                                "QPushButton:disabled { color: %6; }"
                                "QToolButton { background-color: %5; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
                                "QToolButton:hover { background-color: %7; }"
                                "QToolButton:pressed { background-color: %8; }"
                                "QToolButton:checked { background-color: %7; }"
                                "QToolButton:disabled { color: %6; }"
                                "QScrollBar:vertical, QScrollBar:horizontal { background-color: %1; }")
                                .arg(panelBg.name(), fg.name(), panelInputBg.name(), panelBorder.name(), panelButtonBg.name(),
                                     panelDisabledText.name(), panelButtonHoverBg.name(), panelButtonPressedBg.name(), rootName);

        panel->setStyleSheet(css);
    };

    applyPanelStyle(d->leftPanel, "leftPanelRoot");
    applyPanelStyle(d->rightPanelContainer, "rightPanelRoot");

    if (menuBar())
    {
        const QString menuCss = QString(
                                    "QMenuBar { background-color: %1; color: %2; }"
                                    "QMenuBar::item { background: transparent; color: %2; padding: 4px 8px; }"
                                    "QMenuBar::item:selected { background: %3; }"
                                    "QMenu { background-color: %1; color: %2; border: 1px solid %4; }"
                                    "QMenu::item:selected { background-color: %3; }")
                                    .arg(bg.name(), fg.name(), panelButtonBg.name(), panelBorder.name());
        menuBar()->setStyleSheet(menuCss);
    }

    if (d->mainToolBar)
    {
        const QString toolbarCss = QString(
                                       "QToolBar { background-color: %1; color: %2; border-bottom: 1px solid %4; border-top: 1px solid %4; spacing: 6px; padding: 2px 4px; }"
                                       "QToolButton { background-color: %5; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
                                       "QToolButton:hover { background-color: %6; }"
                                       "QToolButton:pressed { background-color: %7; }")
                                       .arg(panelBg.name(), fg.name(), panelInputBg.name(), panelBorder.name(), panelButtonBg.name(),
                                            panelButtonHoverBg.name(), panelButtonPressedBg.name());
        d->mainToolBar->setStyleSheet(toolbarCss);
        if (d->pluginToolBar)
            d->pluginToolBar->setStyleSheet(toolbarCss);
    }

    if (statusBar())
    {
        statusBar()->setStyleSheet(QString("QStatusBar { background-color: %1; color: %2; border-top: 1px solid %3; }")
                                       .arg(panelBg.name(), fg.name(), panelBorder.name()));
    }

    if (d->splitter)
    {
        d->splitter->setStyleSheet(QString("QSplitter::handle { background-color: %1; }").arg(panelBorder.name()));
    }

    if (d->rightDensityBar)
        d->rightDensityBar->update();

    // Global dialog/message box theming. This keeps popups readable on dark backgrounds.
    const QString dialogCss = QString(
                                  "QDialog, QMessageBox, QInputDialog { background-color: %1; color: %2; }"
                                  "QDialog QLabel, QMessageBox QLabel, QInputDialog QLabel, QDialog QGroupBox { color: %2; }"
                                  "QDialog QLabel[hintText=\"true\"] { color: %6; }"
                                  "QDialog QLineEdit, QDialog QTextEdit, QDialog QPlainTextEdit, QDialog QComboBox,"
                                  "QDialog QAbstractSpinBox, QDialog QListWidget, QDialog QTreeWidget, QDialog QTableWidget,"
                                  "QMessageBox QLineEdit, QInputDialog QLineEdit {"
                                  "  background-color: %3; color: %2; border: 1px solid %4; }"
                                  "QDialog QAbstractItemView, QMessageBox QAbstractItemView, QInputDialog QAbstractItemView {"
                                  "  background-color: %3; color: %2; border: 1px solid %4;"
                                  "  selection-background-color: %5; selection-color: %6; }"
                                  "QDialog QPushButton, QMessageBox QPushButton, QInputDialog QPushButton {"
                                  "  background-color: %5; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
                                  "QDialog QPushButton:hover, QMessageBox QPushButton:hover, QInputDialog QPushButton:hover {"
                                  "  background-color: %7; }"
                                  "QDialog QPushButton:pressed, QMessageBox QPushButton:pressed, QInputDialog QPushButton:pressed {"
                                  "  background-color: %8; }"
                                  "QDialog QPushButton:disabled, QMessageBox QPushButton:disabled, QInputDialog QPushButton:disabled {"
                                  "  color: %6; }"
                                  "QDialog QMenuBar, QDialog QMenu { background-color: %1; color: %2; }"
                                  "QDialog QTabWidget::pane { border: 1px solid %4; }")
                                  .arg(panelBg.name(), fg.name(), panelInputBg.name(), panelBorder.name(), panelButtonBg.name(),
                                       panelDisabledText.name(), panelButtonHoverBg.name(), panelButtonPressedBg.name());
    qApp->setStyleSheet(dialogCss);
}
