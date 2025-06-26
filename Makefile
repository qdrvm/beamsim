# BeamSim Makefile

# Docker image configuration
DOCKER_REGISTRY ?= qdrvm
IMAGE_NAME ?= beamsim
PLATFORM ?= amd64
NS3_VERSION ?= 3.44
CLANG_VERSION ?= 19

# Get git commit hash (first 7 characters)
GIT_COMMIT := $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo "unknown")
DEFAULT_TAG := $(GIT_COMMIT)-$(PLATFORM)

# Allow override of tag
DOCKER_TAG ?= $(DEFAULT_TAG)
FULL_IMAGE_NAME := $(if $(DOCKER_REGISTRY),$(DOCKER_REGISTRY)/,)$(IMAGE_NAME):$(DOCKER_TAG)

# Build arguments
DOCKER_BUILD_ARGS ?= --build-arg NS3_VERSION=$(NS3_VERSION) --build-arg CLANG_VERSION=$(CLANG_VERSION)
DOCKER_BUILDKIT ?= 1

# Build configuration
BUILD_DATE := $(shell date -u +"%Y-%m-%dT%H:%M:%SZ")
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")

.PHONY: help docker_image docker_push docker_clean info security_scan docker_manifest

help: ## Show this help message
	@echo "BeamSim Docker Build"
	@echo ""
	@echo "Usage:"
	@echo "  make docker_image                      # Build with default tag ($(DEFAULT_TAG))"
	@echo "  make docker_image DOCKER_TAG=latest   # Build with custom tag"
	@echo "  make docker_image PLATFORM=arm64      # Build for different platform"
	@echo "  make docker_image NS3_VERSION=3.43    # Build with different NS-3 version"
	@echo "  make docker_image CLANG_VERSION=18    # Build with different Clang version"
	@echo ""
	@echo "Available targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  %-20s %s\n", $$1, $$2}'

info: ## Show build information
	@echo "Build Information:"
	@echo "  Git commit:     $(GIT_COMMIT)"
	@echo "  Version:        $(VERSION)"
	@echo "  Build date:     $(BUILD_DATE)"
	@echo "  Platform:       $(PLATFORM)"
	@echo "  NS-3 version:   $(NS3_VERSION)"
	@echo "  Clang version:  $(CLANG_VERSION)"
	@echo "  Image name:     $(IMAGE_NAME)"
	@echo "  Docker tag:     $(DOCKER_TAG)"
	@echo "  Full image:     $(FULL_IMAGE_NAME)"
	@echo "  Registry:       $(if $(DOCKER_REGISTRY),$(DOCKER_REGISTRY),<none>)"
	@echo "  BuildKit:       $(DOCKER_BUILDKIT)"

docker_image: ## Build Docker image
	@echo "Building BeamSim Docker image..."
	@echo "Image: $(FULL_IMAGE_NAME)"
	@echo "Platform: $(PLATFORM)"
	@echo ""
	DOCKER_BUILDKIT=$(DOCKER_BUILDKIT) docker build \
		--platform linux/$(PLATFORM) \
		--target beamsim-runtime \
		--label "org.opencontainers.image.created=$(BUILD_DATE)" \
		--label "org.opencontainers.image.version=$(VERSION)" \
		--label "org.opencontainers.image.revision=$(GIT_COMMIT)" \
		$(DOCKER_BUILD_ARGS) \
		-t $(FULL_IMAGE_NAME) \
		.
	@echo ""
	@echo "Build completed successfully!"
	@echo "Image: $(FULL_IMAGE_NAME)"
	@echo ""
	@echo "Usage examples:"
	@echo "  docker run --rm $(FULL_IMAGE_NAME)"
	@echo "  docker run --rm $(FULL_IMAGE_NAME) beamsim --backend delay --topology direct"
	@echo "  docker run --rm $(FULL_IMAGE_NAME) beamsim --backend queue --groups 5 --group-validators 4"

docker_push: ## Push Docker image to registry
	@if [ -z "$(DOCKER_REGISTRY)" ]; then \
		echo "Error: DOCKER_REGISTRY is not set"; \
		echo "Usage: make docker_push DOCKER_REGISTRY=your-registry.com"; \
		exit 1; \
	fi
	@echo "Pushing $(FULL_IMAGE_NAME)..."
	docker push $(FULL_IMAGE_NAME)

security_scan: ## Run security scan on the image
	@echo "Running security scan on $(FULL_IMAGE_NAME)..."
	@if command -v trivy >/dev/null 2>&1; then \
		trivy image $(FULL_IMAGE_NAME); \
	elif command -v docker >/dev/null 2>&1; then \
		docker run --rm -v /var/run/docker.sock:/var/run/docker.sock \
			aquasec/trivy image $(FULL_IMAGE_NAME); \
	else \
		echo "No security scanner found. Install trivy or use Docker to run scan."; \
	fi

docker_clean: ## Remove built Docker images
	@echo "Removing Docker images..."
	-docker rmi $(FULL_IMAGE_NAME)
	@echo "Cleaning up dangling images..."
	-docker image prune -f

# Development targets
docker_shell: ## Run interactive shell in the image
	docker run --rm -it --user root $(FULL_IMAGE_NAME) bash

docker_test: ## Run basic tests
	@echo "Testing Docker image..."
	docker run --rm $(FULL_IMAGE_NAME) /usr/local/bin/beamsim --help
	@echo "Testing health check..."
	docker run --rm $(FULL_IMAGE_NAME) sh -c '/usr/local/bin/beamsim --help > /dev/null && echo "Health check passed"'
	@echo "Basic tests passed!"

# Multi-platform builds (requires buildx)
docker_buildx: ## Build multi-platform image using buildx
	@echo "Building multi-platform image..."
	docker buildx build \
		--platform linux/amd64,linux/arm64 \
		--target beamsim-runtime \
		--label "org.opencontainers.image.created=$(BUILD_DATE)" \
		--label "org.opencontainers.image.version=$(VERSION)" \
		--label "org.opencontainers.image.revision=$(GIT_COMMIT)" \
		$(DOCKER_BUILD_ARGS) \
		-t $(if $(DOCKER_REGISTRY),$(DOCKER_REGISTRY)/,)$(IMAGE_NAME):$(GIT_COMMIT) \
		$(if $(DOCKER_REGISTRY),--push,--load) \
		.

# Size optimization
docker_size: ## Show image size information
	@echo "Image size information:"
	docker images $(FULL_IMAGE_NAME) --format "table {{.Repository}}\t{{.Tag}}\t{{.Size}}"
	@echo ""
	@echo "Layer breakdown:"
	docker history $(FULL_IMAGE_NAME)

# Create and push multi-architecture manifest
docker_manifest: ## Create and push multi-architecture manifest
	@echo "Creating multi-architecture manifest for $(DOCKER_TAG)..."
	docker manifest create --amend \
		$(DOCKER_REGISTRY)/$(IMAGE_NAME):$(DOCKER_TAG) \
		$(DOCKER_REGISTRY)/$(IMAGE_NAME):$(DOCKER_TAG)-amd64 \
		$(DOCKER_REGISTRY)/$(IMAGE_NAME):$(DOCKER_TAG)-arm64
	@echo "Pushing manifest..."
	docker manifest push $(DOCKER_REGISTRY)/$(IMAGE_NAME):$(DOCKER_TAG)
	@echo "Multi-architecture manifest created and pushed successfully!"

# Clean all
clean: docker_clean ## Clean all build artifacts
	@echo "Cleaning build artifacts..."
	-rm -rf build/
	-rm -rf external/
	-docker system prune -f

# Release workflow
release: docker_image security_scan docker_test ## Build, scan, and test image for release
	@echo "Release workflow completed for $(FULL_IMAGE_NAME)"
