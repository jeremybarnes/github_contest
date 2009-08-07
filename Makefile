-include local.mk

default: all
.PHONY: default

BUILD   ?= build
ARCH    ?= $(shell uname -m)
OBJ     := $(BUILD)/$(ARCH)/obj
BIN     := $(BUILD)/$(ARCH)/bin
TESTS   := $(BUILD)/$(ARCH)/tests
SRC     := .

JML_TOP := jml

include $(JML_TOP)/arch/$(ARCH).mk

CXXFLAGS += -Ijml
CXXLINKFLAGS += -Ljml/../build/$(ARCH)/bin -Wl,--rpath,jml/../build/$(ARCH)/bin

ifeq ($(MAKECMDGOALS),failed)
include .target.mk
failed:
        +make $(FAILED) $(GOALS)
else

include $(JML_TOP)/functions.mk
include $(JML_TOP)/rules.mk

$(shell echo GOALS := $(MAKECMDGOALS) > .target.mk)
endif



GITHUB_SOURCES := \
	github.cc

LIBBOOSTING_TOOLS_LINK :=       boosting

$(eval $(call add_sources,exception_hook.cc))

$(eval $(call program,github,utils ACE boost_program_options-mt boost_regex-mt db arch,github.cc exception_hook.cc,tools))



