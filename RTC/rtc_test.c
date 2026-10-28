// SPDX-License-Identifier: GPL-2.0
/*
 * rtc_test.c - Chuong trinh user-space test driver RTC qua /dev/rtcN (ioctl)
 *
 * Cach dung:
 *   rtc_test [-d /dev/rtcN] read                     in thoi gian RTC (UTC)
 *   rtc_test [-d /dev/rtcN] epoch                    in thoi gian RTC dang so giay
 *   rtc_test [-d /dev/rtcN] set "YYYY-MM-DD HH:MM:SS" dat thoi gian (UTC)
 *   rtc_test [-d /dev/rtcN] sync                     dat RTC = gio he thong
 *   rtc_test [-d /dev/rtcN] diff                     in (RTC - gio he thong) giay
 *   rtc_test [-d /dev/rtcN] watch N                  in thoi gian moi giay, N lan
 *   rtc_test find                                    in ten /dev/rtcN cua ds1307-plat
 *
 * Khong co -d: tu tim RTC co ten "ds1307-plat" trong /sys/class/rtc/.
 * Ma thoat: 0 = thanh cong, 1 = loi.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DRV_NAME "ds1307-plat"

static char devpath[64];

static int find_rtc(void)
{
	char path[64], name[128];
	int i;

	for (i = 0; i < 16; i++) {
		FILE *f;

		snprintf(path, sizeof(path), "/sys/class/rtc/rtc%d/name", i);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f) && strstr(name, DRV_NAME)) {
			fclose(f);
			snprintf(devpath, sizeof(devpath), "/dev/rtc%d", i);
			return 0;
		}
		fclose(f);
	}
	return -1;
}

static int open_rtc(void)
{
	int fd = open(devpath, O_RDONLY);

	if (fd < 0)
		fprintf(stderr, "open %s: %s\n", devpath, strerror(errno));
	return fd;
}

static int rtc_read(int fd, struct rtc_time *rt)
{
	memset(rt, 0, sizeof(*rt));
	if (ioctl(fd, RTC_RD_TIME, rt) < 0) {
		fprintf(stderr, "RTC_RD_TIME: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int rtc_write(int fd, time_t t)
{
	struct tm tm;
	struct rtc_time rt;

	gmtime_r(&t, &tm);
	memset(&rt, 0, sizeof(rt));
	rt.tm_sec  = tm.tm_sec;
	rt.tm_min  = tm.tm_min;
	rt.tm_hour = tm.tm_hour;
	rt.tm_mday = tm.tm_mday;
	rt.tm_mon  = tm.tm_mon;
	rt.tm_year = tm.tm_year;
	rt.tm_wday = tm.tm_wday;
	rt.tm_yday = tm.tm_yday;

	if (ioctl(fd, RTC_SET_TIME, &rt) < 0) {
		fprintf(stderr, "RTC_SET_TIME: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static time_t rtc_to_epoch(const struct rtc_time *rt)
{
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	tm.tm_sec  = rt->tm_sec;
	tm.tm_min  = rt->tm_min;
	tm.tm_hour = rt->tm_hour;
	tm.tm_mday = rt->tm_mday;
	tm.tm_mon  = rt->tm_mon;
	tm.tm_year = rt->tm_year;
	return timegm(&tm);
}

static void print_time(const struct rtc_time *rt)
{
	static const char *const wday[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};

	printf("%04d-%02d-%02d %02d:%02d:%02d UTC (%s) epoch=%lld\n",
	       rt->tm_year + 1900, rt->tm_mon + 1, rt->tm_mday,
	       rt->tm_hour, rt->tm_min, rt->tm_sec,
	       (rt->tm_wday >= 0 && rt->tm_wday <= 6) ? wday[rt->tm_wday] : "?",
	       (long long)rtc_to_epoch(rt));
}

static int parse_time(const char *s, time_t *out)
{
	struct tm tm;
	int n;

	memset(&tm, 0, sizeof(tm));
	n = sscanf(s, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		   &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
	if (n != 6)
		return -1;
	tm.tm_year -= 1900;
	tm.tm_mon  -= 1;
	*out = timegm(&tm);
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: rtc_test [-d /dev/rtcN] read|epoch|sync|diff\n"
		"       rtc_test [-d /dev/rtcN] set \"YYYY-MM-DD HH:MM:SS\"\n"
		"       rtc_test [-d /dev/rtcN] watch N\n"
		"       rtc_test find\n");
	exit(1);
}

int main(int argc, char **argv)
{
	struct rtc_time rt;
	const char *cmd;
	int fd, ret = 0;

	if (argc >= 3 && strcmp(argv[1], "-d") == 0) {
		snprintf(devpath, sizeof(devpath), "%s", argv[2]);
		argv += 2;
		argc -= 2;
	} else if (find_rtc() < 0) {
		fprintf(stderr, "no RTC named '%s' in /sys/class/rtc (driver loaded?)\n",
			DRV_NAME);
		return 1;
	}
	if (argc < 2)
		usage();
	cmd = argv[1];

	if (strcmp(cmd, "find") == 0) {
		printf("%s\n", devpath);
		return 0;
	}

	fd = open_rtc();
	if (fd < 0)
		return 1;

	if (strcmp(cmd, "read") == 0) {
		ret = rtc_read(fd, &rt);
		if (!ret)
			print_time(&rt);
	} else if (strcmp(cmd, "epoch") == 0) {
		ret = rtc_read(fd, &rt);
		if (!ret)
			printf("%lld\n", (long long)rtc_to_epoch(&rt));
	} else if (strcmp(cmd, "set") == 0) {
		time_t t;

		if (argc < 3 || parse_time(argv[2], &t) < 0)
			usage();
		ret = rtc_write(fd, t);
		if (!ret && rtc_read(fd, &rt) == 0) {
			printf("set ok, now: ");
			print_time(&rt);
		}
	} else if (strcmp(cmd, "sync") == 0) {
		struct timeval tv;

		/* DS1307 chi co do phan giai 1 giay: cho toi dau giay moi roi ghi */
		gettimeofday(&tv, NULL);
		usleep(1000000 - tv.tv_usec);
		ret = rtc_write(fd, time(NULL));
		if (!ret && rtc_read(fd, &rt) == 0) {
			printf("synced from system time: ");
			print_time(&rt);
		}
	} else if (strcmp(cmd, "diff") == 0) {
		ret = rtc_read(fd, &rt);
		if (!ret)
			printf("%lld\n", (long long)(rtc_to_epoch(&rt) - time(NULL)));
	} else if (strcmp(cmd, "watch") == 0) {
		int i, n = argc >= 3 ? atoi(argv[2]) : 5;

		for (i = 0; i < n && !ret; i++) {
			ret = rtc_read(fd, &rt);
			if (!ret)
				print_time(&rt);
			fflush(stdout);
			if (i + 1 < n)
				sleep(1);
		}
	} else {
		usage();
	}

	close(fd);
	return ret ? 1 : 0;
}
