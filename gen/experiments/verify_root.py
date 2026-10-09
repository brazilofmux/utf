"""Check smpy against the shipped DUCET tables (expect 702/206/43141 and 294/77/2813)."""
import os
from smpy import Pool, utf8
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'data', '')
p = Pool()
p.add('root', [(utf8(int(c, 16)), int(v)) for c, v in (l.strip().split(';') for l in open(D+'tr_ducet.txt'))])
print('singles', p.stats())
q = Pool(); res = {}
ent = []
for l in open(D+'tr_ducet_contract.txt'):
    a, v = l.strip().split(';'); c1, c2 = a.split()
    idx = res.setdefault(int(v, 16), len(res) + 1)
    ent.append((utf8(int(c1, 16)) + utf8(int(c2, 16)), idx))
q.add('root', ent)
print('pairs', q.stats())
