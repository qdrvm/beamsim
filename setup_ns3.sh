#!/bin/bash
set -e  # Exit on error

echo "Setting up NS-3 for the Gossip Protocol Simulation"
echo "===================================================="

# Create directory for external dependencies
EXTERNAL="external"
mkdir -p "$EXTERNAL"

# Set NS-3 version
NS3_VERSION="3.44"
NS3_NAME="ns-allinone-$NS3_VERSION"
NS3_ROOT="$EXTERNAL/$NS3_NAME"
NS3_INSTALL="$NS3_ROOT/install"
NS3_ARCHIVE_NAME="$NS3_NAME.tar.bz2"
NS3_ARCHIVE_PATH="$EXTERNAL/$NS3_ARCHIVE_NAME"
NS3_URL="https://www.nsnam.org/releases/$NS3_ARCHIVE_NAME"
NS3_CLI="$NS3_ROOT/ns-$NS3_VERSION/ns3"

# Build NS-3 if not already built
if [ ! -d "$NS3_INSTALL" ]; then
    # Extract NS-3 if not already extracted
    if [ ! -d "$NS3_ROOT" ]; then
        # Download NS-3 if not already downloaded
        if [ ! -f "$NS3_ARCHIVE_PATH" ]; then
            echo "Downloading NS-3 version $NS3_VERSION..."
            ARCHIVE_TMP="$NS3_ARCHIVE_PATH.tmp"
            if which curl; then
                curl -L -C - -o "$ARCHIVE_TMP" "$NS3_URL"
            elif which wget; then
                wget -O "$ARCHIVE_TMP" "$NS3_URL"
            else
                which curl wget
            fi
            mv "$ARCHIVE_TMP" "$NS3_ARCHIVE_PATH"
        else
            echo "NS-3 archive already downloaded."
        fi

        echo "Extracting NS-3..."
        tar -C "$EXTERNAL" -xf "$NS3_ARCHIVE_PATH"
    else
        echo "NS-3 already extracted."
    fi

    echo "Building NS-3 (this may take a while)..."
    $NS3_CLI configure -G Ninja "--prefix=$NS3_INSTALL" --enable-mpi
    $NS3_CLI build
    $NS3_CLI install
    echo "NS-3 built successfully!"
else
    echo "NS-3 already built."
fi

echo ""
echo "NS-3 setup complete!"
echo "You can now build the project with:"
echo "  cmake -G Ninja -B build -D CMAKE_BUILD_TYPE=RelWithDebInfo -D ns3_DIR=$NS3_INSTALL/lib/cmake/ns3"
echo "  ninja -C build"
