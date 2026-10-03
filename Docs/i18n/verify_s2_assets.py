"""Verify S2's real catalogs, deployment and recorded offline host test."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    manifest = json.loads((ROOT / 'locate/manifest.json').read_text(encoding='utf-8'))
    keys = set(manifest['requiredKeys']['host'])
    records = json.loads((OUT / 'S2-strings.json').read_text(encoding='utf-8'))
    assert len(records) == len(keys) == len({r['key'] for r in records})
    audit = (ROOT / 'i18n翻译核对.md').read_text(encoding='utf-8-sig')
    runtime = ROOT / 'build/release/bin'
    packages = []
    for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
        path = ROOT / 'locate' / f'{language}.json'
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['language'] == language
        assert catalog['catalogVersion'] == manifest['catalogVersion']
        assert catalog['moduleId'] == manifest['moduleId']
        assert set(catalog['domains']['host']) == keys
        for row in records:
            text = catalog['domains']['host'][row['key']]
            assert text and text == row[language]
            assert set(re.findall(r'\{([A-Za-z][A-Za-z0-9_]*)\}', text)) == set(re.findall(r'\{([A-Za-z][A-Za-z0-9_]*)\}', row['en-US']))
            audit_row = next(line for line in audit.splitlines() if line.startswith('| host | '+row['key']+' |'))
            assert text.replace('|', '&#124;').replace('\n', '<br>') in audit_row
            assert '待母语/领域审校' in audit_row
        assert path.read_bytes() == (runtime / 'locate' / path.name).read_bytes()
        packages.append({'language': language, 'keys': len(keys)})
    assert (runtime / 'locate/manifest.json').read_bytes() == (ROOT / 'locate/manifest.json').read_bytes()
    for name in ('qtbase_zh_CN.qm', 'qtbase_ja.qm', 'qtbase_ko.qm'):
        assert (runtime / 'locate/qt' / name).read_bytes() == (Path('C:/Qt/6.10.3/msvc2022_64/translations') / name).read_bytes()
    test_log = (OUT / 'S2-tests.log').read_text(encoding='utf-8-sig')
    assert '100% tests passed' in test_log
    assert test_log.count('QT_QPA_PLATFORM=windows') >= 1
    assert 'QT_QPA_PLATFORM=offscreen' not in test_log
    assert 'SYSTEM_FONT ' in test_log
    count = re.search(r'PASS host i18n assertions (\d+)', test_log)
    assert count
    sources = ['src/app/main.cpp', 'src/app/CMakeLists.txt', 'src/ui/src/HostI18n.cpp',
               'src/ui/include/WinJACKNexus/UI/HostI18n.hpp', 'src/ui/src/I18nManager.cpp',
               'src/ui/src/TrayController.cpp', 'src/ui/src/ThemeCatalog.cpp', 'src/ui/src/Controls.cpp',
               'src/ui/src/IconManager.cpp', 'src/ui/src/ApplicationSettings.cpp',
               'src/ui/include/WinJACKNexus/UI/Controls.hpp', 'src/ui/include/WinJACKNexus/UI/IconManager.hpp',
               'src/ui/include/WinJACKNexus/UI/TrayController.hpp', 'src/ui/resources/ui.qrc', 'src/ui/resources/qt-translations.qrc.in',
               'src/ui/CMakeLists.txt', 'Develop/HostI18nTests.cpp', 'Develop/CMakeLists.txt']
    sources += [p.relative_to(ROOT).as_posix() for p in (ROOT / 'locate').glob('*.json')]
    sources += [p.relative_to(ROOT).as_posix() for p in (ROOT / 'src/ui/resources/icons').glob('*.svg') if p.stem in ('copy','pin','hide','theme')]
    renders=[]
    for scale in ('1', '1.5', '2'):
        for language in ('en-US', 'zh-CN', 'ja-JP', 'ko-KR'):
            for window in ('global','jack','quickbar','directory'):
                path=OUT / 'S2-rendered' / f'scale-{scale}' / f'{window}-{language}.png'
                assert path.exists() and path.read_bytes().startswith(b'\x89PNG')
                renders.append(path)
    artifacts = ['WinJACKNexus.exe', 'ui.dll', 'wjn_host_i18n_tests.exe']
    report = {'stage': 'S2', 'result': 'PASS', 'configuration': 'Release', 'packages': packages,
              'host_test_assertions': int(count[1]),
              'platform': 'windows', 'font': re.search(r'SYSTEM_FONT (.+)',test_log)[1].strip(),
              'render_hashes': {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in renders},
              'source_hashes': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
              'artifact_hashes': {p: hashlib.sha256((runtime / p).read_bytes()).hexdigest() for p in artifacts},
              'limits': ['Translation drafts await human language/domain review.',
                         'S2 host scope only; S3-S7 remain separate stages.',
                         'Offline dialogs disable JACK process management and device probing.']}
    (OUT / 'S2-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"S2 assets PASS: {len(keys)} host keys in four deployed catalogs, Qt QM and audit match.")


if __name__ == '__main__':
    main()
