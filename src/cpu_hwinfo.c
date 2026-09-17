#include "efi_types.h"
#include "core.h"
#include "gfx.h"
#include "cpu_hwinfo.h"

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


/* ========================================================================= */
/* Hardware Information Gathering                                            */
/* ========================================================================= */
void gather_hardware_info(EFI_HANDLE ImageHandle) {
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

