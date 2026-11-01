package com.carlos.boz;
/** Keeps the original party size until all peers return or the match ends. */
final class CoopPauseState {
    private long session;
    private int peak;
    private boolean waiting;
    boolean update(boolean active,long id,int peers) {
        if(!active) {session=0;peak=0;waiting=false;return false;}
        if(id==0 || peers<0 || peers>16) return waiting;
        if(session!=id) {session=id;peak=0;waiting=false;}
        peak=Math.max(peak,peers);
        waiting=peak>0 && peers<peak;
        return waiting;
    }
}
