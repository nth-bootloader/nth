#include <efi.h>
#include <efilib.h>
#include "file.h"
#include "elf.h"
#include "gop.h"
#include "memory.h"
#include "nth_protocol.h"
#include "menu.h"
#include "config.h"
#include "paging.h"

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    Framebuffer fb;
    InitializeGOP(&fb);

    BootEntry entries[10];
    UINTN entry_count = 0;
    INTN Timeout = 10;

    VOID *ConfigBuffer = NULL;
    UINTN ConfigSize = 0;

    if (!EFI_ERROR(LoadFile(ImageHandle, L"\\nth.cfg", &ConfigBuffer, &ConfigSize)))
    {
        entry_count = ParseConfig(ConfigBuffer, ConfigSize, entries, 9, &Timeout);
        uefi_call_wrapper(BS->FreePool, 1, ConfigBuffer);
    }
    else
    {
        entries[0].Name = L"NTH OS (Default)";
        entries[0].KernelPath = L"\\kernel.elf";
        entries[0].Options = NULL;
        entry_count = 1;
    }

    if (entry_count == 0)
    {
        entries[0].Name = L"NTH OS (Default)";
        entries[0].KernelPath = L"\\kernel.elf";
        entries[0].Options = NULL;
        entry_count = 1;
    }

    entries[entry_count].Name = L"Reboot System";
    entries[entry_count].KernelPath = NULL;
    entries[entry_count].Options = NULL;
    entry_count++;

    BootEntry *SelectedEntry = ShowGraphicalMenu(SystemTable, &fb, entries, entry_count, Timeout);

    uefi_call_wrapper(SystemTable->ConOut->ClearScreen, 1, SystemTable->ConOut);
    Print(L"Attempting to boot: %s\n", SelectedEntry->Name);

    if (SelectedEntry == NULL || SelectedEntry->KernelPath == NULL)
    {
        uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
    }

    VOID *KernelBuffer = NULL;
    UINTN KernelSize = 0;
    EFI_STATUS Status = LoadFile(ImageHandle, SelectedEntry->KernelPath, &KernelBuffer, &KernelSize);
    if (EFI_ERROR(Status))
    {
        Print(L"Failed to load kernel: %s\n", SelectedEntry->KernelPath);
        uefi_call_wrapper(BS->Stall, 1, 3000000);
        return Status;
    }

    UINT8 *magic = (UINT8 *)KernelBuffer;
    if (KernelSize >= 4 && magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
    {
        EFI_MEMORY_DESCRIPTOR *PagingMemoryMap = NULL;
        UINTN PagingMapSize, PagingMapKey, PagingDescriptorSize;
        ReadMemoryMap(&PagingMemoryMap, &PagingMapSize, &PagingMapKey, &PagingDescriptorSize);

        UINT64 *PML4 = SetupPaging(PagingMemoryMap, PagingMapSize, PagingDescriptorSize);

        uefi_call_wrapper(BS->FreePool, 1, PagingMemoryMap);

        UINT64 EntryPoint = LoadELF(KernelBuffer, PML4);
        typedef void (*KernelStart)(NthBootInfo *);
        KernelStart kernel_main = (KernelStart)EntryPoint;

        NthFramebuffer boot_fb;
        boot_fb.BaseAddress = fb.BaseAddress;
        boot_fb.BufferSize = fb.BufferSize;
        boot_fb.Width = fb.Width;
        boot_fb.Height = fb.Height;
        boot_fb.PixelsPerScanLine = fb.PixelsPerScanLine;

        EFI_MEMORY_DESCRIPTOR *MemoryMap = NULL;
        UINTN MapSize, MapKey, DescriptorSize;
        ReadMemoryMap(&MemoryMap, &MapSize, &MapKey, &DescriptorSize);

        void *rsdp = NULL;
        EFI_GUID Acpi2TableGuid = ACPI_20_TABLE_GUID;

        for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++)
        {
            if (CompareGuid(&SystemTable->ConfigurationTable[i].VendorGuid, &Acpi2TableGuid) == 0)
            {
                rsdp = SystemTable->ConfigurationTable[i].VendorTable;
                break;
            }
        }

        Status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, MapKey);
        if (EFI_ERROR(Status))
        {
            ReadMemoryMap(&MemoryMap, &MapSize, &MapKey, &DescriptorSize);
            uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, MapKey);
        }

        NthBootInfo boot_info;
        boot_info.Framebuffer = &boot_fb;
        boot_info.MemoryMap = (void *)MemoryMap;
        boot_info.MapSize = MapSize;
        boot_info.DescriptorSize = DescriptorSize;
        boot_info.Rsdp = rsdp;

        SwitchPageTable(PML4);

        kernel_main(&boot_info);

        while (1)
        {
            __asm__("hlt");
        }
    }
    else if (KernelSize >= 2 && magic[0] == 'M' && magic[1] == 'Z')
    {
        Print(L"Detected PE/COFF kernel (Linux EFI stub). Preparing to load...\n");
        uefi_call_wrapper(BS->Stall, 1, 1000000);

        EFI_HANDLE NewImageHandle;

        EFI_LOADED_IMAGE *ParentLoadedImage = NULL;
        EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
        uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &lipGuid, (VOID **)&ParentLoadedImage);

        Print(L"Creating DevicePath...\n");
        uefi_call_wrapper(BS->Stall, 1, 1000000);

        EFI_DEVICE_PATH *KernelDevicePath = FileDevicePath(ParentLoadedImage->DeviceHandle, SelectedEntry->KernelPath);

        Print(L"Calling BS->LoadImage from DevicePath...\n");
        uefi_call_wrapper(BS->Stall, 1, 1000000);

        Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, KernelDevicePath, NULL, 0, &NewImageHandle);
        if (EFI_ERROR(Status))
        {
            Print(L"Failed to LoadImage: %r\n", Status);
            uefi_call_wrapper(BS->Stall, 1, 3000000);
            return Status;
        }

        Print(L"LoadImage successful. Setting options...\n");
        uefi_call_wrapper(BS->Stall, 1, 1000000);

        if (SelectedEntry->Options != NULL)
        {
            EFI_LOADED_IMAGE *NewLoadedImage = NULL;
            Status = uefi_call_wrapper(BS->HandleProtocol, 3, NewImageHandle, &lipGuid, (VOID **)&NewLoadedImage);
            if (!EFI_ERROR(Status))
            {
                NewLoadedImage->LoadOptions = SelectedEntry->Options;
                NewLoadedImage->LoadOptionsSize = (StrLen(SelectedEntry->Options) + 1) * sizeof(CHAR16);
            }
        }

        Print(L"Calling StartImage...\n");
        uefi_call_wrapper(BS->Stall, 1, 1000000);

        uefi_call_wrapper(SystemTable->ConOut->ClearScreen, 1, SystemTable->ConOut);

        Status = uefi_call_wrapper(BS->StartImage, 3, NewImageHandle, NULL, NULL);
        if (EFI_ERROR(Status))
        {
            Print(L"Failed to StartImage: %r\n", Status);
            uefi_call_wrapper(BS->Stall, 1, 3000000);
            return Status;
        }
    }
    else
    {
        Print(L"Unknown kernel format!\n");
        uefi_call_wrapper(BS->Stall, 1, 3000000);
    }

    return EFI_SUCCESS;
}
