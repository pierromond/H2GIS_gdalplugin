# Use Ubuntu 22.04 to match the target machine's environment (GDAL 3.4.1, GLIBC 2.35)
# This ensures binary compatibility with the colleague's setup.
FROM ubuntu:22.04

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
