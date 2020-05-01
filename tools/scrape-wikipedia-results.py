# vim: ft=python ts=4 sts=4 ts=8 et:
# -*- coding: utf8 -*-
import argparse
import bs4
import codecs
import os
import re
import sys
import unidecode
from collections import OrderedDict
from configparser import ConfigParser

kIgnoreRaces = set([
    'American Samoa',
    'District of Columbia',
    'Guam',
    'Northern Mariana Islands',
    'United States Virgin Islands',
    'U.S. Virgin Islands',
])

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
            rows.append(th.get_text())
            continue

        colspan = th.get('colspan')
        if colspan and int(colspan) > 1:
            if second_row:
                return None
            rows.extend([None] * int(colspan))
            continue

        if index is not None:
            rows[index] = th.get_text()
            index = find_empty_pos(rows, index + 1)
            if index is None:
                break
        else:
            rows.append(th.get_text())
    return rows

kPatterns = [
    "([^\(]+) \((.+)\)\s*(?:\[[^\]]+\])?\s*(\d+\.?\d*)%",
]

def get_candidates(args, td):
    lis = td.find_all('li')
    dem = None
    gop = None
    multi_dem = False
    multi_gop = False

    kPattern = "([A-Za-z\. -]+) \(([^)]+)\)\s*(?:\[[^\]]+\])?\s*(\d+\.?\d*)%"

    matches = []
    if lis:
        for li in lis:
            text = unidecode.unidecode(li.get_text())
            m = re.search(kPattern, text)
            if m is None:
                continue
            matches.append(m)
    else:
        text = unidecode.unidecode(td.get_text())
        for m in re.finditer(kPattern, text):
            matches.append(m)

    for m in matches:
        name = m.group(1)
        party = m.group(2)
        pct = m.group(3)
        if party in ["Republican", "R"]:
            if gop is not None:
                multi_gop = True
            gop = name, pct
        elif party in ["Democratic", "D", "DFL"]:
            if dem is not None:
                multi_dem = True
            dem = name, pct
        else:
            continue

    # Don't output one-sided races; however, do coalesce to TBD.
    if multi_dem:
        if gop is None:
            return None
        dem = None
    if multi_gop:
        if dem is None:
            return None
        gop = None

    if dem is None or gop is None:
        return None
    return dem[1], gop[1]

def find_districts(args, table):
    ths = table.find_all('th')
    if not ths:
        return False
    headers = flatten_headers(ths)
    if headers is None:
        return False

    # Note: real indices are offset by 1 because the first column uses headers.
    district_index = None
    candidate_index = None
    party_index = None
    for index, text in enumerate(headers):
        if text is None:
            break
        if 'District' in text or 'Location' in text or 'State' in text:
            district_index = index
        elif 'Location' in text:
            district_index = index
        elif 'Candidates' in text:
            candidate_index = index
        elif 'Party' in text:
            party_index = index

    if candidate_index is None or district_index != 0:
        return False

    assert party_index is not None

    trs = table.find_all('tr')
    for tr in trs:
        tds = tr.find_all('td')
        if not tds:
            continue
        tds = flatten_cells(tds)

        th = tr.find('th')
        if th is not None:
            idx_offs = -1
            district_text = th.get_text()
        else:
            idx_offs = 0
            district_text = tds[district_index].get_text()

        district = district_text.strip().encode('ascii', 'ignore').decode('utf8')
        if district.endswith('at-large'):
            district = district[0 : len(district) - len('at-large')]

        m = re.match("([^\d]+)(\d+)", district)
        if m is not None:
            district = m.group(1) + " " + m.group(2)
        district = district.strip()
        if district in kIgnoreRaces:
            continue

        if party_index + idx_offs >= len(tds):
            continue

        party_text = tds[party_index + idx_offs].get_text()
        if "Republican" in party_text:
            party = "gop"
        elif "Democratic" in party_text:
            party = "dem"
        else:
            party = "vacant"

        if candidate_index + idx_offs >= len(tds):
            continue
        td = tds[candidate_index + idx_offs]

        # Ignore no contest races.
        margins = get_candidates(args, td)
        if margins is None:
            continue

        print('{} = {} - {}'.format(district, margins[0], margins[1]))

    return True

def main():
    parser = argparse.ArgumentParser()
    args = parser.parse_args()

    text = sys.stdin.read()
    doc = bs4.BeautifulSoup(text, 'html.parser')

    tables = doc.find_all('table')
    for table in tables:
        find_districts(args, table)

if __name__ == '__main__':
    main()
