#!/usr/bin/env python3
"""Sons do BOZ (CIwSoundDataADPCM, leitor em 0x10b8ac/0x10b90e): exporta para WAV e cria a partir de WAV.

Dados do objeto (depois do hash do nome):
  u32 tamanho dos dados, u32 nº de amostras, dados IMA ADPCM, u32 taxa (22050), u16 formato (2 = ADPCM),
  u32 amostras por bloco (1017), u32 bytes por bloco (512)
Cada bloco de 512 bytes = cabeçalho IMA (i16 amostra, u8 índice, u8 0) + 1016 nibbles (= MS IMA ADPCM, mono).

Uso:
  iwsound.py export <blackops_etc.dz> <pasta>           todos os sons -> WAV PCM 16 bits
  iwsound.py encode <entrada.wav> <saida.bin> [taxa]    WAV (PCM 16 bits, qualquer taxa/canais) -> dados do objeto
"""
import os
import struct
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
         107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
         876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
         4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
         22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]
BLOCK, SPB = 512, 1017


def _step(pred, idx, nib):
    st = STEPS[idx]
    diff = st >> 3
    if nib & 1:
        diff += st >> 2
    if nib & 2:
        diff += st >> 1
    if nib & 4:
        diff += st
    pred = pred - diff if nib & 8 else pred + diff
    pred = max(-32768, min(32767, pred))
    idx = max(0, min(88, idx + IDX[nib & 7]))
    return pred, idx


def decode(data, count, block=BLOCK):
    out = []
    for b in range(0, len(data), block):
        blk = data[b:b + block]
        if len(blk) < 4:
            break
        pred, idx = struct.unpack_from('<hB', blk, 0)
        idx = min(idx, 88)
        out.append(pred)
        for c in blk[4:]:
            for nib in (c & 0xF, c >> 4):
                pred, idx = _step(pred, idx, nib)
                out.append(pred)
    return out[:count]


def encode(samples):
    out = bytearray()
    pos = 0
    pred, idx = 0, 0
    while pos < len(samples):
        blk = samples[pos:pos + SPB]
        blk += [blk[-1]] * (SPB - len(blk))
        pred = blk[0]
        out += struct.pack('<hBB', pred, idx, 0)
        nibs = []
        for s in blk[1:]:
            st = STEPS[idx]
            diff = s - pred
            nib = 0
            if diff < 0:
                nib = 8
                diff = -diff
            if diff >= st:
                nib |= 4
                diff -= st
            if diff >= st >> 1:
                nib |= 2
                diff -= st >> 1
            if diff >= st >> 2:
                nib |= 1
            pred, idx = _step(pred, idx, nib)
            nibs.append(nib)
        for i in range(0, len(nibs), 2):
            out.append(nibs[i] | (nibs[i + 1] << 4))
        pos += SPB
    return bytes(out)


def parse(d):
    size, count = struct.unpack_from('<2I', d, 0)
    rate, fmt, spb, blk = struct.unpack_from('<IHII', d, 8 + size)
    return {'data': d[8:8 + size], 'count': count, 'rate': rate, 'fmt': fmt, 'spb': spb, 'block': blk}


def build(data, count, rate):
    return struct.pack('<2I', len(data), count) + data + struct.pack('<IHII', rate, 2, SPB, BLOCK)


def read_wav(path, rate):
    w = wave.open(path)
    ch, sw, r, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    assert sw == 2, 'precisa de PCM 16 bits'
    s = struct.unpack('<%dh' % (n * ch), raw)
    mono = [sum(s[i:i + ch]) // ch for i in range(0, len(s), ch)]
    if r != rate:                                   # reamostragem linear
        k = r / rate
        mono = [mono[min(int(i * k), len(mono) - 1)] for i in range(int(len(mono) / k))]
    return mono


def main():
    a = sys.argv[1:]
    if a and a[0] == 'export':
        import dz
        import iwgroup
        os.makedirs(a[2], exist_ok=True)
        ents, _ = dz.entries_of(a[1])
        n = 0
        for d, fn, b, _ in ents:
            if not fn.endswith('.group.bin'):
                continue
            try:
                g = iwgroup.Group.load(b)
            except Exception:
                continue
            for L in g.lists():
                for o in L.objects:
                    if (o.get('cls') or L.cls) != iwgroup.djb2('CIwSoundDataADPCM'):
                        continue
                    s = parse(o['data'])
                    if s['fmt'] != 2:
                        continue
                    pcm = decode(s['data'], s['count'], s['block'])
                    name = '%s__%s.wav' % (d.replace('\\', '_'), iwgroup.name(o['name']))
                    w = wave.open(os.path.join(a[2], name), 'wb')
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(s['rate'])
                    w.writeframes(struct.pack('<%dh' % len(pcm), *pcm))
                    w.close()
                    n += 1
        print('sons exportados:', n)
    elif a and a[0] == 'encode':
        rate = int(a[3]) if len(a) > 3 else 22050
        pcm = read_wav(a[1], rate)
        open(a[2], 'wb').write(build(encode(pcm), len(pcm), rate))
        print('%s: %d amostras, %.1f s' % (a[2], len(pcm), len(pcm) / rate))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
