#ifndef DONUT_H
#define DONUT_H
#include "efi_types.h"

int easter_egg_donut(EFI_BOOT_SERVICES *bs,
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 *fb, UINT32 *back_buf,
  UINT32 stride, UINT32 screen_w, UINT32 screen_h,
  int *cursor_x, int *cursor_y, BOOLEAN *prev_left_btn);

#endif /* DONUT_H */
