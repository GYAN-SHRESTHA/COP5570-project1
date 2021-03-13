CC = gcc
CFLAGS = -Wall -g -std=gnu99 -pedantic
OBJ = mytoolkit.o

TARGET = mytoolkit

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

.PHONY : clean

clean:
	rm -f $(TARGET) *.o 
