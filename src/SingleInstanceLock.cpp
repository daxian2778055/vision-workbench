#include "SingleInstanceLock.h"
#include <QLockFile>

namespace {
QLockFile *g_lock = nullptr;
}

void setSingleInstanceLock(QLockFile *lock)
{
    g_lock = lock;
}

void releaseSingleInstanceLock()
{
    if (!g_lock)
        return;
    g_lock->unlock();
    g_lock = nullptr;
}
