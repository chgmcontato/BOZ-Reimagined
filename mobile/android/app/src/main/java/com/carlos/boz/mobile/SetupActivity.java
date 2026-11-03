package com.carlos.boz.mobile;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.StatFs;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Locale;

/**
 * First screen of the proof of concept: the player picks their own BOZ-TV.apk (the Reimagined
 * TV build, made from their original APK) and the original OBB with the Storage Access Framework.
 * Both are copied into the app's external files folder, where the runtime reads them like the PC
 * launcher's install. No game file is part of this app.
 */
public final class SetupActivity extends Activity {
    private static final int PICK_APK = 1;
    private static final int PICK_OBB = 2;
    /** blackops_etc.dz of Android 1.0.11 (tv/README.md); another size is shown as a warning. */
    private static final long OBB_SIZE = 526783202L;

    private File m_Import, m_Data, m_Apk, m_Obb;
    private TextView m_ApkState, m_ObbState, m_Progress;
    private Button m_Play, m_PickApk, m_PickObb;
    private volatile boolean m_Copying;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        File base = getExternalFilesDir(null);
        m_Import = new File(base, "import");
        m_Data = new File(base, "game");
        m_Import.mkdirs();
        m_Data.mkdirs();
        m_Apk = new File(m_Import, "BOZ-TV.apk");
        m_Obb = new File(m_Import, "blackops_etc.dz");
        setContentView(buildLayout());
        refresh();
    }

    private View buildLayout() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setPadding(dp(24), dp(24), dp(24), dp(24));
        column.setBackgroundColor(0xFF151211);

        TextView title = text("BLACK OPS ZOMBIES  ·  REIMAGINED MOBILE", 22, 0xFFF2EEE4);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        column.addView(title);
        column.addView(text("Proof of concept: the PC runtime on ARM64. Pick your own files; nothing is"
                + " downloaded or shared.", 14, 0xFFD5C9AC));

        m_ApkState = text("", 15, 0xFFF2EEE4);
        m_PickApk = button("Choose BOZ-TV.apk", v -> pick(PICK_APK));
        m_ObbState = text("", 15, 0xFFF2EEE4);
        m_PickObb = button("Choose OBB (blackops_etc.dz)", v -> pick(PICK_OBB));
        m_Progress = text("", 14, 0xFFD5C9AC);
        m_Play = button("PLAY", v -> play());
        m_Play.setBackgroundColor(0xFF791D24);

        column.addView(space());
        column.addView(m_ApkState);
        column.addView(m_PickApk);
        column.addView(space());
        column.addView(m_ObbState);
        column.addView(m_PickObb);
        column.addView(space());
        column.addView(m_Progress);
        column.addView(m_Play);
        column.addView(space());
        column.addView(text("Log of the last run: " + new File(getExternalFilesDir(null), "boz-runtime.log")
                + "  (also in logcat, tag BozRuntime)", 12, 0xFF827763));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(0xFF151211);
        scroll.addView(column);
        return scroll;
    }

    private void refresh() {
        m_ApkState.setText(m_Apk.isFile()
                ? String.format(Locale.US, "APK: %s (%,d bytes)%s", m_Apk.getName(), m_Apk.length(),
                        installed() ? ", installed" : ", will be installed on PLAY")
                : "APK: not chosen yet");
        String obb = "OBB: not chosen yet";
        if (m_Obb.isFile()) {
            obb = String.format(Locale.US, "OBB: %s (%,d bytes)%s", m_Obb.getName(), m_Obb.length(),
                    m_Obb.length() == OBB_SIZE ? "" : "  WARNING: expected 526,783,202 bytes");
        }
        m_ObbState.setText(obb);
        boolean ready = m_Apk.isFile() && m_Obb.isFile() && !m_Copying;
        m_Play.setEnabled(ready);
        m_Play.setAlpha(ready ? 1f : 0.4f);
        m_PickApk.setEnabled(!m_Copying);
        m_PickObb.setEnabled(!m_Copying);
    }

    /** The runtime unpacks the game image on install; a newer APK means installing again. */
    private boolean installed() {
        File image = new File(m_Data, "reimagined/assets/boz.s3e.unpacked");
        if (!image.isFile()) image = new File(m_Data, "original/assets/boz.s3e.unpacked");
        return image.isFile() && image.lastModified() >= m_Apk.lastModified();
    }

    private void pick(int what) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, what);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        final File target = request == PICK_APK ? m_Apk : m_Obb;
        m_Copying = true;
        refresh();
        new Thread(() -> copy(uri, target), "boz-import").start();
    }

    private void copy(Uri uri, File target) {
        File partial = new File(target.getPath() + ".part");
        String error = null;
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(partial)) {
            if (in == null) throw new IOException("cannot open the chosen file");
            long free = new StatFs(m_Import.getPath()).getAvailableBytes();
            byte[] buffer = new byte[1 << 20];
            long total = 0, shown = 0;
            for (int n; (n = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, n);
                total += n;
                if (total - shown >= (16L << 20)) {
                    shown = total;
                    final String line = String.format(Locale.US, "Copying %s: %,d MB (free before: %,d MB)",
                            target.getName(), total >> 20, free >> 20);
                    runOnUiThread(() -> m_Progress.setText(line));
                }
            }
        } catch (IOException | SecurityException e) {
            error = e.getMessage();
        }
        if (error == null && !partial.renameTo(target)) {
            target.delete();
            if (!partial.renameTo(target)) error = "could not move the copy into place";
        }
        if (error != null) partial.delete();
        final String message = error == null ? "Copied " + target.getName() + "." : "Copy failed: " + error;
        runOnUiThread(() -> {
            m_Copying = false;
            m_Progress.setText(message);
            refresh();
        });
    }

    private void play() {
        ArrayList<String> args = new ArrayList<>();
        args.add("--data");
        args.add(m_Data.getPath());
        if (!installed()) {
            args.add("--install");
            args.add(m_Apk.getPath());
        }
        args.add("--obb");
        args.add(m_Obb.getPath());
        args.add("--play");
        Intent intent = new Intent(this, BozActivity.class);
        intent.putExtra(BozActivity.EXTRA_ARGS, args.toArray(new String[0]));
        startActivity(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (m_ApkState != null) refresh();
    }

    private TextView text(String s, int sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setPadding(0, dp(4), 0, dp(4));
        return t;
    }

    private Button button(String label, View.OnClickListener click) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setGravity(Gravity.CENTER);
        b.setOnClickListener(click);
        return b;
    }

    private View space() {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(1, dp(16)));
        return v;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
