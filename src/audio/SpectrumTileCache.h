#pragma once
#include "SpectrumService.h"

namespace spectrum_cache
{
constexpr double tileMs = 2000;
QString sourceIdentity(const QString &path);
SpectrumService::Page find(const QString &identity, double start, double duration);
void remember(const QString &identity, double start, double duration, SpectrumService::Page);
SpectrumService::Page tile(const QString &path, const QString &identity, int index);
SpectrumService::Page assemble(const QString &identity, double start, double duration,
    QVector<SpectrumService::Page> tiles, const std::shared_ptr<std::atomic<bool>> &cancel);
SpectrumService::CacheStats stats();
void sharedRequest();
}
