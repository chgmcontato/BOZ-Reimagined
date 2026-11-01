#!/usr/bin/env python3
"""Ferramentas locais para catalogar, extrair e preparar dados do BOZ. Só stdlib.

Use --help. Nenhum comando instala APK, usa ADB ou altera o pacote de origem.
Relatórios distinguem integridade estrutural de compatibilidade em execução.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import sys
import tempfile
import zlib
import bz2
import lzma
import math

import dz
import iwgroup
import clone_level
import hud_patch
import modelcheck

ROOT = Path(__file__).resolve().parents[2]
H = iwgroup.djb2
MAX_ENTRY = 256 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()


def safe_path(value):
    value = str(value).replace('\\', '/')
    if '\x00' in value or value.startswith('/') or re.match(r'^[A-Za-z]:', value):
        raise ValueError('caminho absoluto/inválido: %r' % value)
    if '..' in value.split('/'):
        raise ValueError('caminho escapa da raiz: %r' % value)
    clean = str(PurePosixPath(value))
    if clean in ('', '.'):
        raise ValueError('caminho vazio')
    return clean


def target_file(root, relative):
    target = root / safe_path(relative)
    if not target.resolve().is_relative_to(root.resolve()):
        raise ValueError('link/caminho escapa da raiz: %s' % target)
    return target


def new_directory(path):
    path = Path(path).absolute()
    if path.exists() or path.is_symlink():
        raise ValueError('destino já existe; escolha uma pasta nova: %s' % path)
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def write_json(path, obj):
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')


class Archive:
    def __init__(self, path):
        self.path = Path(path).resolve()
        self.data, self.files, self.chunks, self.archives, self.header_end = dz.read(self.path)
        self.dirs = list(dz.read.dirs)
        self.version = self.data[8]
        self.records = {}
        lower = set()
        for record in self.files:
            name, directory, indices, di = record
            full = safe_path(directory + '/' + name if directory else name)
            if full.casefold() in lower:
                raise ValueError('caminho duplicado (inclusive maiúsculas): %s' % full)
            lower.add(full.casefold())
            self.records[full] = record
        self._externals = {}

    def read(self, path):
        _, _, indices, _ = self.records[path]
        if sum(self.chunks[i][2] for i in indices) > MAX_ENTRY:
            raise ValueError('arquivo excede o limite de extração de 256 MiB')
        parts = []
        for i in indices:
            offset, packed, size, flags, archive_id = self.chunks[i]
            if archive_id:
                rel = safe_path(self.archives[archive_id])
                source = target_file(self.path.parent, rel)
                if archive_id not in self._externals:
                    self._externals[archive_id] = source.read_bytes()
                data = self._externals[archive_id]
            else:
                data = self.data
            if offset + packed > len(data):
                raise ValueError('bloco truncado no pacote %d' % archive_id)
            raw = data[offset:offset + packed]
            # Codecs/transforms não decodificados são recusados, sem fingir que são copy.
            if flags == 0x100:
                out = raw
            elif flags == 0x80:
                out = b'\0' * size
            elif flags == 8:
                dec = zlib.decompressobj()
                out = dec.decompress(raw, size + 1)
                if not dec.eof or dec.unused_data or dec.unconsumed_tail:
                    raise ValueError('stream zlib incompleto/excessivo')
            elif flags == 0x10:
                dec = bz2.BZ2Decompressor()
                out = dec.decompress(raw, max_length=size + 1)
                if not dec.eof or dec.unused_data:
                    raise ValueError('stream bzip incompleto/excessivo')
            elif flags == 0x200:
                out = None
                for candidate in (raw[:5] + struct.pack('<Q', size) + raw[5:], raw):
                    try:
                        dec = lzma.LZMADecompressor(lzma.FORMAT_ALONE, memlimit=128 * 1024 * 1024)
                        value = dec.decompress(candidate, max_length=size + 1)
                        if len(value) == size and dec.eof and not dec.unused_data:
                            out = value
                            break
                    except lzma.LZMAError:
                        pass
                if out is None:
                    raise ValueError('LZMA incompatível/truncado')
            else:
                raise ValueError('flags DTRZ não suportadas: 0x%x' % flags)
            if len(out) != size:
                raise ValueError('bloco %d: tamanho real %d, declarado %d' % (i, len(out), size))
            parts.append(out)
        return b''.join(parts)

    def fingerprint(self):
        return {'path': str(self.path), 'sha256': sha(self.data), 'bytes': len(self.data),
                'version': self.version, 'entries': len(self.records)}


def children(group):
    out = []
    for tag, body in group.chunks:
        if tag != iwgroup.CHILDREN:
            continue
        iwgroup.need(body, 0, 1, 'filhos')
        p = 1
        for _ in range(body[0]):
            e = body.index(b'\0', p)
            iwgroup.need(body, e + 1, 12, 'referência de subgrupo')
            path = safe_path(body[p:e].decode('latin1'))
            if path.endswith('.group'):
                path += '.bin'
            out.append({'path': path, 'hash': '%08x' % struct.unpack_from('<I', body, e + 9)[0]})
            p = e + 13
        if p != len(body):
            raise ValueError('bytes extras no bloco de subgrupos')
    return out


def objects(group):
    for li, lst in enumerate(group.lists()):
        for oi, obj in enumerate(lst.objects):
            yield li, oi, obj


def identity(obj):
    return {'class': iwgroup.name(obj['cls']), 'class_hash': '%08x' % obj['cls'],
            'name': iwgroup.name(obj['name']), 'name_hash': '%08x' % obj['name']}


def decoded(obj):
    import reflres
    import entityspec
    import portals
    import navmesh
    data, cls = obj['data'], obj['cls']
    def reflected_fields(parsed):
        return [{'name': reflres.field_name(f['name']), 'hash': '%08x' % f['name'],
                 'type': reflres.TYPES.get(f['type'], '%08x' % f['type']),
                 'value': reflres.show_value(f), 'raw_hex': f['value'].hex()} for f in parsed['fields']]
    if reflres.is_reflected(data):
        parsed = reflres.parse(data)
        if reflres.build(parsed) != data:
            raise ValueError('objeto refletido não regrava idêntico')
        return {'kind': 'reflected', 'fields': reflected_fields(parsed)}
    if cls == entityspec.ENTITY:
        spec, rest = entityspec.parse(data)
        if entityspec.write_spec(spec) + rest != data:
            raise ValueError('entidade não regrava idêntica')
        def describe(s):
            result = {'components': [], 'children': []}
            for item in s['components']:
                if item['cls']:
                    component = {'type': iwgroup.name(item['type']),
                        'name': iwgroup.name(item['name']), 'data_bytes': len(item['data']),
                        'collision_vertices': len(item.get('verts', b'')) // 12,
                        'bullet_bytes': len(item.get('bullet', b''))}
                    raw = item['data']
                    if raw.startswith(b'LFER'):
                        reflected = reflres.parse(struct.pack('<I', len(raw)) + raw)
                        component['fields'] = reflected_fields(reflected)
                    result['components'].append(component)
            for item in s['children']:
                if item['cls'] and 'spec' in item:
                    result['children'].append({'name': iwgroup.name(item['name']), **describe(item['spec'])})
            return result
        return {'kind': 'entity', **describe(spec), 'opaque_tail_bytes': len(rest)}
    if cls in (portals.PORTAL, portals.OCCLUDER):
        parsed = portals.parse(cls, data)
        if portals.build(cls, parsed) != data:
            raise ValueError('portal/oclusor não regrava idêntico')
        parsed['opaque_tail_bytes'] = len(parsed.pop('rest'))
        return {'kind': 'portal' if cls == portals.PORTAL else 'occluder', **parsed}
    if cls == H('CIsNavMesh'):
        parsed = navmesh.parse(data)
        if navmesh.build(parsed) != data:
            raise ValueError('navmesh não regrava idêntica')
        return {'kind': 'navmesh', 'config': parsed['cfg'], 'version': parsed['version'],
                'tiles': len(parsed['tiles']), 'tile_bytes': sum(len(t) for _, t in parsed['tiles']),
                'opaque_tail_bytes': len(parsed['rest'])}
    if cls == H('CIwModel'):
        return {'kind': 'model', **modelcheck.inspect(data)}
    return None


def scan(archive):
    report = {'schema': 1, 'source': archive.fingerprint(), 'scope': 'static; no runtime approval',
              'groups': [], 'errors': [], 'unresolved_children': [], 'classes': {},
              'decode_counts': {}, 'decode_examples': []}
    catalogue = []
    counts, decoding = collections.Counter(), collections.Counter()
    paths = {p.casefold() for p in archive.records}
    for path in archive.records:
        try:
            data = archive.read(path)
            if not path.endswith('.group.bin'):
                continue
            group = iwgroup.Group.load(data)
            if group.dump() != data:
                raise ValueError('grupo não regrava byte a byte')
            deps = children(group)
            row = {'path': path, 'bytes': len(data), 'sha256': sha(data), 'version': group.version,
                   'objects': 0, 'children': deps}
            for dep in deps:
                if dep['path'].casefold() not in paths:
                    report['unresolved_children'].append({'from': path, **dep})
            for li, oi, obj in objects(group):
                row['objects'] += 1
                counts[iwgroup.name(obj['cls'])] += 1
                record = {'group': path, 'list_index': li, 'object_index': oi, **identity(obj),
                          'bytes': len(obj['data']), 'sha256': sha(obj['data'])}
                try:
                    detail = decoded(obj)
                    record['decoded'] = detail
                    record['decode_status'] = 'decoded' if detail else 'opaque'
                except modelcheck.UnsupportedModel as e:
                    record['decode_status'] = 'unsupported'
                    record['decode_message'] = str(e)
                except (ValueError, AssertionError, struct.error, IndexError, KeyError, RecursionError) as e:
                    record['decode_status'] = 'invalid_for_decoder'
                    record['decode_message'] = str(e)
                except Exception as e:
                    # entityspec.Opaque is an explicit unknown class, not a successful decode.
                    if type(e).__name__ != 'Opaque':
                        raise
                    record['decode_status'] = 'unsupported'
                    record['decode_message'] = str(e)
                decoding[record['class'] + ':' + record['decode_status']] += 1
                if record['decode_status'] in ('unsupported', 'invalid_for_decoder') and len(report['decode_examples']) < 30:
                    report['decode_examples'].append({k: record[k] for k in (
                        'group', 'class', 'name', 'decode_message')})
                catalogue.append(record)
            report['groups'].append(row)
        except (ValueError, AssertionError, struct.error, IndexError, OSError) as e:
            report['errors'].append({'path': path, 'message': str(e)})
    report['classes'] = dict(counts.most_common())
    report['decode_counts'] = dict(sorted(decoding.items()))
    statuses = collections.Counter()
    for key, count in decoding.items():
        statuses[key.rsplit(':', 1)[1]] += count
    report['summary'] = {'groups': len(report['groups']), 'resources': len(catalogue),
                         'errors': len(report['errors']), 'unresolved_children': len(report['unresolved_children']),
                         'decoding': dict(statuses)}
    return report, catalogue


def scan_command(args):
    archive = Archive(args.archive)
    report, catalogue = scan(archive)
    dest = new_directory(args.out)
    with tempfile.TemporaryDirectory(prefix='.portkit-', dir=dest.parent) as temp:
        tmp = Path(temp)
        write_json(tmp / 'report.json', report)
        with (tmp / 'resources.jsonl').open('w', encoding='utf-8') as f:
            for obj in catalogue:
                f.write(json.dumps(obj, ensure_ascii=False, allow_nan=False) + '\n')
        summary = report['summary']
        text = ['# Inventário do pacote', '', '`%s`' % archive.path, '',
                'SHA-256: `%s`' % report['source']['sha256'], '',
                '%d grupos; %d recursos; %d erros de estrutura; %d dependências fora deste pacote.' % (
                    summary['groups'], summary['resources'], summary['errors'], summary['unresolved_children']), '',
                'A regravação idêntica valida o envelope do grupo. Recursos opacos e decodificadores parciais',
                'continuam sem validação semântica. Dependências ausentes podem estar no APK/loader.', '',
                '| Classe | Recursos |', '|---|---:|']
        text += ['| %s | %d |' % item for item in report['classes'].items()]
        text += ['', 'Detalhes: `report.json`. Um recurso por linha em `resources.jsonl`, pesquisável com rg.', '']
        (tmp / 'README.md').write_text('\n'.join(text), encoding='utf-8')
        os.rename(tmp, dest)
    print(json.dumps(report['summary'], ensure_ascii=False))
    print(dest)
    return 1 if report['errors'] or report['summary']['decoding'].get('invalid_for_decoder') else 0


def extract_command(args):
    archive = Archive(args.archive)
    selected = [p for p in archive.records if args.filter.casefold() in p.casefold()]
    if not selected:
        raise ValueError('filtro não encontrou arquivos')
    dest = new_directory(args.out)
    with tempfile.TemporaryDirectory(prefix='.portkit-', dir=dest.parent) as temp:
        tmp = Path(temp)
        manifest = {'schema': 1, 'source': archive.fingerprint(), 'filter': args.filter, 'entries': []}
        for path in selected:
            data = archive.read(path)
            target = target_file(tmp / 'assets', path)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            manifest['entries'].append({'path': path, 'bytes': len(data), 'sha256': sha(data)})
        write_json(tmp / 'extraction.json', manifest)
        os.rename(tmp, dest)
    print('%d arquivos extraídos em %s/assets' % (len(selected), dest))


def resource_diff(before, after):
    ga, gb = iwgroup.Group.load(before), iwgroup.Group.load(after)
    def records(g):
        occurrences = collections.Counter()
        result = {}
        for _, _, obj in objects(g):
            key = (obj['cls'], obj['name'])
            idx = occurrences[key]
            occurrences[key] += 1
            result[key + (idx,)] = obj
        return result
    a, b = records(ga), records(gb)
    out = []
    for key in sorted(a.keys() | b.keys()):
        x, y = a.get(key), b.get(key)
        if x and y and x['data'] == y['data']:
            continue
        obj = y or x
        out.append({**identity(obj), 'occurrence': key[2],
                    'change': 'added' if x is None else 'removed' if y is None else 'modified',
                    'before_bytes': len(x['data']) if x else None, 'after_bytes': len(y['data']) if y else None})
    return out


def diff_archives(a, b):
    result = {'before': a.fingerprint(), 'after': b.fingerprint(), 'added': [], 'removed': [], 'changed': []}
    for path in sorted(a.records.keys() | b.records.keys()):
        if path not in a.records:
            result['added'].append(path)
        elif path not in b.records:
            result['removed'].append(path)
        else:
            x, y = a.read(path), b.read(path)
            if x != y:
                row = {'path': path, 'before_sha256': sha(x), 'after_sha256': sha(y),
                       'before_bytes': len(x), 'after_bytes': len(y)}
                if path.endswith('.group.bin'):
                    try:
                        row['resources'] = resource_diff(x, y)
                    except ValueError as e:
                        row['resource_diff_error'] = str(e)
                result['changed'].append(row)
    return result


def json_output(path, value):
    dest = Path(path)
    if dest.exists() or dest.is_symlink():
        raise ValueError('saída já existe: %s' % dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open('x', encoding='utf-8') as f:
        json.dump(value, f, indent=2, ensure_ascii=False, allow_nan=False)
        f.write('\n')


def diff_command(args):
    result = diff_archives(Archive(args.before), Archive(args.after))
    json_output(args.out, result)
    print('adicionados=%d removidos=%d alterados=%d' % tuple(len(result[k]) for k in ('added', 'removed', 'changed')))


def clone_entries(archive, old, new):
    if not re.fullmatch('[a-z][a-z0-9_]{2,31}', new) or not re.fullmatch('[a-z][a-z0-9_]{2,31}', old):
        raise ValueError('nome de mapa: 3–32 caracteres minúsculos, números ou underscore')
    if len(old) != len(new) or old == new:
        raise ValueError('este clone exige nomes distintos com o mesmo tamanho')
    if 'levels/%s/%s.group.bin' % (old, old) not in archive.records:
        raise ValueError('mapa de origem não encontrado')
    if any(p.startswith('levels/%s/' % new) for p in archive.records):
        raise ValueError('o mapa de destino já existe no pacote')
    clones = {}
    for path in archive.records:
        # Mesmo conjunto de arquivos do clonador legado, incluindo arms/weapons/loading.
        if old in path.lower():
            target = safe_path(clone_level.swap(path, old, new))
            if target.casefold() in {p.casefold() for p in archive.records}:
                raise ValueError('clone substituiria arquivo existente: %s' % target)
            data = archive.read(path)
            if path.endswith('.group.bin'):
                data = clone_level.clone_group(data, old, new)
            if target in clones:
                raise ValueError('colisão de caminhos do clone')
            clones[target] = data
    return clones


def init_command(args):
    archive = Archive(args.archive)
    clones = clone_entries(archive, args.base, args.name)
    # Exige também a chave de loading antes de criar um projeto que não poderia ser montado.
    clone_level.clone_loading_key(archive.read('fixed/fixed.group.bin'), args.base, args.name)
    dest = new_directory(args.out)
    with tempfile.TemporaryDirectory(prefix='.portkit-', dir=dest.parent) as temp:
        tmp = Path(temp)
        manifest = {'schema': 1, 'source': archive.fingerprint(), 'base_map': args.base, 'map': args.name,
                    'runtime_registration': 'pending: APK map table and frontend menu',
                    'assets': [{'path': p, 'baseline_sha256': sha(d)} for p, d in sorted(clones.items())]}
        for path, data in clones.items():
            target = target_file(tmp / 'assets', path)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        write_json(tmp / 'map.json', manifest)
        (tmp / 'README.md').write_text(
            '# Projeto de mapa: %s\n\nEdite os grupos em `assets/`. A origem fica preservada.\n\n'
            'Montar: `python3 maps/tools/portkit.py map-build %s --out maps/out/%s.dz`\n\n'
            'Este projeto clona dados. O nome ainda precisa ser registrado na tabela do APK e no menu.\n'
            'Geometria nova exige colisão, navmesh, portais e entidades coerentes.\n' % (args.name, dest, args.name),
            encoding='utf-8')
        os.rename(tmp, dest)
    print('Projeto %s: %d arquivos. Registro no APK/menu continua pendente.' % (dest, len(clones)))


def validate_changed_group(before, after):
    ga, gb = iwgroup.Group.load(before), iwgroup.Group.load(after)
    if (ga.version, ga.pad, ga.tail) != (gb.version, gb.pad, gb.tail):
        raise ValueError('versão/cabeçalho/terminador mudou no grupo editado')
    if gb.dump() != after:
        raise ValueError('grupo editado não regrava idêntico')
    children(gb)
    old_models = collections.Counter((o['cls'], o['name'], sha(o['data'])) for _, _, o in objects(ga))
    cpu_positions = {o['name'] for _, _, o in objects(ga) if o['cls'] == H('CIwModel')
                     and len(o['data']) >= 4 and struct.unpack_from('<I', o['data'])[0] & 0x100000}
    import reflres
    for _, _, obj in objects(gb):
        if reflres.is_reflected(obj['data']):
            reflected = reflres.parse(obj['data'])
            if reflres.build(reflected) != obj['data']:
                raise ValueError('recurso refletido inválido no grupo editado')
        if obj['cls'] == H('CIwModel'):
            key = (obj['cls'], obj['name'], sha(obj['data']))
            if old_models[key]:
                old_models[key] -= 1
            else:
                checked = modelcheck.inspect(obj['data'])  # modelo desconhecido não é aprovado
                if obj['name'] in cpu_positions and not checked['flags'] & 0x100000:
                    raise ValueError('modelo perdeu a flag 0x100000 de posições na CPU (risco de crash na sala)')


def destination_archive(value, source):
    if Path(value).is_symlink():
        raise ValueError('saída não pode ser um link simbólico')
    dest = Path(value).resolve()
    if dest == source.path or dest.exists() or dest.is_symlink():
        raise ValueError('use um novo arquivo de saída; origem/saída existente não será substituída')
    if Path(str(dest) + '.json').exists():
        raise ValueError('relatório de saída já existe')
    dest.parent.mkdir(parents=True, exist_ok=True)
    return dest


def publish(archive, entries, dest, report):
    # DTRZ temporário validado antes de publicar. O relatório é parte do resultado local.
    with tempfile.TemporaryDirectory(prefix='.portkit-', dir=dest.parent) as temp:
        staged = Path(temp) / 'package.dz'
        dz.write(staged, entries, version=archive.version, dirs=archive.dirs)
        check = Archive(staged)
        for path in check.records:
            check.read(path)
        report['output'] = {'path': str(dest), 'sha256': file_sha(staged), 'bytes': staged.stat().st_size}
        write_json(Path(temp) / 'report.json', report)
        os.link(staged, dest)  # não substitui um destino criado durante a validação
        os.link(Path(temp) / 'report.json', Path(str(dest) + '.json'))
    print(dest)


def build_command(args):
    root = Path(args.project).resolve()
    manifest = json.loads((root / 'map.json').read_text(encoding='utf-8'))
    if manifest['schema'] != 1:
        raise ValueError('versão de projeto desconhecida')
    archive = Archive(manifest['source']['path'])
    if sha(archive.data) != manifest['source']['sha256']:
        raise ValueError('pacote de origem mudou desde a criação do projeto')
    clones = clone_entries(archive, manifest['base_map'], manifest['map'])
    declared = {r['path']: r['baseline_sha256'] for r in manifest['assets']}
    if len(declared) != len(manifest['assets']) or declared != {p: sha(d) for p, d in clones.items()}:
        raise ValueError('manifesto não corresponde ao clone da origem')
    actual = {p.relative_to(root / 'assets').as_posix() for p in (root / 'assets').rglob('*') if p.is_file()}
    if actual != set(clones):
        raise ValueError('arquivos ausentes/extras em assets: %s' % sorted(actual ^ set(clones)))
    changes, prepared = [], []
    for path, baseline in clones.items():
        data = target_file(root / 'assets', path).read_bytes()
        if path.endswith('.group.bin'):
            validate_changed_group(baseline, data)
        if data != baseline:
            changes.append({'path': path, 'before_sha256': sha(baseline), 'after_sha256': sha(data)})
        parent, name = path.rsplit('/', 1) if '/' in path else ('', path)
        prepared.append((parent.replace('/', '\\'), name, data))
    entries = []
    for path, (name, directory, _, di) in archive.records.items():
        data = archive.read(path)
        if path == 'fixed/fixed.group.bin':
            data = clone_level.clone_loading_key(data, manifest['base_map'], manifest['map'])
        entries.append((directory, name, data, di))
    known = {p.casefold() for p in archive.records} | {p.casefold() for p in clones}
    missing = []
    for directory, name, data in prepared:
        if name.endswith('.group.bin'):
            for dep in children(iwgroup.Group.load(data)):
                if dep['path'].casefold().startswith('levels/%s/' % manifest['map']) and dep['path'].casefold() not in known:
                    missing.append(dep['path'])
    if missing:
        raise ValueError('subgrupos do mapa ausentes: %s' % missing)
    dest = destination_archive(args.out, archive)
    publish(archive, entries + prepared, dest, {'schema': 1, 'source': archive.fingerprint(),
        'map': manifest['map'], 'base_map': manifest['base_map'], 'edited_assets': changes,
        'runtime_registration': 'pending', 'runtime_tested': False})


def typed_field_value(field, value):
    """Edição de campos de tamanho fixo, sem inventar serialização de tipos opacos."""
    import reflres
    name = reflres.TYPES.get(field['type'])
    formats = {'unsigned int': 'I', 'int': 'i', 'short': 'h', 'unsigned char': 'B',
               'char': 'b', 'float': 'f', 'bool': '?', 'CIwFVec2': '2f',
               'CIwFVec3': '3f', 'CIwFQuat': '4f'}
    if name not in formats:
        raise ValueError('tipo não editável automaticamente: %s' % name)
    values = value if isinstance(value, list) else [value]
    if name == 'bool':
        if type(value) is not bool:
            raise ValueError('bool exige true ou false em JSON')
    elif name == 'float' or name.startswith('CIwF'):
        if any(type(v) not in (int, float) or not math.isfinite(v) for v in values):
            raise ValueError('valor exige números finitos')
    elif any(type(v) is not int for v in values):
        raise ValueError('valor exige inteiros')
    try:
        result = struct.pack('<' + formats[name], *values)
    except (struct.error, OverflowError) as e:
        raise ValueError('valor fora do tipo %s: %s' % (name, e)) from e
    if len(result) != len(field['value']):
        raise ValueError('edição mudaria o tamanho do campo')
    return result


def set_command(args):
    import reflres
    root = Path(args.project).resolve()
    manifest = json.loads((root / 'map.json').read_text(encoding='utf-8'))
    relative = safe_path(args.group)
    if relative not in {r['path'] for r in manifest['assets']}:
        raise ValueError('grupo fora do manifesto')
    path = target_file(root / 'assets', relative)
    before = path.read_bytes()
    group = iwgroup.Group.load(before)
    target_hash = int(args.resource[2:], 16) if args.resource.startswith('0x') else H(args.resource)
    candidates = [o for _, _, o in objects(group) if o['name'] == target_hash
                  and (args.cls is None or o['cls'] == H(args.cls))]
    if len(candidates) != 1:
        raise ValueError('recurso ausente/ambíguo; use --class para desambiguar')
    obj = candidates[0]
    reflected = reflres.parse(obj['data'])
    fields = [f for f in reflected['fields'] if f['name'] == H(args.field)]
    if len(fields) != 1:
        raise ValueError('campo ausente/ambíguo')
    field = fields[0]
    expected = typed_field_value(field, json.loads(args.expect))
    if expected != field['value']:
        raise ValueError('valor atual difere de --expect; nenhuma alteração feita')
    old_value = field['value']
    field['value'] = typed_field_value(field, json.loads(args.value))
    obj['data'] = reflres.build(reflected)
    after = group.dump()
    validate_changed_group(before, after)
    if before == after:
        print('Campo já tem o valor pedido; nenhum arquivo alterado.')
        return
    history = root / 'history'
    history.mkdir(exist_ok=True)
    record = {'group': relative, **identity(obj), 'field': args.field,
              'before_hex': old_value.hex(), 'after_hex': field['value'].hex(),
              'before_sha256': sha(before), 'after_sha256': sha(after)}
    fd, temp = tempfile.mkstemp(prefix='.field-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(after)
            f.flush()
            os.fsync(f.fileno())
        # O registro é escrito antes da publicação e permite reconstruir o valor anterior.
        log = history / ('%04d-%s.json' % (len(list(history.glob('*.json'))) + 1, sha(after)[:12]))
        json_output(log, record)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    print('%s: %s atualizado. Histórico: %s' % (relative, args.field, log))


def hud_snapshot(data):
    group = iwgroup.Group.load(data)
    result = []
    protected = ('grenade_button', 'grenade_icon', 'Grenade_Label', 'Clip_Label', 'ig_poiReg')
    for li, oi, obj in objects(group):
        if obj['cls'] not in hud_patch.CLASSES:
            continue
        envelope = struct.pack('<II', obj['cls'], obj['name']) + obj['data']
        result.append({'list': li, 'object': oi, **identity(obj), 'bytes': len(obj['data']),
                       'sha256': sha(obj['data']), 'name_occurrences': {
                           n: envelope.count(struct.pack('<I', H(n))) for n in protected}})
    return result


def validate_hud_atlases(data):
    """As áreas usadas para esconder controles devem continuar transparentes."""
    group = iwgroup.Group.load(data)
    checks = []
    for name, rect in (('HUD_ALL', hud_patch.CLEAR_UV), ('hud_icons', ((144, 0), (160, 16)))):
        candidates = [o for _, _, o in objects(group) if o['cls'] == H('CIwTexture') and o['name'] == H(name)]
        if len(candidates) != 1:
            raise ValueError('atlas %s ausente/ambíguo' % name)
        raw = candidates[0]['data']
        iwgroup.need(raw, 0, 23, 'cabeçalho de textura')
        width, height, pitch = struct.unpack_from('<HHH', raw, 13)
        if raw[10] != 5 or pitch != 2 * width or len(raw) != 24 + pitch * height:
            raise ValueError('atlas %s fora do perfil RGBA4444 conhecido' % name)
        (x0, y0), (x1, y1) = rect
        if not 0 <= x0 < x1 <= width or not 0 <= y0 < y1 <= height:
            raise ValueError('recorte transparente fora do atlas')
        for y in range(y0, y1):
            for x in range(x0, x1):
                pixel, = struct.unpack_from('<H', raw, 23 + y * pitch + 2 * x)
                if pixel & 15:
                    raise ValueError('recorte de %s tem pixel opaco em %d,%d' % (name, x, y))
        checks.append({'atlas': name, 'width': width, 'height': height, 'clear_rect': rect,
                       'sha256': sha(raw), 'transparent': True})
    return checks


def hud_command(args):
    archive = Archive(args.archive)
    path = 'ingame/ingame.group.bin'
    before = archive.read(path)
    atlases = validate_hud_atlases(before)
    after, edits = hud_patch.patch_report(before)
    again, second_edits = hud_patch.patch_report(after)
    if again != after or second_edits:
        raise ValueError('patch de HUD não é idempotente')
    a, b = hud_snapshot(before), hud_snapshot(after)
    for x, y in zip(a, b):
        if x['name_occurrences'] != y['name_occurrences']:
            raise ValueError('patch alterou identificadores protegidos')
    if not a:
        raise ValueError('nenhum layout de HUD conhecido')
    report = {'schema': 1, 'source': archive.fingerprint(), 'group': path,
              'group_before_sha256': sha(before), 'group_after_sha256': sha(after),
              'bytes_preserved': len(before) == len(after), 'idempotent': True,
              'changed_resources': edits, 'layouts': a, 'atlases': atlases, 'runtime_tested': False,
              'limits': ['Busca de propriedades por padrões; árvore IwUI completa ainda não decodificada.',
                         'Overlay Java e leituras de memória exigem teste separado no aparelho.']}
    if args.command == 'hud-plan':
        json_output(args.out, report)
        print('%d layouts; %d recursos seriam alterados.' % (len(a), len(edits)))
        return
    expected = json.loads(Path(args.plan).read_text(encoding='utf-8'))
    if expected != json.loads(json.dumps(report)):
        raise ValueError('plano difere da origem/implementação atual; gere hud-plan novamente')
    dest = destination_archive(args.out, archive)
    entries = [(directory, name, after if p == path else archive.read(p), di)
               for p, (name, directory, _, di) in archive.records.items()]
    publish(archive, entries, dest, report)


def doctor_command(args):
    paths = ['build/base.apk', 'obb/blackops_etc.dz', 'obb_moon/blackops_etc.dz',
             'maps/tools/hashes.json', 'build/re/out/reflect.json', 'build/re/out/symbols.json']
    print('Python:', sys.version.split()[0], '| projeto:', ROOT)
    for rel in paths:
        p = ROOT / rel
        print('%-40s %s' % (rel, ('%d bytes' % p.stat().st_size) if p.exists() else 'ausente'))
    print('Portkit usa somente a biblioteca padrão. Build Android e conversor NML têm dependências próprias.')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    q = sub.add_parser('doctor', help='verificar entradas locais')
    q.set_defaults(func=doctor_command)
    for cmd, func, helptext in (
        ('scan', scan_command, 'inventário e recursos JSONL'),
        ('extract', extract_command, 'extrair com manifesto SHA-256'),
        ('map-init', init_command, 'novo projeto a partir de um mapa existente'),
        ('hud-plan', hud_command, 'prévia verificável dos patches de HUD'),
        ('hud-apply', hud_command, 'aplicar plano a uma cópia do pacote')):
        q = sub.add_parser(cmd, help=helptext)
        q.add_argument('archive')
        q.add_argument('--out', required=True)
        q.set_defaults(func=func)
        if cmd == 'extract':
            q.add_argument('--filter', default='')
        if cmd == 'map-init':
            q.add_argument('--base', default='kino')
            q.add_argument('--name', required=True)
        if cmd == 'hud-apply':
            q.add_argument('--plan', required=True)
    q = sub.add_parser('diff', help='comparar pacotes e recursos alterados')
    q.add_argument('before')
    q.add_argument('after')
    q.add_argument('--out', required=True)
    q.set_defaults(func=diff_command)
    q = sub.add_parser('map-build', help='montar projeto em novo pacote')
    q.add_argument('project')
    q.add_argument('--out', required=True)
    q.set_defaults(func=build_command)
    q = sub.add_parser('map-set', help='editar campo refletido com tipo e valor anterior conferidos')
    q.add_argument('project')
    q.add_argument('--group', required=True, help='caminho interno completo do grupo')
    q.add_argument('--resource', required=True, help='nome ou hash com prefixo 0x')
    q.add_argument('--class', dest='cls')
    q.add_argument('--field', required=True)
    q.add_argument('--expect', required=True, help='valor anterior em JSON')
    q.add_argument('--value', required=True, help='novo valor em JSON')
    q.set_defaults(func=set_command)
    args = p.parse_args(argv)
    try:
        return args.func(args) or 0
    except (ValueError, OSError, KeyError, IndexError, struct.error, AssertionError) as e:
        print('ERRO: %s' % e, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
