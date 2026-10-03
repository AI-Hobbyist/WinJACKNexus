"""One offline failure-path run per probe architecture; no scanner UI run."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Docs/i18n'


def main():
    records = []
    for bits in (32, 64):
        binary = ROOT / f'build/release/bin/Vst Scanner Probe{bits}.exe'
        result = ROOT / f'build/S6-probe-{bits}-result.json'
        result.unlink(missing_ok=True)
        missing = ROOT / 'build/S6-不存在-absent-plugin.dll'
        assert not missing.exists()
        process = subprocess.run([str(binary), '--vst2', str(missing),
                                  '--output', str(result)], capture_output=True,
                                 timeout=20)
        assert process.returncode == 1, (bits, process.returncode)
        data = json.loads(result.read_text(encoding='utf-8'))
        assert data == {'ok': False, 'skippable': False,
                        'error': 'LoadLibraryExW failed',
                        'error_key': 'probe.loadLibrary', 'error_args': []}, data
        records.append({'bits': bits, 'runs': 1, 'exit_code': process.returncode,
                        'result': data,
                        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()})
        print(f'Probe{bits} PASS: one missing-library run, structured key and English fallback')
    report = {'stage': 'S6', 'result': 'PASS_LIMITED_FAILURE_PATH',
              'scope': 'VST2 missing-library IPC for actual Release Probe32/Probe64',
              'ui_runs': 0, 'records': records,
              'unverified': ['Other VST2/VST3 runtime branches',
                             'Scanner-side translated runtime presentation',
                             'Real plug-in loading and audio integration']}
    (OUT / 'S6-probe-verification.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
