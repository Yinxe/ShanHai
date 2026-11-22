"""Offline checks for the BDS package. No BDS, no Windows, no network."""

import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

P = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(P / 'ports/bds-server'))
sys.path.insert(0, str(P / 'tools'))


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((P / 'ports/bds-server/manifest.json').read_bytes())

    def test_platform_and_entry(self):
        # A client manifest in a server package fails to load with an
        # unrelated symbol error, so the two fields are asserted here.
        self.assertEqual(self.manifest['platform'], 'server')
        self.assertEqual(self.manifest['type'], 'native')
        self.assertEqual(self.manifest['entry'], 'karma-worldgen-bds.dll')

    def test_no_client_entry(self):
        self.assertNotEqual(self.manifest['entry'], 'karma-worldgen-client.dll')

    def test_name_is_a_safe_folder(self):
        name = self.manifest['name']
        self.assertTrue(name)
        self.assertNotIn('/', name)
        self.assertNotIn('\\', name)
        self.assertNotIn(':', name)
        self.assertNotIn(name, ('.', '..'))


class PackagingTest(unittest.TestCase):
    def test_package_layout(self):
        import package_bds
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'karma-worldgen-bds.dll'
            binary.write_bytes(b'MZ' + b'\0' * 64)
            kernel = Path(tmp) / 'native-worldgen-check.exe'
            kernel.write_bytes(b'MZ')
            out = Path(tmp) / 'pkg.zip'
            result = package_bds.package(binary, out, '26.51', kernel)
            self.assertTrue(out.is_file())
            self.assertEqual(len(result['sha256']), 64)
            with zipfile.ZipFile(out) as archive:
                names = set(archive.namelist())
        root = 'mountains&seas/'
        for required in ['manifest.json', 'karma-worldgen-bds.dll', 'README.md',
                         'ports/bds-server/install_world.py', 'ports/bds-server/prepare_world.py',
                         'tools/build_streaming_seed.py', 'tools/select_spawn.py',
                         'build/windows/x64/release/native-worldgen-check.exe',
                         'data/seed-template.json', 'data/runtime-template.json',
                         'tests/fixtures/void-world/level.dat', 'LICENSE']:
            self.assertIn(root + required, names)
        # Developer-only scripts must not ship.
        self.assertNotIn(root + 'tools/mcctl.py', names)
        self.assertNotIn(root + 'ports/windows-client/prepare_world.py', names)

    def test_entry_mismatch_is_rejected(self):
        import package_bds
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'wrong-name.dll'
            binary.write_bytes(b'MZ')
            kernel = Path(tmp) / 'native-worldgen-check.exe'
            kernel.write_bytes(b'MZ')
            with self.assertRaisesRegex(ValueError, 'Manifest entry'):
                package_bds.package(binary, Path(tmp) / 'pkg.zip', '26.51', kernel)

    def test_existing_output_is_rejected(self):
        import package_bds
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'karma-worldgen-bds.dll'
            binary.write_bytes(b'MZ')
            kernel = Path(tmp) / 'native-worldgen-check.exe'
            kernel.write_bytes(b'MZ')
            out = Path(tmp) / 'pkg.zip'
            out.write_bytes(b'')
            with self.assertRaisesRegex(ValueError, 'new output'):
                package_bds.package(binary, out, '26.51', kernel)


class ToolClosureTest(unittest.TestCase):
    def test_seed_tools_resolve(self):
        import package_bds
        tools = package_bds.shipped_tools()
        # A missing import in the shipped set makes seed baking fail on the
        # operator's machine, where no repository checkout exists.
        for name in ['select_spawn', 'level_dat', 'forest_profile', 'process_guard']:
            self.assertIn(name, tools)
        for name in tools:
            self.assertTrue((P / 'tools' / (name + '.py')).is_file(), name)

    def test_subprocess_only_dependency_is_collected(self):
        import package_bds
        # build_seed spawns bake_rivers.py as a child process, so it never
        # appears in the import graph; path-based discovery must find it.
        tools = package_bds._tool_closure(['build_seed'])
        for name in ['bake_rivers', 'bake_natural_lakes', 'bake_scarps']:
            self.assertIn(name, tools)


class RecipeTest(unittest.TestCase):
    def test_height_pack_matches_512_recipe(self):
        manifest = json.loads((P / 'data/build-height-pack/manifest.json').read_bytes())
        self.assertEqual(manifest['header']['min_engine_version'][:2], [1, 26])
        dimension = json.loads((P / 'data/build-height-pack/dimensions/overworld.json').read_bytes())
        bounds = dimension['minecraft:dimension']['components']['minecraft:dimension_bounds']
        # The BDS port prepares Y-64..511 worlds; a 320 world would silently
        # clip every mountain the kernel generates.
        self.assertEqual(bounds, {'min': -64, 'max': 512})


if __name__ == '__main__':
    unittest.main()
