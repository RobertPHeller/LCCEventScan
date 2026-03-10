OPENMRNPATH ?= $(shell \
sh -c "if [ \"X`printenv OPENMRNPATH`\" != \"X\" ]; then printenv OPENMRNPATH; \
     elif [ -d /opt/openmrn/src ]; then echo /opt/openmrn; \
     elif [ -d ~/openmrn/src ]; then echo ~/openmrn; \
     elif [ -d ../../../src ]; then echo ../../..; \
     else echo OPENMRNPATH not found; fi" \
)

SUBDIRS = targets doc

-include config.mk
include $(OPENMRNPATH)/etc/recurse.mk

refman.pdf: doc/latex/refman.pdf
	cp doc/latex/refman.pdf ./

Release_aarch64: all
	-rm -f Release-aarch64-`git rev-parse --short HEAD`.zip
	zip -j Release-aarch64-`git rev-parse --short HEAD`.zip \
		targets/linux.aarch64/LCCEventScan* \
		 doc/latex/*.pdf

Release_armv7a: all
	-rm -f Release-armv7a-`git rev-parse --short HEAD`.zip
	zip -j Release-armv7a-`git rev-parse --short HEAD`.zip \
		targets/linux.armv7a/LCCEventScan* \
		 doc/latex/*.pdf

Release_x86: all
	-rm -f Release-x86-`git rev-parse --short HEAD`.zip
	zip -j Release-x86-`git rev-parse --short HEAD`.zip \
		targets/linux.x86/LCCEventScan* \
		 doc/latex/*.pdf


doc/latex/refman.pdf: FORCE
	$(MAKE) -c doc pdf
