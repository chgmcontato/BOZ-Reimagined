package com.carlos.boz;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.Log;
import java.io.InputStream;

/** Identidade visual apenas: o SWF e a SurfaceView continuam recebendo os toques. */
final class BrandScreen {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF bounds = new RectF();
    private final Path strip = new Path();
    private final Typeface comic;
    private final Bitmap title;

    BrandScreen(Context context) {
        Bitmap loaded = null;
        try (InputStream in = context.getAssets().open("branding/title-reimagined-v1.png")) {
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inScaled = false;
            loaded = BitmapFactory.decodeStream(in, null, options);
        } catch (Exception e) {
            Log.e("BozPad", "Branding: usando título original", e);
        }
        title = loaded;
        Typeface face;
        try { face = Typeface.createFromAsset(context.getAssets(), "character-menu/comic.ttf"); }
        catch (Exception e) { face = Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC); }
        comic = face;
    }

    void drawTitle(Canvas canvas, int width, int height) {
        if (title == null) return; // fallback: imagem original do jogo continua visível
        canvas.save();
        canvas.scale(width / 1280f, height / 720f);
        bounds.set(0, 0, 1280, 720);
        paint.setColor(0xFFFFFFFF);
        canvas.drawBitmap(title, null, bounds, paint);
        paint.setTypeface(comic);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(29);
        paint.setColor(0xFFF2EEE4);
        canvas.drawText("PRESS A TO START", 640, 660, paint);
        canvas.restore();
    }

    void drawMainBadge(Canvas canvas, int width, int height) {
        canvas.save();
        canvas.scale(width / 1280f, height / 720f);
        // Espaço central sob o logo, fora dos botões e das sondas de reconhecimento.
        strip.reset();
        strip.moveTo(480, 409); strip.lineTo(740, 403);
        strip.lineTo(734, 446); strip.lineTo(474, 451); strip.close();
        paint.setColor(0xFF151211); canvas.drawPath(strip, paint);
        paint.setTypeface(comic);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(29);
        paint.setColor(0xFFF2EEE4);
        canvas.drawText("REIMAGINED", 607, 437, paint);
        canvas.restore();
    }
}
