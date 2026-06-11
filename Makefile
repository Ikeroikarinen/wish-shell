CC = gcc
CFLAGS = -Wall -Werror
TARGET = wish

all: $(TARGET)

$(TARGET): wish.c
	$(CC) $(CFLAGS) -o $(TARGET) wish.c

clean:
	rm -f $(TARGET) *.o output.txt batch.txt
