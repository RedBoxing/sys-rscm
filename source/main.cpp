#include "main.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <cstdarg>
#include <poll.h>

#include "debug.hpp"
#include "utils.hpp"

#define INNER_HEAP_SIZE 0x41A000

u64 mainLoopSleepTime = 50;

#ifdef __cplusplus
extern "C"
{
#endif

    u32 __nx_applet_type = AppletType_None;
    u32 __nx_fs_num_sessions = 1;

    void __libnx_initheap(void)
    {
        static u8 inner_heap[INNER_HEAP_SIZE];
        extern void *fake_heap_start;
        extern void *fake_heap_end;

        fake_heap_start = inner_heap;
        fake_heap_end = inner_heap + sizeof(inner_heap);
    }

    void __appInit(void)
    {
        Result rc;

        rc = smInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

        rc = setsysInitialize();
        if (R_SUCCEEDED(rc))
        {
            SetSysFirmwareVersion fw;
            rc = setsysGetFirmwareVersion(&fw);
            if (R_SUCCEEDED(rc))
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
            setsysExit();
        }

        rc = ldrDmntInitialize();
        if (R_FAILED(rc))
        {
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized));
        }

        rc = pmdmntInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_NotInitialized));

        rc = socketInitialize(socketGetDefaultInitConfig());
        if (R_FAILED(rc))
            fatalThrow(rc);

        rc = pminfoInitialize();
        if (R_FAILED(rc))
            fatalThrow(rc);

        rc = fsInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

        rc = fsdevMountSdmc();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

        smExit();
    }

    void __appExit(void)
    {
        fsdevUnmountAll();
        fsExit();
        pminfoExit();
        socketExit();
        pmdmntExit();
        ldrDmntExit();
    }

#ifdef __cplusplus
}
#endif

int main(int argc, char *argv[])
{
    debug("Starting sys-rscm...");

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1337);
    addr.sin_addr.s_addr = INADDR_ANY;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        debug("Failed to create socket: %d", sock);
        return 1;
    }

    int ret = bind(sock, (sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        debug("Failed to bind socket: %d", ret);
        return 1;
    }

    ret = listen(sock, SOMAXCONN);
    if (ret < 0)
    {
        debug("Failed to listen on socket: %d", ret);
        return 1;
    }

    debug("Listening on port 1337");

    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (appletMainLoop())
    {
        struct timeval tv;
        fd_set rfsd;
        FD_ZERO(&rfsd);
        FD_SET(sock, &rfsd);

        tv.tv_usec = 0.0;
        tv.tv_sec = 1.0;

        if (select(sock + 1, &rfsd, NULL, NULL, &tv) > 0)
        {
            int client_socket = accept(sock, (sockaddr *)&client_addr, &client_addr_len);
            if (client_socket > 0)
            {
                debug("Accepted client %d", client_socket);

                Client *client = new Client(client_socket);
                clients.push_back(client);
            }
        }

        flushEvents();
        svcSleepThread(0);
    }

    close(sock);
    return 0;
}