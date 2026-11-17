#!/usr/bin/env python3
"""Writes iOS/Resources/ba-ding.caf, the push notification's sound: a short
low note ("ba") and a longer high one that rings out ("ding"), made here from
sine waves, so the app ships no sound of anyone else's. Needs macOS's afconvert.

    scripts/make-ba-ding-sound.py
"""
import math
import os
import struct
import subprocess
import tempfile
import wave

sample_rate = 44100
peak_amplitude = 0.6
largest_sample = 32767
ba_frequency = 880.0          # A5
ding_frequency = 1318.51      # E6, a fifth above
ba_seconds = 0.11
gap_seconds = 0.03
ding_seconds = 0.9
ba_decay_per_second = 18.0
ding_decay_per_second = 4.5
attack_seconds = 0.004
bell_partials = [(1.0, 1.0), (2.0, 0.35), (3.01, 0.12)]


def make_note(frequency, seconds, decay_per_second):
    samples = []
    for index in range(int(seconds * sample_rate)):
        time = index / sample_rate
        attack = min(1.0, time / attack_seconds)
        envelope = attack * math.exp(-decay_per_second * time)
        tone = sum(weight * math.sin(2 * math.pi * frequency * ratio * time)
                   for ratio, weight in bell_partials)
        samples.append(envelope * tone)
    return samples


def main():
    total_weight = sum(weight for _, weight in bell_partials)
    samples = (make_note(ba_frequency, ba_seconds, ba_decay_per_second)
               + [0.0] * int(gap_seconds * sample_rate)
               + make_note(ding_frequency, ding_seconds, ding_decay_per_second))
    scale = peak_amplitude * largest_sample / total_weight
    frames = b"".join(struct.pack("<h", int(sample * scale)) for sample in samples)

    output = os.path.join(os.path.dirname(__file__), "..", "iOS", "Resources", "ba-ding.caf")
    with tempfile.TemporaryDirectory() as folder:
        wav_path = os.path.join(folder, "ba-ding.wav")
        with wave.open(wav_path, "wb") as file:
            file.setnchannels(1)
            file.setsampwidth(2)
            file.setframerate(sample_rate)
            file.writeframes(frames)
        subprocess.run(["afconvert", "-f", "caff", "-d", "LEI16", wav_path, output], check=True)
    print(f"wrote {os.path.normpath(output)}: {len(samples) / sample_rate:.2f} s")


if __name__ == "__main__":
    main()
