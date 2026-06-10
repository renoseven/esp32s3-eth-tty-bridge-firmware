.PHONY: all help assets build flash upload boards clean

# --- Config ---

SCRIPTS_DIR := scripts
PROFILE ?= release

PORT ?=
VERBOSE ?=
NO_GIT_HASH ?=
FORCE_ASSETS ?=

BUILD_FLAGS :=
UPLOAD_FLAGS := -u

ifneq ($(PROFILE),release)
BUILD_FLAGS += -m $(PROFILE)
endif
ifeq ($(FORCE_ASSETS),1)
BUILD_FLAGS += --force-assets
endif
ifeq ($(VERBOSE),1)
BUILD_FLAGS += -v
endif
ifeq ($(NO_GIT_HASH),1)
BUILD_FLAGS += --no-git-hash
endif
ifneq ($(PORT),)
UPLOAD_FLAGS += -p $(PORT)
endif

# --- Default ---

all: build

# --- Assets ---

assets:
	@cd $(SCRIPTS_DIR) && ./embed_assets.py

# --- Compile ---

build:
	@cd $(SCRIPTS_DIR) && ./build.py $(BUILD_FLAGS)

# --- Upload ---

flash upload:
	@cd $(SCRIPTS_DIR) && ./build.py $(UPLOAD_FLAGS) $(BUILD_FLAGS)

# --- Utilities ---

boards:
	@cd $(SCRIPTS_DIR) && ./build.py --board-list

clean:
	@cd $(SCRIPTS_DIR) && ./build.py --clean -m $(PROFILE)

# --- Help ---

help:
	@echo "SerialBridge Firmware"
	@echo ""
	@echo "targets:"
	@echo "  compile:"
	@echo "    make build          compile firmware (default)"
	@echo "    make assets         force regenerate src/assets.cpp"
	@echo "  upload:"
	@echo "    make flash          compile and upload (auto-detect port)"
	@echo "    make upload         alias for flash"
	@echo "  utilities:"
	@echo "    make boards         list boards and serial ports"
	@echo "    make clean          remove arduino-cli build cache"
	@echo ""
	@echo "variables:"
	@echo "  compile:"
	@echo "    PROFILE=release     debug | debug-full | release | release-full"
	@echo "    FORCE_ASSETS=1      regenerate src/assets.cpp before compile"
	@echo "    VERBOSE=1           verbose arduino-cli output"
	@echo "    NO_GIT_HASH=1       skip FW_VERSION_ID injection"
	@echo "  upload:"
	@echo "    PORT=/dev/ttyACM0   upload port (optional if only one device)"
	@echo ""
	@echo "examples:"
	@echo "  make build"
	@echo "  make build PROFILE=debug"
	@echo "  make flash PORT=/dev/ttyACM0"
	@echo "  make boards"
	@echo "  make clean"
