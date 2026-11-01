package com.carlos.boz;
public class ControlsAndPartyTest {
 static void check(boolean b){if(!b)throw new AssertionError();}
 public static void main(String[] a) {
  float[] l=new float[2],r=new float[2];
  StickMath.filter(.05f,.05f,.1f,l);check(l[0]==0&&l[1]==0);
  StickMath.filter(.1001f,0,.1f,l);check(l[0]>0&&l[0]<.001f);
  StickMath.filter(1,1,.1f,l);check(Math.abs(Math.hypot(l[0],l[1])-1)<.0001);
  float lx=l[0],ly=l[1];StickMath.filter(-.7f,.4f,.1f,r);check(l[0]==lx&&l[1]==ly);
  StickMath.filter(0,1,.1f,r);check(r[0]==0&&r[1]==1);
  StickMath.filter(Float.NaN,1,.1f,r);check(r[0]==0&&r[1]==0);
  CoopPauseState p=new CoopPauseState();
  check(!p.update(true,1,3));check(p.update(true,1,2));check(p.update(true,1,1));check(p.update(true,1,99));
  check(!p.update(true,1,3));check(p.update(true,1,0));check(!p.update(false,1,0));
  check(!p.update(true,2,1));check(p.update(true,2,0));check(!p.update(true,2,1));
  System.out.println("PASS: radial dead zone continuity, diagonals, independent sticks, peer loss/rejoin and new matches");
 }
}
