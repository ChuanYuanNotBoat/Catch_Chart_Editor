#include "ProjectIO.h"
#include "utils/Logger.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QProcessEnvironment>
#include <QDirIterator>
#include <QSet>

namespace
{
bool copyFileKeepingStructure(const QString &baseDir, const QString &relativePath, const QString &destRoot);

QString normalizedRelativePath(const QString &baseDir, const QString &pathLike)
{
    QString p = QDir::fromNativeSeparators(pathLike).trimmed();
    if (p.isEmpty())
        return QString();
    p = QDir::cleanPath(p);
    if (p.isEmpty() || p == ".")
        return QString();
    if (QDir::isAbsolutePath(p))
        return QString();
    if (p.startsWith("../") || p == "..")
        return QString();
    const QString abs = QDir(baseDir).absoluteFilePath(p);
    const QString rel = QDir(baseDir).relativeFilePath(abs);
    if (rel.startsWith("../") || rel == "..")
        return QString();
    return QDir::cleanPath(rel);
}

bool isAllowedAssociatedFile(const QString &relativePath)
{
    const QString ext = QFileInfo(relativePath).suffix().toLower();
    if (ext == "mc")
        return true;
    static const QSet<QString> kAllowedAssetExt = {
        "ogg", "mp3", "wav", "flac", "m4a", "aac",
        "jpg", "jpeg", "png", "bmp", "webp", "gif",
        "mp4", "mkv", "avi", "webm", "mov"};
    return kAllowedAssetExt.contains(ext);
}

void collectReferencedFilesFromMc(const QString &mcAbsPath, const QString &baseDir, QSet<QString> &outFiles)
{
    QFile f(mcAbsPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;

    const QJsonObject root = doc.object();
    const QJsonObject meta = root.value("meta").toObject();

    const auto addRef = [&](const QString &ref) {
        const QString rel = normalizedRelativePath(baseDir, ref);
        if (!rel.isEmpty() && isAllowedAssociatedFile(rel))
            outFiles.insert(rel);
    };

    addRef(meta.value("audio").toString());
    addRef(meta.value("background").toString());

    const QJsonArray notes = root.value("note").toArray();
    for (const QJsonValue &v : notes)
    {
        const QJsonObject obj = v.toObject();
        if (obj.value("type").toInt(0) == 1)
            addRef(obj.value("sound").toString());
    }
}

void collectAllFilesFromDirectory(const QString &baseDir,
                                  const QString &outputMczPath,
                                  QSet<QString> &outFiles)
{
    const QString excludedOutput = QFileInfo(outputMczPath).absoluteFilePath();
    QDirIterator it(baseDir,
                    QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString absPath = it.next();
        if (QFileInfo(absPath).absoluteFilePath() == excludedOutput)
            continue;
        const QString rel = QDir(baseDir).relativeFilePath(absPath);
        const QString cleanRel = QDir::cleanPath(rel);
        if (cleanRel.startsWith("../") || cleanRel == "..")
            continue;
        outFiles.insert(cleanRel);
    }
}

bool packSelectedFilesToMcz(const QString &outputMczPath,
                            const QString &chartBaseDir,
                            const QSet<QString> &selectedRelativeFiles,
                            const QString &logTag)
{
    if (selectedRelativeFiles.isEmpty())
    {
        Logger::error(QString("%1 - No files selected for export").arg(logTag));
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
    {
        Logger::error(QString("%1 - Failed to create temporary directory").arg(logTag));
        return false;
    }

    const QString packRootDir = tempDir.path() + "/mczpack";
    const QString payloadDir = packRootDir + "/0";
    if (!QDir().mkpath(payloadDir))
    {
        Logger::error(QString("%1 - Failed to create payload directory").arg(logTag));
        return false;
    }

    int copiedCount = 0;
    for (const QString &rel : selectedRelativeFiles)
    {
        if (copyFileKeepingStructure(chartBaseDir, rel, payloadDir))
            copiedCount++;
    }

    if (copiedCount <= 0)
    {
        Logger::error(QString("%1 - No files copied into payload directory").arg(logTag));
        return false;
    }

    QString tempZipPath = tempDir.path() + "/output.zip";
    QProcess process;

#ifdef Q_OS_WIN
    // 使用 ZipArchive 明确生成 '/' 分隔的 entry，避免 Windows '\' 路径导致 MCZ 兼容性问题。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("MALODY_MCZ_PACK_DIR", QDir::toNativeSeparators(packRootDir));
    env.insert("MALODY_MCZ_TEMP_ZIP", QDir::toNativeSeparators(tempZipPath));
    process.setProcessEnvironment(env);

    QStringList args;
    args << "-NoProfile"
         << "-NonInteractive"
         << "-Command"
         << "$ErrorActionPreference='Stop'; "
            "$src = $env:MALODY_MCZ_PACK_DIR; "
            "$dst = $env:MALODY_MCZ_TEMP_ZIP; "
            "if ([string]::IsNullOrWhiteSpace($src) -or [string]::IsNullOrWhiteSpace($dst)) "
            "{ throw 'Missing export paths in environment.' }; "
            "if (-not (Test-Path -LiteralPath $src)) "
            "{ throw ('Pack directory not found: ' + $src) }; "
            "$items = Get-ChildItem -LiteralPath $src -Recurse -File -Force; "
            "if ($null -eq $items -or $items.Count -eq 0) "
            "{ throw ('Pack directory is empty: ' + $src) }; "
            "if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }; "
            "Add-Type -AssemblyName 'System.IO.Compression'; "
            "Add-Type -AssemblyName 'System.IO.Compression.FileSystem'; "
            "$base = (Resolve-Path -LiteralPath $src).Path; "
            "if (-not $base.EndsWith('\\')) { $base = $base + '\\' }; "
            "$fs = [System.IO.File]::Open($dst, [System.IO.FileMode]::Create); "
            "try { "
            "  $zip = New-Object System.IO.Compression.ZipArchive($fs, [System.IO.Compression.ZipArchiveMode]::Create, $false); "
            "  try { "
            "    foreach ($f in $items) { "
            "      $full = (Resolve-Path -LiteralPath $f.FullName).Path; "
            "      if (-not $full.StartsWith($base, [System.StringComparison]::OrdinalIgnoreCase)) { continue }; "
            "      $entryName = $full.Substring($base.Length) -replace '\\\\','/'; "
            "      $entry = $zip.CreateEntry($entryName, [System.IO.Compression.CompressionLevel]::Optimal); "
            "      $entryStream = $entry.Open(); "
            "      try { "
            "        $in = [System.IO.File]::OpenRead($f.FullName); "
            "        try { $in.CopyTo($entryStream) } finally { $in.Dispose() } "
            "      } finally { $entryStream.Dispose() } "
            "    } "
            "  } finally { $zip.Dispose() } "
            "} finally { $fs.Dispose() }";
    process.start("powershell.exe", args);
#else
    process.setWorkingDirectory(packRootDir);
    process.start("zip", QStringList() << "-r" << "-q" << tempZipPath << ".");
#endif

    if (!process.waitForFinished(60000))
    {
        Logger::error(QString("%1 - Zip process timeout").arg(logTag));
        process.kill();
        return false;
    }

    if (process.exitCode() != 0)
    {
        QString errMsg = process.readAllStandardError();
        Logger::error(QString("%1 - Zip failed: %2").arg(logTag, errMsg));
        return false;
    }

    if (QFile::exists(outputMczPath))
        QFile::remove(outputMczPath);

    if (!QFile::rename(tempZipPath, outputMczPath))
    {
        Logger::error(QString("%1 - Failed to rename zip to mcz").arg(logTag));
        return false;
    }

    Logger::info(QString("%1 - Export successful (copied %2 files under 0/)")
                     .arg(logTag)
                     .arg(copiedCount));
    return true;
}

bool copyFileKeepingStructure(const QString &baseDir, const QString &relativePath, const QString &destRoot)
{
    const QString src = QDir(baseDir).absoluteFilePath(relativePath);
    if (!QFileInfo::exists(src) || !QFileInfo(src).isFile())
        return false;

    const QString dst = QDir(destRoot).absoluteFilePath(relativePath);
    const QString dstDir = QFileInfo(dst).absolutePath();
    if (!QDir().mkpath(dstDir))
        return false;
    QFile::remove(dst);
    return QFile::copy(src, dst);
}
}

bool ProjectIO::extractMcz(const QString &mczPath, const QString &outputDir, QString &outExtractedDir)
{
    Logger::info(QString("ProjectIO::extractMcz - Extracting %1 to %2").arg(mczPath, outputDir));

    QDir outDir(outputDir);
    if (!outDir.exists() && !outDir.mkpath("."))
    {
        Logger::error(QString("ProjectIO::extractMcz - Failed to create output directory: %1").arg(outputDir));
        return false;
    }

    if (!QFile::exists(mczPath))
    {
        Logger::error(QString("ProjectIO::extractMcz - Source file does not exist: %1").arg(mczPath));
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(outputDir);

#ifdef Q_OS_WIN
    QTemporaryDir tempZipDir;
    if (!tempZipDir.isValid())
    {
        Logger::error("ProjectIO::extractMcz - Failed to create temporary directory for ZIP conversion");
        return false;
    }

    const QString tempZipPath = QDir(tempZipDir.path()).filePath("mcz_extract.zip");

    if (!QFile::copy(mczPath, tempZipPath))
    {
        Logger::error(QString("ProjectIO::extractMcz - Failed to copy MCZ to temporary ZIP: src=%1, dst=%2")
                          .arg(mczPath, tempZipPath));
        return false;
    }

    // 固定命令 + 位置参数，避免命令拼接带来的 PowerShell 注入问题。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("MALODY_MCZ_TEMP_ZIP", QDir::toNativeSeparators(tempZipPath));
    env.insert("MALODY_MCZ_OUTPUT_DIR", QDir::toNativeSeparators(outputDir));
    process.setProcessEnvironment(env);

    QStringList args;
    args << "-NoProfile"
         << "-NonInteractive"
         << "-Command"
         << "$ErrorActionPreference='Stop'; "
            "$zip = $env:MALODY_MCZ_TEMP_ZIP; "
            "$dst = $env:MALODY_MCZ_OUTPUT_DIR; "
            "if ([string]::IsNullOrWhiteSpace($zip) -or [string]::IsNullOrWhiteSpace($dst)) "
            "{ throw 'Missing extraction paths in environment.' }; "
            "Expand-Archive -LiteralPath $zip -DestinationPath $dst -Force; "
            "Remove-Item -LiteralPath $zip -Force";
    process.start("powershell.exe", args);
#else
    process.start("unzip", QStringList() << "-o" << mczPath << "-d" << outputDir);
#endif

    if (!process.waitForFinished(30000))
    {
        Logger::error("ProjectIO::extractMcz - Extraction timeout");
        process.kill();
#ifdef Q_OS_WIN
        QFile::remove(tempZipPath);
#endif
        return false;
    }

    if (process.exitCode() != 0)
    {
        QString errMsg = process.readAllStandardError();
        Logger::error(QString("ProjectIO::extractMcz - Extraction failed: %1").arg(errMsg));
#ifdef Q_OS_WIN
        QFile::remove(tempZipPath);
#endif
        return false;
    }

#ifdef Q_OS_WIN
    if (QFile::exists(tempZipPath))
        QFile::remove(tempZipPath);
#endif

    outExtractedDir = outputDir;
    Logger::info(QString("ProjectIO::extractMcz - Successfully extracted to %1").arg(outputDir));
    return true;
}

bool ProjectIO::exportToMcz(const QString &outputMczPath, const QString &sourceChartPath)
{
    Logger::info(QString("ProjectIO::exportToMcz - Exporting to %1 from %2").arg(outputMczPath, sourceChartPath));

    if (!QFile::exists(sourceChartPath))
    {
        Logger::error(QString("ProjectIO::exportToMcz - Chart file not found: %1").arg(sourceChartPath));
        return false;
    }

    const QString chartDir = QFileInfo(sourceChartPath).absolutePath();
    const QString chartBaseDir = QDir(chartDir).absolutePath();
    QSet<QString> selectedRelativeFiles;
    collectAllFilesFromDirectory(chartBaseDir, outputMczPath, selectedRelativeFiles);
    return packSelectedFilesToMcz(outputMczPath, chartBaseDir, selectedRelativeFiles, "ProjectIO::exportToMcz");
}

bool ProjectIO::exportToMczPure(const QString &outputMczPath, const QString &sourceChartPath)
{
    Logger::info(QString("ProjectIO::exportToMczPure - Exporting to %1 from %2").arg(outputMczPath, sourceChartPath));

    if (!QFile::exists(sourceChartPath))
    {
        Logger::error(QString("ProjectIO::exportToMczPure - Chart file not found: %1").arg(sourceChartPath));
        return false;
    }

    const QString chartDir = QFileInfo(sourceChartPath).absolutePath();
    const QString chartBaseDir = QDir(chartDir).absolutePath();

    // 收集允许打包的文件：
    // 1) 所有 .mc（包括不同难度，文件名不做限制）
    // 2) 每个 .mc 中引用到的音频/背景/sound 资源
    QSet<QString> selectedRelativeFiles;
    QDirIterator it(chartBaseDir, QStringList() << "*.mc", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString mcAbsPath = it.next();
        const QString rel = QDir(chartBaseDir).relativeFilePath(mcAbsPath);
        const QString cleanRel = QDir::cleanPath(rel);
        if (cleanRel.startsWith("../") || cleanRel == "..")
            continue;
        selectedRelativeFiles.insert(cleanRel);
        collectReferencedFilesFromMc(mcAbsPath, chartBaseDir, selectedRelativeFiles);
    }

    if (selectedRelativeFiles.isEmpty())
    {
        Logger::error("ProjectIO::exportToMczPure - No eligible .mc found under chart directory");
        return false;
    }

    QSet<QString> filteredFiles;
    for (const QString &rel : selectedRelativeFiles)
    {
        if (isAllowedAssociatedFile(rel))
            filteredFiles.insert(rel);
    }

    return packSelectedFilesToMcz(outputMczPath, chartBaseDir, filteredFiles, "ProjectIO::exportToMczPure");
}

QList<QPair<QString, QString>> ProjectIO::findChartsInDirectory(const QString &dirPath)
{
    QList<QPair<QString, QString>> charts;
    QDir dir(dirPath);
    if (!dir.exists())
        return charts;

    // 扫描当前目录下的 .mc 文件
    QStringList mcFiles = dir.entryList(QStringList() << "*.mc", QDir::Files);
    for (const QString &file : mcFiles)
    {
        QString fullPath = dir.absoluteFilePath(file);
        QString difficulty = getDifficultyFromMc(fullPath);
        if (difficulty.isEmpty())
            difficulty = QFileInfo(fullPath).baseName();
        charts.append(qMakePair(fullPath, difficulty));
    }

    // 递归子目录
    QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subDir : subDirs)
    {
        charts.append(findChartsInDirectory(dir.absoluteFilePath(subDir)));
    }

    return charts;
}

QString ProjectIO::getDifficultyFromMc(const QString &mcPath)
{
    QFile file(mcPath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isObject())
    {
        QJsonObject root = doc.object();
        QJsonObject meta = root.value("meta").toObject();
        // Malody 使用 "version" 字段存储难度名
        return meta.value("version").toString();
    }
    return QString();
}
