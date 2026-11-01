#include "host.h"

enum { HEAP_SLOTS = 8 };

static uint32_t g_heaps[HEAP_SLOTS];
static uint32_t g_user_mgr[3];

IMPORT(s3eMallocBase) {
    ret(gmalloc(arg(0) ? arg(0) : 1));
}

IMPORT(s3eReallocBase) {
    ret(grealloc(arg(0), arg(1) ? arg(1) : 1));
}

IMPORT(s3eFreeBase) {
    gfree(arg(0));
}

IMPORT(s3eMemoryGetInt) {
    /* S3E_MEMORY_SIZE / FREE / USED / LFB: plenty of headroom */
    ret(768u * 1024u * 1024u);
}

IMPORT(s3eMemorySetInt) {
    ret(0);
}

IMPORT(s3eMemorySetUserMemMgr) {
    /* Marmalade's Base entry points never dispatch through a user manager. */
    if (arg(0)) {
        memcpy(g_user_mgr, G2H(arg(0)), sizeof(g_user_mgr));
    } else {
        memset(g_user_mgr, 0, sizeof(g_user_mgr));
    }
    ret(0);
}

IMPORT(s3eMemoryHeapCreate) {
    uint32_t index = arg(0);
    if (index >= HEAP_SLOTS) {
        ret(1);
        return;
    }
    if (!g_heaps[index]) {
        g_heaps[index] = gcalloc(1, index == 0 ? 128u << 20 : 16u << 20);
    }
    ret(g_heaps[index] ? 0 : 1);
}

void register_memory_imports(void) {
    REG(s3eMallocBase);
    REG(s3eReallocBase);
    REG(s3eFreeBase);
    REG(s3eMemoryGetInt);
    REG(s3eMemorySetInt);
    REG(s3eMemorySetUserMemMgr);
    REG(s3eMemoryHeapCreate);
}
