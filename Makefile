QJS = third_party/quickjs
QJS_SRC = $(QJS)/quickjs.c $(QJS)/libregexp.c $(QJS)/libunicode.c $(QJS)/dtoa.c
CFLAGS ?= -O2 -pipe
CXXFLAGS ?= $(CFLAGS)
QJS_CFLAGS = $(CFLAGS) -D_GNU_SOURCE -w
INC = -I$(QJS) -Ithird_party/cjson -Isrc
CFLAGS_ALL = $(CFLAGS) -Wall $(INC)
CXXFLAGS_ALL = $(CXXFLAGS) -Wall -std=c++11 $(INC) $(shell fltk-config --cxxflags)
LIBS = -lcurl -lm -lpthread -ldl -latomic
FLTK_LIBS = $(shell fltk-config --use-images --ldflags)
CORE = build/engine.o build/hash.o build/ws.o build/stream.o build/download.o build/cjson.o build/qjs.a
APP_OBJ = build/app.o build/app_watch.o build/app_music.o build/app_settings.o build/app_download.o build/ui.o build/views.o build/app_store.o \
	build/backend.o build/parse.o build/store.o build/player.o build/cookies.o build/chatsend.o

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/tinytv

$(shell mkdir -p build)

all: build/tinytv build/tinytv-cli build/sp-test build/dl-test build/hackstream_proxy

build/qjs.a: $(QJS_SRC)
	mkdir -p build/qjs
	cd build/qjs && for f in $(QJS_SRC); do $(CC) $(QJS_CFLAGS) -c ../../$$f || exit 1; done
	ar rcs $@ build/qjs/*.o

build/engine.o: src/engine.c src/engine.h src/hash.h src/ws.h
	$(CC) $(CFLAGS_ALL) -c src/engine.c -o $@

build/ws.o: src/ws.c src/ws.h
	$(CC) $(CFLAGS_ALL) -c src/ws.c -o $@

build/hash.o: src/hash.c src/hash.h
	$(CC) $(CFLAGS_ALL) -c src/hash.c -o $@

build/stream.o: src/stream.c src/stream.h
	$(CC) $(CFLAGS_ALL) -c src/stream.c -o $@

build/download.o: src/download.c src/download.h
	$(CC) $(CFLAGS_ALL) -c src/download.c -o $@

build/cjson.o: third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -w -c $< -o $@

build/%.o: src/%.cpp src/*.h
	$(CXX) $(CXXFLAGS_ALL) -c $< -o $@

build/tinytv: $(APP_OBJ) $(CORE)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(FLTK_LIBS) $(LIBS)

build/tinytv-cli: tools/sanson_cli.c build/engine.o build/hash.o build/ws.o build/cjson.o build/qjs.a
	$(CC) $(CFLAGS_ALL) $^ -o $@ $(LIBS)

build/sp-test: tools/sp_test.c build/stream.o
	$(CC) $(CFLAGS_ALL) $^ -o $@ $(LIBS)

build/dl-test: tools/dl_test.c build/download.o
	$(CC) $(CFLAGS_ALL) $^ -o $@ $(LIBS)

install: all
	install -Dm755 build/tinytv $(DESTDIR)$(BINDIR)/tinytv
	install -Dm755 build/tinytv-cli $(DESTDIR)$(BINDIR)/tinytv-cli
	mkdir -p $(DESTDIR)$(DATADIR)
	cp -r plugins js $(DESTDIR)$(DATADIR)/

clean:
	rm -rf build
build/hackstream_proxy: src/hackstream_proxy.c
	@mkdir -p build
	$(CC) -O2 -pipe -Wall -Wextra -Wno-unused-result -std=c11 $< -o $@ -lcurl -pthread
