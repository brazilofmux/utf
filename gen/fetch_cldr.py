#!/usr/bin/env python3
"""Fetch the CLDR locale data that consumers of the collators need.

libutf itself reads only data/cldr/collation/*.xml, which are committed.
A consumer of the 53 collators (the SLOW-32 COBOL runtime, for its
LOCALE-DATE / LOCALE-TIME / NUMVAL-C LOCALE support) also needs the same
CLDR release's main and supplemental files for those locales, and reads
them from this checkout so the two stay in step.  Those files are large
(about 28 MB), so they are cached here rather than committed:

  data/cldr/main/<locale>.xml            root and each locale in LOCALES
  data/cldr/supplemental/supplementalData.xml
  data/cldr/supplemental/likelySubtags.xml

Each file is fetched from the release-46 tag of unicode-org/cldr (or
linked from a local checkout, see --mirror) and checked against
data/cldr/SHA256SUMS, which is committed and pins the content.  Files
already present and matching the manifest are left alone.

Usage:
  fetch_cldr.py                  fetch what is missing, verify everything
  fetch_cldr.py --verify         verify only; exit 1 on any mismatch
  fetch_cldr.py --mirror DIR     DIR is a CLDR checkout (has common/);
                                 symlink instead of downloading
  fetch_cldr.py --update-manifest
                                 re-fetch everything and rewrite SHA256SUMS
                                 (only when moving to a new CLDR release)
"""

import argparse
import hashlib
import os
import sys
import urllib.request

from gen_ducet import LOCALES

RELEASE = 'release-46'
BASE_URL = f'https://raw.githubusercontent.com/unicode-org/cldr/{RELEASE}/'

HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(HERE, 'data', 'cldr')
MANIFEST = os.path.join(DEST, 'SHA256SUMS')

# (path under common/, path under data/cldr/)
FILES = [(f'main/{loc}.xml', f'main/{loc}.xml') for loc in ['root'] + LOCALES]
FILES += [(f'supplemental/{f}', f'supplemental/{f}')
          for f in ('supplementalData.xml', 'likelySubtags.xml')]


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def read_manifest():
    sums = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    digest, name = line.split(None, 1)
                    sums[name.lstrip('*')] = digest
    return sums


def write_manifest(sums):
    with open(MANIFEST, 'w') as f:
        f.write(f'# sha256 of the CLDR {RELEASE} files cached by fetch_cldr.py\n')
        for name in sorted(sums):
            f.write(f'{sums[name]}  {name}\n')


def fetch(src, dst, mirror):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.lexists(dst):
        os.remove(dst)
    if mirror:
        path = os.path.join(mirror, 'common', src)
        if not os.path.exists(path):
            sys.exit(f'{path}: not in mirror')
        os.symlink(os.path.abspath(path), dst)
        return
    url = BASE_URL + 'common/' + src
    print(f'  {url}')
    tmp = dst + '.part'
    with urllib.request.urlopen(url) as r, open(tmp, 'wb') as f:
        f.write(r.read())
    os.replace(tmp, dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--verify', action='store_true')
    ap.add_argument('--mirror', metavar='DIR',
                    default=os.environ.get('CLDR_MIRROR'))
    ap.add_argument('--update-manifest', action='store_true')
    args = ap.parse_args()

    sums = read_manifest()
    if not sums and not args.update_manifest:
        sys.exit(f'{MANIFEST}: missing; run with --update-manifest')

    bad = 0
    fetched = 0
    for src, rel in FILES:
        dst = os.path.join(DEST, rel)
        present = os.path.exists(dst)
        if args.update_manifest or (not present and not args.verify):
            fetch(src, dst, args.mirror)
            fetched += 1
        elif not present:
            print(f'{rel}: missing', file=sys.stderr)
            bad += 1
            continue
        digest = sha256(dst)
        if args.update_manifest:
            sums[rel] = digest
        elif sums.get(rel) != digest:
            print(f'{rel}: sha256 {digest} does not match SHA256SUMS',
                  file=sys.stderr)
            bad += 1

    if args.update_manifest:
        write_manifest(sums)
        print(f'wrote {MANIFEST}')
    print(f'{len(FILES)} files, {fetched} fetched, {bad} bad')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
