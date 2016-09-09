TARGET_SHARE = libswevent.so
TARGET_STATIC = libswevent.a
INSTALL_DIR:=/usr/local
CC := cc
AR := ar
CFLAGS := -Wall -O0 -g -fPIC
LDFLAGS := -shared
SRCS := sw_event.c sw_log.c sw_util.o
OBJS := sw_event.o sw_log.o sw_util.o

all: $(TARGET_SHARE) $(TARGET_STATIC)

$(TARGET_SHARE): $(OBJS) 
	$(CC) -o $@ $(LDFLAGS) $(OBJS)

$(TARGET_STATIC): $(OBJS) 
	$(AR) -cr $(TARGET_STATIC) $(OBJS)

sw_event.o : sw_event.c
	$(CC) -c -o $@ $(CFLAGS) $<
sw_log.o : sw_log.c
	$(CC) -c -o $@ $(CFLAGS) $<
sw_util.o : sw_util.c
	$(CC) -c -o $@ $(CFLAGS) $<

install:
	install -d $(INSTALL_DIR)/{include,lib}
	install sw_event.h sw_util.h $(INSTALL_DIR)/include
	install $(TARGET_SHARE) $(TARGET_STATIC) $(INSTALL_DIR)/lib

clean:
	rm -f $(OBJS)
	rm -f $(TARGET_SHARE) $(TARGET_STATIC)
