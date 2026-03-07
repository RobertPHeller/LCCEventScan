#export TARGET:=linux.aarch64
#export EXECUTABLE:=$(notdir $(realpath $(CURDIR)/../..))

OPENMRNPATH ?= $(shell \
sh -c "if [ \"X`printenv OPENMRNPATH`\" != \"X\" ]; then printenv OPENMRNPATH; \
     elif [ -d /opt/openmrn/src ]; then echo /opt/openmrn; \
     elif [ -d ~/openmrn/src ]; then echo ~/openmrn; \
     elif [ -d ../../../src ]; then echo ../../..; \
     else echo OPENMRNPATH not found; fi" \
)

APP_PATH ?= $(realpath ../..)
-include ../../config.mk
INCLUDES += $(shell pkg-config --cflags libxml++-2.6)
include $(OPENMRNPATH)/etc/prog.mk
SYSLIBRARIES += $(shell pkg-config --libs libxml++-2.6) -lcsv
CXXFLAGS += -Wno-deprecated-declarations
CFLAGS += -Wno-deprecated-declarations


