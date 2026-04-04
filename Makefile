RACK_DIR ?= ../Rack-SDK

SLUG = maybe
VERSION = 2.0.0

FLAGS +=
CFLAGS +=
CXXFLAGS +=

LDFLAGS +=

SOURCES += src/plugin.cpp
SOURCES += src/Skyline.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += plugin.json

include $(RACK_DIR)/plugin.mk
