# vim: set ts=4 sw=4 tw=99 et:
import sys

text = sys.stdin.read()
lines = text.split('\n')

num_gop = 0
num_dem = 0
for line in lines:
    if not line:
        break
    parts = line.split('\t')
    district = parts[0]
    names = parts[1]
    if '(D)' in names and '(R)' not in names:
        num_dem += 1
    elif '(R)' in names and '(D)' not in names:
        num_gop += 1
    else:
        print('Could not determine: {}'.format(district))
print('Num DEM: {}'.format(num_dem))
print('Num GOP: {}'.format(num_gop))
