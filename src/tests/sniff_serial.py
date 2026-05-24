#!/usr/bin/env python3
"""
sniff_serial.py – dump raw bytes from /dev/rrc to identify the STM32 packet format.

Run on the Pi (with ros2_control_node NOT running, so /dev/rrc is free):
    sudo python3 sniff_serial.py

Press Ctrl-C to stop after a few seconds.
"""
import serial
import sys
import time

DEVICE = "/dev/rrc"
BAUDRATE = 1000000
DURATION = 10  # seconds to listen

def main():
    try:
        port = serial.Serial(DEVICE, BAUDRATE, timeout=1)
        port.rts = False
        port.dtr = False
    except Exception as e:
        print(f"Cannot open {DEVICE}: {e}")
        sys.exit(1)

    print(f"Listening on {DEVICE} @ {BAUDRATE} baud for {DURATION}s ...")
    print("Looking for 0xAA 0x55 framing ...\n")

    buf = bytearray()
    start = time.time()
    packet_count = 0

    while time.time() - start < DURATION:
        chunk = port.read(256)
        if chunk:
            buf.extend(chunk)

        # scan for 0xAA 0x55 frames
        i = 0
        while i < len(buf) - 1:
            if buf[i] == 0xAA and buf[i+1] == 0x55:
                # Print up to 16 bytes of context
                end = min(i + 16, len(buf))
                frame_bytes = buf[i:end]
                hex_str = " ".join(f"{b:02X}" for b in frame_bytes)
                print(f"[offset {i:4d}] {hex_str}")
                packet_count += 1
                i += 2
            else:
                i += 1

        # Keep only the last 32 bytes in case a frame spans chunks
        if len(buf) > 512:
            buf = buf[-32:]

    port.close()
    print(f"\nFound {packet_count} frames in {DURATION}s")
    print("\nExpected formats:")
    print("  Pi4 (I2C – no serial frames expected)")
    print("  Pi5: AA 55 <LEN> <FUNC> <data...> <CRC>  ← LENGTH before FUNCTION")
    print("       or: AA 55 <FUNC> <LEN> <data...> <CRC>  ← FUNCTION before LENGTH")

if __name__ == "__main__":
    main()
