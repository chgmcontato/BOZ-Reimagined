package com.carlos.boz;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

/**
 * Tela de morte no estilo do Black Ops 1 (PC): escurece, "GAME OVER" grande em vermelho e
 * "You Survived N Rounds"; depois some e revela o placar do jogo (foco em RETRY).
 */
final class GameOverFx extends View {
    private static final long FADE_IN = 900, HOLD = 4200, FADE_OUT = 900;

    private final Paint m_Title = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint m_Glow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint m_Sub = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint m_Bg = new Paint();
    private long m_Start = -1;
    private String m_Rounds;

    GameOverFx(Activity activity) {
        super(activity);
        setVisibility(GONE);
        Typeface bold = Typeface.create(Typeface.SERIF, Typeface.BOLD);
        m_Title.setTypeface(bold);
        m_Title.setTextAlign(Paint.Align.CENTER);
        m_Title.setColor(0xFFB3120E);
        m_Title.setLetterSpacing(0.08f);
        m_Glow.set(m_Title);
        m_Glow.setColor(0xFF4A0000);
        m_Glow.setStyle(Paint.Style.STROKE);
        m_Sub.setTypeface(Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL));
        m_Sub.setTextAlign(Paint.Align.CENTER);
        m_Sub.setColor(0xFFE8E2D6);
        m_Sub.setLetterSpacing(0.05f);
        activity.addContentView(this, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** Mostra a sequência; round <= 0 = não deu para ler a rodada (só "GAME OVER"). */
    void show(int round) {
        m_Rounds = round <= 0 ? null : "You Survived " + round + (round == 1 ? " Round" : " Rounds");
        m_Start = SystemClock.uptimeMillis();
        setVisibility(VISIBLE);
        bringToFront();
        invalidate();
    }

    boolean isShowing() {
        return getVisibility() == VISIBLE;
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        m_Bg.setShader(new LinearGradient(0, 0, 0, h,
                new int[]{0xF0000000, 0xFA0A0000, 0xF0000000}, null, Shader.TileMode.CLAMP));
    }

    @Override
    protected void onDraw(Canvas c) {
        long t = SystemClock.uptimeMillis() - m_Start;
        if (t > FADE_IN + HOLD + FADE_OUT) {
            setVisibility(GONE);
            return;
        }
        float a = t < FADE_IN ? t / (float) FADE_IN
                : t < FADE_IN + HOLD ? 1f : 1f - (t - FADE_IN - HOLD) / (float) FADE_OUT;
        int alpha = Math.round(255 * Math.max(0f, Math.min(1f, a)));
        int w = getWidth(), h = getHeight();

        m_Bg.setAlpha(alpha);
        c.drawRect(0, 0, w, h, m_Bg);

        // o título "assenta" levemente (de 106% para 100%) durante o fade
        float settle = 1f + 0.06f * Math.max(0f, 1f - t / (float) (FADE_IN + 400));
        float size = h * 0.15f * settle;
        m_Title.setTextSize(size);
        m_Glow.setTextSize(size);
        m_Glow.setStrokeWidth(size * 0.08f);
        m_Title.setAlpha(alpha);
        m_Glow.setAlpha(alpha / 2);
        float y = h * 0.47f;
        c.drawText("GAME OVER", w / 2f, y, m_Glow);
        c.drawText("GAME OVER", w / 2f, y, m_Title);

        if (m_Rounds != null && t > 600) {
            float sa = Math.min(1f, (t - 600) / 600f);
            m_Sub.setTextSize(h * 0.05f);
            m_Sub.setAlpha(Math.round(alpha * sa));
            c.drawText(m_Rounds, w / 2f, y + h * 0.11f, m_Sub);
        }
        postInvalidateOnAnimation();
    }
}
