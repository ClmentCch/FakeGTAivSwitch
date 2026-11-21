#---------------------------------------------------------------------------------
# Grand Theft Auto VI - Nintendo Switch homebrew video player
#
# Requires devkitPro with: switch-dev, switch-sdl2 and switch-ffmpeg
#   export DEVKITPRO=/opt/devkitpro
#   make
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

export DEVKITPRO

DEVKITA64   ?= $(DEVKITPRO)/devkitA64
PORTLIBS    ?= $(DEVKITPRO)/portlibs/switch
PREFIX      := $(DEVKITA64)/bin/aarch64-none-elf-
CC          := $(PREFIX)gcc
NACPTOOL    := $(DEVKITPRO)/tools/bin/nacptool
ELF2NRO     := $(DEVKITPRO)/tools/bin/elf2nro

TARGET      := gtaVI
BUILD       := build
SRC         := source
ICON        := icon.jpg
NACP        := $(TARGET).nacp

APP_TITLE   := Grand Theft Auto VI
APP_AUTHOR  := ClmentCch
APP_VERSION := 1.0.1

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ftls-model=local-exec

CFLAGS      := -g -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu17 \
               -ffunction-sections -fdata-sections $(ARCH) \
               -D__SWITCH__ \
               -I$(DEVKITPRO)/libnx/include -I$(PORTLIBS)/include

LDFLAGS     := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
               -L$(DEVKITPRO)/libnx/lib -L$(PORTLIBS)/lib

LIBS        := -lSDL2 -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lstdc++ \
               -lavformat -lavcodec -lswresample -lswscale -lavutil -ldav1d \
               -lz -lbz2 -lm -lpthread -lnx

SOURCES     := $(wildcard $(SRC)/*.c)
OBJS        := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(SOURCES))
DEPS        := $(OBJS:.o=.d)

.PHONY: all clean dist

all: $(TARGET).nro

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	@echo "  CC   $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD):
	@mkdir -p $@

$(TARGET).elf: $(OBJS)
	@echo "  LD   $@"
	@$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(NACP):
	@echo "  NACP $@"
	@$(NACPTOOL) --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(NACP) $(ICON)
	@echo "  NRO  $@"
	@$(ELF2NRO) $(TARGET).elf $@ --icon=$(ICON) --nacp=$(NACP)

dist: all
	@rm -rf dist && mkdir -p dist/switch/$(TARGET)
	@cp $(TARGET).nro dist/switch/$(TARGET)/
	@cp packaging/README.txt dist/switch/$(TARGET)/
	@cd dist && zip -9 -q -r $(TARGET)-$(APP_VERSION).zip switch
	@echo "  ZIP  dist/$(TARGET)-$(APP_VERSION).zip (switch/$(TARGET)/)"

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp $(TARGET).map dist

-include $(DEPS)
