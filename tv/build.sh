#!/usr/bin/env bash
# Gera port/BOZ-TV.apk: APK de TV (apk/) + suporte a controle (java/ + patch_smali.py).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BT="${ANDROID_HOME:-$HOME/Android/Sdk}/build-tools/36.0.0"
ANDROID_JAR="${ANDROID_HOME:-$HOME/Android/Sdk}/platforms/android-36/android.jar"
APKTOOL=(java -jar "$HERE/tools/apktool.jar")
OUT="$HERE/out"
KEY="$HERE/boz-tv.jks"
# Nunca gerar chave nova: APK com outra assinatura não atualiza o instalado na TV.
if [[ ! -f "$KEY" ]]; then
  echo "ERRO: chave $KEY não encontrada." >&2
  echo "Restaure o backup: cp ~/backups/chaves/boz-tv.jks \"$KEY\"" >&2
  exit 1
fi
APK="$OUT/apk"
# Verifique entradas antes de limpar os intermediários. O kit de menus também
# publica OBBs em out/: esses artefatos não pertencem à limpeza do APK.
for input in "$HERE/tools/apktool.jar" "$HERE/tools/baksmali.jar" "$HERE/base.apk" "$ANDROID_JAR" "$BT/d8" "$BT/zipalign" "$BT/apksigner"; do
  [[ -f "$input" ]] || { echo "ERRO: ferramenta/entrada ausente: $input" >&2; exit 1; }
done
rm -rf "$APK" "$OUT/classes" "$OUT/dex" "$OUT/smali"
mkdir -p "$OUT/classes" "$OUT/dex" "$OUT/smali"

# base.apk = APK de TV (banner + LEANBACK) montado a partir do original do jogo
echo "== Decodificando base.apk =="
"${APKTOOL[@]}" d -q -f -o "$APK" "$HERE/base.apk"
# targetSdk 23: some a tela de revisão de permissões de app antigo (instale com adb install -g)
sed -i 's/targetSdkVersion: .*/targetSdkVersion: 23/' "$APK/apktool.yml"
python3 -I "$HERE/patch_brand.py" "$APK"

echo "== Compilando Java =="
javac --release 8 -nowarn -cp "$ANDROID_JAR" -d "$OUT/classes" $(find "$HERE/java" -name '*.java')
"$BT/d8" --min-api 19 --lib "$ANDROID_JAR" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
java -jar "$HERE/tools/baksmali.jar" d -o "$OUT/smali" "$OUT/dex/classes.dex"
cp -r "$OUT/smali/." "$APK/smali/"

echo "== Aplicando hooks no smali =="
python3 -I "$HERE/patch_smali.py" "$APK/smali"
python3 -I "$HERE/patch_text.py" "$APK/assets/blackops_loader.dz"
python3 -I "$HERE/patch_s3e.py" "$APK/assets/boz.s3e"
python3 -I "$HERE/patch_pickups.py" "$APK/assets/boz.s3e"
python3 -I "$HERE/patch_pickups_console.py"
python3 -I "$HERE/menus/menus.py" "$APK/assets/boz_menus.json" >/dev/null
python3 -I "$HERE/patch_lib.py" "$APK/lib/armeabi/libs3e_android.so"
# ícones dos indicadores de granada desenhados pelo app (Hud.java), recortados dos atlas do HUD do jogo
mkdir -p "$APK/assets/hud" && cp "$HERE/hud/"*.png "$APK/assets/hud/"
mkdir -p "$APK/assets/character-menu"
cp "$HERE/character-menu/assets/"* "$APK/assets/character-menu/"
mkdir -p "$APK/assets/branding"
cp "$HERE/branding/assets/title-reimagined-v1.png" "$APK/assets/branding/"
mkdir -p "$APK/assets/main-menu"
cp "$HERE/main-menu/assets/"* "$APK/assets/main-menu/"
# telas SWF repaginadas (StyledScreen.java); mesma geometria usada pelo menus.py
mkdir -p "$APK/assets/menus-style"
cp "$HERE/menus-style/assets/screens.json" "$APK/assets/menus-style/"

# BO1_MUSIC=1: a música do menu (mus_theatre_underscore, tocada ao entrar no frontend) vira "Damned", o tema do
# menu de zumbis do BO1, da trilha do jogo de PC (Steam) em port/Call of Duty Black Ops/Soundtrack
if [[ "${BO1_MUSIC:-0}" == 1 ]]; then
  echo "== Música do menu: Damned (BO1) =="
  cp "$HERE/../Call of Duty Black Ops/Soundtrack/Damned.mp3" "$APK/assets/blackops-music/mus_theatre_underscore.mp3"
fi

# FRIDA=1 ./build.sh -> versão de teste com frida-gadget (porta 27042); nunca usar na versão final
if [[ "${FRIDA:-0}" == 1 ]]; then
  echo "== Incluindo frida-gadget (TESTE) =="
  cp "$HERE/frida/libgadget.so" "$HERE/frida/libgadget.config.so" "$APK/lib/armeabi/"
fi

echo "== Montando APK =="
"${APKTOOL[@]}" b -o "$OUT/unsigned.apk" "$APK"
"$BT/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

# A TV removeu o pacote após falha de instalação incremental. Sem assinatura v4
# lateral, adb não escolhe esse modo automaticamente; instalar com --no-incremental.
rm -f "$HERE/../BOZ-TV.apk.idsig"
"$BT/apksigner" sign --v4-signing-enabled false --ks "$KEY" --ks-pass pass:boztv123 --ks-key-alias boz \
  --out "$HERE/../BOZ-TV.apk" "$OUT/aligned.apk"
echo "OK -> $(cd "$HERE/.." && pwd)/BOZ-TV.apk"
