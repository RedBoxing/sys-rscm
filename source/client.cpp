#include "debug.hpp"
#include "utils.hpp"

#include <sys/socket.h>
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
        log("Failed to set socket to non-blocking");
        close(this->socket_fd);
        return;
    }

    int res;
    pthread_t *thread_id = (pthread_t *)malloc(sizeof(pthread_t));
    if ((res = pthread_create(thread_id, NULL, &handle_connection_thread, (void *)this)) != 0)
    {
        log("Failed to create thread : %d", res);
        close(this->socket_fd);
    }
}

void Client::queue_packet(Packet *packet)
{
    this->outgoing_packet_queue.push(packet);
}

void *Client::handle_connection()
{
    while (true)
    {
        if (!this->incomming_packet_queue.empty())
        {
            Packet *packet = this->incomming_packet_queue.front();
            this->incomming_packet_queue.pop();

            this->handle_packet(packet);
        }

        svcSleepThread(50 * 1e+6L);
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
            status = Status::Paused;
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

        for (u32 i = 0; i < max; i++)
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
                log("Failed to continue debug event: %d", rc);
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
        log("Getting current PID");
        rc = pmdmntGetApplicationProcessId(&pid);
        if (R_FAILED(rc))
        {
            log("Failed to get current PID: %d", rc);
        }

        data->writeUnsignedInt(rc);
        data->writeUnsignedLong(pid);
        log("Current PID: %d", pid);
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

    log("Command %d processed", command);

    if (data->getWriteOffset() > 0)
    {
        log("Sending response for command %d", command);
        Packet *packet = new Packet{new PacketHeader{command, uuid, data->getWriteOffset()}, data};
        this->outgoing_packet_queue.push(packet);
    }
}