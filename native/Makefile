# Builds the mjxl command line tool with GCC or Clang on Linux.
# The DirectShow filter and the Media Foundation decoder are Windows only.
#
# Ubuntu packages:
#   sudo apt install build-essential pkg-config libjxl-dev libbrotli-dev \
#       libavformat-dev libavcodec-dev libavutil-dev

CXX ?= g++
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= ../_build/linux
OUTPUT_DIR ?= ../dist/linux

PACKAGES := libjxl libjxl_threads libavformat libavcodec libavutil libbrotlienc libbrotlidec
SOURCES := encoder.cpp cli.cpp core.cpp flac_audio.cpp original_file.cpp
OBJECTS := $(SOURCES:%.cpp=$(BUILD_DIR)/%.o)
TARGET := $(OUTPUT_DIR)/mjxl

CXXFLAGS ?= -O2
ALL_CXXFLAGS := -std=c++20 -Wall -Wextra $(CXXFLAGS) $(shell $(PKG_CONFIG) --cflags $(PACKAGES))
LIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(ALL_CXXFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) $(TARGET)

-include $(OBJECTS:.o=.d)
