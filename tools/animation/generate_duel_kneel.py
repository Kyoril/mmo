# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Author grounded one-knee pose/enter/exit clips on the four existing player rigs.

Requires numpy. Existing skeleton chunks and animation clips are preserved byte-for-byte.
Run from the repository root: python tools/animation/generate_duel_kneel.py
"""
from pathlib import Path
import struct, numpy as np

def mul(a,b):
 w,x,y,z=a;v,i,j,k=b
 return np.array([w*v-x*i-y*j-z*k,w*i+x*v+y*k-z*j,w*j-x*k+y*v+z*i,w*k+x*j-y*i+z*v])
def inv(q):
 return q*np.array([1,-1,-1,-1])/np.dot(q,q)
def rot(q,v):
 return mul(mul(q,np.r_[0,v]),inv(q))[1:]
def blend(a,b,t):
 if np.dot(a,b)<0:b=-b
 q=a*(1-t)+b*t
 return q/np.linalg.norm(q)

def read(path):
 data=Path(path).read_bytes();offset=0;bones={};parents={};anims={};chunks=[];version=0
 while offset<len(data):
  magic,size=struct.unpack_from('<4sI',data,offset);payload=data[offset+8:offset+8+size];chunks.append((magic,payload));offset+=8+size
  if magic==b'REVM':version=struct.unpack('<I',payload)[0]
  if magic==b'ENOB':
   n=payload[0];name=payload[1:1+n].decode();handle=struct.unpack_from('<H',payload,1+n)[0]
   values=np.array(struct.unpack_from('<10f',payload,3+n));bones[handle]=(name,values[:3],values[3:7],values[7:])
  if magic==b'REIH':
   child,parent=struct.unpack('<HH',payload);parents[child]=parent
  if magic==b'MINA':
   n=payload[0];name=payload[1:1+n].decode();duration,base=struct.unpack_from('<ff',payload,1+n);p=9+n;bl=payload[p];p+=1+bl
   if version>=0x300:p+=4
   count=struct.unpack_from('<H',payload,p)[0];p+=2;tracks={}
   for _ in range(count):
    h,nk=struct.unpack_from('<HH',payload,p);p+=4
    tracks[h]=np.array([struct.unpack_from('<11f',payload,p+44*i) for i in range(nk)]);p+=44*nk
   anims[name]=(duration,tracks)
 return bones,parents,anims,chunks

def sample(anim,t):
 out={}
 for h,keys in anim[1].items():
  i=int(np.searchsorted(keys[:,0],t,side='right'))
  if i==0:k=keys[0,1:]
  elif i==len(keys):k=keys[-1,1:]
  else:
   a,b=keys[i-1],keys[i];f=(t-a[0])/(b[0]-a[0]);k=a[1:]*(1-f)+b[1:]*f;k[3:7]=blend(a[4:8],b[4:8],f)
  out[h]=k.copy()
 return out

def world(bones,parents,pose):
 out={}
 def calc(h):
  if h in out:return out[h]
  name,p,q,s=bones[h];k=pose.get(h,np.array([0,0,0,1,0,0,0,1,1,1]));p=p+k[:3];q=mul(q,k[3:7]);s=s*k[7:]
  if h in parents:
   pp,pq,ps=calc(parents[h]);p=pp+rot(pq,ps*p);q=mul(pq,q);s=ps*s
  out[h]=(p,q,s);return out[h]
 for h in bones:calc(h)
 return out

def between(a,b):
 a=a/np.linalg.norm(a);b=b/np.linalg.norm(b)
 q=np.r_[1+np.dot(a,b),np.cross(a,b)]
 if np.linalg.norm(q)<1e-6:
  axis=np.cross(a,[1,0,0])
  if np.linalg.norm(axis)<1e-6:axis=np.cross(a,[0,1,0])
  q=np.r_[0,axis]
 return q/np.linalg.norm(q)

def build_pose(bones,parents,anims,amount,phase=0):
 names={v[0].lower():h for h,v in bones.items()}
 pose=sample(anims['Idle'],phase%anims['Idle'][0])
 baseline=world(bones,parents,sample(anims['Idle'],0))
 reference=world(bones,parents,pose)
 hip=names['pelvis']; scale=baseline[hip][0][1]

 def set_position(h,position):
  w=world(bones,parents,pose)
  if h in parents:
   pp,pq,ps=w[parents[h]];position=rot(inv(pq),position-pp)/ps
  pose[h][:3]=position-bones[h][1]

 def set_orientation(h,orientation):
  w=world(bones,parents,pose)
  if h in parents:orientation=mul(inv(w[parents[h]][1]),orientation)
  pose[h][3:7]=mul(inv(bones[h][2]),orientation)

 # Lower the pelvis while keeping the upright torso and idle breathing.
 p=reference[hip][0].copy();p[1]-=scale*.45*amount
 set_position(hip,p)
 lowered=world(bones,parents,pose)
 for side in ['l','r']:
  thigh,calf,foot=[names[n+'_'+side] for n in ['thigh','calf','foot']]
  h=lowered[thigh][0];k=baseline[calf][0];f=baseline[foot][0]
  upper=np.linalg.norm(baseline[calf][0]-baseline[thigh][0])
  lower=np.linalg.norm(baseline[foot][0]-baseline[calf][0])
  target=f.copy()
  if side=='r':
   # Back knee rests just above ground, its shin extends backward from the knee.
   final_hip=reference[thigh][0]-np.array([0,scale*.45,0])
   knee_y=f[1]*.65
   knee_z=final_hip[2]+np.sqrt(max(0,upper**2-(final_hip[1]-knee_y)**2))
   target=np.array([final_hip[0],f[1],knee_z-np.sqrt(max(0,lower**2-(f[1]-knee_y)**2))])
  else:
   target[0]=baseline[thigh][0][0]+scale*.03
   target[2]=baseline[thigh][0][2]+scale*.48
  target=f*(1-amount)+target*amount
  delta=target-h;distance=np.linalg.norm(delta);direction=delta/distance
  distance=np.clip(distance,abs(upper-lower)+1e-5,upper+lower-1e-5)
  pole=np.array([0.,0.,1.]);pole-=direction*np.dot(pole,direction);pole/=np.linalg.norm(pole)
  along=(upper**2-lower**2+distance**2)/(2*distance)
  knee=h+direction*along+pole*np.sqrt(max(0,upper**2-along**2))
  w=world(bones,parents,pose)
  set_orientation(thigh,mul(between(w[calf][0]-w[thigh][0],knee-h),w[thigh][1]))
  w=world(bones,parents,pose)
  set_orientation(calf,mul(between(w[foot][0]-w[calf][0],target-w[calf][0]),w[calf][1]))
  set_orientation(foot,baseline[foot][1])

 # A small downward head tilt gives the defeat pose a readable silhouette.
 head=names['head'];angle=.16*amount
 set_orientation(head,mul(np.array([np.cos(angle/2),np.sin(angle/2),0,0]),reference[head][1]))
 return pose

def encode_animation(name,duration,poses,times,version):
 b=bytes([len(name)])+name.encode()+struct.pack('<ffB',duration,0,0)
 if version>=0x300:b+=struct.pack('<f',1)
 handles=sorted(poses[0]);b+=struct.pack('<H',len(handles))
 for h in handles:
  values=[pose[h] for pose in poses]
  # Constant finger/helper tracks need only endpoints.
  indexes=range(len(times)) if max(np.max(np.abs(v-values[0])) for v in values)>1e-5 else [0,len(times)-1]
  b+=struct.pack('<HH',h,len(indexes))
  for i in indexes:b+=struct.pack('<11f',times[i],*values[i])
 if version>=0x200:b+=struct.pack('<H',0)
 return b'MINA'+struct.pack('<I',len(b))+b

def author(path):
 bones,parents,anims,chunks=read(path)
 version=struct.unpack('<I',next(v for m,v in chunks if m==b'REVM'))[0]
 generated=[]
 for name,duration,count in [('KneelStart',.9,25),('Kneel',2.,41),('KneelEnd',.75,25)]:
  times=np.linspace(0,duration,count);poses=[]
  for t in times:
   f=t/duration
   if name=='Kneel':amount=1.;phase=t/duration*anims['Idle'][0]
   else:
    amount=f*f*(3-2*f)
    if name=='KneelEnd':amount=1-amount
    phase=0.
   poses.append(build_pose(bones,parents,anims,amount,phase))
  generated.append(encode_animation(name,duration,poses,times,version))
 # Regeneration replaces our three clips; no original bone or animation chunk changes.
 kept=[]
 for magic,payload in chunks:
  if magic==b'MINA' and payload[1:payload[0]+1].decode() in ['KneelStart','Kneel','KneelEnd']:continue
  kept.append(magic+struct.pack('<I',len(payload))+payload)
 result=b''.join(kept+generated)
 Path(path).write_bytes(result)
 # Read the serialized asset back, not just the authoring arrays.
 new_bones,new_parents,new_anims,_=read(path)
 pose=sample(new_anims['Kneel'],0);w=world(new_bones,new_parents,pose)
 names={v[0].lower():h for h,v in new_bones.items()}
 for name in ['KneelStart','Kneel','KneelEnd']:
  assert all(np.isfinite(k).all() for k in new_anims[name][1].values())
 assert w[names['calf_r']][0][1]<w[names['foot_r']][0][1]+.03
 assert w[names['calf_l']][0][1]>w[names['calf_r']][0][1]+.2
 print(path,'clips verified; kneeling knee height',round(w[names['calf_r']][0][1],3),'m')

if __name__=='__main__':
 root=Path('data/client/Models/Character')
 for model in ['Human/Male/HumanMale','Human/Female/HumanFemale_V2','Orc/Male/OrcMale','Orc/Female/OrcFemale']:
  author(root/(model+'.skel'))
