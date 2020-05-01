# vim: ft=python ts=4 sts=4 ts=8 et:
import argparse
import bs4
import datetime
import google.protobuf.text_format
import os
import poll_pb2
import sys

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--year', type=int, default=2012)
    parser.add_argument('--format', type=str, default='new')
    args = parser.parse_args()

    if hasattr(sys.stdin, 'reconfigure'):
        sys.stdin.reconfigure(encoding='utf-8', errors='ignore')

    text = sys.stdin.read()
    doc = bs4.BeautifulSoup(text, 'html.parser')

    if args.format == 'old':
        dump_old(args, doc)
        return

    polls = poll_pb2.PollList()

    table, columns = find_polling_table(doc)
    assert table

    sample_index = columns.get('sample_index', None)
    gop_index = columns['gop_index']
    dem_index = columns['dem_index']

    trs = table.find_all('tr')
    for row in trs:
        ths = row.find_all('th')

        if row.get('class') is not None and 'final' in row.get('class'):
            continue
        tds = row.find_all('td')
        if not tds:
            continue
        link_elt = tds[0].find_all('a')
        if not link_elt:
            continue

        poll_desc = tds[0].get_text().split('\r\n')[0]
        date = tds[1].get_text()
        dem = tds[dem_index].get_text()
        gop = tds[gop_index].get_text()

        start, end = date.split(' - ')
        start = start.split('/')
        end = end.split('/')

        if sample_index is not None:
            sample_text = tds[sample_index].get_text()
        else:
            sample_text = None
        sample_size, sample_type = derive_sample_info(sample_text)

        poll = poll_pb2.Poll()
        poll.url = link_elt[0].get('href')
        poll.description = poll_desc
        poll.dem = float(dem)
        poll.gop = float(gop)
        poll.margin = poll.dem - poll.gop
        poll.start.year = args.year
        poll.start.month = int(start[0])
        poll.start.day = int(start[1])
        poll.end.year = args.year
        poll.end.month = int(end[0])
        poll.end.day = int(end[1])
        poll.sample_size = sample_size
        poll.sample_type = sample_type
        polls.polls.extend([poll])

    sys.stdout.write(google.protobuf.text_format.MessageToString(polls))
    sys.stdout.flush()

def derive_sample_info(text):
    if text is None:
        return 0, ''

    sample_size = 0
    sample_type = ''
    parts = text.split(' ')
    if len(parts) == 2:
        try:
            sample_size = int(parts[0])
            sample_type = parts[1].lower()
        except:
            pass
    elif len(parts) == 1:
        try:
            sample_size = int(parts[0])
        except:
            sample_type = parts[0].lower()
    if sample_type not in ['lv', 'rv', 'a', 'v']:
        sample_type = ''
    return sample_size, sample_type

def get_parent_table(elt):
    while elt.parent and elt.parent.name != 'table':
        elt = elt.parent
    return elt.parent

def flatten_row(tr):
    row = []
    parent_table = get_parent_table(tr)
    for td in tr.find_all('td'):
        if get_parent_table(td) is not parent_table:
            continue
        row.append(td.get_text().strip())
    return row

def rm_oddities(text):
    text = text.strip()
    text = text.replace('\n', '')
    while '  ' in text:
        text = text.replace('  ', ' ')
    text = text.replace('*', '')
    text = text.replace('//', '/')
    return text

def dump_old(args, doc):
    polls = poll_pb2.PollList()

    poll_header = None
    poll_rows = None

    tables = doc.find_all('table')
    for table in tables:
        # Only look at innermost tables.
        if table.find('table') is not None:
            continue

        trs = table.find_all('tr')
        if trs is None:
            continue

        for index, tr in enumerate(trs):
            if get_parent_table(tr) is not table:
                break

            row = flatten_row(tr)
            if 'Poll' in row[0] and 'Date' in row[0]:
                poll_header = row
                poll_rows = trs[index + 1:]
                break

        if poll_header is not None:
            break

    poll_date_index = None
    sample_index = None
    gop_index = None
    dem_index = None
    for index, text in enumerate(poll_header):
        if 'Poll' in text:
            poll_date_index = index
        elif 'Sample' in text:
            sample_index = index
        elif 'Bush' in text:
            gop_index = index
        elif 'Kerry' in text:
            dem_index = index

    assert poll_date_index is not None
    assert sample_index is not None
    assert gop_index is not None
    assert dem_index is not None

    for row in poll_rows:
        tds = row.find_all('td')

        poll_date_text = rm_oddities(tds[poll_date_index].get_text())
        if 'RCP Average' in poll_date_text:
            continue

        info = tds[poll_date_index].get_text().split('|')
        if len(info) == 1:
            parts = poll_date_text.split(' ')
            info = [' '.join(parts[:-1]), parts[-1]]
        elif len(rm_oddities(info[1])) == 0:
            # Something like "A 5/10|"
            parts = info[0].split(' ')
            info = [' '.join(parts[:-1]), parts[-1]]

        poll_desc = rm_oddities(info[0])

        dates = rm_oddities(info[1])
        if dates.startswith('wk of ') or dates.startswith('w/o '):
            start_date = dates.split(' ')[-1]
            end_date = start_date.split('/')
            end_date = datetime.date(args.year, int(end_date[0]), int(end_date[1]))
            end_date += datetime.timedelta(days = 7)
            end_date = '{}/{}'.format(end_date.month, end_date.day)
        elif ', ' in dates:
            start_date, end_date = dates.split(', ')
            # Fix a case, '1-2-3'.
            if len(start_date.split('-')) == 3:
                parts = start_date.split('-')
                start_date = '{}/{}-{}'.format(*parts)
        elif '-' in dates:
            start_date, end_date = dates.split('-')
            start_date = start_date.replace(',', '/')
        elif len(dates) != 0:
            start_date = dates
            end_date = dates
        else:
            # No date == can't use this poll.
            continue

        start_date = start_date.split('/')
        end_date = end_date.split('/')
        if len(end_date) == 1:
            # Unknown case or no date string.
            if '-' not in dates:
                continue

            # Date like 5/10-13. Redo the split.
            start_date, end_date = dates.split('-')
            start_date = start_date.split('/')
            end_date = [start_date[0], end_date]

        if len(start_date) == 1:
            # Detect malformed date, eg 719 instead of 7/19.
            if len(start_date[0]) == 3:
                start_date = [start_date[0][0], start_date[0][1:]]

        if '-' in start_date[1]:
            start_date[1] = start_date[1].split('-')[0]
        if '-' in end_date[1]:
            end_date[1] = end_date[1].split('-')[0]

        dem_pct = float(tds[dem_index].get_text())
        gop_pct = float(tds[gop_index].get_text())

        sample_text = rm_oddities(tds[sample_index].get_text())
        sample_size, sample_type = derive_sample_info(sample_text)

        a_href = tds[poll_date_index].find('a')
        if a_href is not None:
            url = a_href.get('href')
        else:
            url = ''

        poll = poll_pb2.Poll()
        poll.url = url
        poll.description = poll_desc
        poll.dem = dem_pct
        poll.gop = gop_pct
        poll.margin = poll.dem - poll.gop
        poll.start.year = args.year
        poll.start.month = int(start_date[0])
        poll.start.day = int(start_date[1])
        poll.end.year = args.year
        poll.end.month = int(end_date[0])
        poll.end.day = int(end_date[1])
        poll.sample_size = sample_size
        poll.sample_type = sample_type
        polls.polls.extend([poll])

    sys.stdout.write(google.protobuf.text_format.MessageToString(polls))
    sys.stdout.flush()

def find_polling_table(doc):
    columns = {}
    table = doc.find(id = 'polling-data-full')
    if table:
        for row in table.find_all('tr'):
            ths = row.find_all('th')
            if ths is None:
                continue
            return table, find_poll_table_columns(ths)
        return table, {}

    tds = doc.find_all('td')
    for td in tds:
        if td.find('table'):
            continue
        if "Polling Data" in td.get_text():
            candidate = td

    if candidate:
        table = get_parent_table(candidate)
        if table is None:
            return None, None
        table = table.find('table')
        first_row = table.find('tr')
        tds = first_row.find_all('td')
        return table, find_poll_table_columns(tds)

    return None, {}

def find_poll_table_columns(ths):
    columns = {}
    for index, th in enumerate(ths):
        if '(D)' in th.get_text() or 'Democrat' in th.get_text():
            columns['dem_index'] = index
        elif '(R)' in th.get_text() or 'Republican' in th.get_text():
            columns['gop_index'] = index
        elif 'Sample' in th.get_text():
            columns['sample_index'] = index
    return columns

if __name__ == '__main__':
    main()
