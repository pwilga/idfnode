import socket
import hashlib
import sys
import subprocess
import argparse
from pathlib import Path

# Parse command line arguments
parser = argparse.ArgumentParser(description='OTA firmware upload to ESP32')
parser.add_argument('address',
                    help='ESP32 IP address or hostname')
parser.add_argument('--port', type=int, default=5555,
                    help='ESP32 port (default: 5555)')
parser.add_argument('--skip-build', action='store_true',
                    help='Skip building the project')
args = parser.parse_args()

if not args.skip_build:
    print("Building project...")
    build_cmd = (
        "bash -c 'source ~/repos/esp-idf/export.sh > /dev/null 2>&1 && idf.py build'"
    )
    result = subprocess.run(build_cmd, shell=True, capture_output=True, text=True)

    print(result.stdout)
    if result.stderr:
        print(result.stderr)

    if result.returncode != 0:
        print("❌ Build failed!")
        sys.exit(1)

    print("✅ Build successful!")
else:
    print("⏭️  Skipping build...")

# ESP32 connection details
ESP_IP = args.address
ESP_PORT = args.port

print(f"📡 Connecting to {ESP_IP}:{ESP_PORT}...")

magic_bytes = bytes([0xAF, 0xCA, 0xEC, 0x2D, 0xFE, 0x55])
expected_ack = bytes([0xAA, 0x55])

sw_version = bytes([0x09, 0x05])


def check_ack(s):
    ack = s.recv(2)

    if ack == expected_ack:
        print("✅ Received 2-byte ACK from ESP32!")
    else:
        print(f"❌ Unexpected response: {ack.hex()}")
        sys.exit(1)


firmware_file = Path("build/idfnode.bin")
firmware_file_size = firmware_file.stat().st_size

print(firmware_file_size)
fw_size = firmware_file_size.to_bytes(4, byteorder="big")

print("Bytes:", fw_size.hex())

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((ESP_IP, ESP_PORT))

    s.sendall(magic_bytes)
    check_ack(s)

    s.sendall(sw_version)
    check_ack(s)

    s.sendall(fw_size)
    check_ack(s)

    # Open,close, read file and calculate MD5 on its contents
    with open(firmware_file, "rb") as firmware_fh:
        file_content = firmware_fh.read()
        m5_hash = hashlib.md5(file_content).digest()

        print(m5_hash)
        s.sendall(m5_hash)
        check_ack(s)

        s.sendall(file_content)
        s.shutdown(socket.SHUT_WR)
        check_ack(s)

    # with open(firmware_file, "rb") as firmware_fh:
    # index = 0
    # while True:
    #     index = index + 1
    #     chunk = firmware_fh.read(256)
    #     if not chunk:
    #         break
    #     s.sendall(chunk)

    # 2. Wyślij plik binarny w paczkach

    # for i in range(0, firmware_file_size, 1024):
    #     chunk = file_content[i : i + 1024]
    #     s.sendall(chunk)
    #     print(i)
    # sleep(5)

    # s.close()

    # print(f"INDEX {i}")
    # check_ack(s)

    # s.sendall(file_content)
