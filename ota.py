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

def get_activation_script(idf_path):
    """Prefer the EIM-managed activation script for this IDF checkout (if EIM
    installed it), falling back to the standard IDF export.sh otherwise."""
    eim_idf_json = Path.home() / '.espressif' / 'tools' / 'eim_idf.json'
    if eim_idf_json.exists():
        try:
            with open(eim_idf_json, 'r') as f:
                data = json.load(f)
            idf_path_resolved = str(Path(idf_path).resolve())
            for entry in data.get('idfInstalled', []):
                if str(Path(entry['path']).resolve()) == idf_path_resolved:
                    return entry['activationScript']
        except Exception:
            pass
    return f"{idf_path}/export.sh"

def find_cmake_preset(project_dir, device_profile, device_variant):
    """If CMakePresets.json exists, idf.py auto-picks the *first* preset unless one
    is named explicitly (and then forces its IDF_TARGET, breaking cross-target builds).
    Return the preset whose environment matches the requested profile/variant so we
    can pass it via --preset, or None to fall back to plain -DDEVICE_PROFILE."""
    presets_file = Path(project_dir) / 'CMakePresets.json'
    if not presets_file.exists():
        return None
    try:
        with open(presets_file, 'r') as f:
            presets = json.load(f)
    except Exception as e:
        print(f"Warning: Failed to read CMakePresets.json: {e}")
        return None

    want_variant = device_variant or None
    for preset in presets.get('configurePresets', []):
        env = preset.get('environment', {})
        if env.get('DEVICE_PROFILE') != device_profile:
            continue
        if (env.get('DEVICE_PROFILE_VARIANT') or None) != want_variant:
            continue
        return preset.get('name')
    return None


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

    project_dir = Path(__file__).parent
    preset = find_cmake_preset(project_dir, device_profile, device_variant)

    if preset is None and (project_dir / 'CMakePresets.json').exists():
        print(f"❌ CMakePresets.json exists but has no preset for DEVICE_PROFILE={device_profile} "
              f"DEVICE_PROFILE_VARIANT={device_variant or '(none)'}. idf.py would fall back to the "
              f"first preset and force its IDF_TARGET. Add a matching preset or remove CMakePresets.json.")
        sys.exit(1)

    if preset:
        idf_profile_args = f"--preset {preset}"
        print(f"Building: preset={preset} (DEVICE_PROFILE={device_profile} "
              f"DEVICE_PROFILE_VARIANT={device_variant or '(none)'}, IDF: {idf_path})")
    else:
        variant_define = f" -DDEVICE_PROFILE_VARIANT={device_variant}" if device_variant else ""
        idf_profile_args = f"-DDEVICE_PROFILE={device_profile}{variant_define}"
        print(f"Building: DEVICE_PROFILE={device_profile} "
              f"DEVICE_PROFILE_VARIANT={device_variant or '(none)'} (IDF: {idf_path})")

    app_desc_obj = project_dir / "build" / "esp-idf" / "esp_app_format" / "CMakeFiles" / "__idf_esp_app_format.dir" / "esp_app_desc.c.obj"
    app_desc_obj.unlink(missing_ok=True)

    activation_script = get_activation_script(idf_path)
    build_cmd = (
        f"bash -c 'source {activation_script} > /dev/null && "
        f"idf.py {idf_profile_args} build'"
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
