#include "utils.hpp"
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include "debug.hpp"
#include <vector>

void log(const char *format, ...)
{
    FILE *fp = fopen("logs/sys-rscm.log", "a");

    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);

    for (Client *client : clients)
    {
        Packet *packet = new Packet{new PacketHeader{
            Command::Log,
        }};
    }
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
                log("ProcessAttachEvent(title_id:%08lx, pid:%08lx, name:'%s', mmu_flags:%d)\r\n", pad.title_id, pad.pid, pad.name, pad.mmu_flags);
            }
            break;
            case 1:
            {
                ThreadAttachData tad = event.data.thread_attach;
                log("ThreadAttachEvent(thread_id:%08lx, tls_pointer:%08lx, entry_point:%08lx)\r\n", tad.thread_id, tad.tls_pointer, tad.entry_point);
            }
            break;
            case 2:
            case 3:
            {
                ExitData ed = event.data.exit;
                log("ExitEvent(type:%d)\r\n", ed.type);

                if (ed.type == 0)
                {
                    svcContinueDebugEvent(debugHandle, 4 | 2 | 1, 0, 0);
                }
            }
            break;
            case 4:
            {
                ExceptionData ed = event.data.exception;
                log("ExceptionEvent(type:%d, fault_reg:%08lx, per_exception:%08lx)\r\n", ed.type, ed.fault_reg, ed.per_exception);
            }
            break;
            default:
                log("UnknownEvent(type:%d Data:)", event.event_type);
                /* for (u32 i = 0; i < DEBUG_DATA_SIZE; i++)
                 {
                     log("%02X", event->data.raw[i]);
                 }
                 log(")\r\n");*/
            }
        }
    } while (R_SUCCEEDED(rc));
}