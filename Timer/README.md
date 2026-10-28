# Platform Driver tự viết cho System Timer (kênh 1) của SoC BCM2835 – Raspberry Pi Zero W

Driver theo mẫu **"5) Platform Driver"**: thiết bị **tích hợp trong SoC**, mô tả bằng **Device Tree**, interface `platform_driver` + `probe()` / `remove()`.

| | |
|---|---|
| Phần cứng | System Timer trong BCM2835, `timer@7e003000`, **kênh so sánh 1** |
| Driver | `bcm_timer_plat.c` → `bcm_timer_plat.ko` |
| Overlay | `bcm-timer-plat-overlay.dts` → `bcm-timer-plat.dtbo` |
| `compatible` | `"edu,bcm2835-timer-ch1"` |
| Chức năng | Tạo ngắt định kỳ (100 µs – 10 s), đếm số ngắt, đo độ trễ ngắt, nháy LED trên GPIO17 theo từng ngắt |

**Quan hệ với timer của Pi:** driver này **chạy song song**, không thay timer hệ thống. Timer hệ thống `bcm2835_timer` (dùng kênh 3) được biên dịch thẳng vào kernel (built-in) và khởi tạo bằng `TIMER_OF_DECLARE()` từ rất sớm lúc boot, trước cả khi có cơ chế `platform_driver`. Nó là "nhịp tim" của Linux: lập lịch, `sleep` đều dựa vào nó. Muốn thay phải biên dịch lại cả kernel, và sai một chút là Pi không khởi động được.

System Timer có 4 kênh so sánh. Theo tài liệu Device Tree binding của kernel: kênh 0 và 2 do GPU dùng, kênh 1 và 3 dành cho ARM. Linux đã dùng kênh 3, nên driver này dùng **kênh 1** còn trống.

| File trong gói | Vai trò |
|---|---|
| `bcm_timer_plat.c` | Code driver |
| `bcm-timer-plat-overlay.dts` | Device Tree overlay (thêm node `edu-timer`) |
| `Makefile` | Build module `.ko` + overlay `.dtbo`, `make install` |
| `README.md`, `wiring-led-timer.png/.svg` | Hướng dẫn và sơ đồ, để trên máy tính |

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

![Sơ đồ nối LED](wiring-led-timer.png)

| Từ chân Pi | Qua | Về chân Pi |
|---|---|---|
| **Chân 11** (GPIO17) | điện trở 330 Ω → LED | **Chân 9** (GND) |

1. **Tắt Pi, rút nguồn.**
2. Cắm LED lên breadboard, mỗi chân một hàng.
3. Nhận chân LED: **chân dài = (+) anode**, **chân ngắn = (−) cathode** (phía vành nhựa bị vát phẳng).
4. Một đầu điện trở cùng hàng với chân dài; đầu kia nối dây về **chân 11**. Chân ngắn nối dây về **chân 9**.
5. Chân 9 và 11 đều ở hàng phía trong của header (chân 1 là pad vuông, gần khe thẻ SD).
6. Kiểm tra: LED đúng chiều, có điện trở nối tiếp, không dây nào cắm vào chân 1, 2, 4 (nguồn).
7. Cắm nguồn.

4 dây DS1307 (chân 3–6) và LED của bài PWM (chân 12, 14) có thể giữ nguyên, không xung đột.

**Test LED trước khi dùng driver** (tùy chọn): `pinctrl set 17 op dh` thì LED sáng, `pinctrl set 17 op dl` thì tắt. Không sáng là LED bị ngược hoặc dây lỏng. Test xong thì khởi động lại Pi trước khi nạp driver.

---

## 3. Chuẩn bị Raspberry Pi OS

Nếu đã làm cho bài khác (DS1307, PWM) thì bỏ qua mục này.

```bash
sudo apt update
sudo apt full-upgrade -y
sudo reboot
# sau khi khởi động lại:
sudo apt install -y build-essential device-tree-compiler linux-headers-rpi-v6
ls -d /lib/modules/$(uname -r)/build      # phải tồn tại
```

---

## 4. Chép code lên Pi

Chỉ cần **3 file**: `bcm_timer_plat.c`, `bcm-timer-plat-overlay.dts`, `Makefile`.

Từ máy tính (Windows: mở PowerShell trong thư mục chứa các file):

```bash
ssh <user>@<hostname>.local "mkdir -p ~/bcm-timer-plat"
scp bcm_timer_plat.c bcm-timer-plat-overlay.dts Makefile <user>@<hostname>.local:~/bcm-timer-plat/
```

---

## 5. Build

```bash
cd ~/bcm-timer-plat
make
```

Kết quả: `bcm_timer_plat.ko` (kernel module) và `bcm-timer-plat.dtbo` (overlay đã biên dịch).

---

## 6. Nạp overlay và driver

### Cách A (khuyên dùng): cài cố định

```bash
sudo make install
sudo nano /boot/firmware/config.txt
```

Thêm vào **cuối file** (dưới dòng `[all]`):

```
dtoverlay=bcm-timer-plat
```

Lưu và `sudo reboot`. Khi khởi động, udev tự `modprobe bcm_timer_plat` theo `compatible`, rồi `probe()` chạy. **LED bắt đầu nháy 1 lần/giây ngay khi Pi khởi động xong.**

Tham số tùy chọn: `dtoverlay=bcm-timer-plat,period_us=100000` (chu kỳ ngắt khi khởi động), `led_gpio=27` (đổi chân LED).

### Cách B: nạp tạm, không cần khởi động lại (mất khi reboot)

```bash
cd ~/bcm-timer-plat
sudo dtoverlay -d . bcm-timer-plat
sudo insmod ./bcm_timer_plat.ko
```

---

## 7. Kiểm tra `probe()` đã chạy

```bash
sudo dmesg | grep bcm-timer-plat
```

```
bcm-timer-plat 20003000.edu-timer: System Timer channel 1: IRQ 25, period 500000 us, LED on GPIO, counter 4152622 us
```

Số IRQ và giá trị `counter` trên Pi của bạn sẽ khác.

---

## 8. Test driver

Đọc file trong `/sys` thì không cần quyền gì, nhưng **ghi** vào đó cần quyền root. Vì vậy mọi lệnh ghi đều có dạng:

```bash
echo <giá trị> | sudo tee <file> > /dev/null
```

`sudo tee` ghi giá trị vào file với quyền root; `> /dev/null` chỉ để `tee` không in lại giá trị ra màn hình. **Không** dùng `sudo echo <giá trị> > <file>`: dấu `>` được shell của user thường xử lý trước khi `sudo` chạy, nên vẫn báo `Permission denied`. Các lệnh `rmmod`, `modprobe`, `insmod`, `dmesg` cũng cần `sudo`.

Biến `$T` (đường dẫn thư mục driver trong `/sys`) được đặt ở mục 8.1. Nếu mở terminal mới, chạy lại lệnh đặt biến đó trước.

Kết quả mẫu là output thật khi chạy đúng các lệnh này trên Pi Zero giả lập (mục 12). Riêng dòng báo lỗi được ghi theo `tee` của Raspberry Pi OS (`tee: <file>: Invalid argument`); `tee` rút gọn của máy giả lập in `I/O error` thay cho `Invalid argument`.

### 8.1 Thư mục điều khiển của driver

```bash
T=$(ls -d /sys/bus/platform/drivers/bcm-timer-plat/*.edu-timer); echo $T; ls $T
```

```
/sys/bus/platform/drivers/bcm-timer-plat/20003000.edu-timer
counter  driver  driver_override  enable  irq_count  max_latency_us  missed
modalias  of_node  period_us  power  subsystem  supplier:platform:20200000.gpio  uevent
```

Các thuộc tính do driver tạo ra:

| File | Quyền | Ý nghĩa |
|---|---|---|
| `enable` | đọc/ghi | `1` = chạy, `0` = dừng; ghi `1` sẽ đặt lại thống kê |
| `period_us` | đọc/ghi | Chu kỳ ngắt, 100 – 10 000 000 µs |
| `irq_count` | đọc | Số lần ngắt từ lần `enable` gần nhất |
| `missed` | đọc | Số chu kỳ bị lỡ (ngắt tới quá trễ) |
| `max_latency_us` | đọc | Độ trễ ngắt lớn nhất (µs) |
| `counter` | đọc | Bộ đếm 64 bit của System Timer (µs từ lúc cấp nguồn) |

### 8.2 Hai ngắt timer chạy song song

```bash
grep -e timer /proc/interrupts
```

```
 25:          0  ARMCTRL-level  33 Edge      20003000.edu-timer
 27:        424  ARMCTRL-level  35 Edge      timer
```

- `ARMCTRL-level 33` = ngắt GPU số 1 (32 + 1) = **kênh 1, driver của bạn**.
- `ARMCTRL-level 35` = ngắt GPU số 3 = **kênh 3, timer hệ thống của Linux**, vẫn chạy bình thường.

Chạy lại lệnh vài lần: cột số đếm của cả hai dòng đều tăng.

### 8.3 Ngắt định kỳ 0.5 s, LED nháy

```bash
cat $T/period_us $T/enable
echo 1 | sudo tee $T/enable > /dev/null; sleep 2; cat $T/irq_count
```

```
500000
1
4
```

LED đổi trạng thái mỗi lần ngắt, tức nháy 1 lần/giây. Ghi `1` vào `enable` đặt `irq_count` về 0, nên sau 2 giây đếm được khoảng 4 ngắt (2 ngắt/giây). Nếu đọc `irq_count` mà không đặt lại, bạn sẽ thấy số lớn (ví dụ 930): đó là số ngắt kể từ lúc driver được nạp khi Pi khởi động, 930 ngắt ≈ 7 phút 45 giây.

### 8.4 Đổi chu kỳ

```bash
echo 100000 | sudo tee $T/period_us > /dev/null
sleep 1; cat $T/irq_count
```

```
14
```

LED nháy nhanh hẳn (5 lần/giây). `irq_count` không bị đặt lại khi đổi chu kỳ (4 + khoảng 10 = 14); nó chỉ về 0 khi ghi `1` vào `enable`.

### 8.5 Ngắt 1 kHz, đo độ trễ ngắt

```bash
echo 1000 | sudo tee $T/period_us > /dev/null; echo 1 | sudo tee $T/enable > /dev/null; sleep 1; cat $T/irq_count $T/missed $T/max_latency_us
```

```
1005
6
5943
```

- `irq_count` ≈ 1000: đúng 1000 ngắt mỗi giây.
- `missed`: số chu kỳ bị lỡ vì ngắt tới quá trễ (driver tự bù để không trôi nhịp).
- `max_latency_us`: độ trễ lớn nhất từ lúc phần cứng so khớp đến lúc vào hàm phục vụ ngắt (ISR).

Hai số cuối trong kết quả mẫu cao vì **máy giả lập không chạy thời gian thực**. Trên Pi thật, `missed` thường bằng 0 và `max_latency_us` nhỏ hơn nhiều. Ghi lại số đo trên Pi của bạn: đây là số liệu tốt cho tiểu luận. Có thể thử thêm khi Pi đang bận: chạy `yes > /dev/null &`, đo lại, xong thì `kill %1`.

LED lúc này đảo trạng thái 1000 lần/giây nên trông như **sáng mờ liên tục**.

### 8.6 Từ chối giá trị sai

```bash
echo 50 | sudo tee $T/period_us > /dev/null
echo 20000000 | sudo tee $T/period_us > /dev/null
```

```
tee: /sys/bus/platform/drivers/bcm-timer-plat/20003000.edu-timer/period_us: Invalid argument
tee: /sys/bus/platform/drivers/bcm-timer-plat/20003000.edu-timer/period_us: Invalid argument
```

Chu kỳ hợp lệ: 100 µs đến 10 s. Báo lỗi ở đây là đúng.

### 8.7 Dừng timer

```bash
echo 0 | sudo tee $T/enable > /dev/null; cat $T/irq_count; sleep 1; cat $T/irq_count
```

```
1045
1045
```

Số đếm đứng yên, LED tắt.

### 8.8 Bộ đếm 64 bit của System Timer

```bash
cat $T/counter; cat /proc/uptime
```

```
9894455
9.09 4.93
```

`counter` là số µs kể từ khi SoC được cấp nguồn (1 MHz), nên lớn hơn `uptime` của Linux một chút (thời gian firmware khởi động).

### 8.9 `remove()` và `probe()`: gỡ rồi nạp lại

```bash
echo 500000 | sudo tee $T/period_us > /dev/null; echo 1 | sudo tee $T/enable > /dev/null
sudo rmmod bcm_timer_plat; sudo dmesg | tail -n 1; grep -c edu-timer /proc/interrupts
sleep 1; cat /proc/uptime
```

```
bcm-timer-plat 20003000.edu-timer: removing timer (0 interrupts since last enable)
0
10.35 5.90
```

`remove()` đã chạy: ngắt kênh 1 bị gỡ khỏi `/proc/interrupts` (đếm được `0` dòng), LED tắt. `uptime` vẫn tăng, tức timer hệ thống không bị ảnh hưởng. Nạp lại:

```bash
sudo modprobe bcm_timer_plat          # Cách A
# hoặc: sudo insmod ./bcm_timer_plat.ko   (Cách B)
sudo dmesg | tail -n 1
```

```
bcm-timer-plat 20003000.edu-timer: System Timer channel 1: IRQ 25, period 500000 us, LED on GPIO, counter 11209957 us
```

`probe()` chạy lại, LED nháy trở lại với chu kỳ mặc định 0.5 s.

---

## 9. Gỡ cài đặt

```bash
sudo nano /boot/firmware/config.txt     # xóa dòng dtoverlay=bcm-timer-plat
cd ~/bcm-timer-plat && sudo make uninstall && make clean
sudo reboot
```

---

## 10. Xử lý lỗi thường gặp

| Hiện tượng | Nguyên nhân / Cách sửa |
|---|---|
| `make`: `/lib/modules/.../build: No such file or directory` | Chưa cài `linux-headers-rpi-v6`, hoặc kernel và headers lệch phiên bản (`full-upgrade` rồi `reboot`). |
| `insmod: ... Invalid module format` | Module build cho kernel khác. `make clean && make` lại sau khi reboot. |
| `dmesg` không có dòng `bcm-timer-plat` | Overlay chưa nạp. Kiểm tra `ls /proc/device-tree/soc/ \| grep edu-timer`. Với Cách B, chạy `dtoverlay` trước `insmod`. |
| `cannot get led-gpios` / `-EBUSY` | GPIO17 đang bị chương trình khác giữ. Đổi chân bằng `dtoverlay=bcm-timer-plat,led_gpio=27` và nối LED sang chân 13. |
| LED không nháy nhưng `irq_count` tăng | LED cắm ngược, thiếu GND, sai chân (phải là **chân 11**). |
| `irq_count` đứng ở 0 | `enable` đang là 0: `echo 1 \| sudo tee $T/enable`. |
| `... /sys/...: Permission denied` | Ghi vào `/sys` mà không có quyền root. Dùng `echo <giá trị> \| sudo tee <file>`, không dùng `echo ... > file` hay `sudo echo ... > file`. |

---

## 11. Giải thích code (dùng cho báo cáo)

### 11.1 Luồng hoạt động

```
config.txt: dtoverlay=bcm-timer-plat      firmware ghép overlay vào Device Tree
        │
        ▼
Node mới /soc/edu-timer@7e003000:
    compatible = "edu,bcm2835-timer-ch1";
    reg        = <0x7e003000 0x1c>;     // CS, CLO, CHI, C0..C3
    interrupts = <1 1>;                 // ngắt GPU số 1 = kênh 1
    led-gpios  = <&gpio 17 0>;
    edu,period-us = <500000>;
        │  kernel tạo platform_device; udev modprobe theo alias "of:...Cedu,bcm2835-timer-ch1"
        ▼
platform_driver_register()  ──khớp compatible──►  probe(pdev)
        ├─ platform_get_resource() + devm_ioremap()   → ánh xạ thanh ghi (dùng chung, không request)
        ├─ platform_get_irq()                          → số IRQ của kênh 1
        ├─ devm_gpiod_get_optional("led")             → GPIO17
        ├─ devm_request_irq(bcm_timer_isr)
        └─ đặt compare C1 = CLO + period              → ngắt đầu tiên

Mỗi lần CLO == C1:  bcm_timer_isr()
        ├─ ghi 1 vào bit M1 của CS (xóa cờ)
        ├─ đo độ trễ, tăng irq_count, đảo LED
        └─ C1 = mốc cũ + period                        → ngắt tiếp theo

rmmod  ──►  remove()  ──►  dừng lập lịch, tắt LED, synchronize_irq()  ──►  devm gỡ IRQ, GPIO, iomap
```

### 11.2 Các điểm kỹ thuật chính

| Điểm | Cách làm |
|---|---|
| Node mới, không đụng node gốc | Overlay thêm node `edu-timer@7e003000` dưới `&soc`; node `timer@7e003000` của Linux giữ nguyên. |
| Dùng chung vùng thanh ghi | Khối System Timer dùng chung với timer hệ thống, nên driver dùng `devm_ioremap()` (chỉ ánh xạ) chứ **không** `request_mem_region()`. |
| Xóa cờ ngắt an toàn | Thanh ghi `CS` kiểu *write-1-to-clear*: chỉ ghi bit `M1`, nên không xóa nhầm cờ của kênh 3 (Linux) hay kênh 0, 2 (GPU). |
| Không trôi nhịp | Mốc tiếp theo = mốc trước + chu kỳ (không tính từ lúc vào ISR). Nếu mốc đã qua (ngắt đến quá trễ), driver tính lại từ hiện tại và tăng `missed`. |
| Đo độ trễ ngắt | Trong ISR: `CLO − mốc so sánh` = thời gian từ lúc phần cứng khớp đến lúc CPU vào ISR, lưu giá trị lớn nhất vào `max_latency_us`. |
| Ngắt "cũ" khi đổi mốc | Khi đặt mốc mới (đổi chu kỳ, bật lại), driver xóa cờ khớp cũ; ISR bỏ qua nếu chưa tới mốc đang chờ. |
| Bộ đếm 32 bit tràn | So sánh bằng hiệu có dấu `(s32)(now − next)`, nên vẫn đúng khi `CLO` tràn (khoảng 71 phút một lần). |
| Đồng bộ | `spinlock` giữa ISR và các hàm sysfs; bộ đếm 64 bit đọc dưới khóa (ARM 32 bit không đọc 64 bit một lần được). |
| GPIO trong ngắt cứng | Kiểm tra `gpiod_cansleep()` trong `probe()`: GPIO của BCM2835 không ngủ nên đổi mức LED ngay trong ISR được. |
| `remove()` | Dừng lập lịch, xóa cờ, tắt LED, `synchronize_irq()` chờ ISR đang chạy kết thúc; sau đó devm gỡ IRQ và GPIO. |

---

## 12. Đã kiểm thử những gì

Code được kiểm thử với **kernel Raspberry Pi thật** (mã nguồn `raspberrypi/linux`, cấu hình `bcmrpi_defconfig` cho Pi Zero / Zero W, ARMv6) trên **máy Pi Zero giả lập** (QEMU 10.2, máy `raspi0`; có System Timer với ngắt từng kênh và GPIO).

| Hạng mục | Kết quả |
|---|---|
| Biên dịch với kernel Pi **6.6**, **6.12**, **6.18** (`W=1`) | Không có cảnh báo nào |
| `checkpatch.pl` | 0 lỗi; chỉ còn cảnh báo về `LINUX_VERSION_CODE` (cố ý, để chạy được trên nhiều phiên bản kernel) |
| Overlay ghép vào `bcm2708-rpi-zero-w.dtb` (bằng `dtmerge` của Raspberry Pi) | Node `edu-timer` có ngắt `<1 1>`, LED GPIO17; tham số `period_us`, `led_gpio` áp dụng đúng |
| Toàn bộ lệnh mục 7 → 8, theo đúng thứ tự, kernel 6.12 và 6.18 | Cho kết quả như mô tả |
| Overlay nạp lúc boot và nạp lúc chạy (configfs, như lệnh `dtoverlay`) | `probe()` thành công; gỡ overlay lúc chạy thì `remove()` chạy |
| Ngắt kênh 1 song song với timer hệ thống kênh 3 | `uptime` vẫn đúng khi nạp/gỡ driver; LED GPIO17 đổi mức theo từng ngắt |
| Chu kỳ 100 µs (10 kHz) | Chạy ổn định, không lỗi kernel |

**Phần chưa thể kiểm thử thay bạn:** độ trễ ngắt thật. Máy giả lập không chạy thời gian thực, nên con số `missed` / `max_latency_us` ở mục 8.5 không đại diện cho Pi. Trên Pi thật, hãy xác nhận LED nháy ở mục 8.3 và ghi lại số đo ở mục 8.5.
