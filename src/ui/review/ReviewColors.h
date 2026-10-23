#pragma once

#include <QColor>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace CceReviewUi
{
inline QColor commentAuthorColor(const QJsonObject &author)
{
    const QString key = author.value("authorId").toString(author.value("authorNameSnapshot").toString());
    const auto hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
    const int hue = (quint8(hash[0]) * 256 + quint8(hash[1])) % 360;
    return QColor::fromHsv(hue, 110, 225);
}

inline QHash<QString, QColor> commentAuthorColors(const QJsonArray &comments)
{
    QHash<QString, QColor> authorColors;
    QVector<int> usedHues;
    for (const auto &value : comments)
    {
        const auto author = value.toObject().value("author").toObject();
        const auto id = author.value("authorId").toString();
        if (authorColors.contains(id))
            continue;
        QColor color = commentAuthorColor(author);
        int hue = color.hsvHue();
        // Nearby hashes should not make two participants look like one author.
        for (int attempt = 0; attempt < 36; ++attempt)
        {
            const bool nearby = std::any_of(usedHues.cbegin(), usedHues.cend(), [hue](int used) {
                const int distance = qAbs(hue - used);
                return qMin(distance, 360 - distance) < 50;
            });
            if (!nearby)
                break;
            hue = (hue + 137) % 360;
        }
        color.setHsv(hue, 110, 225);
        usedHues.append(hue);
        authorColors.insert(id, color);
    }
    return authorColors;
}
} // namespace CceReviewUi
