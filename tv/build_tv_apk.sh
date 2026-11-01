#!/bin/sh
# Builds the Android TV APK: the Java in tv/java over a base APK (classes.dex), plus the TV menu JSON
# (tv/assets) and the icon/banner (assets/icon), signed with the same key as before.
# usage: tv/build_tv_apk.sh BASE.apk OUT.apk
# Needs: Android SDK (ANDROID_SDK, default ~/Android/Sdk), apktool.jar + baksmali.jar (BOZ_TOOLS),
#        keystore (BOZ_KEYSTORE, default ~/.config/boz/boz-tv.jks) and its password in
#        ~/.config/boz/keystore.pass (never committed).
set -e
P=$(cd "$(dirname "$0")/.." && pwd)
SDK=${ANDROID_SDK:-$HOME/Android/Sdk}; BT=$SDK/build-tools/36.0.0
T=${BOZ_TOOLS:-$HOME/Downloads/COD.BOZ/Port/build/tools}
KS=${BOZ_KEYSTORE:-$HOME/.config/boz/boz-tv.jks}
PASS=$(cat "$HOME/.config/boz/keystore.pass")
R=$P/build/tv
rm -rf "$R"; mkdir -p "$R/classes" "$R/dex"
javac --release 8 -nowarn -cp "$SDK/platforms/android-36/android.jar" -d "$R/classes" $(find "$P/tv/java" -name '*.java')
"$BT/d8" --min-api 19 --lib "$SDK/platforms/android-36/android.jar" --output "$R/dex" $(find "$R/classes" -name '*.class')
java -jar "$T/baksmali.jar" d -o "$R/smali" "$R/dex/classes.dex"
java -jar "$T/apktool.jar" d -q -f -o "$R/apk" "$1"
cp "$R"/smali/com/carlos/boz/*.smali "$R/apk/smali/com/carlos/boz/"
java -jar "$T/apktool.jar" b -q -o "$R/rebuilt.apk" "$R/apk"
python3 -I - "$1" "$R" "$P" <<'PY'
import os, sys, zipfile
base, r, p = sys.argv[1:]
icon = p + '/assets/icon/'
swap = {'res/drawable-%s/icon.png' % k: icon + 'codboz-pc-%d.png' % v for k, v in
        {'ldpi': 36, 'mdpi': 48, 'hdpi': 72, 'xhdpi': 96, 'xxhdpi': 144, 'xxxhdpi': 192}.items()}
swap['res/drawable-xhdpi/banner.png'] = icon + 'banner-320x180.png'
for rel in ('boz_menus.json', 'menus-style/screens.json'):
    if os.path.exists(p + '/tv/assets/' + rel):
        swap['assets/' + rel] = p + '/tv/assets/' + rel
with zipfile.ZipFile(base) as old, zipfile.ZipFile(r + '/rebuilt.apk') as b, zipfile.ZipFile(r + '/unsigned.apk', 'w') as out:
    for i in old.infolist():
        n = i.filename
        if n.upper().startswith('META-INF/'):
            continue
        data = b.read(n) if n == 'classes.dex' else open(swap[n], 'rb').read() if n in swap else old.read(n)
        out.writestr(i, data)
PY
"$BT/zipalign" -f -p 4 "$R/unsigned.apk" "$R/aligned.apk"
"$BT/apksigner" sign --v4-signing-enabled false --ks "$KS" --ks-pass "pass:$PASS" --ks-key-alias boz --out "$2" "$R/aligned.apk"
"$BT/apksigner" verify "$2" && echo "OK $2"
