"""Install or verify mountains&seas terrain data on a dedicated server.

This is the server operator's entry point, run once per world seed:

    python -X utf8 ports/bds-server/install_world.py --seed 20260415 \
        --world "Bedrock level" --server-dir /path/to/bds

It bakes the immutable seed package, prepares the world, and writes the
plugin's data root. Nothing here touches saved chunks: the world must be new.
"""
import argparse, json, os, shutil, subprocess, sys, tempfile
from pathlib import Path

P = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(P / 'tools'))
sys.path.insert(0, str(P / 'ports/bds-server'))
from prepare_world import prepare, sha, PLUGIN_FOLDER
from forest_profile import TARGETS


def build_seed(seed, output, status, game_line):
    """Bake one immutable seed package with the shipped seed tools."""
    env = os.environ.copy()
    env.pop('PYTHONHOME', None)
    env.pop('PYTHONPATH', None)
    env['PYTHONUTF8'] = '1'
    env['PYTHONNOUSERSITE'] = '1'
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    env['OPENBLAS_NUM_THREADS'] = '1'
    env['OMP_NUM_THREADS'] = '1'
    subprocess.run([sys.executable, '-X', 'utf8', str(P / 'tools/build_streaming_seed.py'),
                    '--seed', seed, '--output', str(output),
                    '--status', str(status), '--target-game', game_line],
                   cwd=P, env=env, check=True, timeout=3600)


def install(server_dir, seed, world_name, game_line='26.51', creative=False, force=False):
    server_dir = Path(server_dir).resolve()
    if game_line not in TARGETS:
        raise ValueError('Unsupported game line')
    if str(int(seed)) != str(seed):
        raise ValueError('Signed 64-bit decimal seed required')
    world = server_dir / 'worlds' / world_name
    plugin = server_dir / PLUGIN_FOLDER
    # PreLoaderConfig.json points modsPath at ./plugins/, so a mod under mods/
    # is silently ignored and the server starts vanilla.
    dll = server_dir / 'plugins' / 'mountains&seas' / 'karma-worldgen-bds.dll'
    if not dll.is_file():
        raise ValueError('Install the mod folder under plugins/ first: ' + str(dll))
    stale = server_dir / 'mods' / 'mountains&seas'
    if stale.is_dir():
        raise ValueError('A copy exists under mods/, which LeviLamina does not load: ' + str(stale))
    if world.exists() and not force:
        raise ValueError('World already exists; terrain data is immutable per seed. '
                         'Create a new world or pass --force to overwrite plugin data only.')
    plugin.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.karma-seed-', dir=server_dir) as staging:
        source = Path(staging) / 'native-input.json'
        status = Path(staging) / 'status.json'
        build_seed(seed, source, status, game_line)
        prepared = Path(staging) / 'world'
        result = prepare(source, P / 'tests/fixtures/void-world/level.dat', prepared,
                         world_name, plugin, creative=creative)
        if world.exists():
            shutil.rmtree(world)
        shutil.move(str(prepared), str(world))
    # The structure runtime reads its own opt-in config; terrain alone is the
    # default so a server never installs native structure hooks unasked.
    config = plugin / 'structures' / 'config.json'
    if not config.exists():
        config.parent.mkdir(parents=True, exist_ok=True)
        config.write_text(json.dumps(dict(automaticStructures=False, smallFeatures=False), indent=2), 'utf-8')
    return dict(world=str(world), plugin=str(plugin), **result)


def verify(server_dir, world_name):
    """Read-only preflight: identity only, not in-game integration."""
    import hashlib
    server_dir = Path(server_dir).resolve()
    world = server_dir / 'worlds' / world_name
    plugin = server_dir / PLUGIN_FOLDER
    marker = json.loads((world / 'karma-worldgen.json').read_bytes())
    if marker.get('host') != 'bds-server':
        raise ValueError('World was not prepared for BDS')
    if marker != json.loads((plugin / 'active-world.json').read_bytes()):
        raise ValueError('Activation mismatch')
    meta = json.loads((plugin / 'terrain.json').read_bytes())
    profile = (plugin / 'profile.json').read_bytes()
    digest = hashlib.sha256(profile).hexdigest()
    if digest != marker['sha256'] or digest != meta['sha256']:
        raise ValueError('Profile hash mismatch')
    if json.loads(profile) != {k: v for k, v in meta.items() if k != 'sha256'}:
        raise ValueError('Metadata mismatch')
    if meta['terrainSeed'] != marker['worldSeed']:
        raise ValueError('Terrain seed mismatch')
    for name, expected in meta.get('runtimeFiles', {}).items():
        if sha(plugin / name) != expected:
            raise ValueError('Runtime file changed: ' + name)
    return dict(ok=True, world=str(world), seed=meta['terrainSeed'], kernel=meta['version'])


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--server-dir', type=Path, required=True, help='BDS root (contains worlds/, mods/, plugins/)')
    p.add_argument('--world', required=True, help='World folder name under worlds/')
    p.add_argument('--seed', help='Signed 64-bit seed; required when installing')
    p.add_argument('--game-line', choices=sorted(TARGETS), default='26.51')
    p.add_argument('--creative', action='store_true')
    p.add_argument('--force', action='store_true', help='Replace an existing world')
    p.add_argument('--verify', action='store_true', help='Read-only identity check')
    a = p.parse_args()
    if a.verify:
        print(json.dumps(verify(a.server_dir, a.world), ensure_ascii=False, indent=2))
    else:
        if not a.seed:
            p.error('--seed is required when installing')
        print(json.dumps(install(a.server_dir, a.seed, a.world, a.game_line, a.creative, a.force),
                         ensure_ascii=False, indent=2))
