JGRAPH_SOURCES := \
	jgraph.cc \
	basic_graph.cc \
	attribute.cc

JGRAPH_LINK :=

$(eval $(call library,jgraph,$(JGRAPH_SOURCES),$(JGRAPH_LINK)))


$(eval $(call program,jgraph_test,jgraph utils boost_program_options-mt,github_import.cc ../exception_hook.cc,tools))
