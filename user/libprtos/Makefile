all: libs

include ../../prtos_config
include $(PRTOS_PATH)/config.mk
include $(PRTOS_PATH)/user/rules.mk

-include dep.mk
-include dephost.mk

HOST_CFLAGS += --include prtos_inc/config.h --include prtos_inc/arch/arch_types.h

SRCS = common/comm.c common/hypercalls.c common/init.c common/trace.c common/hm.c common/hypervisor.c common/lazy.c common/status.c $(ARCH)/hypercalls.c $(ARCH)/init.c common/xef.c common/lzss.c common/md5.c common/container.c

OBJS = $(patsubst %.c,%.o, $(filter %.c, $(SRCS))) $(patsubst %.S,%.o, $(filter %.S, $(SRCS)))

OBJS = $(patsubst %.c,%.o, $(filter %.c, $(SRCS))) $(patsubst %.S,%.o, $(filter %.S, $(SRCS)))

HOST_SRCS = common/md5.c common/lzss.c common/xef.c
HOST_OBJS = $(patsubst %.c,%.host.o, $(HOST_SRCS))

LIBPRTOS=libprtos.a libprtoshost.a

libs: $(LIBPRTOS)

libprtos.a: $(OBJS)
	$(TARGET_AR) -s -c -r libprtos.a $(OBJS)

libprtoshost.a: $(HOST_OBJS)
	@$(HOST_AR) -s -c -r libprtoshost.a $(HOST_OBJS)

distclean: clean
	@$(RM) -f $(LIBPRTOS_PATH)/include/arch
	@$(RM) -f $(LIBPRTOS_PATH)/include/prtos_inc
	@$(RM) -f $(LIBPRTOS_PATH)/common/md5.c

clean:
	@$(RM) -f *.o *.a *.o *.host.o *~ include/*~ include/arch/*~ $(ARCH)/*~ $(ARCH)/*.o common/*.o common/*~ dep.mk dephost.mk
