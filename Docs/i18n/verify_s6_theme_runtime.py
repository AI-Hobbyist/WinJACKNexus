"""Reconcile the sole Theme execution with current deployment and source evidence."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Docs/i18n'
RUNTIME=ROOT/'build/release/bin'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    runtime=json.loads((OUT/'S6-theme-runtime.json').read_text(encoding='utf-8'))
    assert runtime['module_test_runs']==1 and runtime['scale_matrix_runs']==0
    assert runtime['platform']=='windows' and runtime['font']=='Microsoft YaHei UI'
    assert runtime['failures']==0 and runtime['window_shows']==5
    # Payload precedes the final report-write assertion; terminal log includes it.
    assert runtime['checks']==194
    log=(OUT/'S6-theme-tests.log').read_text(encoding='utf-8')
    assert 'S6 Theme PASS: 195 checks, 0 failures, 5 shows, 24 native captures' in log
    build=(OUT/'S6-theme-build.log').read_text(encoding='utf-8')
    assert 'wjn_theme_management_tests.exe' in build and 'error ' not in build
    manifest=json.loads((ROOT/'locate/manifest.json').read_text(encoding='utf-8'))
    assert manifest['catalogVersion']==10 and len(manifest['requiredKeys']['theme'])==238
    assert sum(map(len,manifest['requiredKeys'].values()))==774
    deployment=[]
    for name in ('manifest.json','en-US.json','zh-CN.json','ja-JP.json','ko-KR.json'):
        source=ROOT/'locate'/name; target=RUNTIME/'locate'/name
        assert digest(source)==digest(target)
        deployment.append({'file':str(target.relative_to(ROOT)), 'sha256':digest(target)})
    paths=[OUT/'S6-theme-runtime.json', OUT/'S6-theme-tests.log', OUT/'S6-theme-build.log',
           ROOT/'tests/theme/ThemeManagementTests.cpp',ROOT/'tests/theme/NativeDesktopCapture.hpp',
           ROOT/'src/ui/src/ThemeCatalog.cpp',ROOT/'src/ui/src/ThemeManagementDialog.cpp',
           ROOT/'src/ui/src/LocalizedThemeMessages.hpp', ROOT/'src/ui/src/LocalizedThemeDialogs.hpp',
           RUNTIME/'ui.dll',RUNTIME/'wjn_default_theme.dll',RUNTIME/'wjn_theme_management_tests.exe']
    assert len(runtime['captures'])==24
    expected={'baseline','overlay-hint-zh-CN','color-zh-CN','preview-zh-CN'}
    expected|={f'diagnostic-{lang}' for lang in ('en-US','zh-CN','ja-JP','ko-KR')}
    expected|={f'{kind}-{lang}' for kind in ('package.import','file.fontTitle','file.imageTitle','file.exportTitle')
               for lang in ('en-US','zh-CN','ja-JP','ko-KR')}
    assert {Path(c['path']).stem for c in runtime['captures']}==expected
    for capture in runtime['captures']:
        assert capture['protocol']=='native-window-ownership-v2' and capture['ownership_samples']==5
        assert capture['client_width']>0 and capture['client_height']>0
        path=ROOT/capture['path']
        assert path.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        paths.append(path)
    result={'stage':'S6','result':'THEME_RELEASE_AND_SINGLE_WINDOWS_PASS_HUMAN_REVIEW_PENDING',
            'host_catalog_version':10,'host_keys':774,'theme_keys':238,
            'module_test_runs':1,'checks':195,'failures':0,'window_shows':5,'native_captures':24,
            'platform':'windows','font':runtime['font'],'scale_matrix_runs':0,
            'coverage':['Four-language live Theme labels, diagnostic/error status and retained authoring drafts',
                        'Four open file workflows: live title/filter, filename, directory and reverse selection',
                        'Actual package/namespace failure payloads and Catalog menu failure callbacks',
                        'Existing authoring/background/color/default export/preview/apply/round-trip workflow'],
            'limitations':['Each canonical template is source/catalog checked; not every failure branch executed',
                           'File workflows cancelled; no interactive save-overwrite confirmation tested',
                           'Catalog menu callback tested; no separate tray notification UI test',
                           'No cold-start/language/scale matrix reruns; no JACK integration',
                           'Source audit S0 reconciliation and user human review remain outstanding'],
            'deployment':deployment,
            'evidence':[{'file':str(p.relative_to(ROOT)), 'sha256':digest(p)} for p in paths]}
    (OUT/'S6-theme-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Theme runtime/deployment evidence PASS: one run, 195 checks, 5 shows, 24 owned desktop captures; human review pending.')


if __name__=='__main__':
    main()
