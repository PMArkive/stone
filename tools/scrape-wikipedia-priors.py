# vim: ft=python ts=4 sts=4 ts=8 et:
# -*- coding: utf8 -*-
import argparse
import bs4
import codecs
import os
import re
import sys
from collections import OrderedDict
from configparser import ConfigParser

kIgnoreRaces = set([
    'American Samoa',
    'District of Columbia',
    'Guam',
    'Northern Mariana Islands',
    'United States Virgin Islands',
])

Districts = OrderedDict()

def fix_unicode(text):
    text = text.strip()
    text = text.replace("ñ", "n")
    return text.encode('ascii', 'ignore').decode('utf8')

def find_empty_pos(rows, index):
    while index < len(rows):
        if rows[index] is None:
            break
        index += 1
    if index >= len(rows):
        return None
    return index

def flatten_cells(tds):
    expanded = []
    for td in tds:
        colspan = td.get('colspan')
        if colspan is None:
            expanded.append(td)
        else:
            expanded.extend([td] * int(colspan))
    return expanded

def flatten_headers(ths):
    rows = []
    index = None
    second_row = False
    old_parent = ths[0].parent
    for th in ths:
        if th.parent is not old_parent:
            if second_row:
                break

            second_row = True
            old_parent = th.parent

            index = find_empty_pos(rows, 0)
            if index is None:
                break
        #endif

        if th.get('rowspan'):
            assert not second_row
            rows.append(th.get_text().strip())
            continue

        colspan = th.get('colspan')
        if colspan and int(colspan) > 1:
            assert not second_row
            rows.extend([None] * int(colspan))
            continue

        # Treat an empty cell in the first row as colspan=1
        if th.get_text().strip() == '' and not second_row:
            rows.extend([None])
            continue

        if index is not None:
            rows[index] = th.get_text().strip()
            index = find_empty_pos(rows, index + 1)
            if index is None:
                break
        else:
            rows.append(th.get_text())
    return rows

def get_candidates(args, td):
    lis = td.find_all('li')
    dem = None
    gop = None
    multi_dem = False
    multi_gop = False
    for li in lis:
        m = re.search("([^\(]+) \((.+)\)", li.get_text())
        if m is None:
            continue
        name = m.group(1)
        party = m.group(2)
        if party == "Republican":
            if gop is not None:
                multi_gop = True
            gop = name
        elif party == "Democratic":
            if dem is not None:
                multi_dem = True
            dem = name
        else:
            continue

    # Don't output one-sided races; however, do coalesce to TBD.
    if multi_dem:
        if gop is None:
            return None
        dem = "TBD"
    if multi_gop:
        if dem is None:
            return None
        gop = "TBD"

    if dem is None and args.allow_tbd:
        dem = "TBD"
    if gop is None and args.allow_tbd:
        gop = "TBD"

    if dem is None or gop is None:
        return None
    return fix_unicode(dem), fix_unicode(gop)

def dump_priors_new(args, table):
    ths = table.find_all('th')
    if not ths:
        return
    headers = flatten_headers(ths)
    if headers is None:
        return

    state_index = None
    first_pct_index = None
    second_pct_index = None
    for index, text in enumerate(headers):
        if 'State' in text or 'District' in text:
            state_index = index
        elif '%' in text and first_pct_index is None:
            first_pct_index = index
        elif '%' in text and second_pct_index is None:
            second_pct_index = index

    if first_pct_index is None:
        return

    assert state_index is not None
    assert first_pct_index is not None
    assert second_pct_index is not None

    if args.year in [2016, 2012, 2008]:
        dem_index = first_pct_index
        gop_index = second_pct_index
    elif args.year in [2000, 2004]:
        dem_index = second_pct_index
        gop_index = first_pct_index

    trs = table.find_all('tr')
    for tr in trs:
        tds = tr.find_all('td')
        if not tds:
            continue
        tds = flatten_cells(tds)

        state = tds[state_index].get_text().strip()
        if '.' in state:
            # 2016 table is abbreviated, try to derive the full name.
            a_href = tds[state_index].find('a')
            href = a_href.get('href')
            state = href[href.find('_in_') + len('_in_'):]
            state = state.replace('_', ' ')

        dem_pct = tds[dem_index].get_text().strip().replace('%', '')
        gop_pct = tds[gop_index].get_text().strip().replace('%', '')
        print('{} = {} - {}'.format(state, dem_pct, gop_pct))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('year', type=int)
    args = parser.parse_args()

    text = sys.stdin.read()
    doc = bs4.BeautifulSoup(text, 'html.parser')

    tables = doc.find_all(class_ = 'sortable')
    for table in tables:
        dump_priors_new(args, table)

if __name__ == '__main__':
    main()
