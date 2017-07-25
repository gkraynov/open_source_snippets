// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Proof of concept, don't use it.

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#define EIR_SERVICE_DATA     0x16
#define EIR_SCALE_UUID       0x181D  // Weight Scale.
#define EIR_SCALE_KG_ACTIVE  0x02    // Measuring in progress.
#define EIR_SCALE_KG_STABLE  0x22    // Reading is stable.
#define EIR_SCALE_KG_IDLE    0xA2    // Last recorded value.

#define UNITS_KG  1
#define UNITS_LBS 2
#define UNITS_JIN 3

#define STATE_ACTIVE 1
#define STATE_STABLE 2
#define STATE_IDLE   3

char* device_bdaddr_str = "XX:XX:XX:XX:XX:XX";

static void report_weight(int value, int units, int state)
{
    printf("%d %d.%d\n", state, value / 10, value % 10);
}

// Try to extract weight reading from Extended Inquiry Response.
static void parse_weight(const uint8_t* eir, size_t eir_size)
{
    int offset = 0;

    while (offset < eir_size)
    {
        int field_size = eir[0];
        if (field_size == 0 || offset + field_size + 1 > eir_size) return;

        if (eir[1] == EIR_SERVICE_DATA && field_size >= 6)
        {
            int uuid = 256 * eir[3] + eir[2];
            if (uuid == EIR_SCALE_UUID) {
                int value = 256 * eir[6] + eir[5];
                switch (eir[4]) {
                case EIR_SCALE_KG_ACTIVE:
                    report_weight(value / 20, UNITS_KG, STATE_ACTIVE);
                    break;
                case EIR_SCALE_KG_STABLE:
                    report_weight(value / 20, UNITS_KG, STATE_STABLE);
                    break;
                case EIR_SCALE_KG_IDLE:
                    report_weight(value / 20, UNITS_KG, STATE_IDLE);
                    break;
                }
            }
        }

        offset += field_size + 1;
        eir += field_size + 1;
    }
}

static int signal_received = 0;

static void sigint_handler(int sig)
{
    signal_received = sig;
}

static void assert(int condition, const char* message)
{
    if (!condition)
    {
        perror(message);
        exit(1);
    }
}


static int print_advertising_devices(int dd)
{
    unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr;
    struct hci_filter nf, of;
    struct sigaction sa;
    socklen_t olen;
    int len;
    bdaddr_t device_bdaddr;

    str2ba(device_bdaddr_str, &device_bdaddr);

    olen = sizeof(of);
    if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
        printf("Could not get socket options\n");
        return -1;
    }

    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);

    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
        printf("Could not set socket options\n");
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_NOCLDSTOP;
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    while (1) {
        evt_le_meta_event *meta;
        le_advertising_info *info;
        char addr[18];

        while ((len = read(dd, buf, sizeof(buf))) < 0) {
            if (errno == EINTR && signal_received == SIGINT) {
                len = 0;
                goto done;
            }

            if (errno == EAGAIN || errno == EINTR)
                continue;
            goto done;
        }

        ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
        len -= (1 + HCI_EVENT_HDR_SIZE);

        meta = (void *) ptr;

        if (meta->subevent != 0x02)
            goto done;

        
        info = (le_advertising_info *) (meta->data + 1);
        if (memcmp(&info->bdaddr, &device_bdaddr, sizeof(bdaddr_t)) == 0) {
            parse_weight(info->data, info->length);
        }
    }

done:
    setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));

    if (len < 0)
        return -1;

    return 0;
}


static void scan_for_devices() {
    int dev, ret;

    dev = hci_open_dev(hci_get_route(NULL));
    assert(dev > 0, "Failed to open device");
    ret = hci_le_set_scan_parameters(dev, 1, htobs(0x0010), htobs(0x0010), 0, 0, 1000);
    assert(ret >= 0, "Bluetooth scan failed");
    ret = hci_le_set_scan_enable(dev, 1, 0, 1000);
    assert(ret >= 0, "Bluetooth scan failed");

    printf("Scanning...\n");

    ret = print_advertising_devices(dev);
    assert(ret >= 0, "Bluetooth scan failed");

    ret = hci_le_set_scan_enable(dev, 0, 0, 1000);
    assert(ret >= 0, "Bluetooth scan stop failure");
    hci_close_dev(dev);
}

int main(int argc, char* argv[]) {
    scan_for_devices();
    return 0;
}

