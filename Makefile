.PHONY: help test build clean \
       deps deps-firmware deps-webapp deps-pico-sdk \
       firmware-test firmware-test-e2e firmware-build firmware-clean \
       android-test android-build android-clean \
       webapp-build webapp-run webapp-run-sim webapp-clean \
       container-firmware container-android

PICO_SDK_DIR ?= $(HOME)/pico-sdk

help: ## Show available targets
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | sort | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-22s\033[0m %s\n", $$1, $$2}'

# ===== Dependencies =====

deps: deps-firmware deps-webapp ## Install all development dependencies

deps-firmware: ## Install firmware toolchain (macOS: brew cask, Linux: apt)
ifeq ($(shell uname),Darwin)
	brew install cmake
	brew install --cask gcc-arm-embedded
else
	sudo apt-get update && sudo apt-get install -y \
		build-essential cmake gcc-arm-none-eabi libnewlib-arm-none-eabi python3
endif

deps-webapp: ## Install webapp dependencies (Go modules)
	cd webapp && go mod download

deps-pico-sdk: ## Clone Pico SDK to ~/pico-sdk
	@if [ -d "$(PICO_SDK_DIR)" ]; then \
		echo "Pico SDK already exists at $(PICO_SDK_DIR), pulling latest..."; \
		cd "$(PICO_SDK_DIR)" && git pull && git submodule update --init; \
	else \
		git clone https://github.com/raspberrypi/pico-sdk.git "$(PICO_SDK_DIR)" --recurse-submodules; \
	fi
	@echo ""
	@echo "Add to your shell profile:"
	@echo "  export PICO_SDK_PATH=$(PICO_SDK_DIR)"

# ===== Combined =====

test: firmware-test firmware-test-e2e android-test ## Run all tests (firmware + e2e + android)

build: firmware-build android-build webapp-build ## Build all components

clean: firmware-clean android-clean webapp-clean ## Clean all build artifacts

# ===== Firmware =====

firmware-test: ## Run firmware unit tests (host-compiled)
	cd firmware/test && make test

firmware-test-e2e: ## Run end-to-end tests (simulated serial)
	cd tests/e2e && make test

firmware-build: ## Cross-compile firmware to .uf2 (requires PICO_SDK_PATH)
ifndef PICO_SDK_PATH
	$(error PICO_SDK_PATH is not set. Install the Pico SDK and export PICO_SDK_PATH=/path/to/pico-sdk)
endif
	mkdir -p firmware/build
	cd firmware/build && cmake .. -DPICO_BOARD=pico_w && make -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu)

firmware-clean: ## Clean firmware build artifacts
	rm -rf firmware/build
	cd firmware/test && make clean
	cd tests/e2e && make clean

# ===== Android =====

android-test: ## Run Android unit tests
	cd android && ./gradlew test --no-daemon

android-build: ## Build Android debug APK
	cd android && ./gradlew assembleDebug --no-daemon

android-clean: ## Clean Android build artifacts
	cd android && ./gradlew clean --no-daemon

# ===== Webapp =====

webapp-build: ## Build Go webapp binary
	cd webapp && go build -o superdro-web ./cmd/superdro-web/

webapp-run-sim: webapp-build ## Run webapp with simulated serial data
	cd webapp && ./superdro-web -sim

webapp-run: webapp-build ## Run webapp with auto-detected serial port
	cd webapp && ./superdro-web

webapp-clean: ## Clean webapp binary
	rm -f webapp/superdro-web

# ===== Container Build =====

CONTAINER_ENGINE ?= podman

container-firmware: ## Build firmware in container, copy .uf2 to firmware/build/
	$(CONTAINER_ENGINE) build -f Dockerfile.firmware -t superdro-firmware .
	mkdir -p firmware/build
	$(CONTAINER_ENGINE) create --name superdro-fw-tmp superdro-firmware true 2>/dev/null || \
		($(CONTAINER_ENGINE) rm superdro-fw-tmp && $(CONTAINER_ENGINE) create --name superdro-fw-tmp superdro-firmware true)
	$(CONTAINER_ENGINE) cp superdro-fw-tmp:/superdro.uf2 firmware/build/superdro.uf2
	$(CONTAINER_ENGINE) rm superdro-fw-tmp
	@echo ""
	@echo "Firmware built: firmware/build/superdro.uf2"

container-android: ## Build Android app in container
	$(CONTAINER_ENGINE) build -f Dockerfile.android -t superdro-android .
