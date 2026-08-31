
.PHONY: all clean

all: application primitive_benchmark

SAN_OBJS = Application.o gcm.o
PRIMITIVE_BENCH_OBJS = PrimitiveBenchmark.o

%.o: %.cpp
	g++ -std=c++11 -O0 -ggdb -c $<

application: $(SAN_OBJS)
	g++ -std=c++11 $^ -L=$(CURDIR) -lOSort -lcrypto -Wl,--rpath=$(CURDIR) -o $@

primitive_benchmark: $(PRIMITIVE_BENCH_OBJS)
	g++ -std=c++11 $^ -L=$(CURDIR) -lOSort -lcrypto -Wl,--rpath=$(CURDIR) -o $@

clean:
	rm -f $(SAN_OBJS) $(PRIMITIVE_BENCH_OBJS) application primitive_benchmark
