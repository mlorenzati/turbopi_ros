#!/usr/bin/env python3
"""
sniff_serial.py – dump raw bytes from /dev/rrc to identify the STM32 packet format.

Run on the Pi (with ros2_control_node NOT running, so /dev/rrc is free):
    python3 sniff_serial.py

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
    print("Will dump ALL bytes received (hex) and look for 0xAA 0x55 framing\n")

    buf = bytearray()
    start = time.time()
    total_bytes = 0
    packet_count = 0

    while time.time() - start < DURATION:
        chunk = port.read(256)
        if chunk:
            buf.extend(chunk)
            total_bytes += len(chunk)

    port.close()

    print(f"\nTotal bytes received: {total_bytes}")

    if total_bytes == 0:
        print("\n*** NO DATA RECEIVED ***")
        print("Possible causes:")
        print("  1. /dev/rrc is not connected to the STM32 board")
        print("  2. STM32 board is not powered or not running firmware")
        print("  3. Wrong device – try /dev/ttyACM0 or /dev/ttyUSB1")
        print("  4. Wrong baud rate – STM32 might use a different rate")
        print("\nChecking what serial devices are available:")
        import subprocess
        result = subprocess.run(["ls", "-la", "/dev/rrc", "/dev/ttyACM0", "/dev/ttyUSB0",
                                  "/dev/ttyUSB1", "/dev/serial/by-id/"],
                                 capture_output=True, text=True)
        print(result.stdout)
        print(result.stderr)
        return

    # Dump first 128 bytes as hex
    print(f"\nFirst {min(128, len(buf))} bytes (hex):")
    chunk = buf[:128]
    for i in range(0, len(chunk), 16):
        row = chunk[i:i+16]
        hex_str = " ".join(f"{b:02X}" for b in row)
        asc_str = "".join(chr(b) if 32 <= b < 127 else '.' for b in row)
        print(f"  {i:04d}: {hex_str:<48}  {asc_str}")

    # scan for 0xAA 0x55 frames
    print(f"\nScanning for 0xAA 0x55 frames...")
    i = 0
    while i < len(buf) - 1:
        if buf[i] == 0xAA and buf[i+1] == 0x55:
            end = min(i + 16, len(buf))
            frame_bytes = buf[i:end]
            hex_str = " ".join(f"{b:02X}" for b in frame_bytes)
            print(f"  [offset {i:4d}] {hex_str}")
            packet_count += 1
            i += 2
        else:
            i += 1

    print(f"\nFound {packet_count} frames with 0xAA 0x55 header")

    if packet_count > 0:
        print("\nPacket format analysis (3rd byte after AA 55):")
        print("  If 3rd byte < 12 → likely FUNCTION (0=SYS, 3=MOTOR, 4=PWM_SERVO, ...)")
        print("  If 3rd byte is a length value → LENGTH comes before FUNCTION")

if __name__ == "__main__":
    main()
