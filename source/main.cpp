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
                log("Accepted client %d", client_socket);

                Client *client = new Client(client_socket);
                clients.push_back(client);
            }
        }

        for (Client *client : clients)
        {
            if (!client->get_outgoing_packet_queue()->empty())
            {
                Packet *packet = client->get_outgoing_packet_queue()->front();
                client->get_outgoing_packet_queue()->pop();

                size_t size = packet->data->getWriteOffset() + (1 + 16 + 4);

                packet->data->offset(1 + 16 + 4);
                packet->data->setWriteOffset(0);
                packet->data->writeUnsignedByte((u8)packet->header->command);
                packet->data->write(packet->header->uuid, 16);
                packet->data->writeUnsignedInt(size);

                log("Sending packet with command %d and size %d", packet->header->command, size);

                size_t bytes_written = (size_t)send(client->get_socket_fd(), packet->data->getBuffer(), packet->data->getWriteOffset() + size, 0);

                if (bytes_written < 0)
                {
                    log("Failed to send data: %d", bytes_written);
                    free(packet->data);
                    free(packet);
                    break;
                }
                else if (bytes_written != (packet->data->getWriteOffset() + size))
                {
                    log("Failed to send all data: %d", bytes_written);
                    free(packet->data);
                    free(packet);
                    break;
                }

                free(packet->data);
                free(packet);
            }

            struct pollfd pfds[1];

            pfds[0].fd = client->get_socket_fd();
            pfds[0].events = POLLIN;

            int ret = poll(pfds, 1, 0);
            if (ret > 0 && (pfds[0].revents & POLLIN))
            {
                Packet *packet = read_packet(client->get_socket_fd());
                if (packet == nullptr)
                {
                    log("Failed to read packet");
                    continue;
                }

                client->get_incomming_packet_queue()->push(packet);
            }
        }

        flushEvents();
        svcSleepThread(mainLoopSleepTime * 1e+6L);
    }

    close(sock);
    return 0;
}

Packet *read_packet(int socket_fd)
{
    char *raw_buffer = (char *)malloc(1 + 16 + 4);
    if (recv(socket_fd, raw_buffer, 1 + 16 + 4, 0) < 0)
        return nullptr;

    Buffer *buffer = new Buffer(raw_buffer, 1 + 16 + 4);

    u8 command = buffer->readUnsignedByte();
    u8 *uuid = buffer->read_array<u8>(16);
    u32 size = buffer->readUnsignedInt();

    PacketHeader *header = new PacketHeader{static_cast<Command>(command), uuid, size};

    log("Received packet with command %d and size %d", header->command, header->size);

    char *data = (char *)malloc(header->size);
    if (header->size > 0 && recv(socket_fd, data, header->size, 0) < 0)
    {
        log("Failed to read data");
        return nullptr;
    }

    return new Packet{header, new Buffer(data, header->size)};
}
