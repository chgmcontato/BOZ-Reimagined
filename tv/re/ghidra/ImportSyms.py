# -*- coding: utf-8 -*-
# Ghidra (Jython): aplica os símbolos de build/re/out/ghidra_syms.json na imagem crua (offset 0 = imagem).
# Uso: analyzeHeadless <proj> boz -import img.bin -loader BinaryLoader -processor ARM:LE:32:v7 -noanalysis
#      -postScript ImportSyms.py <ghidra_syms.json>
# @category BOZ
import json, re
from ghidra.program.model.symbol import SourceType
from java.math import BigInteger

args = getScriptArgs()
data = json.load(open(args[0]))
af = currentProgram.getAddressFactory().getDefaultAddressSpace()
fm = currentProgram.getFunctionManager()
st = currentProgram.getSymbolTable()
tmode = currentProgram.getProgramContext().getRegister("TMode")
ctx = currentProgram.getProgramContext()
listing = currentProgram.getListing()

def clean(n):
    n = n.replace('::', '__')
    return re.sub(r'[^A-Za-z0-9_.]', '_', n)[:200]

# strings da rodata
mem = currentProgram.getMemory()
start, end = 0x3b3800, 0x410000
buf = bytearray(end - start)
for i in range(end - start):
    pass
raw = getBytes(af.getAddress(start), end - start)
i = 0
n_str = 0
while i < len(raw):
    j = i
    while j < len(raw) and 32 <= (raw[j] & 0xff) < 127:
        j += 1
    if j - i >= 3 and j < len(raw) and raw[j] == 0:
        try:
            createAsciiString(af.getAddress(start + i), j - i + 1)
            n_str += 1
        except:
            pass
        i = j + 1
    else:
        i = j + 1 if j > i else i + 1
print("strings: %d" % n_str)

for a, n in data['labels']:
    try:
        st.createLabel(af.getAddress(a), clean(n), SourceType.USER_DEFINED)
    except:
        pass

ok = 0
for a, n, thumb, mod in data['funcs']:
    addr = af.getAddress(a)
    try:
        ctx.setValue(tmode, addr, addr, BigInteger.ONE if thumb else BigInteger.ZERO)
    except:
        pass
for a, n, thumb, mod in data['funcs']:
    addr = af.getAddress(a)
    try:
        disassemble(addr)
        f = fm.getFunctionAt(addr) or createFunction(addr, clean(n))
        if f:
            f.setName(clean(n), SourceType.USER_DEFINED)
            ok += 1
    except Exception as e:
        pass
print("funcoes: %d" % ok)
