#include "main.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <cstdarg>
#include <pthread.h>

#include "dmnt/dmntcht.hpp"
#include "utils.hpp"

#define INNER_HEAP_SIZE 0x41A000
#define MAX_BUFFER_SIZE 2048 * 4

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

u64 mainLoopSleepTime = 50;
u64 attachedProcessId = 0;
Handle debugHandle;
Status status = Status::Stopped;

static const SocketInitConfig sockInitConf = {
    .bsdsockets_version = 1,

    .tcp_tx_buf_size = 0x200,
    .tcp_rx_buf_size = 0x400,
    .tcp_tx_buf_max_size = 0x400,
    .tcp_rx_buf_max_size = 0x800,
    // We're not using tcp anyways

    .udp_tx_buf_size = 0x2600,
    .udp_rx_buf_size = 0xA700,

    .sb_efficiency = 2,

    .num_bsd_sessions = 3,
    .bsd_service_type = BsdServiceType_User};

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

        rc = pmdmntInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_NotInitialized));

        rc = fsInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

        fsdevMountSdmc();

        rc = socketInitialize(&sockInitConf);
        if (R_FAILED(rc))
            fatalThrow(rc);

        rc = pminfoInitialize();
        if (R_FAILED(rc))
            fatalThrow(rc);

        /*  rc = dmntcht::initialize();
          if (R_FAILED(rc))
              fatalThrow(rc);*/

        smExit();
    }

    void __appExit(void)
    {
        // dmntcht::exit();
        pminfoExit();
        socketExit();
        pmdmntExit();
        fsdevUnmountAll();
        fsExit();
        smExit();
    }

#ifdef __cplusplus
}
#endif

Packet *readPacket(int sock)
{
    PacketHeader header = {Command::None, {0}, 0};
    if (recv(sock, &header, sizeof(header), 0) < 0)
        return nullptr;

    log("Received packet with command %d and size %d", header.command, header.size);

    for (int i = 0; i < 16; i++)
        log("%02x", header.uuid[i]);

    char *data = (char *)malloc(header.size);
    if (recv(sock, data, header.size, 0) < 0)
        return nullptr;

    log("Received packet data: %s", data);

    return new Packet{header, new Buffer(data, header.size)};
}

void *handle_connection(void *arg)
{
    int sock = (intptr_t)arg;

    while (true)
    {
        Packet *packet = readPacket(sock);
        if (packet == nullptr)
        {
            log("Failed to read packet");
            break;
        }

        Buffer *data = processCommands(packet->header.command, packet->data);
        delete packet->data;

        // data->reallocate(data->getWriteOffset() + (1 + 16 + 4));
        size_t size = data->getWriteOffset() + (1 + 16 + 4);

        data->offset(1 + 16 + 4);
        data->setWriteOffset(0);
        data->writeUnsignedByte((u8)packet->header.command);
        data->write(packet->header.uuid, 16);
        data->writeUnsignedInt(size);

        int bytes_written = send(sock, data->getBuffer(), data->getWriteOffset() + size, 0);

        if (bytes_written < 0)
        {
            log("Failed to send data: %d", bytes_written);
            delete data;
            free(data);
            break;
        }
        else if (bytes_written != data->getWriteOffset() + size)
        {
            log("Failed to send all data: %d", bytes_written);
            free(data);
            free(data);
            break;
        }

        delete data;
        free(data);
    }

    close(sock);
    return NULL;
}

int main(int argc, char *argv[])
{
    log("Starting sys-rscm...");

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1337);
    addr.sin_addr.s_addr = INADDR_ANY;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        log("Failed to create socket: %d", sock);
        return 1;
    }

    int ret = bind(sock, (sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        log("Failed to bind socket: %d", ret);
        return 1;
    }

    ret = listen(sock, SOMAXCONN);
    if (ret < 0)
    {
        log("Failed to listen on socket: %d", ret);
        return 1;
    }

    log("Listening on port 6060");

    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (appletMainLoop())
    {
        int client_socket = accept(sock, (sockaddr *)&client_addr, &client_addr_len);
        if (client_socket > 0)
        {
            log("Accepted client %d", client_socket);

            int res;
            pthread_t *thread_id = (pthread_t *)malloc(sizeof(pthread_t));
            if ((res = pthread_create(thread_id, NULL, &handle_connection, (void *)client_socket)) != 0)
            {
                log("Failed to create thread : %d", res);
                close(client_socket);
            }
        }

        svcSleepThread(mainLoopSleepTime * 1e+6L);
    }

    close(sock);
    return 0;
}

Buffer *processCommands(Command command, Buffer *buffer)
{
    Buffer *data = new Buffer(128);

    Result rc;
    MemoryInfo meminfo;
    u64 address;
    size_t size;
    void *buf;
    u64 pid;
    u32 max;
    u64 *pids;
    u32 id;
    u64 flags;
    u64 tid;
    int maxpids;
    s32 count;

    log("Processing command %d", command);

    switch (command)
    {
    case Command::Attach:
        if (debugHandle != INVALID_HANDLE)
            svcCloseHandle(debugHandle);

        pid = buffer->readUnsignedLong();
        attachedProcessId = pid;
        rc = svcDebugActiveProcess(&debugHandle, pid);
        data->writeUnsignedInt(rc);
        break;
    case Command::Detach:
        attachedProcessId = 0;
        if (status == Status::Paused)
        {
            void *event;
            rc = svcGetDebugEvent(&event, debugHandle);
            if (R_FAILED(rc))
            {
                data->writeUnsignedInt(rc);
                break;
            }

            rc = svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
            if (R_SUCCEEDED(rc))
                status = Status::Running;
        }
        rc = svcCloseHandle(debugHandle);
        debugHandle = INVALID_HANDLE;
        data->writeUnsignedInt(rc);
        break;
    case Command::GetStatus:
        data->writeUnsignedByte((u8)status);
        data->writeUnsignedByte(VERSION_MAJOR);
        data->writeUnsignedByte(VERSION_MINOR);
        data->writeUnsignedByte(VERSION_PATCH);
        break;
    case Command::QueryMemory:
        address = buffer->readUnsignedLong();
        u32 pageinfo;
        meminfo = {0};
        rc = svcQueryDebugProcessMemory(&meminfo, &pageinfo, debugHandle, address);

        data->writeUnsignedLong(meminfo.addr);
        data->writeUnsignedLong(meminfo.size);
        data->writeUnsignedInt(meminfo.type);
        data->writeUnsignedInt(meminfo.perm);

        data->writeUnsignedInt(rc);
        break;
    case Command::QueryMemoryMulti:
        address = buffer->readUnsignedLong();
        max = buffer->readUnsignedInt();

        for (int i = 0; i < max; i++)
        {
            u32 pageinfo;
            meminfo = {0};
            rc = svcQueryDebugProcessMemory(&meminfo, &pageinfo, debugHandle, address);

            data->writeUnsignedLong(meminfo.addr);
            data->writeUnsignedLong(meminfo.size);
            data->writeUnsignedInt(meminfo.type);
            data->writeUnsignedInt(meminfo.perm);

            data->writeUnsignedInt(rc);

            if (meminfo.type == MemoryType::MemType_Reserved || R_FAILED(rc))
            {
                break;
            }

            address += meminfo.size;
        }

        data->writeUnsignedInt(rc);
        break;
    case Command::ReadMemory:
        address = buffer->readUnsignedLong();
        size = buffer->readUnsignedInt();

        while (size > 0)
        {
            u64 len = size < MAX_BUFFER_SIZE ? size : MAX_BUFFER_SIZE;
            buf = malloc(len);
            rc = svcReadDebugProcessMemory(buf, debugHandle, address, len);
            data->writeUnsignedInt(rc);

            if (R_FAILED(rc))
            {
                free(buf);
                break;
            }

            data->writeCompressed(buf, len);
            free(buf);

            address += len;
            size -= len;
        }
        break;
    case Command::WriteMemory:
        address = buffer->readUnsignedLong();
        buf = buffer->readCompressed<u8 *>((u32 *)&size);

        rc = svcWriteDebugProcessMemory(debugHandle, buf, address, size);
        data->writeUnsignedInt(rc);
        break;
    case Command::Pause:
        rc = svcBreakDebugProcess(debugHandle);
        if (R_SUCCEEDED(rc))
            status = Status::Paused;
        data->writeUnsignedInt(rc);
        break;
    case Command::Resume:
        rc = svcGetDebugEvent(&buf, debugHandle);
        if (R_FAILED(rc))
        {
            data->writeUnsignedInt(rc);
            break;
        }

        rc = svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
        if (R_SUCCEEDED(rc))
            status = Status::Running;
        data->writeUnsignedInt(rc);
        break;
    case Command::GetCurrentPID:
        rc = pmdmntGetApplicationProcessId(&pid);
        data->writeUnsignedInt(rc);
        data->writeUnsignedLong(pid);
        break;
    case Command::GetAttachedPID:
        data->writeUnsignedInt(attachedProcessId);
        break;
    case Command::GetTitleID:
        pid = buffer->readUnsignedLong();

        rc = pminfoGetProgramId(&tid, pid);
        data->writeUnsignedInt(rc);
        data->writeUnsignedLong(tid);
        break;
    case Command::GetPIDs:
        maxpids = MAX_BUFFER_SIZE / sizeof(u64);

        pids = (u64 *)malloc(MAX_BUFFER_SIZE);
        rc = svcGetProcessList(&count, pids, maxpids);

        data->writeUnsignedInt(rc);
        if (R_SUCCEEDED(rc))
        {
            data->writeUnsignedInt(count);
            data->write(pids, count * sizeof(u64));
        }
        break;
    case Command::SetBreakpoint:
        id = buffer->readUnsignedInt();
        address = buffer->readUnsignedLong();
        flags = buffer->readUnsignedLong();

        if (address == 0)
            address = debugHandle;

        rc = svcSetHardwareBreakPoint(id, flags, address);
        data->writeUnsignedInt(rc);
        break;
    default:
        log("Unknown command %lx", (u8)command);
        data->writeString("Unknown command");
        break;
    }

    log("Processed command %x", (u8)command);

    return data;
}