#!/bin/bash

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
SOURCE_DIR="$(dirname "$SCRIPT_DIR")"

echo "Building Docker image for legacy compatibility..."
docker build -t h2gis-driver-builder "$SCRIPT_DIR"

echo "Compiling gdal_H2GIS.so in container..."
# Run container using the current user ID to avoid permission issues on the output file
docker run --rm \
    -v "$SOURCE_DIR":/build \
    -v "$SOURCE_DIR/build-legacy":/output \
    -u $(id -u):$(id -g) \
    h2gis-driver-builder

echo "Done! The compatible driver is in ../build-legacy/gdal_H2GIS.so"
