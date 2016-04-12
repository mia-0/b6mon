/* Copyright © 2016, Martin Herkt <lachs0r@srsfckn.biz>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or  without fee is hereby granted,  provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING  FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <getopt.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int find_device(const char *vendor, const char *product)
{
    int fd = -1;
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev, *parent;

    udev = udev_new();
    if (udev == NULL) {
        fputs("Cannot create udev context\n", stderr);
        exit(1);
    }

    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    udev_enumerate_scan_devices(enumerate);

    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path, *vid, *pid;

        path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);
        parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb",
                                                               "usb_device");

        if (dev != NULL) {
            vid = udev_device_get_sysattr_value(parent, "idVendor");
            pid = udev_device_get_sysattr_value(parent, "idProduct");

            if (!strcmp(vid, vendor) && !strcmp(pid, product)) {
                path = udev_device_get_devnode(dev);
                fd = open(path, O_RDWR);

                if (fd < 0)
                    perror("open");
            }

            udev_device_unref(parent);

            if (fd >= 0)
                break;
        }
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return fd;
}

typedef struct ChargeInfo {
    int workState;
    int mAh;
    int time;
    double voltage;
    double current;
    int tempExt;
    int tempInt;
    int impedanceInt;
    double cells[6];
} ChargeInfo;

typedef struct SysInfo {
    int cycleTime;
    int timeLimitOn;
    int timeLimit;
    int capLimitOn;
    int capLimit;
    int keyBuzz;
    int sysBuzz;
    double inDClow;
    int tempLimit;
    double voltage;
    double cells[6];
} SysInfo;

typedef struct DevInfo {
    char core_type[6];
    int upgrade_type;
    int is_encrypt;
    int customer_id;
    int language_id;
    float sw_version;
    float hw_version;
    float rsvd;
} DevInfo;

void command(int fd, unsigned char *buf, char cmdid) {
    unsigned char cmd[7] = { 0x0f, 0x03, cmdid, 0x00, cmdid, 0xff, 0xff };
    write(fd, cmd, 7);
    read(fd, buf, 64);
}

int read2b(unsigned char **bp) {
    int tmp = **bp * 256;
    *bp += 1;
    tmp += **bp;
    *bp += 1;

    return tmp;
}

ChargeInfo get_chg(int fd) {
    unsigned char buf[64], *bp;
    command(fd, buf, 0x55); /* get working process data from MCU */

    ChargeInfo info = {0};
    bp = buf + 4;

    info.workState = *bp++;
    info.mAh = read2b(&bp);
    info.time = read2b(&bp);
    info.voltage = read2b(&bp) / 1000.0;
    info.current = read2b(&bp) / 1000.0;
    info.tempExt = *bp++;
    info.tempInt = *bp++;
    info.impedanceInt = read2b(&bp);

    for (int i = 0; i < 6; i++) {
        info.cells[i] = read2b(&bp) / 1000.0;
    }

    return info;
}

SysInfo get_sys(int fd) {
    unsigned char buf[64], *bp;
    command(fd, buf, 0x5a); /* get system data from MCU */

    SysInfo info = {0};
    bp = buf + 4;

    info.cycleTime = *bp++;
    info.timeLimitOn = *bp++;
    info.timeLimit = read2b(&bp);
    info.capLimitOn = *bp++;
    info.capLimit = read2b(&bp);
    info.keyBuzz = *bp++;
    info.sysBuzz = *bp++;
    info.inDClow = read2b(&bp) / 1000.0;
    bp += 2;
    info.tempLimit = *bp++;
    info.voltage = read2b(&bp) / 1000.0;

    for (int i = 0; i < 6; i++) {
        info.cells[i] = read2b(&bp) / 1000.0;
    }

    return info;
}

DevInfo get_devinfo(int fd) {
    unsigned char buf[64], *bp;
    command(fd, buf, 0x57);

    DevInfo info = {0};
    bp = buf + 5;

    memcpy(info.core_type, bp, 6);
    bp += 6;
    info.upgrade_type = *bp++;
    info.is_encrypt = *bp++;
    info.customer_id = read2b(&bp);
    info.language_id = *bp++;
    info.sw_version = ((float)bp[0] + (float)bp[1]) / 100.0;
    bp += 2;
    info.hw_version = (float)*bp;

    return info;
}

double difftime_ms(struct timespec t1, struct timespec t2)
{
    time_t elapsed = (t2.tv_sec - t1.tv_sec) * 1000;
    elapsed += round((t2.tv_nsec - t1.tv_nsec) / 1.0e6);

    return elapsed / 1000.0;
}

void monitor_system(int fd)
{
    struct timespec interval = { 0, 150000000 }, t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    fprintf(stderr, "Monitoring system status, ^C to exit\n");
    printf("# Time   V     C1    C2    C3    C4    C5    C6\n");

    for(;;) {
        nanosleep(&interval, NULL);
        SysInfo ci = get_sys(fd);

        clock_gettime(CLOCK_MONOTONIC, &t2);

        printf("  %-6.1f %-5.3f", difftime_ms(t1, t2), ci.voltage);

        for (int i = 0; i < 6; i++) {
            printf(" %-5.3f", ci.cells[i] > 2.0 ? ci.cells[i] : 0);
        }

        printf("\n");
        fflush(stdout);
    }
}

void monitor_process(int fd)
{
    struct timespec interval = { 0, 150000000 }, t1, t2;
    int started = 0;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    fprintf(stderr, "Monitoring process status until end of process, "
                    "^C to exit\n");
    printf("# Time   mAh   V      A     TempE TempI "
           "C1    C2    C3    C4    C5    C6\n");

    for(;;) {
        nanosleep(&interval, NULL);
        ChargeInfo ci = get_chg(fd);

        if (ci.workState == 1) {
            started = 1;
            clock_gettime(CLOCK_MONOTONIC, &t2);

            printf("  %-6.1f %-5d %-5.3f %-5.3f %-5d %-5d",
                   difftime_ms(t1, t2), ci.mAh, ci.voltage, ci.current,
                   ci.tempExt, ci.tempInt);

            for (int i = 0; i < 6; i++) {
                printf(" %-5.3f", ci.cells[i] > 2.0 ? ci.cells[i] : 0);
            }

            printf("\n");
            fflush(stdout);
        } else if (!started) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
        } else {
            break;
        }
    }
}

void usage(const char *n) {
    fprintf(stderr,
            "Usage: %s -[p|s]\n"
            "\n"
            "-p     monitor process status until end of process\n"
            "-s     continuously monitor system status (cell voltage only)\n",
            n);
}

int main(int argc, char **argv)
{
    extern char *optarg;
    extern int optind, opterr, optopt;

    int opt;
    int o_sys = 0, o_proc = 0;

    while ((opt = getopt(argc, argv, "sp")) != -1) {
        switch(opt) {
            case 'p':
                o_proc = 1;
                break;
            case 's':
                o_sys = 1;
                break;
            case '?':
                usage(argv[0]);
                return 1;
        }
    }

    if (o_sys && o_proc) {
        fprintf(stderr, "%s: -s and -p are mutually exclusive\n", argv[0]);
        return 1;
    } else if (!o_sys && !o_proc) {
        usage(argv[0]);
        return 1;
    }

    /* yeah, really. lazy chinese people :V */
    int fd = find_device("0000", "0001");

    if (fd < 0) {
        fputs("Could not find/open an iMAX B6 Mini\n", stderr);
        goto error;
    }

    DevInfo di = get_devinfo(fd);
    fprintf(stderr, "Found device: core type %s, hw %.2f, sw %.2f\n",
            di.core_type, di.hw_version, di.sw_version);

    SysInfo si = get_sys(fd);
    fprintf(stderr, "Device settings:\n"
                    "Cycle time: %d min\n"
                    "Time limit: %d min (%s) / Capacity limit: %d mAh (%s)\n"
                    "Key buzz: %s / System buzz: %s\n"
                    "Low input voltage threshold: %.2f\n"
                    "Temperature limit: %d °C\n"
                    "\n"
                    "Status:\n"
                    "Voltage: %.2f V\n",
            si.cycleTime,
            si.timeLimit, si.timeLimitOn ? "on" : "off",
            si.capLimit, si.capLimitOn ? "on" : "off",
            si.keyBuzz ? "on" : "off", si.sysBuzz ? "on" : "off",
            si.inDClow, si.tempLimit, si.voltage);

    for (int i = 0; i < 6; i++) {
        if (si.cells[i] > 2.0)
            fprintf(stderr, "Cell %d: %.2f V\n", i + 1, si.cells[i]);
    }

    if (o_sys)
        monitor_system(fd);
    else if (o_proc)
        monitor_process(fd);

error:

    if (fd >= 0)
        close(fd);

    return 0;
}

