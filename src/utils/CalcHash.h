#pragma once

#include <qjsonarray.h>
#include <qbytearray.h>
#include <qstring.h>


class CalcHash
{
public:
	/// 计算文件前 64KB 的 SHA-256 哈希，用于快速比较文件内容
	static QString computeQuickHash(const QString& filePath);
	
	/// 根据谱面note计算SHA-256
	static QByteArray computeHashFromJsonArray(const QJsonArray& array);
};