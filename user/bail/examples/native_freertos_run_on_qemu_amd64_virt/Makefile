# native FreeRTOS on QEMU x86_64
#	make run
#	Ctrl-A X (Exit)

# GCC flags
CFLAG = -c
OFLAG = -o
INCLUDEFLAG = -I

CC = gcc
AS = gcc
LD = ld
OBJDUMP = objdump
OBJCOPY = objcopy

CFLAGS = -m64 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone \
         -Wall -Wextra -g -DGUEST -mcmodel=kernel -fno-pie
ASM_FLAGS = -m64 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone \
            -g -mcmodel=kernel -fno-pie

# Compiler/target path in FreeRTOS/Source/portable
PORT_COMP_TARG = GCC/X86_64_BIT/

# Intermediate directory for all *.o and other files:
OBJDIR = obj/

# FreeRTOS source base directory
FREERTOS_SRC = ./FreeRTOS/Source/

# Directory with memory management source files
FREERTOS_MEMMANG_SRC = $(FREERTOS_SRC)portable/MemMang/

# Directory with platform specific source files
FREERTOS_PORT_SRC = $(FREERTOS_SRC)portable/$(PORT_COMP_TARG)

# Directory with Application source files
APP_SRC = ./FreeRTOS/Demo/X86_64/

# FreeRTOS core objects
FREERTOS_OBJS = queue.o list.o tasks.o timers.o

# Memory management
FREERTOS_MEMMANG_OBJS = heap_1.o

# Port layer
FREERTOS_PORT_OBJS = port.o portASM.o

# Application objects
APP_OBJS = main.o start.o FreeRTOS_tick_config.o uart.o
APP_OBJS += printf-stdarg.o example.o nostdlib.o

# All object files
OBJS = $(addprefix $(OBJDIR), $(FREERTOS_OBJS) $(FREERTOS_MEMMANG_OBJS) $(FREERTOS_PORT_OBJS) $(APP_OBJS))

ELF_IMAGE = image.elf

# Include paths
INC_FREERTOS = $(FREERTOS_SRC)include/
INC_APP = $(APP_SRC)include/

INC_FLAGS = $(INCLUDEFLAG)$(INC_FREERTOS) $(INCLUDEFLAG)$(INC_APP) $(INCLUDEFLAG)$(FREERTOS_PORT_SRC)


all: $(OBJDIR) $(ELF_IMAGE)

$(OBJDIR) :
	mkdir -p $@

$(ELF_IMAGE): $(APP_SRC)linker.ld $(OBJS)
	$(LD) -m elf_x86_64 -T $(APP_SRC)linker.ld $(OBJS) -o $@

# FreeRTOS core
$(OBJDIR)%.o : $(FREERTOS_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# MemMang
$(OBJDIR)%.o : $(FREERTOS_MEMMANG_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# Port layer (C)
$(OBJDIR)%.o : $(FREERTOS_PORT_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# Port layer (ASM)
$(OBJDIR)%.o : $(FREERTOS_PORT_SRC)%.S
	$(CC) $(CFLAG) $(ASM_FLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# Application (ASM)
$(OBJDIR)%.o : $(APP_SRC)%.S
	$(CC) $(CFLAG) $(ASM_FLAGS) $(INC_FLAGS) $< $(OFLAG) $@

# Application (C)
$(OBJDIR)%.o : $(APP_SRC)%.c
	$(CC) $(CFLAG) $(CFLAGS) $(INC_FLAGS) $< $(OFLAG) $@

run:
	$(MAKE) all
	qemu-system-x86_64 -nographic -kernel $(ELF_IMAGE)

clean:
	rm -f *.o *.elf *.list
	rm -rf $(OBJDIR)

.PHONY: run clean
