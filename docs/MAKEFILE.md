# Makefile Command Reference

This document provides a comprehensive reference for all Makefile targets and variables available in the BeamSim project.

## Variables

### Build Configuration

| Variable | Default | Description | Example |
|----------|---------|-------------|---------|
| `DOCKER_REGISTRY` | `qdrvm/beamsim` | Docker registry prefix | `DOCKER_REGISTRY=ghcr.io/myorg` |
| `IMAGE_NAME` | `beamsim` | Docker image name | `IMAGE_NAME=beamsim-dev` |
| `PLATFORM` | `amd64` | Target platform | `PLATFORM=arm64` |
| `NS3_VERSION` | `3.44` | NS-3 simulator version | `NS3_VERSION=3.43` |
| `CLANG_VERSION` | `19` | Clang compiler version | `CLANG_VERSION=18` |

### Docker Configuration

| Variable | Default | Description | Example |
|----------|---------|-------------|---------|
| `DOCKER_TAG` | `$(GIT_COMMIT)-$(PLATFORM)` | Image tag | `DOCKER_TAG=latest` |
| `DOCKER_BUILDKIT` | `1` | Enable BuildKit | `DOCKER_BUILDKIT=0` |
| `DOCKER_BUILD_ARGS` | Auto-generated | Additional build args | Custom build arguments |

### Computed Variables

| Variable | Description | Example Value |
|----------|-------------|---------------|
| `GIT_COMMIT` | Git commit hash (7 chars) | `a1b2c3d` |
| `DEFAULT_TAG` | Default image tag | `a1b2c3d-amd64` |
| `FULL_IMAGE_NAME` | Complete image name | `qdrvm/beamsim/beamsim:a1b2c3d-amd64` |
| `BUILD_DATE` | ISO 8601 build timestamp | `2025-06-24T12:00:00Z` |
| `VERSION` | Git describe or "dev" | `v1.0.0` or `dev` |

## Primary Targets

### `docker_image`
Builds the BeamSim Docker image with all dependencies.

```bash
make docker_image
make docker_image CLANG_VERSION=18
make docker_image PLATFORM=arm64 NS3_VERSION=3.43
```

**Process:**
1. Builds NS-3 simulator in dedicated stage
2. Compiles BeamSim with Clang
3. Creates minimal runtime image
4. Adds metadata and health checks

### `docker_test`
Runs basic functionality tests on the built image.

```bash
make docker_test
```

**Tests performed:**
- Help command execution
- Health check validation
- Basic command-line parsing

### `docker_push`
Pushes the built image to the configured registry.

```bash
make docker_push
make docker_push DOCKER_REGISTRY=ghcr.io/myorg
```

**Requirements:**
- `DOCKER_REGISTRY` must be set
- Docker login credentials configured
- Image must be built first

## Development Targets

### `docker_shell`
Opens an interactive shell in the built image.

```bash
make docker_shell
```

**Use cases:**
- Debugging runtime issues
- Exploring the container environment
- Manual testing

### `docker_size`
Shows detailed size information about the built image.

```bash
make docker_size
```

**Output includes:**
- Total image size
- Layer-by-layer breakdown
- Size optimization opportunities

### `docker_clean`
Removes the built image and cleans up dangling images.

```bash
make docker_clean
```

**Actions:**
- Removes tagged image
- Prunes dangling images
- Frees up disk space

## Advanced Targets

### `docker_buildx`
Builds multi-platform images using Docker Buildx.

```bash
make docker_buildx
make docker_buildx DOCKER_REGISTRY=myregistry.com
```

**Features:**
- Builds for linux/amd64 and linux/arm64
- Automatically pushes if registry is set
- Requires buildx setup

### `security_scan`
Performs security scanning on the built image (if Trivy is installed).

```bash
make security_scan
```

**Scans for:**
- Known vulnerabilities
- Misconfigurations
- Security best practices

### `release`
Complete release workflow: build, scan, and test.

```bash
make release
```

**Process:**
1. `docker_image` - Build the image
2. `security_scan` - Security validation
3. `docker_test` - Functionality tests

## Utility Targets

### `clean`
Comprehensive cleanup of all build artifacts.

```bash
make clean
```

**Removes:**
- Local build directories
- External dependencies
- Docker images and dangling containers
- Build cache (if enabled)

### `info`
Displays current build configuration and computed values.

```bash
make info
```

**Shows:**
- All variable values
- Computed image names
- Build metadata
- Git information

### `help`
Shows available targets with descriptions and usage examples.

```bash
make help
make  # Same as help
```

## Usage Patterns

### Development Workflow

```bash
# Initial setup
make docker_image

# Iterative development
make docker_image && make docker_test

# Full validation
make release

# Cleanup
make clean
```

### CI/CD Integration

```bash
# Build and test
make docker_image DOCKER_TAG=${CI_COMMIT_SHA}
make docker_test

# Security and release
make security_scan
make docker_push
```

### Multi-Platform Development

```bash
# Test different architectures
make docker_image PLATFORM=amd64
make docker_image PLATFORM=arm64

# Multi-platform release
make docker_buildx
```

### Experimentation

```bash
# Try different compilers
make docker_image CLANG_VERSION=18
make docker_image CLANG_VERSION=20

# Test different NS-3 versions
make docker_image NS3_VERSION=3.43
make docker_image NS3_VERSION=3.45

# Custom configurations
make docker_image DOCKER_TAG=experimental-$(date +%s)
```

## Error Handling

### Common Issues and Solutions

**Build fails with "unknown flag":**
```bash
# Disable BuildKit for debugging
make docker_image DOCKER_BUILDKIT=0
```

**Registry push fails:**
```bash
# Check registry configuration
make info
# Ensure DOCKER_REGISTRY is set
make docker_push DOCKER_REGISTRY=your-registry.com
```

**Multi-platform build fails:**
```bash
# Ensure buildx is set up
docker buildx create --use
make docker_buildx
```

**Out of disk space:**
```bash
# Clean up aggressively
make clean
docker system prune -af
```

## Integration Examples

### GitHub Actions

```yaml
name: Build and Test
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: make docker_image DOCKER_TAG=${{ github.sha }}
      - name: Test
        run: make docker_test
      - name: Security Scan
        run: make security_scan
```

### Docker Compose

```yaml
version: '3.8'
services:
  beamsim:
    image: qdrvm/beamsim/beamsim:latest
    build:
      context: .
      target: beamsim-runtime
      args:
        - NS3_VERSION=3.44
        - CLANG_VERSION=19
    command: ["beamsim", "--backend", "delay", "--topology", "direct"]
```

## Best Practices

1. **Use specific tags** for production deployments
2. **Test multi-platform builds** before releasing
3. **Run security scans** in CI/CD pipelines
4. **Clean up regularly** to save disk space
5. **Use the info target** to verify configuration
6. **Pin dependency versions** for reproducible builds
