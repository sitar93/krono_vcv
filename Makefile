RACK_DIR ?= ../Rack-SDK

# Match Rack plugin.mk Windows defaults; also pin libgcc into the DLL (fewer CRT edge cases).
EXTRA_LDFLAGS += -static-libgcc

SOURCES += src/plugin.cpp
SOURCES += src/Krono.cpp
SOURCES += src/krono_hw_engine.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += LICENSE

include $(RACK_DIR)/plugin.mk
