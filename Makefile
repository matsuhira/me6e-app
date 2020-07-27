TARGET	= me6eapp me6ectl

COM_SRCS = \
	me6eapp_log.c \
	me6eapp_socket.c \

APP_SRCS = \
	me6eapp_main.c \
	me6eapp_config.c \
	me6eapp_hashtable.c \
	me6eapp_network.c \
	me6eapp_netlink.c \
	me6eapp_print_packet.c \
	me6eapp_util.c \
	me6eapp_timer.c \
	me6eapp_setup.c \
	me6eapp_Controller.c \
	me6eapp_ProxyArp.c \
	me6eapp_ProxyNdp.c \
	me6eapp_MacManager.c \
	me6eapp_Capsuling.c \
	me6eapp_mainloop.c \
	me6eapp_statistics.c \
	me6eapp_pr.c \

CTL_SRCS = \
	me6ectl.c \
	me6eapp_command.c \

COM_OBJS = $(COM_SRCS:.c=.o)
APP_OBJS = $(APP_SRCS:.c=.o)
CTL_OBJS = $(CTL_SRCS:.c=.o)

OBJS	= $(COM_OBJS) $(APP_OBJS) $(CTL_OBJS)
DEPENDS	= $(COM_SRCS:.c=.d) $(APP_SRCS:.c=.d) $(CTL_SRCS:.c=.d)
LIBS	= -lpthread -lrt

CC	= gcc
# for release flag
CFLAGS	= -O2 -Wall -std=gnu99 -D_GNU_SOURCE
# for debug flag
#CFLAGS	= -O2 -Wall -std=gnu99 -D_GNU_SOURCE -DDEBUG -g
INCDIR	= -I.
LD	= gcc
LDFLAGS	=$(CFLAGS)
LIBDIR	=


all: $(TARGET)

cleanall:
	rm -f $(OBJS) $(DEPENDS) $(TARGET)

clean:
	rm -f $(OBJS) $(DEPENDS)

me6eapp: $(COM_OBJS) $(APP_OBJS)
	$(LD) $(LIBDIR) $(LDFLAGS) -o $@ $(COM_OBJS) $(APP_OBJS) $(LIBS)

me6ectl: $(COM_OBJS) $(CTL_OBJS)
	$(LD) $(LIBDIR) $(LDFLAGS) -o $@ $(COM_OBJS) $(CTL_OBJS)

.c.o:
	$(CC) $(INCDIR) $(CFLAGS) -c $<

%.d: %.c
	@set -e; $(CC) -MM $(CINCS) $(CFLAGS) $< \
		| sed 's/\($*\)\.o[ :]*/\1.o $@ : /g' > $@; \
		[ -s $@ ] || rm -f $@

-include $(DEPENDS)
