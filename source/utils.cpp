#include "utils.hpp"
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include "debug.hpp"
#include <vector>
#include <random>
#include <climits>
#include <algorithm>
#include <functional>
#include <sys/socket.h>

using random_bytes_engine = std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;

void log(char *message, ...)
{
    Buffer *buffer = new Buffer(0x104);
    va_list args;
    va_start(args, message);
    debug(message, args);
    buffer->writeString(message, args);
    va_end(args);

    u8 *uuid = new u8[16];
    randomBytes(uuid, 16);

    Packet *packet = new Packet{
        new PacketHeader{Command::Log, uuid, buffer->getWriteOffset()}, buffer};

    for (Client *client : clients)
    {
        // client->send_packet(packet);
    }
}

void debug(const char *format, ...)
{
    FILE *fp = fopen("logs/sys-rscm.log", "a");

    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}

int set_non_blocking(int socket)
{
    int flags;

    if ((flags = fcntl(socket, F_GETFL, 0)) == -1)
        flags = 0;

    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

void flushEvents()
{
    if (debugHandle == INVALID_HANDLE)
        return;

    Result rc;
    do
    {
        DebugEvent event;
        rc = svcGetDebugEvent((u8 *)&event, debugHandle);

        if (R_SUCCEEDED(rc))
        {
            switch (event.event_type)
            {
            case 0:
            {
                ProcessAttachData pad = event.data.proc_attach;
                debug("ProcessAttachEvent(title_id:%08lx, pid:%08lx, name:'%s', mmu_flags:%d)\r\n", pad.title_id, pad.pid, pad.name, pad.mmu_flags);
            }
            break;
            case 1:
            {
                ThreadAttachData tad = event.data.thread_attach;
                debug("ThreadAttachEvent(thread_id:%08lx, tls_pointer:%08lx, entry_point:%08lx)\r\n", tad.thread_id, tad.tls_pointer, tad.entry_point);
            }
            break;
            case 2:
            case 3:
            {
                ExitData ed = event.data.exit;
                debug("ExitEvent(type:%d)\r\n", ed.type);
                // svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);

                /*if (ed.type == 0)
                {
                    svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
                }*/
            }
            break;
            case 4:
            {
                ExceptionData ed = event.data.exception;
                debug("ExceptionEvent(type:%d, fault_reg:%08lx, per_exception:%08lx)\r\n", ed.type, ed.fault_reg, ed.per_exception);
                // svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
            }
            break;
            default:
                debug("UnknownEvent(type:%d Data:)", event.event_type);
                /* for (u32 i = 0; i < DEBUG_DATA_SIZE; i++)
                 {
                     log("%02X", event->data.raw[i]);
                 }
                 log(")\r\n");*/
            }
        }
    } while (R_SUCCEEDED(rc));
}

void randomBytes(unsigned char *buffer, size_t size)
{
    random_bytes_engine rbe;
    std::vector<unsigned char> data(1000);
    std::generate(begin(data), end(data), std::ref(rbe));
    std::copy_n(begin(data), size, buffer);
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

    debug("Received packet with command %d and size %d", header->command, header->size);

    char *data = (char *)malloc(header->size);
    if (header->size > 0 && recv(socket_fd, data, header->size, 0) < 0)
    {
        return nullptr;
    }

    return new Packet{header, new Buffer(data, header->size)};
}