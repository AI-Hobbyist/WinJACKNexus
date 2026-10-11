"""Reconcile S5 catalogs, call-site inventory, deployment and Windows test evidence.

This verifies evidence consistency; image inspection and human language review
remain independent gates.
"""

import csv
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'
LANGUAGES = ('en-US', 'zh-CN', 'ja-JP', 'ko-KR')
EFFECTS = (
    'gate',
    'graphic_eq',
    'deesser',
    'compressor',
    'limiter',
    'delay',
    'parametric_eq',
    'reverb',
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    runtime = ROOT / 'build/release/bin'
    records = json.loads((OUT / 'S5-strings.json').read_text(encoding='utf-8'))
    assert len(records) == len({(r['effect'], r['key']) for r in records}) == 275
    audit = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8-sig')
    placeholders = lambda value: set(
        re.findall(r'%(?:L?\d+|n)|\{[A-Za-z_]\w*\}', value)
    )
    identities, evidence = set(), []
    for effect in EFFECTS:
        source = ROOT / 'src/native_fx/effects' / effect / 'locate'
        destination = runtime / 'nodes/native_fx' / effect / 'locate'
        manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
        domain = 'nativefx.' + manifest['moduleId']
        selected = [r for r in records if r['effect'] == effect]
        keys = {r['key'] for r in selected}
        assert keys == set(manifest['requiredKeys'][domain])
        identities.update((domain, key) for key in keys)
        assert (source / 'manifest.json').read_bytes() == (
            destination / 'manifest.json'
        ).read_bytes()
        dll = runtime / 'nodes/native_fx' / effect / ('mod_' + effect + '.dll')
        assert dll.is_file()
        evidence.append({'file': str(dll.relative_to(ROOT)), 'sha256': digest(dll)})
        for language in LANGUAGES:
            path = source / (language + '.json')
            catalog = json.loads(path.read_text(encoding='utf-8'))
            assert (
                catalog['moduleId'] == manifest['moduleId']
                and catalog['language'] == language
            )
            assert catalog['catalogVersion'] == manifest['catalogVersion']
            assert (
                set(catalog['domains']) == {domain}
                and set(catalog['domains'][domain]) == keys
            )
            assert path.read_bytes() == (destination / path.name).read_bytes()
            for row in selected:
                text = catalog['domains'][domain][row['key']]
                assert text.strip() and text == row[language]
                assert placeholders(text) == placeholders(row['en-US'])
                lines = [
                    line
                    for line in audit.splitlines()
                    if line.startswith(f"| {domain} | {row['key']} |")
                ]
                assert (
                    len(lines) == 1
                    and text.replace('|', '&#124;').replace('\n', '<br>') in lines[0]
                )
            evidence.append(
                {'file': str(path.relative_to(ROOT)), 'sha256': digest(path)}
            )
    shared = list(
        csv.DictReader(
            (OUT / 'S5-shared-strings.tsv').open(encoding='utf-8'), delimiter='\t'
        )
    )
    assert len(shared) == 5
    host_manifest = json.loads(
        (ROOT / 'locate/manifest.json').read_text(encoding='utf-8')
    )
    assert (
        host_manifest['catalogVersion'] == 5
        and sum(map(len, host_manifest['requiredKeys'].values())) == 533
    )
    for language in LANGUAGES:
        path = ROOT / 'locate' / (language + '.json')
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert path.read_bytes() == (runtime / 'locate' / path.name).read_bytes()
        for row in shared:
            assert catalog['domains'][row['domain']][row['key']] == row[language]
            assert row['key'] in host_manifest['requiredKeys'][row['domain']]
            lines = [
                line
                for line in audit.splitlines()
                if line.startswith(f"| {row['domain']} | {row['key']} |")
            ]
            assert len(lines) == 1 and row[language] in lines[0]
    with (OUT / 'S0-keys.csv').open(encoding='utf-8-sig', newline='') as file:
        scope = [
            r for r in csv.DictReader(file) if r['file'].startswith('src/native_fx/')
        ]
    assert scope and all((r['domain'], r['key']) in identities for r in scope), [
        (r['file'], r['line'], r['key'])
        for r in scope
        if (r['domain'], r['key']) not in identities
    ]
    with (OUT / 'S0-pending.csv').open(encoding='utf-8-sig', newline='') as file:
        assert not list(csv.DictReader(file))
    # Old unbounded matrix logs and black menu captures cannot satisfy this gate.
    # Each new record is one foreground invocation, with immutable source/runtime
    # fingerprints and a measured popup budget. Preserve full coverage by union.
    from PIL import Image
    from verify_qt_size_correction import verify as verify_qt_transition

    transition = verify_qt_transition()
    runs = []
    historical = []
    coverage = {}
    fresh_dialogs = set()
    required_dialogs = {('1', 'hot'), ('1.5', 'hot'), ('2', 'hot')}
    for record_path in sorted((OUT / 'S5-bounded-records').glob('*.json')):
        run = json.loads(record_path.read_text(encoding='utf-8-sig'))
        assert (
            run['scale'] in ('1', '1.5', '2')
            and run['language'] in ('hot',) + LANGUAGES
        )
        assert run['exit_code'] == 0 and run['platform'] == 'windows'
        assert 0 < run['popup_shows'] <= 100 and run['checks'] > 0
        assert run['scope'] in ('Effects', 'Host', 'Community', 'Shared')
        assert run['evidence'] and run['captures']
        if 'capture_protocol' not in run:
            # Retain immutable history, but occlusion was not checked by that
            # fixture. It cannot satisfy the visible-window completion gate.
            assert digest(ROOT / run['log']) == run['log_sha256']
            for entry in run['captures']:
                assert digest(ROOT / entry['file']) == entry['sha256']
            historical.append(
                {
                    'record': str(record_path.relative_to(ROOT)),
                    'reason': 'historical behavior evidence; native pixel ownership not proven',
                }
            )
            continue
        assert run['capture_protocol'] == 'native-window-ownership-v2'
        for fingerprint in run['evidence'] + run['captures']:
            path = ROOT / fingerprint['file']
            assert digest(path) == fingerprint['sha256'], (
                'stale run evidence',
                record_path,
                path,
            )
        required_fingerprints = {
            'Develop/NativeFx/NativeFxI18nTests.cpp',
            'Develop/I18nExample.hpp',
            'src/ui/src/Controls.cpp',
            'src/ui/src/FxSettingsWindow.cpp',
            'build/release/bin/wjn_native_fx_i18n_tests.exe',
            'build/release/bin/ui.dll',
        }
        required_fingerprints.update(
            'build/release/bin/nodes/native_fx/' + effect + '/mod_' + effect + '.dll'
            for effect in EFFECTS
        )
        assert required_fingerprints <= {entry['file'] for entry in run['evidence']}
        path = ROOT / run['log']
        assert digest(path) == run['log_sha256']
        log = path.read_text(encoding='utf-8-sig')
        match = re.search(
            r'S5 module Windows checks PASS: (\d+); platform=windows font=(.+?) scale=([\d.]+)',
            log,
        )
        budget = re.search(r'S5 popup budget: (\d+) / 100', log)
        assert (
            match
            and budget
            and int(match[1]) == run['checks']
            and match[2] == run['font']
            and match[3] == run['scale']
        )
        assert int(budget[1]) == run['popup_shows'] and 'S5 FAILED' not in log
        assert 'S5 capture protocol: native-window-ownership-v2' in log
        is_effect = run['scope'] == 'Effects'
        assert bool(run['effects']) if is_effect else not run['effects']
        assert set(run['effects']) <= set(EFFECTS)
        if is_effect and run['language'] == 'hot':
            assert len(run['effects']) <= 2
        expected_scope = 'S5 scope: effects=' + ','.join(run['effects'])
        expected_scope += f" host={int(run['scope']=='Host')} community={int(run['scope']=='Community')} shared={int(run['scope']=='Shared')} cold={run['language']}"
        assert expected_scope in log, ('scope metadata disagrees', record_path)
        names = {Path(entry['file']).name for entry in run['captures']}

        def has_capture(stem):
            assert any(
                name.startswith(stem) and name.endswith('.png') for name in names
            ), (record_path, stem)

        locales = LANGUAGES if run['language'] == 'hot' else (run['language'],)
        group = coverage.setdefault((run['scale'], run['language']), set())
        if is_effect:
            if 'graphic_eq' in run['effects']:
                fresh_dialogs.add((run['scale'], run['language']))
            for effect in run['effects']:
                for locale in locales:
                    for kind in ('mixer', 'node', 'preset-menu', 'draft'):
                        has_capture(effect + '-' + kind + '-' + locale)
                    if effect not in ('graphic_eq', 'reverb'):
                        has_capture(effect + '-operations-menu-' + locale)
                    if effect == 'graphic_eq':
                        for kind in ('load-dialog', 'save-dialog'):
                            has_capture(effect + '-' + kind + '-' + locale)
                has_capture(effect + '-partial-fallback')
                group.add(effect)
        elif run['scope'] == 'Host':
            for effect in EFFECTS:
                for locale in locales:
                    has_capture('fx-host-' + effect.replace('_', '-') + '-' + locale)
            group.add('host')
        elif run['scope'] == 'Community':
            for locale in locales:
                for kind in ('node', 'mixer'):
                    has_capture('community-' + kind + '-' + locale)
            group.add('community')
        else:
            assert run['language'] == 'hot'
            for locale in LANGUAGES:
                has_capture('client-name-' + locale)
                has_capture('shared-elision-' + locale + '-0-0')
                has_capture('shared-elision-' + locale + '-viewport')
            group.add('shared')
        for entry in run['captures']:
            if '-menu-' in entry['file']:
                with Image.open(ROOT / entry['file']) as bitmap:
                    assert any(
                        low != high for low, high in bitmap.convert('RGB').getextrema()
                    ), ('blank native menu', entry['file'])
        runs.append(
            {
                'record': str(record_path.relative_to(ROOT)),
                'scale': run['scale'],
                'cold': run['language'],
                'checks': run['checks'],
                'popup_shows': run['popup_shows'],
                'font': run['font'],
                'captures': len(names),
                'log': run['log'],
                'sha256': run['log_sha256'],
                'runtime_basis': 'current runtime with native-window-ownership-v2',
            }
        )
    missing = []
    for scale in ('1', '1.5', '2'):
        for language in ('hot',) + LANGUAGES:
            expected = set(EFFECTS) | {'host', 'community'}
            if language == 'hot':
                expected.add('shared')
            covered = coverage.get((scale, language), set())
            if expected - covered:
                missing.append(
                    {
                        'scale': scale,
                        'language': language,
                        'missing': sorted(expected - covered),
                    }
                )
    for scale, language in sorted(required_dialogs - fresh_dialogs):
        group = next(
            (
                entry
                for entry in missing
                if entry['scale'] == scale and entry['language'] == language
            ),
            None,
        )
        if group is None:
            missing.append(
                {
                    'scale': scale,
                    'language': language,
                    'missing': ['corrected Qt file-dialog regression'],
                }
            )
        else:
            group['missing'].append('corrected Qt file-dialog regression')
    policy_path = OUT / 'S5-validation-policy.json'
    policy = json.loads(policy_path.read_text(encoding='utf-8'))
    assert policy['instruction'] == '后续不再进行缩放测试。节省时间'
    assert policy['additional_scale_tests'] is False
    assert (
        policy['additional_user_instruction']
        == '后续每个模块只测试一次，节省时间，不想等了'
    )
    assert policy['maximum_future_test_runs_per_module'] == 1
    required_missing = []
    for language in ('hot',) + LANGUAGES:
        expected = set(EFFECTS) | {'host', 'community'}
        if language == 'hot':
            expected.add('shared')
        covered = set().union(
            *(
                items
                for (scale, locale), items in coverage.items()
                if locale == language
            )
        )
        if expected - covered:
            required_missing.append(
                {'language': language, 'missing': sorted(expected - covered)}
            )
    # The user discontinued further scale tests. Preserve their original gaps
    # without claiming those combinations passed; language/behavior gates remain.
    remaining_cold = [entry for entry in required_missing if entry['language'] != 'hot']
    required_missing = [
        entry for entry in required_missing if entry['language'] == 'hot'
    ]
    if '--progress' not in sys.argv:
        assert not required_missing, (
            'missing required one-pass coverage',
            required_missing,
        )
    evidence.append(
        {'file': str(policy_path.relative_to(ROOT)), 'sha256': digest(policy_path)}
    )
    source_paths = [
        ROOT / 'src/native_fx/src/I18n.cpp',
        ROOT / 'src/native_fx/src/NativeFxApi.cpp',
        ROOT / 'src/ui/src/Controls.cpp',
        ROOT / 'src/ui/src/FxSettingsWindow.cpp',
        ROOT / 'Develop/NativeFx/NativeFxI18nTests.cpp',
        ROOT / 'Develop/I18nExample.hpp',
        ROOT / 'Develop/NativeFx/ExampleNativeFx.cpp',
        ROOT / 'Develop/Node/ExampleNode.cpp',
        ROOT / 'src/ui/src/I18nManager.cpp',
        ROOT / 'src/ui/src/HostI18n.cpp',
        ROOT / 'Docs/i18n/S5-strings.tsv',
        ROOT / 'Docs/i18n/S5-shared-strings.tsv',
        runtime / 'ui.dll',
        runtime / 'wjn_native_fx_i18n_tests.exe',
        runtime / 'examples/native_fx/wjn_example_native_fx.dll',
        runtime / 'examples/node/wjn_example_node.dll',
    ]
    source_paths.extend(
        ROOT / 'src/native_fx/effects' / effect / 'src/Ui.cpp' for effect in EFFECTS
    )
    evidence.extend(
        {'file': str(path.relative_to(ROOT)), 'sha256': digest(path)}
        for path in source_paths
    )
    progress = '--progress' in sys.argv
    report = {
        'stage': 'S5',
        'result': 'INCOMPLETE' if progress else 'PASS',
        'scope': 'catalog/deployment/inventory/Windows test evidence consistency',
        'module_keys': len(records),
        'shared_keys': len(shared),
        'source_display_occurrences': len(scope),
        'runs': runs,
        'evidence': evidence,
        'missing_bounded_coverage': missing,
        'missing_required_language_coverage': required_missing,
        'unverified_cold_start_coverage': remaining_cold,
        'validation_policy': policy,
        'historical_runs_not_counted': historical,
        'qt_resource_transition': transition,
        'human_review': 'pending S6; automated checks do not approve translations',
    }
    destination = 'S5-bounded-progress.json' if progress else 'S5-verification.json'
    (OUT / destination).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    if progress:
        print(
            f'S5 bounded evidence valid: {len(runs)} batches, {sum(r["captures"] for r in runs)} captures; stage INCOMPLETE, {len(required_missing)} required language groups lack full coverage; {len(missing)} discontinued scale groups remain unverified.'
        )
    else:
        print(
            f'S5 assets PASS: {len(records)} module keys, {len(shared)} shared keys, {len(scope)} source displays, {sum(r["captures"] for r in runs)} native Windows captures.'
        )


if __name__ == '__main__':
    main()
