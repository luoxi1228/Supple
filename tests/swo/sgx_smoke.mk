# Test-only build overlay; invoke from the repository root. The simulator uses
# the host FS register, so enclave TLS must go through __tls_get_addr rather
# than native host TLS offsets. Keep the production Makefile flags unchanged.
SGX_MODE := SIM
include Makefile

Enclave_Cpp_Flags := $(filter-out -fpie,$(Enclave_Cpp_Flags)) -fPIC -ftls-model=global-dynamic
swo_pie_flag := -Wl,-pie,-eenclave_entry
Enclave_Link_Flags := $(filter-out $(swo_pie_flag),$(Enclave_Link_Flags)) -shared -Wl,-eenclave_entry
