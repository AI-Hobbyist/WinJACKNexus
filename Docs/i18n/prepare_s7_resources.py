"""Prepare an incremental Release resource inventory without executing modules."""

import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
BIN = ROOT / 'build/release/bin'
LANGUAGES = {'en-US', 'zh-CN', 'ja-JP', 'ko-KR'}


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=unique)


def fingerprint(path):
    return {
        'file': str(path.relative_to(ROOT)).replace('\\', '/'),
        'bytes': path.stat().st_size,
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def main():
    cache = (ROOT / 'build/CMakeCache.txt').read_text(encoding='utf-8')
    assert 'WJN_ENABLE_VST2:BOOL=ON' in cache
    scopes = [
        ('locate', 'locate', 'WinJACKNexus.exe'),
        ('src/vst/locate', 'nodes/vst/locate', 'nodes/vst/mod_vst_host.dll'),
        ('tools/vst_scanner/locate', 'vstscanner/locate', 'Vst Scanner.exe'),
        (
            'Develop/Node/locate',
            'examples/node/locate',
            'examples/node/wjn_example_node.dll',
        ),
        (
            'Develop/NativeFx/locate',
            'examples/native_fx/locate',
            'examples/native_fx/wjn_example_native_fx.dll',
        ),
    ]
    effects = [
        'gate',
        'graphic_eq',
        'deesser',
        'compressor',
        'limiter',
        'delay',
        'parametric_eq',
        'reverb',
    ]
    scopes += [
        (
            f'src/native_fx/effects/{fx}/locate',
            f'nodes/native_fx/{fx}/locate',
            f'nodes/native_fx/{fx}/mod_{fx}.dll',
        )
        for fx in effects
    ]
    resources, modules = [], []
    placeholder = lambda text: sorted(
        re.findall(r'\{[A-Za-z][A-Za-z0-9_]*\}|%[1-9][0-9]*', text)
    )
    for source_root, destination_root, binary in scopes:
        source = ROOT / source_root
        manifest = load(source / 'manifest.json')
        assert (
            manifest['defaultLanguage'] == 'en-US'
            and set(manifest['languages']) == LANGUAGES
        )
        required = {
            (domain, key)
            for domain, keys in manifest['requiredKeys'].items()
            for key in keys
        }
        assert len(required) == sum(
            len(keys) for keys in manifest['requiredKeys'].values()
        )
        assert set(manifest['domains']) == set(manifest['requiredKeys'])
        english = load(source / manifest['languages']['en-US'])['domains']
        coverage = {}
        for language, filename in manifest['languages'].items():
            assert Path(filename).name == filename and filename.endswith('.json')
            path = source / filename
            assert path.stat().st_size <= 1024 * 1024
            catalog = load(path)
            assert (
                catalog['language'] == language
                and catalog['moduleId'] == manifest['moduleId']
            )
            assert catalog['catalogVersion'] == manifest['catalogVersion']
            values = catalog['domains']
            actual = {(d, k) for d, entries in values.items() for k in entries}
            missing = required - actual
            intentional = {
                ('example.node', 'greeting'),
                ('example.node', 'description'),
            }
            if manifest['moduleId'] == 'example-node' and language != 'en-US':
                assert missing == intentional and actual <= required
            else:
                assert actual == required, (
                    source_root,
                    language,
                    missing,
                    actual - required,
                )
            coverage[language] = {
                'translated_keys': len(actual),
                'english_fallback_keys': [d + '/' + k for d, k in sorted(missing)],
            }
            for domain, key in actual:
                value, fallback = values[domain][key], english[domain][key]
                fallback_text = (
                    fallback['other'] if isinstance(fallback, dict) else fallback
                )
                variants = value if isinstance(value, dict) else {'other': value}
                assert 'other' in variants and set(variants) <= {
                    'zero',
                    'one',
                    'two',
                    'few',
                    'many',
                    'other',
                }
                assert all(
                    isinstance(text, str)
                    and text.strip()
                    and placeholder(text) == placeholder(fallback_text)
                    for text in variants.values()
                ), (source_root, language, domain, key)
            deployed = BIN / destination_root / filename
            assert deployed.read_bytes() == path.read_bytes(), str(deployed)
            resources.append(
                {
                    'destination': destination_root + '/' + filename,
                    'source': fingerprint(path),
                    'deployed': fingerprint(deployed),
                }
            )
        deployed_manifest = BIN / destination_root / 'manifest.json'
        assert deployed_manifest.read_bytes() == (source / 'manifest.json').read_bytes()
        resources.append(
            {
                'destination': destination_root + '/manifest.json',
                'source': fingerprint(source / 'manifest.json'),
                'deployed': fingerprint(deployed_manifest),
            }
        )
        modules.append(
            {
                'module_id': manifest['moduleId'],
                'domains': manifest['domains'],
                'catalog_version': manifest['catalogVersion'],
                'display_keys': len(required),
                'languages': sorted(LANGUAGES),
                'catalog_root': destination_root,
                'language_coverage': coverage,
                'binary': fingerprint(BIN / binary),
                'binary_catalog_association': 'CMake target deployment and module descriptor; no module execution added',
            }
        )
    for name in ['qtbase_zh_CN.qm', 'qtbase_ja.qm', 'qtbase_ko.qm']:
        path = BIN / 'locate/qt' / name
        assert path.is_file() and path.stat().st_size > 0
        resources.append(
            {
                'destination': 'locate/qt/' + name,
                'deployed': fingerprint(path),
                'source_policy': 'Chinese project Size override; Japanese/Korean Qt 6.10.3 catalog, CMake-deployed',
            }
        )
    # Explicit runtime allowlist: never enumerate the bin tree into a package.
    runtime = [
        'WinJACKNexus.exe',
        'ui.dll',
        'common.dll',
        'node.dll',
        'audio.dll',
        'patchbay_core.dll',
        'mod_patchbay.dll',
        'mod_meterbrigde.dll',
        'nodes/meter/mod_meterbridge_probe.dll',
        'nodes/mixer/mod_digital_mixer.dll',
        'nodes/adapter/mod_adapter.dll',
        'nodes/vst/mod_vst_host.dll',
        'Vst Scanner.exe',
        'Vst Scanner Probe32.exe',
        'Vst Scanner Probe64.exe',
    ]
    runtime += [f'nodes/native_fx/{fx}/mod_{fx}.dll' for fx in effects]
    runtime += [
        'examples/node/wjn_example_node.dll',
        'examples/native_fx/wjn_example_native_fx.dll',
    ]
    files = [{'destination': value, **fingerprint(BIN / value)} for value in runtime]
    destinations = [r['destination'] for r in resources] + [
        r['destination'] for r in files
    ]
    assert len(destinations) == len(set(destinations))
    assert all(
        not re.search(
            r'(?:\.ts$|\.pdb$|tests|fixture|themes/|settings|\.ini$)', p, re.I
        )
        for p in destinations
    )
    commit = subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True
    ).strip()
    report = {
        'stage': 'S7',
        'date': '2026-10-04',
        'status': 'RESOURCE_INVENTORY_PREPARED_NOT_RELEASE_ACCEPTED',
        'implementation_commit': commit,
        'package_kind': 'incremental Release overlay, draft',
        'modules': modules,
        'resource_files': resources,
        'runtime_candidates': files,
        'counts': {
            'modules': len(modules),
            'resource_files': len(resources),
            'runtime_candidates': len(files),
            'catalog_keys': sum(m['display_keys'] for m in modules),
        },
        'preserves_user_selection': 'allowlist contains no user settings, projects, presets, themes, background or ini files; no installation performed',
        'module_test_runs_added': 0,
        'ui_shows_added': 0,
        'archive_created': False,
        'pending': [
            'S6 four-language/audio-domain human review from the user.',
            'Final overlay archive, dependency compatibility and archive member/hash verification.',
            'Deployment/restart runtime acceptance is not added under the one-test-per-module rule. Keep earlier cold-start gaps and supplemental UI gaps explicit.',
        ],
        'limits': [
            'Byte equality, placeholder checks and association fingerprints prove resources only; they do not prove DLL behavior or translation quality.',
            'Existing install must supply compatible Qt 6.10.3/MSVC/JACK and unchanged VSTProcess dependencies. This draft is not a full installation package.',
            'Community example binaries/catalogs are optional API samples, not auto-installed nodes. ExampleNode intentionally omits description/greeting in three non-English catalogs under the S1 fallback demonstration contract; six missing entries are reported, not called translated.',
            'Resource files and DLL hashes must be refreshed after any approved translation/code change.',
        ],
    }
    (OUT / 'S7-resource-inventory.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    print(
        f"S7 draft resource inventory PASS: {len(modules)} modules, {len(resources)} resources, {len(files)} runtime candidates; no archive or module run."
    )


if __name__ == '__main__':
    main()
