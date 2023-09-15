#pragma once

#include <switch.h>
#include "buffer.hpp"

enum class Command : u8
{
    None,

    Attach,
    Detach,
    GetStatus,

    QueryMemory,
    QueryMemoryMulti,
    ReadMemory,
    WriteMemory,

    Pause,
    Resume,

    GetCurrentPID,
    GetAttachedPID,
    GetTitleID,
    GetPIDs,

    SetBreakpoint
};

enum class Status : u8
{
    Stopped,
    Running,
    Paused
};

struct PacketHeader
{
    Command command;
    u8 *uuid; // u8 uuid[16];
    u32 size;
};

struct Packet
{
    PacketHeader *header;
    Buffer *data;
};

Buffer *
processCommands(Command command, Buffer *buffer);