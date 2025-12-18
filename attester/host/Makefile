CC      ?= $(CROSS_COMPILE)gcc
LD      ?= $(CROSS_COMPILE)ld
AR      ?= $(CROSS_COMPILE)ar
NM      ?= $(CROSS_COMPILE)nm
OBJCOPY ?= $(CROSS_COMPILE)objcopy
OBJDUMP ?= $(CROSS_COMPILE)objdump
READELF ?= $(CROSS_COMPILE)readelf

OBJS = main.o client.o
LIBS = rust-ffi/target/aarch64-unknown-linux-gnu/release/libveraison_client_ffi.a

CFLAGS += -Wall -I../ta/include -I$(TEEC_EXPORT)/include
# Build-time define so TA headers choose TEEC_UUID on host side
CFLAGS += -DHOST_BUILD=1
#Add/link other required libraries here
LDADD += -lteec -L$(TEEC_EXPORT)/lib -lpthread -ldl

BINARY = optee_remote_attestation

.PHONY: all
all: $(BINARY)

$(BINARY): $(OBJS) $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS) $(LDADD) -lm

.PHONY: clean
clean:
	rm -f $(OBJS) $(BINARY)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
