"""Extract the original Glue appearance and derive the navy/brass theme. Run once."""
from pathlib import Path
import copy
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / 'data/client/Interface/GlueUI'
ART = 'Interface/GameUI/Themes/BlueGold/Textures/'

def border(component, asset, dest, source, tint='FFFFFFFF'):
    area = component.find('Area')
    component.clear()
    component.tag = 'BorderComponent'
    component.attrib.update(texture=ART+asset+'.htex', borderSize=str(dest),
        leftSize=str(source), rightSize=str(source), topSize=str(source), bottomSize=str(source), tint=tint)
    if area is not None:
        component.append(area)

def blue(visual):
    result = copy.deepcopy(visual)
    for section in result.findall('ImagerySection'):
        state = section.get('name','').lower()
        tint = 'FFBEB19A'
        if 'hover' in state: tint = 'FFF3DFBA'
        if 'push' in state: tint = 'FF837D70'
        if 'disable' in state: tint = 'FF625F59'
        for c in list(section):
            texture = c.get('texture','').split('/')[-1]
            if texture in ('GlueSkinPanel.htex','DefaultFrameBorderBack.htex','FrameBorder01.htex'):
                index = list(section).index(c)
                fill = ET.Element('ImageComponent', texture=ART+'NavyWellV4.htex', tint='FFB7C0D0')
                if c.find('Area') is not None: fill.append(copy.deepcopy(c.find('Area')))
                section.insert(index, fill)
                border(c, 'WindowFrameV2', 40, 168, 'FFC1B9A8')
            elif texture in ('GlueListWell.htex','GlueSkinWellFrame.htex'):
                index = list(section).index(c)
                section.insert(index, ET.Element('ImageComponent',texture=ART+'NavyWellV4.htex',tint='FF8795AB'))
                border(c,'QuietSlotV2',12,100,'FF68768B')
            elif texture == 'GlueSkinSteelFrame.htex' and c.get('bottomSize') == '494':
                c.set('texture','Interface/GlueUI/GlueListAccent.htex')
                c.attrib.update(leftSize='2',topSize='14',rightSize='22',bottomSize='14')
            elif texture in ('GlueSkinSteelFrame.htex','GlueSkinSteelFrameSelected.htex') or texture.startswith(('Button','PrimaryButton','CharButton')) or texture == 'fg4_slotBrown_result.htex':
                selected = 'select' in texture.lower() or 'select' in state or 'check' in state
                border(c,'SelectedTabV5' if selected else 'ButtonV2',18 if selected else 14,176 if selected else 120,'FFEEE0BD' if selected else tint)
            elif texture.startswith('GlueSkinButton'):
                # The beveled border owns the face; avoid a second opaque fill over it.
                section.remove(c)
            elif texture in ('GlueSkinWellFill.htex','GlueSkinFieldFill.htex'):
                c.set('texture',ART+'NavyWellV4.htex')
                c.set('tint','FF8795AB')
            elif texture == 'GlueListFade.htex':
                old = c.get('tint','')
                c.set('tint', {'5AC8642E':'504A719D','80B4502A':'60435F82','CC96241A':'B07D6030'}.get(old,old))
            elif texture == 'GlueListAccent.htex' and c.get('tint') == 'FFDEA660':
                c.set('tint','FFE5BE70')
            if c.tag == 'TextComponent':
                if c.get('color') == 'FFF3EEE2': c.set('color','FFF0E6D2')
    return result

assert not (UI/'Themes').exists(), 'Already extracted; edit theme XML directly.'
styles = []
fonts = []
sources = {}
for path in UI.glob('*.xml'):
    text = path.read_text(encoding='utf-8')
    tree = ET.fromstring(text)
    for node in tree.iter():
        node.tag = node.tag.split('}')[-1]
    owners = [f for f in tree.iter('Frame') if f.find('Visual') is not None]
    visuals = iter(owners)
    def extract(match):
        owner = next(visuals)
        name = 'ThemeGlue'+owner.get('name')+'Visual'
        styles.append((name,copy.deepcopy(owner.find('Visual'))))
        return '<Visual inherits="'+name+'" />'
    text = re.sub(r'<Visual>.*?</Visual>',extract,text,flags=re.S)
    for f in tree.findall('Font'): fonts.append(copy.deepcopy(f))
    text = re.sub(r'\s*<Font\b[^>]*/>', '',text)
    sources[path] = text

for theme in ('Legacy','BlueGold'):
    folder = UI/'Themes'/theme
    folder.mkdir(parents=True)
    root = ET.Element('UiLayout')
    for name, original in styles:
        frame = ET.SubElement(root,'Frame',name=name)
        # Binding defaults belong to prototypes too, and are rebound on copy.
        for prop in sorted(set(re.findall(r'\$([A-Za-z0-9_]+)',ET.tostring(original,encoding='unicode')))):
            ET.SubElement(frame,'Property',name=prop,value='FFFFFFFF' if 'Color' in prop else '')
        frame.append(blue(original) if theme=='BlueGold' else original)
    ET.indent(root,space='\t')
    (folder/'Styles.xml').write_text(ET.tostring(root,encoding='unicode')+'\n',encoding='utf-8')
    root = ET.Element('UiLayout')
    for original in fonts:
        f = copy.deepcopy(original)
        if theme=='BlueGold': f.set('file','Interface/GameUI/Themes/BlueGold/Fonts/PTSerif-Regular.ttf')
        root.append(f)
    ET.indent(root,space='\t')
    (folder/'Fonts.xml').write_text(ET.tostring(root,encoding='unicode')+'\n',encoding='utf-8')
    (folder/'Theme.toc').write_text('Fonts.xml\nStyles.xml\n',encoding='utf-8')
for path,text in sources.items(): path.write_text(text,encoding='utf-8')
p = UI/'GlueUI.toc'
p.write_text('# Load exactly one appearance before the shared layouts.\nThemes/BlueGold/Theme.toc\n#Themes/Legacy/Theme.toc\n'+p.read_text(encoding='utf-8'),encoding='utf-8')
print(f'Extracted {len(styles)} appearances and {len(fonts)} fonts into both Glue themes.')
