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

Packet *readPacket(int sock)
{
    char *raw_buffer = (char *)malloc(1 + 16 + 4);
    if (recv(sock, raw_buffer, 1 + 16 + 4, 0) < 0)
        return nullptr;

    Buffer *buffer = new Buffer(raw_buffer, 1 + 16 + 4);

    u8 command = buffer->readUnsignedByte();
    u8 *uuid = buffer->read_array<u8>(16);
    u32 size = buffer->readUnsignedInt();

    PacketHeader *header = new PacketHeader{static_cast<Command>(command), uuid, size};

    log("Received packet with command %d and size %d", header->command, header->size);

    char *data = (char *)malloc(header->size);
    if (recv(sock, data, header->size, 0) < 0)
        return nullptr;

    return new Packet{header, new Buffer(data, header->size)};
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

        Buffer *data = processCommands(packet->header->command, packet->data);
        delete packet->data;

        size_t size = data->getWriteOffset() + (1 + 16 + 4);

        data->offset(1 + 16 + 4);
        data->setWriteOffset(0);
        data->writeUnsignedByte((u8)packet->header->command);
        data->write(packet->header->uuid, 16);
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

    log("Listening on port 1337");

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
        pid = buffer->readUnsignedLong();
        log("Attaching to process %d", pid);

        if (debugHandle != INVALID_HANDLE)
        {
            log("Closing old debug handle");

            rc = svcCloseHandle(debugHandle);
            if (R_FAILED(rc))
            {
                log("Failed to close debug handle: %d", rc);
                data->writeUnsignedInt(rc);
                break;
            }
        }

        rc = svcDebugActiveProcess(&debugHandle, pid);
        if R_SUCCEEDED (rc)
        {
            attachedProcessId = pid;
            log("Attached to process %d", pid);
        }
        else
        {
            log("Failed to attach to process: %d", rc);
            debugHandle = INVALID_HANDLE;
        }

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
                log("Failed to get debug event: %d", rc);
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
            if (R_FAILED(rc))
            {
                log("Failed to query memory: %d", rc);
                break;
            }

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
        if (status == Status::Paused)
        {
            void *event;
            rc = svcGetDebugEvent(&event, debugHandle);
            if (R_FAILED(rc))
            {
                log("Failed to get debug event: %d", rc);
                data->writeUnsignedInt(rc);
                break;
            }

            rc = svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
            if (R_SUCCEEDED(rc))
                status = Status::Running;
        }
        else
        {
            rc = 0;
        }

        data->writeUnsignedInt(rc);
        break;
    case Command::GetCurrentPID:
        rc = pmdmntGetApplicationProcessId(&pid);
        if (R_FAILED(rc))
        {
            log("Failed to get current PID: %d", rc);
        }
        else
        {
            log("Current PID: %d", pid);
        }

        data->writeUnsignedInt(rc);
        data->writeUnsignedLong(pid);
        break;
    case Command::GetAttachedPID:
        data->writeUnsignedInt(attachedProcessId);
        break;
    case Command::GetTitleID:
        pid = buffer->readUnsignedLong();

        rc = pminfoGetProgramId(&tid, pid);
        if (R_FAILED(rc))
        {
            log("Failed to get title ID: %d", rc);
        }
        else
        {
            log("Title ID: %016lx", tid);
        }

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
        data->writeUnsignedInt(0);
        break;
    }

    return data;
}