OBJS = main.o ImapResolve.o PeelHeader.o
G = g++
CFLAGS = -std=c++11 -Wall -O
 
main:$(OBJS)
	$(G) $(CFLAGS) $(OBJS) -o main

main.o:ImapResolve.h PeelHeader.h main.cpp

ImapResolve.o:ImapResolve.h PeelHeader.h ImapResolve.cpp

PeelHeader.o:PeelHeader.h PeelHeader.cpp

.PHONY:clean
clean:
	-rm -rf *.o main
