"""Compare the tailoring engine's order with ICU 72 (CLDR 42) per locale."""
import os, sys, subprocess, itertools, unicodedata
import tailor
HERE = os.path.dirname(os.path.abspath(__file__))

def mykey(loc, s, levels):
    ces = loc.ces([ord(c) for c in s])
    return tuple(tuple(ce[l] for ce in ces if ce[l]) for l in range(levels))

def testset(loc):
    pool = set()
    for key in loc.map:
        if len(key) > 3: continue
        s = ''.join(map(chr, key)); pool.add(s)
        base = tailor.nfd(key)
        for c in (base[0], base[0] + 1, base[0] - 1):
            if c in tailor.ROOT_SINGLES and unicodedata.category(chr(c)).startswith('L'):
                pool.add(chr(c))
        if len(key) > 1:
            for t in 'az': pool.add(''.join(map(chr, key[:-1])) + t)
        pool.add(s + 'a'); pool.add(s + 'z')
    pool |= set('abcdefghijklmnopqrstuvwxyz')
    # keep to NFC strings ICU and we both read the same way
    return sorted(p for p in pool if unicodedata.normalize('NFC', p) == p)

def check(name, levels=3):
    loc = tailor.load_locale(name)
    if not loc.map: return name, 0, 0, []
    if any(o.startswith('caseFirst') for o in loc.settings): levels = 2
    ts = testset(loc)
    if len(ts) > 700:
        import random; random.Random(1).shuffle(ts); ts = sorted(ts[:700])
    proc = subprocess.run([os.path.join(HERE, 'icukey'), name] + (['2'] if levels == 2 else []),
                         input='\n'.join(ts) + '\n', capture_output=True, text=True)
    out = proc.stdout.split('\n')
    actual = proc.stderr.strip()
    if actual != name and not name.startswith(actual + '_'):
        return name + '(icu:' + actual + ')', 0, 0, []
    icu = {s: bytes.fromhex(k) for s, k in zip(ts, out)}
    # ICU's tertiary key ends with the level-3 bytes; truncate at secondary
    bad, tot, ex = 0, 0, []
    mine = {s: mykey(loc, s, levels) for s in ts}
    reorder = any(o.startswith('reorder') for o in loc.settings)
    script = lambda s: unicodedata.name(s[0], '?').split()[0]
    for a, b in itertools.combinations(ts, 2):
        if reorder and script(a) != script(b): continue
        x = (icu[a] > icu[b]) - (icu[a] < icu[b])
        y = (mine[a] > mine[b]) - (mine[a] < mine[b])
        tot += 1
        if x != y:
            bad += 1
            if len(ex) < 4: ex.append((a, b, x, y))
    return name, tot, bad, ex

if __name__ == '__main__':
    for n in sys.argv[1:] or open(os.path.join(HERE, 'locales.txt')).read().split():
        name, tot, bad, ex = check(n)
        print('%-8s pairs %6d  disagree %5d  %s' % (name, tot, bad, ex))
