#pragma once

#include <switch.h>
#include "buffer.hpp"

#define DEBUG_DATA_SIZE (0x30)

#pragma pack(push, 1)
struct ProcessAttachData
{
    u64 title_id;
    u64 pid;
    char name[12];
    u32 mmu_flags;
};

struct ThreadAttachData
{
    u64 thread_id;
    u64 tls_pointer;
    u64 entry_point;
};

struct ExitData
{
    u64 type;
};

struct ExceptionData
{
    u64 type;
    u64 fault_reg;
    u64 per_exception;
};

union DebugData
{
    u8 raw[DEBUG_DATA_SIZE];
    ProcessAttachData proc_attach;
    ThreadAttachData thread_attach;
    // unk2
    ExitData exit;
    ExceptionData exception;
};

struct DebugEvent
{
    u32 event_type;
    u32 flags;
    u64 thread_id;
    DebugData data;
};
#pragma pack(pop)

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

    SetBreakpoint,

    Log
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

Packet *read_packet(int socket_fd);