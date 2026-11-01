#ifndef CODBOZ_PC_COOP_STATS_H
#define CODBOZ_PC_COOP_STATS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Live co-op stats between the host layers (PC and Hud.java). The game only sends the others'
 * stats with the end-of-match results, so each device publishes its own over UDP 27051, little endian:
 *   "BOZS" | u32 version (1) | u32 name hash | i32 kills, downs, revives, headshots
 * Sent every second to each interface's broadcast address and to every peer heard from.
 */
struct coop_stats {
    int kills, downs, revives, headshots;
};

/* Call every frame during a co-op match with the local player's numbers. */
void coop_stats_tick(uint32_t name_hash, const struct coop_stats *mine);
/* Latest numbers from that player, if heard in the last 5 seconds. */
bool coop_stats_lookup(uint32_t name_hash, struct coop_stats *out);

#endif
