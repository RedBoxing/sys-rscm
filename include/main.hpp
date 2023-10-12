#pragma once

#include <switch.h>
#include "packet.hpp"
#include "buffer.hpp"

Packet *read_packet(int socket_fd);

Buffer *
processCommands(Command command, Buffer *buffer);

void flushEvents();