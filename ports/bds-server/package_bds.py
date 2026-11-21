"""Package the BDS (LeviLamina server) build for distribution.

The archive contains only what a server operator needs:

    mountains&seas/
      manifest.json                 BDS native-mod manifest (platform: server)
      karma-worldgen-bds.dll        the mod
      README.md                     install and world-preparation steps
      LICENSE, THIRD_PARTY_NOTICES.md
      tools/                        seed baking and world preparation
      data/                         seed/runtime templates and the height pack

Terrain data is generated per world on the operator's machine, not shipped:
one package serves every seed.
"""
import argparse, hashlib, json, sys, zipfile
from pathlib import Path

P = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(P / 'tools'))
from check_release_text import check as check_release_text

GAME_BUILDS = {'26.20': '1.26.20.04', '26.51': '1.26.51.01'}
# The path tools/build_streaming_seed.py and tools/select_spawn.py default to.
KERNEL_TOOL_PATH = 'build/windows/x64/release/native-worldgen-check.exe'


def asset_name(version, game_line):
    return f'mountains-and-seas-{version}-BDS-MC{GAME_BUILDS[game_line]}.zip'


def _tool_closure(roots):
    """Collect every script reachable from the seed tools.

    Two kinds of dependency exist and both must ship: ``import`` (resolved by
    the interpreter) and ``subprocess`` (resolved by path at runtime). A tool
    invoked as a child process never appears in the import graph, so missing
    one fails only on the operator's machine, where no checkout exists.
    """
    import ast
    pending = list(roots)
    selected = set()
    while pending:
        name = pending.pop()
        src = P / 'tools' / (name + '.py')
        if name in selected or not src.is_file():
            continue
        selected.add(name)
        tree = ast.parse(src.read_text('utf-8'))
        for entry in ast.walk(tree):
            if isinstance(entry, ast.ImportFrom) and entry.module:
                pending.append(entry.module.split('.')[0])
            elif isinstance(entry, ast.Import):
                pending.extend(a.name.split('.')[0] for a in entry.names)
            elif isinstance(entry, ast.Constant) and isinstance(entry.value, str):
                # subprocess arguments are plain strings such as 'tools/bake_rivers.py'.
                value = entry.value
                if value.startswith('tools/') and value.endswith('.py'):
                    pending.append(Path(value).stem)
    return selected


def shipped_tools():
    """Every seed/world tool an operator needs, resolved from the entry points."""
    return _tool_closure(['build_streaming_seed', 'select_spawn', 'level_dat'])


def package(binary, output, game_line='26.51', kernel_tool=None):
    if output.exists():
        raise ValueError('Use a new output path')
    if kernel_tool is None:
        raise ValueError('Seed baking samples landmarks with native-worldgen-check; '
                         'pass --kernel-tool')
    if not Path(kernel_tool).is_file():
        raise ValueError('Missing kernel tool: ' + str(kernel_tool))
    manifest = json.loads((P / 'ports/bds-server/manifest.json').read_bytes())
    name = manifest['name']
    if not name or name in ('.', '..') or any(c in name for c in '/\\:'):
        raise ValueError('Invalid mod directory name')
    if manifest.get('platform') != 'server' or manifest.get('type') != 'native':
        raise ValueError('BDS manifest must be a native server mod')
    if manifest['entry'] != binary.name:
        raise ValueError('Manifest entry does not match the built DLL: ' + manifest['entry'])

    tools = shipped_tools()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED) as archive:
        root = name + '/'
        archive.write(binary, root + binary.name)
        archive.write(P / 'ports/bds-server/manifest.json', root + 'manifest.json')
        archive.write(P / 'ports/bds-server/README.md', root + 'README.md')
        archive.write(P / 'ports/bds-server/install_world.py', root + 'ports/bds-server/install_world.py')
        archive.write(P / 'ports/bds-server/prepare_world.py', root + 'ports/bds-server/prepare_world.py')
        # Seed baking samples landmark heights with the SDK-free kernel CLI, so
        # the operator needs it. It ships at the exact path the shipped tools
        # default to (build_streaming_seed, select_spawn); placing it anywhere
        # else makes world preparation fail only after packaging.
        archive.write(Path(kernel_tool), root + KERNEL_TOOL_PATH)
        for tool in sorted(tools):
            archive.write(P / 'tools' / (tool + '.py'), root + 'tools/' + tool + '.py')
        for src in sorted((P / 'tools/seed').glob('*')):
            if src.is_file():
                archive.write(src, root + 'tools/seed/' + src.name)
        for rel in ['data/seed-template.json', 'data/runtime-template.json', 'data/palette-contract.json',
                    'data/forest-capabilities.json', 'src/NativeRuntime.h', 'src/WorldBounds.h',
                    'native/vanilla-ore-rules.json', 'native/DEEPSLATE-LICENSE.txt',
                    'native/EPIC-TERRAIN-LICENSE.txt']:
            archive.write(P / rel, root + rel)
        for src in sorted((P / 'data/build-height-pack').rglob('*')):
            if src.is_file():
                archive.write(src, root + str(src.relative_to(P)))
        archive.write(P / 'tests/fixtures/void-world/level.dat', root + 'tests/fixtures/void-world/level.dat')
        archive.write(P / 'THIRD_PARTY_NOTICES.md', root + 'THIRD_PARTY_NOTICES.md')
        archive.write(P / 'LICENSE', root + 'LICENSE')
        archive.writestr(root + 'game-target.json', json.dumps(
            dict(schema=1, gameLine=game_line, package='bds-server'), indent=2))
    return dict(package=str(output), sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                gameLine=game_line, version=manifest['version'])


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--binary', type=Path,
                   default=P / 'ports/bds-server/build/windows/x64/release/karma-worldgen-bds.dll')
    p.add_argument('--output', type=Path)
    p.add_argument('--output-dir', type=Path, help='Name the ZIP using version, platform and Minecraft build')
    p.add_argument('--game-line', choices=sorted(GAME_BUILDS), default='26.51')
    p.add_argument('--kernel-tool', type=Path, default=P / 'build/windows/x64/release/native-worldgen-check.exe')
    a = p.parse_args()
    version = json.loads((P / 'ports/bds-server/manifest.json').read_bytes())['version']
    output = a.output or (a.output_dir / asset_name(version, a.game_line))
    print(json.dumps(package(a.binary, output, a.game_line, a.kernel_tool), indent=2))
