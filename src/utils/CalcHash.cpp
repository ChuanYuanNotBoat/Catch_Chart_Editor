#include "CalcHash.h"
#include <qfile.h>
#include <qcryptographichash.h>
#include <qbytearray.h>
#include <qiodevice.h>
#include <qjsonarray.h>
#include <qstring.h>
#include <qjsondocument.h>

QString CalcHash::computeQuickHash(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray chunk = f.read(65536);
    f.close();
    return QString::fromLatin1(
        QCryptographicHash::hash(chunk, QCryptographicHash::Sha256).toHex());
}

QByteArray CalcHash::computeHashFromJsonArray(const QJsonArray& array)
{
    QJsonDocument doc(array);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    QByteArray hash = QCryptographicHash::hash(jsonData, QCryptographicHash::Sha256);

    return hash;
}
