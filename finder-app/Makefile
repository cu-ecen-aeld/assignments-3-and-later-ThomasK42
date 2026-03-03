SRC := writer.c
TARGET = writer
OBJS := $(SRC:.c=.o)

ifdef CROSS_COMPILE
	CC := $(CROSS_COMPILE)gcc
else
	CC := gcc
endif

all: $(TARGET)

default: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map
