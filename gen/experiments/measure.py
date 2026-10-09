"""Shared-pool cost of CLDR collation tailorings on libutf's DUCET DFAs."""
import os, sys, re, time, json, collections
import tailor
HERE = os.path.dirname(os.path.abspath(__file__))
from smpy import Pool, utf8

GEN = tailor.GEN
names = open(os.path.join(HERE, 'locales.txt')).read().split()

t0 = time.time()
locs = {n: tailor.load_locale(n) for n in names}
print('tailored %d locales in %.1fs' % (len(locs), time.time() - t0), file=sys.stderr)

# Root CE-sequence indices exactly as gen_ducet.pl assigns them.
root_idx = {}
for key, ces in tailor.ROOT.items():
    root_idx.setdefault(tuple(ces), len(root_idx) + 1)
shipped = {int(c, 16): int(v) for c, v in (l.strip().split(';') for l in open(GEN + 'tr_ducet.txt'))}
assert all(root_idx[tuple(tailor.ROOT_SINGLES[cp])] == v for cp, v in shipped.items())

def measure(group):
    """Respace over group, intern CE sequences, build both DFAs in one pool."""
    maps, sizes = tailor.respace([locs[n] for n in group])
    seq_idx = {tuple(tailor.to_int(ce, maps) for ce in ces): i for ces, i in root_idx.items()}
    n_root_seq = len(seq_idx)
    new_ces = 0
    def idx_of(ces, loc):
        nonlocal new_ces
        key = tuple(tailor.to_int(tuple(ce), maps, loc) for ce in ces)
        if key not in seq_idx:
            seq_idx[key] = len(seq_idx) + 1; new_ces += len(key)
        return seq_idx[key]

    singles, pairs = Pool(), Pool()
    root_pairs = {k: root_idx[tuple(v)] for k, v in tailor.ROOT_CONTRACT.items() if len(k) == 2}
    result = {}                     # pairs side table: CE index -> result slot
    rslot = lambda i: result.setdefault(i, len(result) + 1)
    singles.add('root', [(utf8(cp), v) for cp, v in shipped.items()])
    pairs.add('root', [(utf8(a) + utf8(b), rslot(v)) for (a, b), v in sorted(root_pairs.items())])
    per = {}
    for n in group:
        L = locs[n]
        s = dict(shipped)
        p = {k: v for k, v in root_pairs.items() if k[0] not in L.suppressed}
        three = 0
        for key, ces in L.map.items():
            if len(key) == 1: s[key[0]] = idx_of(ces, L)
            elif len(key) == 2: p[key] = idx_of(ces, L)
            else: three += 1
        singles.add(n, [(utf8(cp), v) for cp, v in sorted(s.items())])
        pairs.add(n, [(utf8(a) + utf8(b), rslot(v)) for (a, b), v in sorted(p.items())])
        per[n] = dict(singles_changed=sum(1 for k in L.map if len(k) == 1),
                      pairs=len(p) - len(root_pairs), three=three, settings=L.settings,
                      unsupported=dict(L.unsupported))
    return dict(singles=singles, pairs=pairs, per=per, sizes=sizes,
                new_seqs=len(seq_idx) - n_root_seq, new_ces=new_ces)

def total(m, names):
    a, b = m['singles'].stats(names), m['pairs'].stats(names)
    return a, b, a['bytes'] + b['bytes']

if __name__ == '__main__':
    groups = json.loads(sys.argv[1])
    out = {}
    for gname, group in groups.items():
        group = [n for n in group if n in locs]
        m = measure(group)
        base_s, base_p, base = total(m, ['root'])
        all_s, all_p, alltot = total(m, ['root'] + group)
        marg = {}
        for n in group:
            s, p, tb = total(m, ['root', n])
            marg[n] = tb - base
        out[gname] = dict(n=len(group), base=base, base_singles=base_s, base_pairs=base_p,
                          pool=alltot, pool_singles=all_s, pool_pairs=all_p,
                          weight_slots=m['sizes'], new_seqs=m['new_seqs'], new_ces=m['new_ces'],
                          marginal=marg, per=m['per'])
        print(gname, json.dumps({k: v for k, v in out[gname].items() if k not in ('marginal', 'per')}), file=sys.stderr)
    json.dump(out, open(os.path.join(HERE, 'results.json'), 'w'), indent=1, ensure_ascii=False)
