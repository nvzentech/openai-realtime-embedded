import os
import re
import wave
import struct

# Paths
INPUT_LOG = r"C:\Users\Thomas\Desktop\esp_log.txt"
DESKTOP_DIR = r"C:\Users\Thomas\Desktop"

FULL_WAV = os.path.join(DESKTOP_DIR, "openai_full.wav")
FULL_BIN = os.path.join(DESKTOP_DIR, "openai_full.bin")
SPEECH_WAV = os.path.join(DESKTOP_DIR, "openai_speech_only.wav")
SPEECH_BIN = os.path.join(DESKTOP_DIR, "openai_speech_only.bin")

# Audio format from media.cpp
SAMPLE_RATE = 8000
CHANNELS = 2          # Stereo (Left + Right)
SAMPLE_WIDTH = 2      # 16-bit PCM (2 bytes per sample per channel = 4 bytes/frame)
BYTES_PER_FRAME = CHANNELS * SAMPLE_WIDTH

# Voice threshold (amplitude above background noise)
SILENCE_THRESHOLD = 150

def extract_and_segregate():
    if not os.path.exists(INPUT_LOG):
        print(f"Error: Log file not found at: {INPUT_LOG}")
        return

    # Auto-detect encoding (PowerShell 5.1 writes UTF-16 LE)
    with open(INPUT_LOG, 'rb') as f:
        bom = f.read(2)
    encoding = 'utf-16' if bom in (b'\xff\xfe', b'\xfe\xff') else 'utf-8'
    print(f"Reading '{INPUT_LOG}' using encoding: {encoding}...")

    raw_bytes = bytearray()
    in_dump_block = False

    with open(INPUT_LOG, 'r', encoding=encoding, errors='ignore') as f:
        for line in f:
            # Strip ANSI terminal colors
            line = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', line)

            # Start of dump block
            if "ALL AUDIO CAPTURED" in line:
                in_dump_block = True
                raw_bytes.clear()
                continue

            # End of dump block
            if "FINISHED PRINTING AUDIO" in line:
                in_dump_block = False
                break

            # Parse hex lines from realtimeapi-sdk
            if in_dump_block and "realtimeapi-sdk:" in line:
                parts = line.split("realtimeapi-sdk:", 1)
                tokens = parts[1].strip().split()
                if tokens and all(len(t) == 2 and all(c in '0123456789abcdefABCDEF' for c in t) for t in tokens):
                    raw_bytes.extend(bytes.fromhex("".join(tokens)))

    if not raw_bytes:
        print("Error: No audio dump found in log file.")
        return

    total_frames = len(raw_bytes) // BYTES_PER_FRAME
    total_duration = total_frames / SAMPLE_RATE

    print("\n" + "=" * 55)
    print("EXTRACTION SUMMARY")
    print("=" * 55)
    print(f" - Total bytes parsed: {len(raw_bytes)} bytes ({len(raw_bytes) / 1024:.2f} KB)")
    print(f" - Full session duration: {total_duration:.2f} seconds")

    # 1. Save full session files (.bin and .wav)
    with open(FULL_BIN, 'wb') as f:
        f.write(raw_bytes)
    with wave.open(FULL_WAV, 'wb') as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(SAMPLE_WIDTH)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(raw_bytes)

    # 2. Segregate Voice Speech from Silence
    speech_start_frame = None
    speech_end_frame = None

    for i in range(total_frames):
        val_l, val_r = struct.unpack_from('<hh', raw_bytes, i * BYTES_PER_FRAME)
        if abs(val_l) > SILENCE_THRESHOLD or abs(val_r) > SILENCE_THRESHOLD:
            if speech_start_frame is None:
                speech_start_frame = max(0, i - int(SAMPLE_RATE * 0.1))  # 100ms head padding
            speech_end_frame = min(total_frames, i + int(SAMPLE_RATE * 0.1))  # 100ms tail padding

    if speech_start_frame is not None and speech_end_frame is not None:
        speech_bytes = raw_bytes[speech_start_frame * BYTES_PER_FRAME : speech_end_frame * BYTES_PER_FRAME]
        speech_duration = len(speech_bytes) / (SAMPLE_RATE * BYTES_PER_FRAME)
        start_time = speech_start_frame / SAMPLE_RATE
        end_time = speech_end_frame / SAMPLE_RATE

        print(f"\nSPEECH SEGREGATION:")
        print(f" - Speech starts at: {start_time:.2f} s")
        print(f" - Speech ends at:   {end_time:.2f} s")
        print(f" - Pure voice speech duration: {speech_duration:.2f} s ({len(speech_bytes)} bytes)")

        # Save segregated speech-only files (.bin and .wav)
        with open(SPEECH_BIN, 'wb') as f:
            f.write(speech_bytes)
        with wave.open(SPEECH_WAV, 'wb') as wav:
            wav.setnchannels(CHANNELS)
            wav.setsampwidth(SAMPLE_WIDTH)
            wav.setframerate(SAMPLE_RATE)
            wav.writeframes(speech_bytes)

        print("\n" + "=" * 55)
        print("FILES CREATED ON DESKTOP:")
        print("=" * 55)
        print(f" [1] PURE VOICE SPEECH (Trimmed):")
        print(f"     - Playable WAV: {SPEECH_WAV}")
        print(f"     - Binary data:  {SPEECH_BIN}")
        print(f" [2] FULL SESSION (With silence):")
        print(f"     - Playable WAV: {FULL_WAV}")
        print(f"     - Binary data:  {FULL_BIN}")
    else:
        print("\nNo voice speech above threshold detected (entire buffer was silent).")

if __name__ == "__main__":
    extract_and_segregate()
