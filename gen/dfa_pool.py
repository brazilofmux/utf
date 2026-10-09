"""Shared-pool DFAs in smutil's compressed format.

A Pool holds several byte-string -> integer machines at once, one per
collator, each reached from its own start state.  Rows are hash-consed, so a
state identical in two machines is stored once: a locale that tailors a few
dozen code points costs only the rows on their paths, and everything else
is root's.

Machines follow integers/pairs with a concrete default (-d 0): an undefined
transition goes to accepting state 0.  There are no don't-cares, so merging
rows is plain equality and can never let one machine's transition fill
another's hole.  As in smutil's MergeAcceptingStates, a non-start state whose
every transition reaches one accepting state collapses into it.

Emission matches smutil and the reader in collate.c: an input translation
table (itt) of byte -> column; a state offset table (sot) into a blob (sbt)
of run-length-coded rows, where an entry y < 128 is a RUN of y copies of the
next value and y >= 128 is a COPY of the 256 - y values that follow.
States are numbered 0..N-1; accepting state v is N + v.
"""

MAX_RUN = 127
MAX_COPY = 128


class Pool:
    def __init__(self):
        self.intern = {}        # row tuple -> state id
        self.rows = []          # state id -> row: 256 of ('A', value) or ('S', id)
        self.starts = {}        # machine name -> start state id

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
        if not is_start and row[0][0] == 'A' and all(r == row[0] for r in row):
            return row[0]
        sid = self.intern.get(row)
        if sid is None:
            sid = len(self.rows)
            self.intern[row] = sid
            self.rows.append(row)
        return ('S', sid)

    def add(self, name, entries):
        """entries: iterable of (bytes, value); no string a prefix of another."""
        trie = {}
        for bs, v in entries:
            if v <= 0:
                raise ValueError('values start at 1; 0 means no match')
            t = trie
            for b in bs[:-1]:
                t = t.setdefault(b, {})
                if not isinstance(t, dict):
                    raise ValueError('%r extends a shorter string' % bs)
            if bs[-1] in t:
                raise ValueError('%r recorded twice or as a prefix' % bs)
            t[bs[-1]] = v
        self.starts[name] = self._node(trie, True)[1]

    def emit(self):
        """Compress every machine into one table set.  Returns a dict with
        itt, sot, sbt, nstates (= the accepting-states start), max_value and
        starts (machine name -> state number)."""
        seen, order, stack = set(), [], sorted(set(self.starts.values()), reverse=True)
        while stack:                     # depth-first from the starts, in order
            s = stack.pop()
            if s in seen:
                continue
            seen.add(s)
            order.append(s)
            stack.extend(sorted((v for k, v in self.rows[s] if k == 'S' and v not in seen),
                                reverse=True))
        number = {s: i for i, s in enumerate(order)}
        n = len(order)

        def target(e):
            return number[e[1]] if e[0] == 'S' else n + e[1]

        columns, itt, seen_cols = [], [0] * 256, {}
        for b in range(256):
            key = tuple(self.rows[s][b] for s in order)
            if key not in seen_cols:
                seen_cols[key] = len(columns)
                columns.append(b)
            itt[b] = seen_cols[key]

        sot, sbt = [], []
        for s in order:
            sot.append(len(sbt))
            sbt.extend(rle([target(self.rows[s][b]) for b in columns]))

        max_value = max((v for s in order for k, v in self.rows[s] if k == 'A'), default=0)
        return dict(itt=itt, sot=sot, sbt=sbt, nstates=n, max_value=max_value,
                    starts={name: number[s] for name, s in self.starts.items()})


def rle(vals):
    """One row in smutil's phrases: runs of 2+ equal values, the rest copies."""
    out, copy, i = [], [], 0

    def flush():
        for j in range(0, len(copy), MAX_COPY):
            chunk = copy[j:j + MAX_COPY]
            out.append(256 - len(chunk))
            out.extend(chunk)
        copy.clear()
    while i < len(vals):
        j = i
        while j + 1 < len(vals) and vals[j + 1] == vals[i]:
            j += 1
        run = j - i + 1
        if run >= 2:
            flush()
            while run:
                k = min(run, MAX_RUN)
                out.extend((k, vals[i]))
                run -= k
        else:
            copy.append(vals[i])
        i = j + 1
    flush()
    return out


def run(tables, start, data):
    """Feed bytes through an emitted machine as collate.c does; returns the
    accepting value, or None if the bytes ran out first."""
    s, n = start, tables['nstates']
    itt, sot, sbt = tables['itt'], tables['sot'], tables['sbt']
    for b in data:
        if s >= n:
            break
        col, off = itt[b], sot[s]
        while True:
            y = sbt[off]
            if y < 128:
                if col < y:
                    s = sbt[off + 1]; break
                col -= y; off += 2
            else:
                y = 256 - y
                if col < y:
                    s = sbt[off + col + 1]; break
                col -= y; off += y + 1
    return s - n if s >= n else None
