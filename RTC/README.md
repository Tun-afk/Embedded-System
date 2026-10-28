# Platform driver for the DS1307 RTC – Raspberry Pi Zero W

Full guide: wire the hardware on a breadboard → build the code → load the driver → run the tests.

| File | Content |
|---|---|
| `ds1307_plat.c` | **Platform driver** (`platform_driver`, `probe()`, `remove()`), uses the Device Tree and registers the RTC device `/dev/rtcN` |
| `ds1307-plat-overlay.dts` | Device Tree overlay: enables I2C1 and declares the `ds1307-plat` platform node |
| `Makefile` | Builds the `.ko` module, the `.dtbo` overlay and the test program; `make install` |
| `rtc_test.c` | User-space test program: reads/writes the RTC with `ioctl(RTC_RD_TIME / RTC_SET_TIME)` |
| `wiring.png` | Wiring diagram |

---

## 1. Components

| Component | Note |
|---|---|
| Raspberry Pi Zero W | With a **soldered 40-pin header** (the Zero W ships without one; the Zero WH has it). At least pins 1–6 must be soldered. |
| microSD card ≥ 8 GB | Raspberry Pi OS **32-bit** (the Pi Zero W is ARMv6 and cannot run the 64-bit image) |
| 5 V – 2 A micro-USB power supply | Plugged into the **PWR IN** port |
| DS1307 RTC module ("Tiny RTC" type, with DS1307 + AT24C32 EEPROM) | |
| 3 V CR2032 cell | |
| Breadboard + 4 **female–male** jumper wires | Red, black, blue and yellow, for easy identification |
| Multimeter | To check the module before connecting it to the Pi |
| (Optional) LED + 1 kΩ resistor | To test the SQW pin (1 Hz square wave) |
| (Optional) Soldering iron, desoldering pump | If the module needs modifying (section 3) |

---

## 2. Quick answers: are resistors, capacitors or a separate supply needed?

**Separate power supply for the DS1307? NOT needed.**
Take 5 V directly from pin 4 (or pin 2) of the Pi. The DS1307 draws at most about 1.5 mA. If an external 5 V supply is
used anyway, its **GND must be connected to the Pi's GND**.

**Supply 5 V, not 3.3 V.**
The DS1307 needs VCC between 4.5 V and 5.5 V. It compares VCC with the battery voltage: when VCC < 1.25 × VBAT
(≈ 3.75 V with a 3 V cell), the chip **locks out I2C communication**. With 3.3 V, `i2cdetect` will not see the chip.

**Is the Pi's 3.3 V I2C logic enough for a chip running at 5 V? Yes.**
The DS1307 reads a logic "1" from 2.2 V upwards, so the Pi's 3.3 V is enough. No level shifter is needed,
**provided** that SDA/SCL are not pulled up to 5 V (see below).

**Pull-up resistors on SDA/SCL? NO extra ones needed.**
GPIO2/GPIO3 of the Pi already have 1.8 kΩ pull-ups to 3.3 V on the board. The problem is the opposite: Tiny RTC modules
usually carry two 3.3 kΩ resistors pulling SDA/SCL up to **5 V**. The Pi's GPIOs **are not 5 V tolerant**. These two
resistors must be removed (section 3.2).

**Capacitor? NOT needed.**
The module already has a 100 nF decoupling capacitor. Only a discrete DS1307 chip needs one (section 4.4).

**Can a CR2032 be used on a Tiny RTC module? Yes, but the charging circuit must be removed.**
The Tiny RTC module is designed for a **rechargeable** LIR2032 cell and has a circuit that charges the cell from 5 V.
A CR2032 is **not rechargeable**: when charged it heats up, swells and may leak or burst. Check for the charging
circuit and remove it (section 3.1).

---

## 3. Checking (and fixing if needed) the module BEFORE connecting it to the Pi

The component names R2, R3, D1… below follow the common Tiny RTC board. Clones may be numbered differently, so
**always measure to confirm**; do not rely only on the names printed on the board.

Power for the measurements: no battery fitted, SDA/SCL not connected. Only connect **module VCC → pin 4 (5 V)** and
**module GND → pin 6 (GND)** of the Pi, then power the Pi. Any other 5 V supply also works.

### 3.1 Checking the battery charging circuit (REQUIRED with a CR2032)

- Measure the voltage between the **+ terminal of the battery holder** (the metal tab on top) and **GND**, with no battery fitted.
- **≈ 0 V**: the module has no charging circuit; a CR2032 can be used as is.
- **A few volts (usually 3–4.5 V)**: there is a charging circuit. Power off and remove **diode D1** (a small diode, usually a 1N4148, near the battery holder). Removing D1 is enough to cut the charging path.
- The full fix often used on the Tiny RTC: remove D1, R4, R5 and R6, then **bridge the two pads of R6**. This also removes the voltage divider that drains the battery. Only do this if the board really is a Tiny RTC.
- Measure again after the fix: it must read **≈ 0 V**.

To avoid soldering, use a rechargeable **LIR2032** instead of the CR2032 and leave the module unchanged.

### 3.2 Checking for 5 V pull-ups on SDA/SCL (REQUIRED before connecting to the Pi)

- Keep the 5 V supply as above, **SDA/SCL not connected to the Pi**.
- Measure **SDA–GND** and **SCL–GND**.
- **≈ 0 V or floating**: no pull-ups; the module can be connected directly to the Pi.
- **≈ 5 V**: there are pull-ups to 5 V. Power off and remove the two pull-up resistors. On the Tiny RTC they are usually **R2 and R3**, 3.3 kΩ (marked **332**). Each has one end on SDA (or SCL) and the other on VCC; a continuity test finds them.
- To keep the resistors, put a bidirectional I2C level shifter (BSS138) between the Pi (3.3 V) and the module (5 V).
- Measure again after the fix: it must read **≈ 0 V**. Once connected to the Pi, SDA/SCL sit at 3.3 V through the Pi's resistors.

Why this matters: with 3.3 kΩ pull-ups to 5 V in parallel with the Pi's 1.8 kΩ pull-ups to 3.3 V, the idle I2C lines
sit at about 3.9 V. This is above the 3.6 V absolute maximum of the GPIOs and can damage GPIO2/3 over time.

### 3.3 Fitting the battery

- Power off. Put the CR2032 in the holder, **(+) side facing up** (outwards).
- Measure the cell: it must read about 2.8 V to 3.3 V. Below 2 V the DS1307 cannot keep the time during a power loss.

---

## 4. Pinout and breadboard wiring

![Wiring diagram](wiring.png)

### 4.1 Wiring table

| Wire colour | DS1307 module pin | Pi Zero W physical pin | Pi pin name | Note |
|---|---|---|---|---|
| Red | **VCC** | **Pin 4** (or pin 2) | 5V | NOT pin 1 (3.3 V) |
| Black | **GND** | **Pin 6** | GND | |
| Blue | **SDA** | **Pin 3** | GPIO2 / SDA1 | I2C1 bus, `/dev/i2c-1` |
| Yellow | **SCL** | **Pin 5** | GPIO3 / SCL1 | |
| — | SQ, DS, BAT | Not connected | | SQ is only used for the square-wave test (section 9.7) |

Pi pins 3, 4, 5 and 6 sit next to each other as a 2×2 block, which makes mistakes unlikely.

### 4.2 Locating the pins on the Pi Zero W header

```
Seen from above, header along the top edge of the board, SD-card slot on the left:

 outer row (board edge) →   2      4      6      8      10    ...  40
                            5V     5V     GND    GPIO14 GPIO15
 inner row              →   ■1     3      5      7      9     ...  39
                            3V3    SDA1   SCL1   GPIO4  GND
                                   GPIO2  GPIO3
```

- Pin 1 has a **square solder pad** and is at the end of the header **next to the SD-card slot**.
- The outer row holds the even pins (2, 4, 6…); the inner row holds the odd pins (1, 3, 5…).
- Pins 2 and 4 are both 5 V. Pin 6 is GND.

### 4.3 Assembly steps

1. **Shut down the Pi and unplug it.**
2. Solder a straight header onto the module's **P1** row (7 pins: SQ, DS, SCL, SDA, VCC, GND, BAT), if it has none.
3. Insert the module into the breadboard so that **each pin is in a different breadboard column**. Never put two pins in the same 5-hole strip.
4. Insert the **male** ends of the 4 jumper wires into the same breadboard columns as the module's VCC, GND, SDA and SCL pins.
5. Plug the **female** ends onto the Pi header following table 4.1, one wire at a time, checking the pin numbers.
6. **Check again before powering up:**
   - Red wire on pin 4 (5 V), not pin 1 (3.3 V).
   - Black wire on pin 6 (GND).
   - SDA on pin 3, SCL on pin 5. Swapping them does no damage, but the chip will not answer.
   - No module pins touching each other on the breadboard.
7. Plug the power supply into the Pi's **PWR IN** port.

The breadboard can be skipped by using 4 **female–female** wires directly from the module to the Pi; the wiring is the same.

### 4.4 Using a discrete DS1307 chip (DIP-8) instead of the module

| DS1307 pin | Connect to |
|---|---|
| 1 X1, 2 X2 | **32.768 kHz** crystal (12.5 pF load), close to the chip, no capacitors needed |
| 3 VBAT | + of the CR2032 (− to GND). Without a battery, connect VBAT to GND |
| 4 GND | GND (Pi pin 6) |
| 5 SDA | Pi pin 3 (no pull-up needed, the Pi has 1.8 kΩ to 3.3 V) |
| 6 SCL | Pi pin 5 |
| 7 SQW/OUT | Leave open, or 10 kΩ pull-up to 3.3 V if used (open-drain output) |
| 8 VCC | 5 V (Pi pin 4) + **100 nF capacitor** from VCC to GND, close to the chip pin |

---

## 5. Preparing Raspberry Pi OS

1. Use **Raspberry Pi Imager** to write **Raspberry Pi OS Lite (32-bit)** to the card. In the advanced settings, set the hostname, user/password, Wi-Fi, and enable SSH.
2. Boot the Pi and log in over SSH: `ssh <user>@<hostname>.local`
3. Update and **reboot**, so that the running kernel matches the kernel headers about to be installed:

```bash
sudo apt update
sudo apt full-upgrade -y
sudo reboot
```

4. Install the build tools, kernel headers, `dtc` and i2c-tools:

```bash
sudo apt install -y build-essential device-tree-compiler i2c-tools linux-headers-rpi-v6
# Older OS images (Bullseye and earlier) use: sudo apt install -y raspberrypi-kernel-headers
```

5. Check that the headers match the running kernel:

```bash
uname -r                              # e.g. 6.12.x+rpt-rpi-v6
ls -d /lib/modules/$(uname -r)/build  # must exist
```

If the `build` directory does not exist, the kernel and headers do not match. Repeat step 3 (`full-upgrade`, then `reboot`).

6. If the `hwclock` command is missing: `sudo apt install -y util-linux-extra`

---

## 6. Checking the hardware with i2c-tools (before using the driver)

This step confirms that the wiring is correct, independently of the driver.

```bash
sudo raspi-config nonint do_i2c 0   # enable I2C (same as dtparam=i2c_arm=on)
sudo reboot
```

After the reboot:

```bash
ls /dev/i2c-1
sudo i2cdetect -y 1
```

The correct result shows **68** (DS1307). A Tiny RTC module also shows **50** (AT24C32 EEPROM):

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
...
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
```

Read the seconds register:

```bash
sudo i2cget -y 1 0x68 0x00
```

- `0x80` or higher: CH bit = 1, the oscillator is stopped (normal with a newly fitted battery). The driver restarts it when the time is set.
- Two reads a few seconds apart give different values: the chip is counting.

No `68`: see section 13 (Troubleshooting). **Do not continue** until `68` appears.

> **Important:** `config.txt` must NOT contain `dtoverlay=i2c-rtc,ds1307`. That line loads the kernel's built-in
> `rtc-ds1307` driver, which claims address 0x68, and our driver then reports `-EBUSY`.

---

## 7. Building the code

Only **4 files** need to be copied to the Pi. `README.md` and `wiring.png` are documentation and stay on the PC.

| File | Role |
|---|---|
| `ds1307_plat.c` | Driver (`platform_driver` side: `of_match_table`, `probe()`, `remove()`) |
| `ds1307-plat-overlay.dts` | Device Tree overlay (device side: `compatible = "edu,ds1307-platform"`) |
| `Makefile` | Builds both files above and `rtc_test` |
| `rtc_test.c` | Test program used in sections 9 and 10 |

From the PC (on Windows, open PowerShell in the folder that contains the files):

```bash
ssh <user>@<hostname>.local "mkdir -p ~/RTC"
scp ds1307_plat.c ds1307-plat-overlay.dts Makefile rtc_test.c <user>@<hostname>.local:~/RTC/
```

If the `Makefile` was opened and saved in a Windows editor, also run `sed -i 's/\r$//' Makefile` on the Pi to remove
Windows line endings.

On the Pi:

```bash
cd ~/RTC
make
```

This produces 3 files:

| File | Meaning |
|---|---|
| `ds1307_plat.ko` | Kernel module (platform driver) |
| `ds1307-plat.dtbo` | Compiled Device Tree overlay |
| `rtc_test` | User-space test program |

The build takes about 1–2 minutes on a Pi Zero W. Check the module:

```bash
modinfo ds1307_plat.ko
# alias: of:N*T*Cedu,ds1307-platform...  vermagic: must match uname -r
```

---

## 8. Loading the overlay and the driver

### Method A (recommended): permanent installation, loaded automatically at boot

```bash
sudo make install
```

This copies `ds1307_plat.ko` to `/lib/modules/$(uname -r)/extra/`, runs `depmod`, and copies `ds1307-plat.dtbo` to
`/boot/firmware/overlays/` (older OS images: `/boot/overlays/`).

Edit the boot configuration:

```bash
sudo nano /boot/firmware/config.txt      # older OS images: /boot/config.txt
```

Add at the **end of the file**, below the `[all]` line (`dtparam=i2c_arm=on` may already have been added by raspi-config
in section 6; if so, do not add it again):

```
dtparam=i2c_arm=on
dtoverlay=ds1307-plat
```

Save and reboot: `sudo reboot`

At boot, the firmware merges the overlay into the Device Tree and the kernel creates the platform device `ds1307-plat`.
Thanks to `MODULE_DEVICE_TABLE(of, ...)`, udev runs `modprobe ds1307_plat` and `probe()` runs.

### Method B: temporary loading without a reboot

Useful while experimenting. Lost after a reboot.

```bash
cd ~/RTC
sudo dtoverlay -d . ds1307-plat     # load the overlay at run time (through configfs)
sudo insmod ./ds1307_plat.ko        # load the driver -> probe()
```

To remove: `sudo rmmod ds1307_plat` (calls `remove()`), then `sudo dtoverlay -r ds1307-plat`.

### Checking that the driver is running

```bash
dmesg | grep ds1307-plat
```

Expected result:

```
ds1307-plat ds1307-plat: DS1307 found at 0x68 on i2c-1, regs: 27 31 14 07 10 10 26 00
ds1307-plat ds1307-plat: registered as rtc0
ds1307-plat ds1307-plat: setting system clock to 2026-10-10T14:31:27 UTC (1791642687)
```

The third line means the kernel has set the system time from the RTC (section 11). With a newly fitted battery there is
also an `oscillator is stopped ...` line: set the time once with `sudo hwclock -w`.

Other checks:

```bash
ls /sys/bus/platform/drivers/ds1307-plat/   # "ds1307-plat" listed -> bound
cat /sys/class/rtc/rtc0/name                # ds1307-plat ds1307-plat
sudo i2cdetect -y 1                         # cell 68 shows "UU" (address claimed by the driver)
```

The Pi Zero W has no other RTC, so the device is normally `/dev/rtc0`. The test commands find the right name themselves
and do not depend on the number.

---

## 9. Testing the driver (step by step)

All commands are run in the code directory on the Pi (`cd ~/RTC`). The sample outputs below come from running exactly
these commands, in this order, on an emulated Pi Zero (section 15). Only the "day of week" byte is shown as returned by
a real chip; the emulated chip reads this byte back wrongly (see the note in section 15). The times printed on your Pi
will of course differ.

The RTC always stores **UTC**. `rtc_test` prints UTC; `hwclock -r` prints the Pi's local time zone (to set Vietnam
time: `sudo timedatectl set-timezone Asia/Ho_Chi_Minh`).

Before testing, connect the Pi to the network so the system time is correct: `timedatectl` must show
`System clock synchronized: yes`.

### 9.1 The driver's `probe()` succeeded

```bash
ls /sys/bus/platform/drivers/ds1307-plat/
cat /sys/class/rtc/rtc0/name
ls /dev/rtc*
```

```
bind  ds1307-plat  module  uevent  unbind
ds1307-plat ds1307-plat
/dev/rtc0
```

The `ds1307-plat` entry in the first listing means the platform device (created from the Device Tree) is bound to the driver.

### 9.2 Reading the time

```bash
sudo ./rtc_test read
sudo hwclock -r
```

```
2026-10-12 09:54:09 UTC (Mon) epoch=1791798849
```

The second command (`hwclock -r`) prints the same moment in the Pi's time zone. The time must match the real time
(`rtc_test` prints UTC, i.e. Vietnam time minus 7 hours).

### 9.3 Setting the time, clock running, year rollover

```bash
sudo ./rtc_test set "2030-12-31 23:59:58"
sudo ./rtc_test watch 4
```

```
set ok, now: 2030-12-31 23:59:58 UTC (Tue) epoch=1924991998
2030-12-31 23:59:58 UTC (Tue) epoch=1924991998
2030-12-31 23:59:59 UTC (Tue) epoch=1924991999
2031-01-01 00:00:00 UTC (Wed) epoch=1924992000
2031-01-01 00:00:01 UTC (Wed) epoch=1924992001
```

Three things are checked at once: the write succeeded, the chip is counting (each line is 1 s later), and the year
rolls over correctly (the weekday also changes from Tue to Wed).

### 9.4 Leap year

```bash
sudo ./rtc_test set "2028-02-28 23:59:59"
sleep 2; sudo ./rtc_test read
```

```
set ok, now: 2028-02-28 23:59:59 UTC (Mon) epoch=1835395199
2028-02-29 00:00:01 UTC (Tue) epoch=1835395201
```

2028 is a leap year, so 28 February must be followed by 29 February, not 1 March.

### 9.5 Rejecting out-of-range times

The DS1307 stores only two year digits (00–99); the driver declares the range 2000–2099.

```bash
sudo ./rtc_test set "2100-01-01 00:00:00"
sudo ./rtc_test set "1999-12-31 23:59:59"
```

```
RTC_SET_TIME: Numerical result out of range
RTC_SET_TIME: Numerical result out of range
```

The error is **correct**: the rtc core blocks out-of-range values (`-ERANGE`) before calling the driver's `set_time()`.

### 9.6 BCD registers

```bash
sudo ./rtc_test set "2026-10-10 20:35:00"
cat /sys/class/rtc/rtc0/regs
```

```
set ok, now: 2026-10-10 20:35:00 UTC (Sat) epoch=1791664500
00 35 20 07 10 10 26 00
```

Meaning of the 8 bytes (registers 0x00 → 0x07, BCD, so they read directly as decimal):

```
00 35 20 07 10 10 26 00
 |  |  |  |  |  |  |  └ control (SQW off)
 |  |  |  |  |  |  └ year 26
 |  |  |  |  |  └ month 10
 |  |  |  |  └ date 10
 |  |  |  └ day of week (1 = Sun … 7 = Sat; 10/10/2026 is a Saturday)
 |  |  └ hours 20 (bit 6 = 0: 24-hour mode)
 |  └ minutes 35
 └ seconds 00 (bit 7 = CH = 0: oscillator running)
```

The seconds may read `01` or `02` if the second command is typed slowly.

### 9.7 SQW square wave

```bash
echo 1 | sudo tee /sys/class/rtc/rtc0/sqw
cat /sys/class/rtc/rtc0/sqw
cat /sys/class/rtc/rtc0/regs
echo off | sudo tee /sys/class/rtc/rtc0/sqw
```

```
1
1
00 35 20 07 10 10 26 10
off
```

The last byte (control register) changes from `00` to `10`: SQWE bit = 1, RS = 00 → 1 Hz. Valid values: `off`, `1`,
`4096`, `8192`, `32768`. Any other value (for example `1234`) is rejected with `Invalid argument`.

To **see** the pulses (optional): power off and connect **Pi pin 1 (3.3 V) → 1 kΩ resistor → LED long leg → LED short leg
→ module SQ pin**. Power on again and run `echo 1 | sudo tee /sys/class/rtc/rtc0/sqw`: the LED blinks once per second.
SQW is an open-drain output, so the LED lights while SQW is low.

### 9.8 56-byte NVRAM (kept by the battery during a power loss)

```bash
NV=$(ls /sys/bus/nvmem/devices/ds1307_nvram*/nvmem)
echo "Xin chao DS1307" | sudo dd of=$NV bs=56 count=1 status=none
sudo hexdump -C $NV
```

```
00000000  58 69 6e 20 63 68 61 6f  20 44 53 31 33 30 37 0a  |Xin chao DS1307.|
00000010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000030  00 00 00 00 00 00 00 00                           |........|
00000038
```

The size is exactly 0x38 = 56 bytes (DS1307 registers 0x08–0x3F). The string is still there after the power loss in section 10.

### 9.9 Restoring the correct time (REQUIRED before step 9.10)

```bash
sudo ./rtc_test sync
sudo ./rtc_test diff
```

```
synced from system time: 2026-10-12 09:54:16 UTC (Mon) epoch=1791798856
0
```

`diff` is the RTC offset from the system time in seconds: `0` or `±1` passes. `sudo hwclock -w` can be used instead of
`rtc_test sync`.

> Why it is required: every time the driver registers `rtc0`, the kernel sets the system time from the RTC. If the RTC
> still holds a test time (for example 2030) when step 9.10 is run, the Pi's clock jumps to 2030 until NTP corrects it.

### 9.10 `remove()` and `probe()`: unload and reload the driver

```bash
sudo rmmod ds1307_plat
ls /dev/rtc*
dmesg | tail -n 1
```

```
ls: cannot access '/dev/rtc*': No such file or directory
ds1307-plat ds1307-plat: removing rtc0
```

`remove()` ran and `/dev/rtc0` is gone. Wait a few seconds, then reload:

```bash
sudo modprobe ds1307_plat        # if installed with Method A (make install)
# or: sudo insmod ./ds1307_plat.ko   (Method B)
dmesg | grep ds1307-plat | tail -n 3
sudo ./rtc_test diff
```

```
ds1307-plat ds1307-plat: DS1307 found at 0x68 on i2c-1, regs: 20 54 09 02 12 10 26 00
ds1307-plat ds1307-plat: registered as rtc0
ds1307-plat ds1307-plat: setting system clock to 2026-10-12T09:54:20 UTC (1791798860)
0
```

`probe()` runs again and `/dev/rtc0` is back. The `regs` bytes are the time held by the chip (09:54:20, Monday 12/10/26),
and `diff` is still ≈ 0: the DS1307 kept counting while no driver was loaded.

If `rmmod` reports `Module ds1307_plat is in use`, a program has `/dev/rtc0` open (usually a running `hwclock`).
Wait a few seconds and try again.

---

## 10. Power-loss test (the most important one)

1. Make sure the RTC is correct: `sudo ./rtc_test sync`, then `sudo ./rtc_test read`. If the NVRAM string from section 9.8 was written, keep it to check it as well.
2. Shut down: `sudo poweroff`. Wait until the ACT LED is fully off, then **unplug the power**.
3. Wait **at least 10 minutes**. The CR2032 keeps the DS1307 running meanwhile.
4. Plug the power back in, log in over SSH and run **immediately**:

```bash
sudo ./rtc_test read     # RTC time: must equal the real time (compare with a phone, in UTC)
sudo ./rtc_test diff     # after NTP has synchronised: 0..2 s offset
sudo hexdump -C /sys/bus/nvmem/devices/ds1307_nvram*/nvmem   # the string "Xin chao DS1307" is still there
```

If the RTC shows the correct time, the chip kept counting on the battery throughout the power loss. **Test passed.**
`dmesg | grep ds1307-plat` shows the line `setting system clock to ...`: at boot, before the network was up, the Pi took
its time from the DS1307.

Extra test (optional), simulating a flat battery: power off, **remove the battery**, wait 1 minute and power on again
(battery still out). The DS1307 normally stops its oscillator (CH bit = 1); then:

```
$ dmesg | grep ds1307-plat
ds1307-plat ds1307-plat: oscillator is stopped (new/empty battery?): set the time with 'hwclock -w'
$ sudo ./rtc_test read
RTC_RD_TIME: Invalid argument
```

The driver reports an error instead of returning a wrong time. Set the time again with `sudo ./rtc_test sync` (clears the
CH bit and restarts the chip), then power off and refit the battery. If the chip still returns a time (no error), the
time is simply no longer correct, which is also a normal result.

The DS1307's accuracy depends on the crystal; it usually drifts by a few seconds per day.

---

## 11. Taking the time from the RTC at boot without a network

No extra configuration is needed. The Raspberry Pi kernel enables `CONFIG_RTC_HCTOSYS` with the device `rtc0`. When the
driver registers `rtc0` at boot, the rtc core sets the system time from the RTC; this is the `setting system clock to ...`
line in `dmesg`. The Pi Zero W has no other RTC, so the DS1307 is always `rtc0`.

The opposite direction is automatic too: once the network is up and NTP has synchronised, the kernel writes the system
time to `rtc0` every 11 minutes (`CONFIG_RTC_SYSTOHC`).

The Pi OS `fake-hwclock` package can stay installed. It only sets the time when the saved time is newer than the current
one, so it does not overwrite the correct time from the RTC.

---

## 12. Uninstalling

```bash
sudo nano /boot/firmware/config.txt      # delete the line dtoverlay=ds1307-plat
cd ~/RTC && sudo make uninstall && make clean
sudo reboot
```

---

## 13. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `i2cdetect` does not show `68` | VCC at 3.3 V instead of 5 V; SDA/SCL swapped; loose wire; I2C not enabled (`ls /dev/i2c-1`); bad solder joint on the Pi header; battery > VCC/1.25. VCC–GND on the module must read ≈ 5 V. |
| `50` appears but not `68` | Faulty DS1307 chip or crystal (rare), or VCC too low |
| `dmesg`: `no DS1307 answering at 0x68 ... err -121` | The chip does not ACK. Check section 6 again, then reload the driver as in the next rows. |
| `dmesg`: `cannot claim address 0x68 ... -16` | The address is claimed by another driver. Remove `dtoverlay=i2c-rtc,...` from `config.txt`, or run `sudo rmmod rtc_ds1307`. |
| `dmesg`: `no DS1307 at 0x68 ... (bus reads all 0xff)` | No chip answers on the bus: check the SDA/SCL wires and the 5 V supply. Then reload the driver: `sudo rmmod ds1307_plat && sudo modprobe ds1307_plat` |
| No RTC device, and no error in `dmesg` | `probe()` may be deferred (`-EPROBE_DEFER`) because the I2C bus is not enabled. See why with `sudo cat /sys/kernel/debug/devices_deferred`. `I2C bus not ready yet` means `dtparam=i2c_arm=on` is missing. |
| No `ds1307-plat` line at all in `dmesg` | Overlay not loaded: check `ls /proc/device-tree/ds1307-plat` and the `.dtbo` file name in the overlays folder. With Method B, `insmod` must come after `dtoverlay`. |
| `insmod: ERROR: ... Invalid module format` | Module built with headers for another kernel. `full-upgrade`, `reboot`, then `make clean && make`. |
| `make`: `/lib/modules/.../build: No such file or directory` | `linux-headers-rpi-v6` not installed, or the headers do not match the kernel (section 5). |
| `rtc_test read`: `Invalid argument` | CH bit = 1, oscillator stopped (new or flat battery). Set the time: `sudo ./rtc_test sync`. |
| After a power loss the RTC shows 2000-01-01 or `Invalid argument` | Battery flat or reversed; charging circuit not removed and the battery damaged; poor contact in the battery holder. |
| RTC runs much too fast or slow | Poor-quality crystal; keep the module away from noise sources. |
| Module or battery gets hot | **Unplug immediately.** The charging circuit is charging the CR2032 (section 3.1). |

---

## 14. Code explanation

### 14.1 Why a platform driver for an I2C chip?

In the Linux driver model the DS1307 sits on the I2C bus, so it would normally be written as an `i2c_driver`. Here the
driver follows the **platform driver** model (`platform_driver` + Device Tree + `probe()`/`remove()`):

- The Device Tree declares a **platform node** at the root of the tree (`/ds1307-plat`), not inside the I2C node.
- The node describes the hardware with properties: `compatible` (to match the driver), `i2c-bus` (phandle to the SoC's I2C1 controller) and `i2c-address` (0x68).
- The kernel creates a `struct platform_device` from the node. When the `compatible` string matches the `of_match_table`, the kernel calls `probe()`.
- In `probe()`, the driver gets the I2C bus from the Device Tree and claims the address on that bus.

This follows the "platform driver with Device Tree" pattern and also shows how a driver obtains a resource described by
the Device Tree (here, an I2C bus).

### 14.2 Flow

```
config.txt: dtoverlay=ds1307-plat
        │  (firmware merges the overlay into the Device Tree)
        ▼
Device Tree: /ds1307-plat { compatible = "edu,ds1307-platform"; i2c-bus = <&i2c_arm>; ... }
        │  (kernel creates a platform_device; udev modprobe from alias "of:...")
        ▼
platform_driver_register()  ──compatible matches──►  ds_probe(pdev)
        │
        ├─ of_parse_phandle("i2c-bus") + of_get_i2c_adapter_by_node()
        │       └─ bus not there yet → return -EPROBE_DEFER (kernel retries later)
        ├─ devm_i2c_new_dummy_device(0x68)       → claim the address ("UU")
        ├─ read registers 0x00..0x07             → check the chip answers, CH bit
        ├─ devm_rtc_allocate_device() + ops      → read_time / set_time
        ├─ rtc_add_group()                       → sysfs: sqw, regs
        ├─ devm_rtc_register_device()            → /dev/rtcN, /sys/class/rtc/rtcN
        └─ devm_rtc_nvmem_register()             → /sys/bus/nvmem/devices/ds1307_nvram0

User space: hwclock / rtc_test ──ioctl(RTC_RD_TIME)──► rtc core ──► ds_read_time()
                                                              └─ i2c_transfer(): write 0x00, read 7 BCD bytes

rmmod ds1307_plat ──► ds_remove() ──► devm frees in reverse: nvmem → rtc → i2c client → i2c_put_adapter()
```

### 14.3 Key technical points

| Point | Implementation |
|---|---|
| Consistent time read | The 7 registers are read in **one** I2C transaction (pointer write + repeated START + read). The DS1307 latches the values at START, so there is no "seconds jump" between bytes. |
| BCD format | `bcd2bin()` when reading, `bin2bcd()` when writing |
| 12/24-hour mode | Both modes are read correctly; writes always use 24-hour mode |
| CH (Clock Halt) bit | CH = 1 → `read_time` returns `-EINVAL` (time not trustworthy). `set_time` writes CH = 0 to restart the oscillator. |
| Year range | `range_min/max` = 2000..2099; the rtc core rejects out-of-range values itself (`-ERANGE`) |
| Probe order | I2C bus not ready → `-EPROBE_DEFER`; the kernel retries automatically |
| Synchronisation | A `mutex` protects read-modify-write operations (sysfs `sqw`, NVRAM) |
| Resource management | Everything uses `devm_*` / `devm_add_action_or_reset()`: a failure part-way through `probe()`, or `remove()`, frees everything in the right order |
| Kernel compatibility | `.remove` returns `void` (kernel ≥ 6.11), `.remove_new` (6.3–6.10), `int` (< 6.3) |

---

## 15. Testing done

The code was tested with the **real Raspberry Pi kernel** (`raspberrypi/linux` sources, `bcmrpi_defconfig` for
Pi Zero / Zero W, ARMv6) on an **emulated Pi Zero** (QEMU 10.2, `raspi0` machine, BCM2835 I2C controller) with an emulated
DS1307-compatible RTC chip at address 0x68 on the I2C1 bus, then on the **real Raspberry Pi Zero W** with a Tiny RTC module
(kernel 6.18.50+rpt-rpi-v6).

| Item | Result |
|---|---|
| Build with Pi kernels **6.6**, **6.12**, **6.18** (`W=1`) | No warnings |
| `checkpatch.pl` | 0 errors; only warnings about `LINUX_VERSION_CODE` (intentional, to support several kernel versions) |
| Overlay merged into `bcm2708-rpi-zero-w.dtb` | Correct: node `/ds1307-plat` points to `i2c@7e804000` (I2C1, pins GPIO2/GPIO3) |
| All commands of sections 9.1 → 9.10, in order | All results as described |
| Overlay loaded at boot and at run time through configfs (as `dtoverlay` does), kernels 6.12 and 6.18 | `probe()` succeeds both ways |
| Driver `insmod`-ed **before** the I2C driver | `-EPROBE_DEFER`, then probes successfully once the bus is ready |
| `rmmod`/`insmod`, sysfs `unbind`/`bind`, removing the overlay at run time | `remove()` runs, `/dev/rtc0` disappears and comes back, no kernel errors |
| CH bit = 1 (emulating a new or flat battery) | Driver warns; reading the time returns `Invalid argument`; setting the time clears CH and the clock runs again |
| No chip on the bus | `probe()` fails cleanly with `-ENODEV`; `rmmod` works normally |
| `i2cdetect -y 1` while the driver is loaded | Cell 0x68 shows `UU` |
| Real Pi Zero W, power-loss test (section 10) | Time and NVRAM string kept through a power loss of more than 10 minutes |

**Note on the "day of week" byte:** QEMU's emulated chip recalculates the weekday its own way, so it reads back wrongly
(for example `04` instead of `07` for 10/10/2026). The emulator's I2C bus trace shows that the driver writes the correct
`0x07` to register 0x03. A real DS1307 stores the value as written, so the real Pi shows `07` as in section 9.6.
