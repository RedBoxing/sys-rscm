#pragma once

#include <switch.h>
#include "packet.hpp"
#include "buffer.hpp"

Buffer *
processCommands(Command command, Buffer *buffer);

void flushEvents();