"""Prove the reviewed Qt resource delta without exempting application code changes."""
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = '90f9633'
CORRECTION = '5b49f43'
OLD_UI = '51b9bc07134e08287307f62174b02a480474f852980cf7a82eab814fdebe0039'
QT = Path('C:/Qt/6.10.3/msvc2022_64')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def messages(path):
    tree = ET.parse(path)
    result = {}
    for context in tree.findall('context'):
        for item in context.findall('message'):
            key = (context.findtext('name'), item.findtext('source'), item.findtext('comment', ''))
            assert key not in result
            translation = item.find('translation')
            result[key] = (item.attrib, translation.attrib, tuple(translation.itertext()))
    return result

def verify():
    # The transition applies only to this reviewed build, not arbitrary ui.dll
    # replacements. Every application C++ file remains at the tested baseline.
    protected = ('src/ui/src', 'src/ui/include')
    status = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=all', '--', *protected], cwd=ROOT, text=True)
    assert not status.strip(), ('unverified UI source edits', status)
    changes = subprocess.check_output(['git', 'diff', '--name-only', BASELINE, '--', *protected], cwd=ROOT, text=True)
    assert not changes.strip(), ('application code changed since tested baseline', changes)
    resource_files = ('src/ui/CMakeLists.txt', 'src/app/CMakeLists.txt',
                      'src/ui/resources/qt-translations.qrc.in', 'src/ui/resources/qt-overrides-zh_CN.ts')
    for file in resource_files:
        committed = subprocess.check_output(['git', 'show', CORRECTION+':'+file], cwd=ROOT)
        assert (ROOT/file).read_bytes().replace(b'\r\n', b'\n') == committed.replace(b'\r\n', b'\n'), file
    build_log = ROOT/'Docs/i18n/Qt-size-correction-build.log'
    compiled = [line.strip() for line in build_log.read_text(encoding='utf-8-sig').splitlines()
                if line.strip().endswith('.cpp')]
    assert compiled == ['qrc_qt-translations_Release.cpp'], compiled
    for name, source in (('before', QT/'translations/qtbase_zh_CN.qm'),
                         ('after', ROOT/'build/qtbase_zh_CN.qm')):
        target = ROOT/'build'/('qt-size-transition-'+name+'.ts')
        subprocess.run([str(QT/'bin/lconvert.exe'), '-i', str(source), '-o', str(target)], check=True)
    before = messages(ROOT/'build/qt-size-transition-before.ts')
    after = messages(ROOT/'build/qt-size-transition-after.ts')
    assert before.keys() == after.keys()
    changed = {key for key in before if before[key] != after[key]}
    assert changed == {('QDirModel', 'Size', ''), ('QFileSystemModel', 'Size', '')}, changed
    for key in changed:
        assert before[key][2] == ('体积',) and after[key][2] == ('大小',)
        assert before[key][:2] == after[key][:2]
    runtime = ROOT/'build/release/bin'
    assert (ROOT/'build/qtbase_zh_CN.qm').read_bytes() == (runtime/'locate/qt/qtbase_zh_CN.qm').read_bytes()
    for language in ('ja', 'ko'):
        assert (runtime/f'locate/qt/qtbase_{language}.qm').read_bytes() == (QT/f'translations/qtbase_{language}.qm').read_bytes()
    qrc = ET.parse(ROOT/'build/src/ui/qt-translations.qrc')
    chinese = next(file for file in qrc.findall('.//file') if file.get('alias') == 'qtbase_zh_CN.qm')
    assert Path(chinese.text).resolve() == (ROOT/'build/qtbase_zh_CN.qm').resolve()
    previous = json.loads((ROOT/'Docs/i18n/Qt-size-correction-verification.json').read_text(encoding='utf-8'))
    assert previous['result'] == 'PASS'
    for entry in previous['evidence']:
        assert digest(ROOT/entry['file']) == entry['sha256'], entry['file']
    files = list(resource_files) + ['Docs/i18n/Qt-size-correction-build.log',
        'Docs/i18n/Qt-size-correction-verification.json', 'build/release/bin/ui.dll',
        'build/qtbase_zh_CN.qm', 'build/release/bin/locate/qt/qtbase_zh_CN.qm']
    files += [str(path.relative_to(ROOT)).replace('\\','/') for folder in protected
              for path in (ROOT/folder).rglob('*') if path.is_file()]
    return {'result': 'PASS', 'baseline_commit': BASELINE, 'correction_commit': CORRECTION,
            'old_ui_sha256': OLD_UI, 'current_ui_sha256': digest(runtime/'ui.dll'),
            'changed_messages': sorted(changed), 'preserved_messages': len(before)-2,
            'scope': 'retain unchanged application behavior evidence; corrected dialogs require fresh three-scale hot checks',
            'evidence': [{'file':file, 'sha256':digest(ROOT/file)} for file in files]}

if __name__ == '__main__':
    report = verify()
    (ROOT/'Docs/i18n/Qt-size-correction-transition.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'Qt resource transition PASS: UI C++ unchanged; two reviewed entries corrected; {report["preserved_messages"]} messages preserved.')
