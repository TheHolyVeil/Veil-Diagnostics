/*
 * hwdiag.c — UEFI Hardware Diagnostics & Graphical Control Center (x86-64)
 *
 * Freestanding: no libc, no EDK2 headers. Fully self-contained UEFI application
 * with GOP graphical home menu, interactive cursor/pointer, formatted buttons,
 * text diagnostic info pager, 3D spinning donut, and WIP diagnostic suite.
 */

/* ---- CRT symbol required for floating-point ----------------------------- */
int _fltused = 1;

#if defined(_MSC_VER)
  #define EFIAPI __cdecl
#elif defined(__GNUC__) || defined(__clang__)
  #define EFIAPI __attribute__((ms_abi))
#endif

/* ---- Base types -------------------------------------------------------- */
typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef signed char        INT8;
typedef signed short       INT16;
typedef signed int         INT32;
typedef signed long long   INT64;
typedef UINT64             UINTN;
typedef INT64              INTN;
typedef UINT8              BOOLEAN;
typedef UINT16             CHAR16;
typedef void               VOID;
typedef UINT64             EFI_STATUS;
typedef VOID*              EFI_HANDLE;
typedef UINT64             EFI_PHYSICAL_ADDRESS;
typedef UINT64             EFI_VIRTUAL_ADDRESS;
typedef UINTN              EFI_TPL;

#ifndef NULL
#define NULL ((VOID*)0)
#endif

#define TRUE  1
#define FALSE 0

/* Status bits */
#define EFIERR(a) ((EFI_STATUS)(0x8000000000000000ULL | (a)))
#define EFI_ERROR(s) (((INT64)(s)) < 0)

#define EFI_SUCCESS                0
#define EFI_BUFFER_TOO_SMALL       EFIERR(5)
#define EFI_NOT_FOUND              EFIERR(14)
#define EFI_UNSUPPORTED            EFIERR(3)

/* AllocatePages / memory types */
#define EFI_LOADER_DATA 2

#define EFI_RESERVED_MEMORY_TYPE       0
#define EFI_LOADER_CODE                1
#define EFI_LOADER_DATA                2
#define EFI_BOOT_SERVICES_CODE         3
#define EFI_BOOT_SERVICES_DATA         4
#define EFI_RUNTIME_SERVICES_CODE      5
#define EFI_RUNTIME_SERVICES_DATA      6
#define EFI_CONVENTIONAL_MEMORY        7
#define EFI_UNUSABLE_MEMORY            8
#define EFI_ACPI_RECLAIM_MEMORY        9
#define EFI_ACPI_MEMORY_NVS            10
#define EFI_MEMORY_MAPPED_IO           11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12
#define EFI_PAL_CODE                   13
#define EFI_PERSISTENT_MEMORY          14
#define EFI_UNACCEPTED_MEMORY          15

/* Pixel formats (GOP) */
#define PIXEL_RGB_RESERVED_8BIT_PER_COLOR 0
#define PIXEL_BGR_RESERVED_8BIT_PER_COLOR 1
#define PIXEL_BIT_MASK                    2
#define PIXEL_BLT_ONLY                    3

/* ---- GUID --------------------------------------------------------------- */
typedef struct {
  UINT32 Data1;
  UINT16 Data2;
  UINT16 Data3;
  UINT8  Data4[8];
} EFI_GUID;

#define GUID(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
  { (a), (b), (c), { (d0),(d1),(d2),(d3),(d4),(d5),(d6),(d7) } }

static const EFI_GUID gEfiSmbiosTableGuid = GUID(
  0x17088572,0x377F,0x44EF, 0x8F,0x4E,0xB0,0x9F,0xFF,0x46,0xA0,0x70);
static const EFI_GUID gEfiAcpi20TableGuid = GUID(
  0x8868E871,0xE4F1,0x11D3, 0xBC,0x22,0x00,0x80,0xC7,0x3C,0x88,0x81);
static const EFI_GUID gEfiAcpi10TableGuid = GUID(
  0xEB9D2D30,0x2D88,0x11D3, 0x9A,0x16,0x00,0x90,0x27,0x3F,0xC1,0x4D);
static const EFI_GUID gEfiGraphicsOutputProtocolGuid = GUID(
  0x9042A9DE,0x23DC,0x4A38, 0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A);
static const EFI_GUID gEfiGlobalVariableGuid = GUID(
  0x8BE4DF61,0x93CA,0x11D2, 0xAA,0x0D,0x00,0xE0,0x98,0x03,0x2B,0x8C);
static const EFI_GUID gEfiBlockIoProtocolGuid = GUID(
  0x964E5B21,0x6459,0x11D2, 0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B);
static const EFI_GUID gEfiSimplePointerProtocolGuid = GUID(
  0x31878C87,0x0B75,0x11D2, 0x9E,0x49,0x00,0xA0,0xC9,0x69,0x72,0x3B);
static const EFI_GUID gEfiAbsolutePointerProtocolGuid = GUID(
  0x8D59D32B,0xC655,0x4AE9, 0x9B,0x15,0xCA,0x7B,0x56,0x90,0x30,0x69);

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
  const UINT8 *x = (const UINT8*)a, *y = (const UINT8*)b;
  int i;
  for (i = 0; i < 16; i++) if (x[i] != y[i]) return 0;
  return 1;
}

/* ---- Table header ------------------------------------------------------- */
typedef struct {
  UINT32 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 CRC32;
  UINT32 Reserved;
} EFI_TABLE_HEADER;

/* ---- Forward declarations ----------------------------------------------- */
struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct _EFI_BOOT_SERVICES;
struct _EFI_RUNTIME_SERVICES;

/* ---- Simple text output ------------------------------------------------- */
typedef struct {
  INT32  MaxMode;
  INT32  Mode;
  UINT32 Attribute;
  UINT32 CursorColumn;
  UINT32 CursorRow;
  BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
  struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
  struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(
  struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber,
  UINTN *Columns, UINTN *Rows);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(
  struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
  struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
  struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  EFI_TEXT_RESET          Reset;
  EFI_TEXT_STRING         OutputString;
  VOID*                   TestString;
  EFI_TEXT_QUERY_MODE     QueryMode;
  EFI_TEXT_SET_MODE       SetMode;
  EFI_TEXT_SET_ATTRIBUTE  SetAttribute;
  EFI_TEXT_CLEAR_SCREEN   ClearScreen;
  VOID*                   SetCursorPosition;
  VOID*                   EnableCursor;
  SIMPLE_TEXT_OUTPUT_MODE *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* ---- Simple text input -------------------------------------------------- */
typedef struct {
  UINT16 ScanCode;
  CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(
  struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(
  struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
  EFI_INPUT_RESET    Reset;
  EFI_INPUT_READ_KEY ReadKeyStroke;
  VOID*              WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

/* ---- Simple Pointer ---------------------------------------------------- */
typedef struct {
  INT32 RelativeMovementX;
  INT32 RelativeMovementY;
  INT32 RelativeMovementZ;
  BOOLEAN LeftButton;
  BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_STATE;

typedef struct {
  UINT64 ResolutionX;
  UINT64 ResolutionY;
  UINT64 ResolutionZ;
  BOOLEAN LeftButton;
  BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_MODE;

struct _EFI_SIMPLE_POINTER_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_POINTER_RESET)(
  struct _EFI_SIMPLE_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_POINTER_GET_STATE)(
  struct _EFI_SIMPLE_POINTER_PROTOCOL *This, EFI_SIMPLE_POINTER_STATE *State);

typedef struct _EFI_SIMPLE_POINTER_PROTOCOL {
  EFI_SIMPLE_POINTER_RESET Reset;
  EFI_SIMPLE_POINTER_GET_STATE GetState;
  VOID *WaitForKey;
  EFI_SIMPLE_POINTER_MODE *Mode;
} EFI_SIMPLE_POINTER_PROTOCOL;

/* ---- Absolute Pointer -------------------------------------------------- */
typedef struct {
  UINT64 AbsoluteMinX;
  UINT64 AbsoluteMinY;
  UINT64 AbsoluteMinZ;
  UINT64 AbsoluteMaxX;
  UINT64 AbsoluteMaxY;
  UINT64 AbsoluteMaxZ;
  UINT32 Attributes;
} EFI_ABSOLUTE_POINTER_MODE;

typedef struct {
  UINT64 CurrentX;
  UINT64 CurrentY;
  UINT64 CurrentZ;
  UINT32 ActiveButtons;
} EFI_ABSOLUTE_POINTER_STATE;

struct _EFI_ABSOLUTE_POINTER_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_ABSOLUTE_POINTER_RESET)(
  struct _EFI_ABSOLUTE_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_ABSOLUTE_POINTER_GET_STATE)(
  struct _EFI_ABSOLUTE_POINTER_PROTOCOL *This, EFI_ABSOLUTE_POINTER_STATE *State);

typedef struct _EFI_ABSOLUTE_POINTER_PROTOCOL {
  EFI_ABSOLUTE_POINTER_RESET Reset;
  EFI_ABSOLUTE_POINTER_GET_STATE GetState;
  VOID *WaitForKey;
  EFI_ABSOLUTE_POINTER_MODE *Mode;
} EFI_ABSOLUTE_POINTER_PROTOCOL;

/* ---- Time --------------------------------------------------------------- */
typedef struct {
  UINT16 Year;
  UINT8  Month;
  UINT8  Day;
  UINT8  Hour;
  UINT8  Minute;
  UINT8  Second;
  UINT8  Pad1;
  UINT32 Nanosecond;
  INT16  TimeZone;
  UINT8  Daylight;
  UINT8  Pad2;
} EFI_TIME;

/* ---- Runtime services --------------------------------------------------- */
typedef EFI_STATUS (EFIAPI *EFI_GET_TIME)(
  EFI_TIME *Time, VOID *Capabilities);
typedef EFI_STATUS (EFIAPI *EFI_GET_VARIABLE)(
  CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 *Attributes,
  UINTN *DataSize, VOID *Data);

typedef struct _EFI_RUNTIME_SERVICES {
  EFI_TABLE_HEADER  Hdr;
  EFI_GET_TIME      GetTime;
  VOID*             SetTime;
  VOID*             GetWakeupTime;
  VOID*             SetWakeupTime;
  VOID*             SetVirtualAddressMap;
  VOID*             ConvertPointer;
  EFI_GET_VARIABLE  GetVariable;
  VOID*             GetNextVariableName;
  VOID*             SetVariable;
  VOID*             GetNextHighMonotonicCount;
  VOID*             ResetSystem;
  VOID*             UpdateCapsule;
  VOID*             QueryCapsuleCapabilities;
  VOID*             QueryVariableInfo;
} EFI_RUNTIME_SERVICES;

/* ---- Boot services ------------------------------------------------------ */
typedef EFI_STATUS (EFIAPI *BS_GET_MEMORY_MAP)(
  UINTN *MemoryMapSize, VOID *MemoryMap, UINTN *MapKey,
  UINTN *DescriptorSize, UINT32 *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *BS_ALLOCATE_POOL)(
  UINTN PoolType, UINTN Size, VOID **Buffer);
typedef EFI_STATUS (EFIAPI *BS_FREE_POOL)(VOID *Buffer);
typedef EFI_STATUS (EFIAPI *BS_WAIT_FOR_EVENT)(
  UINTN NumberOfEvents, VOID **Event, UINTN *Index);
typedef EFI_STATUS (EFIAPI *BS_STALL)(UINTN Microseconds);
typedef EFI_STATUS (EFIAPI *BS_SET_WATCHDOG_TIMER)(
  UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);
typedef EFI_STATUS (EFIAPI *BS_LOCATE_PROTOCOL)(
  EFI_GUID *Protocol, VOID *Registration, VOID **Interface);
typedef EFI_STATUS (EFIAPI *BS_HANDLE_PROTOCOL)(
  EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface);
typedef EFI_STATUS (EFIAPI *BS_LOCATE_HANDLE_BUFFER)(
  UINTN SearchType, EFI_GUID *Protocol, VOID *SearchKey,
  UINTN *NoHandles, EFI_HANDLE **Buffer);
typedef EFI_STATUS (EFIAPI *BS_OPEN_PROTOCOL)(
  EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface,
  EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, UINT32 Attributes);
typedef EFI_STATUS (EFIAPI *BS_CONNECT_CONTROLLER)(
  EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle,
  VOID *RemainingDevicePath, BOOLEAN Recursive);

#define EFI_LOCATE_BY_PROTOCOL 2
#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL  0x00000001
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL        0x00000002

typedef struct _EFI_BOOT_SERVICES {
  EFI_TABLE_HEADER        Hdr;
  VOID*                  RaiseTPL;
  VOID*                  RestoreTPL;
  VOID*                  AllocatePages;
  VOID*                  FreePages;
  BS_GET_MEMORY_MAP      GetMemoryMap;
  BS_ALLOCATE_POOL       AllocatePool;
  BS_FREE_POOL           FreePool;
  VOID*                  CreateEvent;
  VOID*                  SetTimer;
  BS_WAIT_FOR_EVENT      WaitForEvent;
  VOID*                  SignalEvent;
  VOID*                  CloseEvent;
  VOID*                  CheckEvent;
  VOID*                  InstallProtocolInterface;
  VOID*                  ReinstallProtocolInterface;
  VOID*                  UninstallProtocolInterface;
  BS_HANDLE_PROTOCOL     HandleProtocol;
  VOID*                  Reserved;
  VOID*                  RegisterProtocolNotify;
  VOID*                  LocateHandle;
  VOID*                  LocateDevicePath;
  VOID*                  InstallConfigurationTable;
  VOID*                  LoadImage;
  VOID*                  StartImage;
  VOID*                  Exit;
  VOID*                  UnloadImage;
  VOID*                  ExitBootServices;
  VOID*                  GetNextMonotonicCount;
  BS_STALL               Stall;
  BS_SET_WATCHDOG_TIMER  SetWatchdogTimer;
  BS_CONNECT_CONTROLLER  ConnectController;
  VOID*                  DisconnectController;
  BS_OPEN_PROTOCOL       OpenProtocol;
  VOID*                  CloseProtocol;
  VOID*                  OpenProtocolInformation;
  VOID*                  ProtocolsPerHandle;
  BS_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
  BS_LOCATE_PROTOCOL     LocateProtocol;
  VOID*                  InstallMultipleProtocolInterfaces;
  VOID*                  UninstallMultipleProtocolInterfaces;
  VOID*                  CalculateCrc32;
  VOID*                  CopyMem;
  VOID*                  SetMem;
  VOID*                  CreateEventEx;
} EFI_BOOT_SERVICES;

/* ---- Configuration table / system table --------------------------------- */
typedef struct {
  EFI_GUID VendorGuid;
  VOID*    VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
  EFI_TABLE_HEADER               Hdr;
  CHAR16*                        FirmwareVendor;
  UINT32                         FirmwareRevision;
  EFI_HANDLE                     ConsoleInHandle;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
  EFI_HANDLE                     ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  EFI_HANDLE                     StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
  EFI_RUNTIME_SERVICES*          RuntimeServices;
  EFI_BOOT_SERVICES*            BootServices;
  UINTN                          NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE*      ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---- GOP ---------------------------------------------------------------- */
typedef struct {
  UINT32 RedMask;
  UINT32 GreenMask;
  UINT32 BlueMask;
  UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
  UINT32                  Version;
  UINT32                  HorizontalResolution;
  UINT32                  VerticalResolution;
  UINT32                  PixelFormat;
  EFI_PIXEL_BITMASK      PixelInformation;
  UINT32                  PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  UINT32                                 MaxMode;
  UINT32                                 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
  UINTN                                  SizeOfInfo;
  EFI_PHYSICAL_ADDRESS                   FrameBufferBase;
  UINTN                                  FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef EFI_STATUS (EFIAPI *GOP_QUERY_MODE)(
  VOID *This, UINT32 ModeNumber, UINTN *SizeOfInfo,
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);

typedef struct {
  GOP_QUERY_MODE                         QueryMode;
  VOID*                                  SetMode;
  VOID*                                  Blt;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE*   Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* ---- Memory descriptor -------------------------------------------------- */
typedef struct {
  UINT32               Type;
  UINT32               Pad;
  EFI_PHYSICAL_ADDRESS PhysicalStart;
  EFI_VIRTUAL_ADDRESS  VirtualStart;
  UINTN                NumberOfPages;
  UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* ---- Block I/O ---------------------------------------------------------- */
typedef UINT64 EFI_LBA;

typedef struct {
  UINT32  MediaId;
  BOOLEAN RemovableMedia;
  BOOLEAN MediaPresent;
  BOOLEAN LogicalPartition;
  BOOLEAN ReadOnly;
  BOOLEAN WriteCaching;
  UINT32  BlockSize;
  UINT32  IoAlign;
  EFI_LBA LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct _EFI_BLOCK_IO_PROTOCOL {
  UINT64             Revision;
  EFI_BLOCK_IO_MEDIA *Media;
  VOID*              Reset;
  VOID*              ReadBlocks;
  VOID*              WriteBlocks;
  VOID*              FlushBlocks;
} EFI_BLOCK_IO_PROTOCOL;

/* ========================================================================= */
/* Runtime globals                                                           */
/* ========================================================================= */
static EFI_SYSTEM_TABLE *ST;

#define MAX_POINTER_HANDLES 16
static EFI_SIMPLE_POINTER_PROTOCOL *g_simple_pointers[MAX_POINTER_HANDLES];
static UINTN g_num_simple_pointers = 0;

static EFI_ABSOLUTE_POINTER_PROTOCOL *g_abs_pointers[MAX_POINTER_HANDLES];
static UINTN g_num_abs_pointers = 0;

static UINT64 g_mouse_packets_count = 0;
static INT32 g_last_dx = 0, g_last_dy = 0;

/* ========================================================================= */
/* Minimal runtime services (no libc)                                        */
/* ========================================================================= */
VOID* memset(VOID *dst, int c, UINTN n) {
  UINT8 *p = (UINT8*)dst;
  while (n--) *p++ = (UINT8)c;
  return dst;
}
VOID* memcpy(VOID *dst, const VOID *src, UINTN n) {
  UINT8 *d = (UINT8*)dst; const UINT8 *s = (const UINT8*)src;
  while (n--) *d++ = *s++;
  return dst;
}

/* ========================================================================= */
/* Console printing & Pager                                                  */
/* ========================================================================= */
static CHAR16 wbuf[1024];

#define OUTBUF_MAX 24000
static CHAR16 outbuf[OUTBUF_MAX];
static UINTN  outbuf_len = 0;

static void wprint_raw(const CHAR16 *s) {
  if (ST->ConOut) ST->ConOut->OutputString(ST->ConOut, (CHAR16*)s);
}

static void wprint(const CHAR16 *s) {
  UINTN i = 0;
  while (s[i] && outbuf_len < OUTBUF_MAX - 1) {
    outbuf[outbuf_len++] = s[i++];
  }
}

static void putc16(CHAR16 c) {
  if (c == L'\n') {
    wbuf[0] = L'\r';
    wbuf[1] = 0;
    wprint(wbuf);
  }
  wbuf[0] = c;
  wbuf[1] = 0;
  wprint(wbuf);
}

static void printa(const char *s) {
  UINTN i = 0, o = 0;
  while (s[i] && o < 1000) {
    if (s[i] == '\n') {
      wbuf[o++] = L'\r';
      if (o < 1000) wbuf[o++] = L'\n';
    } else {
      wbuf[o++] = (CHAR16)(UINT8)s[i];
    }
    i++;
  }
  wbuf[o] = 0;
  wprint(wbuf);
}

static void print_u64(UINT64 v, int base, int width) {
  char tmp[24];
  int i = 0, j;
  const char *hexd = "0123456789abcdef";
  if (v == 0) tmp[i++] = '0';
  while (v) {
    tmp[i++] = hexd[v % (UINT64)base];
    v /= (UINT64)base;
  }
  while (i < width) tmp[i++] = '0';
  for (j = i - 1; j >= 0; j--) putc16((CHAR16)(UINT8)tmp[j]);
}

static UINTN strlen_a(const char *s) {
  UINTN n = 0;
  while (s[n]) n++;
  return n;
}

static void uprintf_str(char *buf, UINTN buf_size, const char *fmt, ...) {
  UINTN pos = 0;
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  while (*fmt && pos < buf_size - 1) {
    if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
    fmt++;
    if (*fmt == 'u') {
      UINT64 v = __builtin_va_arg(ap, UINT64);
      char tmp[24]; int i = 0, j;
      if (v == 0) tmp[i++] = '0';
      while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
      for (j = i - 1; j >= 0 && pos < buf_size - 1; j--) buf[pos++] = tmp[j];
    } else if (*fmt == 'd' || *fmt == 'i') {
      INT64 v = __builtin_va_arg(ap, INT64);
      if (v < 0) { buf[pos++] = '-'; v = -v; }
      char tmp[24]; int i = 0, j;
      if (v == 0) tmp[i++] = '0';
      while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
      for (j = i - 1; j >= 0 && pos < buf_size - 1; j--) buf[pos++] = tmp[j];
    }
    if (*fmt) fmt++;
  }
  buf[pos] = 0;
  __builtin_va_end(ap);
}

static void __attribute__((used)) uprintf(const char *fmt, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, fmt);
  while (*fmt) {
    if (*fmt != '%') { putc16((CHAR16)(UINT8)*fmt++); continue; }
    fmt++;

    int left_align = 0;
    int width = 0;
    if (*fmt == '-') { left_align = 1; fmt++; }
    while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

    switch (*fmt) {
      case 's': {
        const char *s = __builtin_va_arg(ap, const char*);
        UINTN len = s ? strlen_a(s) : 0;
        int pad;
        if (!left_align) for (pad = (int)len; pad < width; pad++) putc16(L' ');
        if (s) printa(s);
        if (left_align) for (pad = (int)len; pad < width; pad++) putc16(L' ');
        break;
      }
      case 'S': { const CHAR16 *s = __builtin_va_arg(ap, const CHAR16*); if (s) wprint(s); break; }
      case 'u':
      case 'x':
      case 'X': {
        UINT64 v = __builtin_va_arg(ap, UINT64);
        int base = (*fmt == 'u') ? 10 : 16;
        int fixed_width = (*fmt == 'X') ? 8 : 0;
        if (width > 0 && !left_align) {
          UINT64 tmp = v;
          int digits = (tmp == 0) ? 1 : 0;
          while (tmp) { digits++; tmp /= (UINT64)base; }
          for (; digits < width; digits++) putc16(L' ');
        }
        print_u64(v, base, fixed_width);
        break;
      }
      case 'c': putc16((CHAR16)(UINT8)__builtin_va_arg(ap, int)); break;
      case '%': putc16(L'%'); break;
      default:  putc16((CHAR16)(UINT8)*fmt); break;
    }
    if (*fmt) fmt++;
  }
  __builtin_va_end(ap);
}

static void print_rev(UINT32 rev) {
  UINT32 major = rev >> 16;
  UINT32 minor = ((rev & 0xFFFF) >> 12) * 100 + (((rev & 0xFFFF) >> 8) & 0xF) * 10 + ((rev & 0xFFFF) & 0xF);
  uprintf("%u.%u", (UINT64)major, (UINT64)minor);
}

/* ========================================================================= */
/* Font Data (8x16 Terminus Bitmap)                                          */
/* ========================================================================= */
/* Terminus 8x16 Font Bitmap for ASCII 32..126 */
static const UINT8 font8x16[95][16] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 32 */
  { 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 33 */
  { 0x00, 0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 34 */
  { 0x00, 0x00, 0x24, 0x24, 0x24, 0x7E, 0x24, 0x24, 0x7E, 0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00 }, /* 35 */
  { 0x00, 0x10, 0x10, 0x7C, 0x92, 0x90, 0x90, 0x7C, 0x12, 0x12, 0x92, 0x7C, 0x10, 0x10, 0x00, 0x00 }, /* 36 */
  { 0x00, 0x00, 0x64, 0x94, 0x68, 0x08, 0x10, 0x10, 0x20, 0x2C, 0x52, 0x4C, 0x00, 0x00, 0x00, 0x00 }, /* 37 */
  { 0x00, 0x00, 0x18, 0x24, 0x24, 0x18, 0x30, 0x4A, 0x44, 0x44, 0x44, 0x3A, 0x00, 0x00, 0x00, 0x00 }, /* 38 */
  { 0x00, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 39 */
  { 0x00, 0x00, 0x08, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00 }, /* 40 */
  { 0x00, 0x00, 0x20, 0x10, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00 }, /* 41 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x18, 0x7E, 0x18, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 42 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 43 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x20, 0x00, 0x00, 0x00 }, /* 44 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 45 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 46 */
  { 0x00, 0x00, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00 }, /* 47 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x46, 0x4A, 0x52, 0x62, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 48 */
  { 0x00, 0x00, 0x08, 0x18, 0x28, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00, 0x00, 0x00, 0x00 }, /* 49 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* 50 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x02, 0x1C, 0x02, 0x02, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 51 */
  { 0x00, 0x00, 0x02, 0x06, 0x0A, 0x12, 0x22, 0x42, 0x7E, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00 }, /* 52 */
  { 0x00, 0x00, 0x7E, 0x40, 0x40, 0x40, 0x7C, 0x02, 0x02, 0x02, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 53 */
  { 0x00, 0x00, 0x1C, 0x20, 0x40, 0x40, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 54 */
  { 0x00, 0x00, 0x7E, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 55 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 56 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x3E, 0x02, 0x02, 0x04, 0x38, 0x00, 0x00, 0x00, 0x00 }, /* 57 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 58 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x20, 0x00, 0x00, 0x00 }, /* 59 */
  { 0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00 }, /* 60 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 61 */
  { 0x00, 0x00, 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00 }, /* 62 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x04, 0x08, 0x08, 0x00, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00 }, /* 63 */
  { 0x00, 0x00, 0x7C, 0x82, 0x9E, 0xA2, 0xA2, 0xA2, 0xA6, 0x9A, 0x80, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* 64 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 65 */
  { 0x00, 0x00, 0x7C, 0x42, 0x42, 0x42, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x7C, 0x00, 0x00, 0x00, 0x00 }, /* 66 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x40, 0x40, 0x40, 0x40, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 67 */
  { 0x00, 0x00, 0x78, 0x44, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00, 0x00, 0x00, 0x00 }, /* 68 */
  { 0x00, 0x00, 0x7E, 0x40, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* 69 */
  { 0x00, 0x00, 0x7E, 0x40, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00 }, /* 70 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x40, 0x40, 0x4E, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 71 */
  { 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 72 */
  { 0x00, 0x00, 0x38, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 }, /* 73 */
  { 0x00, 0x00, 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 }, /* 74 */
  { 0x00, 0x00, 0x42, 0x44, 0x48, 0x50, 0x60, 0x60, 0x50, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 75 */
  { 0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* 76 */
  { 0x00, 0x00, 0x82, 0xC6, 0xAA, 0x92, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x00, 0x00, 0x00, 0x00 }, /* 77 */
  { 0x00, 0x00, 0x42, 0x42, 0x42, 0x62, 0x52, 0x4A, 0x46, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 78 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 79 */
  { 0x00, 0x00, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00 }, /* 80 */
  { 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x4A, 0x3C, 0x02, 0x00, 0x00, 0x00 }, /* 81 */
  { 0x00, 0x00, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x7C, 0x50, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 82 */
  { 0x00, 0x00, 0x3C, 0x42, 0x40, 0x40, 0x3C, 0x02, 0x02, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 83 */
  { 0x00, 0x00, 0xFE, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 84 */
  { 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 85 */
  { 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x24, 0x24, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00 }, /* 86 */
  { 0x00, 0x00, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x92, 0xAA, 0xC6, 0x82, 0x00, 0x00, 0x00, 0x00 }, /* 87 */
  { 0x00, 0x00, 0x42, 0x42, 0x24, 0x24, 0x18, 0x18, 0x24, 0x24, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 88 */
  { 0x00, 0x00, 0x82, 0x82, 0x44, 0x44, 0x28, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 89 */
  { 0x00, 0x00, 0x7E, 0x02, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x40, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* 90 */
  { 0x00, 0x00, 0x38, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x00, 0x00, 0x00, 0x00 }, /* 91 */
  { 0x00, 0x00, 0x40, 0x40, 0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00 }, /* 92 */
  { 0x00, 0x00, 0x38, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x38, 0x00, 0x00, 0x00, 0x00 }, /* 93 */
  { 0x00, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 94 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00 }, /* 95 */
  { 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 96 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x02, 0x3E, 0x42, 0x42, 0x42, 0x3E, 0x00, 0x00, 0x00, 0x00 }, /* 97 */
  { 0x00, 0x00, 0x40, 0x40, 0x40, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7C, 0x00, 0x00, 0x00, 0x00 }, /* 98 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x42, 0x40, 0x40, 0x40, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 99 */
  { 0x00, 0x00, 0x02, 0x02, 0x02, 0x3E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3E, 0x00, 0x00, 0x00, 0x00 }, /* 100 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x42, 0x42, 0x7E, 0x40, 0x40, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 101 */
  { 0x00, 0x00, 0x0E, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 102 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3E, 0x02, 0x02, 0x3C, 0x00 }, /* 103 */
  { 0x00, 0x00, 0x40, 0x40, 0x40, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 104 */
  { 0x00, 0x00, 0x10, 0x10, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 }, /* 105 */
  { 0x00, 0x00, 0x04, 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38, 0x00 }, /* 106 */
  { 0x00, 0x00, 0x40, 0x40, 0x40, 0x42, 0x44, 0x48, 0x70, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 107 */
  { 0x00, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 }, /* 108 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x00, 0x00, 0x00, 0x00 }, /* 109 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 110 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 }, /* 111 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00 }, /* 112 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3E, 0x02, 0x02, 0x02, 0x00 }, /* 113 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x5E, 0x60, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00 }, /* 114 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x40, 0x40, 0x3C, 0x02, 0x02, 0x7C, 0x00, 0x00, 0x00, 0x00 }, /* 115 */
  { 0x00, 0x00, 0x10, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0E, 0x00, 0x00, 0x00, 0x00 }, /* 116 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3E, 0x00, 0x00, 0x00, 0x00 }, /* 117 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x24, 0x24, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00 }, /* 118 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x82, 0x92, 0x92, 0x92, 0x92, 0x7C, 0x00, 0x00, 0x00, 0x00 }, /* 119 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x24, 0x18, 0x24, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00 }, /* 120 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3E, 0x02, 0x02, 0x3C, 0x00 }, /* 121 */
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* 122 */
  { 0x00, 0x00, 0x0C, 0x10, 0x10, 0x10, 0x20, 0x10, 0x10, 0x10, 0x10, 0x0C, 0x00, 0x00, 0x00, 0x00 }, /* 123 */
  { 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 }, /* 124 */
  { 0x00, 0x00, 0x30, 0x08, 0x08, 0x08, 0x04, 0x08, 0x08, 0x08, 0x08, 0x30, 0x00, 0x00, 0x00, 0x00 }, /* 125 */
  { 0x00, 0x62, 0x92, 0x8C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 126 */
};


/* ========================================================================= */
/* Graphics Rendering Primitives                                             */
/* ========================================================================= */
static inline UINT32 make_color_gop(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT8 r, UINT8 g, UINT8 b) {
  if (gop && gop->Mode && gop->Mode->Info && gop->Mode->Info->PixelFormat == PIXEL_RGB_RESERVED_8BIT_PER_COLOR) {
    return (UINT32)r | ((UINT32)g << 8) | ((UINT32)b << 16);
  }
  return (UINT32)b | ((UINT32)g << 8) | ((UINT32)r << 16);
}

static void fb_pixel(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int x, int y, UINT32 color) {
  if (x >= 0 && (UINT32)x < w && y >= 0 && (UINT32)y < h) {
    fb[(UINT32)y * stride + (UINT32)x] = color;
  }
}

static void fb_fill_rect(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int rx, int ry, int rw, int rh, UINT32 color) {
  int x, y;
  int x2 = rx + rw;
  int y2 = ry + rh;
  if (rx < 0) rx = 0;
  if (ry < 0) ry = 0;
  if (x2 > (int)w) x2 = (int)w;
  if (y2 > (int)h) y2 = (int)h;

  for (y = ry; y < y2; y++) {
    UINT32 *row = &fb[(UINT32)y * stride];
    for (x = rx; x < x2; x++) {
      row[x] = color;
    }
  }
}

static void fb_draw_rect(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int rx, int ry, int rw, int rh, int border, UINT32 color) {
  fb_fill_rect(fb, stride, w, h, rx, ry, rw, border, color);
  fb_fill_rect(fb, stride, w, h, rx, ry + rh - border, rw, border, color);
  fb_fill_rect(fb, stride, w, h, rx, ry, border, rh, color);
  fb_fill_rect(fb, stride, w, h, rx + rw - border, ry, border, rh, color);
}

static void fb_draw_char(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int x, int y, char c, int scale, UINT32 fg, UINT32 bg, int draw_bg) {
  int idx = (int)(UINT8)c - 32;
  if (idx < 0 || idx >= 95) idx = 0;
  const UINT8 *glyph = font8x16[idx];
  int row, col, sx, sy;

  for (row = 0; row < 16; row++) {
    UINT8 bits = glyph[row];
    for (col = 0; col < 8; col++) {
      int pixel_set = (bits & (0x80 >> col)) != 0;
      UINT32 color = pixel_set ? fg : bg;
      if (pixel_set || draw_bg) {
        for (sy = 0; sy < scale; sy++) {
          for (sx = 0; sx < scale; sx++) {
            fb_pixel(fb, stride, w, h, x + col * scale + sx, y + row * scale + sy, color);
          }
        }
      }
    }
  }
}

static void fb_draw_text(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int x, int y, const char *text, int scale, UINT32 fg, UINT32 bg, int draw_bg) {
  int cx = x;
  while (*text) {
    if (*text == '\n') {
      cx = x;
      y += 16 * scale;
    } else {
      fb_draw_char(fb, stride, w, h, cx, y, *text, scale, fg, bg, draw_bg);
      cx += 8 * scale;
    }
    text++;
  }
}

/* 12x18 Arrow Pointer Cursor with drop shadow, outline, and click pulse indicator */
static const UINT16 cursor_bitmap[18] = {
  0b100000000000,
  0b110000000000,
  0b111000000000,
  0b111100000000,
  0b111110000000,
  0b111111000000,
  0b111111100000,
  0b111111110000,
  0b111111111000,
  0b111111111100,
  0b111111000000,
  0b110111100000,
  0b100011110000,
  0b000001111000,
  0b000000111100,
  0b000000011100,
  0b000000001100,
  0b000000000000
};

static void fb_draw_cursor(UINT32 *fb, UINT32 stride, UINT32 w, UINT32 h, int cx, int cy, UINT32 white_col, UINT32 black_col, BOOLEAN is_clicked) {
  int r, c;
  /* Visual click pulse ring when left mouse button is held down */
  if (is_clicked) {
    fb_draw_rect(fb, stride, w, h, cx - 4, cy - 4, 18, 18, 2, 0x0000F5FF);
  }

  /* Drop shadow */
  for (r = 0; r < 18; r++) {
    UINT16 bits = cursor_bitmap[r];
    for (c = 0; c < 12; c++) {
      if (bits & (0x800 >> c)) {
        fb_pixel(fb, stride, w, h, cx + c + 2, cy + r + 2, 0x00000000);
      }
    }
  }
  /* Outer outline */
  for (r = 0; r < 18; r++) {
    UINT16 bits = cursor_bitmap[r];
    for (c = 0; c < 12; c++) {
      if (bits & (0x800 >> c)) {
        fb_pixel(fb, stride, w, h, cx + c - 1, cy + r, black_col);
        fb_pixel(fb, stride, w, h, cx + c + 1, cy + r, black_col);
        fb_pixel(fb, stride, w, h, cx + c, cy + r - 1, black_col);
        fb_pixel(fb, stride, w, h, cx + c, cy + r + 1, black_col);
      }
    }
  }
  /* Main body */
  UINT32 fill_col = is_clicked ? 0x0038BDF8 : white_col;
  for (r = 0; r < 18; r++) {
    UINT16 bits = cursor_bitmap[r];
    for (c = 0; c < 12; c++) {
      if (bits & (0x800 >> c)) {
        fb_pixel(fb, stride, w, h, cx + c, cy + r, fill_col);
      }
    }
  }
}

/* ========================================================================= */
/* Port I/O Helpers & UEFI Driver Protocol Discovery                        */
/* ========================================================================= */
static inline void outb(UINT16 port, UINT8 val) {
  __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline UINT8 inb(UINT16 port) {
  UINT8 ret;
  __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static void connect_all_controllers(EFI_BOOT_SERVICES *bs) {
  if (!bs || !bs->LocateHandleBuffer || !bs->ConnectController) return;

  UINTN count = 0;
  EFI_HANDLE *handles = NULL;
  #define EFI_ALL_HANDLES 0
  if (!EFI_ERROR(bs->LocateHandleBuffer(EFI_ALL_HANDLES, NULL, NULL, &count, &handles)) && handles) {
    UINTN i;
    for (i = 0; i < count; i++) {
      bs->ConnectController(handles[i], NULL, NULL, TRUE);
    }
    bs->FreePool(handles);
  }
}

static void add_simple_pointer_if_new(EFI_SIMPLE_POINTER_PROTOCOL *sp) {
  if (!sp) return;
  UINTN i;
  for (i = 0; i < g_num_simple_pointers; i++) {
    if (g_simple_pointers[i] == sp) return;
  }
  if (g_num_simple_pointers < MAX_POINTER_HANDLES) {
    g_simple_pointers[g_num_simple_pointers++] = sp;
  }
}

static void add_abs_pointer_if_new(EFI_ABSOLUTE_POINTER_PROTOCOL *ap) {
  if (!ap) return;
  UINTN i;
  for (i = 0; i < g_num_abs_pointers; i++) {
    if (g_abs_pointers[i] == ap) return;
  }
  if (g_num_abs_pointers < MAX_POINTER_HANDLES) {
    g_abs_pointers[g_num_abs_pointers++] = ap;
  }
}

static void init_pointer_protocols(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES *bs) {
  g_num_simple_pointers = 0;
  g_num_abs_pointers = 0;

  if (!bs) return;

  connect_all_controllers(bs);

  if (bs->LocateProtocol) {
    EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
    if (!EFI_ERROR(bs->LocateProtocol((EFI_GUID*)&gEfiSimplePointerProtocolGuid, NULL, (VOID**)&sp)) && sp) {
      add_simple_pointer_if_new(sp);
    }
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL;
    if (!EFI_ERROR(bs->LocateProtocol((EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, NULL, (VOID**)&ap)) && ap) {
      add_abs_pointer_if_new(ap);
    }
  }

  if (ST->ConsoleInHandle && bs->HandleProtocol) {
    EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
    if (!EFI_ERROR(bs->HandleProtocol(ST->ConsoleInHandle, (EFI_GUID*)&gEfiSimplePointerProtocolGuid, (VOID**)&sp)) && sp) {
      add_simple_pointer_if_new(sp);
    }
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL;
    if (!EFI_ERROR(bs->HandleProtocol(ST->ConsoleInHandle, (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, (VOID**)&ap)) && ap) {
      add_abs_pointer_if_new(ap);
    }
  }

  if (bs->LocateHandleBuffer) {
    UINTN count = 0;
    EFI_HANDLE *handles = NULL;

    if (!EFI_ERROR(bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, (EFI_GUID*)&gEfiSimplePointerProtocolGuid, NULL, &count, &handles)) && handles) {
      UINTN i;
      for (i = 0; i < count; i++) {
        EFI_SIMPLE_POINTER_PROTOCOL *sp = NULL;
        if (bs->OpenProtocol && !EFI_ERROR(bs->OpenProtocol(handles[i], (EFI_GUID*)&gEfiSimplePointerProtocolGuid, (VOID**)&sp, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) && sp) {
          add_simple_pointer_if_new(sp);
        } else if (bs->HandleProtocol && !EFI_ERROR(bs->HandleProtocol(handles[i], (EFI_GUID*)&gEfiSimplePointerProtocolGuid, (VOID**)&sp)) && sp) {
          add_simple_pointer_if_new(sp);
        }
      }
      bs->FreePool(handles);
    }

    count = 0; handles = NULL;
    if (!EFI_ERROR(bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, NULL, &count, &handles)) && handles) {
      UINTN i;
      for (i = 0; i < count; i++) {
        EFI_ABSOLUTE_POINTER_PROTOCOL *ap = NULL;
        if (bs->OpenProtocol && !EFI_ERROR(bs->OpenProtocol(handles[i], (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, (VOID**)&ap, ImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) && ap) {
          add_abs_pointer_if_new(ap);
        } else if (bs->HandleProtocol && !EFI_ERROR(bs->HandleProtocol(handles[i], (EFI_GUID*)&gEfiAbsolutePointerProtocolGuid, (VOID**)&ap)) && ap) {
          add_abs_pointer_if_new(ap);
        }
      }
      bs->FreePool(handles);
    }
  }

  /* Flush/reset pointer buffer states once on init */
  UINTN i;
  for (i = 0; i < g_num_simple_pointers; i++) {
    if (g_simple_pointers[i] && g_simple_pointers[i]->Reset) {
      g_simple_pointers[i]->Reset(g_simple_pointers[i], FALSE);
    }
  }
  for (i = 0; i < g_num_abs_pointers; i++) {
    if (g_abs_pointers[i] && g_abs_pointers[i]->Reset) {
      g_abs_pointers[i]->Reset(g_abs_pointers[i], FALSE);
    }
  }
}

static void poll_pointer_inputs(UINT32 screen_w, UINT32 screen_h, int *cursor_x, int *cursor_y, BOOLEAN *curr_left_btn) {
  UINTN i;

  for (i = 0; i < g_num_simple_pointers; i++) {
    EFI_SIMPLE_POINTER_PROTOCOL *sp = g_simple_pointers[i];
    if (!sp || !sp->GetState) continue;

    EFI_SIMPLE_POINTER_STATE state;
    if (sp->GetState(sp, &state) == EFI_SUCCESS) {
      g_mouse_packets_count++;
      
      *cursor_x += state.RelativeMovementX;
      *cursor_y += state.RelativeMovementY;

      if (state.LeftButton) *curr_left_btn = TRUE;
    }
  }

  for (i = 0; i < g_num_abs_pointers; i++) {
    EFI_ABSOLUTE_POINTER_PROTOCOL *ap = g_abs_pointers[i];
    if (!ap || !ap->GetState) continue;

    EFI_ABSOLUTE_POINTER_STATE astate;
    if (ap->GetState(ap, &astate) == EFI_SUCCESS) {
      if (ap->Mode && (ap->Mode->AbsoluteMaxX - ap->Mode->AbsoluteMinX > 0)) {
        *cursor_x = (int)((astate.CurrentX * screen_w) / (ap->Mode->AbsoluteMaxX - ap->Mode->AbsoluteMinX));
        *cursor_y = (int)((astate.CurrentY * screen_h) / (ap->Mode->AbsoluteMaxY - ap->Mode->AbsoluteMinY));
      }
      if (astate.ActiveButtons & 1) *curr_left_btn = TRUE;
    }
  }
}

/* ========================================================================= */
/* CPUID                                                                     */
/* ========================================================================= */
static void cpuidex(UINT32 leaf, UINT32 sub, UINT32 r[4]) {
#if defined(__x86_64__) || defined(_M_X64)
  __asm__ volatile("cpuid"
                   : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                   : "a"(leaf), "c"(sub));
#else
  r[0] = r[1] = r[2] = r[3] = 0;
#endif
}

static void print_cpu_info(void) {
  UINT32 r[4], maxleaf, maxext;
  char brand[49];
  int i;

  cpuidex(0, 0, r);
  maxleaf = r[0];
  {
    UINT32 *vend = (UINT32*)brand;
    vend[0] = r[1]; vend[1] = r[3]; vend[2] = r[2];
    brand[12] = 0;
    uprintf("  Vendor:             %s\n", brand);
  }

  if (maxleaf >= 1) {
    UINT32 fam, model, stepping;
    cpuidex(1, 0, r);
    stepping = r[0] & 0xF;
    fam   = (r[0] >> 8) & 0xF;
    model = (r[0] >> 4) & 0xF;
    if (fam == 0xF) fam += (r[0] >> 20) & 0xFF;
    if (fam == 0x6 || fam == 0xF) model += ((r[0] >> 16) & 0xF) << 4;
    uprintf("  Family:Model:Step: %u:%X:%u\n", (UINT64)fam, (UINT64)model, (UINT64)stepping);

    uprintf("  Features:          ");
    if (r[3] & (1u<<23)) printa(" MMX");
    if (r[3] & (1u<<25)) printa(" SSE");
    if (r[3] & (1u<<26)) printa(" SSE2");
    if (r[2] & (1u<<9))  printa(" SSSE3");
    if (r[2] & (1u<<19)) printa(" SSE4.1");
    if (r[2] & (1u<<20)) printa(" SSE4.2");
    if (r[2] & (1u<<25)) printa(" AES-NI");
    if (r[2] & (1u<<28)) printa(" AVX");
    uprintf("\n");
    if (r[2] & (1u<<21)) printa("  (x2APIC present)\n");

    if (r[2] & (1u<<31)) {
      UINT32 hv[4];
      char hv_vendor[13];
      UINT32 *v = (UINT32*)hv_vendor;
      cpuidex(0x40000000u, 0, hv);
      v[0] = hv[1]; v[1] = hv[2]; v[2] = hv[3];
      hv_vendor[12] = 0;
      uprintf("  Hypervisor:         %s (running in a VM)\n", hv_vendor);
    } else {
      printa("  Hypervisor:         none detected (bare metal, or hidden)\n");
    }
  }

  cpuidex(0x80000000u, 0, r);
  maxext = r[0];
  if (maxext >= 0x80000004u) {
    UINT32 *b = (UINT32*)brand;
    for (i = 0; i < 3; i++) { cpuidex(0x80000002u + (UINT32)i, 0, r); b[i*4+0]=r[0]; b[i*4+1]=r[1]; b[i*4+2]=r[2]; b[i*4+3]=r[3]; }
    brand[48] = 0;
    for (i = 0; brand[i] == ' '; i++);
    uprintf("  Brand:              %s\n", brand + i);
  }

  if (maxext >= 0x80000001u) {
    cpuidex(0x80000001u, 0, r);
    printa("  Long mode (x86-64): ");
    printa((r[3] & (1u<<29)) ? "yes" : "no");
    uprintf("\n");
  }

  if (maxleaf >= 0x16) {
    cpuidex(0x16, 0, r);
    uprintf("  Base frequency:    %u MHz\n", (UINT64)r[0]);
    uprintf("  Max frequency:     %u MHz\n", (UINT64)r[1]);
  } else {
    printa("  Frequency:         (CPUID leaf 0x16 unsupported)\n");
  }

  {
    UINT32 smt = 0, core_level = 0;
    if (maxleaf >= 0x1F) {
      for (i = 0; i < 32; i++) {
        cpuidex(0x1F, (UINT32)i, r);
        if ((r[2] & 0xFF) == 0) break;
        if ((r[2] & 0xFF00) == 0x0100) smt        = r[1] & 0xFFFF;
        if ((r[2] & 0xFF00) == 0x0200) core_level = r[1] & 0xFFFF;
      }
    } else if (maxleaf >= 0xB) {
      for (i = 0; i < 8; i++) {
        cpuidex(0xB, (UINT32)i, r);
        if ((r[2] & 0xFF) == 0) break;
        if ((r[2] & 0xFF00) == 0x0100) core_level = r[1] & 0xFFFF;
        if ((r[2] & 0xFF00) == 0x0200) smt        = core_level ? core_level / (r[1] & 0xFFFF) : 0, core_level = r[1] & 0xFFFF;
      }
    }
    if (core_level) {
      uprintf("  Logical CPUs:      %u", (UINT64)core_level);
      if (smt) uprintf("  (SMT threads/core: %u, physical cores: %u)",
                       (UINT64)smt, (UINT64)(core_level / smt));
      uprintf("\n");
    } else {
      printa("  Topology:          (topology CPUID leaves unsupported)\n");
    }
  }
}

static void section(const char *title) {
  uprintf("\n== %s ==\n", title);
}

static void set_best_text_mode(void) {
  if (!ST->ConOut || !ST->ConOut->Mode) return;

  INT32 max_mode = ST->ConOut->Mode->MaxMode;
  UINTN best_mode = (UINTN)ST->ConOut->Mode->Mode;
  UINTN best_cols = 0, best_rows = 0;
  INT32 m;

  for (m = 0; m < max_mode; m++) {
    UINTN cols = 0, rows = 0;
    if (!EFI_ERROR(ST->ConOut->QueryMode(ST->ConOut, (UINTN)m, &cols, &rows))) {
      if (cols * rows > best_cols * best_rows) {
        best_cols = cols;
        best_rows = rows;
        best_mode = (UINTN)m;
      }
    }
  }

  if (best_cols > 0) {
    ST->ConOut->SetMode(ST->ConOut, best_mode);
  }
}

/* ========================================================================= */
/* PC Speaker & Donut Animation                                              */
/* ========================================================================= */
static void pcspeaker_off(void) {
  outb(0x61, inb(0x61) & 0xFC);
}

static void pcspeaker_on(UINT32 freq_hz) {
  if (freq_hz == 0) { pcspeaker_off(); return; }
  UINT32 divisor = 1193182u / freq_hz;
  outb(0x43, 0xB6);
  outb(0x42, (UINT8)(divisor & 0xFF));
  outb(0x42, (UINT8)((divisor >> 8) & 0xFF));
  outb(0x61, inb(0x61) | 0x03);
}

static const struct { UINT32 freq; UINT32 dur_ms; } donut_tune[] = {
  {523, 150}, {659, 150}, {784, 150}, {1047, 300},
  {880, 150}, {784, 150}, {659, 150}, {523, 300},
  {0,   100},
  {659, 150}, {784, 150}, {880, 150}, {1047, 400},
  {0,   300},
};
#define DONUT_TUNE_LEN (sizeof(donut_tune) / sizeof(donut_tune[0]))

#define DONUT_W 70
#define DONUT_H 22
static char  donut_out[DONUT_H][DONUT_W + 1];
static float donut_z[DONUT_H][DONUT_W];

static void print_ascii_row_raw(const char *s) {
  CHAR16 line[DONUT_W + 3];
  UINTN i = 0;
  while (s[i]) { line[i] = (CHAR16)(UINT8)s[i]; i++; }
  line[i++] = L'\r';
  line[i++] = L'\n';
  line[i] = 0;
  wprint_raw(line);
}

static void render_donut_frame(float cosA, float sinA, float cosB, float sinB) {
  const float R1 = 1.0f, R2 = 2.0f, K2 = 5.0f;
  const float K1 = (float)DONUT_W * K2 * 3.0f / (8.0f * (R1 + R2));
  UINTN x, y, pi, ti;
  float cosPhi = 1.0f, sinPhi = 0.0f;

  for (y = 0; y < DONUT_H; y++) {
    for (x = 0; x < DONUT_W; x++) { donut_out[y][x] = ' '; donut_z[y][x] = 0.0f; }
    donut_out[y][DONUT_W] = 0;
  }

  for (pi = 0; pi < 314; pi++) {
    float cosTheta = 1.0f, sinTheta = 0.0f;
    for (ti = 0; ti < 90; ti++) {
      float circleX = R2 + R1 * cosTheta;
      float circleY = R1 * sinTheta;

      float xw = circleX * (cosB * cosPhi + sinA * sinB * sinPhi) - circleY * cosA * sinB;
      float yw = circleX * (sinB * cosPhi - sinA * cosB * sinPhi) + circleY * cosA * cosB;
      float ze = K2 + cosA * circleX * sinPhi + circleY * sinA;
      float ooz = 1.0f / ze;

      int xp = (int)((float)DONUT_W / 2.0f + K1 * ooz * xw);
      int yp = (int)((float)DONUT_H / 2.0f - 0.5f * K1 * ooz * yw);

      float L = cosPhi * cosTheta * sinB - cosA * cosTheta * sinPhi - sinA * sinTheta
               + cosB * (cosA * sinTheta - cosTheta * sinA * sinPhi);

      if (xp >= 0 && xp < DONUT_W && yp >= 0 && yp < DONUT_H && L > 0.0f) {
        if (ooz > donut_z[yp][xp]) {
          int lum = (int)(L * 8.0f);
          if (lum > 11) lum = 11;
          if (lum < 0) lum = 0;
          donut_z[yp][xp] = ooz;
          donut_out[yp][xp] = ".,-~:;=!*#$@"[lum];
        }
      }

      float nCosT = cosTheta * 0.997551000f - sinTheta * 0.069943559f;
      float nSinT = sinTheta * 0.997551000f + cosTheta * 0.069943559f;
      cosTheta = nCosT; sinTheta = nSinT;
    }
    float nCosP = cosPhi * 0.999800007f - sinPhi * 0.019998667f;
    float nSinP = sinPhi * 0.999800007f + cosPhi * 0.019998667f;
    cosPhi = nCosP; sinPhi = nSinP;
  }

  ST->ConOut->ClearScreen(ST->ConOut);
  for (y = 0; y < DONUT_H; y++) print_ascii_row_raw(donut_out[y]);
  print_ascii_row_raw("(Press Esc, Enter, Space, Q, or Mouse Click to return)");
}

static void easter_egg_donut(EFI_BOOT_SERVICES *bs) {
  float cosA = 1.0f, sinA = 0.0f, cosB = 1.0f, sinB = 0.0f;
  UINTN note_idx = 0;
  UINT64 note_elapsed_us = 0;

  /* Flush pending keyboard buffer */
  if (ST->ConIn) {
    EFI_INPUT_KEY dummy;
    while (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &dummy)));
  }

  for (;;) {
    if (DONUT_TUNE_LEN > 0) {
      if (donut_tune[note_idx].freq > 0) pcspeaker_on(donut_tune[note_idx].freq);
      else pcspeaker_off();
    }

    render_donut_frame(cosA, sinA, cosB, sinB);

    /* Keyboard Exit Check */
    if (ST->ConIn) {
      EFI_INPUT_KEY key = {0, 0};
      if (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &key))) {
        if (key.ScanCode != 0 || key.UnicodeChar != 0) break;
      }
    }

    /* Mouse Click Exit Check */
    BOOLEAN mouse_click = FALSE;
    int dummy_x = 0, dummy_y = 0;
    poll_pointer_inputs(800, 600, &dummy_x, &dummy_y, &mouse_click);
    if (mouse_click) break;

    if (bs && bs->Stall) bs->Stall(33000);

    note_elapsed_us += 33000;
    if (DONUT_TUNE_LEN > 0 && note_elapsed_us >= (UINT64)donut_tune[note_idx].dur_ms * 1000) {
      note_elapsed_us = 0;
      note_idx = (note_idx + 1 < DONUT_TUNE_LEN) ? note_idx + 1 : 0;
    }

    float nCosA = cosA * 0.999200107f - sinA * 0.039989334f;
    float nSinA = sinA * 0.999200107f + cosA * 0.039989334f;
    cosA = nCosA; sinA = nSinA;
    float nCosB = cosB * 0.999800007f - sinB * 0.019998667f;
    float nSinB = sinB * 0.999800007f + cosB * 0.019998667f;
    cosB = nCosB; sinB = nSinB;
  }
  pcspeaker_off();
}

/* ========================================================================= */
/* Text Output Pager                                                         */
/* ========================================================================= */
#define SCAN_UP        0x01
#define SCAN_DOWN      0x02
#define SCAN_RIGHT     0x03
#define SCAN_LEFT      0x04
#define SCAN_PAGE_UP   0x09
#define SCAN_PAGE_DOWN 0x0A
#define SCAN_ESC       0x17

#define PAGER_MAX_LINES 700

static UINTN line_starts[PAGER_MAX_LINES];

static void print_uint_raw(UINT64 v) {
  CHAR16 tmp[24], out[24];
  int i = 0, j;
  if (v == 0) tmp[i++] = L'0';
  while (v) { tmp[i++] = (CHAR16)(L'0' + (v % 10)); v /= 10; }
  for (j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
  out[i] = 0;
  wprint_raw(out);
}

static void render_line(UINTN start, UINTN end) {
  CHAR16 line[512];
  UINTN n = 0, k;
  for (k = start; k < end && n < 509; k++) {
    if (outbuf[k] == L'\r' || outbuf[k] == L'\n') continue;
    line[n++] = outbuf[k];
  }
  line[n++] = L'\r';
  line[n++] = L'\n';
  line[n] = 0;
  wprint_raw(line);
}

static EFI_INPUT_KEY read_key_nonblocking(void) {
  EFI_INPUT_KEY key = { 0, 0 };
  if (ST->ConIn) {
    ST->ConIn->ReadKeyStroke(ST->ConIn, &key);
  }
  return key;
}

static void run_pager(EFI_BOOT_SERVICES *bs) {
  UINTN line_count = 0, i, cols = 0, rows = 0, top, max_top, visible_rows, shown;

  line_starts[line_count++] = 0;
  for (i = 0; i < outbuf_len && line_count < PAGER_MAX_LINES; i++) {
    if (outbuf[i] == L'\n' && i + 1 < outbuf_len) {
      line_starts[line_count++] = i + 1;
    }
  }

  if (!ST->ConOut || ST->ConOut->QueryMode(ST->ConOut, (UINTN)ST->ConOut->Mode->Mode, &cols, &rows) != 0 || rows < 3) {
    rows = 25;
  }
  visible_rows = rows - 2;
  max_top = (line_count > visible_rows) ? (line_count - visible_rows) : 0;
  top = 0;

  /* Flush key buffer before entering pager */
  if (ST->ConIn) {
    EFI_INPUT_KEY dummy;
    while (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &dummy)));
  }

  for (;;) {
    ST->ConOut->ClearScreen(ST->ConOut);
    shown = 0;
    for (i = top; i < line_count && shown < visible_rows; i++, shown++) {
      UINTN end = (i + 1 < line_count) ? line_starts[i + 1] : outbuf_len;
      render_line(line_starts[i], end);
    }

    wprint_raw(L"-- line ");
    print_uint_raw(top + 1);
    wprint_raw(L"-");
    print_uint_raw(top + shown);
    wprint_raw(L" of ");
    print_uint_raw(line_count);
    wprint_raw(L" | Up/Dn scroll, PgUp/PgDn page | Press Esc, Enter, Space, Q, or Click to Exit --");

    BOOLEAN exit_requested = FALSE;
    EFI_INPUT_KEY key = read_key_nonblocking();

    if (key.ScanCode == SCAN_UP) {
      if (top > 0) top--;
    } else if (key.ScanCode == SCAN_DOWN) {
      if (top < max_top) top++;
    } else if (key.ScanCode == SCAN_PAGE_UP) {
      top = (top > visible_rows) ? top - visible_rows : 0;
    } else if (key.ScanCode == SCAN_PAGE_DOWN) {
      top = (top + visible_rows < max_top) ? top + visible_rows : max_top;
    } else if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 ||
               key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n' ||
               key.UnicodeChar == L'q'  || key.UnicodeChar == L'Q'  ||
               key.UnicodeChar == L' '  || key.UnicodeChar == L'b'  || key.UnicodeChar == L'B') {
      exit_requested = TRUE;
    }

    /* Check mouse click to exit pager */
    BOOLEAN mouse_click = FALSE;
    int dummy_x = 0, dummy_y = 0;
    poll_pointer_inputs(800, 600, &dummy_x, &dummy_y, &mouse_click);
    if (mouse_click) exit_requested = TRUE;

    if (exit_requested) break;

    if (bs && bs->Stall) bs->Stall(20000);
  }
}

/* ========================================================================= */
/* Hardware Information Gathering                                            */
/* ========================================================================= */
static void gather_hardware_info(EFI_HANDLE ImageHandle) {
  EFI_BOOT_SERVICES *bs = ST->BootServices;
  EFI_RUNTIME_SERVICES *rt = ST->RuntimeServices;
  UINTN i;

  outbuf_len = 0;
  uprintf("hwdiag.efi — Hardware Information Summary\n");

  /* ---- Firmware ------------------------------------------------------- */
  section("Firmware / Platform");
  if (ST->FirmwareVendor) uprintf("  Firmware vendor:   %S\n", ST->FirmwareVendor);
  uprintf("  Firmware revision: 0x%X\n", (UINT64)ST->FirmwareRevision);
  uprintf("  UEFI spec version: ");
  print_rev(ST->Hdr.Revision);
  uprintf("\n");
  if (ST->ConOut) {
    UINTN cols = 0, rows = 0;
    ST->ConOut->QueryMode(ST->ConOut, (UINTN)ST->ConOut->Mode->Mode, &cols, &rows);
    uprintf("  Console mode:      %u x %u\n", (UINT64)cols, (UINT64)rows);
  }
  {
    EFI_TIME t;
    if (rt && rt->GetTime && !EFI_ERROR(rt->GetTime(&t, NULL))) {
      uprintf("  RTC:               %u-%u-%u %u:%u:%u\n",
              (UINT64)t.Year, (UINT64)t.Month, (UINT64)t.Day,
              (UINT64)t.Hour, (UINT64)t.Minute, (UINT64)t.Second);
    }
  }
  uprintf("  Image handle:      0x%x\n", (UINT64)(UINTN)ImageHandle);

  /* ---- Secure Boot ---------------------------------------------------- */
  section("Secure Boot");
  {
    static CHAR16 varname[] = { 'S','e','c','u','r','e','B','o','o','t',0 };
    UINT8 val = 0;
    UINTN size = sizeof(val);
    UINT32 attrs = 0;
    if (rt && rt->GetVariable && !EFI_ERROR(rt->GetVariable(varname, (EFI_GUID*)&gEfiGlobalVariableGuid, &attrs, &size, &val))) {
      uprintf("  SecureBoot:        %s (0x%x)\n", val ? "ENABLED" : "disabled", (UINT64)val);
    } else {
      printa("  SecureBoot:        variable not accessible ( firmware may not support it )\n");
    }
  }

  /* ---- CPU ------------------------------------------------------------ */
  section("CPU");
  print_cpu_info();

  /* ---- Memory --------------------------------------------------------- */
  section("Memory Map");
  if (bs) {
    UINTN mapsize = 0, mapkey = 0, descsize = 0;
    UINT32 descver = 0;
    VOID *mmap = NULL;
    EFI_MEMORY_DESCRIPTOR *d;
    UINT8 *p;
    static const char *type_names[16] = {
      "Reserved",          "LoaderCode",   "LoaderData",      "BootServicesCode",
      "BootServicesData",  "RuntimeCode",  "RuntimeData",     "Conventional",
      "Unusable",          "ACPIReclaim",  "ACPI_NVS",        "MMIO",
      "MMIO_PortSpace",    "PalCode",      "Persistent",      "Unaccepted"
    };
    UINT64 pages_by_type[16];
    UINT64 total_pages = 0, usable_pages = 0, largest_free_pages = 0;

    for (i = 0; i < 16; i++) pages_by_type[i] = 0;

    EFI_STATUS s = bs->GetMemoryMap(&mapsize, NULL, &mapkey, &descsize, &descver);
    if (EFI_ERROR(s) && s != EFI_BUFFER_TOO_SMALL) {
      uprintf("  GetMemoryMap probe failed: 0x%x\n", (UINT64)s);
    } else {
      mapsize += 2 * descsize + 64;
      if (!EFI_ERROR(bs->AllocatePool(EFI_LOADER_DATA, mapsize, &mmap))) {
        s = bs->GetMemoryMap(&mapsize, mmap, &mapkey, &descsize, &descver);
        if (EFI_ERROR(s)) {
          uprintf("  GetMemoryMap failed: 0x%x\n", (UINT64)s);
        } else {
          uprintf("  Descriptor size:   %u bytes, version %u\n",
                  (UINT64)descsize, (UINT64)descver);
          for (p = (UINT8*)mmap; p < (UINT8*)mmap + mapsize; p += descsize) {
            d = (EFI_MEMORY_DESCRIPTOR*)p;
            if (d->Type < 16) pages_by_type[d->Type] += d->NumberOfPages;
            total_pages += d->NumberOfPages;
            if (d->Type == EFI_CONVENTIONAL_MEMORY) {
              usable_pages += d->NumberOfPages;
              if (d->NumberOfPages > largest_free_pages) largest_free_pages = d->NumberOfPages;
            }
          }
          uprintf("  Total mapped RAM:  %u MB\n", (UINT64)((total_pages << 12) >> 20));
          uprintf("  Usable (convent.): %u MB\n", (UINT64)((usable_pages << 12) >> 20));
          uprintf("  Largest free block:%u MB\n", (UINT64)((largest_free_pages << 12) >> 20));
          uprintf("  %-22s %12s\n", "Type", "MB");
          for (i = 0; i < 16; i++) {
            if (pages_by_type[i])
              uprintf("  %-22s %12u\n", type_names[i],
                      (UINT64)((pages_by_type[i] << 12) >> 20));
          }
        }
        bs->FreePool(mmap);
      } else {
        printa("  AllocatePool failed for memory map\n");
      }
    }
  }

  /* ---- Configuration tables: ACPI / SMBIOS ---------------------------- */
  section("ACPI / SMBIOS");
  {
    VOID *rsdp = NULL, *smbios = NULL;
    int have20 = 0;
    for (i = 0; i < ST->NumberOfTableEntries; i++) {
      if (guid_equal(&ST->ConfigurationTable[i].VendorGuid, &gEfiAcpi20TableGuid)) {
        rsdp = ST->ConfigurationTable[i].VendorTable; have20 = 1;
      }
      if (guid_equal(&ST->ConfigurationTable[i].VendorGuid, &gEfiAcpi10TableGuid) && !rsdp) {
        rsdp = ST->ConfigurationTable[i].VendorTable;
      }
      if (guid_equal(&ST->ConfigurationTable[i].VendorGuid, &gEfiSmbiosTableGuid)) {
        smbios = ST->ConfigurationTable[i].VendorTable;
      }
    }
    if (rsdp) {
      UINT8 *b = (UINT8*)rsdp;
      char oem[7];
      uprintf("  RSDP signature:    %c%c%c%c%c%c%c%c\n",
              b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7]);
      for (i = 0; i < 6; i++) oem[i] = (char)b[9+i];
      oem[6] = 0;
      uprintf("  ACPI OEM ID:       %s\n", oem);
      uprintf("  ACPI revision:     %u%s\n", (UINT64)b[15], have20 ? " (ACPI 2.0+ XSDT)" : " (ACPI 1.0 RSDT)");
    } else {
      printa("  RSDP:              not found\n");
    }
    if (smbios) {
      UINT8 *b = (UINT8*)smbios;
      if (b[0]=='_'&&b[1]=='S'&&b[2]=='M'&&b[3]=='_'&&b[4]=='3') {
        uprintf("  SMBIOS:            %u.%u (3.x entry point)\n", (UINT64)b[7], (UINT64)b[8]);
        uprintf("  SMBIOS table addr: 0x%x\n", *(UINT64*)(b + 0x11));
      } else if (b[0]=='_'&&b[1]=='S'&&b[2]=='M'&&b[3]=='_') {
        uprintf("  SMBIOS:            %u.%u\n", (UINT64)b[7], (UINT64)b[8]);
        uprintf("  SMBIOS table addr: 0x%x\n", (UINT64)*(UINT32*)(b + 0x1B));
        uprintf("  Structures:        %u\n", (UINT64)*(UINT16*)(b + 0x1F));
      } else {
        printa("  SMBIOS:            table present, unknown anchor\n");
      }
    } else {
      printa("  SMBIOS:            not found\n");
    }
  }

  /* ---- Graphics -------------------------------------------------------- */
  section("Graphics");
  if (bs) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    if (!EFI_ERROR(bs->LocateProtocol((EFI_GUID*)&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gop)) && gop) {
      static const char *pixfmt[4] = { "RGBX 8-bit", "BGRX 8-bit", "bitmask", "blt-only" };
      EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m = gop->Mode;
      UINTN count = 0;
      UINT32 j;
      for (j = 0; ; j++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN infosz = 0;
        if (EFI_ERROR(gop->QueryMode(gop, j, &infosz, &info))) break;
        count++;
      }
      uprintf("  GOP modes:         %u (current: mode %u)\n", (UINT64)count, (UINT64)m->Mode);
      uprintf("  Current mode:      %u x %u, pixel format %s\n",
              (UINT64)m->Info->HorizontalResolution,
              (UINT64)m->Info->VerticalResolution,
              pixfmt[m->Info->PixelFormat < 4 ? m->Info->PixelFormat : 3]);
      uprintf("  Pixels/scanline:   %u\n", (UINT64)m->Info->PixelsPerScanLine);
      uprintf("  Framebuffer:       0x%x (%u MB)\n",
              (UINT64)m->FrameBufferBase, (UINT64)(m->FrameBufferSize >> 20));
    } else {
      printa("  GOP:               not available (text mode / no driver)\n");
    }
  }

  /* ---- Storage (Block I/O) --------------------------------------------- */
  section("Storage (Block I/O)");
  if (bs && bs->LocateHandleBuffer && bs->HandleProtocol) {
    UINTN count = 0;
    EFI_HANDLE *handles = NULL;
    EFI_STATUS s = bs->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL,
                                           (EFI_GUID*)&gEfiBlockIoProtocolGuid,
                                           NULL, &count, &handles);
    if (EFI_ERROR(s) || count == 0) {
      printa("  No block I/O devices found\n");
    } else {
      UINTN dev = 0;
      for (i = 0; i < count; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio = NULL;
        if (EFI_ERROR(bs->HandleProtocol(handles[i], (EFI_GUID*)&gEfiBlockIoProtocolGuid, (VOID**)&bio))
            || !bio || !bio->Media) {
          continue;
        }
        EFI_BLOCK_IO_MEDIA *media = bio->Media;
        if (media->LogicalPartition) continue;

        dev++;
        uprintf("  Device %u:\n", (UINT64)dev);
        uprintf("    Media present:   %s\n", media->MediaPresent ? "yes" : "no");
        uprintf("    Removable:       %s\n", media->RemovableMedia ? "yes" : "no");
        uprintf("    Read only:       %s\n", media->ReadOnly ? "yes" : "no");
        uprintf("    Block size:      %u bytes\n", (UINT64)media->BlockSize);
        if (media->MediaPresent && media->BlockSize > 0) {
          UINT64 size_bytes = (media->LastBlock + 1) * (UINT64)media->BlockSize;
          uprintf("    Capacity:        %u MB\n", (UINT64)(size_bytes >> 20));
        }
      }
      if (dev == 0) printa("  No physical (non-partition) block devices found\n");
      bs->FreePool(handles);
    }
  } else {
    printa("  Block I/O enumeration unavailable\n");
  }
}

/* ========================================================================= */
/* Graphical UI Page & Menu Engine                                           */
/* ========================================================================= */
typedef struct {
  int x, y, w, h;
  const char *title;
  const char *subtext;
  UINT32 bg_normal, bg_hover, bg_active;
  UINT32 border_normal, border_hover;
  UINT32 fg_title, fg_subtext;
} UI_BUTTON;

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
  return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

/* Render Diagnostic WIP Page */
static int render_diagnostic_wip_screen(
  EFI_HANDLE ImageHandle,
  EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
  UINT32 *fb, UINT32 *back_buf, UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn)
{
  (VOID)ImageHandle;
  UINT32 col_bg       = make_color_gop(gop, 15, 23, 42);   /* #0F172A */
  UINT32 col_card     = make_color_gop(gop, 30, 41, 59);   /* #1E293B */
  UINT32 col_topbar   = make_color_gop(gop, 15, 23, 42);   /* Top bar bg */
  UINT32 col_border   = make_color_gop(gop, 51, 65, 85);   /* #334155 */
  UINT32 col_amber    = make_color_gop(gop, 245, 158, 11); /* #F59E0B */
  UINT32 col_white    = make_color_gop(gop, 255, 255, 255);
  UINT32 col_gray     = make_color_gop(gop, 148, 163, 184);/* #94A3B8 */
  UINT32 col_btn_bg   = make_color_gop(gop, 37, 99, 235);  /* #2563EB */
  UINT32 col_btn_hov  = make_color_gop(gop, 59, 130, 246); /* #3B82F6 */
  UINT32 col_exit_norm= make_color_gop(gop, 153, 27, 27);  /* #991B1B */
  UINT32 col_exit_hov = make_color_gop(gop, 220, 38, 38);  /* #DC2626 */

  int card_w = 600, card_h = 360;
  int card_x = ((int)screen_w - card_w) / 2;
  int card_y = ((int)screen_h - card_h) / 2 + 10;

  int back_btn_x = card_x + (card_w - 240) / 2;
  int back_btn_y = card_y + card_h - 60;
  int back_btn_w = 240, back_btn_h = 42;

  int top_back_x = 15, top_back_y = 10, top_back_w = 90, top_back_h = 32;
  int top_exit_x = (int)screen_w - 110, top_exit_y = 10, top_exit_w = 95, top_exit_h = 32;

  /* Flush key buffer */
  if (ST->ConIn) {
    EFI_INPUT_KEY dummy;
    while (!EFI_ERROR(ST->ConIn->ReadKeyStroke(ST->ConIn, &dummy)));
  }

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;
    BOOLEAN curr_left_btn = FALSE;

    /* Poll Pointer Device Movements */
    poll_pointer_inputs(screen_w, screen_h, cursor_x, cursor_y, &curr_left_btn);

    BOOLEAN click_event = (curr_left_btn && !(*prev_left_btn));
    *prev_left_btn = curr_left_btn;

    /* Handle Keyboard */
    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode == SCAN_ESC || key.UnicodeChar == 27 || key.UnicodeChar == L'b' || key.UnicodeChar == L'B' || key.UnicodeChar == L'\r') {
      return 0; /* Back to home */
    }
    if (key.UnicodeChar == L'x' || key.UnicodeChar == L'X') {
      return 1; /* Exit application */
    }

    /* Correct UEFI ScanCode directional cursor movement */
    if (key.ScanCode == SCAN_UP)    *cursor_y -= 15;
    if (key.ScanCode == SCAN_DOWN)  *cursor_y += 15;
    if (key.ScanCode == SCAN_LEFT)  *cursor_x -= 15;
    if (key.ScanCode == SCAN_RIGHT) *cursor_x += 15;

    if (*cursor_x < 0) *cursor_x = 0;
    if (*cursor_y < 0) *cursor_y = 0;
    if (*cursor_x >= (int)screen_w) *cursor_x = (int)screen_w - 1;
    if (*cursor_y >= (int)screen_h) *cursor_y = (int)screen_h - 1;

    /* Check button hovers */
    int hov_back_top = point_in_rect(*cursor_x, *cursor_y, top_back_x, top_back_y, top_back_w, top_back_h);
    int hov_exit_top = point_in_rect(*cursor_x, *cursor_y, top_exit_x, top_exit_y, top_exit_w, top_exit_h);
    int hov_back_main= point_in_rect(*cursor_x, *cursor_y, back_btn_x, back_btn_y, back_btn_w, back_btn_h);

    if (click_event) {
      if (hov_back_top || hov_back_main) return 0; /* Back to home */
      if (hov_exit_top) return 1;                  /* Exit */
    }

    /* Draw Background */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

    /* Header Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_topbar);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 120, 14, "DIAGNOSTIC SUITE [WIP]", 2, col_white, 0, 0);

    /* Top Back Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h,
                 hov_back_top ? col_btn_hov : col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_back_x, top_back_y, top_back_w, top_back_h, 2, col_border);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_back_x + 14, top_back_y + 8, "< Back", 1, col_white, 0, 0);

    /* Top Exit Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h,
                 hov_exit_top ? col_exit_hov : col_exit_norm);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, top_exit_x, top_exit_y, top_exit_w, top_exit_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, top_exit_x + 18, top_exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

    /* Center Card Box */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, col_card);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, 2, col_border);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, 4, col_amber);

    /* WIP Content */
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 30, "HARDWARE DIAGNOSTICS", 2, col_white, 0, 0);

    /* Large WIP Badge */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 70, 240, 32, col_amber);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 40, card_y + 78, "WORK IN PROGRESS (WIP)", 1, col_topbar, 0, 0);

    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 120,
                 "The comprehensive hardware diagnostic engine is under development.\n"
                 "Planned test suites include:\n"
                 "  * CPU Multi-core Stress & Instruction Validation\n"
                 "  * System RAM Integrity & Pattern Test\n"
                 "  * Storage Block I/O Performance & SMART Check\n"
                 "  * PCI Express & ACPI Device Discovery", 1, col_gray, 0, 0);

    /* Center Back Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h,
                 hov_back_main ? col_btn_hov : col_btn_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, back_btn_x, back_btn_y, back_btn_w, back_btn_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, back_btn_x + 28, back_btn_y + 13, "Back to Main Menu", 1, col_white, 0, 0);

    /* Draw Pointer Cursor with click pulse visual */
    fb_draw_cursor(draw_fb, stride, screen_w, screen_h, *cursor_x, *cursor_y, col_white, make_color_gop(gop, 0, 0, 0), curr_left_btn);

    /* Blit back buffer if available */
    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, (UINTN)screen_h * stride * sizeof(UINT32));
    }

    if (bs && bs->Stall) bs->Stall(16000);
  }
}

/* Render Graphical Home Menu */
static void run_graphical_home_menu(EFI_HANDLE ImageHandle, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
  EFI_BOOT_SERVICES *bs = ST->BootServices;
  UINT32 *fb = (UINT32*)gop->Mode->FrameBufferBase;
  UINT32 stride = gop->Mode->Info->PixelsPerScanLine;
  UINT32 screen_w = gop->Mode->Info->HorizontalResolution;
  UINT32 screen_h = gop->Mode->Info->VerticalResolution;

  UINT32 *back_buf = NULL;
  UINTN buf_size = (UINTN)screen_h * stride * sizeof(UINT32);

  if (bs && bs->AllocatePool) {
    bs->AllocatePool(EFI_LOADER_DATA, buf_size, (VOID**)&back_buf);
  }

  /* Comprehensive Pointer Protocols & PS/2 Hardware Driver Discovery */
  init_pointer_protocols(ImageHandle, bs);

  int cursor_x = (int)screen_w / 2;
  int cursor_y = (int)screen_h / 2;
  BOOLEAN prev_left_btn = FALSE;
  int selected_btn_idx = 0;

  /* Theme Palette */
  UINT32 col_bg        = make_color_gop(gop, 15, 23, 42);    /* #0F172A Dark Slate */
  UINT32 col_card_bg   = make_color_gop(gop, 30, 41, 59);    /* #1E293B Card Slate */
  UINT32 col_header_bg = make_color_gop(gop, 15, 23, 42);    /* Header bar */
  UINT32 col_card_brd  = make_color_gop(gop, 51, 65, 85);    /* #334155 Border */
  UINT32 col_accent_cyan= make_color_gop(gop, 56, 189, 248);  /* #38BDF8 Accent */
  UINT32 col_white     = make_color_gop(gop, 255, 255, 255);
  UINT32 col_gray      = make_color_gop(gop, 148, 163, 184); /* #94A3B8 Text */
  UINT32 col_black     = make_color_gop(gop, 0, 0, 0);

  /* Button Styling */
  UI_BUTTON buttons[3];
  int card_w = 540, card_h = 360;
  int card_x = ((int)screen_w - card_w) / 2;
  int card_y = ((int)screen_h - card_h) / 2 + 15;

  /* 1. Hardware Information Button */
  buttons[0].x = card_x + 30;
  buttons[0].y = card_y + 90;
  buttons[0].w = 480;
  buttons[0].h = 68;
  buttons[0].title = "1. Hardware Information";
  buttons[0].subtext = "View CPU, Memory Map, ACPI, SMBIOS & Storage";
  buttons[0].bg_normal = make_color_gop(gop, 15, 23, 42);
  buttons[0].bg_hover  = make_color_gop(gop, 30, 58, 138);  /* #1E3A8A Dark Blue */
  buttons[0].bg_active = make_color_gop(gop, 29, 78, 216);  /* #1D4ED8 Active Blue */
  buttons[0].border_normal = make_color_gop(gop, 71, 85, 105);
  buttons[0].border_hover  = make_color_gop(gop, 96, 165, 250);/* #60A5FA */
  buttons[0].fg_title = col_white;
  buttons[0].fg_subtext = col_gray;

  /* 2. Donut Easter Egg Button */
  buttons[1].x = card_x + 30;
  buttons[1].y = card_y + 175;
  buttons[1].w = 480;
  buttons[1].h = 68;
  buttons[1].title = "2. 3D Spinning Donut";
  buttons[1].subtext = "View 3D ASCII Donut & PC Speaker Riff";
  buttons[1].bg_normal = make_color_gop(gop, 15, 23, 42);
  buttons[1].bg_hover  = make_color_gop(gop, 88, 28, 135);  /* #581C87 Dark Purple */
  buttons[1].bg_active = make_color_gop(gop, 126, 34, 206); /* #7E22CE Active Purple */
  buttons[1].border_normal = make_color_gop(gop, 71, 85, 105);
  buttons[1].border_hover  = make_color_gop(gop, 192, 132, 252);/* #C084FC */
  buttons[1].fg_title = col_white;
  buttons[1].fg_subtext = col_gray;

  /* 3. Diagnostic Suite (WIP) Button */
  buttons[2].x = card_x + 30;
  buttons[2].y = card_y + 260;
  buttons[2].w = 480;
  buttons[2].h = 68;
  buttons[2].title = "3. Diagnostic Suite [WIP]";
  buttons[2].subtext = "Hardware Diagnostic & Stress Tests (Work In Progress)";
  buttons[2].bg_normal = make_color_gop(gop, 15, 23, 42);
  buttons[2].bg_hover  = make_color_gop(gop, 120, 53, 15);  /* #78350F Dark Amber */
  buttons[2].bg_active = make_color_gop(gop, 180, 83, 9);   /* #B45309 Active Amber */
  buttons[2].border_normal = make_color_gop(gop, 71, 85, 105);
  buttons[2].border_hover  = make_color_gop(gop, 251, 191, 36);/* #FBBF24 */
  buttons[2].fg_title = col_white;
  buttons[2].fg_subtext = col_gray;

  /* Top Right Exit Button */
  int exit_x = (int)screen_w - 110, exit_y = 10, exit_w = 95, exit_h = 32;
  UINT32 col_exit_norm = make_color_gop(gop, 153, 27, 27);  /* #991B1B */
  UINT32 col_exit_hov  = make_color_gop(gop, 220, 38, 38);  /* #DC2626 */

  for (;;) {
    UINT32 *draw_fb = back_buf ? back_buf : fb;
    BOOLEAN curr_left_btn = FALSE;

    /* Poll Mouse/Tablet Input across all handles */
    poll_pointer_inputs(screen_w, screen_h, &cursor_x, &cursor_y, &curr_left_btn);

    BOOLEAN click_event = (curr_left_btn && !prev_left_btn);
    prev_left_btn = curr_left_btn;

    /* Keyboard Input Handling with Correct Standard UEFI ScanCodes */
    EFI_INPUT_KEY key = read_key_nonblocking();
    if (key.ScanCode == SCAN_UP) {
      selected_btn_idx = (selected_btn_idx + 2) % 3;
      cursor_y -= 15;
    } else if (key.ScanCode == SCAN_DOWN) {
      selected_btn_idx = (selected_btn_idx + 1) % 3;
      cursor_y += 15;
    } else if (key.ScanCode == SCAN_LEFT) {
      cursor_x -= 15;
    } else if (key.ScanCode == SCAN_RIGHT) {
      cursor_x += 15;
    } else if (key.UnicodeChar == L'1' || key.UnicodeChar == L'h' || key.UnicodeChar == L'H') {
      gather_hardware_info(ImageHandle);
      run_pager(bs);
      continue;
    } else if (key.UnicodeChar == L'2' || key.UnicodeChar == L'd' || key.UnicodeChar == L'D') {
      easter_egg_donut(bs);
      continue;
    } else if (key.UnicodeChar == L'3' || key.UnicodeChar == L'w' || key.UnicodeChar == L'W') {
      int ret = render_diagnostic_wip_screen(ImageHandle, bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn);
      if (ret == 1) break;
      continue;
    } else if (key.UnicodeChar == L'x' || key.UnicodeChar == L'X' || key.ScanCode == SCAN_ESC || key.UnicodeChar == 27) {
      break; /* Exit application */
    } else if (key.UnicodeChar == L'\r' || key.UnicodeChar == L' ') {
      if (selected_btn_idx == 0) {
        gather_hardware_info(ImageHandle);
        run_pager(bs);
        continue;
      } else if (selected_btn_idx == 1) {
        easter_egg_donut(bs);
        continue;
      } else if (selected_btn_idx == 2) {
        int ret = render_diagnostic_wip_screen(ImageHandle, bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn);
        if (ret == 1) break;
        continue;
      }
    }

    /* Clamp cursor bounds */
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= (int)screen_w) cursor_x = (int)screen_w - 1;
    if (cursor_y >= (int)screen_h) cursor_y = (int)screen_h - 1;

    /* Hover & Click Detection */
    int hover_exit = point_in_rect(cursor_x, cursor_y, exit_x, exit_y, exit_w, exit_h);
    int i;
    for (i = 0; i < 3; i++) {
      if (point_in_rect(cursor_x, cursor_y, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h)) {
        selected_btn_idx = i;
      }
    }

    if (click_event) {
      if (hover_exit) break; /* Exit application */
      if (selected_btn_idx == 0 && point_in_rect(cursor_x, cursor_y, buttons[0].x, buttons[0].y, buttons[0].w, buttons[0].h)) {
        gather_hardware_info(ImageHandle);
        run_pager(bs);
        continue;
      } else if (selected_btn_idx == 1 && point_in_rect(cursor_x, cursor_y, buttons[1].x, buttons[1].y, buttons[1].w, buttons[1].h)) {
        easter_egg_donut(bs);
        continue;
      } else if (selected_btn_idx == 2 && point_in_rect(cursor_x, cursor_y, buttons[2].x, buttons[2].y, buttons[2].w, buttons[2].h)) {
        int ret = render_diagnostic_wip_screen(ImageHandle, bs, gop, fb, back_buf, stride, screen_w, screen_h, &cursor_x, &cursor_y, &prev_left_btn);
        if (ret == 1) break;
        continue;
      }
    }

    /* ---- Draw Menu Frame ----------------------------------------------- */

    /* Background Clear */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, screen_h, col_bg);

    /* Header Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 0, screen_w, 50, col_header_bg);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, 50, screen_w, 2, col_card_brd);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, 20, 14, "UEFI HARDWARE DIAGNOSTICS", 2, col_white, 0, 0);

    /* Top Right Exit Button */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, exit_x, exit_y, exit_w, exit_h, hover_exit ? col_exit_hov : col_exit_norm);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, exit_x, exit_y, exit_w, exit_h, 2, col_white);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, exit_x + 18, exit_y + 8, "[X] Exit", 1, col_white, 0, 0);

    /* Centered Menu Card Container */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, col_card_bg);
    fb_draw_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, card_h, 2, col_card_brd);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, card_x, card_y, card_w, 4, col_accent_cyan);

    /* Card Header Titles */
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 24, "CONTROL CENTER MENU", 2, col_accent_cyan, 0, 0);
    fb_draw_text(draw_fb, stride, screen_w, screen_h, card_x + 30, card_y + 60, "Click a formatted button or use Arrow Keys + Enter", 1, col_gray, 0, 0);

    /* Render Menu Buttons */
    for (i = 0; i < 3; i++) {
      int is_selected = (selected_btn_idx == i);
      UINT32 btn_bg  = is_selected ? (curr_left_btn ? buttons[i].bg_active : buttons[i].bg_hover) : buttons[i].bg_normal;
      UINT32 btn_brd = is_selected ? buttons[i].border_hover : buttons[i].border_normal;

      fb_fill_rect(draw_fb, stride, screen_w, screen_h, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, btn_bg);
      fb_draw_rect(draw_fb, stride, screen_w, screen_h, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, is_selected ? 2 : 1, btn_brd);

      /* Left indicator bar for selected button */
      if (is_selected) {
        fb_fill_rect(draw_fb, stride, screen_w, screen_h, buttons[i].x, buttons[i].y, 6, buttons[i].h, btn_brd);
      }

      fb_draw_text(draw_fb, stride, screen_w, screen_h, buttons[i].x + 20, buttons[i].y + 14, buttons[i].title, 1, buttons[i].fg_title, 0, 0);
      fb_draw_text(draw_fb, stride, screen_w, screen_h, buttons[i].x + 20, buttons[i].y + 38, buttons[i].subtext, 1, buttons[i].fg_subtext, 0, 0);
    }

    /* Bottom Status Bar */
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, screen_h - 26, screen_w, 26, col_header_bg);
    fb_fill_rect(draw_fb, stride, screen_w, screen_h, 0, screen_h - 27, screen_w, 1, col_card_brd);

    char status_str[160];
    uprintf_str(status_str, sizeof(status_str), "Pointers: (Simple:%u Abs:%u) | Pkts:%u | dX:%d dY:%d | Use Arrow Keys or Mouse",
                (UINT64)g_num_simple_pointers, (UINT64)g_num_abs_pointers, g_mouse_packets_count, (INT64)g_last_dx, (INT64)g_last_dy);

    fb_draw_text(draw_fb, stride, screen_w, screen_h, 20, screen_h - 20, status_str, 1, col_gray, 0, 0);

    /* Draw Mouse Cursor Pointer with click pulse visual */
    fb_draw_cursor(draw_fb, stride, screen_w, screen_h, cursor_x, cursor_y, col_white, col_black, curr_left_btn);

    /* Frame buffer copy */
    if (back_buf) {
      memcpy((void*)fb, (const void*)back_buf, buf_size);
    }

    if (bs && bs->Stall) bs->Stall(16000); /* ~60 FPS */
  }

  if (back_buf && bs && bs->FreePool) {
    bs->FreePool(back_buf);
  }
}

/* ========================================================================= */
/* Entry Point                                                               */
/* ========================================================================= */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  EFI_BOOT_SERVICES *bs;

  ST = SystemTable;
  bs = ST->BootServices;

  if (bs && bs->SetWatchdogTimer) bs->SetWatchdogTimer(0, 0, 0, NULL);

  if (ST->ConOut) {
    set_best_text_mode();
    ST->ConOut->SetAttribute(ST->ConOut, 0x0F);
    ST->ConOut->ClearScreen(ST->ConOut);
  }

  /* Query Graphics Output Protocol */
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
  if (bs && bs->LocateProtocol) {
    bs->LocateProtocol((EFI_GUID*)&gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gop);
  }

  if (gop && gop->Mode && gop->Mode->FrameBufferBase && gop->Mode->Info) {
    /* Launch Graphical Control Center Home Menu */
    run_graphical_home_menu(ImageHandle, gop);
  } else {
    /* Fallback for pure text mode systems without GOP */
    gather_hardware_info(ImageHandle);
    run_pager(bs);
  }

  if (ST->ConOut) {
    ST->ConOut->ClearScreen(ST->ConOut);
  }

  return EFI_SUCCESS;
}
