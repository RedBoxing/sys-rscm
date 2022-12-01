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

#define INNER_HEAP_SIZE 0x80000
u64 mainLoopSleepTime = 50;

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

        rc = fsInitialize();
        if (R_FAILED(rc))
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

        fsdevMountSdmc();

        rc = socketInitialize(&sockInitConf);
        if (R_FAILED(rc))
            fatalThrow(rc);

        rc = dmntcht::initialize();
        if (R_FAILED(rc))
            fatalThrow(rc);

        smExit();
    }

    void __appExit(void)
    {
        dmntcht::exit();
        socketExit();
        fsdevUnmountAll();
        fsExit();
    }

#ifdef __cplusplus
}
#endif

Buffer *readData(int sock)
{
    size_t length = 0;
    if (recv(sock, &length, sizeof(length), 0) < 0)
        return nullptr;

    char *data = (char *)malloc(length);
    if (recv(sock, data, length, 0) < 0)
        return nullptr;

    return new Buffer(data, length);
}

void *handle_connection(void *arg)
{
    int sock = (intptr_t)arg;

    while (true)
    {
        Buffer *input_buffer = readData(sock);
        if (input_buffer == nullptr)
            break;

        Command command = (Command)input_buffer->readUnsignedByte();
        Data *data = processCommands(command, input_buffer);
        delete input_buffer;

        data->buffer->reallocate(data->buffer->getWriteOffset() + 9);
        data->buffer->offset(9);
        data->buffer->writeUnsignedLong(0, data->buffer->getWriteOffset() - 8);
        data->buffer->writeBoolean(8, data->success);

        log("Sending %d bytes of data", data->buffer->getWriteOffset());
        int bytes_written = send(sock, data->buffer->getBuffer(), data->buffer->getWriteOffset(), 0);

        if (bytes_written < 0)
        {
            log("Failed to send data: %d", bytes_written);
            delete data->buffer;
            free(data);
            break;
        }
        else if (bytes_written != data->buffer->getWriteOffset())
        {
            log("Failed to send all data: %d/%d", bytes_written, data->buffer->getWriteOffset());
            free(data->buffer);
            free(data);
            break;
        }

        log("Sent %d bytes", bytes_written);

        delete data->buffer;
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
    addr.sin_port = htons(6060);
    addr.sin_addr.s_addr = INADDR_ANY;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
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

Data *processCommands(Command command, Buffer *buffer)
{
    Data *data = (Data *)malloc(sizeof(Data));
    data->success = false;
    data->buffer = new Buffer(128);

    log("Processing command %d", command);

    dmntcht::CheatProcessMetadata meta;
    Result rc;

    if (command != Command::forceOpenCheatProcess)
    {
        log("Getting cheat process metadata");
        rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            log("Failed to get cheat process metadata: %lx", rc);
            data->buffer->writeString("Failed to get cheat process metadata");
            return data;
        }
    }

    char *mode;
    u64 address;
    size_t size;
    void *buf;

    switch (command)
    {
    case Command::forceOpenCheatProcess:
        log("Forcing cheat process open");
        rc = dmntcht::forceOpenCheatProcess();
        if (R_FAILED(rc))
        {
            log("Failed to force cheat process open: %lx", rc);
            data->buffer->writeString("Failed to force open cheat process");
        }
        else
        {
            log("Successfully forced cheat process open");
            data->success = true;
        }
        break;

    case Command::readMemory:
        log("Reading memory");
        mode = buffer->readString();
        address = buffer->readUnsignedLong();
        size = buffer->readUnsignedLong();

        if (!strcmp(mode, "heap"))
        {
            address = meta.heap_extents.base + address;
        }
        else if (!strcmp(mode, "main"))
        {
            address = meta.main_nso_extents.base + address;
        }

        log("Reading 0x%lx bytes from 0x%lx", size, address);
        buf = malloc(size);
        rc = dmntcht::readCheatProcessMemory(address, buf, size);
        if (R_FAILED(rc))
        {
            buffer->writeString("Failed to read memory at 0x%lx", address);
        }
        else
        {
            data->success = true;
            data->buffer->write(buf, size);
        }
        break;
    case Command::writeMemory:
        log("Writing memory");
        mode = buffer->readString();
        address = buffer->readUnsignedLong();
        size = buffer->readUnsignedLong();
        buf = buffer->read<void *>(buffer->getReadOffset(), size);

        if (!strcmp(mode, "heap"))
        {
            address = meta.heap_extents.base + address;
        }
        else if (!strcmp(mode, "main"))
        {
            address = meta.main_nso_extents.base + address;
        }

        rc = dmntcht::writeCheatProcessMemory(address, buf, size);
        if (R_FAILED(rc))
        {
            buffer->writeString("Failed to write memory at 0x%lx", address);
        }
        else
        {
            data->success = true;
        }
        break;
    case Command::getTitleID:
        log("Getting title ID");
        data->buffer->writeUnsignedLong(meta.title_id);
        data->success = true;
        break;
    case Command::getBuildID:
        log("Getting build ID");
        data->buffer->writeUnsignedLong(sizeof(u8) * 32);
        data->buffer->write(meta.main_nso_build_id, sizeof(u8) * 32);
        data->success = true;
        break;
    case Command::getHeapBaseAddress:
        log("Getting heap base address");
        data->buffer->writeUnsignedLong(meta.heap_extents.base);
        data->success = true;
        break;
    case Command::getHeapSize:
        log("Getting heap size");
        data->buffer->writeUnsignedLong(meta.heap_extents.size);
        data->success = true;
        break;
    case Command::getMainNsoBaseAddress:
        log("Getting main NSO base address");
        data->buffer->writeUnsignedLong(meta.main_nso_extents.base);
        data->success = true;
        break;
    case Command::getMainNsoSize:
        log("Getting main NSO size");
        data->buffer->writeUnsignedLong(meta.main_nso_extents.size);
        data->success = true;
        break;
    default:
        log("Unknown command %lx", (u8)command);
        data->buffer->writeString("Unknown command");
        break;
    }

    log("Processed command %x", (u8)command);

    return data;
}