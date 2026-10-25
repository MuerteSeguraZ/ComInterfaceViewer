#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <strsafe.h>
#include <string>

#include "RegistryMode.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")

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

static BOOL ReadStringValue(HKEY hKey, LPCWSTR valueName, WCHAR* buf, DWORD cchBuf)
{
    DWORD cb = cchBuf * sizeof(WCHAR);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)buf, &cb);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
    {
        buf[0] = 0;
        return FALSE;
    }
    if (type == REG_EXPAND_SZ)
    {
        WCHAR tmp[MAX_PATH];
        ExpandEnvironmentStringsW(buf, tmp, MAX_PATH);
        StringCchCopyW(buf, cchBuf, tmp);
    }
    return TRUE;
}

static void TypeDescToString(ITypeInfo* pTypeInfo, const TYPEDESC* pTd, std::wstring& out)
{
    switch (pTd->vt)
    {
    case VT_PTR:
        TypeDescToString(pTypeInfo, pTd->lptdesc, out);
        out += L"*";
        return;

    case VT_SAFEARRAY:
        out += L"SAFEARRAY(";
        TypeDescToString(pTypeInfo, pTd->lptdesc, out);
        out += L")";
        return;

    case VT_CARRAY:
        TypeDescToString(pTypeInfo, &pTd->lpadesc->tdescElem, out);
        for (USHORT d = 0; d < pTd->lpadesc->cDims; ++d)
        {
            WCHAR dim[32];
            StringCchPrintfW(dim, 32, L"[%lu]", pTd->lpadesc->rgbounds[d].cElements);
            out += dim;
        }
        return;

    case VT_USERDEFINED:
    {
        // IFoo, GUID, some enum, etc.
        ITypeInfo* pRef = NULL;
        BSTR bstrName = NULL;
        if (SUCCEEDED(pTypeInfo->GetRefTypeInfo(pTd->hreftype, &pRef)) && pRef)
        {
            pRef->GetDocumentation(MEMBERID_NIL, &bstrName, NULL, NULL, NULL);
            pRef->Release();
        }
        if (bstrName && *bstrName)
            out += bstrName;
        else
            out += L"(unknown user type)";
        if (bstrName)
            SysFreeString(bstrName);
        return;
    }

    case VT_I1:       out += L"char"; return;
    case VT_UI1:      out += L"unsigned char"; return;
    case VT_I2:       out += L"short"; return;
    case VT_UI2:      out += L"unsigned short"; return;
    case VT_I4:       out += L"long"; return;
    case VT_UI4:      out += L"unsigned long"; return;
    case VT_I8:       out += L"__int64"; return;
    case VT_UI8:      out += L"unsigned __int64"; return;
    case VT_INT:      out += L"int"; return;
    case VT_UINT:     out += L"unsigned int"; return;
    case VT_R4:       out += L"float"; return;
    case VT_R8:       out += L"double"; return;
    case VT_CY:       out += L"CURRENCY"; return;
    case VT_DATE:     out += L"DATE"; return;
    case VT_BSTR:     out += L"BSTR"; return;
    case VT_BOOL:     out += L"VARIANT_BOOL"; return;
    case VT_VARIANT:  out += L"VARIANT"; return;
    case VT_DECIMAL:  out += L"DECIMAL"; return;
    case VT_ERROR:    out += L"SCODE"; return;
    case VT_HRESULT:  out += L"HRESULT"; return;
    case VT_DISPATCH: out += L"IDispatch*"; return;
    case VT_UNKNOWN:  out += L"IUnknown*"; return;
    case VT_VOID:     out += L"void"; return;
    case VT_LPSTR:    out += L"LPSTR"; return;
    case VT_LPWSTR:   out += L"LPWSTR"; return;
    }

    WCHAR unk[32];
    StringCchPrintfW(unk, 32, L"VT_%u", (UINT)pTd->vt);
    out += unk;
}

// "[in, out, retval]" or nothing at all
static void ParamFlagsToString(USHORT flags, std::wstring& out)
{
    std::wstring f;
    if (flags & PARAMFLAG_FIN)      f += L"in";
    if (flags & PARAMFLAG_FOUT)     f += f.empty() ? L"out" : L", out";
    if (flags & PARAMFLAG_FLCID)    f += f.empty() ? L"lcid" : L", lcid";
    if (flags & PARAMFLAG_FRETVAL)  f += f.empty() ? L"retval" : L", retval";
    if (flags & PARAMFLAG_FOPT)     f += f.empty() ? L"optional" : L", optional";

    if (!f.empty())
    {
        out += L"[";
        out += f;
        out += L"] ";
    }
}

void AddMethodItem(HWND hTree, HTREEITEM hParent, ITypeInfo* pTypeInfo, FUNCDESC* pFuncDesc, BOOL showParams)
{
    // memid -> name (index 0) followed by the parameter names
    BSTR names[64] = {0};
    UINT cNames = 0;
    pTypeInfo->GetNames(pFuncDesc->memid, names, ARRAYSIZE(names), &cNames);

    // little prefix so you can see what kind it is
    LPCWSTR kind = L"";
    switch (pFuncDesc->invkind)
    {
    case INVOKE_FUNC:           kind = L"[method] "; break;
    case INVOKE_PROPERTYGET:    kind = L"[get] ";    break;
    case INVOKE_PROPERTYPUT:    kind = L"[put] ";    break;
    case INVOKE_PROPERTYPUTREF: kind = L"[putref] "; break;
    }

    std::wstring text = kind;

    if (!showParams)
    {
        if (cNames && names[0] && *names[0])
            text += names[0];
        else
            text = L"(unnamed method)";
    }
    else
    {
        // return type
        TypeDescToString(pTypeInfo, &pFuncDesc->elemdescFunc.tdesc, text);
        text += L" ";

        if (cNames && names[0] && *names[0])
            text += names[0];
        else
            text += L"(unnamed method)";

        // parameters
        text += L"(";
        for (SHORT p = 0; p < pFuncDesc->cParams; ++p)
        {
            const ELEMDESC* pElem = &pFuncDesc->lprgelemdescParam[p];

            if (p)
                text += L", ";

            ParamFlagsToString(pElem->paramdesc.wParamFlags, text);
            TypeDescToString(pTypeInfo, &pElem->tdesc, text);
            text += L" ";

            if ((UINT)(p + 1) < cNames && names[p + 1] && *names[p + 1])
            {
                text += names[p + 1];
            }
            else
            {
                WCHAR argName[16];
                StringCchPrintfW(argName, 16, L"arg%d", (int)p);
                text += argName;
            }
        }
        text += L")";

        // vtable offset (bytes, as stored in the typelib) or dispid
        WCHAR where[48];
        if (pFuncDesc->funckind == FUNC_DISPATCH)
            StringCchPrintfW(where, 48, L"  [dispid 0x%lX]", (ULONG)pFuncDesc->memid);
        else
            StringCchPrintfW(where, 48, L"  [vtbl +0x%X]", (UINT)pFuncDesc->oVft);
        text += where;
    }

    AddItem(hTree, hParent, text.c_str());

    for (UINT i = 0; i < cNames; ++i)
        if (names[i])
            SysFreeString(names[i]);
}

static void AddTypeLibMethods(HWND hTree, HTREEITEM hParent, HKEY hClsidKey, REFGUID clsidGuid, LPCWSTR dllPath, BOOL showParams)
{
    ITypeLib* pTypeLib = NULL;

    WCHAR typelibGuidStr[64] = {0};
    if (ReadStringValue(hClsidKey, L"TypeLib", typelibGuidStr, 64))
    {
        GUID typelibGuid = {0};
        if (SUCCEEDED(CLSIDFromString(typelibGuidStr, &typelibGuid)))
        {
            HKEY hTypeLibRoot = NULL;
            WCHAR keyPath[128];
            StringCchPrintfW(keyPath, 128, L"TypeLib\\%s", typelibGuidStr);

            if (RegOpenKeyExW(HKEY_CLASSES_ROOT, keyPath, 0, KEY_READ, &hTypeLibRoot) == ERROR_SUCCESS)
            {
                WCHAR version[32] = {0};
                DWORD cch = 32;
                if (RegEnumKeyExW(hTypeLibRoot, 0, version, &cch, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
                {
                    USHORT major = 1, minor = 0;
                    swscanf(version, L"%hu.%hu", &major, &minor);
                    LoadRegTypeLib(typelibGuid, major, minor, LOCALE_SYSTEM_DEFAULT, &pTypeLib);
                }
                RegCloseKey(hTypeLibRoot);
            }
        }
    }

    if (!pTypeLib && dllPath)
        LoadTypeLib(dllPath, &pTypeLib);

    if (!pTypeLib)
        return;

    UINT count = pTypeLib->GetTypeInfoCount();
    for (UINT i = 0; i < count; ++i)
    {
        ITypeInfo* pTypeInfo = NULL;
        if (FAILED(pTypeLib->GetTypeInfo(i, &pTypeInfo)) || !pTypeInfo)
            continue;

        TYPEATTR* pAttr = NULL;
        if (FAILED(pTypeInfo->GetTypeAttr(&pAttr)) || !pAttr)
        {
            pTypeInfo->Release();
            continue;
        }

        if (pAttr->typekind == TKIND_COCLASS && InlineIsEqualGUID(pAttr->guid, clsidGuid))
        {
            for (WORD ifc = 0; ifc < pAttr->cImplTypes; ++ifc)
            {
                HREFTYPE hRef = 0;
                if (FAILED(pTypeInfo->GetRefTypeOfImplType(ifc, &hRef)))
                    continue;

                ITypeInfo* pIfaceInfo = NULL;
                if (FAILED(pTypeInfo->GetRefTypeInfo(hRef, &pIfaceInfo)) || !pIfaceInfo)
                    continue;

                TYPEATTR* pIfaceAttr = NULL;
                if (SUCCEEDED(pIfaceInfo->GetTypeAttr(&pIfaceAttr)) && pIfaceAttr)
                {
                    BSTR bstrIfaceName = NULL;
                    pIfaceInfo->GetDocumentation(MEMBERID_NIL, &bstrIfaceName, NULL, NULL, NULL);

                    WCHAR szIface[256];
                    if (bstrIfaceName && *bstrIfaceName)
                        StringCchPrintfW(szIface, 256, L"%s", bstrIfaceName);
                    else
                        StringCchCopyW(szIface, 256, L"(unnamed interface)");

                    HTREEITEM hIface = AddItem(hTree, hParent, szIface);

                    // methods of this interface
                    for (UINT m = 0; m < pIfaceAttr->cFuncs; ++m)
                    {
                        FUNCDESC* pFuncDesc = NULL;
                        if (FAILED(pIfaceInfo->GetFuncDesc(m, &pFuncDesc)) || !pFuncDesc)
                            continue;

                        AddMethodItem(hTree, hIface, pIfaceInfo, pFuncDesc, showParams);

                        pIfaceInfo->ReleaseFuncDesc(pFuncDesc);
                    }

                    if (bstrIfaceName) SysFreeString(bstrIfaceName);
                    pIfaceInfo->ReleaseTypeAttr(pIfaceAttr);
                }
                pIfaceInfo->Release();
            }
        }

        pTypeInfo->ReleaseTypeAttr(pAttr);
        pTypeInfo->Release();
    }

    pTypeLib->Release();
}

BOOL PopulateFromRegistry(HWND hTree, LPCWSTR dllPath, BOOL showDetails, BOOL showParams)
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
            if (ReadStringValue(hInproc, NULL, serverPath, MAX_PATH))
            {
                WCHAR normServer[MAX_PATH] = {0};
                if (GetNormalizedPath(serverPath, normServer, MAX_PATH) &&
                    wcscmp(normServer, normDll) == 0)
                {
                    WCHAR friendly[256] = {0};
                    if (!ReadStringValue(hThisClsid, NULL, friendly, 256) || !friendly[0])
                        StringCchCopyW(friendly, 256, L"(no name)");

                    WCHAR text[512];
                    StringCchPrintfW(text, 512, L"%s  %s", clsidName, friendly);
                    HTREEITEM hClass = AddItem(hTree, hClasses, text);
                    foundAny = TRUE;

                    // optional registry details
                    if (showDetails)
                    {
                        WCHAR tmp[256];

                        if (ReadStringValue(hInproc, L"ThreadingModel", tmp, 256) && tmp[0])
                        {
                            WCHAR line[300];
                            StringCchPrintfW(line, 300, L"ThreadingModel: %s", tmp);
                            AddItem(hTree, hClass, line);
                        }

                        if (ReadStringValue(hThisClsid, L"ProgID", tmp, 256) && tmp[0])
                        {
                            WCHAR line[300];
                            StringCchPrintfW(line, 300, L"ProgID: %s", tmp);
                            AddItem(hTree, hClass, line);
                        }

                        if (ReadStringValue(hThisClsid, L"VersionIndependentProgID", tmp, 256) && tmp[0])
                        {
                            WCHAR line[300];
                            StringCchPrintfW(line, 300, L"VersionIndependentProgID: %s", tmp);
                            AddItem(hTree, hClass, line);
                        }

                        if (ReadStringValue(hThisClsid, L"TypeLib", tmp, 256) && tmp[0])
                        {
                            WCHAR line[300];
                            StringCchPrintfW(line, 300, L"TypeLib: %s", tmp);
                            AddItem(hTree, hClass, line);
                        }
                    }

                    // try to show methods from associated typelib
                    GUID clsidGuid = {0};
                    if (SUCCEEDED(CLSIDFromString(clsidName, &clsidGuid)))
                    {
                        AddTypeLibMethods(hTree, hClass, hThisClsid, clsidGuid, dllPath, showParams);
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