#ifndef UI_H
#define UI_H
#include "efi_types.h"

int run_pager(EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 *fb, UINT32 *back_buf,
  UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn);
void run_graphical_home_menu(EFI_HANDLE ImageHandle, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

#endif /* UI_H */
