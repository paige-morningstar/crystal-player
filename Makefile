#---------------------------------------------------------------------------------
# Crystal Player - MP3 player for the Nintendo 3DS
#
# Single-pass build (no recursive make) so the project also builds from paths
# containing spaces. Usage:
#     export DEVKITPRO=/opt/devkitpro
#     make
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

ifeq ($(strip $(DEVKITARM)),)
export DEVKITARM := $(DEVKITPRO)/devkitARM
endif

include $(DEVKITARM)/3ds_rules

TARGET		:=	MusicPlayer3DS
BUILD		:=	build
SOURCES		:=	source
INCLUDES	:=	source vendor

APP_TITLE	:=	Crystal Player
APP_DESCRIPTION	:=	MP3 player with album art and M3U8 playlists
APP_AUTHOR	:=	devkitPro homebrew

ARCH		:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS		:=	-g -Wall -O2 -mword-relocations \
				-ffunction-sections -fdata-sections \
				$(ARCH) \
				$(foreach dir,$(INCLUDES),-I$(dir)) \
				-I$(CTRULIB)/include \
				-D__3DS__

LDFLAGS		:=	-specs=3dsx.specs -g $(ARCH)
LIBS		:=	-L$(CTRULIB)/lib -lcitro2d -lcitro3d -lctru -lm

CFILES		:=	$(wildcard $(SOURCES)/*.c)
OFILES		:=	$(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))
DEPS		:=	$(OFILES:.o=.d)

ICON		:=	resources/icon.png
BANNER		:=	resources/banner.bnr
STRIP		:=	$(DEVKITARM)/bin/arm-none-eabi-strip
MAKEROM		:=	$(if $(wildcard tools/makerom),tools/makerom,makerom)
CIA_RSF		:=	resources/crystal.rsf

HOSTCC		?=	cc
PYTHON		?=	python3

.PHONY: all clean cia test

all: $(TARGET).3dsx

# Build the installable title. Requires makerom on PATH, or dropped in tools/.
cia: $(TARGET).cia

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	@echo $(notdir $<)
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(TARGET).elf: $(OFILES)
	@echo linking ... $(notdir $@)
	@$(CC) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

$(TARGET).smdh: $(ICON)
	@echo smdh ... $(notdir $@)
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" "$(ICON)" $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	@3dsxtool $< $@ --smdh=$(TARGET).smdh
	@echo built $@

$(TARGET).cia: $(TARGET).elf $(TARGET).smdh $(CIA_RSF) $(BANNER)
	@echo packaging ... $(notdir $@)
	@$(STRIP) -s $(TARGET).elf -o $(BUILD)/cia.elf
	@$(MAKEROM) -f cia -o $@ -rsf $(CIA_RSF) -target t -exefslogo \
		-elf $(BUILD)/cia.elf -icon $(TARGET).smdh -banner $(BANNER)
	@echo built $@

# Host-side tests: the grouping, playlist and tag helpers are plain C, so they
# run on the build machine instead of the console.
test:
	@mkdir -p $(BUILD)
	@for t in albums playlists queue; do \
		$(HOSTCC) -std=c99 -O1 -Wall -I source tests/test_$$t.c \
			source/library.c source/id3.c source/mp3info.c source/m3u8.c \
			source/util.c -o $(BUILD)/test_$$t || exit 1; \
		$(BUILD)/test_$$t || exit 1; \
	done
	@$(PYTHON) tests/test_converter.py

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).cia

-include $(DEPS)
