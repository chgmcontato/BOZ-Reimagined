#include <assert.h>
#include <sys/mman.h>
#include "../src/input.c"
int g_log_verbose;
static uint64_t test_now;
static bool test_title, test_main;
uint64_t monotonic_ms(void) {return test_now;}
int video_width(void) {return 1280;}
int video_height(void) {return 720;}
bool screens_is(const char *id) {return (test_title && strcmp(id,"title")==0) || (test_main && strcmp(id,"main")==0);}
void *arena_alloc(size_t size) {return mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);}
uint32_t guest_call2(uint32_t f,uint32_t a,uint32_t b) {return 0;}
int main(void) {
    assert(!menu_mouse_owns_release());
    g_menu_mouse_owned=true;
    assert(menu_mouse_owns_release());
    assert(!menu_mouse_owns_release());
    /* Ownership outlives the synthetic tap: a delayed physical mouse-up must
     * still be consumed rather than move/release a different Flash control. */
    g_menu_mouse_owned=true;g_tap_release_at=0;
    assert(menu_mouse_owns_release());
    pointer_button(1);g_tap_release_at=250;g_tap_press_at=200;
    input_cancel_tap();
    assert(!input_tap_busy() && !g_tap_press_at && !g_pointer_down);
    assert(g_pointer_state==POINTER_STATE_RELEASED);
    test_now=100;test_title=false;g_input_frame=10;
    input_tap(374,290);
    assert(!g_pointer_down && input_tap_busy());
    test_now=300;tap_update();
    assert(!g_pointer_down); /* elapsed time alone cannot press on a stale frame */
    input_after_swap();tap_update();
    assert(g_pointer_down && g_pointer_x==374 && g_pointer_y==290);
    test_now=500;tap_update();assert(g_pointer_down);
    input_after_swap();tap_update();assert(g_pointer_down);
    input_after_swap();tap_update();assert(!g_pointer_down && !input_tap_busy());
    test_main=true;g_input_frame=20;test_now=700;
    input_tap(913,383);input_after_swap();test_now=900;tap_update();
    assert(!g_pointer_down); /* main-page target is not pressed after only one frame */
    input_after_swap();tap_update();assert(g_pointer_down);
    assert(g_pointer_x==913 && g_pointer_y==383);
    input_cancel_tap();test_main=false;
    test_title=true;input_tap(640,650);assert(g_pointer_down);
    input_cancel_tap();assert(!g_pointer_down);
    puts("PASS: overlay mouse-up ownership; transition cancellation; frame-separated motion/down; minimum hold frames; immediate title");
}
