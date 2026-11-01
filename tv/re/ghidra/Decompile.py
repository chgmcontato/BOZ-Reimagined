# -*- coding: utf-8 -*-
# Ghidra (Jython): descompila as funções e grava um .c por módulo.
# Uso: analyzeHeadless <proj> boz -process img.bin -noanalysis -postScript Decompile.py <ghidra_syms.json> <saida> [regex de módulo]
# @category BOZ
import json, re, os
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

args = getScriptArgs()
data = json.load(open(args[0]))
out = args[1]
rx = re.compile(args[2] if len(args) > 2 else '.')
if not os.path.isdir(out):
    os.makedirs(out)
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
fm = currentProgram.getFunctionManager()
di = DecompInterface()
opt = DecompileOptions()
di.setOptions(opt)
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
files = {}
n = 0
for a, name, thumb, mod in sorted(data['funcs']):
    if not rx.search(mod):
        continue
    f = fm.getFunctionAt(af.getAddress(a))
    if f is None:
        continue
    r = di.decompileFunction(f, 30, mon)
    if r is None or not r.decompileCompleted():
        txt = "/* 0x%x %s: falhou */\n" % (a, name)
    else:
        txt = "/* 0x%x %s [%s] */\n%s\n" % (a, name, 'thumb' if thumb else 'arm', r.getDecompiledFunction().getC())
    fp = files.get(mod)
    if fp is None:
        fp = files[mod] = open(os.path.join(out, mod + '.c'), 'w')
    fp.write(txt)
    n += 1
    if n % 500 == 0:
        print("decompiladas %d" % n)
for fp in files.values():
    fp.close()
print("total %d" % n)
