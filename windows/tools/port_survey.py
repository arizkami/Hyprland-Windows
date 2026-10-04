"""Summarises a keep-going build of the hyprland_port_survey target.

Usage:
  port_survey.py <log>                     count failed files per first blocker
  port_survey.py <log> <substring>         list failed files whose path or blocker matches
  port_survey.py --compiled <object dir>   list upstream src/ files that compiled

Produce the log with a Ninja build directory configured with -DHYPRLAND_PORT_SURVEY=ON:
  ninja -C <dir> -k 0 hyprland_port_survey > survey.log
"""
import collections
import os
import re
import sys


def compiled(objects):
    for root, _, files in os.walk(objects):
        for name in files:
            if not name.endswith('.obj'):
                continue
            path = os.path.join(root, name).replace('\\', '/')
            marker = path.rfind('/src/')
            if marker != -1:
                yield path[marker + 5:-4]


def blockers(log):
    current = None
    first = {}
    for line in log:
        m = re.match(r'FAILED: .*?src[/\\](.*?)\.obj', line)
        if m:
            current = m.group(1).replace('\\', '/')
            continue
        if not current or current in first:
            continue
        m = re.search(r"Cannot open include file: '([^']+)'", line)
        if m:
            first[current] = m.group(1)
            continue
        m = re.search(r'error C\d+: (.*)', line)
        if m:
            first[current] = 'ERR: ' + m.group(1)[:90]
    return first


def main():
    if sys.argv[1] == '--compiled':
        for path in sorted(compiled(sys.argv[2])):
            print(path)
        return

    log = open(sys.argv[1], encoding='utf-8', errors='replace').read().split('\n')
    first = blockers(log)
    if len(sys.argv) > 2:
        for path, blocker in sorted(first.items()):
            if sys.argv[2] in path or sys.argv[2] in blocker:
                print(path, '->', blocker)
        return

    for blocker, count in collections.Counter(first.values()).most_common(60):
        print(count, blocker)
    print('failed files:', len(first))


if __name__ == '__main__':
    main()
