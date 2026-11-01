#!/bin/sh
# Refaz toda a análise do boz.s3e e a documentação em docs/re/.
# Precisa do capstone: PYLIB=<pasta com capstone> (pip install --target <pasta> capstone).
# IMG_CACHE (opcional) guarda a imagem descomprimida para as próximas rodadas.
set -e
cd "$(dirname "$0")"
: "${IMG_CACHE:=out/img.bin}"
: "${PYLIB:=$HOME/re-pylib}"
export PYLIB IMG_CACHE
mkdir -p out
python3 analyze.py --cache "$IMG_CACHE"     # funções, RTTI, vtables, importações -> out/db.json
python3 reflect.py                          # campos refletidos -> out/reflect.json
python3 factories.py                        # IwClassFactoryAdd -> out/factories.json
python3 globals_.py                         # singletons e estáticos -> out/globals.json
[ -d ../../BOZ-NX-main ] && python3 nx_import.py   # endereços do port de Switch -> known.json
python3 gendocs.py                          # nomes/módulos -> out/symbols.*, docs/re/*.md
python3 xref.py                             # parâmetros, eventos, tabelas -> docs/re/
