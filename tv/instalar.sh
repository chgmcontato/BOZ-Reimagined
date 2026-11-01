#!/usr/bin/env bash
# Instala o BOZ Reimagined numa Android TV a partir dos SEUS arquivos originais.
# Uso: bash instalar.sh <IP-da-TV> <base.apk> <blackops_etc.dz> <boz-reimagined.xdelta>
set -euo pipefail
[ $# -eq 4 ] || { echo "Uso: bash instalar.sh <IP-da-TV> <base.apk> <blackops_etc.dz> <patch.xdelta>"; exit 1; }
TV="$1:5555"; APK="$2"; OBB="$3"; PATCH="$4"
APK_SHA=348c3ca53291b28380eca44f25198cc64e2e8ce9d56ed4ac170310aff3a86747
OBB_SHA=4537aabe0be836048bafd7493ac5f18b33c0bc19d6dd0b873533bb953bbd3a91
OUT_SHA=dff2994ecb56fc7817c8198a0e368fc03ec52085612e37a9955e8c25914bd400

for c in adb xdelta3 sha256sum; do command -v $c >/dev/null || { echo "Falta o programa: $c"; exit 1; }; done
check() { echo -n "Conferindo $1... "; [ "$(sha256sum "$1" | cut -d' ' -f1)" = "$2" ] && echo ok || { echo "hash diferente: não é a versão certa"; exit 1; }; }
check "$APK" $APK_SHA
check "$OBB" $OBB_SHA

echo "Aplicando o patch..."
xdelta3 -f -d -s "$APK" "$PATCH" BOZ-TV.apk
check BOZ-TV.apk $OUT_SHA

adb connect "$TV"
A=(adb -s "$TV")
if "${A[@]}" shell pm list packages | grep -q '^package:com.activision.boz$' && \
   ! "${A[@]}" install --no-incremental -r -g BOZ-TV.apk; then
  echo "A versão instalada tem outra assinatura (é o jogo original). Desinstale-a antes (apaga os saves):"
  echo "  adb -s $TV uninstall com.activision.boz"; exit 1
fi
"${A[@]}" shell pm list packages | grep -q '^package:com.activision.boz$' || "${A[@]}" install --no-incremental -r -g BOZ-TV.apk

D=/storage/emulated/0/Android/obb/com.activision.boz
"${A[@]}" shell mkdir -p $D
echo "Copiando o OBB (526 MB, alguns minutos)..."
"${A[@]}" push "$OBB" $D/blackops_etc.dz
echo "Pronto. Abra o Black Ops Zombies na TV."
