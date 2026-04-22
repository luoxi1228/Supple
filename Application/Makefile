
.PHONY: all clean

all: application

SAN_OBJS = Application.o gcm.o

%.o: %.cpp
	g++ -std=c++11 -O0 -ggdb -c $<

application: $(SAN_OBJS)
	g++ -std=c++11 $^ -L=$(CURDIR) -lOSort -lcrypto -Wl,--rpath=$(CURDIR) -o $@

clean:
	rm -f $(SAN_OBJS) application
