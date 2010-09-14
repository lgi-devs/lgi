PKG_PACKAGES = gobject-introspection-1.0 lua5.1
OUTPUT = lgi.so

OBJS = core.o marshal.o compound.o callable.o glib.o

CFLAGS = -c -pipe -Wall $(shell pkg-config --cflags $(PKG_PACKAGES))
LDFLAGS = -shared $(shell pkg-config --libs $(PKG_PACKAGES))

ifdef DEBUG
	CFLAGS += -g -O0
else
	CFLAGS += -O2 -DNDEBUG
endif

ifneq ($(shell uname -o), Cygwin)
	CFLAGS += -fPIC
endif

.PHONY : all clean
all : $(OUTPUT)

$(OUTPUT) : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

core.o : core.c lgi.h
callable.o : callable.c lgi.h decltype.h
marshal.o : marshal.c lgi.h decltype.h
compound.o : compound.c lgi.h
glib.o : glib.c lgi.h decltype.h

clean :
	rm -f $(OBJS) $(OUTPUT) *.stackdump *~
