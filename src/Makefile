PKG_PACKAGES = gobject-introspection-1.0 lua
OUTPUT = lgi.so

CFLAGS = -c -pipe -Wall `pkg-config --cflags $(PKG_PACKAGES)`
LDFLAGS = -shared `pkg-config --libs $(PKG_PACKAGES)`

ifdef DEBUG
	CFLAGS += -g -O0
else
	CFLAGS += -O2 -DNDEBUG
endif

.PHONY : all clean
all : $(OUTPUT)

$(OUTPUT) : lgi.o
	$(CC) -o $@ $< $(LDFLAGS)

clean :
	rm -f lgi.o $(OUTPUT)
