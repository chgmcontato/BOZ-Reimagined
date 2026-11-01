#ifndef COOP_PAUSE_STATE_H
#define COOP_PAUSE_STATE_H
#include <stdbool.h>
#include <stdint.h>
struct coop_pause_state {uint32_t session;int peak;int peers;bool waiting;};
static bool coop_pause_update(struct coop_pause_state *s,bool active,uint32_t session,int peers) {
 if(!active){s->session=0;s->peak=0;s->peers=0;s->waiting=false;return false;}
 if(!session || peers<0 || peers>16)return s->waiting;
 if(s->session!=session){s->session=session;s->peak=0;s->waiting=false;}
 s->peers=peers;
 if(peers>s->peak)s->peak=peers;
 s->waiting=s->peak>0 && peers<s->peak;
 return s->waiting;
}
#endif
