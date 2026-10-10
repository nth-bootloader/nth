#include "menu.h"
#include "font.h"

static void DrawPixel(Framebuffer *fb, UINT32 x, UINT32 y, UINT32 color)
{
    if (x >= fb->Width || y >= fb->Height)
        return;
    UINT32 *video_memory = (UINT32 *)fb->BaseAddress;
    video_memory[y * fb->PixelsPerScanLine + x] = color;
}

static void DrawChar(Framebuffer *fb, char c, UINT32 x, UINT32 y, UINT32 scale, UINT32 color)
{
    if (c < 32 || c > 127)
        return;
    UINT8 *glyph = (UINT8 *)font8x8[(int)c - 32];

    for (UINT32 cy = 0; cy < 8; cy++)
    {
        for (UINT32 cx = 0; cx < 8; cx++)
        {
            if ((glyph[cy] & (0x80 >> cx)))
            {
                for (UINT32 sy = 0; sy < scale; sy++)
                {
                    for (UINT32 sx = 0; sx < scale; sx++)
                    {
                        DrawPixel(fb, x + (cx * scale) + sx, y + (cy * scale) + sy, color);
                    }
                }
            }
        }
    }
}

static void DrawString(Framebuffer *fb, const CHAR16 *str, UINT32 x, UINT32 y, UINT32 scale, UINT32 color)
{
    while (*str)
    {
        char ascii = (*str < 128) ? (char)*str : '?';
        DrawChar(fb, ascii, x, y, scale, color);
        x += 8 * scale;
        str++;
    }
}

static void ClearRect(Framebuffer *fb, UINT32 x, UINT32 y, UINT32 width, UINT32 height, UINT32 color)
{
    if (x >= fb->Width || y >= fb->Height)
        return;

    UINT32 max_x = (x + width > fb->Width) ? fb->Width : x + width;
    UINT32 max_y = (y + height > fb->Height) ? fb->Height : y + height;
    UINT32 *video_memory = (UINT32 *)fb->BaseAddress;

    for (UINT32 cy = y; cy < max_y; cy++)
    {
        for (UINT32 cx = x; cx < max_x; cx++)
        {
            video_memory[cy * fb->PixelsPerScanLine + cx] = color;
        }
    }
}

static void IntToStr(INTN num, CHAR16 *str)
{
    CHAR16 temp[32];
    INTN i = 0;
    if (num < 0)
    {
        *str++ = L'-';
        num = -num;
    }
    if (num == 0)
    {
        *str++ = L'0';
        *str = L'\0';
        return;
    }
    while (num > 0)
    {
        temp[i++] = (CHAR16)(L'0' + (num % 10));
        num /= 10;
    }
    while (i > 0)
    {
        *str++ = temp[--i];
    }
    *str = L'\0';
}

static void DrawTimer(Framebuffer *fb, INTN RemainingSeconds, BOOLEAN TimerActive)
{
    UINT32 timer_y = (fb->Height > 80) ? (fb->Height - 80) : 0;
    ClearRect(fb, 80, timer_y, (fb->Width > 80) ? (fb->Width - 80) : 0, 20, 0x00000000);

    if (TimerActive && RemainingSeconds >= 0)
    {
        CHAR16 buffer[128];
        CHAR16 numBuf[32];
        IntToStr(RemainingSeconds, numBuf);

        StrCpy(buffer, L"The highlighted entry will be executed automatically in ");
        StrCat(buffer, numBuf);
        StrCat(buffer, L"s.");

        DrawString(fb, buffer, 80, timer_y, 1, 0x00AAAAAA);
    }
}

BootEntry *ShowGraphicalMenu(EFI_SYSTEM_TABLE *SystemTable, Framebuffer *fb, BootEntry *Entries, UINTN EntryCount, INTN Timeout)
{
    if (EntryCount == 0)
        return NULL;

    if (Timeout == 0 && Entries[0].KernelPath != NULL)
    {
        return &Entries[0];
    }

    UINTN SelectedIndex = 0;
    INTN RemainingSeconds = (Timeout > 0) ? Timeout : -1;
    BOOLEAN TimerActive = (Timeout > 0 && Entries[SelectedIndex].KernelPath != NULL);
    UINTN ElapsedMicros = 0;
    BOOLEAN NeedFullRedraw = TRUE;

    EFI_STATUS Status;
    EFI_INPUT_KEY Key;

    uefi_call_wrapper(SystemTable->ConIn->Reset, 2, SystemTable->ConIn, FALSE);

    while (1)
    {
        if (NeedFullRedraw)
        {
            ClearRect(fb, 0, 0, fb->Width, fb->Height, 0x00000000);

            DrawString(fb, L"NTH BOOT MANAGER", 80, 60, 2, 0x00FFFFFF);

            UINT32 start_y = 130;
            for (UINTN i = 0; i < EntryCount; i++)
            {
                UINT32 color = (i == SelectedIndex) ? 0x0000FF00 : 0x00FFFFFF;
                CHAR16 buffer[128];

                if (i == SelectedIndex)
                {
                    StrCpy(buffer, L"> ");
                    StrCat(buffer, Entries[i].Name);
                }
                else
                {
                    StrCpy(buffer, L"  ");
                    StrCat(buffer, Entries[i].Name);
                }

                DrawString(fb, buffer, 80, start_y + (i * 24), 1, color);
            }

            if (fb->Height > 50)
            {
                DrawString(fb, L"Use Up/Down arrows to navigate, Enter to boot.", 80, fb->Height - 50, 1, 0x00777777);
            }

            DrawTimer(fb, RemainingSeconds, TimerActive);

            NeedFullRedraw = FALSE;
        }

        BOOLEAN KeyPressed = FALSE;

        while (1)
        {
            Status = uefi_call_wrapper(SystemTable->ConIn->ReadKeyStroke, 2, SystemTable->ConIn, &Key);
            if (!EFI_ERROR(Status))
            {
                KeyPressed = TRUE;
                break;
            }

            uefi_call_wrapper(BS->Stall, 1, 20000);

            if (TimerActive)
            {
                ElapsedMicros += 20000;
                if (ElapsedMicros >= 1000000)
                {
                    ElapsedMicros -= 1000000;
                    RemainingSeconds--;

                    if (RemainingSeconds <= 0)
                    {
                        if (Entries[SelectedIndex].KernelPath != NULL)
                        {
                            return &Entries[SelectedIndex];
                        }
                        else
                        {
                            TimerActive = FALSE;
                            DrawTimer(fb, RemainingSeconds, TimerActive);
                        }
                    }
                    else
                    {
                        DrawTimer(fb, RemainingSeconds, TimerActive);
                    }
                }
            }
        }

        if (KeyPressed)
        {
            if (Key.ScanCode == SCAN_UP)
            {
                if (SelectedIndex > 0)
                {
                    SelectedIndex--;
                    NeedFullRedraw = TRUE;
                }

                if (Entries[SelectedIndex].KernelPath == NULL)
                {
                    TimerActive = FALSE;
                }
                else if (Timeout > 0)
                {
                    TimerActive = TRUE;
                    RemainingSeconds = Timeout;
                    ElapsedMicros = 0;
                }

                if (!NeedFullRedraw)
                {
                    DrawTimer(fb, RemainingSeconds, TimerActive);
                }
            }
            else if (Key.ScanCode == SCAN_DOWN)
            {
                if (SelectedIndex < EntryCount - 1)
                {
                    SelectedIndex++;
                    NeedFullRedraw = TRUE;
                }

                if (Entries[SelectedIndex].KernelPath == NULL)
                {
                    TimerActive = FALSE;
                }
                else if (Timeout > 0)
                {
                    TimerActive = TRUE;
                    RemainingSeconds = Timeout;
                    ElapsedMicros = 0;
                }

                if (!NeedFullRedraw)
                {
                    DrawTimer(fb, RemainingSeconds, TimerActive);
                }
            }
            else if (Key.UnicodeChar == L'\r' || Key.UnicodeChar == L'\n' || Key.UnicodeChar == L' ' || Key.ScanCode == 0x0B)
            {
                return &Entries[SelectedIndex];
            }
            else if (Key.ScanCode == SCAN_ESC)
            {
                TimerActive = FALSE;
                DrawTimer(fb, RemainingSeconds, TimerActive);
            }
        }
    }
}
