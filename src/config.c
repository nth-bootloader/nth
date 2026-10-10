#include "config.h"

VOID AsciiToUnicode(CHAR8 *AsciiStr, CHAR16 *UnicodeStr, UINTN MaxLen)
{
    UINTN i = 0;
    while (AsciiStr[i] != '\0' && AsciiStr[i] != '\r' && AsciiStr[i] != '\n' && i < MaxLen - 1)
    {
        UnicodeStr[i] = (CHAR16)AsciiStr[i];
        i++;
    }
    UnicodeStr[i] = L'\0';
}

static BOOLEAN AsciiStrEqualsCaseInsensitive(const CHAR8 *s1, const CHAR8 *s2)
{
    while (*s1 && *s2)
    {
        CHAR8 c1 = *s1;
        CHAR8 c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z')
            c1 = (CHAR8)(c1 + ('a' - 'A'));
        if (c2 >= 'A' && c2 <= 'Z')
            c2 = (CHAR8)(c2 + ('a' - 'A'));
        if (c1 != c2)
            return FALSE;
        s1++;
        s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

static INTN AsciiToInt(const CHAR8 *str)
{
    INTN val = 0;
    INTN sign = 1;
    while (*str == ' ' || *str == '\t')
        str++;
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }
    while (*str >= '0' && *str <= '9')
    {
        val = val * 10 + (*str - '0');
        str++;
    }
    return val * sign;
}

UINTN ParseConfig(VOID *FileBuffer, UINTN FileSize, BootEntry *Entries, UINTN MaxEntries, INTN *Timeout)
{
    CHAR8 *Buffer = (CHAR8 *)FileBuffer;
    UINTN EntryCount = 0;
    UINTN i = 0;

    while (i < FileSize)
    {
        // Skip leading whitespace, newlines, and carriage returns
        while (i < FileSize && (Buffer[i] == '\r' || Buffer[i] == '\n' || Buffer[i] == ' ' || Buffer[i] == '\t'))
            i++;
        if (i >= FileSize)
            break;

        // Skip comment lines starting with '#' or ';'
        if (Buffer[i] == '#' || Buffer[i] == ';')
        {
            while (i < FileSize && Buffer[i] != '\r' && Buffer[i] != '\n')
                i++;
            continue;
        }

        CHAR8 NameBuf[64] = {0};
        UINTN n = 0;
        while (i < FileSize && Buffer[i] != '=' && Buffer[i] != '\n' && Buffer[i] != '\r' && n < 63)
        {
            NameBuf[n++] = Buffer[i++];
        }
        NameBuf[n] = '\0';

        // Trim trailing whitespace from NameBuf
        while (n > 0 && (NameBuf[n - 1] == ' ' || NameBuf[n - 1] == '\t'))
        {
            NameBuf[--n] = '\0';
        }

        if (i < FileSize && Buffer[i] == '=')
            i++;

        // Skip leading whitespace after '='
        while (i < FileSize && (Buffer[i] == ' ' || Buffer[i] == '\t'))
            i++;

        CHAR8 PathBuf[64] = {0};
        UINTN p = 0;
        while (i < FileSize && Buffer[i] != '\r' && Buffer[i] != '\n' && Buffer[i] != '|' && p < 63)
        {
            PathBuf[p++] = Buffer[i++];
        }
        PathBuf[p] = '\0';

        CHAR8 OptionsBuf[256] = {0};
        UINTN o = 0;
        if (i < FileSize && Buffer[i] == '|')
        {
            i++;
            while (i < FileSize && (Buffer[i] == ' ' || Buffer[i] == '\t'))
                i++;

            while (i < FileSize && Buffer[i] != '\r' && Buffer[i] != '\n' && o < 255)
            {
                OptionsBuf[o++] = Buffer[i++];
            }
        }
        OptionsBuf[o] = '\0';

        // Advance to end of current line
        while (i < FileSize && Buffer[i] != '\r' && Buffer[i] != '\n')
            i++;

        // Trim trailing whitespace from PathBuf
        while (p > 0 && (PathBuf[p - 1] == ' ' || PathBuf[p - 1] == '\t' || PathBuf[p - 1] == '\r' || PathBuf[p - 1] == '\n'))
        {
            PathBuf[--p] = '\0';
        }

        // Trim trailing whitespace from OptionsBuf
        while (o > 0 && (OptionsBuf[o - 1] == ' ' || OptionsBuf[o - 1] == '\t' || OptionsBuf[o - 1] == '\r' || OptionsBuf[o - 1] == '\n'))
        {
            OptionsBuf[--o] = '\0';
        }

        // Check if this directive sets the timeout
        if (AsciiStrEqualsCaseInsensitive(NameBuf, "timeout"))
        {
            if (Timeout != NULL)
            {
                if (AsciiStrEqualsCaseInsensitive(PathBuf, "off") || AsciiStrEqualsCaseInsensitive(PathBuf, "none"))
                {
                    *Timeout = -1;
                }
                else
                {
                    *Timeout = AsciiToInt(PathBuf);
                }
            }
            continue;
        }

        // Otherwise add as a boot entry if valid
        if (n > 0 && p > 0 && EntryCount < MaxEntries)
        {
            uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, 128, (VOID **)&Entries[EntryCount].Name);
            uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, 128, (VOID **)&Entries[EntryCount].KernelPath);

            if (o > 0)
            {
                uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, 512, (VOID **)&Entries[EntryCount].Options);
                AsciiToUnicode(OptionsBuf, Entries[EntryCount].Options, 256);
            }
            else
            {
                Entries[EntryCount].Options = NULL;
            }

            AsciiToUnicode(NameBuf, Entries[EntryCount].Name, 64);
            AsciiToUnicode(PathBuf, Entries[EntryCount].KernelPath, 64);

            EntryCount++;
        }
    }

    return EntryCount;
}
