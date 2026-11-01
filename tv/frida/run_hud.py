#!/usr/bin/env python3
"""Roda hudtree.js com os hashes dos nomes do HUD e imprime cada elemento com os campos (ponteiros resolvidos)."""
import json, os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../maps/tools'))
import iwgroup
NAMES = ['Perk_Revival_Image', 'Perk_Juggernog_Image', 'Perk_Speed_Cola_Image', 'Perk_Double_Tap_Image',
         'Perk_StaminUp_Image', 'Perk_PHD_Flopper_Image', 'Perk_Deadshot_Image', 'Double_Points_Image', 'wave_1',
         'white_wave_1', '10_waves_or_more', 'Score_Points', 'Clip_Label', 'weapon_icon', 'ammo_images', 'ammo_labels',
         'Grenade_Label', 'grenade_icon', 'Melee_Btn', 'Low_Ammo_Label', 'Alternate_Fire_Switch_Img', 'hud_icons', 'HUD_ALL']
known = {iwgroup.djb2(n): n for n in NAMES}
known.update(iwgroup.NAMES)
here = os.path.dirname(os.path.abspath(__file__))
js = open(os.path.join(here, 'hudtree.js')).read().replace('HUD_HASHES', json.dumps([iwgroup.djb2(n) for n in NAMES]))
tmp = sys.argv[1]
open(tmp, 'w').write(js)
out = subprocess.run([sys.executable, os.path.join(here, 'run.py'), tmp, '40'], capture_output=True, text=True).stdout
for line in out.splitlines():
    if not line.startswith('['):
        print(line); continue
    for o in json.loads(line):
        nm = known.get(int(o['h'], 16), o['h'])
        w = [('@' + known.get(int(x[1:], 16), x[1:])) if x.startswith('@') else x for x in o['w']]
        print('%s %s vt=img+%s' % (nm, o['at'], o['vt']))
        print('   ' + ' '.join('%d:%s' % (i, x) for i, x in enumerate(w)))
