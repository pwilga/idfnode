import socket
import hashlib
import sys
import subprocess
import argparse
import os
import json
import time
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

def find_cmake_preset(project_dir, device_profile):
    """If CMakePresets.json exists, idf.py auto-picks the *first* preset unless one
    is named explicitly (and then forces its IDF_TARGET, breaking cross-target builds).
    Return the preset whose environment matches the requested profile so we
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

    for preset in presets.get('configurePresets', []):
        if preset.get('environment', {}).get('DEVICE_PROFILE') == device_profile:
            return preset.get('name')
    return None


def strip_jsonc_comments(text):
    """VS Code reads settings.json as JSONC: drop // and /* */ comments, keep strings as they are
    (a URL like rfc2217://kropka holds //)"""
    out = []
    i = 0
    in_string = False
    while i < len(text):
        c = text[i]
        if in_string:
            out.append(c)
            if c == '\\' and i + 1 < len(text):
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
            out.append(c)
        elif text.startswith('//', i):
            while i < len(text) and text[i] != '\n':
                i += 1
            continue
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            i = len(text) if end == -1 else end + 2
            continue
        else:
            out.append(c)
        i += 1
    return ''.join(out)


def get_vscode_settings():
    """Read idf.currentSetup and DEVICE_PROFILE from .vscode/settings.json"""
    settings_file = Path(__file__).parent / '.vscode' / 'settings.json'
    if not settings_file.exists():
        return None, None
    try:
        with open(settings_file, 'r') as f:
            settings = json.loads(strip_jsonc_comments(f.read()))
        idf_path = settings.get('idf.espIdfPath') or settings.get('idf.currentSetup')
        extra_vars = settings.get('idf.customExtraVars', {})
        return idf_path, extra_vars.get('DEVICE_PROFILE')
    except Exception as e:
        print(f"Warning: Failed to read .vscode/settings.json: {e}")
        return None, None

if not args.skip_build:
    vscode_idf, vscode_profile = get_vscode_settings()

    idf_path = args.idf_path or vscode_idf or os.environ.get('IDF_PATH')
    if not idf_path:
        print("❌ Cannot determine IDF path. Set idf.currentSetup in .vscode/settings.json or use --idf-path.")
        sys.exit(1)

    device_profile = args.device_profile or vscode_profile
    if not device_profile:
        print("❌ Cannot determine DEVICE_PROFILE. Set idf.customExtraVars.DEVICE_PROFILE in .vscode/settings.json or use --device-profile.")
        sys.exit(1)

    project_dir = Path(__file__).parent
    preset = find_cmake_preset(project_dir, device_profile)

    if preset is None and (project_dir / 'CMakePresets.json').exists():
        print(f"❌ CMakePresets.json exists but has no preset for DEVICE_PROFILE={device_profile}. "
              f"idf.py would fall back to the first preset and force its IDF_TARGET. "
              f"Add a matching preset or remove CMakePresets.json.")
        sys.exit(1)

    if preset:
        idf_profile_args = f"--preset {preset}"
        print(f"Building: preset={preset} (DEVICE_PROFILE={device_profile}, IDF: {idf_path})")
    else:
        idf_profile_args = f"-DDEVICE_PROFILE={device_profile}"
        print(f"Building: DEVICE_PROFILE={device_profile} (IDF: {idf_path})")

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


SEND_CHUNK_SIZE = 16 * 1024
# Small on purpose: whatever sits in the OS send buffer is already "sent" for the bar, but still
# on its way to the device. The default buffer (up to MBs on macOS) would swallow the whole image
# in seconds and show 100% while the device has barely started.
SEND_BUFFER_SIZE = 16 * 1024
BAR_WIDTH = 30


def format_duration(seconds):
    seconds = int(round(seconds))
    return f"{seconds // 60}:{seconds % 60:02d}"


def send_image(s, data):
    """Send the image in chunks and draw a progress bar. The bar follows what the socket accepted;
    with the small send buffer that is within SEND_BUFFER_SIZE of what the device has received.
    The summary after the final ACK is the real time."""
    total = len(data)
    sent = 0
    transfer_start = time.monotonic()
    window_start, window_sent = transfer_start, 0
    speed = 0.0

    while sent < total:
        chunk = data[sent:sent + SEND_CHUNK_SIZE]
        s.sendall(chunk)
        sent += len(chunk)

        now = time.monotonic()
        if now - window_start >= 1.0 or sent == total:
            # Speed over the last second, so a stall shows up instead of being averaged away
            speed = (sent - window_sent) / (now - window_start) / 1024
            window_start, window_sent = now, sent

        elapsed = now - transfer_start
        average = sent / elapsed / 1024 if elapsed else 0
        # The first second is only the OS buffers filling up, so speed and ETA would be nonsense
        measured = elapsed >= 1.0 and speed > 0
        eta = (total - sent) / (average * 1024) if measured and average else 0
        speed_text = f"{speed:5.1f}" if measured else "  ---"
        eta_text = format_duration(eta) if measured else "--:--"
        filled = BAR_WIDTH * sent // total
        bar = "█" * filled + "░" * (BAR_WIDTH - filled)
        print(f"\r  {bar} {100 * sent // total:3d}%  {sent // 1024}/{total // 1024} KB  "
              f"{speed_text} KB/s  ETA {eta_text}   ", end="", flush=True)

    print()
    return transfer_start


def print_summary(elapsed, size):
    print(f"✅ Image delivered: {size / 1024:.0f} KB in {format_duration(elapsed)} "
          f"({elapsed:.1f} s), average {size / elapsed / 1024:.1f} KB/s")


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
    s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, SEND_BUFFER_SIZE)
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

        transfer_start = send_image(s, file_content)
        s.shutdown(socket.SHUT_WR)
        print("  Waiting for the device to confirm it received everything - do not close this window")
        check_ack(s)
        print_summary(time.monotonic() - transfer_start, len(file_content))
