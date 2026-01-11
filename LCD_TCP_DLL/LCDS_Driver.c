#include <stdio.h>
#include "LCDS_Driver.H"
#include <windows.h>
#include <stdlib.h>
#include "debug.h"
#include "tcp.h"
#include <excpt.h>

// ---------------------------------------------------------
// CONSTANTS & ENUMS
// ---------------------------------------------------------
#define DRIVER_NAME         "LCD Smartie DLL TCP mingw v1.0.0.2b"
#define DEFAULT_PARAMS      "192.168.1.134:2400"
#define USAGE_TEXT          "IP address like:192.168.1.134:2400"

#define BUFFER_SIZE         256
#define PARAM_SIZE          512
#define CMD_OFFSET          3
#define CUSTOM_CHAR_COUNT   8
#define CUSTOM_CHAR_SIZE    8

// Ring Buffer Size for Input Keys
#define KEY_BUF_SIZE        32

typedef enum {
    CMD_INIT = 0x01,
    CMD_SET_BACKLIGHT = 0x02,
    CMD_SET_CONTRAST = 0x03,
    CMD_SET_BRIGHTNESS = 0x04,
    CMD_WRITE_DATA = 0x05,
    CMD_SET_CURSOR = 0x06,
    CMD_CUSTOM_CHAR = 0x07,
    CMD_DE_INIT = 0x0B,
    CMD_SET_GPO = 0x0C, // Matrix Orbital GPO
    CMD_SET_FAN = 0x0D  // Matrix Orbital Fan
} LcdCommand;

// ---------------------------------------------------------
// DATA STRUCTURES
// ---------------------------------------------------------
typedef struct _LCD_INIT_DATA
{
    char paramStr[PARAM_SIZE];
    int sizeX;
    int sizeY;
    int cursorX;
    int cursorY;
    char customChar[CUSTOM_CHAR_COUNT][CUSTOM_CHAR_SIZE];
    unsigned char backLightOnOff;
    unsigned char contrastLevel;
    unsigned char brightnessLevel;
} LCD_INIT_DATA;

// ---------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------
static LCD_INIT_DATA gLcdInitData;
static unsigned char Buf[BUFFER_SIZE];
static CRITICAL_SECTION g_cs;

// Threading & State
volatile int globalConnStatus = 0; // 0=Disconnected, 1=Connected
volatile int bStopThread = 0;
HANDLE hWorkerThread = NULL;

// Connection Params
int IP_Array[4];
uint32_t u32_IP = 0;
uint32_t portNum = 0;

// Input Key Buffer (Ring Buffer)
volatile int g_KeyBuf[KEY_BUF_SIZE];
volatile int g_KeyReadIdx = 0;
volatile int g_KeyWriteIdx = 0;

// ---------------------------------------------------------
// HELPER FUNCTIONS
// ---------------------------------------------------------
uint8_t* GetCmdDataPtr(void) { return (uint8_t*)(&Buf[CMD_OFFSET]); }

// Buffer a key received from TCP (Thread Safe-ish for single producer/consumer)
void BufferKey(int key)
{
    int nextWrite = (g_KeyWriteIdx + 1) % KEY_BUF_SIZE;
    if (nextWrite != g_KeyReadIdx) // If not full
    {
        g_KeyBuf[g_KeyWriteIdx] = key;
        g_KeyWriteIdx = nextWrite;
        // d1printf("Key Input: %d", key); // Uncomment to debug inputs
    }
}

// Pop a key for LCD Smartie
int ReadKeyFromBuffer()
{
    if (g_KeyReadIdx == g_KeyWriteIdx) return -1; // Empty

    int key = g_KeyBuf[g_KeyReadIdx];
    g_KeyReadIdx = (g_KeyReadIdx + 1) % KEY_BUF_SIZE;
    return key;
}

BOOL SendDataTcp(uint32_t len)
{
    int status = -1;
    if ((len < 2) || (len > 250)) return -1;

    // Caller must hold lock
    if (globalConnStatus == 1) {
        status = TcpSendData((char*)Buf, len);
    }
    else {
        return -1;
    }

    if (status < 0) {
        globalConnStatus = 0;
        d1printf("TCP Send Failed. Status: %d. Disconnecting.", status);
    }
    return status;
}

int SendCmdData(uint32_t cmdDataLen)
{
    Buf[0] = 'n';
    Buf[1] = 'w';
    Buf[2] = cmdDataLen;
    // Protocol: 'n', 'w', LEN, [CMD, DATA...]
    return SendDataTcp(cmdDataLen + CMD_OFFSET);
}

// ---------------------------------------------------------
// WORKER THREAD (Connection + Input Reading)
// ---------------------------------------------------------
DWORD WINAPI ConnectionThread(LPVOID lpParam)
{
    char rxBuffer[32];
    d1printf("Worker Thread Started.");

    while (!bStopThread)
    {
        if (globalConnStatus == 0)
        {
            // --- DISCONNECTED: Try to connect ---
            uint32_t targetIP = u32_IP;
            uint16_t targetPort = (uint16_t)portNum;

            if (targetIP != 0 && targetPort != 0)
            {
                int result = TcpInit(targetIP, targetPort);

                if (result == 1 && !bStopThread) {
                    EnterCriticalSection(&g_cs);
                    globalConnStatus = 1;

                    // Resend Config
                    uint8_t* cmdBuf = GetCmdDataPtr();
                    cmdBuf[0] = CMD_INIT;
                    cmdBuf[1] = gLcdInitData.sizeX;
                    cmdBuf[2] = gLcdInitData.sizeY;
                    SendCmdData(3);

                    // Resend Custom Chars
                    for (int i = 0; i < CUSTOM_CHAR_COUNT; i++) {
                        cmdBuf[0] = CMD_CUSTOM_CHAR;
                        cmdBuf[1] = i;
                        for (int k = 0; k < 8; k++) cmdBuf[k + 2] = gLcdInitData.customChar[i][k];
                        SendCmdData(10);
                    }

                    d1printf("Thread: Connected & Restored.");
                    LeaveCriticalSection(&g_cs);
                }
                else {
                    Sleep(2000); // Wait before retry
                }
            }
            else {
                Sleep(1000); // IP not set yet
            }
        }
        else
        {
            // --- CONNECTED: Check for Inputs ---
            // Check for incoming data with 50ms timeout
            int hasData = TcpHasData(50);

            if (hasData > 0)
            {
                // Data exists, read it
                int bytesRead = TcpRecvData(rxBuffer, sizeof(rxBuffer));
                if (bytesRead > 0) {
                    for (int i = 0; i < bytesRead; i++) {
                        BufferKey((unsigned char)rxBuffer[i]);
                    }
                }
                else {
                    // 0 or -1 means disconnect
                    d1printf("TCP Disconnected during read.");
                    EnterCriticalSection(&g_cs);
                    globalConnStatus = 0;
                    TcpDeInit();
                    LeaveCriticalSection(&g_cs);
                }
            }
            else if (hasData < 0) {
                // Error in select
                globalConnStatus = 0;
            }
            // If hasData == 0 (Timeout), loop again
        }
    }

    d1printf("Worker Thread Exiting.");
    return 0;
}

void StopConnectionThread()
{
    if (hWorkerThread != NULL) {
        d1printf("Stopping Worker Thread...");
        bStopThread = 1;
        TcpDeInit(); // Break blocking calls
        WaitForSingleObject(hWorkerThread, 3000);
        CloseHandle(hWorkerThread);
        hWorkerThread = NULL;
        globalConnStatus = 0;
        d1printf("Worker Thread Stopped.");
    }
}

// ---------------------------------------------------------
// EXPORTED FUNCTIONS
// ---------------------------------------------------------

DLL_EXPORT(char*) DISPLAYDLL_Init(LCDS_BYTE size_x, LCDS_BYTE size_y, char* startup_parameters, LCDS_BOOL* ok)
{
    StopConnectionThread(); // Kill old connection on IP change
    EnterCriticalSection(&g_cs);

    if (startup_parameters == NULL || strlen(startup_parameters) == 0) {
        *ok = FALSE;
        LeaveCriticalSection(&g_cs);
        return "Invalid Parameters";
    }

    memset(gLcdInitData.paramStr, 0, PARAM_SIZE);
    strcpy_s(gLcdInitData.paramStr, PARAM_SIZE, startup_parameters);

    sscanf_s(gLcdInitData.paramStr, "%d.%d.%d.%d:%u", &IP_Array[0], &IP_Array[1], &IP_Array[2], &IP_Array[3], &portNum);
    u32_IP = GetIP_U32(IP_Array[0], IP_Array[1], IP_Array[2], IP_Array[3]);

    gLcdInitData.sizeX = size_x;
    gLcdInitData.sizeY = size_y;
    gLcdInitData.cursorX = 1;
    gLcdInitData.cursorY = 1;

    bStopThread = 0;
    hWorkerThread = CreateThread(NULL, 0, ConnectionThread, NULL, 0, NULL);

    *ok = TRUE;
    LeaveCriticalSection(&g_cs);
    return "";
}

DLL_EXPORT(char*) DISPLAYDLL_DriverName(void) { return DRIVER_NAME; }
DLL_EXPORT(char*) DISPLAYDLL_Usage(void) { return USAGE_TEXT; }
DLL_EXPORT(char*) DISPLAYDLL_DefaultParameters(void) { return DEFAULT_PARAMS; }

DLL_EXPORT(void) DISPLAYDLL_SetPosition(LCDS_BYTE x, LCDS_BYTE y)
{
    EnterCriticalSection(&g_cs);
    if (globalConnStatus == 1) {
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_SET_CURSOR;
        if (x < 1) x = 1;
        if (y < 1) y = 1;
        cmdBuf[1] = x - 1;
        cmdBuf[2] = y - 1;
        gLcdInitData.cursorX = x;
        gLcdInitData.cursorY = y;
        SendCmdData(3);
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(void) DISPLAYDLL_Write(char* str)
{
    int i;
    EnterCriticalSection(&g_cs);

    if (globalConnStatus == 1 && str != NULL) {
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_WRITE_DATA;

        __try {
            for (i = 0; i < gLcdInitData.sizeX; i++)
            {
                char charToWrite = ' ';
                if (*str != '\0') {
                    charToWrite = *str;
                    str++;
                }
                cmdBuf[i + 2] = charToWrite;
            }
            cmdBuf[1] = (uint8_t)i;
            SendCmdData(2 + gLcdInitData.sizeX);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(void) DISPLAYDLL_SetBrightness(LCDS_BYTE brightness)
{
    EnterCriticalSection(&g_cs);
    if (globalConnStatus == 1) {
        gLcdInitData.brightnessLevel = brightness;
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_SET_BRIGHTNESS;
        cmdBuf[1] = brightness;
        SendCmdData(2);
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(void) DISPLAYDLL_CustomChar(LCDS_BYTE chr, LCDS_BYTE* data)
{
    int i;
    EnterCriticalSection(&g_cs);
    if (!data || chr < 1 || chr > 8) {
        LeaveCriticalSection(&g_cs);
        return;
    }

    __try {
        for (i = 0; i < 8; i++) gLcdInitData.customChar[chr - 1][i] = data[i];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    if (globalConnStatus == 1) {
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_CUSTOM_CHAR;
        cmdBuf[1] = chr - 1;
        for (i = 0; i < 8; i++) cmdBuf[i + 2] = gLcdInitData.customChar[chr - 1][i];
        SendCmdData(10);
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(LCDS_BYTE) DISPLAYDLL_CustomCharIndex(LCDS_BYTE index)
{
    return index + 7; // Map 1-8 to 8-15
}

DLL_EXPORT(void) DISPLAYDLL_Done(void)
{
    d1printf("Shutdown initiated.");
    StopConnectionThread();
}

// --- NEW INPUT ---
DLL_EXPORT(LCDS_WORD) DISPLAYDLL_ReadKey(void)
{
    int key = ReadKeyFromBuffer();
    if (key != -1) {
        return (LCDS_WORD)key; // LSB=Key, MSB=0
    }
    return 0xFF00; // MSB=0xFF means No Key
}

// --- NEW OUTPUTS ---
DLL_EXPORT(void) DISPLAYDLL_SetGPO(LCDS_BYTE gpo, LCDS_BOOL gpo_on)
{
    EnterCriticalSection(&g_cs);
    if (globalConnStatus == 1) {
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_SET_GPO;
        cmdBuf[1] = gpo;
        cmdBuf[2] = gpo_on;
        SendCmdData(3);
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(void) DISPLAYDLL_SetFan(LCDS_BYTE t1, LCDS_BYTE t2)
{
    EnterCriticalSection(&g_cs);
    if (globalConnStatus == 1) {
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_SET_FAN;
        cmdBuf[1] = t1;
        cmdBuf[2] = t2;
        SendCmdData(3);
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(void) DISPLAYDLL_SetBacklight(LCDS_BOOL light_on)
{
    EnterCriticalSection(&g_cs);
    gLcdInitData.backLightOnOff = light_on;
    if (globalConnStatus == 1) {
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_SET_BACKLIGHT;
        cmdBuf[1] = light_on;
        SendCmdData(2);
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(void) DISPLAYDLL_SetContrast(LCDS_BYTE contrast)
{
    EnterCriticalSection(&g_cs);
    gLcdInitData.contrastLevel = contrast;
    if (globalConnStatus == 1) {
        uint8_t* cmdBuf = GetCmdDataPtr();
        cmdBuf[0] = CMD_SET_CONTRAST;
        cmdBuf[1] = contrast;
        SendCmdData(2);
    }
    LeaveCriticalSection(&g_cs);
}

DLL_EXPORT(void) DISPLAYDLL_PowerResume(void) {}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH: InitializeCriticalSection(&g_cs); break;
    case DLL_PROCESS_DETACH: DeleteCriticalSection(&g_cs); break;
    }
    return TRUE;
}