#include <assert.h>
#include <sys/mman.h>
#include "../src/reimagined.c"
int g_log_verbose;
uint64_t monotonic_ms(void) { return 1000; }
int main(void) {
 assert(mmap((void*)0x4a000000,0x600000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
 assert(mmap((void*)0x50000000,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
 uint32_t app=0x50000100,entries=0x50001000,wrapper=0x50001100,state=0x50002000,net=0x50003000;
 #define PUT(a,v) (*(uint32_t*)G2H(a)=(v))
 PUT(IMG(0x491784),app);PUT(app,IMG(0x42f250));PUT(app+0x44,entries);PUT(app+0x48,1);PUT(entries,0);PUT(entries+4,wrapper);PUT(wrapper+4,state);PUT(state,IMG(0x42f708));PUT(IMG(0x4958d4),net);PUT(net+0x64,2);
 PUT(state+0x50,0x03020100);
 /* the party at the start is the reference */
 PUT(net+0x64,2);sync_coop_pause(true);assert(!g_coop_pause.waiting&&g_coop_pause.baseline==2);
 PUT(net+0x64,1);sync_coop_pause(true);assert(g_coop_pause.waiting && rd(state+0x50)==0x03020101);
 sync_coop_pause(true);assert(g_coop_pause.waiting);
 PUT(state+0x50,0x03020100);sync_coop_pause(true);assert(rd(state+0x50)==0x03020101); /* native unpause cannot bypass */
 PUT(net+0x64,2);sync_coop_pause(true);assert(!g_coop_pause.waiting && rd(state+0x50)==0x03020100);
 /* repeated drops and rejoins */
 for(int i=0;i<3;i++){PUT(net+0x64,1);sync_coop_pause(true);assert(rd(state+0x50)==0x03020101);PUT(net+0x64,2);sync_coop_pause(true);assert(rd(state+0x50)==0x03020100);}
 /* the network object going away counts as everyone gone */
 uint32_t saved=rd(IMG(0x4958d4));PUT(IMG(0x4958d4),0);sync_coop_pause(true);assert(g_coop_pause.waiting);PUT(IMG(0x4958d4),saved);sync_coop_pause(true);assert(!g_coop_pause.waiting);
 /* do not clear a pause owned by the native game */
 PUT(state+0x50,1);PUT(net+0x64,1);sync_coop_pause(true);assert(!g_owned_pause);
 PUT(net+0x64,2);sync_coop_pause(true);assert(rd(state+0x50)==1);
 /* leaving the match forgets everything */
 sync_coop_pause(false);assert(g_coop_pause.baseline==0&&!g_coop_pause.waiting);
 /* solo: one player never waits */
 PUT(state+0x50,0);PUT(net+0x64,1);sync_coop_pause(true);assert(!g_coop_pause.waiting&&rd(state+0x50)==0);sync_coop_pause(false);
 /* unit: start window, late joiner, late first party, new session */
 {struct coop_pause_state c={0};
  coop_pause_update(&c,true,7,1,1000);assert(c.baseline==1&&!c.waiting);
  coop_pause_update(&c,true,7,3,2000);assert(c.baseline==3);
  coop_pause_update(&c,true,7,2,3000);assert(c.waiting);
  coop_pause_update(&c,true,7,3,30000);assert(!c.waiting);
  coop_pause_update(&c,true,7,4,40000);assert(c.baseline==3&&!c.waiting);
  coop_pause_update(&c,true,7,3,41000);assert(!c.waiting);
  coop_pause_update(&c,true,8,2,42000);assert(c.baseline==2&&!c.waiting);
  coop_pause_update(&c,true,8,17,43000);assert(c.baseline==2);
  struct coop_pause_state l={0};
  coop_pause_update(&l,true,9,1,0);coop_pause_update(&l,true,9,2,60000);assert(l.baseline==2);coop_pause_update(&l,true,9,1,61000);assert(l.waiting);}
 PUT(state,0);PUT(state+0x50,0);PUT(net+0x64,0);sync_coop_pause(true);assert(rd(state+0x50)==0); /* reject unknown objects */
 /* the match's co-op flag beats the frame recognition; -1 when nothing can be read */
 uint32_t level=0x50004000;memcpy(G2H(IMG(TIMER)),"1MIT",4);PUT(IMG(TIMER)+8,1);
 PUT(IMG(LEVEL),0);assert(reimagined_match_coop()==-1);
 PUT(IMG(LEVEL),level);PUT(level+0x208,0);assert(reimagined_match_coop()==0);
 PUT(level+0x208,1);assert(reimagined_match_coop()==1);
 PUT(IMG(TIMER)+8,0);assert(reimagined_match_coop()==-1);
 puts("PASS: native state validation, party loss/rejoin, exact pause-byte writes, native pause ownership, match reset, reference party");
}
