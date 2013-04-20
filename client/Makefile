include ../Makefile.inc
include ../backends/build_config.mk

CFLAGS  += -iquote ..
LDFLAGS +=  `pkg-config fuse --libs`
LIBS =../backends/$(LEVELDBLIBRARY) ../common.a

OBJS += ../backends/local_fs.o ../backends/rpc_fs.o ../backends/metadb_fs.o
#OBJS += ../backends/*.o

#TARGETS = ../giga_client 
TARGETS = ../libgigatablefs.a

all: $(TARGETS)

$(OBJS) : $(HDRS)

#../giga_client : $(OBJS)
#	$(CC) -o $@ $^ $(LIBS) $(LDFLAGS)

../libgigatablefs.a : $(OBJS)
	ar rv ../libgigatablefs.a $?
	ranlib ../libgigatablefs.a

clean :
	rm -f $(TARGETS) $(OBJS)
