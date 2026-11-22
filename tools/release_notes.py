"""Describe a release asset the way docs/RELEASE_TEMPLATE.md requires.

The release rules ask for two things this produces: a ``release.json`` recording
the executable source commit and SHA-256 for every asset, and release notes that
follow the template sections rather than a generated commit list.

    python -B -X utf8 tools/release_notes.py \
        --package <zip> --platform Windows --game-build 1.26.51.01 \
        --loader 26.51.9 --kernel karma-terrain-2.225.0-native-agent-a \
        --out-dir <dir>

Writes ``<dir>/release.json`` and ``<dir>/release-notes.md``. When several
platforms are released together, call it once per package with the same
``--out-dir``: assets accumulate in one manifest, which is what "one release per
product version" means.
"""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / 'docs/RELEASE_TEMPLATE.md'
KNOWN_ISSUES = ROOT / 'docs/KNOWN_ISSUES.md'


def git_commit():
    """The commit the binaries were built from, not the one tagging them."""
    env = {'GITHUB_SHA': None}
    import os
    sha = os.environ.get('GITHUB_SHA')
    if sha:
        return sha
    try:
        return subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, check=True,
                              capture_output=True, text=True).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def manifest_for(platform):
    port = 'ports/bds-server' if platform == 'BDS' else 'ports/windows-client'
    return json.loads((ROOT / port / 'manifest.json').read_bytes())


def load_state(out_dir):
    path = out_dir / 'release.json'
    if path.is_file():
        return json.loads(path.read_bytes())
    return None


def build_asset(package, platform, game_build, loader, kernel, commit):
    return {
        'filename': package.name,
        'platform': platform,
        'gameBuild': game_build,
        'levilamina': loader,
        'kernel': kernel,
        'architecture': 'x64',
        'sourceCommit': commit,
        'sha256': sha256(package),
        'size': package.stat().st_size,
    }


def compatibility_rows(state):
    rows = ['| Download filename | Minecraft build | LeviLamina | Kernel | Architecture |',
            '| --- | --- | --- | --- | --- |']
    for asset in state['assets']:
        rows.append('| `{}` | {} | {} | {} | {} |'.format(
            asset['filename'], asset['gameBuild'], asset['levilamina'],
            asset['kernel'], asset['architecture']))
    return '\n'.join(rows)


def render_notes(state, changes, save_compatibility):
    template = TEMPLATE.read_text('utf-8')
    body = template
    body = body.replace('VERSION', state['version'])
    body = body.replace(
        '- Describe the feature or the failure corrected, including the affected platform.',
        changes.rstrip())
    # Replace the whole example table (header plus placeholder row) so the
    # template's own instructions cannot survive into the published notes.
    lines = body.splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith('| Download filename'))
    end = start + 3  # header, separator, placeholder row
    lines[start:end] = compatibility_rows(state).splitlines()
    body = '\n'.join(lines)
    # The sentence under the table is an instruction to the author, not part of
    # the published notes; the manifest it points at is the actual record.
    body = body.replace(
        '\nList only the packages attached to this release. Record the executable source '
        'commit and SHA-256 for each asset in `release.json`; distinguish different '
        'game-build revisions.\n', '\n')
    body = body.replace(
        'Describe whether existing recipes retain their rules and whether new generation\n'
        'requires a new world. State any unsupported save format explicitly.',
        save_compatibility.rstrip())
    issues = known_issues()
    if issues:
        body = body.replace(
            'List concrete symptoms, affected configurations and available workarounds.\n'
            'Omit this section if no issue is documented; do not infer compatibility beyond\n'
            'the builds listed above.',
            issues)
    return body


def known_issues():
    """Product-facing issues only; the tracked file is already written that way."""
    if not KNOWN_ISSUES.is_file():
        return ''
    lines = [line for line in KNOWN_ISSUES.read_text('utf-8').splitlines()
             if line.startswith('- ')]
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--platform', choices=['Windows', 'BDS'], required=True)
    parser.add_argument('--game-build', required=True)
    parser.add_argument('--loader', required=True, help='LeviLamina version')
    parser.add_argument('--kernel', required=True)
    parser.add_argument('--out-dir', type=Path, required=True)
    parser.add_argument('--changes', help='Markdown bullet list; required for the first asset')
    parser.add_argument('--save-compatibility', help='Markdown text; required for the first asset')
    arguments = parser.parse_args()

    package = arguments.package.resolve()
    if not package.is_file():
        raise SystemExit('Missing package: ' + str(package))
    arguments.out_dir.mkdir(parents=True, exist_ok=True)

    commit = git_commit()
    if not commit:
        raise SystemExit('Cannot determine the source commit; set GITHUB_SHA or run inside git')

    manifest = manifest_for(arguments.platform)
    state = load_state(arguments.out_dir) or {
        'version': manifest['version'],
        'sourceCommit': commit,
        'assets': [],
    }
    if state['version'] != manifest['version']:
        raise SystemExit('Version mismatch between the existing release.json and the manifest')
    # One asset per platform: a re-run replaces rather than duplicates.
    state['assets'] = [asset for asset in state['assets'] if asset['platform'] != arguments.platform]
    state['assets'].append(build_asset(package, arguments.platform, arguments.game_build,
                                       arguments.loader, arguments.kernel, commit))
    state['assets'].sort(key=lambda asset: asset['platform'])

    (arguments.out_dir / 'release.json').write_text(
        json.dumps(state, ensure_ascii=False, indent=2) + '\n', 'utf-8')

    # Always re-render: a later platform in the same run adds a table row, so the
    # notes from the first call are already stale. Wording carries over from the
    # existing file when this call does not supply it.
    notes_path = arguments.out_dir / 'release-notes.md'
    previous = notes_path.read_text('utf-8') if notes_path.is_file() else None
    changes = arguments.changes or extract_section(previous, 'Changes')
    compatibility = arguments.save_compatibility or extract_section(previous, 'Save compatibility')
    if not changes or not compatibility:
        raise SystemExit('The first asset needs --changes and --save-compatibility')
    notes_path.write_text(render_notes(state, changes, compatibility), 'utf-8')

    print(json.dumps(state, ensure_ascii=False, indent=2))


def extract_section(notes, heading):
    """Reuse the wording from a previous asset in the same run."""
    if not notes:
        return None
    marker = '## ' + heading
    start = notes.find(marker)
    if start < 0:
        return None
    start = notes.find('\n', start) + 1
    end = notes.find('\n## ', start)
    return notes[start:end if end >= 0 else None].strip()


if __name__ == '__main__':
    main()
