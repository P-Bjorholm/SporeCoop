#!/usr/bin/env python3
"""Docs lint (in `make test`, CLAUDE.md 1 and 4):

* no em dash (U+2014) in any tracked text file;
* every Markdown link to `<file>.md#<anchor>` (and `#<anchor>` inside a file) resolves: to an
  `<a id="...">` or to a heading's GitHub slug in that file;
* anchors (`<a id>`) are unique in their file.

Exit code 1 and one line per problem when something is wrong."""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXT_EXT = ('.md', '.cpp', '.h', '.c', '.py', '.sh', '.ini', '.sym', '.yml', '.yaml', '.txt', '.json',
            '.java', '.rc', '.mk', '')
EM_DASH = '—'
LINK = re.compile(r'\]\(([^)\s]*?)(?:\s+"[^"]*")?\)')
ANCHOR = re.compile(r'<a\s+(?:id|name)="([^"]+)"')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')


def tracked():
    out = subprocess.run(['git', '-C', ROOT, 'ls-files', '-z'], capture_output=True, check=True).stdout
    return [p for p in out.decode().split('\0') if p]


def slug(title):
    """GitHub's heading id: lower case, punctuation dropped, spaces to '-'."""
    t = re.sub(r'<[^>]+>', '', title).strip().lower()
    t = re.sub(r'[`*_~]', '', t)
    t = re.sub(r'[^\w\- ]', '', t, flags=re.UNICODE)
    return t.replace(' ', '-')


def anchors_of(path, cache={}):
    if path in cache:
        return cache[path]
    ids, dup = set(), []
    seen = {}
    try:
        with open(path, encoding='utf-8') as f:
            in_code = False
            for line in f:
                if line.startswith('```'):
                    in_code = not in_code
                    continue
                if in_code:
                    continue
                for a in ANCHOR.findall(line):
                    if a in ids:
                        dup.append(a)
                    ids.add(a)
                m = HEADING.match(line)
                if m:
                    s = slug(m.group(2))
                    n = seen.get(s, 0)
                    seen[s] = n + 1
                    ids.add(s if n == 0 else f'{s}-{n}')
    except FileNotFoundError:
        pass
    cache[path] = (ids, dup)
    return cache[path]


def main():
    problems = []
    files = tracked()
    for rel in files:
        path = os.path.join(ROOT, rel)
        if os.path.splitext(rel)[1] not in TEXT_EXT or not os.path.isfile(path):
            continue
        try:
            with open(path, encoding='utf-8') as f:
                text = f.read()
        except (UnicodeDecodeError, OSError):
            continue
        for n, line in enumerate(text.splitlines(), 1):
            if EM_DASH in line:
                problems.append(f'{rel}:{n}: em dash (U+2014): use a colon, comma, parentheses or "-"')
        if not rel.endswith('.md'):
            continue
        ids, dup = anchors_of(path)
        for a in dup:
            problems.append(f'{rel}: anchor "{a}" defined twice')
        in_code = False
        for n, line in enumerate(text.splitlines(), 1):
            if line.startswith('```'):
                in_code = not in_code
            if in_code:
                continue
            for target in LINK.findall(line):
                if '://' in target or target.startswith('mailto:') or '#' not in target:
                    continue
                file_part, anchor = target.split('#', 1)
                if not anchor:
                    continue
                dest = os.path.normpath(os.path.join(os.path.dirname(path), file_part)) if file_part else path
                if not dest.endswith('.md'):
                    continue
                if not os.path.exists(dest):
                    problems.append(f'{rel}:{n}: link to a missing file {file_part}')
                    continue
                if anchor not in anchors_of(dest)[0]:
                    problems.append(f'{rel}:{n}: link to a missing anchor {target}')
    for p in problems:
        print(p)
    print(f'docs_lint: {len(files)} files, {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
