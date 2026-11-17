ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Install devkitPro and its GBA development tools.")
endif
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set. Install devkitARM.")
endif

include $(DEVKITARM)/gba_rules

TARGET   := opticraft_2d_gba
BUILD    := build
SOURCES  := source
INCLUDES := include
LIBS     := -lgba
LIBDIRS  := $(LIBGBA)

CFLAGS   := -g -Wall -Wextra -O2 -ffunction-sections -fdata-sections
CFLAGS   += $(INCLUDE) -D__GBA__
