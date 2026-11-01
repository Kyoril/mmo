# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Validate both shipped game themes and optionally prove legacy extraction parity."""
from pathlib import Path
import argparse
import copy
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'data/client'
UI = CLIENT / 'Interface/GameUI'
NS = '{http://www.w3schools.com/Ui}'

def qualify(element):
    for node in element.iter():
        if not node.tag.startswith("{"):
            node.tag = NS + node.tag
    return element

def canonical(element):
    element = copy.deepcopy(element)
    for node in element.iter():
        node.text = (node.text or '').strip() or None
        node.tail = None
    return ET.tostring(element)

def validate(legacy_parity=False):
    contracts = []
    for theme in ('Legacy', 'BlueGold'):
        folder = UI / 'Themes' / theme
        styles, fonts = {}, {}
        for line in (folder / 'Theme.toc').read_text().splitlines():
            if not line or line.startswith('#'):
                continue
            layout = ET.parse(folder / line).getroot()
            for frame in layout.findall(NS + 'Frame'):
                name = frame.get('name')
                assert name not in styles, f'Duplicate style: {name}'
                styles[name] = frame.find(NS + 'Visual')
            for font in layout.findall(NS + 'Font'):
                name = font.get('name')
                assert name not in fonts, f'Duplicate font: {name}'
                fonts[name] = font
            for node in layout.iter():
                if node.tag == NS + 'BorderComponent':
                    assert node.get('texture'), f'{theme}: Border requires a constant texture'
                    assert not list(node.iter(NS + 'PropertyValue')), f'{theme}: Border cannot bind an image property'
                for key in ('texture', 'file'):
                    asset = node.get(key)
                    if asset:
                        assert (CLIENT / asset).is_file() or (asset == 'Fonts/FRIZQT__.TTF' and (CLIENT / 'Fonts/EBGaramond-Regular.ttf').is_file()), f'{theme}: Missing {asset}'
                if node.tag == NS + 'Visual':
                    sections = {s.get('name') for s in node.findall(NS + 'ImagerySection')}
                    for section in node.iter(NS + 'Section'):
                        assert section.get('section') in sections, f'{theme}: Unknown section {section.attrib}'
        references = 0
        for path in UI.rglob('*.xml'):
            if 'Themes' in path.parts:
                continue
            layout = qualify(ET.parse(path).getroot())
            for visual in layout.iter(NS + 'Visual'):
                name = visual.get('inherits')
                assert name in styles, f'{path}: Unknown style {name}'
                references += 1
            for prop in layout.iter(NS + 'Property'):
                if prop.get('name') == 'Font':
                    assert prop.get('value') in fonts or prop.get('value') == 'Default', f'{path}: Unknown font {prop.get("value")}'
            if legacy_parity and theme == 'Legacy':
                original = qualify(ET.fromstring(subprocess.check_output(
                    ['git', '-C', str(CLIENT), 'show', 'HEAD:' + path.relative_to(CLIENT).as_posix()])))
                # Fonts moved without changes; expand appearance references back in place.
                for parent in list(layout.iter()):
                    for index, visual in enumerate(list(parent)):
                        if visual.tag == NS + 'Visual':
                            parent.remove(visual)
                            parent.insert(index, copy.deepcopy(styles[visual.get('inherits')]))
                for font in original.findall(NS + 'Font'):
                    assert canonical(font) == canonical(fonts[font.get('name')])
                    original.remove(font)
                assert canonical(original) == canonical(layout), f'Legacy behavior/layout drift: {path}'
        contract = {name: ({s.get('name') for s in v.findall(NS + 'ImagerySection')},
                           {s.get('name') for s in v.findall(NS + 'StateImagery')}) for name, v in styles.items()}
        contracts.append((contract, set(fonts)))
        print(f'{theme}: {len(styles)} styles, {len(fonts)} fonts, {references} references; assets and section links valid')
    assert contracts[0] == contracts[1], 'Themes expose different style/state/section/font contracts'
    print('Theme contracts match' + ('; legacy expansion matches original XML exactly' if legacy_parity else ''))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy-parity', action='store_true', help='Compare expanded Legacy against the client submodule HEAD')
    validate(parser.parse_args().legacy_parity)

