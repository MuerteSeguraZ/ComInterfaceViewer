#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <strsafe.h>
#include <shlwapi.h>

#include "RegistryMode.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

// smol helpers
static HTREEITEM AddItem(HWND hTree, HTREEITEM hParent, LPCWSTR text)
{
    TVINSERTSTRUCT tvis = {0};
    tvis.hParent      = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask    = TVIF_TEXT;
    tvis.item.pszText = const_cast<LPWSTR>(text);
    return TreeView_InsertItem(hTree, &tvis);
}

static void ExpandAllLocal(HWND hTree, HTREEITEM hItem)
{
    if (!hItem) return;
    TreeView_Expand(hTree, hItem, TVE_EXPAND);
    HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
    while (hChild)
    {
        ExpandAllLocal(hTree, hChild);
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

static BOOL GetNormalizedPath(LPCWSTR path, WCHAR* out, size_t cchOut)
{
    WCHAR full[MAX_PATH] = {0};
    if (!GetFullPathNameW(path, MAX_PATH, full, NULL))
        return FALSE;

    // simple lowercase
    for (WCHAR* p = full; *p; ++p)
        *p = (WCHAR)towlower(*p);

    return SUCCEEDED(StringCchCopyW(out, cchOut, full));
}

BOOL PopulateFromRegistry(HWND hTree, LPCWSTR dllPath)
{
    if (!hTree || !dllPath || !*dllPath)
        return FALSE;

    TreeView_DeleteAllItems(hTree);

    WCHAR normDll[MAX_PATH] = {0};
    if (!GetNormalizedPath(dllPath, normDll, MAX_PATH))
        return FALSE;

    // just the filename for the root node
    LPCWSTR fileName = dllPath;
    for (LPCWSTR p = dllPath; *p; ++p)
        if (*p == L'\\' || *p == L'/')
            fileName = p + 1;

    HTREEITEM hRoot = AddItem(hTree, TVI_ROOT, fileName);
    HTREEITEM hClasses = AddItem(hTree, hRoot, L"Registered Classes");

    HKEY hClsid = NULL;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &hClsid) != ERROR_SUCCESS)
        return FALSE;

    DWORD index = 0;
    WCHAR clsidName[128];
    DWORD cchName;
    BOOL foundAny = FALSE;

    while (TRUE)
    {
        cchName = 128;
        LONG rc = RegEnumKeyExW(hClsid, index++, clsidName, &cchName, NULL, NULL, NULL, NULL);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        if (rc != ERROR_SUCCESS)
            continue;

        // open CLSID\{guid}
        HKEY hThisClsid = NULL;
        if (RegOpenKeyExW(hClsid, clsidName, 0, KEY_READ, &hThisClsid) != ERROR_SUCCESS)
            continue;

        // open InprocServer32
        HKEY hInproc = NULL;
        if (RegOpenKeyExW(hThisClsid, L"InprocServer32", 0, KEY_READ, &hInproc) == ERROR_SUCCESS)
        {
            WCHAR serverPath[MAX_PATH] = {0};
            DWORD cb = sizeof(serverPath);
            DWORD type = 0;

            if (RegQueryValueExW(hInproc, NULL, NULL, &type, (LPBYTE)serverPath, &cb) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ))
            {
                // expand env vars if needed
                WCHAR expanded[MAX_PATH] = {0};
                if (type == REG_EXPAND_SZ)
                {
                    ExpandEnvironmentStringsW(serverPath, expanded, MAX_PATH);
                    StringCchCopyW(serverPath, MAX_PATH, expanded);
                }

                WCHAR normServer[MAX_PATH] = {0};
                if (GetNormalizedPath(serverPath, normServer, MAX_PATH))
                {
                    if (wcscmp(normServer, normDll) == 0)
                    {
                        // match! get a friendly name
                        WCHAR friendly[256] = {0};
                        cb = sizeof(friendly);
                        if (RegQueryValueExW(hThisClsid, NULL, NULL, NULL, (LPBYTE)friendly, &cb) != ERROR_SUCCESS
                            || friendly[0] == 0)
                        {
                            StringCchCopyW(friendly, 256, L"(no name)");
                        }

                        WCHAR text[512];
                        StringCchPrintfW(text, 512, L"%s  %s", clsidName, friendly);
                        AddItem(hTree, hClasses, text);
                        foundAny = TRUE;
                    }
                }
            }
            RegCloseKey(hInproc);
        }
        RegCloseKey(hThisClsid);
    }

    RegCloseKey(hClsid);

    if (!foundAny)
    {
        AddItem(hTree, hClasses, L"(no matching CLSIDs found in registry)");
    }

    ExpandAllLocal(hTree, hRoot);
    TreeView_SelectItem(hTree, hRoot);

    return foundAny;
}