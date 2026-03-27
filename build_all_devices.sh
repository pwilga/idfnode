#!/bin/bash
# Build and flash all device profiles

set +e  # Don't exit on error, handle errors manually

# Update repository
echo "========================================"
echo "Updating repository..."
echo "========================================"
git pull || { echo "✗ git pull failed - stopping"; exit 1; }

# Device profiles array: "profile_name:target:port"
DEVICES=(
    "atom_mesh:esp32s3:/dev/ttyAtom"
    "c3_cikonesp_mesh:esp32c3:/dev/ttyESP32c3"
    "cikonesp:esp32:/dev/ttyESP32"
    # "cikonesp_mesh:esp32:/dev/ttyUSB3"
)

# Main build loop
for device_entry in "${DEVICES[@]}"; do
    IFS=':' read -r device target port <<< "$device_entry"
    
    echo ""
    echo "========================================"
    echo "Building: ${device} (${target}) -> ${port}"
    echo "========================================"
    
    # 1. Full clean
    echo "Step 1/4: Full clean"
    sh fullclean.sh || { echo "✗ Full clean failed for ${device}"; continue; }
    
    # 2. Set target
    echo "Step 2/4: Set target to ${target}"
    idf.py -DDEVICE_PROFILE="${device}" set-target "${target}" || { echo "✗ Set target failed for ${device}"; continue; }
    
    # 3. Build
    echo "Step 3/4: Build"
    idf.py -DDEVICE_PROFILE="${device}" build || { echo "✗ Build failed for ${device}"; continue; }
    
    # 4. Flash (optional - uncomment to enable)
    echo "Step 4/4: Flash to ${port}"
    idf.py -p "${port}" flash || { echo "✗ Flash failed for ${device}"; continue; }
    
    echo "✓ ${device} build complete (port: ${port})"
done

echo ""
echo "========================================"
echo "All devices built successfully!"
echo "========================================"
