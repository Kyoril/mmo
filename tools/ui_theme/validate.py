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
    shared_templates = qualify(ET.parse(UI / 'GameTemplates.xml').getroot())
    inherited_styles = {f.get('name'): f.find(NS + 'Visual').get('inherits')
                        for f in shared_templates.findall(NS + 'Frame') if f.find(NS + 'Visual') is not None}
    for theme in ('Legacy', 'BlueGold'):
        folder = UI / 'Themes' / theme
        styles, fonts = {}, {}
        for line in (folder / 'Theme.toc').read_text().splitlines():
            if not line or line.startswith('#'):
                continue
            if line.endswith('.lua'):
                assert (folder / line).is_file(), f'Missing theme script: {line}'
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
                    if theme == 'BlueGold' and '/Textures/' in node.get('texture', ''):
                        assert 0 < float(node.get('borderSize', '0')) <= 48, f'{theme}: Oversized destination corners: {node.attrib}'
                        assert all(float(node.get(k, '0')) > 0 for k in ('leftSize', 'rightSize', 'topSize', 'bottomSize')), f'{theme}: Missing source corner sizes: {node.attrib}'
                for key in ('texture', 'file'):
                    asset = node.get(key)
                    if asset:
                        assert (CLIENT / asset).is_file() or (asset == 'Fonts/FRIZQT__.TTF' and (CLIENT / 'Fonts/EBGaramond-Regular.ttf').is_file()), f'{theme}: Missing {asset}'
                if node.tag == NS + 'Visual':
                    sections = {s.get('name') for s in node.findall(NS + 'ImagerySection')}
                    for section in node.iter(NS + 'Section'):
                        assert section.get('section') in sections, f'{theme}: Unknown section {section.attrib}'
        if theme == 'BlueGold':
            # Empty second-bar slots are intentionally translucent. An unconditional fill
            # here also covers bound-slot artwork, even when EmptySocketTint is zero.
            action = styles['ThemeActionBarButtonTemplateVisual']
            for section_name in ('EmptySocket', 'EmptySocketHovered'):
                section = action.find(NS + f"ImagerySection[@name='{section_name}']")
                assert section is not None and len(section), f'Missing {section_name}'
                assert all(c.get('tint', '').startswith('$') for c in section), f'{section_name}: Every component must honor its visibility tint'
            counter = styles['ThemeCharacterWindowAttributePointsLabelVisual']
            assert all(c.get('horzAlign') == 'CENTER' for c in counter.iter(NS + 'TextComponent')), 'Attribute points must stay centered behind their glow'
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
                # A newly explicit appearance may be identical to the old inherited one
                # (the clock now has its own style). Resolve that implicit legacy visual
                # before comparing; any actual drawing or layout change still fails.
                original_frames = {f.get('name'): f for f in original.iter(NS + 'Frame')}
                for frame in layout.iter(NS + 'Frame'):
                    before = original_frames.get(frame.get('name'))
                    visual = frame.find(NS + 'Visual')
                    inherited = inherited_styles.get(frame.get('inherits'))
                    if before is not None and before.find(NS + 'Visual') is None and visual is not None and inherited:
                        if canonical(visual) == canonical(styles[inherited]):
                            frame.remove(visual)
                assert canonical(original) == canonical(layout), f'Legacy behavior/layout drift: {path}'
        contract = {name: ({s.get('name') for s in v.findall(NS + 'ImagerySection')},
                           {s.get('name') for s in v.findall(NS + 'StateImagery')}) for name, v in styles.items()}
        contracts.append((contract, set(fonts)))
        print(f'{theme}: {len(styles)} styles, {len(fonts)} fonts, {references} references; assets and section links valid')
    assert contracts[0] == contracts[1], 'Themes expose different style/state/section/font contracts'
    print('Theme contracts match' + ('; legacy layout and appearance match original XML (including inherited styles)' if legacy_parity else ''))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy-parity', action='store_true', help='Compare expanded Legacy against the client submodule HEAD')
    validate(parser.parse_args().legacy_parity)

