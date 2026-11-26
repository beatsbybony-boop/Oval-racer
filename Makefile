# Short Track Racer
#
#   make        build the PS2 ELF (needs the ps2dev toolchain, e.g. the Docker image)
#   make test   build and run the headless physics/AI checks on your PC (gcc only)
#   make clean

EE_BIN  = oval-racer.elf
EE_OBJS = ps2/main.o core/vehicle.o core/track.o core/collide.o core/ai.o core/game.o

GSKIT ?= $(PS2DEV)/gsKit
EE_INCS += -I$(GSKIT)/include -I.
EE_LDFLAGS += -L$(GSKIT)/lib
EE_LIBS += -lgskit -ldmakit -lpad -lpatches -lc -lm

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

ifdef PS2SDK
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
else
# No PS2SDK in the environment: only the host-side test target is available.
.PHONY: all
all:
	@echo "PS2SDK is not set. Run this inside the ps2dev toolchain, or use 'make test'."
endif

.PHONY: test
test:
	$(CC) -O2 -std=c99 -Wall -Wextra core/*.c tests/headless.c -lm -o headless-tests
	./headless-tests
