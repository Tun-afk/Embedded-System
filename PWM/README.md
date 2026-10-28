# Platform Driver tự viết cho khối PWM của SoC BCM2835 – Raspberry Pi Zero W

Driver theo mẫu **"5) Platform Driver"**: thiết bị **tích hợp trong SoC**, mô tả bằng **Device Tree**, interface `platform_driver` + `probe()` / `remove()`.

| | |
|---|---|
| Phần cứng | Khối PWM trong BCM2835, `pwm@7e20c000`, 2 kênh, clock 50 MHz |
| Driver | `bcm_pwm_plat.c` → `bcm_pwm_plat.ko` |
| Overlay | `bcm-pwm-plat-overlay.dts` → `bcm-pwm-plat.dtbo` |
| `compatible` | `"edu,bcm2835-pwm-plat"` |
| Giao diện | PWM framework chuẩn của Linux: `/sys/class/pwm/pwmchip0` |
| Ngõ ra | Kênh 0 (PWM0) trên **GPIO18, chân 12** |

**Quan hệ với driver của Pi: thay hẳn `pwm-bcm2835`.** Device Tree của Pi đã có sẵn node `pwm@7e20c000` (mặc định tắt) với `compatible = "brcm,bcm2835-pwm"`, ứng với driver gốc `pwm-bcm2835`. Overlay của bài này **sửa thẳng node đó**: đổi `compatible` sang chuỗi của bạn, bật node, và chuyển GPIO18 sang chức năng PWM. Driver gốc không còn khớp nên không được nạp; khối PWM hoàn toàn do driver của bạn điều khiển.

| File trong gói | Vai trò |
|---|---|
| `bcm_pwm_plat.c` | Code driver |
| `bcm-pwm-plat-overlay.dts` | Device Tree overlay (đè node `&pwm`) |
| `Makefile` | Build module `.ko` + overlay `.dtbo`, `make install` |
| `README.md`, `wiring-led-pwm.png/.svg` | Hướng dẫn và sơ đồ, để trên máy tính |

---

## 1. Linh kiện

| Linh kiện | Số lượng |
|---|---|
| Raspberry Pi Zero W đã hàn header, chạy Raspberry Pi OS 32-bit | 1 |
| LED 5 mm | 1 |
| Điện trở 330 Ω (220–470 Ω đều được) | 1 |
| Breadboard + 2 dây jumper cái–đực | |

Không cần nguồn riêng, tụ hay transistor: LED chỉ ăn khoảng 3–6 mA, GPIO của Pi cấp được.

---

## 2. Lắp phần cứng

![Sơ đồ nối LED](wiring-led-pwm.png)

| Từ chân Pi | Qua | Về chân Pi |
|---|---|---|
| **Chân 12** (GPIO18 = PWM0) | điện trở 330 Ω → LED | **Chân 14** (GND) |

1. **Tắt Pi, rút nguồn.**
2. Cắm LED lên breadboard, mỗi chân một hàng.
3. Nhận chân LED: **chân dài = (+) anode**, **chân ngắn = (−) cathode** (phía vành nhựa bị vát phẳng).
4. Một đầu điện trở cùng hàng với chân dài; đầu kia nối dây về **chân 12**. Chân ngắn nối dây về **chân 14**.
5. Chân 12 và 14 đều ở hàng sát mép board (chân 1 là pad vuông, gần khe thẻ SD; hàng mép là các chân chẵn).
6. Kiểm tra: LED đúng chiều, có điện trở nối tiếp, không dây nào cắm vào chân 2, 4 (5 V).
7. Cắm nguồn.

4 dây DS1307 (chân 3–6) và LED của bài Timer (chân 9, 11) có thể giữ nguyên, không xung đột.

**Test LED trước khi dùng driver** (tùy chọn): `pinctrl set 18 op dh` thì LED sáng, `pinctrl set 18 op dl` thì tắt. Không sáng là LED bị ngược hoặc dây lỏng. Test xong thì **khởi động lại Pi** trước khi nạp driver (lệnh `pinctrl` đã chuyển chân 18 sang chế độ output thường).

---

## 3. Chuẩn bị Raspberry Pi OS

Nếu đã làm cho bài khác (DS1307, Timer) thì chỉ cần làm bước kiểm tra `config.txt` ở cuối mục này.

```bash
sudo apt update
sudo apt full-upgrade -y
sudo reboot
# sau khi khởi động lại:
sudo apt install -y build-essential device-tree-compiler linux-headers-rpi-v6
ls -d /lib/modules/$(uname -r)/build      # phải tồn tại
```

Kiểm tra `config.txt` **không** có overlay PWM gốc của Pi, vì overlay đó dùng cùng node `&pwm`:

```bash
grep -n -e "dtoverlay=pwm" -e "audremap" /boot/firmware/config.txt
```

Lệnh không in gì là đúng. Nếu có dòng nào, thêm dấu `#` ở đầu dòng đó để tắt.

---

## 4. Chép code lên Pi

Chỉ cần **3 file**: `bcm_pwm_plat.c`, `bcm-pwm-plat-overlay.dts`, `Makefile`.

Từ máy tính (Windows: mở PowerShell trong thư mục chứa các file):

```bash
ssh <user>@<hostname>.local "mkdir -p ~/bcm-pwm-plat"
scp bcm_pwm_plat.c bcm-pwm-plat-overlay.dts Makefile <user>@<hostname>.local:~/bcm-pwm-plat/
```

---

## 5. Build

```bash
cd ~/bcm-pwm-plat
make
```

Kết quả: `bcm_pwm_plat.ko` (kernel module) và `bcm-pwm-plat.dtbo` (overlay đã biên dịch).

---

## 6. Nạp overlay và driver

### Cách A (khuyên dùng): cài cố định

```bash
sudo make install
sudo nano /boot/firmware/config.txt
```

Thêm vào **cuối file** (dưới dòng `[all]`):

```
dtoverlay=bcm-pwm-plat
```

Lưu và `sudo reboot`. Khi khởi động, udev tự `modprobe bcm_pwm_plat` theo `compatible`, rồi `probe()` chạy. LED chưa sáng: PWM chỉ bắt đầu xuất xung khi bạn bật ở mục 8.

### Cách B: nạp tạm, không cần khởi động lại (mất khi reboot)

```bash
cd ~/bcm-pwm-plat
sudo dtoverlay -d . bcm-pwm-plat
sudo insmod ./bcm_pwm_plat.ko
```

---

## 7. Kiểm tra `probe()` đã chạy

```bash
sudo dmesg | grep bcm-pwm-plat
```

```
bcm-pwm-plat 2020c000.pwm: PWM ready: 2 channels, clock 50000000 Hz (20 ns/tick), CTL=0x00000000
```

---

## 8. Test driver

Đọc file trong `/sys` thì không cần quyền gì, nhưng **ghi** vào đó cần quyền root. Vì vậy mọi lệnh ghi đều có dạng:

```bash
echo <giá trị> | sudo tee <file> > /dev/null
```

`sudo tee` ghi giá trị vào file với quyền root; `> /dev/null` chỉ để `tee` không in lại giá trị ra màn hình. **Không** dùng `sudo echo <giá trị> > <file>`: dấu `>` được shell của user thường xử lý trước khi `sudo` chạy, nên vẫn báo `Permission denied`. Các lệnh `rmmod`, `modprobe`, `insmod`, `dmesg` cũng cần `sudo`.

Biến `$P` (thư mục driver) được đặt ở mục 8.3, biến `$C` (`/sys/class/pwm/pwmchip0`) ở mục 8.4. Nếu mở terminal mới, chạy lại lệnh đặt biến đó trước.

Kết quả mẫu là output thật khi chạy đúng các lệnh này trên Pi Zero giả lập (mục 12). Riêng dòng báo lỗi được ghi theo `tee` của Raspberry Pi OS (`tee: <file>: Invalid argument`); `tee` rút gọn của máy giả lập in `I/O error` thay cho `Invalid argument`.

### 8.1 Driver tự viết đã thay driver của Pi

```bash
cat /proc/device-tree/soc/pwm@7e20c000/compatible; echo
lsmod | grep -i pwm
ls /sys/bus/platform/drivers/bcm-pwm-plat/
```

```
edu,bcm2835-pwm-plat
bcm_pwm_plat 12288 0 - Live 0xbf006000 (O)
2020c000.pwm  bind  module  uevent  unbind
```

- Node `pwm@7e20c000` giờ mang `compatible` của bạn (gốc là `brcm,bcm2835-pwm`).
- `lsmod` chỉ có `bcm_pwm_plat`, **không có `pwm_bcm2835`**: driver gốc không được nạp.
- Thiết bị `2020c000.pwm` đã gắn với driver `bcm-pwm-plat`.

Trên Pi, `lsmod` in theo dạng cột (`bcm_pwm_plat  12288  0`), nội dung như nhau.

### 8.2 GPIO18 đã chuyển sang chức năng PWM

```bash
sudo sh -c 'grep "pin 18" /sys/kernel/debug/pinctrl/*/pinmux-pins'
```

```
pin 18 (gpio18): 2020c000.pwm (GPIO UNCLAIMED) function alt5 group gpio18
```

Lệnh cần `sudo sh -c '...'` vì thư mục `/sys/kernel/debug` chỉ root mới vào được, kể cả để dấu `*` tìm ra tên file. Kernel mới hơn (6.18) in dạng `pin 18 (gpio18): device 2020c000.pwm function alt5 group gpio18`, cùng ý nghĩa.

Pinctrl trong overlay (`pinctrl-0 = <&pwm0_gpio18>`) đã chuyển chân 18 sang **ALT5 = PWM0** và giao cho thiết bị `2020c000.pwm`. Việc này do driver core làm ngay trước khi gọi `probe()`.

### 8.3 Clock và thanh ghi ban đầu

```bash
P=$(ls -d /sys/bus/platform/drivers/bcm-pwm-plat/*.pwm); cat $P/clock_hz; cat $P/regs
ls /sys/class/pwm/; cat /sys/class/pwm/pwmchip0/npwm
```

```
50000000
CTL=0x00000000 STA=0x00000000 RNG1=0 DAT1=0 RNG2=0 DAT2=0
pwmchip0
2
```

Clock PWM 50 MHz (1 xung = 20 ns), lấy từ `assigned-clock-rates` trong Device Tree. Driver đăng ký 1 `pwmchip` có 2 kênh. `regs` và `clock_hz` là 2 thuộc tính driver tự thêm để xem thanh ghi.

### 8.4 Xuất xung: chu kỳ 1 ms (1 kHz), duty 25%

```bash
C=/sys/class/pwm/pwmchip0; echo 0 | sudo tee $C/export > /dev/null; ls $C/pwm0
echo 1000000 | sudo tee $C/pwm0/period > /dev/null; echo 250000 | sudo tee $C/pwm0/duty_cycle > /dev/null; echo 1 | sudo tee $C/pwm0/enable > /dev/null; cat $P/regs
```

```
capture  duty_cycle  enable  period  polarity  power  uevent
CTL=0x00000081 STA=0x00000000 RNG1=50000 DAT1=12500 RNG2=0 DAT2=0
```

**LED sáng mờ.** Kiểm tra thanh ghi:
- `RNG1 = 50000` = 1 000 000 ns / 20 ns: chu kỳ 1 ms.
- `DAT1 = 12500` = 250 000 ns / 20 ns: mức cao 25%.
- `CTL = 0x81` = bit 7 `MSEN` (chế độ mark-space) + bit 0 `PWEN` (bật kênh 0).

### 8.5 Sáng tối đa và tắt

```bash
echo 1000000 | sudo tee $C/pwm0/duty_cycle > /dev/null; cat $P/regs
echo 0 | sudo tee $C/pwm0/duty_cycle > /dev/null; cat $P/regs
```

```
CTL=0x00000081 STA=0x00000000 RNG1=50000 DAT1=50000 RNG2=0 DAT2=0
CTL=0x00000081 STA=0x00000000 RNG1=50000 DAT1=0 RNG2=0 DAT2=0
```

Duty 100% (`DAT1 = RNG1`) thì LED **sáng nhất**; duty 0 thì LED **tắt**.

### 8.6 LED sáng dần rồi tối dần (3 lần)

```bash
for i in 1 2 3; do for d in $(seq 0 50000 1000000) $(seq 1000000 -50000 0); do echo $d | sudo tee $C/pwm0/duty_cycle > /dev/null; sleep 0.02; done; done; cat $P/regs
```

```
CTL=0x00000081 STA=0x00000000 RNG1=50000 DAT1=0 RNG2=0 DAT2=0
```

LED "thở": sáng dần, tối dần. Mỗi bước gọi `sudo` một lần nên trên Pi Zero mỗi vòng mất vài giây; cả lệnh chạy khoảng 15–30 giây. Đây là bằng chứng trực quan nhất rằng driver điều khiển được khối PWM.

### 8.7 Đảo cực tính

```bash
echo 0 | sudo tee $C/pwm0/enable > /dev/null; echo inversed | sudo tee $C/pwm0/polarity > /dev/null; echo 250000 | sudo tee $C/pwm0/duty_cycle > /dev/null; echo 1 | sudo tee $C/pwm0/enable > /dev/null; cat $P/regs
echo 0 | sudo tee $C/pwm0/enable > /dev/null; echo normal | sudo tee $C/pwm0/polarity > /dev/null; echo 1 | sudo tee $C/pwm0/enable > /dev/null
```

```
CTL=0x00000091 STA=0x00000000 RNG1=50000 DAT1=12500 RNG2=0 DAT2=0
```

`CTL = 0x91`: thêm bit 4 `POLA`. Cùng duty 25% nhưng xung bị đảo nên **LED sáng hơn hẳn** (mức cao 75%). Phải tắt kênh trước khi đổi cực tính (yêu cầu của PWM framework). Lệnh thứ hai trả cực tính về bình thường.

### 8.8 Từ chối chu kỳ quá ngắn

```bash
echo 0 | sudo tee $C/pwm0/duty_cycle > /dev/null; echo 20 | sudo tee $C/pwm0/period > /dev/null
cat $C/pwm0/period
```

```
tee: /sys/class/pwm/pwmchip0/pwm0/period: Invalid argument
1000000
```

20 ns chỉ bằng 1 xung clock; khối PWM cần tối thiểu 2 xung (40 ns). Hàm `apply()` của driver trả `-EINVAL`, và chu kỳ cũ được giữ nguyên.

### 8.9 `remove()` và `probe()`

Trước khi gỡ driver phải **unexport** kênh đang dùng. Nếu còn export, `rmmod` sẽ báo module đang được dùng (kernel giữ tham chiếu tới module).

```bash
echo 0 | sudo tee $C/pwm0/enable > /dev/null; echo 0 | sudo tee $C/unexport > /dev/null; sudo rmmod bcm_pwm_plat; sudo dmesg | tail -n 1; ls /sys/class/pwm/ | wc -l
```

```
bcm-pwm-plat 2020c000.pwm: removing PWM (outputs will be switched off)
0
```

`remove()` chạy, `pwmchip0` biến mất, cả hai kênh bị tắt (`CTL = 0`). Nạp lại:

```bash
sudo modprobe bcm_pwm_plat            # Cách A
# hoặc: sudo insmod ./bcm_pwm_plat.ko   (Cách B)
sudo dmesg | tail -n 1
```

```
bcm-pwm-plat 2020c000.pwm: PWM ready: 2 channels, clock 50000000 Hz (20 ns/tick), CTL=0x00000000
```


---

## 9. Gỡ cài đặt (trả lại driver gốc của Pi)

```bash
sudo nano /boot/firmware/config.txt     # xóa dòng dtoverlay=bcm-pwm-plat
cd ~/bcm-pwm-plat && sudo make uninstall && make clean
sudo reboot
```

Sau khi reboot, node `&pwm` trở lại `compatible = "brcm,bcm2835-pwm"` (trạng thái tắt như mặc định). Muốn dùng driver PWM gốc thì thêm `dtoverlay=pwm`.

---

## 10. Xử lý lỗi thường gặp

| Hiện tượng | Nguyên nhân / Cách sửa |
|---|---|
| `make`: `/lib/modules/.../build: No such file or directory` | Chưa cài `linux-headers-rpi-v6`, hoặc kernel và headers lệch phiên bản (`full-upgrade` rồi `reboot`). |
| `insmod: ... Invalid module format` | Module build cho kernel khác. `make clean && make` lại sau khi reboot. |
| `dmesg` không có dòng `bcm-pwm-plat` | Overlay chưa nạp. Kiểm tra `cat /proc/device-tree/soc/pwm@7e20c000/compatible`. Với Cách B, chạy `dtoverlay` trước `insmod`. |
| `lsmod` có `pwm_bcm2835`, không có `bcm_pwm_plat` | Trong `config.txt` còn `dtoverlay=pwm` hoặc `pwm-2chan`. Tắt dòng đó, reboot. |
| `cannot get PWM clock` | Node `&pwm` thiếu `clocks` (DTB không đúng cho Pi Zero). Kiểm tra `ls /proc/device-tree/soc/pwm@7e20c000/`. |
| LED không sáng nhưng `regs` đúng như mục 8.4 | LED ngược / sai chân (phải là **chân 12**). Kiểm tra mục 8.2 có `function alt5`. Nếu đã dùng `pinctrl set 18 ...` thì reboot. |
| LED sáng nhưng không đổi độ sáng | Chu kỳ quá dài (vài trăm ms trở lên) nên LED nháy thay vì mờ. Dùng `period` ≤ 1 000 000 (1 ms). |
| `echo ... > duty_cycle: Invalid argument` | `duty_cycle` lớn hơn `period`. Đặt `period` trước, `duty_cycle` sau. |
| `rmmod bcm_pwm_plat`: module đang được dùng | Còn kênh PWM được export. `echo 0 \| sudo tee /sys/class/pwm/pwmchip0/unexport` rồi thử lại. |
| `... /sys/...: Permission denied` | Ghi vào `/sys` mà không có quyền root. Dùng `echo <giá trị> \| sudo tee <file>`, không dùng `echo ... > file` hay `sudo echo ... > file`. |

---

## 11. Giải thích code (dùng cho báo cáo)

### 11.1 Luồng hoạt động

```
config.txt: dtoverlay=bcm-pwm-plat        firmware ghép overlay vào Device Tree
        │
        ▼
Node có sẵn /soc/pwm@7e20c000 sau khi bị overlay "đè":
    compatible = "edu,bcm2835-pwm-plat";   // đổi (gốc: "brcm,bcm2835-pwm")
    status     = "okay";                   // bật (gốc: "disabled")
    pinctrl-0  = <&pwm0_gpio18>;           // thêm: GPIO18 ALT5
    reg, clocks, assigned-clock-rates      // giữ nguyên từ DTB gốc của Pi
        │  kernel tạo platform_device; driver core áp dụng pinctrl;
        │  udev modprobe theo alias "of:...Cedu,bcm2835-pwm-plat"
        ▼
platform_driver_register()  ──khớp compatible──►  probe(pdev)
        ├─ devm_platform_ioremap_resource()   → thanh ghi 0x7e20c000
        ├─ devm_clk_get_enabled()             → clock PWM 50 MHz
        ├─ devm_add_action_or_reset(hw_off)   → hàm tắt PWM khi gỡ
        └─ devm_pwmchip_add()                 → /sys/class/pwm/pwmchip0

echo ... > /sys/class/pwm/pwmchip0/pwm0/...  ──►  PWM framework  ──►  bcm_pwm_apply()
        └─ ns → số xung 20 ns; ghi RNG1, DAT1; bật MSEN + PWEN trong CTL

rmmod  ──►  remove()  ──►  devm (ngược): gỡ pwmchip → hw_off (CTL = 0) → tắt clock → bỏ iomap
```

### 11.2 Các điểm kỹ thuật chính

| Điểm | Cách làm |
|---|---|
| "Đè" driver gốc | Overlay sửa **node có sẵn** `&pwm` thay vì thêm node mới, nên dùng lại đúng `reg`, `clocks` mà Raspberry Pi đã khai báo. Chỉ đổi `compatible` là đủ để driver gốc không khớp nữa. |
| Tài nguyên từ DT | Thanh ghi (`reg`), clock (`clocks` + `assigned-clock-rates`), chân ra (`pinctrl`) đều lấy từ Device Tree, không ghi cứng địa chỉ trong code. |
| Tích hợp kernel | Đăng ký `pwm_chip` với **PWM framework**, nên có sẵn giao diện chuẩn `/sys/class/pwm` mà mọi chương trình Linux đều dùng được. |
| `apply()` | Đổi ns → số xung clock (`mul_u64_u64_div_u64`, không tràn số), giới hạn `duty ≤ period`, ghi `RNGn`/`DATn`, bật `MSEN`+`PWEN` (+`POLA` nếu đảo cực). |
| `get_state()` | Đọc ngược thanh ghi ra ns để framework biết trạng thái thật của phần cứng khi đăng ký. |
| Hai kênh dùng chung `CTL` | Đọc–sửa–ghi `CTL` trong `spinlock` để kênh 0 và kênh 1 không ghi đè bit của nhau (bật cả hai kênh thì `CTL = 0x8181`). |
| Thứ tự gỡ | Hàm tắt phần cứng được đăng ký **trước** `devm_pwmchip_add()`, nên khi gỡ (thứ tự ngược) nó chạy **sau** khi `pwmchip` đã bị gỡ: không ai còn bật lại PWM được nữa. |
| Tương thích kernel | Kernel ≥ 6.9 dùng `devm_pwmchip_alloc()`; kernel cũ hơn nhúng `struct pwm_chip` trong struct riêng. `.remove` trả `void` từ 6.11. |

---

## 12. Đã kiểm thử những gì

Code được kiểm thử với **kernel Raspberry Pi thật** (mã nguồn `raspberrypi/linux`, cấu hình `bcmrpi_defconfig` cho Pi Zero / Zero W, ARMv6) trên **máy Pi Zero giả lập** (QEMU 10.2, máy `raspi0`; có clock manager và pinctrl). QEMU không mô phỏng khối PWM, nên mình thêm một mô hình thanh ghi đơn giản để ghi lại mọi lần driver đọc/ghi thanh ghi PWM.

| Hạng mục | Kết quả |
|---|---|
| Biên dịch với kernel Pi **6.6**, **6.12**, **6.18** (`W=1`) | Không có cảnh báo nào |
| `checkpatch.pl` | 0 lỗi; chỉ còn cảnh báo về `LINUX_VERSION_CODE` (cố ý, để chạy được trên nhiều phiên bản kernel) |
| Overlay ghép vào `bcm2708-rpi-zero-w.dtb` (bằng `dtmerge` của Raspberry Pi) | Node `&pwm` đổi `compatible`, bật, pinctrl GPIO18 |
| Toàn bộ lệnh mục 7 → 8, theo đúng thứ tự, kernel 6.12 và 6.18 | Cho kết quả như mô tả |
| Overlay nạp lúc boot và nạp lúc chạy (configfs, như lệnh `dtoverlay`) | `probe()` thành công; gỡ overlay lúc chạy thì `remove()` chạy |
| Giá trị ghi vào thanh ghi | Đúng datasheet: RNG/DAT theo xung 20 ns, CTL `0x81` / `0x91`, kênh 1 dịch 8 bit (`0x8100`), `rmmod` ghi CTL = 0 |
| Chân GPIO18 | pinctrl báo `function alt5`, thuộc thiết bị `2020c000.pwm` |

**Phần chưa thể kiểm thử thay bạn:** xung PWM thật trên chân 12, vì máy giả lập không có khối PWM thật. Trên Pi thật, hãy xác nhận bằng mắt: LED sáng mờ ở mục 8.4 và "thở" ở mục 8.6.
