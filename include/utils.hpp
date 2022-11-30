#pragma once

#include <cstdarg>
#include <cstdio>

void log(const char *format, ...)
{
    FILE *fp = fopen("logs/sys-rscm.log", "a");

    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);

    fprintf(fp, "\n");
    fclose(fp);
}
