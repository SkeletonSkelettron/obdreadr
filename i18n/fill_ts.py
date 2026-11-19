"""Fills obdreadr_ka.ts with the translations in ka_translations.py.

Run after `cmake --build build --target update_translations` (lupdate)
has added new strings; lists the ones that still need a translation.

    python i18n/fill_ts.py
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from ka_translations import T  # noqa: E402

TS = os.path.join(HERE, 'obdreadr_ka.ts')


def main():
    tree = ET.parse(TS)
    missing = []
    for context in tree.getroot().iter('context'):
        name = context.find('name').text
        for message in context.findall('message'):
            source = message.find('source').text or ''
            text = T.get(name + '|' + source, T.get(source))
            translation = message.find('translation')
            if text is None:
                missing.append((name, source))
                continue
            translation.attrib.pop('type', None)
            if message.get('numerus') == 'yes':
                for form in translation.findall('numerusform'):
                    form.text = text
            else:
                translation.text = text
    tree.write(TS, encoding='utf-8', xml_declaration=True)
    with open(TS, encoding='utf-8') as f:
        content = f.read()
    # Quotes in text escaped the way lupdate writes them, so the file only
    # changes where a translation did.
    content = re.sub(r'>([^<]*)<', lambda m: '>' + m.group(1).replace("'", '&apos;').replace('"', '&quot;') + '<',
                     content)
    with open(TS, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content.replace("<?xml version='1.0' encoding='utf-8'?>",
                                '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>').rstrip('\n') + '\n')
    for name, source in missing:
        print('missing:', name, '|', source)
    print(len(missing), 'missing')


if __name__ == '__main__':
    main()
