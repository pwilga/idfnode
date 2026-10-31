#!/bin/bash
# Build and flash all device profiles

set +e  # Don't exit on error, handle errors manually

# Device profiles array: "device_name:target:port"
# Usage: -DDEVICE_PROFILE=cikonesp
DEVICES=(
    "atom:esp32s3:/dev/ttyAtom"
    "h2dev:esp32h2:/dev/ttyH2"
    "c6dev:esp32c6:/dev/ttyC6"
    "p4dev:esp32p4:/dev/ttyP4"
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
