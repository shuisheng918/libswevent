TARGET_SHARE = libswevent.so
TARGET_STATIC = libswevent.a
INSTALL_DIR:=/usr/local
ifdef PREFIX
INSTALL_DIR:=$(PREFIX)
endif
SRC_DIR=.
OBJ_DIR=./objs
MKDIR := mkdir -p
CC := gcc
AR := ar
CFLAGS := -W -Wall -O0 -g -fPIC
LDFLAGS := -shared -O0
SRCS := sw_event.c sw_log.c
basename_srcs := $(notdir $(SRCS))
basename_objs := $(patsubst %.c,%.o,$(basename_srcs) )
OBJS := $(addprefix $(OBJ_DIR)/, $(basename_objs))

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c -o $@ $(CFLAGS) $<

all: $(OBJ_DIR) $(TARGET_SHARE) $(TARGET_STATIC)

$(TARGET_SHARE): $(OBJS) 
	$(CC) -o $@ $(LDFLAGS) $(OBJS)

$(TARGET_STATIC): $(OBJS) 
	$(AR) -cr $(TARGET_STATIC) $(OBJS)

$(OBJ_DIR):
	$(MKDIR) $(OBJ_DIR)

install:
	install -d $(INSTALL_DIR)/{include,lib}
	install sw_event.h $(INSTALL_DIR)/include
	install $(TARGET_SHARE) $(TARGET_STATIC) $(INSTALL_DIR)/lib

clean:
	rm -rf $(OBJ_DIR)
	rm -f $(TARGET_SHARE) $(TARGET_STATIC)
