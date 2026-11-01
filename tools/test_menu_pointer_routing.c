#include <assert.h>
#include "../src/menus.c"
static const char *test_screen;
int g_log_verbose;
static uint64_t test_now = 1000;
static int test_art, tap_count;
static float tap_x, tap_y;
uint64_t monotonic_ms(void) {return test_now;}
bool input_tap_busy(void) {return false;}
void input_tap(float x,float y) {tap_x=x;tap_y=y;tap_count++;}
void screens_art_signature(int out[48]) {for(int i=0;i<48;i++)out[i]=test_art;}
bool reimagined_patched(void) {return true;}
const char *screens_current(void) {return test_screen;}
bool screens_coop_session(void) {return false;}
int main(void) {
 assert(note_screen("charselect",false));
 assert(!note_screen(NULL,false));
 assert(!note_screen("charselect",false)); /* A missed probe must not restart the carousel. */
 assert(note_screen("main",false));
 assert(!note_screen(NULL,true));
 assert(g_prev_screen==NULL); /* Actual gameplay ends the menu transaction. */
 assert(note_screen("title",false));
 assert(!note_screen("title_prompt",false)); /* Blinking prompt cannot cancel the touch. */
 assert(!note_screen("title",false));
 g_enabled=true;
 test_screen="main";
 assert(!menus_pointer_passthrough(200,400)); /* Single tile overlays native Profile */
 test_screen="charselect";assert(!menus_pointer_passthrough(1000,400));
 test_screen="title";assert(!menus_pointer_passthrough(640,650));
 test_screen="coop";assert(menus_pointer_passthrough(600,300));
 g_style_count=1;strcpy(g_styles[0].id,"pause");g_styles[0].page=true;
 test_screen="pause";assert(!menus_pointer_passthrough(930,370));
 g_styles[0].has_window=true;
 float r[]={196,270,560,190};memcpy(g_styles[0].window,r,sizeof(r));
 assert(menus_pointer_passthrough(300,300));
 assert(!menus_pointer_passthrough(900,300));
 g_curtain=true;assert(!menus_pointer_passthrough(300,300));
 g_curtain=false;g_name_open=true;assert(!menus_pointer_passthrough(300,300));
 /* Loading can ignore an arrow: one unchanged attempt cannot establish index 0. */
 g_carousel_at=-1;g_map_busy=g_map_failed=g_launching=false;
 pick_map(3);
 test_now=1899;selection_step();assert(tap_count==0);
 test_now=1900;selection_step();assert(tap_x==447 && g_carousel_at==-1);
 test_now=4401;selection_step();assert(tap_x==447 && g_carousel_at==-1 && g_home_misses==1);
 uint64_t pending_arrow=g_arrow_tap_at;
 pick_map(4);assert(g_arrow_tap_at==pending_arrow); /* Retarget without losing the in-flight tap. */
 test_art=100;test_now+=200;selection_step();assert(g_home_misses==0 && g_carousel_at==-1);
 test_now+=450;selection_step();assert(tap_x==447);
 test_now+=2501;selection_step();assert(g_carousel_at==-1);
 test_now+=2501;selection_step();assert(g_carousel_at==0 && tap_x==845);
 start_selected();assert(g_start_requested && !g_launching); /* One Start click is queued while preparing. */
 for(int i=1;i<=4;i++) {
  test_art=test_art?0:100;test_now+=200;selection_step();
  assert(g_carousel_at==i);
  test_now+=450;selection_step();
 }
 assert(!g_map_busy && !g_map_failed);
 start_selected();assert(g_launching && !g_start_requested && tap_x==297);
 test_now+=300;selection_step();assert(tap_x==980 && tap_y==630);
 puts("PASS: no pointer leak from custom pages; native panels/sliders retained; transitions/editor isolated");
 puts("PASS: carousel settles before homing; confirms left edge twice; retains in-flight arrows; queues one Start");
}
