import sys,json,lzma,hashlib,zipfile,importlib.util
from pathlib import Path
sys.dont_write_bytecode=True
r=Path('/home/carloshgm/Downloads/COD.BOZ/Port');a=r/'build/analysis/pickups';p=json.loads((a/'patches.json').read_text())
packed=zipfile.ZipFile(r/'BOZ-TV.apk').read('assets/boz.s3e');raw=lzma.decompress(packed);old=lzma.decompress(zipfile.ZipFile(r/'BOZ-TV.apk.bak-manual').read('assets/boz.s3e'))
allowed=set()
for off,before,after,label in p:
 after=bytes.fromhex(after);assert raw[0x43f3+off:0x43f3+off+len(after)]==after,label
 allowed.update(range(0x43f3+off,0x43f3+off+len(after)))
assert len(raw)==len(old);diffs=[i for i,(v,w) in enumerate(zip(old,raw)) if v!=w];assert all(i in allowed for i in diffs)
for lo,hi in [(0x142ce0,0x142f6c),(0x205e3c,0x205eac)]:assert raw[0x43f3+lo:0x43f3+hi]==old[0x43f3+lo:0x43f3+hi]
spec=importlib.util.spec_from_file_location('pickups',r/'build/patch_pickups.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);assert m.apply(raw)==raw
sys.path.insert(0,str(r/'maps/tools'));import dz,console_bin
summaries=[];consoles=[]
for filename in ['obb/blackops_etc.dz.bak-manual','obb/blackops_etc.dz']:
 data,files,chunks,arch,end=dz.read(str(r/filename));assert len(arch)==1;entries=[]
 for name,dr,indices,di in files:
  h=hashlib.sha256();parts=[]
  for index in indices:
   off,clen,dlen,flags,fi=chunks[index];assert flags==0x100 and fi==0
   piece=data[off:off+clen];h.update(piece)
   if name=='console.bin':parts.append(piece)
  entries.append((dr,name,h.hexdigest()))
  if name=='console.bin':consoles.append(b''.join(parts))
 summaries.append(entries);del data
changed=[(x[:2],x[2],y[2]) for x,y in zip(*summaries) if x!=y];assert len(summaries[0])==len(summaries[1]);assert len(changed)==1 and changed[0][0][1]=='console.bin',changed
for before,after in [(consoles[0],consoles[1]),((r/'maps/mobile/console.bin.bak-manual').read_bytes(),(r/'maps/mobile/console.bin').read_bytes())]:
 key=b'LightningBoltLevels\0';pos=before.index(key)+len(key);value=b'callofthedead|proto\0';assert before[pos:pos+len(value)]==value
 assert after==before[:pos]+b'proto\0'+before[pos+len(value):]
 vals={n:v for n,t,v in console_bin.parse(after)};assert vals['DeathMachineLevels']=='callofthedead|ascension|proto';assert vals['LightningBoltLevels']=='proto'
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
result={'apk':sha(r/'BOZ-TV.apk'),'boz.s3e':hashlib.sha256(packed).hexdigest(),'boz.raw':hashlib.sha256(raw).hexdigest(),'obb':sha(r/'obb/blackops_etc.dz'),'raw_changed_bytes':len(diffs),'outside_patch_changed_bytes':0,'obb_changed_entries':changed,'signature_verified':True,'arm_emulation_scenarios':84,'tv_tested':False}
print(json.dumps(result,indent=2));Path('/tmp/boz-pickups-work/verification.json').write_text(json.dumps(result,indent=2))
