SRC := aesdsocket.c
TARGET ?= aesdsocket
OBJS := $(SRC:.c=.o)

ifdef CROSS_COMPILE
	CC ?= $(CROSS_COMPILE)gcc
else
	CC ?= gcc
endif

CFLAGS ?= -Wall -Wextra -Werror -g

all: $(TARGET)

default: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map
