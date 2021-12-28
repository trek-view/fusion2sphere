CC = gcc
CFLAGS = -Wall -O3 
INCLUDES = 
LFLAGS = 
LIBS = -ljpeg -lm

OBJS = fusion2sphere.o bitmaplib.o

all: fusion2sphere

fusion2sphere: $(OBJS)
	$(CC) $(INCLUDES) $(CFLAGS) -o fusion2sphere $(OBJS) $(LFLAGS) $(LIBS) 

fusion2sphere.o: fusion2sphere.c fusion2sphere.h
	$(CC) $(INCLUDES) $(CFLAGS) -c fusion2sphere.c
 
bitmaplib.o: bitmaplib.c bitmaplib.h
	$(CC) $(INCLUDES) $(CFLAGS) -c bitmaplib.c

clean:
	rm -rf core fusion2sphere $(OBJS)

