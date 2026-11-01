#include "host.h"

#include <stdlib.h>

enum { MAX_IMPORTS = 1024 };

struct import_def {
    const char *name;
    import_fn fn;
    uint32_t thunk;
};

static struct import_def g_imports[MAX_IMPORTS];
static size_t g_import_count;

void import_register(const char *name, import_fn fn) {
    for (size_t i = 0; i < g_import_count; ++i) {
        if (strcmp(g_imports[i].name, name) == 0) {
            g_imports[i].fn = fn;
            return;
        }
    }
    if (g_import_count < MAX_IMPORTS) {
        g_imports[g_import_count].name = name;
        g_imports[g_import_count].fn = fn;
        g_import_count++;
    }
}

static void unimplemented(void) {
    static const char *seen[512];
    static size_t seen_count;
    const char *name = current_import();
    for (size_t i = 0; i < seen_count; ++i) {
        if (seen[i] == name) {
            ret(0);
            return;
        }
    }
    if (seen_count < 512) {
        seen[seen_count++] = name;
    }
    log_info("unimplemented import %s(0x%x, 0x%x, 0x%x, 0x%x)", name, arg(0), arg(1), arg(2),
             arg(3));
    ret(0);
}

uint32_t import_resolve(const char *name) {
    for (size_t i = 0; i < g_import_count; ++i) {
        if (strcmp(g_imports[i].name, name) == 0) {
            if (!g_imports[i].thunk) {
                g_imports[i].thunk = emu_add_thunk(g_imports[i].name, g_imports[i].fn);
            }
            return g_imports[i].thunk;
        }
    }
    /* Unknown symbols still get a named thunk so a call can be diagnosed. */
    char *copy = strdup(name);
    import_register(copy, unimplemented);
    return import_resolve(copy);
}

uint32_t import_thunk(const char *name, import_fn fn) {
    import_register(name, fn);
    return import_resolve(name);
}
