#include <assert.h>
#include <sys/mman.h>
#include "../src/input.c"

int g_log_verbose;
static uint64_t test_now;
static int downs, ups;
uint64_t monotonic_ms(void) { return test_now; }
int video_width(void) { return 1280; }
int video_height(void) { return 720; }
void *arena_alloc(size_t size) {
    return mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
}
uint32_t guest_call2(uint32_t fn, uint32_t event, uint32_t user) {
    int32_t *e = G2H(event);
    if (e[1]) ++downs; else ++ups;
    return 0;
}

int main(void) {
    g_pointer_cb[0] = (struct callback_slot){0, 1, 0};
    test_now = 100;
    pointer_button(1);
    pointer_button(1);
    assert(downs == 1);
    assert(g_pointer_state == (POINTER_STATE_DOWN | POINTER_STATE_PRESSED));
    pointer_publish();
    assert(g_pointer_published == 3 && g_pointer_state == POINTER_STATE_DOWN);
    pointer_publish();
    assert(g_pointer_published == POINTER_STATE_DOWN);
    pointer_button(0);
    pointer_button(0);
    assert(ups == 1);
    assert(menu_mouse_touch_busy()); /* A second click cannot erase an unpublished UP. */
    pointer_publish();
    assert(g_pointer_published == POINTER_STATE_RELEASED);
    assert(!menu_mouse_touch_busy());
    pointer_publish();
    assert(g_pointer_published == POINTER_STATE_UP);

    /* At 9 FPS a whole physical click can arrive before one guest input poll. */
    test_now = 1000;
    g_mouse_touch_active = true;
    pointer_button(1);
    g_mouse_release_pending = true;
    test_now = 2000;
    mouse_touch_update();
    assert(g_pointer_down); /* Time alone cannot erase an unread DOWN. */
    pointer_publish();
    assert(g_pointer_published == 3);
    mouse_touch_update();
    assert(!g_pointer_down && !g_mouse_release_pending);
    pointer_publish();
    assert(g_pointer_published == POINTER_STATE_RELEASED);

    /* No rendering frame needs to advance: only ms and guest acknowledgement. */
    test_now = 3000;
    input_tap(374, 290);
    assert(input_tap_busy() && !g_pointer_down);
    test_now += 32;
    tap_update();
    assert(!g_pointer_down); /* Guest has not observed the new position yet. */
    pointer_publish();
    tap_update();
    assert(g_pointer_down && g_pointer_x == 374 && g_pointer_y == 290);
    int old_downs = downs;
    input_tap(900, 600);
    assert(downs == old_downs && g_pointer_x == 374);
    test_now += 1000;
    tap_update();
    assert(g_pointer_down);
    pointer_publish();
    tap_update();
    assert(!g_pointer_down && !input_tap_busy());
    pointer_publish();
    assert(g_pointer_published == POINTER_STATE_RELEASED);

    test_now = 5000;
    input_tap(640, 650);
    pointer_publish();
    test_now += 32;
    tap_update();
    pointer_publish();
    test_now += 199;
    tap_update();
    assert(g_pointer_down);
    ++test_now;
    tap_update();
    assert(!g_pointer_down);

    test_now = 6000;
    assert(menu_mouse_begin());
    assert(!menu_mouse_begin());
    g_menu_mouse_down = false;
    test_now += 199;
    assert(!menu_mouse_begin());
    assert(menu_mouse_owns_release());
    g_menu_mouse_down = false;
    ++test_now;
    assert(menu_mouse_begin());
    g_menu_mouse_down = false;

    g_menu_mouse_owned = true;
    input_tap(100, 100);
    input_cancel_tap();
    assert(!input_tap_busy() && !g_pointer_down);
    assert(menu_mouse_owns_release());
    assert(!menu_mouse_owns_release());
    test_now += 300;
    input_tap(100, 100);
    pointer_publish();
    test_now += 32;
    tap_update();
    assert(g_pointer_down);
    int old_ups = ups;
    input_cancel_tap();
    input_cancel_tap();
    assert(!g_pointer_down && ups == old_ups + 1);
    assert(downs == ups);
    puts("PASS: published DOWN/UP; slow guest polls; 200 ms timing/cooldown; duplicate suppression; cancellation");
}
