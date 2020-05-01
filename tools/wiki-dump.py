# vim: ft=python sts=4 ts=8 sw=4 tw=99 et:
import sys

text = sys.stdin.read()
for row in text.split('\n'):
    cols = row.split('\t')
    if len(cols) == 0:
        continue
    state = cols[0]
    dem_pct = float(cols[3].replace('%', ''))
    if 'CD-' in state:
        gop_pct = float(cols[5].replace('%', ''))
    else:
        gop_pct = float(cols[6].replace('%', ''))
    print('        {{"{}", {}}},'.format(state, dem_pct - gop_pct))
