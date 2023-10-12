#pragma once

#include <switch.h>

#define MAX_BUFFER_SIZE 2048 * 4
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

enum class Status : u8
{
    Stopped,
    Running,
    Paused
};

u64 attachedProcessId = 0;
Handle debugHandle;
Status status = Status::Stopped;

std::vector<Client *> clients;