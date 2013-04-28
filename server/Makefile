include ../Makefile.inc
include ../backends/build_config.mk

CFLAGS += -iquote ..
#LDFLAGS +=  -lhdfs -ljvm `pkg-config fuse --libs` -Djava.library.path=$(HADOOP_HOME)/lib/native/Linux-amd64-64/
#LDFLAGS +=  -lhdfs -ljvm `pkg-config fuse --libs` -Djava.library.path=/users/aditm/hadoop-1.0.4/lib/native/Linux-amd64-64/
LDFLAGS +=  -lhdfs -ljvm `pkg-config fuse --libs`
LIBS += ../backends/$(LEVELDBLIBRARY) /usr/lib/libssl.so.0.9.8 ../common.a

OBJS += ../backends/local_fs.o ../backends/rpc_fs.o ../backends/metadb_fs.o
#OBJS += ../backends/*.o

TARGETS = ../giga_server

all: $(TARGETS)

$(OBJS) : $(HDRS)

../giga_server : $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS) $(LDFLAGS)

clean :
	rm -f $(TARGETS) $(OBJS)
