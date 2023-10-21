#include "debug.hpp"
#include "utils.hpp"

#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>
#include <algorithm>

static void *handle_connection_thread(void *arg)
{
    Client *client = (Client *)arg;
    return client->handle_connection();
}

Client::Client(int socket_fd)
{
    this->socket_fd = socket_fd;
    if (set_non_blocking(this->socket_fd) < 0)
    {
        debug("Failed to set socket to non-blocking");
        close(this->socket_fd);
        return;
    }

    int res;
    pthread_t *thread_id = (pthread_t *)malloc(sizeof(pthread_t));
    if ((res = pthread_create(thread_id, NULL, &handle_connection_thread, (void *)this)) != 0)
    {
        debug("Failed to create thread : %d", res);
        close(this->socket_fd);
    }
}

void Client::send_packet(Packet *packet)
{
    size_t size = packet->data->getWriteOffset();

    packet->data->offset(1 + 16 + 4);
    packet->data->setWriteOffset(0);
    packet->data->writeUnsignedByte((u8)packet->header->command);
    packet->data->write(packet->header->uuid, 16);
    packet->data->writeUnsignedInt(size);

    debug("Sending packet with command %d and size %d", packet->header->command, size);

    size_t bytes_written = 0;
    while (bytes_written >= 0 && bytes_written != (packet->data->getWriteOffset() + size))
    {
        size_t b = (size_t)send(this->socket_fd, packet->data->getBuffer() + bytes_written, packet->data->getWriteOffset() + size - bytes_written, 0);
        if (bytes_written < 0)
        {
            debug("Failed to send data: %d", bytes_written);
            close(this->socket_fd);
            break;
        }

        bytes_written += b;
    }

    free(packet->data);
    free(packet);
}

void *Client::handle_connection()
{
    while (true)
    {
        struct pollfd pfds[1];

        pfds[0].fd = this->socket_fd;
        pfds[0].events = POLLIN;

        int ret = poll(pfds, 1, 0);
        if (ret > 0 && (pfds[0].revents & POLLIN))
        {
            Packet *packet = read_packet(this->socket_fd);
            if (packet == nullptr)
            {
                debug("Failed to read packet");
                break;
            }

            this->handle_packet(packet);
        }

        svcSleepThread(0);
    }

    close(this->socket_fd);
    return NULL;
}

void Client::handle_packet(Packet *packet)
{
    Command command = packet->header->command;
    Buffer *buffer = packet->data;

    Buffer *data = new Buffer(128);
    u8 uuid[16] = {0};
    memcpy(uuid, packet->header->uuid, 16);

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
    int nbrMemInfo;

    debug("Processing command %d", command);

    switch (command)
    {
    case Command::Attach:
        pid = buffer->readUnsignedLong();
        debug("Attaching to process %d", pid);

        if (debugHandle != INVALID_HANDLE)
        {
            debug("Closing old debug handle");

            rc = svcCloseHandle(debugHandle);
            if (R_FAILED(rc))
            {
                debug("Failed to close debug handle: %d", rc);
                data->writeUnsignedInt(rc);
                break;
            }
        }

        rc = svcDebugActiveProcess(&debugHandle, pid);
        if R_SUCCEEDED (rc)
        {
            attachedProcessId = pid;
            status = Status::Paused;
            debug("Attached to process %d", pid);
        }
        else
        {
            debug("Failed to attach to process: %d", rc);
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
                debug("Failed to get debug event: %d", rc);
                data->writeUnsignedInt(rc);
                break;
            }

            rc = svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
            if (R_SUCCEEDED(rc))
                status = Status::Running;
        }

        rc = svcBreakDebugProcess(debugHandle);
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

        nbrMemInfo = 0;
        data->writeUnsignedInt(0);

        for (u32 i = 0; i < max; i++)
        {
            u32 pageinfo;
            meminfo = {0};
            rc = svcQueryDebugProcessMemory(&meminfo, &pageinfo, debugHandle, address);

            data->writeUnsignedInt(rc);
            nbrMemInfo++;

            if (R_SUCCEEDED(rc))
            {
                data->writeUnsignedLong(meminfo.addr);
                data->writeUnsignedLong(meminfo.size);
                data->writeUnsignedInt(meminfo.type);
                data->writeUnsignedInt(meminfo.perm);

                if (meminfo.type == MemoryType::MemType_Reserved)
                {
                    break;
                }
            }
            else
            {
                debug("Failed to query memory: %d", rc);
                break;
            }

            address += meminfo.size;
        }

        data->writeUnsignedInt(0, nbrMemInfo);

        break;
    case Command::ReadMemory:
        address = buffer->readUnsignedLong();
        size = buffer->readUnsignedInt();

        debug("Reading %d bytes from %08lx", size, address);

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
        if (status == Status::Running)
        {
            rc = svcBreakDebugProcess(debugHandle);
            if (R_SUCCEEDED(rc))
                status = Status::Paused;
            data->writeUnsignedInt(rc);
        }
        else
        {
            rc = 0;
        }
        break;
    case Command::Resume:
        if (status == Status::Paused)
        {
            flushEvents();
            rc = svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
            if (R_FAILED(rc))
            {
                debug("Failed to continue debug event: %d", rc);
                data->writeUnsignedInt(rc);
                break;
            }

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
            debug("Failed to get current PID: %d", rc);
        }

        data->writeUnsignedInt(rc);
        data->writeUnsignedLong(pid);
        break;
    case Command::GetAttachedPID:
        data->writeUnsignedLong(attachedProcessId);
        break;
    case Command::GetTitleID:
        pid = buffer->readUnsignedLong();

        rc = pminfoGetProgramId(&tid, pid);
        if (R_FAILED(rc))
        {
            debug("Failed to get title ID: %d", rc);
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
        debug("Unknown command %lx", (u8)command);
        data->writeUnsignedInt(0);
        break;
    }

    if (data->getWriteOffset() > 0)
    {
        Packet *packet = new Packet{new PacketHeader{command, uuid, data->getWriteOffset()}, data};
        this->send_packet(packet);
    }
}