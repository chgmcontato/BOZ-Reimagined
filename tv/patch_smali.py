#!/usr/bin/env python3
"""Aplica (de forma idempotente) os hooks do controle no smali decodificado pelo apktool.
Uso: patch_smali.py <apk/smali>"""
import re
import sys
from pathlib import Path

smali = Path(sys.argv[1])
root = smali / "com/ideaworks3d/marmalade"
MARK = "# boz-tv"


def method_span(src, signature):
    start = src.index(".method " + signature)
    end = src.index(".end method", start) + len(".end method")
    return start, end


def patch(path, fn, base=None):
    p = (base or root) / path
    src = p.read_text()
    out = fn(src)
    if out != src:
        p.write_text(out)
        print("patched", path)


def loader_activity(src):
    if MARK in src:
        return src
    # 1) onCreate: anexa o Pad (tela de controle) no final
    s, e = method_span(src, "protected onCreate(Landroid/os/Bundle;)V")
    body = src[s:e]
    i = body.rindex("return-void")
    body = (body[:i] + MARK + "\n    invoke-static {p0}, Lcom/carlos/boz/Pad;->attach(Landroid/app/Activity;)V\n\n    "
            + body[i:])
    src = src[:s] + body + src[e:]

    # 2) onGenericMotionEvent: analógicos do controle vão para o Pad
    s, e = method_span(src, "public onGenericMotionEvent(Landroid/view/MotionEvent;)Z")
    body = src[s:e]
    i = body.index(".prologue") + len(".prologue")
    body = (body[:i] + "\n    " + MARK + "\n"
            "    invoke-static {p1}, Lcom/carlos/boz/Pad;->onMotion(Landroid/view/MotionEvent;)Z\n"
            "    move-result v0\n"
            "    if-eqz v0, :boz_pass\n"
            "    return v0\n"
            "    :boz_pass\n" + body[i:])
    src = src[:s] + body + src[e:]

    # 2b) dispatchGenericMotionEvent: pega os analógicos antes que a view do jogo os consuma
    src += """
.method public dispatchGenericMotionEvent(Landroid/view/MotionEvent;)Z
    .locals 1
    %s
    invoke-static {p1}, Lcom/carlos/boz/Pad;->onMotion(Landroid/view/MotionEvent;)Z
    move-result v0
    if-eqz v0, :boz_sys
    return v0
    :boz_sys
    invoke-super {p0, p1}, Landroid/app/Activity;->dispatchGenericMotionEvent(Landroid/view/MotionEvent;)Z
    move-result v0
    return v0
.end method
""" % MARK

    # 3) dispatchKeyEvent: botões do controle viram botões do Xperia Play e vão direto ao nativo
    #    (o onKeyDown/onKeyUp deste APK não repassa nada ao jogo)
    src += """
.method public dispatchKeyEvent(Landroid/view/KeyEvent;)Z
    .locals 1
    %s
    invoke-static {p0, p1}, Lcom/carlos/boz/Pad;->dispatchKey(Landroid/app/Activity;Landroid/view/KeyEvent;)Z
    move-result v0
    if-eqz v0, :boz_sys
    return v0
    :boz_sys
    invoke-super {p0, p1}, Landroid/app/Activity;->dispatchKeyEvent(Landroid/view/KeyEvent;)Z
    move-result v0
    return v0
.end method
""" % MARK
    return src


def loader_thread(src):
    # o jogo liga o modo Xperia Play quando o modelo bate com XperiaDeviceID (R800*)
    s, e = method_span(src, "public getDeviceModel()Ljava/lang/String;")
    body = re.sub(r"sget-object v0, Landroid/os/Build;->MODEL:Ljava/lang/String;",
                  'const-string v0, "R800i"  ' + MARK, src[s:e])
    return src[:s] + body + src[e:]


def loader_keyboard(src):
    # bit 4 = keypad de jogo exposto (slider do Xperia Play aberto)
    s, e = method_span(src, "public getKeyboardInfo()I")
    if MARK in src[s:e]:
        return src
    body = re.sub(r"(\n\s*)return (v\d+)", r"\1or-int/lit8 \2, \2, 0x4  " + MARK + r"\1return \2", src[s:e])
    return src[:s] + body + src[e:]


def play_services(src):
    # sem Play Services na TV: não mostra o diálogo "Problema desconhecido com o Google Play Services"
    s, e = method_span(src, "private static a(ILandroid/app/Activity;Landroid/support/v4/app/Fragment;ILandroid/content/DialogInterface$OnCancelListener;)Landroid/app/Dialog;")
    if MARK in src[s:e]:
        return src
    body = src[s:e].replace(".locals 5", ".locals 5\n    " + MARK + "\n    const/4 v0, 0x0\n    return-object v0", 1)
    return src[:s] + body + src[e:]


def game_helper(src):
    # login do Play Games falha na TV: não mostra "Failed to sign in"
    for sig in ("public static showFailureDialog(Landroid/app/Activity;II)V", "public showFailureDialog()V"):
        s, e = method_span(src, sig)
        if MARK in src[s:e]:
            continue
        body = re.sub(r"(\.locals \d+)", r"\1\n    " + MARK + "\n    return-void", src[s:e], count=1)
        src = src[:s] + body + src[e:]
    # sem login do Google Play Games: a TV mostrava a seta de "carregando" até o login desistir. connect() já
    # responde "falhou" na hora (mesmo fim do giveUp, sem o diálogo)
    s, e = method_span(src, "connect()V")
    if MARK not in src[s:e]:
        h = "Lcom/ideaworks3d/marmalade/s3eGooglePlayServices/GameHelper;"
        src = src[:s] + (".method connect()V\n    .locals 1\n    " + MARK + "\n    const/4 v0, 0x0\n"
                         "    iput-boolean v0, p0, " + h + "->mConnectOnStart:Z\n"
                         "    iput-boolean v0, p0, " + h + "->mConnecting:Z\n"
                         "    invoke-virtual {p0, v0}, " + h + "->notifyListener(Z)V\n"
                         "    return-void\n.end method") + src[e:]
    return src


def mod_credit(src):
    # o APK modificado mostra dois Toasts "B站：阳光的酥皮" (crédito do autor do mod) toda vez que abre
    s, e = method_span(src, "public onCreate(Landroid/os/Bundle;)V")
    if MARK in src[s:e]:
        return src
    body = re.sub(r'(const-string v1, "B\\u7ad9[^"]*"\n\n\s*invoke-static \{p0, v1, v0\}, Landroid/widget/Toast;->makeText\([^\n]*\n\n\s*move-result-object v0\n\n\s*)invoke-virtual \{v0\}, Landroid/widget/Toast;->show\(\)V',
                  r"\1" + MARK + " (Toast do crédito removido)", src[s:e])
    return src[:s] + body + src[e:]


patch("LoaderActivity.smali", loader_activity)
patch("com/ideaworks3d/studio/IsDeviceActivity.smali", mod_credit, smali)
patch("s3eGooglePlayServices/GameHelper.smali", game_helper)
patch("LoaderThread.smali", loader_thread)
patch("LoaderKeyboard.smali", loader_keyboard)
patch("com/google/android/gms/common/GooglePlayServicesUtil.smali", play_services, smali)
