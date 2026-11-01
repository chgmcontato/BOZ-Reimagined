"""Testes de fronteira: pacotes truncados, escrita interrompida e projetos reproduzíveis."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import dz
import iwgroup
import hud_patch
import modelcheck
import portkit
import reflres
import build_nml

H = iwgroup.djb2


def group(name='sample', resources=(), child_paths=()):
    g = iwgroup.Group()
    g.version = 0x0107033d
    g.chunks = [(H('ResGroupMembers'), name.encode() + b'\0' + struct.pack('<I', 0))]
    if child_paths:
        body = bytes([len(child_paths)])
        for path in child_paths:
            body += path.encode() + b'\0' + b'\0' * 8 + struct.pack('<I', H(Path(path).stem))
        g.chunks.append((iwgroup.CHILDREN, body))
    lists = []
    for cls, nm, data in resources:
        lst = iwgroup.ResList(H(cls), 1, 1)
        lst.objects = [{'cls': H(cls), 'name': H(nm), 'data': data}]
        lists.append(lst)
    if lists:
        g.chunks.append((iwgroup.RESOURCES, lists))
    return g.dump()


def model(index=2, material=0, mem_override=None):
    def block(cls, count, memory, raw):
        return struct.pack('<IIHHH', cls, cls, ((memory + 3) & ~3) + 28, count, 0) + raw
    verts = block(modelcheck.VERTS, 3, 18, struct.pack('<HH9h', 3, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0))
    tris = block(modelcheck.TRIS, 3, 6, struct.pack('<I3H', material, 0, 1, index))
    data = struct.pack('<IHH3iII', 0x120000, 3, 3, 0, 0, 0, 2, 2) + verts + tris
    data += struct.pack('<HHIII', 0, 3, 0, 1, H('material'))
    if mem_override is not None:
        data = bytearray(data)
        struct.pack_into('<H', data, 36, mem_override)
        data = bytes(data)
    return data


class TempCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def archive(self, entries=None):
        p = self.root / 'source.dz'
        dz.write(p, entries or [('folder', 'file', b'example')])
        return p


class ArchiveTests(TempCase):
    def test_roundtrip_preserves_duplicate_directory_indices(self):
        path = self.root / 'source.dz'
        dz.write(path, [('same', 'x', b'data', 2)], dirs=['', 'same', 'same'])
        before = path.read_bytes()
        entries, dirs = dz.entries_of(path)
        dz.write(path, entries, dirs=dirs)
        self.assertEqual(before, path.read_bytes())

    def test_failed_replace_keeps_original_and_cleans_temp(self):
        path = self.archive()
        before = path.read_bytes()
        with mock.patch.object(dz.os, 'replace', side_effect=OSError('simulated disk failure')):
            with self.assertRaises(OSError):
                dz.write(path, [('folder', 'file', b'changed')])
        self.assertEqual(before, path.read_bytes())
        self.assertEqual([path], list(self.root.iterdir()))

    def test_reject_truncated_chunk(self):
        path = self.archive()
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            portkit.Archive(path)

    def test_reject_traversal_and_absolute_paths(self):
        for path in ('../oops', 'a/../../oops', '/tmp/oops', 'C:\\oops', 'a\x00b'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                portkit.safe_path(path)

    def test_reject_escaping_symlink(self):
        assets = self.root / 'assets'
        assets.mkdir()
        (assets / 'escape').symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            portkit.target_file(assets, 'escape/nope')

    def test_duplicate_case_path_rejected(self):
        path = self.archive([('', 'A', b'one'), ('', 'a', b'two')])
        with self.assertRaises(ValueError):
            portkit.Archive(path)

    def test_compression_read_and_legacy_rejection(self):
        payload = b'compressed data' * 100
        compressed = zlib.compress(payload)
        path = self.archive([('', 'file', compressed)])
        raw, _, _, _, header_end = dz.read(path)
        changed = bytearray(raw)
        # one chunk table immediately precedes payload
        struct.pack_into('<IH', changed, header_end - 8, len(payload), 8)
        path.write_bytes(changed)
        self.assertEqual(portkit.Archive(path).read('file'), payload)
        with self.assertRaises(ValueError):
            dz.entries_of(path)

    def test_wrong_uncompressed_size_rejected(self):
        path = self.archive()
        raw, _, _, _, end = dz.read(path)
        changed = bytearray(raw)
        struct.pack_into('<I', changed, end - 8, 999)
        path.write_bytes(changed)
        with self.assertRaises(ValueError):
            portkit.Archive(path).read('folder/file')

    def test_extract_manifest_and_no_overwrite(self):
        archive = self.archive()
        args = argparse.Namespace(archive=archive, out=self.root / 'extract', filter='folder')
        portkit.extract_command(args)
        self.assertEqual((args.out / 'assets/folder/file').read_bytes(), b'example')
        manifest = json.loads((args.out / 'extraction.json').read_text())
        self.assertEqual(manifest['entries'][0]['sha256'], portkit.sha(b'example'))
        with self.assertRaises(ValueError):
            portkit.extract_command(args)


class GroupTests(unittest.TestCase):
    def test_roundtrip(self):
        raw = group(resources=[('CLevel', 'kino', b'opaque')])
        self.assertEqual(iwgroup.Group.load(raw).dump(), raw)

    def test_every_truncation_rejected(self):
        raw = group(resources=[('CLevel', 'kino', b'opaque')])
        for size in range(len(raw)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                iwgroup.Group.load(raw[:size])

    def test_small_chunk_size_rejected(self):
        raw = bytearray(group())
        struct.pack_into('<I', raw, 10, 1)
        with self.assertRaises(ValueError):
            iwgroup.Group.load(raw)

    def test_oversized_resource_rejected(self):
        body = struct.pack('<IIIBBI', 1, H('CLevel'), 1, 1, 1, 0xffffffff)
        with self.assertRaises(ValueError):
            iwgroup.parse_resources(body)

    def test_unsized_list_not_silently_accepted(self):
        body = struct.pack('<IIIBB', 1, H('CLevel'), 1, 1, 0) + b'\0' * 8
        with self.assertRaises(iwgroup.UnsupportedGroup):
            iwgroup.parse_resources(body)

    def test_child_paths_and_truncation(self):
        raw = group(child_paths=['levels/kino//kino_statics.group'])
        g = iwgroup.Group.load(raw)
        self.assertEqual(portkit.children(g)[0]['path'], 'levels/kino/kino_statics.group.bin')
        tag, body = g.chunks[-1]
        g.chunks[-1] = tag, body[:-1]
        with self.assertRaises(ValueError):
            portkit.children(g)


class ModelTests(unittest.TestCase):
    def test_valid_model(self):
        out = modelcheck.inspect(model())
        self.assertEqual(out['triangles'], 1)
        self.assertEqual(out['vertices'], 3)
        self.assertEqual(out['materials'], [H('material')])

    def test_invalid_indices_material_and_memory(self):
        for data in (model(index=3), model(material=1), model(mem_override=0)):
            with self.subTest(data=data.hex()), self.assertRaises(ValueError):
                modelcheck.inspect(data)

    def test_every_truncation_rejected(self):
        data = model()
        for n in range(len(data)):
            with self.subTest(n=n), self.assertRaises(ValueError):
                modelcheck.inspect(data[:n])

    def test_unknown_block_is_explicitly_unsupported(self):
        data = bytearray(model())
        struct.pack_into('<I', data, 28, H('CIwModelBlockNorms'))
        with self.assertRaises(modelcheck.UnsupportedModel):
            modelcheck.inspect(data)


class HudTests(unittest.TestCase):
    def test_atlas_transparency_guard(self):
        textures = []
        for name, height in [('HUD_ALL', 512), ('hud_icons', 1024)]:
            header = bytearray(23)
            header[10] = 5
            struct.pack_into('<HHH', header, 13, 1024, height, 2048)
            textures.append(('CIwTexture', name, bytes(header) + b'\0' * (2048 * height) + b'\0'))
        raw = group(resources=textures)
        self.assertEqual(len(portkit.validate_hud_atlases(raw)), 2)
        g = iwgroup.Group.load(raw)
        obj = g.lists()[0].objects[0]
        pixels = bytearray(obj['data'])
        pixels[23 + 272 * 2048 + 842 * 2] = 15
        obj['data'] = bytes(pixels)
        with self.assertRaisesRegex(ValueError, 'opaco'):
            portkit.validate_hud_atlases(g.dump())

    def test_patch_only_ui_idempotent_preserves_texture_bytes(self):
        uv = struct.pack('<IIhhIIhh', hud_patch.UV0, hud_patch.SVEC2, 23, 14,
                         hud_patch.UV1, hud_patch.SVEC2, 243, 234)
        raw = group(resources=[('CIwUIElement', 'page', uv), ('CIwTexture', 'pixeltrap', uv)])
        patched, edits = hud_patch.patch_report(raw)
        self.assertEqual(len(edits), 1)
        self.assertEqual(len(raw), len(patched))
        objs = list(portkit.objects(iwgroup.Group.load(patched)))
        self.assertEqual(objs[1][2]['data'], uv)
        self.assertEqual(hud_patch.patch_report(patched), (patched, []))

    def test_short_and_false_page_signatures_do_not_overrun(self):
        raw = group(resources=[('CIwUIElement', 'page', struct.pack('<I', H('ig_poiReg')))])
        self.assertEqual(hud_patch.patch_report(raw), (raw, []))


class ProjectTests(TempCase):
    def source(self):
        fixed = group('fixed', [('CResolutionSpecificString', 'loading-kino', b'loading/kino\0')])
        main = group('kino', child_paths=['levels/kino/kino_statics.group'])
        reflected = {'cls': H('CWave'), 'size': 44, 'rest': b'', 'fields': [
            {'owner': H('CWave'), 'type': H('unsigned int'), 'a': 1, 'name': H('m_ZombieCount'),
             'b': 1, 'value': struct.pack('<I', 6)}]}
        statics = group('kino_statics', [('CLevel', 'kino', b'opaque'), ('CIwModel', 'room', model()),
                                       ('CWave', 'wave1', reflres.build(reflected))])
        return self.archive([('fixed', 'fixed.group.bin', fixed), ('levels\\kino', 'kino.group.bin', main),
                             ('levels\\kino', 'kino_statics.group.bin', statics)])

    def init(self):
        path = self.source()
        project = self.root / 'map'
        portkit.init_command(argparse.Namespace(archive=path, base='kino', name='test', out=project))
        return path, project

    def test_init_build_keeps_source_and_load_key(self):
        path, project = self.init()
        before = path.read_bytes()
        out = self.root / 'built.dz'
        portkit.build_command(argparse.Namespace(project=project, out=out))
        built = portkit.Archive(out)
        self.assertIn('levels/test/test.group.bin', built.records)
        fixed = iwgroup.Group.load(built.read('fixed/fixed.group.bin'))
        self.assertIn(H('loading-test'), [o['name'] for _, _, o in portkit.objects(fixed)])
        statics = iwgroup.Group.load(built.read('levels/test/test_statics.group.bin'))
        self.assertIn(H('test'), [o['name'] for _, _, o in portkit.objects(statics)])
        self.assertEqual(before, path.read_bytes())
        self.assertFalse(json.loads(Path(str(out) + '.json').read_text())['runtime_tested'])

    def test_changed_source_refused(self):
        path, project = self.init()
        path.write_bytes(path.read_bytes() + b'changed')
        with self.assertRaisesRegex(ValueError, 'origem mudou'):
            portkit.build_command(argparse.Namespace(project=project, out=self.root / 'built.dz'))

    def test_changed_invalid_model_refused(self):
        path, project = self.init()
        asset = project / 'assets/levels/test/test_statics.group.bin'
        g = iwgroup.Group.load(asset.read_bytes())
        g.lists()[1].objects[0]['data'] = model(index=3)
        asset.write_bytes(g.dump())
        out = self.root / 'bad.dz'
        with self.assertRaisesRegex(ValueError, 'índices'):
            portkit.build_command(argparse.Namespace(project=project, out=out))
        self.assertFalse(out.exists())

    def test_extra_asset_refused(self):
        _, project = self.init()
        (project / 'assets/unexpected').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError, 'extras'):
            portkit.build_command(argparse.Namespace(project=project, out=self.root / 'built.dz'))

    def test_required_cpu_position_flag_cannot_be_removed(self):
        before = group(resources=[('CIwModel', 'room', model())])
        changed = bytearray(model())
        struct.pack_into('<I', changed, 0, 0x20000)
        after = group(resources=[('CIwModel', 'room', bytes(changed))])
        with self.assertRaisesRegex(ValueError, '0x100000'):
            portkit.validate_changed_group(before, after)

    def test_field_edit_checked_typed_and_logged(self):
        _, project = self.init()
        args = argparse.Namespace(project=project, group='levels/test/test_statics.group.bin',
                                  resource='wave1', cls='CWave', field='m_ZombieCount', expect='6', value='8')
        asset = project / 'assets' / args.group
        before = asset.read_bytes()
        portkit.set_command(args)
        reflected = reflres.parse(iwgroup.Group.load(asset.read_bytes()).lists()[2].objects[0]['data'])
        self.assertEqual(reflected['fields'][0]['value'], struct.pack('<I', 8))
        self.assertEqual(len(list((project / 'history').glob('*.json'))), 1)
        current = asset.read_bytes()
        with self.assertRaisesRegex(ValueError, 'expect'):
            portkit.set_command(args)
        self.assertEqual(current, asset.read_bytes())
        args.expect, args.value = '8', '6'
        portkit.set_command(args)
        self.assertEqual(before, asset.read_bytes())


class ReflectedTests(unittest.TestCase):
    def test_no_truncated_field_silently_accepted(self):
        field = {'owner': H('CWave'), 'type': H('unsigned int'), 'a': 1, 'name': H('m_ZombieCount'),
                 'b': 1, 'value': struct.pack('<I', 6)}
        data = reflres.build({'cls': H('CWave'), 'size': 44, 'rest': b'', 'fields': [field]})
        for n in range(len(data)):
            with self.subTest(n=n), self.assertRaises(ValueError):
                reflres.parse(data[:n])

    def test_field_type_range_and_finite_values(self):
        for type_name, value, raw in [('unsigned int', -1, b'\0' * 4), ('bool', 1, b'\0'),
                                       ('float', float('nan'), b'\0' * 4),
                                       ('CIwFVec3', [0, 1], b'\0' * 12), ('opaque', 1, b'\0' * 4)]:
            with self.subTest(type_name=type_name), self.assertRaises(ValueError):
                portkit.typed_field_value({'type': H(type_name), 'value': raw}, value)


@unittest.skipIf(build_nml.np is None, 'numpy/Pillow opcionais: use o Python com dependências para o conversor NML')
class NmlTests(unittest.TestCase):
    def test_generated_models_parse_at_chunk_limit(self):
        np = build_nml.np
        for count in (3, 9000):
            data = build_nml.model_bytes(np.zeros((count, 3)), np.zeros((count, 2)), [128] * count,
                                         [(0, list(range(count)))], [H('material')], 0x120000)
            self.assertEqual(modelcheck.inspect(data)['triangles'], count // 3)

    def test_nonfinite_positions_rejected(self):
        np = build_nml.np
        with self.assertRaises(ValueError):
            build_nml.model_bytes(np.full((3, 3), float('nan')), np.zeros((3, 2)), [0] * 3,
                                  [(0, [0, 1, 2])], [1])

    def test_position_comes_from_named_field_not_first_twelve(self):
        import entityspec
        fields = []
        for name, typ, value in [('decoy', 'unsigned int', struct.pack('<I', 12)),
                                  ('m_localPosition', 'CIwFVec3', struct.pack('<3f', 10, 20, 30))]:
            fields.append({'owner': H('CIsTransform'), 'type': H(typ), 'a': 1, 'name': H(name),
                           'b': 1, 'value': value})
        reflected = reflres.build({'cls': H('CIsTransform'), 'size': 0, 'fields': fields, 'rest': b''})[4:]
        spec = {'components': [{'cls': H('CIsComponentSpec'), 'name': H('transform'),
                                'type': H('CIsTransform'), 'data': reflected}], 'children': []}
        data = group(resources=[('CIsEntitySpec', 'room', entityspec.write_spec(spec))])
        positions = build_nml.spec_positions(iwgroup.Group.load(data))
        self.assertEqual(positions[H('room')].tolist(), [10, 20, 30])


if __name__ == '__main__':
    unittest.main()
