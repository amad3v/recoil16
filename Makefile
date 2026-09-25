# SPDX-License-Identifier: GPL-2.0
# Manual builds of all modules; the module list is in Kbuild (also used by DKMS).
KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
