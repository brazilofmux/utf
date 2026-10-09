"""Python replica of gen/smutil.cpp for concrete-default machines (no don't-cares).

A machine is a set of byte strings -> accepting value; undefined -> accept 0.
Nodes are hash-consed across every machine built through one Pool, so
several locales built into the same Pool share identical rows exactly as a
shared state pool would."""
import sys

class Pool:
    def __init__(self):
        self.intern = {}     # row tuple -> state id
        self.rows = []       # state id -> row tuple; entries: ('A',v) or ('S',id)
        self.starts = {}     # name -> start state id

    def _node(self, trie, is_start):
        row = []
        for b in range(256):
            c = trie.get(b)
            if c is None:
                row.append(('A', 0))
            elif isinstance(c, dict):
                row.append(self._node(c, False))
            else:
                row.append(('A', c))
        row = tuple(row)
        # MergeAcceptingStates: a non-start state whose every transition
        # reaches one accepting state collapses into it.
        if not is_start and all(r == row[0] for r in row) and row[0][0] == 'A':
            return row[0]
        sid = self.intern.get(row)
        if sid is None:
            sid = len(self.rows)
            self.intern[row] = sid
            self.rows.append(row)
        return ('S', sid)

    def add(self, name, entries):
        """entries: iterable of (bytes, value)."""
        trie = {}
        for bs, v in entries:
            t = trie
            for b in bs[:-1]:
                t = t.setdefault(b, {})
                assert isinstance(t, dict)
            assert bs[-1] not in t, bs
            t[bs[-1]] = v
        self.starts[name] = self._node(trie, True)[1]

    def stats(self, names=None):
        """Size of the machine holding exactly the states reachable from names."""
        names = list(self.starts) if names is None else names
        seen, stack = set(), [self.starts[n] for n in names]
        while stack:
            s = stack.pop()
            if s in seen: continue
            seen.add(s)
            for k, v in self.rows[s]:
                if k == 'S' and v not in seen: stack.append(v)
        states = sorted(seen)
        maxacc = max((v for s in states for k, v in self.rows[s] if k == 'A'), default=0)
        # DetectDuplicateColumns: one column per distinct column vector, kept
        # in first-byte order.
        cols, colkeys = [], set()
        for b in range(256):
            key = tuple(self.rows[s][b] for s in states)
            if key not in colkeys:
                colkeys.add(key); cols.append(b)
        sbt = sum(rle_len([self.rows[s][b] for b in cols]) for s in states)
        total = len(states) + maxacc + 1
        sz_state = 1 if total < 256 else 2 if total < 65536 else 4
        sz_off = 1 if sbt < 256 else 2 if sbt < 65536 else 4
        return dict(states=len(states), columns=len(cols), sbt=sbt, maxacc=maxacc,
                    sz_state=sz_state, sz_off=sz_off,
                    bytes=len(states) * sz_off + sbt * sz_state + 256)

def rle_len(vals):
    """Blob entries smutil's OutputTables emits for one row (runs of >=2 equal
    values become RUN phrases of <=127, the rest COPY phrases of <=128)."""
    n, i, N = 0, 0, len(vals)
    copy = 0
    def flush_copy(c):
        return sum(1 + min(c - j, 128) for j in range(0, c, 128))
    while i < N:
        j = i
        while j + 1 < N and vals[j + 1] == vals[i]: j += 1
        run = j - i + 1
        if run >= 2:
            if copy: n += flush_copy(copy); copy = 0
            n += 2 * ((run + 126) // 127)
        else:
            copy += 1
        i = j + 1
    if copy: n += flush_copy(copy)
    return n

def utf8(cp): return chr(cp).encode('utf-8', 'surrogatepass')
