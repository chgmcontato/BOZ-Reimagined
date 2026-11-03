/*
 * Android glue for the runtime (built only into the app's libmain.so, see mobile/native).
 *
 * Logs: the runtime writes everything to stderr (log.h), which Android discards. When the
 * library loads, stderr and stdout are redirected into a pipe; a thread forwards each line to
 * logcat (tag "BozRuntime") and, once BozActivity has named it, to a log file in the app's
 * external files folder, so a run can be read back without adb.
 *
 * Entry point: SDL_main below (see its comment).
 *
 * First-boot diagnostics (proof-of-concept builds): CODBOZ_PERF on (the PC's once-a-second fps /
 * emu / host / map / round lines), process memory every 15 s, and a native crash note saying
 * whether the faulting address lies in the guest reservation (a base + address translation miss)
 * before Android's own crash report (tombstone) runs.
 */
#ifdef __ANDROID__
#include "emu.h"

#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static FILE *g_log_file;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void forward_line(const char *line) {
    int priority = strstr(line, "error") ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO;
    __android_log_write(priority, "BozRuntime", line);
    pthread_mutex_lock(&g_log_lock);
    if (g_log_file) {
        fputs(line, g_log_file);
        fputc('\n', g_log_file);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_log_lock);
}

static void *pump(void *arg) {
    int fd = (int)(intptr_t)arg;
    char buffer[4096];
    size_t used = 0;
    for (;;) {
        ssize_t n = read(fd, buffer + used, sizeof(buffer) - 1 - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
        buffer[used] = 0;
        char *start = buffer, *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = 0;
            forward_line(start);
            start = nl + 1;
        }
        used = (size_t)(buffer + used - start);
        memmove(buffer, start, used);
        if (used == sizeof(buffer) - 1) { /* a line longer than the buffer: flush it as is */
            buffer[used] = 0;
            forward_line(buffer);
            used = 0;
        }
    }
    return NULL;
}

/*
 * Memory diagnosis for the first runs: what the process already has in its low 4 GiB, and whether
 * any of it sits where the PC's identity map would put the guest (0x40000000..0x90000000: stack,
 * arena, thunks, image at 0x4a000000, heap at 0x50000000). On Android the guest lives in its own
 * reservation (CODBOZ_GUEST_BASE), so this only reports; it decides nothing.
 */
static void log_low_mappings(void) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        return;
    }
    char line[512];
    unsigned low = 0, overlap = 0;
    unsigned long long low_bytes = 0;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long long start = 0, end = 0;
        if (sscanf(line, "%llx-%llx", &start, &end) != 2 || start >= 0x100000000ull) {
            continue;
        }
        ++low;
        low_bytes += (end < 0x100000000ull ? end : 0x100000000ull) - start;
        if (start < 0x90000000ull && end > 0x40000000ull) {
            line[strcspn(line, "\n")] = 0;
            if (++overlap <= 12) {
                __android_log_print(ANDROID_LOG_INFO, "BozRuntime", "memory: in the PC guest range: %s",
                                    line);
            }
        }
    }
    fclose(maps);
    __android_log_print(ANDROID_LOG_INFO, "BozRuntime",
                        "memory: %u mappings below 4 GiB (%llu MiB), %u inside 0x40000000-0x90000000; "
                        "page size %ld",
                        low, low_bytes >> 20, overlap, sysconf(_SC_PAGESIZE));
}

__attribute__((constructor)) static void redirect_stdio(void) {
    log_low_mappings();
    int fds[2];
    if (pipe(fds) != 0) {
        return;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    pthread_t thread;
    if (pthread_create(&thread, NULL, pump, (void *)(intptr_t)fds[0]) == 0) {
        pthread_detach(thread);
    }
}

/* ---- memory, every 15 s ---- */

static void *memory_watch(void *unused) {
    (void)unused;
    for (;;) {
        FILE *f = fopen("/proc/self/status", "r");
        char line[128], rss[32] = "?", hwm[32] = "?", vsz[32] = "?";
        while (f && fgets(line, sizeof(line), f)) {
            sscanf(line, "VmRSS: %31s", rss);
            sscanf(line, "VmHWM: %31s", hwm);
            sscanf(line, "VmSize: %31s", vsz);
        }
        if (f) {
            fclose(f);
        }
        __android_log_print(ANDROID_LOG_INFO, "BozRuntime", "memory: rss %s kB, peak rss %s kB, virtual %s kB",
                            rss, hwm, vsz);
        sleep(15);
    }
    return NULL;
}

/* ---- native crash note ---- */

static const int k_fatal[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#define FATAL_COUNT (sizeof(k_fatal) / sizeof(k_fatal[0]))
static struct sigaction g_previous[FATAL_COUNT];

static void crash_note(int sig, siginfo_t *info, void *context) {
    uintptr_t addr = (uintptr_t)info->si_addr, pc = 0;
#if defined(__aarch64__)
    pc = (uintptr_t)((ucontext_t *)context)->uc_mcontext.pc;
#else
    (void)context;
#endif
    Dl_info where = {0};
    dladdr((void *)pc, &where);
    const char *import = current_import();
    uintptr_t base = (uintptr_t)g_guest_base;
    if (base && addr >= base && addr - base <= 0xffffffffu) {
        __android_log_print(ANDROID_LOG_ERROR, "BozRuntime",
                            "crash: signal %d at host %p = GUEST 0x%08x (an unmapped guest page reached "
                            "through G2H), pc %p (%s+0x%lx %s), import %s",
                            sig, (void *)addr, (unsigned)(addr - base), (void *)pc,
                            where.dli_fname ? where.dli_fname : "?",
                            (unsigned long)(pc - (uintptr_t)where.dli_fbase),
                            where.dli_sname ? where.dli_sname : "", import ? import : "-");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "BozRuntime",
                            "crash: signal %d (code %d) at %p, outside the guest space; pc %p (%s+0x%lx %s), "
                            "import %s",
                            sig, info->si_code, (void *)addr, (void *)pc, where.dli_fname ? where.dli_fname : "?",
                            (unsigned long)(pc - (uintptr_t)where.dli_fbase),
                            where.dli_sname ? where.dli_sname : "", import ? import : "-");
    }
    /* hand over to the previous handler (debuggerd: the tombstone with every register) */
    for (size_t i = 0; i < FATAL_COUNT; ++i) {
        sigaction(k_fatal[i], &g_previous[i], NULL);
    }
    if (info->si_code <= 0) { /* sent (abort, kill): resend; a real fault repeats on return */
        raise(sig);
    }
}

static void install_crash_note(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_note;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < FATAL_COUNT; ++i) {
        sigaction(k_fatal[i], &sa, &g_previous[i]);
    }
}

/*
 * SDL_main: SDL calls it on its Java "SDLThread", whose native stack is the Java default (about
 * 1 MiB). The PC's main() (src/main.c, compiled here as boz_main) gets the 8+ MiB it has on a
 * desktop: it runs on its own thread and SDL_main waits for it.
 */
int boz_main(int argc, char **argv);

struct main_args {
    int argc;
    char **argv;
    int rc;
};

static void *run_main(void *p) {
    struct main_args *a = p;
    a->rc = boz_main(a->argc, a->argv);
    return NULL;
}

__attribute__((visibility("default"))) int SDL_main(int argc, char *argv[]) {
    struct main_args a = {argc, argv, 1};
    setenv("CODBOZ_PERF", "1", 0);
    install_crash_note();
    {
        pthread_t watch;
        if (pthread_create(&watch, NULL, memory_watch, NULL) == 0) {
            pthread_detach(watch);
        }
    }
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20);
    if (pthread_create(&thread, &attr, run_main, &a) != 0) {
        __android_log_write(ANDROID_LOG_WARN, "BozRuntime", "no big-stack thread; main runs on SDLThread");
        run_main(&a);
    } else {
        pthread_join(thread, NULL);
    }
    pthread_attr_destroy(&attr);
    __android_log_print(ANDROID_LOG_INFO, "BozRuntime", "main returned %d", a.rc);
    return a.rc;
}

/* BozActivity.nativeSetLogFile(path): start (truncate) the run's log file. */
JNIEXPORT void JNICALL Java_com_carlos_boz_mobile_BozActivity_nativeSetLogFile(JNIEnv *env, jclass cls,
                                                                              jstring path) {
    (void)cls;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) {
        return;
    }
    pthread_mutex_lock(&g_log_lock);
    if (g_log_file) {
        fclose(g_log_file);
    }
    g_log_file = fopen(p, "w");
    pthread_mutex_unlock(&g_log_lock);
    __android_log_print(ANDROID_LOG_INFO, "BozRuntime", "log file: %s (%s)", p,
                        g_log_file ? "open" : strerror(errno));
    (*env)->ReleaseStringUTFChars(env, path, p);
}
#endif
