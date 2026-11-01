#!/bin/bash
# Extrai todo o BO1 (Steam): .iwd (zip: imagens IWI, sons, scripts) e cada fastfile (.ff) com o Unlinker do
# OpenAssetTools num diretório único de assets (o mesmo asset em vários mapas ocupa o espaço uma vez).
# Para se o disco ficar com menos de MINFREE GB. Uso: bo1_extract.sh [pasta do BO1] [saída]
BO1="${1:-$(dirname "$0")/../../Call of Duty Black Ops}"
OUT="${2:-$(dirname "$0")/../pc/bo1}"
U="${UNLINKER:-$HOME/repos/ext/OpenAssetTools/build/bin/Release_x64/Unlinker}"
MINFREE="${MINFREE:-6}"
mkdir -p "$OUT/main" "$OUT/assets" "$OUT/logs"
free_gb() { df -BG --output=avail "$OUT" | tail -1 | tr -dc 0-9; }
for z in "$BO1"/zone/*/*.ff; do
  n=$(basename "$z" .ff)
  [ -f "$OUT/logs/$n.done" ] && continue
  if [ "$(free_gb)" -lt "$MINFREE" ]; then echo "PARADO: pouco espaço antes de $n"; exit 1; fi
  "$U" --search-path "$BO1/main;$BO1/zone/Common;$BO1/zone/English" --model-format OBJ -o "$OUT/assets" "$z" \
    > "$OUT/logs/$n.log" 2>&1 && touch "$OUT/logs/$n.done"
  echo "zona $n $(free_gb)G"
done
for w in "$BO1"/main/*.iwd; do
  n=$(basename "$w")
  [ -f "$OUT/logs/$n.done" ] && continue
  if [ "$(free_gb)" -lt "$MINFREE" ]; then echo "PARADO: pouco espaço antes de $n"; exit 1; fi
  unzip -n -q "$w" -d "$OUT/main" > "$OUT/logs/$n.log" 2>&1; touch "$OUT/logs/$n.done"
  echo "iwd $n $(free_gb)G"
done
echo FIM
