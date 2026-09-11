#include "Skin.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include "utils/FileUtils.h"
#include "utils/Logger.h"

Skin::Skin() : m_valid(false), m_skinPath("")
{
    // 初始化默认缩放为 0.25
    for (int i = 0; i <= 5; ++i)
        m_noteScales[i] = 0.25;
}

bool Skin::loadFromDir(const QString &skinPath)
{
    clear();
    m_skinPath = skinPath;
    QDir dir(skinPath);
    if (!dir.exists())
    {
        Logger::warn(QString("Skin path does not exist: %1").arg(skinPath));
        return false;
    }

    // 加载 note 图片
    auto loadNotePixmap = [&](int type, const QString &filename)
    {
        QFileInfo info(dir.filePath(filename));
        if (info.exists())
        {
            m_notePixmaps[type] = QPixmap(info.filePath());
            if (m_notePixmaps[type].isNull())
                Logger::warn(QString("Failed to load %1").arg(info.filePath()));
            else
                Logger::debug(QString("Loaded note type %1: %2").arg(type).arg(filename));
        }
        else
        {
            Logger::debug(QString("Note skin %1 not found, will use fallback").arg(filename));
        }
    };

    loadNotePixmap(0, "catch-note-0.png");
    loadNotePixmap(1, "catch-note-1.png");
    loadNotePixmap(2, "catch-note-2.png");
    loadNotePixmap(3, "catch-note-3.png");
    loadNotePixmap(4, "catch-note-4.png");
    loadNotePixmap(5, "catch-note-5.png");

    // 加载 bar 图片
    QFileInfo barInfo(dir.filePath("catch-bar.png"));
    if (barInfo.exists())
    {
        m_barPixmap = QPixmap(barInfo.filePath());
        Logger::debug("Loaded catch-bar.png");
    }

    // 加载 light 图片
    for (int i = 0; i <= 16; ++i)
    {
        QFileInfo lightInfo(dir.filePath(QString("catch-light-%1.png").arg(i)));
        if (lightInfo.exists())
        {
            m_lightPixmaps[i] = QPixmap(lightInfo.filePath());
        }
    }
    QFileInfo lightArc(dir.filePath("catch-light-arc.png"));
    if (lightArc.exists())
    {
        m_lightPixmaps[17] = QPixmap(lightArc.filePath());
        Logger::debug("Loaded catch-light-arc.png");
    }
    QFileInfo lightBar(dir.filePath("catch-light-bar.png"));
    if (lightBar.exists())
    {
        m_lightPixmaps[18] = QPixmap(lightBar.filePath());
        Logger::debug("Loaded catch-light-bar.png");
    }

    // 加载校准配置
    loadConfig();

    m_valid = true;
    Logger::info(QString("Skin loaded from %1, note pixmaps count: %2, bar: %3, light: %4")
                     .arg(skinPath)
                     .arg(m_notePixmaps.size())
                     .arg(m_barPixmap.isNull() ? 0 : 1)
                     .arg(m_lightPixmaps.size()));
    return true;
}

void Skin::clear()
{
    m_notePixmaps.clear();
    m_barPixmap = QPixmap();
    m_lightPixmaps.clear();
    m_valid = false;
    m_noteScales.clear();
    for (int i = 0; i <= 5; ++i)
        m_noteScales[i] = 0.25;
    m_skinPath.clear();
}

const QPixmap *Skin::getNotePixmap(int noteType) const
{
    auto it = m_notePixmaps.constFind(noteType);
    if (it != m_notePixmaps.constEnd())
        return &it.value();
    return nullptr;
}

const QPixmap *Skin::getBarPixmap() const
{
    return m_barPixmap.isNull() ? nullptr : &m_barPixmap;
}

const QPixmap *Skin::getLightPixmap(int lightIndex) const
{
    auto it = m_lightPixmaps.constFind(lightIndex);
    if (it != m_lightPixmaps.constEnd())
        return &it.value();
    return nullptr;
}

double Skin::getNoteScale(int noteType) const
{
    return m_noteScales.value(noteType, 0.25);
}

void Skin::setNoteScale(int noteType, double scale)
{
    m_noteScales[noteType] = scale;
}

bool Skin::saveConfig() const
{
    if (m_skinPath.isEmpty())
        return false;
    QDir dir(m_skinPath);
    QString configPath = dir.filePath("skin_config.json");
    QJsonObject root;
    QJsonArray scales;
    for (int i = 0; i <= 5; ++i)
    {
        scales.append(m_noteScales.value(i, 0.25));
    }
    root["noteScales"] = scales;
    QJsonDocument doc(root);
    QString error;
    if (!FileUtils::writeFileAtomically(configPath,
                                        doc.toJson(QJsonDocument::Indented),
                                        &error))
    {
        Logger::error("Failed to save skin config: " + configPath + " (" + error + ")");
        return false;
    }
    Logger::info("Skin config saved to " + configPath);
    return true;
}

bool Skin::loadConfig()
{
    if (m_skinPath.isEmpty())
        return false;
    QDir dir(m_skinPath);
    QString configPath = dir.filePath("skin_config.json");
    if (!QFile::exists(configPath))
    {
        Logger::debug("No skin config found, using defaults");
        return false;
    }
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        Logger::warn("Failed to open skin config: " + configPath);
        return false;
    }
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull())
    {
        Logger::warn("Invalid skin config JSON");
        return false;
    }
    QJsonObject root = doc.object();
    if (root.contains("noteScales") && root["noteScales"].isArray())
    {
        QJsonArray scales = root["noteScales"].toArray();
        for (int i = 0; i < scales.size() && i <= 5; ++i)
        {
            m_noteScales[i] = scales[i].toDouble();
        }
    }
    Logger::info("Skin config loaded from " + configPath);
    return true;
}
