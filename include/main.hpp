#pragma once

#include <switch.h>
#include "buffer.hpp"

#pragma pack(push, 1)
struct Data
{
    bool success;
    Buffer *buffer;
};
#pragma pack(pop)

enum class Command : uint8_t
{
    forceOpenCheatProcess,
    readMemory,
    writeMemory,
    getTitleID,
    getBuildID,
    getHeapBaseAddress,
    getHeapSize,
    getMainNsoBaseAddress,
    getMainNsoSize,
};

Data *processCommands(Command command, Buffer *buffer);