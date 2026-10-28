# Video Demo

Demonstration videos of the three platform drivers running on the Raspberry Pi Zero W.

| # | Driver | What the video shows | Link |
|---|--------|----------------------|------|
| 1 | Timer (`bcm_timer_plat`) | LED on GPIO17 toggled by the System Timer channel 1 interrupt; period changed from sysfs | https://youtu.be/U_tcdEtAESg |
| 2 | PWM (`bcm_pwm_plat`) | LED brightness on GPIO18 set by the duty cycle; breathing loop and inverted polarity | https://youtu.be/Ros7_78uBJg |
| 3 | RTC (`ds1307_plat`) | Power-loss test: the DS1307 keeps the time on its CR2032 cell and restores the system clock at boot | https://youtu.be/DzwE6CQ0-nE |

[![Video 1 – Timer](https://img.youtube.com/vi/U_tcdEtAESg/hqdefault.jpg)](https://youtu.be/U_tcdEtAESg)
[![Video 2 – PWM](https://img.youtube.com/vi/Ros7_78uBJg/hqdefault.jpg)](https://youtu.be/Ros7_78uBJg)
[![Video 3 – RTC power-loss test](https://img.youtube.com/vi/DzwE6CQ0-nE/hqdefault.jpg)](https://youtu.be/DzwE6CQ0-nE)

The test procedures and the measured results behind each video are described in
Chapter 2 of the report: [`FinalReport/Group6_Platform_Driver_Report.pdf`](../FinalReport/Group6_Platform_Driver_Report.pdf).
