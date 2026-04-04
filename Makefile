# maybe VCV Rack Plugin

RACK_DIR ?= ../Rack-SDK

FLAGS += -Isrc

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk
