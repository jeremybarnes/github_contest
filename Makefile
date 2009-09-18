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

CXXFLAGS += -Ijml -Wno-deprecated
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



LIBGITHUB_SOURCES := \
	siamese.cc \
	data.cc \
	ranker.cc \
	decompose.cc \
	keywords.cc \
	candidate_source.cc

LIBGITHUB_LINK := \
	utils ACE boost_date_time-mt db arch boosting svdlibc

$(eval $(call library,github,$(LIBGITHUB_SOURCES),$(LIBGITHUB_LINK)))

$(eval $(call add_sources,exception_hook.cc))

$(eval $(call program,github,github utils ACE boost_program_options-mt db arch boosting svdlibc,github.cc exception_hook.cc,tools))

$(eval $(call program,analyze_keywords,github utils ACE boost_program_options-mt db arch boosting svdlibc,analyze_keywords.cc exception_hook.cc,tools))

$(eval $(call include_sub_makes,svdlibc))

$(eval $(call include_sub_makes,jgraph))

include loadbuild.mk
