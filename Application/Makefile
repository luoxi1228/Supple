
.PHONY: all clean

SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
ifeq ($(shell getconf LONG_BIT), 32)
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib
else
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
endif
ifeq ($(SGX_MODE), HW)
	SGX_RUNTIME_LIBS := -lsgx_urts -lsgx_uae_service
else
	SGX_RUNTIME_LIBS := -lsgx_urts_sim -lsgx_uae_service_sim
endif
APP_LINK_FLAGS := -L$(CURDIR) -lOSort -lcrypto -L$(SGX_LIBRARY_PATH) \
	-Wl,--no-as-needed $(SGX_RUNTIME_LIBS) -Wl,--as-needed -pthread \
	-Wl,--rpath=$(CURDIR) \
	-Wl,--rpath=$(SGX_LIBRARY_PATH)

all: application primitive_benchmark fmscompact_vs_ocompact

SAN_OBJS = Application.o gcm.o
PRIMITIVE_BENCH_OBJS = PrimitiveBenchmark.o
FMSCOMPACT_BENCH_OBJS = FMSCompactApplication.o

%.o: %.cpp
	g++ -std=c++11 -O0 -ggdb -c $<

application: $(SAN_OBJS)
	g++ -std=c++11 $^ $(APP_LINK_FLAGS) -o $@

primitive_benchmark: $(PRIMITIVE_BENCH_OBJS)
	g++ -std=c++11 $^ $(APP_LINK_FLAGS) -o $@

fmscompact_vs_ocompact: $(FMSCOMPACT_BENCH_OBJS)
	g++ -std=c++11 $^ $(APP_LINK_FLAGS) -o $@

clean:
	rm -f $(SAN_OBJS) $(PRIMITIVE_BENCH_OBJS) $(FMSCOMPACT_BENCH_OBJS) application primitive_benchmark fmscompact_vs_ocompact
