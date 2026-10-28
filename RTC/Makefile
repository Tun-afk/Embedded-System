# Makefile cho ds1307_plat (platform driver RTC DS1307)
#
# Tren Raspberry Pi (build truc tiep):
#     make            # build ds1307_plat.ko + ds1307-plat.dtbo + rtc_test
#     sudo make install
#
# Cross-compile tu PC (vi du):
#     make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KDIR=~/linux module

ifneq ($(KERNELRELEASE),)
# ---- Phan nay duoc Kbuild cua kernel doc ----
obj-m := ds1307_plat.o

else
# ---- Phan nay chay khi go "make" trong thu muc nay ----
KDIR    ?= /lib/modules/$(shell uname -r)/build
PWD     := $(shell pwd)
DTC     ?= dtc
CC      ?= gcc
KVER    ?= $(shell uname -r)
OVERLAY_DIR ?= $(shell [ -d /boot/firmware/overlays ] && echo /boot/firmware/overlays || echo /boot/overlays)

.PHONY: all module dtbo clean install uninstall

all: module dtbo rtc_test

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

dtbo: ds1307-plat.dtbo

ds1307-plat.dtbo: ds1307-plat-overlay.dts
	$(DTC) -@ -I dts -O dtb -o $@ $<

rtc_test: rtc_test.c
	$(CC) -O2 -Wall -Wextra -o $@ $<

install: all
	install -D -m 644 ds1307_plat.ko /lib/modules/$(KVER)/extra/ds1307_plat.ko
	depmod -a $(KVER)
	install -m 644 ds1307-plat.dtbo $(OVERLAY_DIR)/ds1307-plat.dtbo
	@echo "Installed module and overlay to $(OVERLAY_DIR)."
	@echo "Add 'dtparam=i2c_arm=on' and 'dtoverlay=ds1307-plat' to config.txt, then reboot."

uninstall:
	rm -f /lib/modules/$(KVER)/extra/ds1307_plat.ko $(OVERLAY_DIR)/ds1307-plat.dtbo
	depmod -a $(KVER)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f ds1307-plat.dtbo rtc_test
endif
