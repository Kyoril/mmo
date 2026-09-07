# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Author the warrior visualization pass; default is a reviewable draft, --apply writes data.
Only visualization_id changes on existing spells. Existing visualization entries are retained.
"""
from pathlib import Path
import argparse, json, subprocess, sys, tempfile, shutil
from datetime import datetime
from google.protobuf import json_format, descriptor_pb2, descriptor_pool, message_factory
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'.agents/skills/mmo-spell-designer/scripts'))
from spell_catalog_lib import load_catalogs
from proto_runtime import find_protoc

OUT=ROOT/'generated/warrior_visuals'

def kit(animation=None, particle=None, sound=None, bone=None, scope='CASTER', delay=0, duration=550):
    k={'scope':scope,'loop':False}
    if animation: k.update(animation_name=animation,duration_ms=duration)
    if particle: k['particles']=['Particles/Warrior/'+particle+'.hpar']
    if sound: k['sounds']=['Sound/Spells/Warrior/'+sound+'.wav']
    if bone: k['attach_bone']=bone
    if delay: k['delay_ms']=delay
    return k

def melee(animation, particle, sound, bone='spine_03', delay=100, duration=550):
    # Verified humanoid sockets; the service falls back to the actor root on other rigs.
    return {'3':{'kits':[kit(animation=animation,duration=duration)]},
            '4':{'kits':[kit(particle=particle,sound=sound,scope='TARGET',bone=bone,delay=delay)]}}

def definitions():
    return [
      ('Strike',[8,71],{'4':{'kits':[kit(particle='SteelImpact',sound='Strike',scope='TARGET',bone='spine_03')]}}),
      ('Battlecry',[9],{'3':{'kits':[kit('CastRelease','RallyBurst','Battlecry',duration=650)]},
                           '5':{'kits':[kit(particle='RallyBurst',scope='TARGET')]}}),
      ('Rend',[19],melee('Attack_1H_01','BloodImpact','Rend')),
      ('Charge',[48],{'3':{'kits':[kit(particle='ChargeDust',sound='Charge')]}}),
      ('Execute',[50],melee('Attack_1H_02','HeavyImpact','Execute',delay=160,duration=650)),
      ('Bloodrush',[62],{'3':{'kits':[kit(particle='RageBurst',sound='Bloodrush')]}}),
      ('Crippling Strike',[69],melee('Attack_1H_01','BloodImpact','Rend',bone='foot_l',delay=100)),
      ('Skullbash',[70],melee('UnarmedAttack01','SteelImpact','Skullbash',bone='head',delay=70,duration=400)),
      ('Shockwave',[122],{'3':{'kits':[kit('UnarmedAttack01',duration=650),kit(particle='ShockwaveDust',sound='Shockwave',delay=120)]}}),
      ('Shield Slam',[142],melee('UnarmedAttack01','HeavyImpact','ShieldSlam',delay=100,duration=500)),
      ('Last Stand',[205],{'3':{'kits':[kit(particle='GuardBurst',sound='LastStand')]}}),
      ('Cleave',[209],{'3':{'kits':[kit('Attack_1H_02',duration=600),kit(particle='CleaveBurst',sound='Cleave',bone='hand_r',delay=80)]},
                        '4':{'kits':[kit(particle='SteelImpact',scope='TARGET',bone='spine_03',delay=130)]}}),
      ('Provoke',[216],{'3':{'kits':[kit('CastRelease','DreadBurst','Provoke',duration=450)]},
                         '4':{'kits':[kit(particle='DreadBurst',scope='TARGET')]}}),
      ('Demoralizing Shout',[217],{'3':{'kits':[kit('CastRelease','DreadBurst','DemoralizingShout',duration=650)]},
                                     '5':{'kits':[kit(particle='DreadBurst',scope='TARGET')]}}),
    ]

def client_types():
    with tempfile.TemporaryDirectory(prefix='warrior_client_schema_') as tmp:
        desc=Path(tmp)/'client.pb'
        src=ROOT/'src/shared/client_data'
        subprocess.run([str(find_protoc(ROOT)),f'-I{src}',f'--descriptor_set_out={desc}','--include_imports','spells.proto','spell_visualizations.proto'],cwd=src,check=True)
        ds=descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool=descriptor_pool.DescriptorPool()
    for f in ds.file: pool.Add(f)
    return [message_factory.GetMessageClass(pool.FindMessageTypeByName('mmo.proto_client.'+n)) for n in ['Spells','SpellVisualizations']]

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--apply',action='store_true'); args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    c=load_catalogs(str(ROOT)); spells=c['spells']; visuals=c['spell_visualizations']
    before={s.id:s.SerializeToString() for s in spells.entry}
    by_id={s.id:s for s in spells.entry}
    existing={v.name:v.id for v in visuals.entry}
    next_id=max(v.id for v in visuals.entry)+1
    drafts=[]; mapping={}; audit=[]
    for name,ids,events in definitions():
        title='Warrior - '+name
        vid=existing.get(title)
        if vid is None: vid=next_id; next_id+=1
        v={'id':vid,'name':title,'kits_by_event':events}
        drafts.append(v)
        for sid in ids:
            s=by_id[sid]
            assert s.name==name, (sid,s.name,name)
            old_vis=next((x for x in visuals.entry if x.id==s.visualization_id),None)
            old_kits=[k for kl in old_vis.kits_by_event.values() for k in kl.kits] if old_vis else []
            audit.append({'id':sid,'name':name,'previous_visualization':s.visualization_id,
                          'previous_animation':any(k.animation_name for k in old_kits),
                          'previous_particles':any(k.particles for k in old_kits),
                          'previous_sound':any(any(k.sounds) for k in old_kits),'new_visualization':vid})
            mapping[sid]=vid
    # Validate the complete draft, asset references and event/scope contract before writes.
    new_visuals=type(visuals)(); new_visuals.CopyFrom(visuals)
    for v in drafts:
        dest=next((x for x in new_visuals.entry if x.id==v['id']),None)
        if dest is None: dest=new_visuals.entry.add()
        else: dest.Clear()
        json_format.ParseDict(v,dest)
        assert dest.IsInitialized()
        for event,kl in dest.kits_by_event.items():
            for k in kl.kits:
                assert not k.loop and not k.HasField('tint')
                assert event!=3 or k.scope==0, 'CastSucceeded has no target list'
                for path in [*k.particles,*k.sounds]: assert (ROOT/'data/client'/path).is_file(),path
    assert len({v.id for v in new_visuals.entry})==len(new_visuals.entry)
    (OUT/'visualizations.json').write_text(json.dumps(drafts,indent=2)+'\n')
    if not (OUT/'audit.json').exists():
        (OUT/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    for sid,vid in mapping.items():
        draft=type(by_id[sid])(); draft.CopyFrom(by_id[sid]); draft.visualization_id=vid
        doc={'format':'mmo-spell','version':1,'spell':json_format.MessageToDict(draft,preserving_proto_field_name=True)}
        (OUT/f'spell_{sid}.json').write_text(json.dumps(doc,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    if not args.apply:
        print('Validated visualization draft; use --apply to install and validate spell links.'); return
    paths=[ROOT/'data/editor/data/spells.data',ROOT/'data/editor/data/spell_visualizations.data',ROOT/'data/client/ClientDB/spells.data',ROOT/'data/client/ClientDB/spell_visualizations.data']
    backup=OUT/('backup_'+datetime.now().strftime('%Y%m%d_%H%M%S')); backup.mkdir()
    for i,p in enumerate(paths): shutil.copy2(p,backup/f'{i}_{p.name}')
    # Make validated visualization IDs available to the skill's spell validator.
    paths[1].write_bytes(new_visuals.SerializeToString())
    for sid in mapping:
        subprocess.run([sys.executable,str(ROOT/'.agents/skills/mmo-spell-designer/scripts/apply_spell_json.py'),str(OUT/f'spell_{sid}.json'),'--project-root',str(ROOT)],check=True)
    client_spells_type,client_visuals_type=client_types()
    cs=client_spells_type.FromString(paths[2].read_bytes())
    cv=client_visuals_type.FromString(paths[3].read_bytes())
    client_before={s.id:s.SerializeToString() for s in cs.entry}
    assert set(mapping).issubset(client_before)
    for s in cs.entry:
        if s.id in mapping: s.visualization_id=mapping[s.id]
    for v in drafts:
        dest=next((x for x in cv.entry if x.id==v['id']),None)
        if dest is None: dest=cv.entry.add()
        else:
            assert dest.name==v['name'], 'Client visualization ID collision'
            dest.Clear()
        json_format.ParseDict(v,dest)
    assert cs.IsInitialized() and cv.IsInitialized()
    paths[2].write_bytes(cs.SerializeToString()); paths[3].write_bytes(cv.SerializeToString())
    after=type(spells).FromString(paths[0].read_bytes())
    for dataset,original in [(after,before),(cs,client_before)]:
        for s in dataset.entry:
            restored=type(s)(); restored.CopyFrom(s)
            if s.id in mapping:
                old=type(s).FromString(original[s.id])
                assert s.visualization_id==mapping[s.id]
                if old.HasField('visualization_id'): restored.visualization_id=old.visualization_id
                else: restored.ClearField('visualization_id')
            assert restored.SerializeToString()==original[s.id],f'Nonvisual data changed: {s.id}'
    print(f'PASS: {len(mapping)} spells, {len(drafts)} visualization kits, editor/client links match; all nonvisual spell data preserved. Backup: {backup}')

if __name__=='__main__': main()
