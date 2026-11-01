package com.carlos.boz;
/** Radial, continuous dead zone; each stick is processed independently. */
final class StickMath {
    static void filter(float x, float y, float dead, float[] out) {
        if (Float.isNaN(x) || Float.isNaN(y) || Float.isInfinite(x) || Float.isInfinite(y)) { out[0]=out[1]=0; return; }
        float length=(float)Math.sqrt(x*x+y*y);
        if(length<=dead) {out[0]=out[1]=0;return;}
        float magnitude=(Math.min(1f,length)-dead)/(1f-dead);
        out[0]=x/length*magnitude;out[1]=y/length*magnitude;
    }
}
