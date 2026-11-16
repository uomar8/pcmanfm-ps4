# PCManFM-PS4 OpenOrbis Makefile
TITLE       := PCManFM-PS4
APP_VER     := 01.15
TITLE_ID    := PCMF00100
CONTENT_ID  := IV0000-PCMF00100_00-PCMANFMPS4000000
PKG_NAME    := PCManFM-PS4-v$(APP_VER).pkg

TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
CDIR        := linux
BUILD_DIR   := build

CC          := clang
CXX         := clang++
LD          := ld.lld

COMMON_FLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables \
                -O3 -march=btver2 -mtune=btver2 -D__PS4__ -D__OPENORBIS__ -D__ORBIS__ \
                -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -c \
                -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include \
                -I$(TOOLCHAIN)/usr/include -I$(TOOLCHAIN)/usr/include/SDL2 \
                -Isrc -Ithird_party/imgui

CFLAGS      := $(COMMON_FLAGS) -Werror=vla -Werror=alloca
CXXFLAGS    := $(COMMON_FLAGS) -std=c++17 -nostdinc++ -isystem $(TOOLCHAIN)/include/c++/v1

LIBS        := $(TOOLCHAIN)/usr/lib/libSDL2.a \
               -lc -lkernel -lc++ -lScePosix \
               -lSceVideoOut -lScePad -lSceUserService -lSceSystemService -lSceSysmodule \
               -lSceImeDialog -lSceCommonDialog -lSceMsgDialog -lSceFreeType -lSceAudioOut

LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
               -L$(TOOLCHAIN)/usr/lib -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

APP_CPPFILES := $(wildcard src/*.cpp)
IMGUI_CPPFILES := $(wildcard third_party/imgui/*.cpp)

APP_OBJS    := $(patsubst src/%.cpp,$(BUILD_DIR)/src_%.o,$(APP_CPPFILES))
IMGUI_OBJS  := $(patsubst third_party/imgui/%.cpp,$(BUILD_DIR)/imgui_%.o,$(IMGUI_CPPFILES))
OBJS        := $(APP_OBJS) $(IMGUI_OBJS)

LIBMODULES  := $(wildcard sce_module/*)
ASSETS      := $(wildcard assets/*)

_mkdir := $(shell mkdir -p $(BUILD_DIR))

.PHONY: all clean

all: $(PKG_NAME)

$(PKG_NAME): pkg.gp4
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build $< .
	mv -f $(CONTENT_ID).pkg $(PKG_NAME)

pkg.gp4: eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png $(LIBMODULES) $(ASSETS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"

sce_sys/param.sfo: Makefile
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_new $@
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(APP_VER)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(APP_VER)'

eboot.bin: $(OBJS)
	$(LD) $(OBJS) -o $(BUILD_DIR)/app.elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(BUILD_DIR)/app.elf -out=$(BUILD_DIR)/app.oelf --eboot "eboot.bin" --paid 0x3800000000000011

$(BUILD_DIR)/src_%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -Werror=vla -Werror=alloca -o $@ $<

$(BUILD_DIR)/imgui_%.o: third_party/imgui/%.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR) eboot.bin pkg.gp4 sce_sys/param.sfo $(CONTENT_ID).pkg *.pkg
