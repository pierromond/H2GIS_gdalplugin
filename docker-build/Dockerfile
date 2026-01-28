# Use an older Ubuntu version to ensure GLIBC compatibility (e.g., Ubuntu 20.04 uses GLIBC 2.31)
# This binary will work on Ubuntu 20.04, 22.04, 24.04, etc.
FROM ubuntu:20.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
# We need GCC, CMake, and GDAL development headers
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libgdal-dev \
    git \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /build

# Copy source code (will be mounted from host)
# Default command: build the plugin
# Use a separate build directory to avoid conflicts with host's CMakeCache
CMD mkdir -p build_container && cd build_container && \
    cmake -DPORTABLE_BUILD=ON .. && \
    make -j$(nproc) && \
    cp gdal_H2GIS.so /output/
