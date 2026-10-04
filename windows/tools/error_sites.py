"""Prints the source line for each MSVC error in a build log matching a substring.

Usage: error_sites.py <log> <substring>
"""
import re
import sys

seen = set()
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if sys.argv[2] not in line:
        continue
    m = re.match(r'\s*([A-Za-z]:[^(]+)\((\d+)(?:,\d+)?\): error', line)
    if not m or (m.group(1), m.group(2)) in seen:
        continue
    seen.add((m.group(1), m.group(2)))
    path, number = m.group(1), int(m.group(2))
    source = open(path, encoding='utf-8', errors='replace').read().split('\n')
    print(f'{path}:{number}: {source[number - 1].strip()}')
