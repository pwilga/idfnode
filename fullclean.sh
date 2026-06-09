#!/bin/bash
# Full clean: removes build artifacts, managed components, config, and lockfile

set -e

# Remove build directory
if [ -d "build" ]; then
    echo "  ✓ Removing build/"
    rm -rf build
fi

# Remove managed components
if [ -d "managed_components" ]; then
    echo "  ✓ Removing managed_components/"
    rm -rf managed_components
fi

# Remove sdkconfig and sdkconfig.old
if [ -f "sdkconfig" ]; then
    echo "  ✓ Removing sdkconfig"
    rm -f sdkconfig
fi

if [ -f "sdkconfig.old" ]; then
    echo "  ✓ Removing sdkconfig.old"
    rm -f sdkconfig.old
fi

# Remove dependencies lockfile
if [ -f "dependencies.lock" ]; then
    echo "  ✓ Removing dependencies.lock"
    rm -f dependencies.lock
fi

echo "✓ Full clean complete"
