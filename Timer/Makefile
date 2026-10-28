# Makefile cho platform driver tu viet tren SoC BCM2835 (Pi Zero W):
#   bcm_timer_plat.ko + bcm-timer-plat.dtbo  (System Timer kenh 1)
#
# Tren Raspberry Pi:
#     make
#     sudo make install
#
# Cross-compile tu PC (vi du):
#     make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KDIR=~/linux module

ifneq ($(KERNELRELEASE),)
# ---- Phan nay duoc Kbuild cua kernel doc ----
obj-m := bcm_timer_plat.o

else
# ---- Phan nay chay khi go "make" trong thu muc nay ----
KDIR    ?= /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)
DTC     ?= dtc
KVER    ?= $(shell uname -r)
OVERLAY_DIR ?= $(shell [ -d /boot/firmware/overlays ] && echo /boot/firmware/overlays || echo /boot/overlays)
DTBOS   := bcm-timer-plat.dtbo

.PHONY: all module dtbo clean install uninstall

all: module dtbo

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

dtbo: $(DTBOS)

%.dtbo: %-overlay.dts
	$(DTC) -@ -I dts -O dtb -o $@ $<

install: all
	install -D -m 644 bcm_timer_plat.ko /lib/modules/$(KVER)/extra/bcm_timer_plat.ko
	depmod -a $(KVER)
	install -m 644 $(DTBOS) $(OVERLAY_DIR)/
	@echo "Installed module and overlay to $(OVERLAY_DIR)."
	@echo "Add 'dtoverlay=bcm-timer-plat' to config.txt, then reboot."

uninstall:
	rm -f /lib/modules/$(KVER)/extra/bcm_timer_plat.ko
	rm -f $(addprefix $(OVERLAY_DIR)/,$(DTBOS))
	depmod -a $(KVER)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(DTBOS)
endif
