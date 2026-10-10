#ifndef CONFIG_H
#define CONFIG_H

#include <efi.h>
#include <efilib.h>
#include "menu.h"

UINTN ParseConfig(VOID *FileBuffer, UINTN FileSize, BootEntry *Entries, UINTN MaxEntries, INTN *Timeout);

#endif
