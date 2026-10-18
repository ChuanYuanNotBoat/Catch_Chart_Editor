#pragma once
class QThreadPool;
// One low-priority worker shared by timing tools. No widget owns this pool, so
// closing a tool never waits for its pending calculation to finish.
QThreadPool *timingPreviewPool();
