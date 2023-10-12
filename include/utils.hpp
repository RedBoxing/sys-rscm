#pragma once

void log(const char *format, ...);
int set_non_blocking(int socket);
void flushEvents();