#!/usr/bin/env python3
"""Publish a validated OBB locally and print a command for a later manual transfer.

This utility is deliberately offline: it never starts ADB or writes to a device.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import os
from pathlib import Path
import shlex
import shutil
import tempfile

from kit import PORT, DEFAULT_WORK, cli_error, file_sha
from repack import build

REMOTE = "/sdcard/Android/obb/com.activision.boz/blackops_etc.dz"


def publish(work=DEFAULT_WORK, output_dir=None):
    work = Path(work).resolve()
    target, report = build(work)
    expected = report["output_sha256"]
    output_dir = Path(output_dir).resolve() if output_dir else PORT / "build/out"
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"blackops_etc-reimagined-{expected[:12]}.dz"
    if output.exists():
        if file_sha(output) != expected:
            raise ValueError(f"Saída existente não corresponde ao hash esperado: {output}")
    else:
        fd, temporary = tempfile.mkstemp(prefix=".obb-publish-", dir=output_dir)
        os.close(fd)
        try:
            shutil.copyfile(target, temporary)
            if file_sha(temporary) != expected:
                raise ValueError("Hash do OBB copiado diverge da build validada")
            os.replace(temporary, output)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
    command = shlex.join(["adb", "push", str(output), REMOTE])
    print("OBB novo salvo localmente:", output)
    print("SHA256:", expected)
    print("Comando para execução manual em outra sessão (não executado):")
    print(command)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--output-dir", type=Path, default=None)
    args = parser.parse_args()
    publish(args.work, args.output_dir)


if __name__ == "__main__":
    cli_error(main)
