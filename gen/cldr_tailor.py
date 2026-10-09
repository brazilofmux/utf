"""Apply CLDR collation tailorings to DUCET.

A locale's rules (data/cldr/collation/<locale>.xml) say where each tailored
string sorts relative to root: "&c < č" puts č just after c at the primary
level.  Locale tracks the result as a map from code-point strings to CE
sequences.  While tailoring, a new weight is a Fraction strictly between two
existing ones, so there is always room; respace() later turns every
locale's Fractions into integers in one weight space shared with root.

Root elements keep their order, and almost always their weights, in every
locale.  That is what lets the locales' DFAs share rows with root's.

Supported: resets (including [before n] and the [first/last ...] positions),
the four relation strengths and '=', star lists, extensions ("/"), imports,
CLDR parent locales, [suppressContractions], and canonical closure.  Prefix
(context) rules and the quaternary level are counted in `unsupported`, and
settings such as [reorder] or [caseFirst] are collected in `settings` for
the caller to implement.
"""
import bisect
import collections
import os
import re
from fractions import Fraction as F

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, 'data')
CLDR = os.path.join(DATA, 'cldr', 'collation')

COMMON_S, COMMON_T = 0x20, 0x02
MAX_CONTRACTION = 4         # longest tailored string libutf can match

# Primaries above 0x72B6 are DUCET's implicit-style leads (FBxx), the trail
# weight after each (0x8000 and up) and U+FFFD's FFFD.  Only the regular
# range is tailored and respaced; the leads stay put as neighbours.
REGULAR_MAX = 0x72B6
TOP = 0x10000               # above every explicit and implicit primary


# ---------------------------------------------------------------- root data

def parse_allkeys(path):
    """allkeys.txt as a dict, in file order: code points -> CE tuple, with a
    CE as (primary, secondary, tertiary, variable)."""
    root = {}
    for line in open(path, encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if not line or line.startswith('@'):
            continue
        cps, ces = line.split(';')
        cps = tuple(int(c, 16) for c in cps.split())
        ces = tuple((int(p, 16), int(s, 16), int(t, 16), v == '*')
                    for v, p, s, t in re.findall(
                        r'\[([.*])([0-9A-F]{4})\.([0-9A-F]{4})\.([0-9A-F]{4})\]', ces))
        if not ces:
            raise ValueError('no CEs parsed: %s' % line)
        root[cps] = ces
    return root


ROOT = parse_allkeys(os.path.join(DATA, 'allkeys.txt'))
ROOT_SINGLES = {k[0]: v for k, v in ROOT.items() if len(k) == 1}
ROOT_CONTRACT = {k: v for k, v in ROOT.items() if len(k) > 1}
LEVEL_VALUES = [sorted({ce[i] for ces in ROOT.values() for ce in ces
                        if i or ce[0] <= REGULAR_MAX or 0xFB00 <= ce[0] <= 0xFBFF})
                for i in range(3)]
LAST_REGULAR = max(ce[0] for ces in ROOT.values() for ce in ces
                   if not ce[3] and ce[0] <= REGULAR_MAX)
LAST_VARIABLE = max(ce[0] for ces in ROOT.values() for ce in ces if ce[3])

IMPLICIT = [(0x4E00, 0x9FFF, 0xFB40), (0xF900, 0xFAFF, 0xFB40), (0x3400, 0x4DBF, 0xFB80),
            (0x20000, 0x2A6DF, 0xFB80), (0x2A700, 0x2B73F, 0xFB80), (0x2B740, 0x2B81F, 0xFB80),
            (0x2B820, 0x2CEAF, 0xFB80), (0x2CEB0, 0x2EBEF, 0xFB80), (0x30000, 0x3134F, 0xFB80),
            (0x31350, 0x323AF, 0xFB80), (0x2EBF0, 0x2F7FF, 0xFB80), (0x17000, 0x18AFF, 0xFB00),
            (0x18D00, 0x18D7F, 0xFB00), (0x1B170, 0x1B2FF, 0xFB01), (0x18B00, 0x18CFF, 0xFB02)]


def implicit(cp):
    """UCA 10.1 implicit weights, as collate.c's ImplicitWeight computes them."""
    base = next((b for s, e, b in IMPLICIT if s <= cp <= e), 0xFBC0)
    return ((base + (cp >> 15), COMMON_S, COMMON_T, False),
            ((cp & 0x7FFF) | 0x8000, 0, 0, False))


# ---------------------------------------------------------- normalization

UDATA = {}
for _line in open(os.path.join(DATA, 'UnicodeData.txt'), encoding='utf-8'):
    _f = _line.split(';')
    UDATA[int(_f[0], 16)] = (_f[2], int(_f[3]), _f[5])
CCC = {cp: v[1] for cp, v in UDATA.items() if v[1]}
DECOMP = {cp: tuple(int(x, 16) for x in v[2].split()) for cp, v in UDATA.items()
          if v[2] and not v[2].startswith('<')}


def category(cp):
    return UDATA.get(cp, ('Cn',))[0]


def nfd(cps):
    out = []

    def put(cp):
        if cp in DECOMP:
            for c in DECOMP[cp]:
                put(c)
        else:
            out.append(cp)
    for cp in cps:
        put(cp)
    i = 0                       # canonical ordering of each run of non-starters
    while i < len(out):
        if CCC.get(out[i], 0):
            j = i
            while j < len(out) and CCC.get(out[j], 0):
                j += 1
            out[i:j] = sorted(out[i:j], key=lambda c: CCC[c])
            i = j
        else:
            i += 1
    return tuple(out)


# ------------------------------------------------------------- rule parser

SYNTAX = set('&<=|/[#')


def read_string(s, i):
    """One string token starting at s[i]; returns (code points, next i)."""
    out = []
    while i < len(s):
        c = s[i]
        if c.isspace() or c in SYNTAX:
            break
        if c == '\\':
            if s[i + 1] == 'u':
                out.append(int(s[i + 2:i + 6], 16)); i += 6
            elif s[i + 1] == 'U':
                out.append(int(s[i + 2:i + 10], 16)); i += 10
            elif s[i + 1] == 'x' and s[i + 2] == '{':
                j = s.index('}', i); out.append(int(s[i + 3:j], 16)); i = j + 1
            else:
                out.append(ord(s[i + 1])); i += 2
        elif c == "'":
            if s[i + 1] == "'":
                out.append(ord("'")); i += 2
            else:
                j = s.index("'", i + 1)
                q = re.sub(r'\\u(\w{4})', lambda m: chr(int(m.group(1), 16)), s[i + 1:j])
                out.extend(ord(x) for x in q); i = j + 1
        else:
            out.append(ord(c)); i += 1
    return out, i


def tokenize(s):
    toks, i = [], 0
    while i < len(s):
        c = s[i]
        if c.isspace():
            i += 1
        elif c == '#':
            j = s.find('\n', i)
            i = len(s) if j < 0 else j
        elif c == '[':
            depth, j = 0, i
            while True:
                if s[j] == '\\':
                    j += 2; continue
                if s[j] == '[':
                    depth += 1
                elif s[j] == ']':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            toks.append(('opt', s[i + 1:j].strip())); i = j + 1
        elif c == '&':
            toks.append(('reset',)); i += 1
        elif c in '<=':
            j = i
            while j < len(s) and s[j] == c:
                j += 1
            star = j < len(s) and s[j] == '*'
            toks.append(('rel', 0 if c == '=' else j - i, star)); i = j + star
        elif c == '|':
            toks.append(('prefix',)); i += 1
        elif c == '/':
            toks.append(('ext',)); i += 1
        else:
            cps, i = read_string(s, i)
            if toks and toks[-1][0] == 'str':     # adjacent pieces form one string
                toks[-1] = ('str', toks[-1][1] + cps)
            else:
                toks.append(('str', cps))
    return toks


def parse_set(text):
    """Minimal UnicodeSet: characters, escapes and a-b ranges in one [...]."""
    body = text.strip()[1:-1]
    cps, i = [], 0
    while i < len(body):
        if body[i].isspace():
            i += 1; continue
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


COLL_RE = re.compile(r'<collation type=["\']([\w-]+)["\'](?![^>]*alt=)[^>]*>'
                     r'(?:(?!</collation>).)*?<cr><!\[CDATA\[(.*?)\]\]></cr>', re.S)
PARENTS = {'nb': 'no', 'nn': 'no'}      # CLDR parentLocales that are not truncation


def collation_rules(loc, ctype=None):
    """The rule text of loc's collation (its default type unless named),
    imports expanded, inheriting from its parent locale if it has none."""
    text = open(os.path.join(CLDR, loc + '.xml'), encoding='utf-8').read()
    if ctype is None:
        m = re.search(r'<defaultCollation>(\w+)', text)
        ctype = m.group(1) if m else 'standard'
    for t, cr in COLL_RE.findall(text):
        if t == ctype:
            return re.sub(r'\[import\s+([\w-]+)\]', _import, cr)
    parent = PARENTS.get(loc) or (loc.rsplit('_', 1)[0] if '_' in loc else None)
    if parent and os.path.exists(os.path.join(CLDR, parent + '.xml')):
        return collation_rules(parent, ctype)
    return ''


def _import(m):
    loc, _, ctype = m.group(1).partition('-u-co-')
    return collation_rules(loc.replace('-', '_'), ctype or 'standard')


# ------------------------------------------------------------- tailoring

class Locale:
    def __init__(self, name, rules):
        self.name = name
        self.map = {}                   # tuple(cps) -> CE list (Fraction weights)
        self.inserted = [[], [], []]    # per level: sorted Fractions this locale added
        self.tertiary_item = {}         # new tertiary Fraction -> the string given it
        self.settings = []
        self.unsupported = collections.Counter()
        self.suppressed = set()
        self.apply(tokenize(rules))

    # -- weight space --------------------------------------------------------
    def neighbours(self, lvl, x):
        """(largest existing weight < x, smallest > x) at level lvl."""
        lo, hi = 0, TOP
        for vals in (LEVEL_VALUES[lvl], self.inserted[lvl]):
            i = bisect.bisect_left(vals, x)
            if i:
                lo = max(lo, vals[i - 1])
            j = bisect.bisect_right(vals, x)
            if j < len(vals):
                hi = min(hi, vals[j])
        return lo, hi

    def new_weight(self, lvl, after):
        _, hi = self.neighbours(lvl, after)
        # A fixed small step keeps a long chain from halving its way to
        # astronomically large denominators; the integers come later.
        step = F(1, 1 << 30)
        gap = F(hi) - F(after)
        w = F(after) + (step if gap > 2 * step else gap / 2)
        bisect.insort(self.inserted[lvl], w)
        return w

    # -- lookups ---------------------------------------------------------------
    def ces(self, cps):
        """CEs of a string as this locale sees it: longest match first."""
        cps, out, i = tuple(cps), [], 0
        while i < len(cps):
            for n in range(MAX_CONTRACTION, 0, -1):
                key = cps[i:i + n]
                if len(key) < n:
                    continue
                if key in self.map:
                    out += self.map[key]; break
                if n > 1 and key in ROOT_CONTRACT and key[0] not in self.suppressed:
                    out += ROOT_CONTRACT[key]; break
                if n == 1:
                    out += ROOT_SINGLES.get(key[0]) or implicit(key[0])
            i += n
        return [tuple(ce) for ce in out]

    # -- rules -------------------------------------------------------------------
    def special_reset(self, what):
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
        }.get(what.strip())
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
                    prefix, cur = [], ce
                elif o.startswith('suppressContractions'):
                    self.suppressed |= parse_set(o[len('suppressContractions'):])
                elif not o.startswith(('optimize', 'import')):
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
                    # A virtual position just below the anchor at that level;
                    # the next relation lands between it and the anchor.
                    lvl = before - 1
                    lo, _ = self.neighbours(lvl, cur[lvl])
                    w = list(cur)
                    w[lvl] = (F(lo) + F(cur[lvl])) / 2
                    if lvl == 0:
                        w[1], w[2] = COMMON_S, COMMON_T
                    if lvl == 1:
                        w[2] = COMMON_T
                    cur = tuple(w)
                before = 0
            elif t[0] == 'rel':
                lvl, star = t[1], t[2]
                i += 1
                ctx = None
                if i + 1 < len(toks) and toks[i + 1][0] == 'prefix':
                    ctx = toks[i][1]; i += 2
                s = toks[i][1]; i += 1
                ext = []
                if i < len(toks) and toks[i][0] == 'ext':
                    ext = toks[i + 1][1]; i += 2
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
            if k + 2 < len(s) and s[k + 1] == ord('-'):
                out.extend(range(s[k], s[k + 2] + 1)); k += 3
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
            self.tertiary_item[new[2]] = tuple(item)
        else:                   # quaternary: no fourth level here
            self.unsupported['quaternary relation'] += 1
            new = cur
        key = tuple(item)
        if len(key) > MAX_CONTRACTION:
            self.unsupported['contraction of %d code points' % len(key)] += 1
        self.map[key] = list(prefix) + [new] + (self.ces(ext) if ext else [])
        return new

    # -- canonical closure --------------------------------------------------------
    def closure(self):
        """Give canonically equivalent spellings the same CEs: a tailored
        string's NFD form (as a contraction); a contraction whose last
        character is spelled precomposed with marks after it ("AÄ" for
        Danish "AA" + diaeresis); and every precomposed character whose NFD
        begins with a tailored string.  Returns how many characters the last
        rule changed."""
        by_nfd = {}
        for key, ces in list(self.map.items()):
            d = nfd(key)
            by_nfd.setdefault(d, ces)
            if d != key and d not in self.map:
                self.map[d] = ces
        starts_with = collections.defaultdict(list)
        for cp in DECOMP:
            d = nfd((cp,))
            if len(d) > 1:
                starts_with[d[0]].append((cp, d[1:]))
        for key, ces in list(self.map.items()):
            if len(key) < 2:
                continue
            for cp, marks in starts_with.get(key[-1], ()):
                spelled = key[:-1] + (cp,)
                if spelled not in self.map and len(spelled) <= MAX_CONTRACTION:
                    self.map[spelled] = list(ces) + self.ces(marks)
        # [suppressContractions]: a precomposed character canonically
        # equivalent to a suppressed contraction (Serbian й, which is и +
        # breve) loses it too, and sorts as its decomposition.
        for cp in DECOMP:
            d = nfd((cp,))
            if d[0] in self.suppressed and (cp,) not in self.map and \
                    any(d[:len(k)] == k for k in ROOT_CONTRACT if k[0] == d[0]):
                self.map[(cp,)] = self.ces(d)
        added = 0
        maxlen = max((len(k) for k in by_nfd), default=0)
        for cp in DECOMP:
            if (cp,) in self.map:
                continue
            d = nfd((cp,))
            for n in range(min(len(d), maxlen), 0, -1):
                if d[:n] in by_nfd:
                    ces = by_nfd[d] if n == len(d) else by_nfd[d[:n]] + self.ces(d[n:])
                    if [tuple(c) for c in ces] != self.ces((cp,)):
                        self.map[(cp,)] = ces
                        added += 1
                    break
        return added


def load_locale(name):
    loc = Locale(name, collation_rules(name))
    loc.closure_added = loc.closure()
    return loc


# --------------------------------------------------------------- respacing

def respace(locales, lvl, root_values, cap):
    """Integer weights at one level for root and every locale.

    Each locale's new weights sit in gaps between root weights.  A gap that
    some locale needs k slots in, and that has fewer than k unused integers
    between its root weights, gets the difference reserved; every root weight
    above moves up by the slots reserved beneath it.  Root weights only ever
    move when a gap is short, so most of root keeps DUCET's numbers.

    Sets loc.local[lvl] (new Fraction -> int) on each locale and returns the
    root map (root weight -> int).  Every result is below cap.
    """
    root = sorted(root_values)
    gaps_of = {}
    need = collections.Counter()
    for loc in locales:
        gaps = collections.defaultdict(list)
        for w in loc.inserted[lvl]:
            g = bisect.bisect_right(root, w) - 1
            if g < 0 or w >= cap:
                raise ValueError('%s: level %d weight %s outside the respaced range'
                                 % (loc.name, lvl + 1, w))
            gaps[g].append(w)
        gaps_of[loc.name] = gaps
        for g, ws in gaps.items():
            need[g] = max(need[g], len(ws))

    out, shift = {}, 0
    for i, v in enumerate(root):
        out[v] = v + shift
        upper = root[i + 1] if i + 1 < len(root) else cap
        shift += max(0, need[i] - (upper - v - 1))
    top = max(out[v] + need[i] for i, v in enumerate(root))
    if top >= cap:
        raise ValueError('level %d needs weight %#x, past %#x' % (lvl + 1, top, cap))

    for loc in locales:
        local = loc.__dict__.setdefault('local', [{}, {}, {}])[lvl]
        local.clear()
        for g, ws in gaps_of[loc.name].items():
            for k, w in enumerate(sorted(ws)):
                local[w] = out[root[g]] + 1 + k
    return out
