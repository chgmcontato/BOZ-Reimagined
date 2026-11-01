#include "host.h"

#include <SDL.h>
#include <stdlib.h>
#include <time.h>

enum {
    S3E_TIMER_SUCCESS = 0,
    S3E_TIMER_ERROR = 1,
    S3E_TIMER_MAX_EVENTS = 32,
};

struct timer_event {
    uint64_t due_ms;
    uint64_t sequence;
    uint32_t callback;
    uint32_t user_data;
    struct timer_event *next;
};

static struct timer_event *g_timers;
static uint64_t g_timer_sequence;
static uint64_t g_start_us;
static uint64_t g_frame_ms, g_frame_sample_us;
static unsigned g_frame_reads;
static bool g_frame_valid, g_cache_enabled;
static struct {
    uint64_t ms_calls, ust_calls, hits, live, frame, guard, stalls;
} g_timer_perf;

uint64_t monotonic_us(void) {
    static uint64_t freq;
    if (!freq) {
        freq = SDL_GetPerformanceFrequency();
    }
    uint64_t counter = SDL_GetPerformanceCounter();
    return (uint64_t)((counter / freq) * 1000000u + (counter % freq) * 1000000u / freq);
}

uint64_t monotonic_ms(void) {
    return monotonic_us() / 1000u;
}

void sleep_ms(uint32_t ms) {
    SDL_Delay(ms);
}

static uint64_t elapsed_at(uint64_t now) {
    if (!g_start_us) {
        g_start_us = now;
    }
    return (now - g_start_us) / 1000u;
}

void timer_frame_tick(void) {
    if (!g_cache_enabled) return;
    g_frame_sample_us = monotonic_us();
    ++g_timer_perf.frame;
    g_frame_ms = elapsed_at(g_frame_sample_us);
    g_frame_reads = 0;
    g_frame_valid = true;
}

static uint64_t elapsed_ms(void) {
    if (g_cache_enabled && g_frame_valid) {
        /* Normal gameplay samples the clock once per swap. A guest can also
         * poll time without presenting (loading/timeouts). Check occasionally
         * and return to the live clock if no frame has arrived for 100 ms;
         * otherwise a timer-only busy wait could freeze the guest forever. */
        if ((++g_frame_reads & 63u) == 0) {
            uint64_t now = monotonic_us();
            ++g_timer_perf.guard;
            if (now - g_frame_sample_us >= 100000u) {
                g_frame_valid = false;
                ++g_timer_perf.stalls;
                return elapsed_at(now);
            }
        }
        ++g_timer_perf.hits;
        return g_frame_ms;
    }
    ++g_timer_perf.live;
    return elapsed_at(monotonic_us());
}

void timer_perf_reset(void) {
    memset(&g_timer_perf, 0, sizeof(g_timer_perf));
}

void timer_perf_report(double seconds) {
    if (seconds <= 0) seconds = 1;
    uint64_t calls = g_timer_perf.ms_calls + g_timer_perf.ust_calls;
    log_info("perf: timer ms/s %.0f ust/s %.0f clock/s %.0f frame_samples/s %.0f guard/s %.0f stale/s %.0f hits %.1f%%",
             g_timer_perf.ms_calls / seconds, g_timer_perf.ust_calls / seconds,
             (g_timer_perf.live + g_timer_perf.frame + g_timer_perf.guard) / seconds,
             g_timer_perf.frame / seconds, g_timer_perf.guard / seconds,
             g_timer_perf.stalls / seconds, calls ? 100.0 * g_timer_perf.hits / calls : 0);
    timer_perf_reset();
}

static struct timer_event *remove_timer(uint32_t callback, uint32_t user_data) {
    for (struct timer_event **link = &g_timers; *link; link = &(*link)->next) {
        struct timer_event *timer = *link;
        if (timer->callback == callback && timer->user_data == user_data) {
            *link = timer->next;
            timer->next = NULL;
            return timer;
        }
    }
    return NULL;
}

void dispatch_due_timers(void) {
    uint64_t now = monotonic_ms();
    uint64_t sequence_limit = g_timer_sequence;
    while (g_timers && g_timers->due_ms <= now && g_timers->sequence <= sequence_limit) {
        struct timer_event *timer = g_timers;
        g_timers = timer->next;
        uint32_t callback = timer->callback;
        uint32_t user_data = timer->user_data;
        free(timer);
        guest_call2(callback, 0, user_data);
    }
}

IMPORT(s3eTimerGetUST) {
    ++g_timer_perf.ust_calls;
    /* Preserve this port's existing millisecond ABI for both entry points. */
    ret64(elapsed_ms());
}

IMPORT(s3eTimerGetMs) {
    ++g_timer_perf.ms_calls;
    ret64(elapsed_ms());
}

IMPORT(s3eTimerGetInt) {
    ret(arg(0) == 0 ? 1 : (uint32_t)-1);
}

IMPORT(s3eTimerSetTimer) {
    uint32_t period = arg(0), callback = arg(1), user_data = arg(2);
    struct timer_event *timer = remove_timer(callback, user_data);
    if (!callback) {
        free(timer);
        ret(S3E_TIMER_ERROR);
        return;
    }
    if (!timer) {
        size_t count = 0;
        for (struct timer_event *t = g_timers; t; t = t->next) {
            count++;
        }
        if (count >= S3E_TIMER_MAX_EVENTS || !(timer = calloc(1, sizeof(*timer)))) {
            ret(S3E_TIMER_ERROR);
            return;
        }
    }
    timer->due_ms = monotonic_ms() + period;
    timer->sequence = ++g_timer_sequence;
    timer->callback = callback;
    timer->user_data = user_data;
    struct timer_event **link = &g_timers;
    while (*link && (*link)->due_ms <= timer->due_ms) {
        link = &(*link)->next;
    }
    timer->next = *link;
    *link = timer;
    ret(S3E_TIMER_SUCCESS);
}

IMPORT(s3eTimerCancelTimer) {
    struct timer_event *timer = remove_timer(arg(0), arg(1));
    free(timer);
    ret(timer ? S3E_TIMER_SUCCESS : S3E_TIMER_ERROR);
}

IMPORT(s3eTimerGetUTC) {
    ret64((uint64_t)time(NULL) * 1000u + (monotonic_ms() % 1000u));
}

IMPORT(s3eTimerGetLocaltimeOffset) {
    time_t now = time(NULL);
    if (arg(0)) {
        now = (time_t)(*(uint64_t *)G2H(arg(0)) / 1000u);
    }
    struct tm local_tm = *localtime(&now);
    struct tm utc_tm = *gmtime(&now);
    local_tm.tm_isdst = 0;
    utc_tm.tm_isdst = 0;
    int64_t offset = (int64_t)difftime(mktime(&local_tm), mktime(&utc_tm));
    ret64((uint64_t)(offset * 1000));
}

void register_timer_imports(void) {
    const char *cache = getenv("CODBOZ_TIMER_CACHE");
    /* The manual COTD A/B did not establish an FPS gain. Keep frame-quantized
     * guest time opt-in rather than changing timer semantics by default. */
    g_cache_enabled = cache && strcmp(cache, "0") != 0;
    g_frame_valid = false;
    log_info("guest timer cache: %s (CODBOZ_TIMER_CACHE=1 enables experimental frame time; live on stalled frames)",
             g_cache_enabled ? "on" : "off");
    REG(s3eTimerGetUST);
    REG(s3eTimerGetMs);
    REG(s3eTimerGetInt);
    REG(s3eTimerSetTimer);
    REG(s3eTimerCancelTimer);
    REG(s3eTimerGetUTC);
    REG(s3eTimerGetLocaltimeOffset);
}
