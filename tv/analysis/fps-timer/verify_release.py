#!/usr/bin/env python3
"""Verify final APK signature/alignment, expected native delta and untouched drops."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import lzma
import os
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    root = args.root.resolve()
    evidence = root / 'build/analysis/fps-timer'
    apk = root / 'BOZ-TV.apk'
    before = evidence / 'BOZ-TV-before-fps-timer.apk'
    bt = Path(os.environ.get('ANDROID_HOME', str(Path.home() / 'Android/Sdk'))) / 'build-tools/36.0.0'
    subprocess.run([str(bt / 'zipalign'), '-c', '-p', '4', str(apk)], check=True)
    certs = []
    for path in (before, apk):
        result = subprocess.run([str(bt / 'apksigner'), 'verify', '--print-certs', str(path)],
                                capture_output=True, text=True, check=True)
        certs.append(re.search(r'Signer #1 certificate SHA-256 digest: ([0-9a-f]+)', result.stdout).group(1))
    assert certs[0] == certs[1], 'APK certificate changed'
    originals = json.loads((evidence / 'baseline-hashes.json').read_text())
    for name, digest in originals.items():
        assert sha(root / name) == digest, name
    oldzip, newzip = zipfile.ZipFile(before), zipfile.ZipFile(apk)
    oldnames, newnames = set(oldzip.namelist()), set(newzip.namelist())
    changed = sorted(n for n in oldnames | newnames if not n.startswith('META-INF/')
                     and (n not in oldnames or n not in newnames or oldzip.read(n) != newzip.read(n)))
    assert changed == ['assets/boz.s3e', 'classes.dex'], changed
    assert not any('libgadget' in n for n in newnames)
    assert b'TEMPO TOTAL' in newzip.read('classes.dex')
    packed = zipfile.ZipFile(root / 'build/base.apk').read('assets/boz.s3e')
    raw = lambda data: lzma.decompress(data, format=lzma.FORMAT_ALONE)
    spec = importlib.util.spec_from_file_location('pickups', root / 'build/patch_pickups.py')
    pickups = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pickups)
    manifests = {name: json.loads((evidence / (name + '-patches.json')).read_text()) for name in ('fps', 'timer')}
    env = {k: v for k, v in os.environ.items() if k not in ('TABLET', 'LOWEND')}
    with tempfile.TemporaryDirectory(prefix='boz-release-') as tmp:
        path = Path(tmp) / 'boz.s3e'
        baseline = None
        for fps, timer, label in [(0, 0, 'baseline'), (1, 0, 'fps'), (0, 1, 'timer'), (1, 1, 'both')]:
            path.write_bytes(packed)
            env.update(FPS30=str(fps), MATCH_TIMER=str(timer))
            subprocess.run(['python3', '-I', str(root / 'build/patch_s3e.py'), str(path)],
                           check=True, stdout=subprocess.DEVNULL, env=env)
            result = raw(path.read_bytes())
            if label == 'baseline':
                baseline = result
            else:
                expected = bytearray(baseline)
                for item in (manifests['fps'] if fps else []) + (manifests['timer'] if timer else []):
                    a = 0x43f3 + item['offset']
                    old, new = bytes.fromhex(item['old']), bytes.fromhex(item['new'])
                    assert expected[a:a + len(old)] == old, item['label']
                    expected[a:a + len(new)] = new
                assert result == expected, label
                assert result[:0x43f3] == baseline[:0x43f3], 'ICF changed'
            if label == 'both':
                first = path.read_bytes()
                subprocess.run(['python3', '-I', str(root / 'build/patch_s3e.py'), str(path)],
                               check=True, stdout=subprocess.DEVNULL, env=env)
                assert first == path.read_bytes(), 'Not idempotent'
                with contextlib.redirect_stdout(io.StringIO()):
                    expected_final = pickups.apply(result)
                    expected_old = pickups.apply(baseline)
                assert raw(newzip.read('assets/boz.s3e')) == expected_final
                assert raw(oldzip.read('assets/boz.s3e')) == expected_old
    subprocess.run(['git', 'diff', '--check'], cwd=root, check=True)
    report = {
        'apk': str(apk), 'apk_sha256': sha(apk), 'previous_apk_sha256': sha(before),
        'signing_certificate_sha256': certs[0], 'signature_verified': True,
        'alignment_verified': True, 'changed_entries_excluding_signatures': changed,
        'exact_native_delta_verified': True, 'idempotency_verified': True,
        'features_independent_verified': True, 'icf_unchanged': True,
        'drop_scripts_build_script_obb_unchanged': originals,
        'device_testing': 'pending; no TV access or installation performed',
    }
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
