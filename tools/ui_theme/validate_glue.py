"""Validate Glue theme contracts, assets and the lossless Legacy extraction."""
from pathlib import Path
import copy
import subprocess
import argparse
import xml.etree.ElementTree as E

CLIENT = Path(__file__).resolve().parents[2]/'data/client'
UI = CLIENT/'Interface/GlueUI'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--legacy-ref', default='b444f0819996509a41e5a41a04473d862e860dc2')
args = parser.parse_args()

def normalize(root):
    for node in root.iter():
        node.tag=node.tag.split('}')[-1]
        node.text=(node.text or '').strip() or None
        node.tail=None
    return root

def canonical(root):
    return E.tostring(normalize(copy.deepcopy(root)))

contracts=[]
for theme in ('Legacy','BlueGold'):
    folder=UI/'Themes'/theme
    styles={f.get('name'):f.find('Visual') for f in E.parse(folder/'Styles.xml').getroot()}
    fonts={f.get('name'):f for f in E.parse(folder/'Fonts.xml').getroot()}
    for p in (folder/'Styles.xml',folder/'Fonts.xml'):
        for node in E.parse(p).iter():
            for key in ('texture','file'):
                if node.get(key): assert (CLIENT/node.get(key)).is_file(), node.attrib
    for path in UI.glob('*.xml'):
        layout=normalize(E.parse(path).getroot())
        for frame in layout.iter('Frame'):
            for v in frame.findall('Visual'):
                assert v.get('inherits') in styles
                index=list(frame).index(v)
                frame.remove(v)
                frame.insert(index,copy.deepcopy(styles[v.get('inherits')]))
        if theme=='Legacy':
            original=normalize(E.fromstring(subprocess.check_output(['git','-C',str(CLIENT),'show',args.legacy_ref+':'+path.relative_to(CLIENT).as_posix()])))
            for font in original.findall('Font'):
                assert canonical(font)==canonical(fonts[font.get('name')])
                original.remove(font)
            if path.name == 'CharSelect.xml':
                # Intentional shared layout fix: Enter World now matches the
                # other bottom buttons; all other Legacy geometry stays exact.
                size = original.find(".//Frame[@name='CharSelectEnterButton']/Area/Size/AbsDimension")
                assert size.attrib == {'x': '560', 'y': '166'}
                size.set('y', '128')
            assert canonical(layout)==canonical(original), path
    contracts.append(({name:({s.get('name') for s in v.findall('ImagerySection')},{s.get('name') for s in v.findall('StateImagery')}) for name,v in styles.items()},set(fonts)))
    print(theme, len(styles),'appearances,',len(fonts),'fonts; assets valid')
assert contracts[0]==contracts[1]
print('Glue contracts match; Legacy matches original XML except the approved Enter World height fix.')
