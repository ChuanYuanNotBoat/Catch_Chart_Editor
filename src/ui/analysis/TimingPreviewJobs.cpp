#include "TimingPreviewJobs.h"
#include <QCoreApplication>
#include <QThreadPool>
#include <QThread>

QThreadPool *timingPreviewPool()
{
    static auto *pool = [] {
        auto *p = new QThreadPool(QCoreApplication::instance());
        p->setMaxThreadCount(1);
        p->setThreadPriority(QThread::LowestPriority);
        return p;
    }();
    return pool;
}
