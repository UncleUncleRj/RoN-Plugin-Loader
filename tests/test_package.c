/*
 * The shipped file (user decision 2026-11-24): one shfolder.dll that holds the loader itself and
 * exports only the system shfolder.dll's two functions, forwarded to shell32. These tests read the
 * built build\shfolder.dll beside tests.exe.
 */
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "ron_plugin.h"

static void build_path(wchar_t *out, const wchar_t *name)
{
    GetModuleFileNameW(NULL, out, MAX_PATH);
    wcscpy(wcsrchr(out, L'\\') + 1, name);
}

static uint8_t *read_built(const wchar_t *name, DWORD *size)
{
    wchar_t path[MAX_PATH];
    build_path(path, name);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    *size = GetFileSize(h, NULL);
    uint8_t *buf = malloc(*size ? *size : 1);
    DWORD got = 0;
    if (!buf || !ReadFile(h, buf, *size, &got, NULL) || got != *size) {
        free(buf);
        buf = NULL;
    }
    CloseHandle(h);
    return buf;
}

static const IMAGE_NT_HEADERS32 *nt_of(const uint8_t *f, DWORD size)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)f;
    if (size < sizeof *dos || dos->e_magic != IMAGE_DOS_SIGNATURE || (DWORD)dos->e_lfanew + 0x200 > size)
        return NULL;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(f + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : NULL;
}

/* The file bytes at an RVA, or NULL when no section holds it. */
static const uint8_t *at_rva(const uint8_t *f, DWORD size, DWORD rva)
{
    const IMAGE_NT_HEADERS32 *nt = nt_of(f, size);
    if (!nt)
        return NULL;
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        DWORD len = s->SizeOfRawData;
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + len) {
            DWORD off = s->PointerToRawData + (rva - s->VirtualAddress);
            return off < size ? f + off : NULL;
        }
    }
    return NULL;
}

static int holds(const uint8_t *f, DWORD size, const void *needle, size_t len)
{
    for (DWORD i = 0; i + len <= size; i++)
        if (memcmp(f + i, needle, len) == 0)
            return 1;
    return 0;
}

TEST(pkg_one_file)
{
    wchar_t p[MAX_PATH];
    build_path(p, L"shfolder.dll");
    CHECK(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
    build_path(p, L"RonPluginLoader.dll");
    CHECK(GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES);
}

TEST(pkg_holds_loader)
{
    DWORD size = 0;
    uint8_t *f = read_built(L"shfolder.dll", &size);
    CHECK(f != NULL);
    if (!f)
        return;
    static const char header[] = "# Load order: top loads first";
    CHECK(holds(f, size, header, sizeof header - 1));               /* the loader's own code is inside */
    static const wchar_t second[] = L"RonPluginLoader.dll";
    CHECK(!holds(f, size, second, sizeof second - sizeof(wchar_t))); /* and no second DLL is looked for */
    free(f);
}

TEST(pkg_exports_two_forwarders)
{
    DWORD size = 0;
    uint8_t *f = read_built(L"shfolder.dll", &size);
    CHECK(f != NULL);
    if (!f)
        return;
    const IMAGE_NT_HEADERS32 *nt = nt_of(f, size);
    CHECK(nt != NULL);
    if (!nt) {
        free(f);
        return;
    }
    const IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const IMAGE_EXPORT_DIRECTORY *ex = (const IMAGE_EXPORT_DIRECTORY *)at_rva(f, size, dd->VirtualAddress);
    CHECK(ex != NULL);
    if (!ex) {
        free(f);
        return;
    }
    CHECK_EQ(ex->NumberOfFunctions, 2);
    CHECK_EQ(ex->NumberOfNames, 2);
    static const char *const want[2][2] = {
        {"SHGetFolderPathA", "shell32.SHGetFolderPathA"},
        {"SHGetFolderPathW", "shell32.SHGetFolderPathW"},
    };
    const DWORD *names = (const DWORD *)at_rva(f, size, ex->AddressOfNames);
    const WORD *ords = (const WORD *)at_rva(f, size, ex->AddressOfNameOrdinals);
    const DWORD *funcs = (const DWORD *)at_rva(f, size, ex->AddressOfFunctions);
    CHECK(names && ords && funcs);
    for (DWORD i = 0; names && ords && funcs && i < ex->NumberOfNames && i < 2; i++) {
        CHECK_STR((const char *)at_rva(f, size, names[i]), want[i][0]);
        DWORD fn = ords[i] < ex->NumberOfFunctions ? funcs[ords[i]] : 0;
        /* a forwarder's RVA points into the export directory, at its "dll.function" text */
        CHECK(fn >= dd->VirtualAddress && fn < dd->VirtualAddress + dd->Size);
        const char *to = (const char *)at_rva(f, size, fn);
        CHECK(to && _stricmp(to, want[i][1]) == 0);
    }
    free(f);
}

/* The mark an installer reads (design section 11): the version resource names the loader, and its
   version is the API's major.minor. */
TEST(pkg_version_resource)
{
    wchar_t path[MAX_PATH];
    build_path(path, L"shfolder.dll");
    DWORD dummy = 0, n = GetFileVersionInfoSizeW(path, &dummy);
    CHECK(n > 0);
    if (!n)
        return;
    void *buf = malloc(n);
    CHECK(buf && GetFileVersionInfoW(path, 0, n, buf));
    wchar_t *name = NULL;
    UINT len = 0;
    CHECK(VerQueryValueW(buf, L"\\StringFileInfo\\040904B0\\ProductName", (void **)&name, &len));
    CHECK(name && wcscmp(name, L"RoN Plugin Loader") == 0);
    VS_FIXEDFILEINFO *fi = NULL;
    CHECK(VerQueryValueW(buf, L"\\", (void **)&fi, &len) && fi);
    if (fi)
        CHECK_EQ(fi->dwFileVersionMS, ((DWORD)RON_API_MAJOR << 16) | RON_API_MINOR);
    free(buf);
}
