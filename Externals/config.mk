#
#
#
-include ../../Makefile.cfg

ifeq ($(ARCH),x86)
 BFD := i586
else ifeq ($(ARCH),x86_64)
 BFD := x86_64
else
 $(error No BFD translation for $(ARCH) in Externals/core.mk)
endif

#PREFIX=$(ACESSDIR)/Externals/Output
#EPREFIX=$(ACESSDIR)/Externals/Output/$(BFD)
PREFIX=$(ACESSDIR)/Externals/Output/$(ARCH)
EPREFIX=$(PREFIX)
SYSROOT=$(ACESSDIR)/Externals/Output/sysroot-$(BFD)
HOST=$(BFD)-acess_proxy-elf
