import socket
import hashlib
import sys
import subprocess
import argparse
import os
import json
from pathlib import Path

# Parse command line arguments
parser = argparse.ArgumentParser(description='OTA firmware upload to ESP32')
parser.add_argument('address',
                    help='ESP32 IP address or hostname')
parser.add_argument('--port', type=int, default=5555,
                    help='ESP32 port (default: 5555)')
parser.add_argument('--skip-build', action='store_true',
                    help='Skip building the project')
parser.add_argument('--idf-path', type=str,
                    help='Path to ESP-IDF (default: auto-detect from .vscode/settings.json)')
parser.add_argument('--device-profile', type=str,
                    help='DEVICE_PROFILE (default: read from .vscode/settings.json idf.customExtraVars)')
parser.add_argument('--device-profile-variant', type=str,
                    help='DEVICE_PROFILE_VARIANT (default: read from .vscode/settings.json idf.customExtraVars)')
args = parser.parse_args()

def get_vscode_settings():
    """Read idf.currentSetup, DEVICE_PROFILE and DEVICE_PROFILE_VARIANT from .vscode/settings.json"""
    settings_file = Path(__file__).parent / '.vscode' / 'settings.json'
    if not settings_file.exists():
        return None, None, None
    try:
        with open(settings_file, 'r') as f:
            settings = json.load(f)
        idf_path = settings.get('idf.espIdfPath') or settings.get('idf.currentSetup')
        extra_vars = settings.get('idf.customExtraVars', {})
        return idf_path, extra_vars.get('DEVICE_PROFILE'), extra_vars.get('DEVICE_PROFILE_VARIANT')
    except Exception as e:
        print(f"Warning: Failed to read .vscode/settings.json: {e}")
        return None, None, None

if not args.skip_build:
    vscode_idf, vscode_profile, vscode_variant = get_vscode_settings()

    idf_path = args.idf_path or vscode_idf or os.environ.get('IDF_PATH')
    if not idf_path:
        print("❌ Cannot determine IDF path. Set idf.currentSetup in .vscode/settings.json or use --idf-path.")
        sys.exit(1)

    device_profile = args.device_profile or vscode_profile
    if not device_profile:
        print("❌ Cannot determine DEVICE_PROFILE. Set idf.customExtraVars.DEVICE_PROFILE in .vscode/settings.json or use --device-profile.")
        sys.exit(1)

    device_variant = args.device_profile_variant or vscode_variant
    if not device_variant:
        print("❌ Cannot determine DEVICE_PROFILE_VARIANT. Set idf.customExtraVars.DEVICE_PROFILE_VARIANT in .vscode/settings.json or use --device-profile-variant.")
        sys.exit(1)

    print(f"Building: DEVICE_PROFILE={device_profile} DEVICE_PROFILE_VARIANT={device_variant} (IDF: {idf_path})")

    project_dir = Path(__file__).parent
    app_desc_obj = project_dir / "build" / "esp-idf" / "esp_app_format" / "CMakeFiles" / "__idf_esp_app_format.dir" / "esp_app_desc.c.obj"
    app_desc_obj.unlink(missing_ok=True)

    build_cmd = (
        f"bash -c 'source {idf_path}/export.sh > /dev/null 2>&1 && "
        f"idf.py -DDEVICE_PROFILE={device_profile} -DDEVICE_PROFILE_VARIANT={device_variant} build'"
    )
    result = subprocess.run(build_cmd, shell=True, cwd=project_dir)

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

addrinfos = socket.getaddrinfo(ESP_IP, ESP_PORT, socket.AF_UNSPEC, socket.SOCK_STREAM)
if not addrinfos:
    print(f"❌ Could not resolve {ESP_IP}")
    sys.exit(1)
af, socktype, proto, _, sockaddr = addrinfos[0]

with socket.socket(af, socktype, proto) as s:
    s.connect(sockaddr)

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
