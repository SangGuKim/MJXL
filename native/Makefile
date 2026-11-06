# Compatibility entry point for the shared CMake build on Linux.
# Requires CMake 3.28+, system libjxl/Brotli/libFLAC/FFmpeg development packages,
# and pinned Apple ALAC sources in ../_deps/alac (see README.md).
CXX ?= g++
CMAKE ?= cmake
JOBS ?= 4
BUILD_DIR ?= ../_build/linux
OUTPUT_DIR ?= ../dist/linux
CXXFLAGS ?= -O2

.PHONY: all clean
all:
	$(CMAKE) -S .. -B $(BUILD_DIR) -DMJXL_USE_BUNDLED_DEPS=OFF -DMJXL_BUILD_WINDOWS_ADAPTERS=OFF -DMJXL_BUILD_CLI=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_CXX_FLAGS="$(CXXFLAGS)" -DMJXL_RUNTIME_DIRECTORY="$(abspath $(OUTPUT_DIR))"
	$(CMAKE) --build $(BUILD_DIR) --target mjxl --parallel $(JOBS)
clean:
	@if test -f $(BUILD_DIR)/CMakeCache.txt; then $(CMAKE) --build $(BUILD_DIR) --target clean; fi
