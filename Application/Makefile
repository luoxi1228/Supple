
.PHONY: all clean

SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
ifeq ($(shell getconf LONG_BIT), 32)
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib
else
	SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
endif
APP_LINK_FLAGS := -L$(CURDIR) -lOSort -lcrypto -pthread \
	-Wl,--rpath=$(CURDIR)
ifneq ($(SGX_MODE), HW)
	# Simulation runtime libraries live in the SDK. In hardware mode the SDK
	# uRTS is only a stub: the real uRTS must come from the installed PSW.
	APP_LINK_FLAGS += -L$(SGX_LIBRARY_PATH) \
		-Wl,--no-as-needed -lsgx_urts_sim -lsgx_uae_service_sim \
		-Wl,--as-needed -Wl,--rpath=$(SGX_LIBRARY_PATH)
endif

all: application primitive_benchmark fmscompact_vs_ocompact

SAN_OBJS = Application.o gcm.o
PRIMITIVE_BENCH_OBJS = PrimitiveBenchmark.o
FMSCOMPACT_BENCH_OBJS = FMSCompactApplication.o

%.o: %.cpp
	g++ -std=c++11 -O0 -ggdb -c $<

application: $(SAN_OBJS) libOSort.so
	g++ -std=c++11 $(SAN_OBJS) $(APP_LINK_FLAGS) -o $@

primitive_benchmark: $(PRIMITIVE_BENCH_OBJS) libOSort.so
	g++ -std=c++11 $(PRIMITIVE_BENCH_OBJS) $(APP_LINK_FLAGS) -o $@

fmscompact_vs_ocompact: $(FMSCOMPACT_BENCH_OBJS) libOSort.so
	g++ -std=c++11 $(FMSCOMPACT_BENCH_OBJS) $(APP_LINK_FLAGS) -o $@

clean:
	rm -f $(SAN_OBJS) $(PRIMITIVE_BENCH_OBJS) $(FMSCOMPACT_BENCH_OBJS) application primitive_benchmark fmscompact_vs_ocompact
