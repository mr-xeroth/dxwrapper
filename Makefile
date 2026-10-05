CXX = g++
CC = gcc

CXXFLAGS = -std=c++17 -O2 -g -Wall -Iinclude -I/mingw32/include -I.
CFLAGS = -O2 -g -Wall -Iinclude -I/mingw32/include -I.

LDFLAGS = -shared -g -static -static-libgcc -static-libstdc++ -L/mingw32/lib -Wl,--enable-stdcall-fixup
LIBS = src/ddraw.def -lglfw3 -lwinpthread -lopengl32 -lgdi32 -ldxguid -ldinput8 -lole32 -luser32 -lkernel32

TARGET = ddraw.dll

OBJS = \
	glad.o \
	src/config.o \
	src/shader.o \
	src/renderer.o \
	src/mouse_hook.o \
	src/ddraw.o \
	src/ddraw_surface.o \
	src/ddraw_palette.o \
	src/ddraw_clipper.o \
	src/ddraw_gamma.o \
	src/dllmain.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
