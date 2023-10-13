#pragma once

#ifndef __DEBUG_HPP__
#define __DEBUG_HPP__

#include <switch.h>
#include "client.hpp"
#include <vector>

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

extern u64 attachedProcessId;
extern Handle debugHandle;
extern Status status;

extern std::vector<Client *> clients;

#endif // __DEBUG_HPP__