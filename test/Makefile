CC ?= gcc
CXX ?= g++
CFLAGS ?= -g -Og -std=c99 -Wall -Werror -I../
CXXFLAGS ?= -g -Og -std=c++11 -Wall -Werror -I../

EXE = .exe
BINS = client$(EXE) service$(EXE)

all: $(BINS)

packet_listener: packet_listener.o
	$(CXX) $(CXXFLAGS) -o $@.exe $@.o

client$(EXE): client.o ../lssdp.o
	$(CXX) -pthread $(CXXFLAGS) -o $@ ../lssdp.o $< -L.. -lncurses -ltinfo -lgpm

service$(EXE): service.o ../lssdp.o
	$(CXX) $(CXXFLAGS) -o $@ ../lssdp.o $< 

clean:
	rm -rf *.o $(BINS)
