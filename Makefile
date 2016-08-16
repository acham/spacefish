BIN = bin
BUILD = build
PREPATH = system
LIBPATH = $(shell realpath $(BIN))
CC = gcc
CFLAGS = -Wall -fPIC

#CPPFLAGS += -g
CPPFLAGS += -DUFS_DEBUG=0
#CPPFLAGS += -DUFS_DEBUG_SHOW_LOCATION=1
#CPPFLAGS += -DUFS_DEBUG_SHOW_PID=1
CPPFLAGS += -DWRITE_TO_DISK=0
CPPFLAGS += -DFILE_OPS
CPPFLAGS += -DLIBPATH='"$(LIBPATH)"'
#CPPFLAGS += -DVFORK_SIGNALING
CPPFLAGS += -D_GNU_SOURCE
#CPPFLAGS += -DDAEMON_BACKGROUND
CPPFLAGS += -DALLOC_ALL_RANDOM
#CPPFLAGS += -DALLOC_ALL_ROUGH_INDEX
#CPPFLAGS += -DALLOC_ALL_PER_CORE_ROUGH_IND
#CPPFLAGS += -DUSE_METROHASH

HEADER = -I$(PREPATH)/include
LDFLAGS = -lrt -lpthread -ldl

GLIBCCALL_SRCS = $(wildcard $(PREPATH)/lib/*.c)
GLIBCCALL_OBJS = $(patsubst $(PREPATH)/lib/%.c, $(BUILD)/lib/%.o, \
				 $(GLIBCCALL_SRCS))

DEMO_OBJECTS = $(BUILD)/util/util.o $(BUILD)/util/data_shm.o \
			   $(BUILD)/util/meta_shm.o ${BUILD}/util/rwlock.o

LIB_OBJECTS = $(BUILD)/util/fd.o $(BUILD)/util/process.o \
			  $(BUILD)/lib/ufs.o $(GLIBCCALL_OBJS)

OBJECTS = $(DEMO_OBJECTS) $(LIB_OBJECTS)

UT = $(BUILD)/util/util.o

EXECS = daemon libufs.so

all : build-dir $(EXECS)

build-dir:
	@if [ ! -d $(BIN) ]; then mkdir $(BIN); fi
	@if [ ! -d $(BUILD) ]; then mkdir $(BUILD); fi
	@if [ ! -d $(BUILD)/lib ]; then mkdir $(BUILD)/basic; fi
	@if [ ! -d $(BUILD)/util ]; then mkdir $(BUILD)/util; fi
	@if [ ! -d $(BUILD)/lib ]; then mkdir $(BUILD)/lib; fi

$(BUILD)/%.o : $(PREPATH)/%.c
	@echo "CC	$@"
	@$(CC) $(CFLAGS) $(CPPFLAGS) $(HEADER) -c $< -o $@

daemon : $(PREPATH)/daemon.c $(DEMO_OBJECTS)
	@echo "CC 	daemon"
	@$(CC) $(CFLAGS) $(CPPFLAGS) $(HEADER) $^ -o $(BIN)/$@ $(LDFLAGS)
	@cp scripts/ufs.in $(BIN)/ufs

libufs.so : $(OBJECTS)
	@echo "CC	libufs.so"
	@$(CC) $(CFLAGS) $(CPPFLAGS) $(HEADER) -shared -o $(BIN)/$@ $^ \
			-ldl $(LDFLAGS)

clean:
	rm -rf $(BIN) $(BUILD)
