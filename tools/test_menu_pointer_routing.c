#include <assert.h>
#include "../src/menus.c"
static const char *test_screen;
bool reimagined_patched(void) {return true;}
const char *screens_current(void) {return test_screen;}
bool screens_coop_session(void) {return false;}
int main(void) {
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
 puts("PASS: no pointer leak from custom pages; native panels/sliders retained; transitions/editor isolated");
}
