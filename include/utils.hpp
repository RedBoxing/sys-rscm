#pragma once

#include <switch.h>
#include "packet.hpp"

void log(char *format, ...);
void debug(const char *format, ...);

int set_non_blocking(int socket);
void flushEvents();

void randomBytes(unsigned char *buffer, size_t size);