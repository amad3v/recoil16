# SPDX-License-Identifier: GPL-2.0
# All recoil16 modules; used by DKMS and by the top-level Makefile.
obj-m += ite8291-mono/
obj-m += ite8233-lightbar/
obj-m += copilot-rctrl/

# uniwill-laptop-pcs tracks linux-7.2.y and uses 7.2 APIs (e.g. the WMI
# min_event_size field); older kernels such as 6.18 LTS get the other modules.
# Keep in sync with BUILD_EXCLUSIVE_KERNEL_MIN[3] in dkms.conf.
ifeq ($(shell [ $(VERSION) -gt 7 ] || { [ $(VERSION) -eq 7 ] && [ $(PATCHLEVEL) -ge 2 ]; } && echo y),y)
obj-m += uniwill-laptop-pcs/
endif
