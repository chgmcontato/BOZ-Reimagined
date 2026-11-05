/* s3eDevice, s3eDebug, s3eSurface, s3eGL and the extension interfaces the game asks for. */
#include "host.h"

#include <stdlib.h>
#include <time.h>

enum {
    S3E_TOUCHPAD_HASH = 0x1dbd7ce8u,
    S3E_ZERO_CONF_HASH = 0x9f590656u,
    IS_AUDIO_UNIT_HASH = 0x9b347d33u,
    IS_DEVICE_HASH = 0xe7c6ef51u,
    IS_DEVICE_RESOURCES_SIZE = 0x728,
    IS_DEVICE_RESOURCE_PATH_A = 0x428,
    IS_DEVICE_RESOURCE_PATH_B = 0x527,
    IS_DEVICE_RESOURCE_PATH_LEN = 0xff,
    DEVICE_CALLBACKS = 16,
};

struct callback_slot {
    uint32_t id;
    uint32_t callback;
    uint32_t user_data;
};

static struct callback_slot g_device_callbacks[DEVICE_CALLBACKS];
static int g_quit_requested;

int device_quit_requested(void) {
    return g_quit_requested;
}

void device_request_quit(void) {
    g_quit_requested = 1;
}

void dispatch_device_event(uint32_t id, uint32_t system_data) {
    for (int i = 0; i < DEVICE_CALLBACKS; ++i) {
        if (g_device_callbacks[i].callback && g_device_callbacks[i].id == id) {
            guest_call2(g_device_callbacks[i].callback, system_data,
                        g_device_callbacks[i].user_data);
        }
    }
}

void host_pump(void) {
    input_pump();
    audio_pump();
    socket_pump();
    dispatch_due_timers();
}

static void wait_with_timers_inner(uint32_t ms);
static void wait_with_timers(uint32_t ms) {
    uint64_t t0 = monotonic_us();
    wait_with_timers_inner(ms);
    g_perf.wait_us += monotonic_us() - t0;
}

static void wait_with_timers_inner(uint32_t ms) {
    uint64_t deadline = monotonic_ms() + ms;
    for (;;) {
        host_pump();
        uint64_t now = monotonic_ms();
        if (now >= deadline) {
            break;
        }
        uint32_t step = (uint32_t)(deadline - now);
        sleep_ms(step > 5 ? 5 : step);
    }
}

IMPORT(s3eDeviceYield) {
    int32_t ms = (int32_t)arg(0);
    /* The game paces itself by asking to yield the rest of a 33 ms frame; unless the launcher
     * limit is "30 (original)", our own limiter/vsync paces frames instead of these sleeps. */
    extern bool video_surface_fixed(void);
    if (g_opt.fps_limit != 30 || video_surface_fixed()) {
        ms = 0;
    }
    if (ms > 0) {
        wait_with_timers((uint32_t)ms);
    } else {
        host_pump();
    }
    ret(0);
}

IMPORT(s3eDeviceYieldUntilEvent) {
    int32_t ms = (int32_t)arg(0);
    wait_with_timers(ms > 0 ? (uint32_t)ms : 1);
    ret(0);
}

IMPORT(s3eDeviceCheckQuitRequest) {
    ret((uint32_t)g_quit_requested);
}

IMPORT(s3eDeviceRequestQuit) {
    g_quit_requested = 1;
    ret(0);
}

/* Every import runs inside a Dynarmic callback, with the JIT still executing. exit() would run
 * the C++ static destructors and delete that Jit: the window close did exactly this and glibc
 * aborted in malloc_consolidate about one exit in four (see input.c). Settings and saves are
 * already on disk; flush the logs and leave without destructors. */
static void leave(int code) {
    audio_shutdown();
    video_shutdown();
    fflush(NULL);
    _Exit(code);
}

IMPORT(s3eDeviceExit) {
    log_info("game requested exit");
    leave(0);
}

IMPORT(s3eDeviceAbort) {
    log_error("game aborted");
    leave(1);
}

IMPORT(s3eDeviceRegister) {
    for (int i = 0; i < DEVICE_CALLBACKS; ++i) {
        if (!g_device_callbacks[i].callback) {
            g_device_callbacks[i] = (struct callback_slot){arg(0), arg(1), arg(2)};
            break;
        }
    }
    ret(0);
}

IMPORT(s3eDeviceUnRegister) {
    for (int i = 0; i < DEVICE_CALLBACKS; ++i) {
        if (g_device_callbacks[i].id == arg(0) && g_device_callbacks[i].callback == arg(1)) {
            memset(&g_device_callbacks[i], 0, sizeof(g_device_callbacks[i]));
        }
    }
    ret(0);
}

/* The Xperia Play profile gives us the physical-controls binding set and no touch sticks. */
IMPORT(s3eDeviceGetInt) {
    switch (arg(0)) {
    case 0:
        ret(0x12); /* S3E_DEVICE_OS: XperiaPlayBO */
        break;
    case 10:
        ret(0x00080101);
        break;
    default:
        ret(0);
        break;
    }
}

static uint32_t device_id_string(void) {
    static uint32_t text;
    if (!text) {
        char path[1200], id[40] = "";
        snprintf(path, sizeof(path), "%s/device_id.txt", g_user_dir);
        FILE *file = fopen(path, "r");
        if (file) {
            if (!fgets(id, sizeof(id), file)) {
                id[0] = 0;
            }
            fclose(file);
            id[strcspn(id, "\r\n")] = 0;
        }
        if (strlen(id) != 32) {
            srand((unsigned)time(NULL) ^ (unsigned)monotonic_us());
            for (int i = 0; i < 32; ++i) {
                id[i] = "0123456789abcdef"[rand() & 15];
            }
            id[32] = 0;
            file = fopen(path, "w");
            if (file) {
                fprintf(file, "%s\n", id);
                fclose(file);
            }
        }
        text = arena_strdup(id);
    }
    return text;
}

IMPORT(s3eDeviceGetString) {
    static uint32_t strings[0x30];
    static const char *values[0x30] = {
        [0x00] = "Android",
        [0x02] = "R800i",
        [0x08] = "ARM7A",
        [0x0d] = "4.1.2",
        [0x13] = "8.6",
        [0x14] = "en_US",
        [0x15] = "Sony Ericsson Xperia Play",
        [0x1f] = "UTC",
        [0x24] = "UTC",
        [0x25] = "UTC",
    };
    uint32_t key = arg(0);
    if (key == 0x19) {
        ret(device_id_string());
        return;
    }
    if (key >= 0x30) {
        key = 0x2f;
    }
    if (!strings[key]) {
        strings[key] = arena_strdup(values[key] ? values[key] : "");
    }
    ret(strings[key]);
}

IMPORT(s3eDeviceSetInt) {
    ret(0);
}

IMPORT(s3eDeviceBacklightOn) {
    ret(0);
}

IMPORT(s3eDebugOutputString) {
    log_debug("debug: %s", (const char *)G2H(arg(0)));
}

IMPORT(s3eDebugTraceLine) {
    log_debug("trace: %s", (const char *)G2H(arg(0)));
}

IMPORT(s3eDebugPrint) {
    ret(0);
}

IMPORT(s3eDebugGetInt) {
    ret(0);
}

IMPORT(s3eDebugIsDebuggerPresent) {
    ret(0);
}

IMPORT(s3eDebugAssertShow) {
    log_info("assert: %s", arg(1) ? (const char *)G2H(arg(1)) : "?");
    ret(0); /* S3E_ERROR_SHOW_CONTINUE */
}

IMPORT(s3eDebugErrorShow) {
    log_info("error dialog: %s", arg(1) ? (const char *)G2H(arg(1)) : "?");
    ret(0);
}

IMPORT(s3eAccelerometerGetInt) {
    ret(0);
}

IMPORT(s3eVideoGetInt) {
    ret(0);
}

IMPORT(s3eCompressionDecompInit) {
    ret(0);
}

IMPORT(s3eCompressionDecomp) {
    if (arg(3)) {
        *(uint32_t *)G2H(arg(3)) = 0;
    }
    ret((uint32_t)-1);
}

/* Software surface: unused by the GL renderer but must exist. */
static uint32_t g_surface_pixels;

IMPORT(s3eSurfaceGetInt) {
    switch (arg(0)) {
    case 0: /* WIDTH */
    case 3: /* DEVICE_WIDTH */
        ret((uint32_t)video_width());
        break;
    case 1:
    case 4:
        ret((uint32_t)video_height());
        break;
    case 2: /* PITCH */
        ret((uint32_t)video_width() * 4u);
        break;
    case 5: /* PIXEL_TYPE: RGB888 */
        ret(4);
        break;
    default:
        ret(0);
        break;
    }
}

IMPORT(s3eSurfacePtr) {
    if (!g_surface_pixels) {
        g_surface_pixels = gcalloc((uint32_t)(video_width() * video_height()), 4);
    }
    ret(g_surface_pixels);
}

IMPORT(s3eSurfaceShow) {
    ret(0);
}

IMPORT(s3eSurfaceSetup) {
    ret(0);
}

IMPORT(s3eGLGetInt) {
    switch (arg(0)) {
    case 0:
        ret((uint32_t)video_width());
        break;
    case 1:
        ret((uint32_t)video_height());
        break;
    case 2:
        ret(2);
        break;
    default:
        ret(0);
        break;
    }
}

IMPORT(s3eGLGetNativeWindow) {
    static uint32_t window;
    if (!window) {
        window = H2G(arena_alloc(16));
    }
    ret(window);
}

/* ---- extension interfaces ---- */

static uint8_t *g_is_device_resources;

static void imp_return0(void) {
    ret(0);
}


IMPORT(isDeviceSetTabletThreshold) {
    ret(arg(0));
}

IMPORT(isDeviceGetDisplayType) {
    ret(2); /* smartphone: larger HUD */
}

IMPORT(isDeviceGetExternalResources) {
    if (!g_is_device_resources) {
        g_is_device_resources = arena_alloc(IS_DEVICE_RESOURCES_SIZE);
    }
    memset(g_is_device_resources, 0, IS_DEVICE_RESOURCES_SIZE);
    g_is_device_resources[0x10] = 1;
    snprintf((char *)g_is_device_resources + IS_DEVICE_RESOURCE_PATH_A,
             IS_DEVICE_RESOURCE_PATH_LEN, "%s/", g_root);
    snprintf((char *)g_is_device_resources + IS_DEVICE_RESOURCE_PATH_B,
             IS_DEVICE_RESOURCE_PATH_LEN, "%s/assets/", g_root);
    ret(H2G(g_is_device_resources));
}

void touchpad_fill_interface(uint32_t *table);
void zeroconf_fill_interface(uint32_t *table);
void audio_unit_fill_interface(uint32_t *table);

static int fill_table(uint32_t iface, uint32_t size, uint32_t expected, const uint32_t *entries) {
    if (!iface || size != expected * 4u) {
        return 1;
    }
    memcpy(G2H(iface), entries, size);
    return 0;
}

IMPORT(s3eExtGetHash) {
    uint32_t hash = arg(0), iface = arg(1), size = arg(2);
    uint32_t table[16] = {0};
    int rc = 1;
    switch (hash) {
    case IS_DEVICE_HASH:
        table[0] = import_thunk("isDeviceCallbackRegister", imp_return0);
        table[1] = import_thunk("isDeviceCallbackUnregister", imp_return0);
        table[2] = import_thunk("isDeviceSetTabletThreshold", imp_isDeviceSetTabletThreshold);
        table[3] = import_thunk("isDeviceGetDisplayType", imp_isDeviceGetDisplayType);
        table[4] = import_thunk("isDeviceGetExternalResources", imp_isDeviceGetExternalResources);
        rc = fill_table(iface, size, 5, table);
        break;
    case S3E_TOUCHPAD_HASH:
        touchpad_fill_interface(table);
        rc = fill_table(iface, size, 5, table);
        break;
    case S3E_ZERO_CONF_HASH:
        zeroconf_fill_interface(table);
        rc = fill_table(iface, size, 5, table);
        break;
    case IS_AUDIO_UNIT_HASH:
        audio_unit_fill_interface(table);
        rc = fill_table(iface, size, 13, table);
        break;
    default:
        log_debug("extension 0x%08x not available", hash);
        break;
    }
    if (rc && iface && size) {
        memset(G2H(iface), 0, size);
    }
    ret((uint32_t)rc);
}

void register_runtime_imports(void) {
    REG(s3eExtGetHash);
    REG(s3eDeviceYield);
    REG(s3eDeviceYieldUntilEvent);
    REG(s3eDeviceCheckQuitRequest);
    REG(s3eDeviceRequestQuit);
    REG(s3eDeviceExit);
    REG(s3eDeviceAbort);
    REG(s3eDeviceRegister);
    REG(s3eDeviceUnRegister);
    REG(s3eDeviceGetInt);
    REG(s3eDeviceGetString);
    REG(s3eDeviceSetInt);
    REG(s3eDeviceBacklightOn);
    REG(s3eDebugOutputString);
    REG(s3eDebugTraceLine);
    REG(s3eDebugPrint);
    REG(s3eDebugGetInt);
    REG(s3eDebugIsDebuggerPresent);
    REG(s3eDebugAssertShow);
    REG(s3eDebugErrorShow);
    REG(s3eAccelerometerGetInt);
    REG(s3eVideoGetInt);
    REG(s3eCompressionDecompInit);
    REG(s3eCompressionDecomp);
    REG(s3eSurfaceGetInt);
    REG(s3eSurfacePtr);
    REG(s3eSurfaceShow);
    REG(s3eSurfaceSetup);
    REG(s3eGLGetInt);
    REG(s3eGLGetNativeWindow);
    import_register("s3eAccelerometerStart", imp_return0);

}
