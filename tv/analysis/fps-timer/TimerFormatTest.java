package com.carlos.boz;
public final class TimerFormatTest {
    public static void main(String[] args) {
        long[] ms = {0, 119999, 120000, 3599999, 3600000, 443045000};
        String[] expected = {"00:00", "01:59", "02:00", "59:59", "01:00:00", "123:04:05"};
        for (int i = 0; i < ms.length; i++) {
            String actual = Hud.formatTime(ms[i], false);
            if (!expected[i].equals(actual)) throw new AssertionError(actual);
        }
        if (!"00:02:00".equals(Hud.formatTime(120000, true))) throw new AssertionError();
        System.out.println("PASS Java timer format: minute/hour boundaries and 123 hours");
    }
}
