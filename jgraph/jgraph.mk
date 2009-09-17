JGRAPH_SOURCES := \
	jgraph.cc \
	attribute.cc \
	attribute_traits.cc \
	attribute_basic_types.cc \
	string_map.cc \
	../exception_hook.cc

#	basic_graph.cc \

JGRAPH_LINK :=

$(eval $(call library,jgraph,$(JGRAPH_SOURCES),$(JGRAPH_LINK)))


$(eval $(call program,jgraph_test,jgraph utils boost_program_options-mt,github_import.cc ../exception_hook.cc,tools))


$(eval $(call include_sub_make,jgraph_testing,testing))
