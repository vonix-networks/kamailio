#
# kazoo module (2600hz build: C core + C++ AMQP layer on a vendored, patched
# AMQP-CPP 4.1 under amqp/, libev event loop, jwt-cpp/nlohmann json headers
# under include/). Links libev, libuuid, OpenSSL, json-c, zlib, libstdc++.
#
# WARNING: do not run this directly, it should be run by the main Makefile

include ../../Makefile.defs
auto_gen=
NAME=kazoo.so

CXX?=g++
LD=g++
CSTDVER?=c++11

AMQPCPP_OBJS = amqp/src/array.o amqp/src/channel.o amqp/src/channelimpl.o \
	amqp/src/connectionimpl.o amqp/src/deferredcancel.o amqp/src/deferredconfirm.o \
	amqp/src/deferredconsumer.o amqp/src/deferredextreceiver.o amqp/src/deferredget.o \
	amqp/src/deferredpublisher.o amqp/src/deferredreceiver.o amqp/src/field.o \
	amqp/src/flags.o amqp/src/receivedframe.o amqp/src/table.o amqp/src/watchable.o \
	amqp/src/linux_tcp/openssl.o amqp/src/linux_tcp/tcpconnection.o
CXX_OBJS = kz_amqp_manager.o kz_jwt.o $(AMQPCPP_OBJS)

# the C++ objects go on the link line before the libraries they use
# (the linker resolves left to right)
LIBS += $(CXX_OBJS)

BUILDER = $(shell which pkg-config)
ifeq ($(BUILDER),)
	DEFS += -I$(LOCALBASE)/include -I$(SYSBASE)/include -I$(SYSBASE)/include/json-c
	LIBS += -L$(LOCALBASE)/lib -ljson-c -luuid -lssl -lcrypto -lz
else
	DEFS += $(shell $(BUILDER) --cflags json-c uuid openssl zlib)
	LIBS += $(shell $(BUILDER) --libs json-c uuid openssl zlib)
endif
# libev ships no pkg-config file
LIBS += -lev -lstdc++ -lm

# module dir first: sources include "amqp/include/amqpcpp.h", "include/x.hpp";
# AMQP-CPP sources include their siblings and "amqpcpp/..." by bare name.
DEFS += -I$(CURDIR) -I$(CURDIR)/amqp/include -I$(CURDIR)/amqp/src -I$(CURDIR)/include

CXXFLAGS=$(CFLAGS:-Wno-deprecated option=)
CXXFLAGS+=-Wno-write-strings -Wno-deprecated -Wno-unused-function -Wno-sign-compare -Wno-strict-aliasing

SERLIBPATH=../../lib
SER_LIBS=$(SERLIBPATH)/srdb1/srdb1

include ../../Makefile.modules

# module C++ (uses kamailio core headers)
kz_amqp_manager.o kz_jwt.o: %.o: %.cpp
	@echo "Compiling $<"
	$(CXX) -std=$(CSTDVER) $(CXXFLAGS) $(CFLAGS) $(C_DEFS) $(DEFS) -c $< -o $@

# vendored AMQP-CPP (plain C++, no kamailio headers)
$(AMQPCPP_OBJS): %.o: %.cpp
	@echo "Compiling $<"
	$(CXX) -std=$(CSTDVER) -O2 -g -fPIC -I$(CURDIR)/amqp/include -I$(CURDIR)/amqp/src -c $< -o $@

kazoo.so: $(CXX_OBJS)

modclean:
	-@rm -f $(CXX_OBJS)

clean clean-modules: modclean

proper realclean distclean maintainer-clean: modclean
