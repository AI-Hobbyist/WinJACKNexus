"""Reproducible S0 source evidence; no translation or application changes."""

import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|u|U|L)?R"(?P<delim>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delim)"|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
)
UI = re.compile(
    r'setText|setToolTip|setWindowTitle|setPlaceholderText|setAccessible|addAction|addItem|addTab|drawText|QMessageBox|QFileDialog|label\(|Button|Label|tooltip|title|description|displayName|names\[|labels\[',
    re.I,
)


def domain(path):
    parts = path.split('/')
    if path.startswith('src/native_fx/effects/'):
        return 'nativefx.' + parts[3]
    if path.startswith('tools/vst_scanner/'):
        return 'vstscanner'
    if path.startswith('tools/vst_process/'):
        return 'vstprocess'
    if path.startswith('Develop/'):
        return 'example.' + parts[1].lower()
    return {
        'app': 'host',
        'patchbay': 'patchbay',
        'meter': 'meterbridge',
        'adapter': 'adapter',
        'ui': 'ui',
        'mixer': 'mixer',
        'native_fx': 'nativefx',
        'node': 'node',
        'audio': 'audio',
        'common': 'common',
        'default_theme': 'theme',
    }.get(parts[1], parts[1])


def collect():
    files, rows = [], []
    for base in ('src', 'tools', 'Develop'):
        for p in sorted((ROOT / base).rglob('*')):
            if p.suffix.lower() not in ('.cpp', '.hpp', '.h', '.inc', '.rc', '.ui'):
                continue
            rel = p.relative_to(ROOT).as_posix()
            if 'Tests' in p.name or '/tests/' in rel.lower():
                continue
            data = p.read_bytes()
            source = data.decode('utf-8-sig')
            lines = source.splitlines()
            files.append(
                {
                    'file': rel,
                    'sha256': hashlib.sha256(data).hexdigest(),
                    'lines': len(lines),
                }
            )
            for token in TOKEN.finditer(source):
                value = token.group()
                if value.startswith(('//', '/*', "'")):
                    continue
                line = source.count('\n', 0, token.start()) + 1
                context = lines[line - 1].strip()
                raw = re.match(r'^(?:u8|u|U|L)?R"', value) is not None
                body = (
                    value[value.index('(') + 1 : value.rfind(')')]
                    if raw
                    else value[value.index('"') + 1 : -1]
                )
                if not body:
                    kind = 'empty'
                elif raw:
                    kind = 'raw-structured-review'
                elif re.search(r'[\u3400-\u9fff]', body):
                    kind = 'cjk-review'
                elif UI.search(context):
                    kind = 'ui-context-review'
                else:
                    kind = 'other-review'
                anchor = hashlib.sha256((rel + '\0' + body).encode()).hexdigest()[:12]
                rows.append(
                    {
                        'id': 's0.' + anchor,
                        'domain': domain(rel),
                        'file': rel,
                        'line': line,
                        'literal': body,
                        'context': context,
                        'evidence_class': kind,
                        'disposition': 'pending',
                        'proposed_key': '',
                        'notes': '',
                    }
                )
    return files, rows


def main():
    files, rows = collect()
    with (OUT / 'S0-source-literals.csv').open(
        'w', encoding='utf-8-sig', newline=''
    ) as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    manifest = {
        'scope': ['src', 'tools', 'Develop'],
        'files': files,
        'literal_occurrences': len(rows),
        'unique_source_ids': len({r['id'] for r in rows}),
        'limitations': [
            'Source evidence is not a classified UI/key inventory.',
            'Dynamic text sources and Qt/OS-owned text require separate inspection.',
            'Adjacent literal fragments are retained individually; raw JSON requires field review.',
        ],
    }
    (OUT / 'S0-source-manifest.json').write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    print(
        f'Scanned {len(files)} files; {len(rows)} literal occurrences; {manifest["unique_source_ids"]} source IDs.'
    )


if __name__ == '__main__':
    main()
