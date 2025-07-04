# BeamSim Build Guide

This guide covers building BeamSim using Docker and the included Makefile system.

## Prerequisites

- Docker with BuildKit support
- Git
- Make (optional, but recommended)

## Quick Start

```bash
# Clone the repository
git clone https://github.com/qdrvm/beamsim.git
cd beamsim

# Build Docker image
make docker_image

# Test the build
make docker_test

# Run BeamSim
docker run --rm qdrvm/beamsim/beamsim:$(git rev-parse --short=7 HEAD)-amd64
```

## Build Configuration

The build system uses the following configurable variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `PLATFORM` | `amd64` | Target architecture (amd64, arm64) |
| `NS3_VERSION` | `3.44` | NS-3 network simulator version |
| `CLANG_VERSION` | `19` | Clang compiler version |
| `DOCKER_TAG` | `$(GIT_COMMIT)-$(PLATFORM)` | Docker image tag |
| `DOCKER_REGISTRY` | `qdrvm` | Docker registry prefix |

## Build Process

BeamSim uses a multi-stage Docker build:

1. **NS-3 Builder Stage**: Downloads and compiles NS-3 network simulator
2. **BeamSim Builder Stage**: Compiles the main BeamSim application using Clang
3. **Runtime Stage**: Creates minimal production image with required libraries

### Compiler Configuration

- **Primary Compiler**: Clang (configurable version, default 19)
- **C++ Standard**: C++23
- **Standard Library**: libc++ (LLVM's implementation)
- **Build Type**: RelWithDebInfo (optimized with debug info)

### Dependencies

**Build Dependencies:**
- Clang/LLVM toolchain
- CMake and Ninja
- NS-3 network simulator
- OpenMPI
- Various development libraries

**Runtime Dependencies:**
- libc++ and libc++abi
- OpenMPI runtime
- NS-3 libraries
- Standard system libraries

## Customizing the Build

### Using Different Clang Version

```bash
# Build with Clang 18
make docker_image CLANG_VERSION=18

# Build with Clang 20 (if available)
make docker_image CLANG_VERSION=20
```

### Building for Different Architecture

```bash
# Build for ARM64
make docker_image PLATFORM=arm64

# Build for both architectures (requires buildx)
make docker_buildx
```

### Using Different NS-3 Version

```bash
# Build with NS-3 version 3.43
make docker_image NS3_VERSION=3.43
```

### Custom Docker Tag

```bash
# Build with custom tag
make docker_image DOCKER_TAG=latest

# Build with version tag
make docker_image DOCKER_TAG=v1.0.0
```

## Build Optimization

The build system is optimized for:

- **Layer Caching**: NS-3 is built in a separate stage for better cache utilization
- **Parallel Compilation**: Uses all available CPU cores (`ninja -j$(nproc)`)
- **Minimal Runtime**: Production image contains only essential dependencies
- **Security**: Runs as non-root user in production

### Build Time Expectations

- **First Build**: ~5-15 minutes (downloads and compiles NS-3)
- **Incremental Builds**: ~2-5 minutes (NS-3 cached, only BeamSim rebuilt)
- **Code-only Changes**: ~30-90 seconds (both NS-3 and dependencies cached)

### Quick Reference

**Single platform build (works everywhere):**
```bash
make docker_image
make docker_image PLATFORM=arm64
```

**Multi-platform build (requires setup):**
```bash
# One-time setup
docker buildx create --name beamsim-builder --use
docker buildx inspect --bootstrap

# Build and push (uses default registry: qdrvm)
make docker_buildx

# Or use custom registry
make docker_buildx DOCKER_REGISTRY=your-custom-registry.com
```

## Troubleshooting

### Common Issues

1. **Missing BuildKit**: Ensure Docker BuildKit is enabled
   ```bash
   export DOCKER_BUILDKIT=1
   ```

2. **Platform Issues**: For ARM64 builds on x86_64, ensure qemu is installed
   ```bash
   docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
   ```

3. **Multi-platform Build Issues**: The `docker_buildx` target requires proper buildx setup
   ```bash
   # Create and use a buildx builder
   docker buildx create --name beamsim-builder --use
   
   # Alternatively, enable containerd image store (Docker Desktop)
   # Settings > Features in development > Use containerd for pulling and storing images
   ```

4. **Memory Issues**: NS-3 compilation requires significant memory (4GB+ recommended)

5. **Network Issues**: Ensure access to apt.llvm.org and ns-3 download servers

### Debug Build Issues

```bash
# Build with verbose output
make docker_image DOCKER_BUILDKIT=0

# Check build logs
docker build --no-cache --progress=plain .

# Inspect intermediate stages
docker build --target ns3-builder -t debug-ns3 .
docker run -it debug-ns3 bash
```

## Advanced Usage

### Multi-Platform Builds

Multi-platform builds require Docker Buildx setup:

```bash
# Method 1: Create a dedicated buildx builder (recommended)
docker buildx create --name beamsim-builder --use
docker buildx inspect --bootstrap

# Method 2: Enable containerd image store (Docker Desktop)
# Go to Settings > Features in development > Use containerd for pulling and storing images

# Verify buildx is working
docker buildx ls
```

Once buildx is configured:

```bash
# Build for multiple platforms simultaneously
make docker_buildx

# Push to custom registry (if needed)
make docker_buildx DOCKER_REGISTRY=your-registry.com/beamsim
```

### Manual Multi-Architecture Workflow

For more control over the build process, you can build each architecture separately and create a manifest:

```bash
# Build for each architecture
make docker_image PLATFORM=amd64 DOCKER_TAG=v1.0.0-amd64
make docker_push DOCKER_TAG=v1.0.0-amd64

make docker_image PLATFORM=arm64 DOCKER_TAG=v1.0.0-arm64
make docker_push DOCKER_TAG=v1.0.0-arm64

# Create multi-architecture manifest
make docker_manifest DOCKER_TAG=v1.0.0 DOCKER_REGISTRY=your-registry.com
```

**Note**: Multi-platform builds automatically push to the registry (default: `qdrvm`). Make sure you're logged in:
```bash
docker login  # For Docker Hub (default)
# or
docker login your-registry.com  # For custom registry
```

### Development Workflow

```bash
# Build and test in one command
make release

# Quick development iteration
make docker_image && make docker_test

# Check image size
make docker_size
```

### Integration with CI/CD

The Makefile is designed for CI/CD integration:

```yaml
# GitHub Actions example
- name: Build BeamSim
  run: |
    make docker_image DOCKER_TAG=${{ github.sha }}
    make docker_test
    make docker_push
```

## Next Steps

- See [MAKEFILE.md](MAKEFILE.md) for detailed Makefile command reference
- See [DEPLOYMENT.md](DEPLOYMENT.md) for deployment guidelines
- See main [README.md](../README.md) for usage examples
