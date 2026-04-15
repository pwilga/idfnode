#!/bin/bash
# Build and flash all device profiles

set +e  # Don't exit on error, handle errors manually

# Device profiles array: "device_name:target:variant:port"
# variant can be: inet, mesh, espnow, etc.
# Usage: -DDEVICE_PROFILE=cikonesp -DDEVICE_PROFILE_VARIANT=inet
DEVICES=(
    "atom:esp32s3:mesh:/dev/ttyAtom"
    "c3_cikonesp:esp32c3:mesh:/dev/ttyESP32c3"
    "cikonesp:esp32:mesh:/dev/ttyESP32"
)

# Main build loop
for device_entry in "${DEVICES[@]}"; do
    IFS=':' read -r device target variant port <<< "$device_entry"
    
    echo ""
    echo "========================================"
    echo "Building: ${device} (${target}, variant: ${variant}) -> ${port}"
    echo "========================================"
    
    # 1. Full clean
    echo "Step 1/4: Full clean"
    sh fullclean.sh || { echo "✗ Full clean failed for ${device}"; continue; }
    
    # 2. Set target
    echo "Step 2/4: Set target to ${target}"
    idf.py -DDEVICE_PROFILE="${device}" -DDEVICE_PROFILE_VARIANT="${variant}" set-target "${target}" || { echo "✗ Set target failed for ${device}"; continue; }
    
    # 3. Build
    echo "Step 3/4: Build"
    idf.py -DDEVICE_PROFILE="${device}" -DDEVICE_PROFILE_VARIANT="${variant}" build || { echo "✗ Build failed for ${device}"; continue; }
    
    # 4. Flash (optional - uncomment to enable)
    echo "Step 4/4: Flash to ${port}"
    idf.py -p "${port}" flash || { echo "✗ Flash failed for ${device}"; continue; }
    
    echo "✓ ${device} (${variant}) build complete (port: ${port})"
done

echo ""
echo "========================================"
echo "All devices built successfully!"
echo "========================================"
