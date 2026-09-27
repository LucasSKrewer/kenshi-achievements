"""Gera mod/KenshiAchievements/achievement.wav: sino metálico em arpejo ascendente (Mi maior).

Só usa a stdlib. Rode:  python scripts/gerar-som.py
"""
import math
import os
import random
import struct
import wave

RATE = 44100
DUR = 1.6
PEAK = 0.45  # ~ -7 dBFS: PlaySound ignora o volume do jogo, então não pode estourar

# (início em s, frequência Hz, ganho)
NOTES = [
    (0.00, 659.25, 0.9),   # E5
    (0.09, 830.61, 0.85),  # G#5
    (0.18, 987.77, 0.8),   # B5
    (0.30, 1318.51, 1.0),  # E6 — nota final, sustenta
]

# Parciais de sino (razão, ganho, decaimento em 1/s). Razões levemente inarmônicas dão o metal.
PARTIALS = [(1.0, 1.0, 3.2), (2.0, 0.45, 5.0), (2.76, 0.25, 7.0), (4.07, 0.12, 10.0), (5.4, 0.06, 14.0)]

n = int(RATE * DUR)
buf = [0.0] * n

for start, freq, gain in NOTES:
    s0 = int(start * RATE)
    for ratio, pg, decay in PARTIALS:
        f = freq * ratio
        if f > RATE / 2.2:
            continue
        w = 2 * math.pi * f / RATE
        for i in range(n - s0):
            t = i / RATE
            attack = min(1.0, t / 0.004)  # 4 ms de ataque: sem clique
            buf[s0 + i] += gain * pg * attack * math.exp(-decay * t) * math.sin(w * i)

# Brilho: ruído filtrado curto no ataque da última nota
random.seed(7)
s0 = int(0.30 * RATE)
prev = 0.0
for i in range(int(0.25 * RATE)):
    x = random.uniform(-1, 1)
    hp = x - prev  # passa-alta simples
    prev = x
    buf[s0 + i] += 0.05 * hp * math.exp(-18 * i / RATE)

# Fade-out final e normalização
fade = int(0.15 * RATE)
for i in range(fade):
    buf[n - fade + i] *= 1 - i / fade
peak = max(abs(v) for v in buf)
buf = [v / peak * PEAK for v in buf]

out = os.path.join(os.path.dirname(__file__), "..", "mod", "KenshiAchievements", "achievement.wav")
with wave.open(out, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", int(v * 32767)) for v in buf))
print("OK:", os.path.abspath(out))
