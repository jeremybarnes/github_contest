SVDLIBC_SOURCES := \
	svdlib.c \
	svdutil.c \
	las2.c

SVDLIBC_LINK :=	m

$(eval $(call library,svdlibc,$(SVDLIBC_SOURCES),$(SVDLIBC_LINK)))
