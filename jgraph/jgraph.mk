JGRAPH_SOURCES := \
	github_import.cc \
	jgraph.cc

JGRAPH_LINK :=

$(eval $(call library,jgraph,$(JGRAPH_SOURCES),$(JGRAPH_LINK)))
