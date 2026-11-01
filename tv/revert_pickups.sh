#!/usr/bin/env bash
# Reversão dos patches desta entrega; não executa git reset automaticamente.
# Alternativa DESTRUTIVA para todas as alterações locais:
#   git reset --hard checkpoint-antes-drops
# O reset não restaura APK/OBB ignorados; os backups abaixo continuam necessários.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
for relative in build/patch_s3e.py build/build.sh instalar-na-tv.sh maps/mobile/console.bin obb/blackops_etc.dz build/out/apk/assets/boz.s3e; do
  target="$ROOT/$relative"
  if [[ -f "$target.bak-manual" ]]; then
    cp -p -- "$target.bak-manual" "$target"
  fi
done
# Scripts novos ficam disponíveis para auditoria, mas o build restaurado não os chama.
bash "$HERE/build.sh"
sha256sum "$ROOT/BOZ-TV.apk"
