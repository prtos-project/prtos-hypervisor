# Native FreeRTOS on QEMU RISC-V 64 virt
#
# Usage:
#   make run    # Build and run on QEMU
#   make clean  # Clean all build artifacts
#   Ctrl-A X    # Exit QEMU

# GCC flags
CFLAG = -c
OFLAG = -o
INCLUDEFLAG = -I
CROSS = riscv64-linux-gnu
CC = $(CROSS)-gcc
AS = $(CROSS)-as
LD = $(CROSS)-ld
OBJDUMP = $(CROSS)-objdump
CFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany -ffreestanding -Wall -Wextra -g -DGUEST
ASM_FLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany -g

# Compiler/target path in FreeRTOS/Source/portable
PORT_COMP_TARG = GCC/RISC_V_64_BIT/

# Intermediate directory for all *.o and other files:
OBJDIR = obj/

# FreeRTOS source base directory
FREERTOS_SRC = ./FreeRTOS/Source/

# Directory with memory management source files
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/

# Directory with platform specific source files
FREERTOS_PORT_SRC = $(FREERTOS_SRC)portable/$(PORT_COMP_TARG)

# Directory with Application source files
APP_SRC = ./FreeRTOS/Demo/RISC_V_64-bit/

# Object files:

FREERTOS_OBJS = queue.o list.o tasks.o
FREERTOS_OBJS += timers.o

FREERTOS_MEMMANG_OBJS = heap_1.o

FREERTOS_PORT_OBJS = port.o portASM.o

APP_OBJS = main.o start.o tick_config.o uart.o
APP_OBJS += printf-stdarg.o example.o
APP_OBJS += nostdlib.o

# All object files specified above are prefixed the intermediate directory
OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) $(FREERTOS_PORT_OBJS) $(APP_OBJS) )

ELF_IMAGE = image.elf

# Include paths to be passed to $(CC) where necessary
INC_FREERTOS = $(FREERTOS_SRC)include/
INC_APP = $(APP_SRC)include/

# Complete include flags to be passed to $(CC) where necessary
INC_FLAGS = $(INCLUDEFLAG)$(INC_FREERTOS) $(INCLUDEFLAG)$(INC_APP) $(INCLUDEFLAG)$(FREERTOS_PORT_SRC)


all: $(OBJDIR) $(ELF_IMAGE)

$(OBJDIR) :
	mkdir -p $@

LIBGCC = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

$(ELF_IMAGE): $(APP_SRC)linker.ld $(OBJS)
	$(LD) -T $(APP_SRC)linker.ld $(OBJS) $(LIBGCC) -o $@

# FreeRTOS core

$(OBJDIR)%.o : $(FREERTOS_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# Rules for all MemMang implementations are provided

$(OBJDIR)%.o : $(FREERTOS_MEMMANG_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# HW specific part, in FreeRTOS/Source/portable/$(PORT_COMP_TARGET)

$(OBJDIR)%.o : $(FREERTOS_PORT_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

$(OBJDIR)%.o : $(FREERTOS_PORT_SRC)%.S
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# Demo application

$(OBJDIR)%.o : $(APP_SRC)%.S
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

$(OBJDIR)%.o : $(APP_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

run:
	$(MAKE) all
	qemu-system-riscv64 -machine virt -cpu rv64 -nographic -bios default -kernel $(ELF_IMAGE)

clean:
	rm -f *.o *.elf *.list
	rm -rf $(OBJDIR)

.PHONY: run clean
