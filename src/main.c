#include "efi_types.h"
#include "core.h"
#include "gfx.h"
#include "cpu_hwinfo.h"
#include "donut.h"
#include "ui.h"

/* ---- CRT symbol required for floating-point ----------------------------- */
int _fltused = 1;

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
/* Entry Point                                                               */
/* ========================================================================= */
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  EFI_BOOT_SERVICES *bs;

  ST = SystemTable;
  bs = ST->BootServices;

  serial_init();
  serial_write_str("hwdiag: EfiMain entered\n");

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
    /* Fallback for pure text mode systems without GOP — pass NULLs for graphical params */
    gather_hardware_info(ImageHandle);
    {
      int dummy_cx = 0, dummy_cy = 0;
      BOOLEAN dummy_btn = FALSE;
      run_pager(bs, NULL, NULL, NULL, 0, 0, 0, &dummy_cx, &dummy_cy, &dummy_btn);
    }
  }

  if (ST->ConOut) {
    ST->ConOut->ClearScreen(ST->ConOut);
  }

  return EFI_SUCCESS;
}
