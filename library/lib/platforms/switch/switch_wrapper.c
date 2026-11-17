/*
    Copyright 2019 natinusala
    Copyright 2019 WerWolv
    Copyright 2019 p-sam

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <stdio.h>
#include <switch.h>
#include <unistd.h>

static int nxlink_sock = -1;

#ifdef __SWITCH2__
#include <errno.h>
#include <sys/socket.h>

// UDP receive buffer size the socket driver accepted, for the app to log.
u32 brls_switch2_udp_rx_buf_size = 0;
// errno from the UDP socket probe for each rejected size (0 = not tried).
int brls_switch2_udp_rx_probe_errno[3] = { 0, 0, 0 };
const u32 brls_switch2_udp_rx_sizes[3] = { 0x100000, 0x80000, 0x40000 };

// socketInitialize accepts any size, but the driver reserves the UDP receive
// buffer when each UDP socket is created and fails with ENOBUFS if the size is
// over its per-socket limit (2 MB failed on Switch 2). Create one UDP socket
// to check the size before keeping it.
static int udpSocketProbe(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return errno ? errno : -1;
    close(fd);
    return 0;
}
#endif

void userAppInit()
{
    printf("userAppInit\n");
    appletLockExit();

    // Init network
    SocketInitConfig cfg = *(socketGetDefaultInitConfig());
    AppletType at        = appletGetAppletType();
    if (at == AppletType_Application || at == AppletType_SystemApplication)
    {
        cfg.num_bsd_sessions = 12; // default is 3
        cfg.sb_efficiency    = 8; // default is 4
#ifdef __SWITCH2__
        // The default UDP receive buffer is 42 KB (0xA500), about one 20 Mbps
        // 1080p60 video frame. At 80 Mbps the video socket has held up to
        // 189 KB while the receive thread caught up. The driver's buffer pool
        // is sb_efficiency * (sum of the per-socket sizes), so this also
        // enlarges the pool. Try the largest size first; a failed
        // socketInitialize cleans up after itself.
        for (size_t i = 0; i < sizeof(brls_switch2_udp_rx_sizes) / sizeof(brls_switch2_udp_rx_sizes[0]); i++)
        {
            SocketInitConfig streaming = cfg;
            streaming.udp_rx_buf_size  = brls_switch2_udp_rx_sizes[i];
            if (R_FAILED(socketInitialize(&streaming)))
            {
                brls_switch2_udp_rx_probe_errno[i] = -1;
                continue;
            }
            int err = udpSocketProbe();
            if (err == 0)
            {
                brls_switch2_udp_rx_buf_size = streaming.udp_rx_buf_size;
                break;
            }
            brls_switch2_udp_rx_probe_errno[i] = err;
            socketExit();
        }
        if (brls_switch2_udp_rx_buf_size == 0 && R_SUCCEEDED(socketInitialize(&cfg)))
            brls_switch2_udp_rx_buf_size = cfg.udp_rx_buf_size;
#else
        socketInitialize(&cfg);
#endif
    }
    else
    {
        cfg.num_bsd_sessions = 2;
        cfg.sb_efficiency    = 1;
        socketInitialize(&cfg);
    }

#ifdef DEBUG
    nxlink_sock = nxlinkStdio();
#endif

    romfsInit();
    hidsysInitialize();
    inssInitialize();
    plInitialize(PlServiceType_User);
    setsysInitialize();
    setInitialize();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);
    lblInitialize();
}

void userAppExit()
{
    printf("userAppExit\n");

    // backlight
    lblExit();
    // network state
    nifmExit();
    // power state
    psmExit();
    // system language...
    setExit();
    // system theme, system version...
    setsysExit();
    // system font
    plExit();

    inssExit();
    hidsysExit();
    romfsExit();

    if (nxlink_sock != -1)
        close(nxlink_sock);

    socketExit();

    appletUnlockExit();
}
