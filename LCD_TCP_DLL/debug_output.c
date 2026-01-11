#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include "debug.h"

#define DEBUG_LOG_FILE "C:\\Temp\\LCD_TCP_Debug.log"

void WINAPI DebugString(LPCSTR format, ...)
{
    FILE* fp;
    va_list arglist;
    char timestamp[64];
    time_t now;
    struct tm timeinfo;

    // Open Read/Write. Fails if file doesn't exist.
    errno_t err = fopen_s(&fp, DEBUG_LOG_FILE, "r+");

    if (err == 0 && fp != NULL)
    {
        fseek(fp, 0, SEEK_END);
        time(&now);
        localtime_s(&timeinfo, &now);
        strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", &timeinfo);

        fputs(timestamp, fp);
        va_start(arglist, format);
        vfprintf(fp, format, arglist);
        va_end(arglist);
        fputs("\n", fp);
        fclose(fp);
    }
}