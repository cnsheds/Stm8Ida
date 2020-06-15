PROC=stm8
CONFIGS=stm8.cfg
CFLAGS=-I../../module -fpermissive
IDAPATH ?= /opt/idapro7
include ../module.mak

# MAKEDEP dependency list ------------------
$(F)stm8$(O)   : $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp $(I)fpro.h  \
                  $(I)funcs.hpp $(I)ida.hpp $(I)idp.hpp $(I)kernwin.hpp     \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)netnode.hpp $(I)pro.h $(I)range.hpp $(I)segment.hpp   \
                  $(I)../module/idaidp.hpp \
                  $(I)ua.hpp $(I)xref.hpp ins.cpp ana.cpp out.cpp reg.cpp emu.cpp

install:
	cp ../../bin/cfg/stm8.cfg $(IDAPATH)/cfg/
	cp ../../bin/procs/stm8.so $(IDAPATH)/procs/
