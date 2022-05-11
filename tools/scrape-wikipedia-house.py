# vim: ft=python ts=4 sts=4 ts=8 et:
import argparse
import bs4
import google.protobuf.text_format
import os
import poll_pb2
import re
import sys

def fix_unicode(text):
    text = text.strip()
    text = text.replace("ñ", "n")
    return text.encode('ascii', 'ignore').decode('utf8')

def fix_district_name(district):
    district = fix_unicode(district)
    if district.endswith('at-large'):
        district = district[0 : len(district) - len('at-large')]

    m = re.match("([^\d]+)(\d+)", district)
    if m is not None:
        district = m.group(1) + " " + m.group(2)
    return district

def scale_rating(text):
    if "Safe D" in text or "Solid D" in text:
        return 3
    elif "Likely D" in text:
        return 2
    elif "Lean D" in text:
        return 1
    elif "Tossup" in text:
        return 0
    elif "Lean R" in text:
        return -1
    elif "Likely R" in text:
        return -2
    elif "Safe R" in text or "Solid R" in text:
        return -3
    elif "N/A":
        return None
    else:
        raise Exception('Unknown rating: {}'.format(text))

kCodeNames = ["tossup", "leans", "likely", "safe"]

def main():
    text = sys.stdin.read()
    doc = bs4.BeautifulSoup(text, 'html.parser')

    data = poll_pb2.HouseRatingList()

    table = doc.find(class_ = 'sortable')
    rows = table.find_all('tr')

    cook_index = None
    uva_index = None
    last_result_index = None
    incumbent = None
    for index, th in enumerate(rows[0].find_all("th")):
        if "Cook" in th.get_text():
            cook_index = index
        elif "Sabato" in th.get_text():
            uva_index = index
        elif "Sab." in th.get_text():
            uva_index = index
        elif "Last result" in th.get_text():
            last_result_index = index
        elif "Previous" in th.get_text():
            last_result_index = index
        elif "Incumbent" in th.get_text():
            incumbent_index = index

    if cook_index is None:
        raise Exception('Could not find Cook column')
    if uva_index is None:
        raise Exception('Could not find UVA column')
    if last_result_index is None:
        raise Exception('Could not find last result column')
    if incumbent_index is None:
        raise Exception('Could not find incumbent column')

    for tr in rows[1:]:
        tds = tr.find_all('td')
        ths = tr.find_all('th')
        try:
            cook_rating = tds[cook_index - 1].get_text()
            uva_rating = tds[uva_index - 1].get_text()
            last_result = tds[last_result_index - 1].get_text()
            incumbent = tds[incumbent_index - 1].get_text()
        except:
            continue

        rating = poll_pb2.HouseRating()
        rating.district = fix_district_name(ths[0].get_text())
        if 'Overall' in rating.district:
            continue

        cook_code = scale_rating(cook_rating)
        #uva_code = scale_rating(uva_rating)
        #chosen_code = None
        #if cook_code >= 0 and uva_code >= 0:
        #    chosen_code = min(cook_code, uva_code)
        #elif cook_code <= 0 and uva_code <= 0:
        #    chosen_code = min(cook_code, uva_code)
        #else:
        #    chosen_code = 0
        chosen_code = cook_code

        if chosen_code is None:
            continue
        if chosen_code > 0:
            rating.presumed_winner = 'dem'
        elif chosen_code < 0:
            rating.presumed_winner = 'gop'
        rating.rating = kCodeNames[abs(chosen_code)]
        data.ratings.extend([rating])

    sys.stdout.write(google.protobuf.text_format.MessageToString(data))
    sys.stdout.flush()

if __name__ == '__main__':
    main()
