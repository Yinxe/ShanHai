"""Prepare a dedicated-server world for mountains&seas from a seed package.

A BDS server world differs from the Windows client's private preview world:

  * the world lives at ``worlds/<name>`` and is created once by the operator,
  * build height comes from the world's behaviour pack, not a client import,
  * the plugin reads ``plugins/karma-worldgen`` once for the whole server.

This tool writes only terrain data and activation markers. It never copies
saved chunks and never edits an existing world in place: the caller points it
at a fresh directory.
"""
import argparse, hashlib, json, os, shutil, sys, tempfile, time
from pathlib import Path

P = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(P / 'tools'))
from level_dat import read_file, write_file
from select_spawn import select
from forest_profile import palette as recipe_palette

HEIGHT_PACK_FOLDER = 'shan-hai-build-height'
PLUGIN_FOLDER = Path('plugins/karma-worldgen')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_recipe(data):
    config = data.get('config', {})
    required = dict(generatedMaxYExclusive=512, mountainPeakY=480, seaLevel=100, elevationRevision=2)
    if any(type(config.get(key)) is not int or config[key] != value for key, value in required.items()):
        raise ValueError('BDS worlds require the 512-height, mountain-480, sea-100 elevation-variant recipe')


def install_height_pack(world):
    """Install the dimension/height behaviour pack into a fresh world."""
    pack = P / 'data/build-height-pack'
    header = json.loads((pack / 'manifest.json').read_bytes())['header']
    shutil.copytree(pack, world / 'behavior_packs' / HEIGHT_PACK_FOLDER)
    (world / 'world_behavior_packs.json').write_text(
        json.dumps([dict(pack_id=header['uuid'], version=header['version'])], indent=2), 'utf-8')
    return dict(folder=HEIGHT_PACK_FOLDER, uuid=header['uuid'], version=header['version'],
                files={str(f.relative_to(pack)).replace('\\', '/'): sha(f)
                       for f in sorted(pack.rglob('*')) if f.is_file()})


def prepare(source, template, destination, name, plugin_dir, creative=False, spawn_report=None):
    source, template, destination, plugin_dir = map(Path, (source, template, destination, plugin_dir))
    if destination.exists():
        raise ValueError('Refusing to replace an existing world')
    data = json.loads(source.read_bytes())
    manifest = data['seedManifest']
    seed = manifest['terrainSeed']
    if str(int(seed)) != seed or not -(1 << 63) <= int(seed) < (1 << 63):
        raise ValueError('Invalid signed 64-bit seed')
    if not manifest.get('ready') or not manifest.get('riversReady'):
        raise ValueError('Seed package is incomplete; run tools/build_seed.py first')
    import re
    kernel = re.search(r'KernelVersion\[\]="([^"]+)"', (P / 'src/NativeRuntime.h').read_text()).group(1)
    if data['version'] != kernel:
        raise ValueError('Seed package uses a different terrain kernel')
    validate_recipe(data)

    ver, rootname, tags = read_file(template)
    if tags.get('Generator') != (3, 2) or 'TheVoid' not in tags.get('FlatWorldLayers', (8, ''))[1]:
        raise ValueError('Expected a verified TheVoid level.dat template')

    generator = P / 'build/windows/x64/release/native-worldgen-check.exe'
    if spawn_report is not None and (spawn_report.get('inputSha256') != sha(source) or
                                    spawn_report.get('generatorSha256') != sha(generator)):
        raise ValueError('Cached spawn belongs to different terrain data or generator')
    if spawn_report is None:
        spawn_report = select(source, generator, None)
    spawn = spawn_report['spawn']

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.karma-bds-', dir=destination.parent) as staging:
        world = Path(staging) / 'world'
        # The plugin owns one shared data root for the whole server, so terrain
        # data is written there; the world carries only its activation marker.
        native = plugin_dir / 'native'
        (native).mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, native / 'native-input.json')
        region_cache = source.parent / 'regional-cache'
        if data.get('regionalGeneration') and region_cache.is_dir():
            shutil.copytree(region_cache, plugin_dir / 'regional-cache', dirs_exist_ok=True)
        for license_name in ['DEEPSLATE-LICENSE.txt', 'EPIC-TERRAIN-LICENSE.txt']:
            shutil.copy2(P / 'native' / license_name, native / license_name)
        shutil.copy2(P / 'native/vanilla-ore-rules.json', native / 'rules.json')

        base = json.loads((P / 'data/runtime-template.json').read_bytes())
        meta = {k: base[k] for k in ['format', 'mode', 'version', 'origin', 'sizeBlocks', 'sizeChunks',
                                     'productionOffset', 'minY', 'maxY', 'palette', 'biomeNames',
                                     'backend', 'chunkCodec', 'vanillaBlocksOnly']}
        meta['version'] = data['version']
        meta.update(recipe_palette(data))
        meta.update(maxY=511, buildMaxYExclusive=512, generatedMaxYExclusive=512,
                    buildHeightPack=install_height_pack(world))
        meta.update(terrainSeed=seed, worldSeed=seed, worldSeedIndependent=False, spawn=spawn,
                    pluginSha256=sha(plugin_dir / 'karma-worldgen-bds.dll'),
                    releaseId=json.loads((plugin_dir / 'manifest.json').read_bytes())['version'],
                    runtimeFiles={str(f.relative_to(plugin_dir)).replace('\\', '/'): sha(f)
                                  for f in sorted(native.iterdir()) if f.is_file()})
        if data.get('regionalGeneration', {}).get('boundsMode') == 'infinite':
            meta.update(boundsMode='infinite', sizeBlocks=None, sizeChunks=None, gameCoordinateLimit=30000000)

        profile = json.dumps(meta, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        (plugin_dir / 'profile.json').write_bytes(profile)
        meta['sha256'] = hashlib.sha256(profile).hexdigest()
        (plugin_dir / 'terrain.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2), 'utf-8')

        marker = dict(host='bds-server', worldName=name, worldSeed=seed, terrainSeed=seed,
                      sha256=meta['sha256'], kernel=kernel,
                      buildMaxYExclusive=512, generatedMaxYExclusive=512)
        marker_bytes = json.dumps(marker, ensure_ascii=False, indent=2).encode('utf-8')
        (world / 'karma-worldgen.json').write_bytes(marker_bytes)
        (plugin_dir / 'active-world.json').write_bytes(marker_bytes)
        (plugin_dir / 'spawn-check.json').write_text(json.dumps(spawn_report, indent=2), 'utf-8')

        for key in list(tags):
            if key in ['BiomeOverride', 'limitedWorldOriginX', 'limitedWorldOriginY', 'limitedWorldOriginZ']:
                tags.pop(key)
        tags.update(LevelName=(8, name), RandomSeed=(4, int(seed)), SpawnX=(3, spawn['x']),
                    SpawnY=(3, spawn['y']), SpawnZ=(3, spawn['z']), Time=(4, 0),
                    LastPlayed=(4, int(time.time())), GameType=(3, 1 if creative else 0),
                    commandsEnabled=(1, 1 if creative else 0), hasBeenLoadedInCreative=(1, 1 if creative else 0),
                    hasSpawned=(1, 0))
        tags.update(domobspawning=(1, 1), dodaylightcycle=(1, 1), doweathercycle=(1, 1),
                    dofiretick=(1, 1), randomtickspeed=(3, 1), keepinventory=(1, 0),
                    experiments=(10, {}), MultiplayerGame=(1, 0), MultiplayerGameIntent=(1, 0))
        write_file(world / 'level.dat', ver, rootname, tags)
        (world / 'levelname.txt').write_text(name, 'utf-8')
        # Rename only after every file is written; no half-prepared world appears.
        os.replace(world, destination)
    return dict(path=str(destination), name=name, seed=seed, spawn=spawn, profileSha256=meta['sha256'])


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--template', type=Path, required=True)
    p.add_argument('--destination', type=Path, required=True)
    p.add_argument('--name', required=True)
    p.add_argument('--plugin-dir', type=Path, required=True,
                   help='BDS plugins/karma-worldgen directory that receives terrain data')
    p.add_argument('--creative', action='store_true')
    a = p.parse_args()
    print(json.dumps(prepare(a.input, a.template, a.destination, a.name, a.plugin_dir, a.creative),
                     ensure_ascii=False))
