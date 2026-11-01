"""Reproduce the original, dependency-free dungeon sound effects."""
from pathlib import Path
import math
import struct
import wave

DESTINATION = Path(__file__).resolve().parents[1] / "assets" / "sounds"
SOUNDS = {
    "shoot": (740, 220, 0.09), "hit": (170, 70, 0.09),
    "hurt": (240, 60, 0.22), "kill": (400, 90, 0.18),
    "heal": (460, 920, 0.28), "dash": (170, 520, 0.12),
    "boss": (110, 45, 0.4),
}


def main():
    DESTINATION.mkdir(parents=True, exist_ok=True)
    for name, (start, end, duration) in SOUNDS.items():
        samples = []
        phase = 0.0
        count = int(22050 * duration)
        for index in range(count):
            fraction = index / count
            phase += 2 * math.pi * (start + (end - start) * fraction) / 22050
            envelope = min(1, fraction * 30) * (1 - fraction) ** 2
            sample = (math.sin(phase) + 0.25 * math.sin(phase * 2)) * envelope
            samples.append(struct.pack('<h', int(sample * 18000)))
        with wave.open(str(DESTINATION / (name + '.wav')), 'wb') as audio:
            audio.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
            audio.writeframes(b''.join(samples))



if __name__ == '__main__':
    main()
