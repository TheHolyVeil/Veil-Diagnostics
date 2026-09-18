#ifndef EFI_TYPES_H
#define EFI_TYPES_H

/*
 * hwdiag.c — UEFI Hardware Diagnostics & Graphical Control Center (x86-64)
 *
 * Freestanding: no libc, no EDK2 headers. Fully self-contained UEFI application
 * with GOP graphical home menu, interactive cursor/pointer, formatted buttons,
 * text diagnostic info pager, 3D spinning donut, and WIP diagnostic suite.
 */


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
  0x31878C87,0x0B75,0x11D5, 0x9A,0x4F,0x00,0x90,0x27,0x3F,0xC1,0x4D);
static const EFI_GUID gEfiAbsolutePointerProtocolGuid = GUID(
  0x8D59D32B,0xC655,0x4AE9, 0x9B,0x15,0xF2,0x59,0x04,0x99,0x2A,0x43);

static inline int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
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


#endif /* EFI_TYPES_H */
