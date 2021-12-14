CC = gcc
CFLAGS = -Wall -O3 
INCLUDES = 
LFLAGS = 
LIBS = -ljpeg -lm

OBJS = dualfish2sphere.o bitmaplib.o

all: dualfish2sphere

dualfish2sphere: $(OBJS)
	$(CC) $(INCLUDES) $(CFLAGS) -o dualfish2sphere $(OBJS) $(LFLAGS) $(LIBS) 

dualfish2sphere.o: dualfish2sphere.c dualfish2sphere.h
	$(CC) $(INCLUDES) $(CFLAGS) -c dualfish2sphere.c
 
bitmaplib.o: bitmaplib.c bitmaplib.h
	$(CC) $(INCLUDES) $(CFLAGS) -c bitmaplib.c

clean:
	rm -rf core dualfish2sphere $(OBJS)

