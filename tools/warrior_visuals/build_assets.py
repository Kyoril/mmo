# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Rebuild original warrior audio and finite HPAR bursts. Requires numpy.
No downloaded recordings or dependency on the separate particle_gen workspace.
"""
from pathlib import Path
import struct
import wave
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'data/client'
RATE = 44100

def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data

def string(value):
    data = value.encode()
    return struct.pack('<H', len(data)) + data

def particle(name, color, count=18, spread=2.0, size=.08, lifetime=.35, smoke=False, height=0):
    # Version 2 emitter layout: particle_emitter_serializer.cpp.
    p = string(name)
    p += struct.pack('<BBB4ffII', 1, 0, 0, .12, 0, 0, 0, 0, count, 1)
    p += struct.pack('<fI', 0, count)
    p += struct.pack('<B3f', 2, .18, height, .18)
    p += struct.pack('<2f6f8f', lifetime*.65, lifetime,
                     -spread, .15, -spread, spread, spread*.65, spread,
                     0, 0, size*.65, size, 0, 6.28, -1, 1)
    p += struct.pack('<3f3f3f3f', 0, -.5 if smoke else -4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1)
    p += struct.pack('<IIBfBf', 1, 1, 0, 0, 0 if smoke else 2, 1.5)
    p += string('Particles/IceSmoke.hmat' if smoke else 'Particles/Additive.hmat')
    p += struct.pack('<I', 2)
    for t,v in [(0,1),(1,1.8 if smoke else .15)]:
        p += struct.pack('<4fB', t,v,0,0,0)
    p += struct.pack('<I', 3)
    for t,a in [(0,.7 if smoke else 1),(.25,.5 if smoke else .8),(1,0)]:
        p += struct.pack('<13fB', t,*color,a,*([0]*8),0)
    p += string('')
    data = chunk(b'VERS',struct.pack('<I',0x200)) + chunk(b'PSYS',struct.pack('<I',1)) + chunk(b'RTME',p)
    out = ASSETS / 'Particles/Warrior' / (name+'.hpar')
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_bytes(data)

PARTICLES = {
    'SteelImpact': ((1,.68,.30),20,2.0,.15,.38,False),
    'HeavyImpact': ((1,.52,.18),32,3.2,.2,.45,False),
    'BloodImpact': ((.48,.025,.015),12,1.5,.23,.4,True),
    'CleaveBurst': ((.8,.63,.35),28,4,.16,.38,False),
    'ChargeDust': ((.42,.33,.22),18,2,.5,.65,True),
    'ShockwaveDust': ((.45,.34,.20),42,6,.65,.65,True),
    'RallyBurst': ((.8,.5,.12),24,2,.18,.7,False),
    'RageBurst': ((.75,.06,.02),20,1.8,.25,.65,True),
    'DreadBurst': ((.35,.16,.10),26,3,.38,.6,True),
    'GuardBurst': ((.55,.65,.8),22,1.5,.18,.65,False),
}

SOUNDS = {
    # duration, weight, metal, air, peak. Deliberately quiet enough to layer in combat.
    'Strike': (.28, .4,.6,.2,.32), 'Rend': (.32,.3,.1,.8,.29),
    'Execute': (.55,1,.6,.4,.46), 'Skullbash': (.3,.7,.9,.1,.36),
    'ShieldSlam': (.48,.8,1,.2,.4), 'Cleave': (.4,.55,.3,1,.35),
    'Charge': (.6,.35,.15,1,.32), 'Shockwave': (.85,1,.2,.6,.46),
    'Battlecry': (.7,.65,.1,.5,.33), 'Bloodrush': (.65,.8,.05,.4,.3),
    'Provoke': (.35,.65,.05,.8,.32), 'DemoralizingShout': (.8,.9,.1,.7,.34),
    'LastStand': (.75,.8,.5,.3,.36),
}

def sound(name, spec, seed):
    duration, weight, metal, air, peak = spec
    t=np.arange(int(RATE*duration))/RATE
    rng=np.random.default_rng(seed)
    noise=rng.normal(0,1,len(t))
    low=np.convolve(noise,np.ones(35)/35,mode='same')
    mid=np.convolve(noise,np.ones(5)/5,mode='same')
    thud=np.sin(2*np.pi*(70*t + 65*.045*(1-np.exp(-t/.045))))*np.exp(-t/(.09+weight*.08))
    ring=sum(np.sin(2*np.pi*f*t)*np.exp(-t/d) for f,d in [(473,.055),(811,.08),(1319,.04)]) / 3
    # Noise transients and filtered air, no musical note or synthetic speech.
    x=weight*(.65*thud+1.4*low*np.exp(-t/.13)) + metal*.35*ring
    x+=air*mid*2.2*np.exp(-t/(duration*.26)) + .15*noise*np.exp(-t/.015)
    x-=np.mean(x)
    x*=np.minimum(1,t/.002)*np.minimum(1,(duration-t)/.025)
    x=x/max(float(np.max(np.abs(x))),1e-8)*peak
    out=ASSETS/'Sound/Spells/Warrior'/(name+'.wav')
    out.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(out),'wb') as w:
        w.setparams((1,2,RATE,0,'NONE','not compressed'))
        w.writeframes((x*32767).astype('<i2').tobytes())
    return x

if __name__=='__main__':
    for name,spec in PARTICLES.items(): particle(name,*spec)
    clips=[]
    for i,(name,spec) in enumerate(SOUNDS.items()):
        clips.extend([sound(name,spec,1700+i), np.zeros(int(RATE*.5))])
    preview=ROOT/'generated/warrior_visuals/sound_preview.wav'
    preview.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(preview),'wb') as w:
        w.setparams((1,2,RATE,0,'NONE','not compressed'))
        w.writeframes((np.concatenate(clips)*32767).astype('<i2').tobytes())
    print(f'Created {len(PARTICLES)} finite particle bursts and {len(SOUNDS)} original synthetic sounds.')
