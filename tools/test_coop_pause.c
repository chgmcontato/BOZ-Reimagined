#include <assert.h>
#include <sys/mman.h>
#include "../src/reimagined.c"
int g_log_verbose;
int main(void) {
 assert(mmap((void*)0x4a000000,0x600000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
 assert(mmap((void*)0x50000000,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
 uint32_t app=0x50000100,entries=0x50001000,wrapper=0x50001100,state=0x50002000,net=0x50003000;
 #define PUT(a,v) (*(uint32_t*)G2H(a)=(v))
 PUT(IMG(0x491784),app);PUT(app,IMG(0x42f250));PUT(app+0x44,entries);PUT(app+0x48,1);PUT(entries,0);PUT(entries+4,wrapper);PUT(wrapper+4,state);PUT(state,IMG(0x42f708));PUT(IMG(0x4958d4),net);PUT(net+0x64,2);
 PUT(state+0x50,0x03020100);
 sync_coop_pause(true);assert(!g_coop_pause.waiting);
 PUT(net+0x64,1);sync_coop_pause(true);assert(g_coop_pause.waiting && rd(state+0x50)==0x03020101);
 sync_coop_pause(true);assert(g_coop_pause.waiting);
 PUT(state+0x50,0x03020100);sync_coop_pause(true);assert(rd(state+0x50)==0x03020101); /* native unpause cannot bypass */
 PUT(net+0x64,2);sync_coop_pause(true);assert(!g_coop_pause.waiting && rd(state+0x50)==0x03020100);
 PUT(state+0x50,1);PUT(net+0x64,0);sync_coop_pause(true);assert(!g_owned_pause);
 PUT(net+0x64,2);sync_coop_pause(true);assert(rd(state+0x50)==1); /* do not clear a pause owned by the native game */
 sync_coop_pause(false);assert(g_coop_pause.peak==0);
 PUT(state,0);PUT(state+0x50,0);PUT(net+0x64,0);sync_coop_pause(true);assert(rd(state+0x50)==0); /* reject unknown objects */
 puts("PASS: native state validation, party loss/rejoin, exact pause-byte writes, native pause ownership, match reset");
}
