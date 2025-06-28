# Multi-stage build for BeamSim with dedicated NS-3 stage
FROM ubuntu:24.04 AS ns3-builder

# Build arguments
ARG NS3_VERSION=3.44
ARG CLANG_VERSION=19

# Install build dependencies for NS-3
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    wget \
    gnupg \
    software-properties-common \
    ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Add LLVM apt repository for Clang (using variable)
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - \
    && add-apt-repository "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-${CLANG_VERSION} main"

# Install Clang and development tools (using variable)
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-${CLANG_VERSION} \
    libc++-${CLANG_VERSION}-dev \
    libc++abi-${CLANG_VERSION}-dev \
    cmake \
    ninja-build \
    git \
    python3 \
    python3-pip \
    python3-venv \
    tar \
    bzip2 \
    pkg-config \
    libssl-dev \
    zlib1g-dev \
    openmpi-bin \
    openmpi-common \
    libopenmpi-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && rm -rf /tmp/* \
    && rm -rf /var/tmp/*

# Set Clang as default compiler (using variable)
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${CLANG_VERSION} 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${CLANG_VERSION} 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/clang-${CLANG_VERSION} 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-${CLANG_VERSION} 100

# Create non-root build user for security
RUN groupadd -r builder && useradd -r -g builder -m -d /home/builder -s /sbin/nologin builder

# Set working directory and copy NS-3 setup script
WORKDIR /build
RUN chown builder:builder /build

# Switch to non-root user for build
USER builder

# Environment variables for Clang (using variable)
ENV CC=clang-${CLANG_VERSION}
ENV CXX=clang++-${CLANG_VERSION}
ENV CXXFLAGS="-stdlib=libc++"
ENV LDFLAGS="-stdlib=libc++"

# Copy only the NS-3 setup script for this stage
COPY --chown=builder:builder setup_ns3.sh .

# Build NS-3 (this will be cached separately from the main project)
RUN chmod +x setup_ns3.sh && ./setup_ns3.sh

# Main project builder stage
FROM ns3-builder AS beamsim-builder

# Main project builder stage inherits NS-3 from ns3-builder
# Already has Clang and NS-3 built, just need to build BeamSim
# User 'builder' already exists from ns3-builder stage

# Switch back to root to setup build directory
USER root

# Set working directory and change ownership  
WORKDIR /build
RUN chown builder:builder /build

# Switch to non-root user for build
USER builder

# Copy source code with proper ownership (excluding setup_ns3.sh as NS-3 is already built)
COPY --chown=builder:builder . .

# Build the project with NS-3 support using all available cores
RUN cmake -G Ninja -B build \
    -D CMAKE_BUILD_TYPE=RelWithDebInfo \
    -D CMAKE_C_COMPILER=clang-${CLANG_VERSION} \
    -D CMAKE_CXX_COMPILER=clang++-${CLANG_VERSION} \
    -D CMAKE_CXX_FLAGS="${CXXFLAGS}" \
    -D CMAKE_EXE_LINKER_FLAGS="${LDFLAGS}" \
    -D ns3_DIR=external/ns-allinone-${NS3_VERSION}/install/lib/cmake/ns3 \
    && ninja -C build -j$(nproc)

# Runtime stage - minimal distroless-like image
FROM ubuntu:24.04 AS beamsim-runtime

# Re-declare build args for runtime stage
ARG NS3_VERSION=3.44

# Add metadata labels
LABEL maintainer="BeamSim Team" \
      version="0.0.1" \
      description="BeamSim - Beam Chain Networking Simulator" \
      org.opencontainers.image.source="https://github.com/qdrvm/beamsim" \
      org.opencontainers.image.documentation="https://github.com/qdrvm/beamsim/README.md"

# Install only essential runtime dependencies for Clang/libc++
RUN apt-get update && apt-get install -y --no-install-recommends \
    libc++1 \
    ca-certificates \
    libopenmpi3 \
    libopenmpi-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && rm -rf /tmp/* \
    && rm -rf /var/tmp/*

# Create non-root user with specific UID for consistency
RUN groupadd -r beamsim -g 10001 \
    && useradd -r -u 10001 -g beamsim -m -d /home/beamsim -s /sbin/nologin beamsim

# Copy binary from builder stage
COPY --from=beamsim-builder --chown=beamsim:beamsim /build/build/main /usr/local/bin/beamsim

# Copy NS-3 libraries from builder stage
COPY --from=beamsim-builder --chown=root:root /build/external/ns-allinone-${NS3_VERSION}/install/lib/ /usr/local/lib/

# Copy shadow-atlas.bin file from builder stage
COPY --from=beamsim-builder --chown=beamsim:beamsim /build/shadow-atlas.bin /home/beamsim/shadow-atlas.bin

RUN chmod 755 /usr/local/bin/beamsim && ldconfig

# Switch to non-root user
USER beamsim
WORKDIR /home/beamsim

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD /usr/local/bin/beamsim --help > /dev/null || exit 1

# Default command with explicit path
CMD ["/usr/local/bin/beamsim", "--help"]
