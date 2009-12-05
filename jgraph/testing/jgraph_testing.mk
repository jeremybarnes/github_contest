$(eval $(call test,attribute_test,jgraph arch,boost))
$(eval $(call test,basic_graph_test,jgraph arch,boost))
$(eval $(call test,basic_graph_boost_test,jgraph arch,boost))
$(eval $(call test,object_test,jgraph arch boost_thread-mt,boost))
