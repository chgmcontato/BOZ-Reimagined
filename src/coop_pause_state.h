#ifndef COOP_PAUSE_STATE_H
#define COOP_PAUSE_STATE_H
#include <stdbool.h>
#include <stdint.h>
/*
 * "Match Paused": the party the co-op match started with is the reference. Everyone may still be
 * connecting when the match goes live, so the reference settles during a short window; a player
 * who joins later does not raise it. Below the reference the match waits.
 */
enum { COOP_PAUSE_SETTLE_MS = 20000 };
struct coop_pause_state {uint32_t session;int baseline;int peers;bool waiting;uint64_t started_ms;};
static bool coop_pause_update(struct coop_pause_state *s,bool active,uint32_t session,int peers,uint64_t now_ms) {
 if(!active){*s=(struct coop_pause_state){0};return false;}
 if(!session || peers<0 || peers>16)return s->waiting;
 if(s->session!=session){*s=(struct coop_pause_state){.session=session,.started_ms=now_ms};}
 s->peers=peers;
 /* a lone first count never counts as a party: the first real party sets it even when late */
 if(peers>s->baseline && (now_ms-s->started_ms<COOP_PAUSE_SETTLE_MS || s->baseline<2))s->baseline=peers;
 s->waiting=s->baseline>=2 && peers<s->baseline;
 return s->waiting;
}
/*
 * What the pause card shows. network+0x64 counts connections and can include stale ones (4 for two
 * real players after a re-join), so only its drop is reliable: the party comes from real players.
 * Same formula as the TV (Hud.java). `waiting` above is deliberately not derived from real players:
 * a returning player connects before the remote-player map has them, so it could never resume.
 */
static void coop_pause_card(const struct coop_pause_state *s,int party,int *present,int *expected) {
 int e=party<2?2:party>4?4:party, gone=s->baseline-s->peers;
 if(gone<1)gone=1;
 *expected=e;
 *present=e-gone<1?1:e-gone;
}
#endif
