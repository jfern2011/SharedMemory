# Assign the compiler that we want to use:
CC=g++

#----------------------------------------------------------------------
# Include directories:
#----------------------------------------------------------------------
IDIRS = . ../../abort/abort ../../util/util ../../types/types

# Object file directory:
ODIR=../obj

# Assign compiler options:
CFLAGS=-c -g -Wall -Wno-unused-function \
        	$(foreach dir, $(IDIRS), -I$(dir)) --std=c++11

LD_FLAGS=-lrt

#----------------------------------------------------------------------
# Header dependencies:
#----------------------------------------------------------------------
_DEPS = SharedMemory.h abort.h util.h types.h

DEPS = \
	$(join $(addsuffix /, $(IDIRS)), $(_DEPS))

#----------------------------------------------------------------------
# Generate object files
#----------------------------------------------------------------------

$(ODIR)/remote_memory.o: SharedMemory_ut.cpp $(DEPS)
	@make make_odir
	@ $(CC) -c -o $@ $< $(CFLAGS)

$(ODIR)/memory_client.o: MemoryClient_ut.cpp $(DEPS)
	@make make_odir
	@ $(CC) -c -o $@ $< $(CFLAGS)

remote_memory: $(ODIR)/remote_memory.o
	$(CC) -g -o $@ $^ $(LD_FLAGS)

memory_client: $(ODIR)/memory_client.o
	$(CC) -g -o $@ $^ $(LD_FLAGS)

# Build unit tests
all: remote_memory memory_client
	@ echo Done.

make_odir:
	@ if ! [ -d $(ODIR) ]; then mkdir $(ODIR); fi

clean:
	@ rm -f  $(ODIR)/*.o  remote_memory  memory_client

# This target is always out-of-date
.PHONY: clean++

clean++:
	@ rm -rf $(ODIR) *~ core $(IDIR)/*~ remote_memory \
		memory_client
	@ echo clean++: all clean!
