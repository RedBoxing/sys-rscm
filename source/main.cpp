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
    recv(sock, &length, sizeof(length), 0);

    char *data = (char *)malloc(length);
    recv(sock, data, length, 0);

    return new Buffer(data, length);
}

void *handle_connection(void *arg)
{
    int sock = (intptr_t)arg;

    while (true)
    {
        Buffer *input_buffer = readData(sock);
        Command command = *(Command *)input_buffer->readUnsignedByte();
        Data *data = processCommands(command, input_buffer);
        delete input_buffer;

        log("Sending %lx bytes of data", (sizeof(data) - sizeof(char *) + data->buffer->getSize()));

        int bytes_written = send(sock, data->buffer, sizeof(data) - sizeof(char *), 0);
        bytes_written += send(sock, data->buffer->getBuffer(), data->buffer->getSize(), 0);

        if (bytes_written < 0)
        {
            log("Failed to send data: %d", bytes_written);
            free(data->buffer);
            free(data);
            break;
        }
        else if (bytes_written != data->buffer->getSize() + sizeof(u64))
        {
            log("Failed to send all data: %d", bytes_written);
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

    log("Processing command %s", command);

    switch (command)
    {
    case Command::forceOpenCheatProcess:
        Result rc = dmntcht::forceOpenCheatProcess();
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to force open cheat process");
        }
        else
        {
            data->success = true;
        }
        break;

    case Command::readMemory:
        char *mode = buffer->readString();
        u64 address = buffer->readUnsignedLong();
        size_t size = buffer->readUnsignedLong();

        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
            break;
        }

        if (!strcmp(mode, "heap"))
        {
            address = meta.heap_extents.base + address;
        }
        else if (!strcmp(mode, "main"))
        {
            address = meta.main_nso_extents.base + address;
        }

        log("Reading 0x%lx bytes from 0x%lx", size, address);
        char *buf = (char *)malloc(size);
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
        char *mode = buffer->readString();
        u64 address = buffer->readUnsignedLong();
        size_t size = buffer->readUnsignedLong();
        void *value = buffer->read<void *>(buffer->getReadOffset(), size);

        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
            break;
        }

        if (!strcmp(mode, "heap"))
        {
            address = meta.heap_extents.base + address;
        }
        else if (!strcmp(mode, "main"))
        {
            address = meta.main_nso_extents.base + address;
        }

        rc = dmntcht::writeCheatProcessMemory(address, value, size);
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
        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
        }
        else
        {
            data->buffer->writeUnsignedLong(meta.title_id);
            data->success = true;
        }
        break;
    case Command::getBuildID:
        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
        }
        else
        {

            data->buffer->writeUnsignedLong(sizeof(u8) * 32);
            data->buffer->write(meta.main_nso_build_id, sizeof(u8) * 32);
            data->success = true;
        }
        break;
    case Command::getHeapBaseAddress:
        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
        }
        else
        {
            data->buffer->writeUnsignedLong(meta.heap_extents.base);
            data->success = true;
        }
        break;
    case Command::getHeapSize:
        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
        }
        else
        {
            data->buffer->writeUnsignedLong(meta.heap_extents.size);
            data->success = true;
        }
        break;
    case Command::getMainNsoBaseAddress:
        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
        }
        else
        {
            data->buffer->writeUnsignedLong(meta.main_nso_extents.base);
            data->success = true;
        }
        break;
    case Command::getMainNsoSize:
        dmntcht::CheatProcessMetadata meta;
        Result rc = dmntcht::getCheatProcessMetadata(&meta);
        if (R_FAILED(rc))
        {
            data->buffer->writeString("Failed to get cheat process metadata");
        }
        else
        {
            data->buffer->writeUnsignedLong(meta.main_nso_extents.size);
            data->success = true;
        }
        break;
    default:
        data->buffer->writeString("Unknown command");
        break;
    }

    return data;
}