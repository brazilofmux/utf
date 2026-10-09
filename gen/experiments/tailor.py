"""Apply CLDR collation tailorings to DUCET (Unicode 16.0, CLDR 46).

Throwaway experiment.  Weights are kept as Fractions while tailoring so a
tailored element can always be placed strictly between two existing ones;
respace() later turns every locale's Fractions into one shared integer
weight space by reserving, after each root weight, the most slots any
single locale needs there.  Root elements keep their order (and, after
respacing, their weights) in every locale, which is what lets the DFAs
share rows.
"""
import re, os, sys, bisect, collections
from fractions import Fraction as F

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, '..', 'data', '')
CLDR = os.path.join(HERE, 'cldr', 'common', 'collation', '')
COMMON_S, COMMON_T = 0x20, 0x02

# ---------------------------------------------------------------- root data

def load_root():
    root = {}
    for line in open(GEN + 'allkeys.txt'):
        line = line.split('#')[0].strip()
        if not line or line.startswith('@'): continue
        cps, ces = line.split(';')
        cps = tuple(int(c, 16) for c in cps.split())
        root[cps] = [(int(p, 16), int(s, 16), int(t, 16), v == '*')
                     for v, p, s, t in re.findall(r'\[([.*])(\w{4})\.(\w{4})\.(\w{4})\]', ces)]
    return root

ROOT = load_root()
ROOT_SINGLES = {k[0]: v for k, v in ROOT.items() if len(k) == 1}
ROOT_CONTRACT = {k: v for k, v in ROOT.items() if len(k) > 1}
# Primaries above 0x72B6 are DUCET's implicit-style leads (FBxx), the
# trailing weight after each (>= 0x8000) and U+FFFD's FFFD.  Only the
# regular range is tailored and respaced; leads stay as neighbours.
REGULAR_MAX = 0x72B6
LEVEL_VALUES = [sorted({ce[i] for ces in ROOT.values() for ce in ces
                        if i or ce[0] <= REGULAR_MAX or 0xFB00 <= ce[0] <= 0xFBFF}) for i in range(3)]
LAST_REGULAR = max(ce[0] for ces in ROOT.values() for ce in ces if not ce[3] and ce[0] <= REGULAR_MAX)
LAST_VARIABLE = max(ce[0] for ces in ROOT.values() for ce in ces if ce[3])
TOP = 0x10000      # above every explicit and implicit primary lead

IMPLICIT = [(0x4E00, 0x9FFF, 0xFB40), (0xF900, 0xFAFF, 0xFB40), (0x3400, 0x4DBF, 0xFB80),
            (0x20000, 0x2A6DF, 0xFB80), (0x2A700, 0x2B73F, 0xFB80), (0x2B740, 0x2B81F, 0xFB80),
            (0x2B820, 0x2CEAF, 0xFB80), (0x2CEB0, 0x2EBEF, 0xFB80), (0x30000, 0x3134F, 0xFB80),
            (0x31350, 0x323AF, 0xFB80), (0x2EBF0, 0x2F7FF, 0xFB80), (0x17000, 0x18AFF, 0xFB00),
            (0x18D00, 0x18D7F, 0xFB00), (0x1B170, 0x1B2FF, 0xFB01), (0x18B00, 0x18CFF, 0xFB02)]

def implicit(cp):
    base = next((b for s, e, b in IMPLICIT if s <= cp <= e), 0xFBC0)
    return [(base + (cp >> 15), COMMON_S, COMMON_T, False), ((cp & 0x7FFF) | 0x8000, 0, 0, False)]

# ---------------------------------------------------------- normalization

UDATA = {}
for line in open(GEN + 'UnicodeData.txt'):
    f = line.split(';')
    UDATA[int(f[0], 16)] = (int(f[3]), f[5])
CCC = {cp: v[0] for cp, v in UDATA.items() if v[0]}
DECOMP = {cp: tuple(int(x, 16) for x in v[1].split()) for cp, v in UDATA.items()
          if v[1] and not v[1].startswith('<')}

def nfd(cps):
    out = []
    def put(cp):
        if cp in DECOMP:
            for c in DECOMP[cp]: put(c)
        else:
            out.append(cp)
    for cp in cps: put(cp)
    # canonical ordering (stable sort of each run of non-starters)
    i = 0
    while i < len(out):
        if CCC.get(out[i], 0):
            j = i
            while j < len(out) and CCC.get(out[j], 0): j += 1
            out[i:j] = sorted(out[i:j], key=lambda c: CCC[c])
            i = j
        else:
            i += 1
    return tuple(out)

# ------------------------------------------------------------- rule parser

SYNTAX = set('&<=|/[#')

def read_string(s, i):
    """One string token starting at s[i]; returns (codepoints, next i)."""
    out = []
    while i < len(s):
        c = s[i]
        if c.isspace() or c in SYNTAX: break
        if c == '\\':
            if s[i+1] == 'u':
                out.append(int(s[i+2:i+6], 16)); i += 6
            elif s[i+1] == 'U':
                out.append(int(s[i+2:i+10], 16)); i += 10
            elif s[i+1] == 'x' and s[i+2] == '{':
                j = s.index('}', i); out.append(int(s[i+3:j], 16)); i = j + 1
            else:
                out.append(ord(s[i+1])); i += 2
        elif c == "'":
            if s[i+1] == "'":
                out.append(ord("'")); i += 2
            else:
                j = s.index("'", i + 1)
                q = s[i+1:j]
                q = re.sub(r'\\u(\w{4})', lambda m: chr(int(m.group(1), 16)), q)
                out.extend(ord(x) for x in q); i = j + 1
        else:
            out.append(ord(c)); i += 1
    return out, i

def tokenize(s):
    toks, i = [], 0
    while i < len(s):
        c = s[i]
        if c.isspace(): i += 1
        elif c == '#': i = s.find('\n', i) if '\n' in s[i:] else len(s)
        elif c == '[':
            depth, j = 0, i
            while True:
                if s[j] == '\\': j += 2; continue
                if s[j] == '[': depth += 1
                elif s[j] == ']':
                    depth -= 1
                    if depth == 0: break
                j += 1
            toks.append(('opt', s[i+1:j].strip())); i = j + 1
        elif c == '&': toks.append(('reset',)); i += 1
        elif c in '<=':
            j = i
            while j < len(s) and s[j] == c: j += 1
            lvl = 0 if c == '=' else j - i
            star = j < len(s) and s[j] == '*'
            toks.append(('rel', lvl, star)); i = j + star
        elif c == '|': toks.append(('prefix',)); i += 1
        elif c == '/': toks.append(('ext',)); i += 1
        else:
            cps, i = read_string(s, i)
            if toks and toks[-1][0] == 'str':      # adjacent pieces form one string
                toks[-1] = ('str', toks[-1][1] + cps)
            else:
                toks.append(('str', cps))
    return toks

def parse_set(text):
    """Minimal UnicodeSet: chars, escapes and a-b ranges inside one [...]."""
    body = text.strip()[1:-1]
    cps, i = [], 0
    while i < len(body):
        if body[i].isspace(): i += 1; continue
        if body[i] == '-' and cps:
            nxt, i = read_string(body, i + 1)
            cps.extend(range(cps[-1] + 1, nxt[0] + 1)); cps.extend(nxt[1:])
            continue
        if body[i] == '\\':
            got, i = read_string(body, i)
        else:
            got, i = [ord(body[i])], i + 1
        cps.extend(got)
    return set(cps)

COLL_RE = re.compile(r'<collation type=["\']([\w-]+)["\'](?![^>]*alt=)[^>]*>(?:(?!</collation>).)*?<cr><!\[CDATA\[(.*?)\]\]></cr>', re.S)

def collation_rules(loc, ctype=None):
    path = CLDR + loc.replace('-', '_') + '.xml'
    text = open(path, encoding='utf-8').read()
    if ctype is None:
        m = re.search(r'<defaultCollation>(\w+)', text)
        ctype = m.group(1) if m else 'standard'
    for t, cr in COLL_RE.findall(text):
        if t == ctype:
            return expand_imports(cr)
    # CLDR inheritance: nb and nn take no's; xx_YY takes xx's.
    parent = {'nb': 'no', 'nn': 'no'}.get(loc) or (loc.rsplit('_', 1)[0] if '_' in loc else None)
    if parent and os.path.exists(CLDR + parent + '.xml'):
        return collation_rules(parent, ctype)
    return ''

def expand_imports(cr):
    def sub(m):
        spec = m.group(1)
        loc, _, rest = spec.partition('-u-co-')
        loc = 'root' if loc in ('und', 'root') else loc
        return collation_rules(loc, rest or 'standard')
    return re.sub(r'\[import\s+([\w-]+)\]', sub, cr)

# ------------------------------------------------------------- tailoring

class Locale:
    def __init__(self, name, rules):
        self.name = name
        self.map = {}                  # tuple(cps) -> CE list (Fraction weights)
        self.inserted = [[], [], []]   # per level: sorted Fractions this locale added
        self.settings, self.unsupported = [], collections.Counter()
        self.suppressed = set()
        self.apply(tokenize(rules))

    # -- weight space ----------------------------------------------------
    def neighbours(self, lvl, x):
        """(largest existing < x, smallest existing > x) at level lvl."""
        r, ins = LEVEL_VALUES[lvl], self.inserted[lvl]
        lo = [v for v in (r[bisect.bisect_left(r, x) - 1] if bisect.bisect_left(r, x) else None,
                          ins[bisect.bisect_left(ins, x) - 1] if bisect.bisect_left(ins, x) else None)
              if v is not None]
        hi = [v for v in (r[bisect.bisect_right(r, x)] if bisect.bisect_right(r, x) < len(r) else None,
                          ins[bisect.bisect_right(ins, x)] if bisect.bisect_right(ins, x) < len(ins) else None)
              if v is not None]
        return (max(lo) if lo else 0), (min(hi) if hi else TOP)

    def new_weight(self, lvl, after):
        _, hi = self.neighbours(lvl, after)
        # a fixed small step keeps long chains (zh: 44k primaries in one gap)
        # from halving their way to astronomically large denominators
        step = F(1, 1 << 30)
        gap = F(hi) - F(after)
        w = F(after) + (step if gap > 2 * step else gap / 2)
        bisect.insort(self.inserted[lvl], w)
        return w

    # -- lookups ---------------------------------------------------------
    def ces(self, cps):
        cps, out, i = tuple(cps), [], 0
        while i < len(cps):
            for n in (3, 2, 1):
                key = cps[i:i+n]
                if len(key) < n: continue
                if key in self.map: out += self.map[key]; break
                if n > 1 and key in ROOT_CONTRACT and key[0] not in self.suppressed:
                    out += ROOT_CONTRACT[key]; break
                if n == 1:
                    out += ROOT_SINGLES.get(key[0]) or implicit(key[0])
            i += n
        return [tuple(ce) for ce in out]

    # -- rules -----------------------------------------------------------
    def special_reset(self, what):
        what = what.strip()
        every = [ce for v in ROOT.values() for ce in v]
        def pick(f, sel, key):
            got = [ce for ce in every if sel(ce)]
            return f(got, key=key) if got else None
        found = {
            'last regular': lambda: (LAST_REGULAR, COMMON_S, COMMON_T, False),
            'first regular': lambda: pick(min, lambda c: not c[3] and c[0], lambda c: c[0]),
            'last variable': lambda: (LAST_VARIABLE, COMMON_S, COMMON_T, True),
            'first variable': lambda: pick(min, lambda c: c[3], lambda c: c[0]),
            'last primary ignorable': lambda: pick(max, lambda c: c[0] == 0 and c[1], lambda c: (c[1], c[2])),
            'first primary ignorable': lambda: pick(min, lambda c: c[0] == 0 and c[1], lambda c: (c[1], c[2])),
            'last secondary ignorable': lambda: pick(max, lambda c: c[0] == 0 and c[1] == 0 and c[2], lambda c: c[2]),
            'first secondary ignorable': lambda: pick(min, lambda c: c[0] == 0 and c[1] == 0 and c[2], lambda c: c[2]),
            'last tertiary ignorable': lambda: (0, 0, 0, False),
            'first tertiary ignorable': lambda: (0, 0, 0, False),
        }.get(what)
        return found() if found else None

    def apply(self, toks):
        i, before = 0, 0
        prefix, cur = [], None
        while i < len(toks):
            t = toks[i]
            if t[0] == 'opt':
                o = t[1]
                if o.startswith('before'):
                    before = int(o.split()[1])
                elif o.startswith(('first ', 'last ')):
                    ce = self.special_reset(o)
                    if ce is None:
                        self.unsupported['reset [' + o + ']'] += 1
                        cur = None
                    else:
                        prefix, cur = [], ce
                elif o.startswith('suppressContractions'):
                    self.suppressed |= parse_set(o[len('suppressContractions'):])
                elif o.startswith(('optimize', 'import')):
                    pass
                else:
                    self.settings.append(o)
                i += 1
            elif t[0] == 'reset':
                i += 1
                if toks[i][0] == 'opt' and toks[i][1].startswith('before'):
                    before = int(toks[i][1].split()[1]); i += 1
                if toks[i][0] == 'opt':
                    ce = self.special_reset(toks[i][1])
                    if ce is None:
                        self.unsupported['reset [' + toks[i][1] + ']'] += 1
                    prefix, cur = [], ce
                else:
                    anchor = self.ces(toks[i][1])
                    prefix, cur = anchor[:-1], anchor[-1]
                i += 1
                if before and cur is not None:
                    lvl = before - 1
                    lo, _ = self.neighbours(lvl, cur[lvl])
                    w = list(cur)
                    # a virtual position just below the anchor at that level;
                    # the next relation lands between it and the anchor
                    w[lvl] = (F(lo) + F(cur[lvl])) / 2
                    if lvl == 0: w[1], w[2] = COMMON_S, COMMON_T
                    if lvl == 1: w[2] = COMMON_T
                    cur = tuple(w)
                before = 0
            elif t[0] == 'rel':
                lvl, star = t[1], t[2]
                i += 1
                ctx = None
                if i + 1 < len(toks) and toks[i+1][0] == 'prefix':
                    ctx = toks[i][1]; i += 2
                s = toks[i][1]; i += 1
                ext = []
                if i < len(toks) and toks[i][0] == 'ext':
                    ext = toks[i+1][1]; i += 2
                if cur is None:
                    self.unsupported['relation after unsupported reset'] += 1
                    continue
                if ctx is not None:
                    self.unsupported['prefix (context) rule'] += 1
                    continue
                items = [[c] for c in self.expand_star(s)] if star else [s]
                for item in items:
                    cur = self.relate(lvl, prefix, cur, item, ext)
            else:
                raise ValueError('unexpected token %r in %s' % (t, self.name))

    @staticmethod
    def expand_star(s):
        out, k = [], 0
        while k < len(s):
            if k + 2 < len(s) and s[k+1] == ord('-'):
                out.extend(range(s[k], s[k+2] + 1)); k += 3
            else:
                out.append(s[k]); k += 1
        return out

    def relate(self, lvl, prefix, cur, item, ext):
        p, s, t, var = cur
        if lvl == 0:
            new = cur
        elif lvl == 1:
            new = (self.new_weight(0, p), COMMON_S, COMMON_T, var)
        elif lvl == 2:
            new = (p, self.new_weight(1, s), COMMON_T, var)
        elif lvl == 3:
            new = (p, s, self.new_weight(2, t), var)
        else:   # quaternary: no fourth level here; same weights
            self.unsupported['quaternary relation'] += 1
            new = cur
        ces = list(prefix) + [new] + (self.ces(ext) if ext else [])
        key = tuple(item)
        if len(key) > 3:
            self.unsupported['contraction of %d code points' % len(key)] += 1
        self.map[key] = ces
        return new

    # -- canonical closure --------------------------------------------------
    def closure(self):
        """Equivalent spellings of tailored strings: the NFD form as a
        contraction, and every precomposed character whose NFD begins with
        a tailored string."""
        by_nfd = {}
        for key, ces in list(self.map.items()):
            d = nfd(key)
            by_nfd.setdefault(d, ces)
            if d != key and d not in self.map:
                self.map[d] = ces
        added = 0
        maxlen = max((len(k) for k in by_nfd), default=0)
        for cp, dec in DECOMP.items():
            if (cp,) in self.map: continue
            d = nfd((cp,))
            for n in range(min(len(d), maxlen), 0, -1):
                if d[:n] in by_nfd:
                    if n == len(d): ces = by_nfd[d]
                    else: ces = by_nfd[d[:n]] + self.ces(d[n:])
                    if [tuple(c) for c in ces] != [tuple(c) for c in self.ces((cp,))]:
                        self.map[(cp,)] = ces; added += 1
                    break
        return added

def load_locale(name):
    rules = collation_rules(name)
    loc = Locale(name, rules)
    loc.closure_added = loc.closure()
    return loc

# --------------------------------------------------------------- respacing

def respace(locales):
    """One shared integer weight space for root plus every locale.

    Returns per-level maps value(Fraction or int) -> int, and the number of
    slots each level needs."""
    maps, sizes = [], []
    for lvl in range(3):
        root = [v for v in LEVEL_VALUES[lvl] if lvl or v <= REGULAR_MAX]
        reserve = collections.Counter()
        for loc in locales:
            gaps = collections.Counter()
            for w in loc.inserted[lvl]:
                gaps[bisect.bisect_right(root, w) - 1] += 1
            for g, n in gaps.items():
                reserve[g] = max(reserve[g], n)
        m, nxt = {}, 0
        # value 0 stays 0 (ignorable at this level)
        start = 0
        if root and root[0] == 0:
            m[0] = 0; nxt = 1; start = 1
            if reserve[0]: nxt += reserve[0]
        for gi in range(start, len(root)):
            m[root[gi]] = nxt
            nxt += 1 + reserve[gi]
        for loc in locales:
            gaps = collections.defaultdict(list)
            for w in loc.inserted[lvl]:
                gaps[bisect.bisect_right(root, w) - 1].append(w)
            local = loc.__dict__.setdefault('local', [{}, {}, {}])[lvl]
            local.clear()
            for g, ws in gaps.items():
                base = m[root[g]] if g >= 0 else 0
                for k, w in enumerate(sorted(ws)):
                    local[w] = base + 1 + k
        maps.append(m)
        sizes.append(nxt)
    return maps, sizes

def to_int(ce, maps, loc=None):
    p, s, t, var = ce
    def look(lvl, x):
        if loc is not None and x in loc.local[lvl]: return loc.local[lvl][x]
        if x in maps[lvl]: return maps[lvl][x]
        return x    # implicit primaries and their trail weights are not respaced
    return (look(0, p), look(1, s), look(2, t), var)
