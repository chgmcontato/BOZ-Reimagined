/* Fake SDL clock: exercise the actual timer imports without sleeping or a guest. */
#include <assert.h>
#include "../src/timer.c"

int g_log_verbose;
static uint64_t fake_us = 1000000, result, clock_reads;
static uint32_t args[4];
static unsigned callbacks;
uint32_t arg(int index) { return args[index]; }
void ret(uint32_t value) { result = value; }
void ret64(uint64_t value) { result = value; }
void import_register(const char *name, import_fn fn) { (void)name; (void)fn; }
uint32_t guest_call2(uint32_t fn, uint32_t a, uint32_t b) {
    assert(fn == 123 && a == 0 && b == 456);
    ++callbacks;
    return 0;
}
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000; }
Uint64 SDL_GetPerformanceCounter(void) { ++clock_reads; return fake_us; }
void SDL_Delay(Uint32 ms) { fake_us += (uint64_t)ms * 1000; }

int main(void) {
    unsetenv("CODBOZ_TIMER_CACHE");
    register_timer_imports();
    assert(!g_cache_enabled); /* No proven COTD FPS gain: preserve live default. */
    setenv("CODBOZ_TIMER_CACHE", "1", 1);
    register_timer_imports();
    imp_s3eTimerGetMs();
    assert(result == 0);
    fake_us += 12000;
    imp_s3eTimerGetUST();
    assert(result == 12); /* Existing UST millisecond ABI, live before swap. */
    timer_frame_tick();
    uint64_t reads = clock_reads;
    for (int i = 0; i < 20; ++i) {
        fake_us += 1000;
        imp_s3eTimerGetMs(); assert(result == 12);
        imp_s3eTimerGetUST(); assert(result == 12);
    }
    assert(clock_reads == reads); /* Forty reads reuse one frame sample. */
    timer_frame_tick();
    imp_s3eTimerGetMs(); assert(result == 32);

    args[0] = 5; args[1] = 123; args[2] = 456;
    imp_s3eTimerSetTimer(); assert(result == S3E_TIMER_SUCCESS);
    fake_us += 10000;
    dispatch_due_timers(); assert(callbacks == 1);
    imp_s3eTimerGetMs(); assert(result == 32); /* Callback clock stays real. */

    fake_us += 200000;
    for (int i = 0; i < 64; ++i) imp_s3eTimerGetMs();
    assert(!g_frame_valid && result == 242 && g_timer_perf.stalls == 1);
    fake_us += 1000;
    imp_s3eTimerGetUST(); assert(result == 243); /* Busy waits keep advancing. */
    timer_frame_tick();
    reads = clock_reads;
    fake_us += 1000;
    imp_s3eTimerGetUST(); assert(result == 243 && clock_reads == reads);

    setenv("CODBOZ_TIMER_CACHE", "0", 1);
    register_timer_imports();
    reads = clock_reads;
    timer_frame_tick(); assert(clock_reads == reads);
    imp_s3eTimerGetMs(); assert(result == 244);
    fake_us += 1000;
    imp_s3eTimerGetUST(); assert(result == 245 && clock_reads == reads + 2);
    timer_perf_report(1);
    assert(g_timer_perf.ms_calls == 0 && g_timer_perf.ust_calls == 0);
    puts("PASS: frame timer reuse; shared Ms/UST epoch; live startup; real callback deadlines; stalled-frame recovery; rollback");
}
