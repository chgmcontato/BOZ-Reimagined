#!/bin/sh
# Pseudocódigo C de todas as funções (Ghidra 11.0.3, JDK 17) -> out/decomp/<módulo>.c
# 1ª vez: importa a imagem com os nomes do mapa e roda a análise automática (~15-30 min).
# Depois: só descompila. Variáveis: GHIDRA (pasta do Ghidra), MOD (regex de módulos, padrão todos).
set -e
cd "$(dirname "$0")"
G="${GHIDRA:-$HOME/repos/ext/ghidra_11.0.3_PUBLIC}"
mkdir -p out/ghidra_proj
python3 - <<'PY'
import json
s = json.load(open('out/symbols.json')); g = json.load(open('out/globals.json'))
db = json.load(open('out/db.json')); kn = json.load(open('known.json'))
labels = []
for c in db['classes'].values():
    for v in c['vtables']:
        labels.append([v['addr'] - 8, 'vtable_' + c['name']])
    labels.append([int(c['ti']), 'typeinfo_' + c['name']])
for k, v in g.items():
    labels.append([int(k, 16), ('p_' if v['kind'] == 'singleton-ptr' else 'g_') + v['class']])
for k, (n, _) in kn['data'].items():
    labels.append([int(k, 16), n])
funcs = [[int(k, 16), v['name'], v['mode'] == 'thumb', v['module']] for k, v in s.items()]
json.dump({'funcs': funcs, 'labels': labels}, open('out/ghidra_syms.json', 'w'))
PY
if [ ! -d out/ghidra_proj/boz.rep ] || [ "$REIMPORT" = 1 ]; then
  "$G/support/analyzeHeadless" out/ghidra_proj boz -import out/img.bin -overwrite -loader BinaryLoader \
    -processor ARM:LE:32:v7 -scriptPath ghidra -preScript ImportSyms.py out/ghidra_syms.json \
    -analysisTimeoutPerFile 5400 > out/ghidra_import.log 2>&1
fi
"$G/support/analyzeHeadless" out/ghidra_proj boz -process img.bin -noanalysis -readOnly -scriptPath ghidra \
  -postScript Decompile.py out/ghidra_syms.json out/decomp_all "${MOD:-.}" > out/ghidra_dec.log 2>&1
tail -1 out/ghidra_dec.log
python3 fixpc.py out/decomp_all out/decomp     # resolve strings/globais relativos ao pc
