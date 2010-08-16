PKG_PACKAGES = gobject-introspection-1.0 lua5.1
OUTPUT = lgi.so

OBJS = lgi.o callable.o

CFLAGS = -c -pipe -Wall -fPIC `pkg-config --cflags $(PKG_PACKAGES)`
LDFLAGS = -shared `pkg-config --libs $(PKG_PACKAGES)`

ifdef DEBUG
	CFLAGS += -g -O0
else
	CFLAGS += -O2 -DNDEBUG
endif

.PHONY : all clean
all : $(OUTPUT)

$(OUTPUT) : $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

lgi.o : lgi.c lgi.h decltype.h
callable.o : callable.c lgi.h decltype.h

clean :
	rm -f $(OBJS) $(OUTPUT)
