// Since we all love old Windows, i'll make this old windows style

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <oleauto.h>
#include <tchar.h>
#include <strsafe.h>

#include "RegistryMode.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

// IDs
#define IDM_FILE_OPEN 1001
#define IDM_FILE_EXIT 1002
#define IDM_FILE_OPEN_REG 1003
#define IDM_VIEW_EXPAND 1101
#define IDM_VIEW_COLLAPSE 1102
#define IDM_HELP_ABOUT 1201
#define IDC_TREEVIEW 2001

// Globals
HINSTANCE g_hInst = NULL;
HWND g_hMain = NULL;
HWND g_hTree = NULL;
TCHAR g_szFile[MAX_PATH] = {0};

// Helpers
void SetStatusText(HWND hWnd, LPCWSTR text)
{
    // Just put it in the window title
    TCHAR title[512];
    if (text && *text)
        StringCchPrintf(title, 512, L"COM Interface Viewer -- %s", text);
    else
        StringCchCopy(title, 512, L"COM Interface Viewer");
    SetWindowText(hWnd, title);
}

HTREEITEM AddTreeItem(HWND hTree, HTREEITEM hParent, LPCWSTR text, LPARAM lParam = 0)
{
    TVINSERTSTRUCT tvis = {0};
    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.item.pszText = const_cast<LPWSTR>(text);
    tvis.item.lParam = lParam;
    return TreeView_InsertItem(hTree, &tvis);
}

void ExpandAll(HWND hTree, HTREEITEM hItem, BOOL expand)
{
    if (!hItem) return;

    TreeView_Expand(hTree, hItem, expand ? TVE_EXPAND : TVE_COLLAPSE);

    HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
    while (hChild)
    {
        ExpandAll(hTree, hChild, expand);
        hChild = TreeView_GetNextSibling(hTree, hChild);
    }
}

// We're not gonna overcomplicate ourselves, let's just list interfaces + methods + GUIDs
void PopulateFromTypeLib(ITypeLib* pTypeLib, LPCWSTR fileName)
{
    TreeView_DeleteAllItems(g_hTree);

    HTREEITEM hRoot = AddTreeItem(g_hTree, TVI_ROOT, fileName);
    HTREEITEM hIfaces = AddTreeItem(g_hTree, hRoot, L"Interfaces");

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

        // Only real interfaces and dual/dispinterfaces
        if (pAttr->typekind == TKIND_INTERFACE || pAttr->typekind == TKIND_DISPATCH)
        {
            BSTR bstrName = NULL;
            pTypeInfo->GetDocumentation(MEMBERID_NIL, &bstrName, NULL, NULL, NULL);

            WCHAR szGuid[64] = {0};
            StringFromGUID2(pAttr->guid, szGuid, 64);

            WCHAR szText[512];
            if (bstrName && *bstrName)
                StringCchPrintf(szText, 512, L"%s  %s", bstrName, szGuid);
            else
                StringCchPrintf(szText, 512, L"(unnamed)  %s", szGuid);

            // interface node (will hold the methods as children)
            HTREEITEM hIface = AddTreeItem(g_hTree, hIfaces, szText);

            // now dump the methods
            for (UINT m = 0; m < pAttr->cFuncs; ++m)
            {
                FUNCDESC* pFuncDesc = NULL;
                if (FAILED(pTypeInfo->GetFuncDesc(m, &pFuncDesc)) || !pFuncDesc)
                    continue;

                BSTR bstrFuncName = NULL;
                pTypeInfo->GetDocumentation(pFuncDesc->memid, &bstrFuncName, NULL, NULL, NULL);

                WCHAR szMethod[256];
                if (bstrFuncName && *bstrFuncName)
                {
                    // little prefix so you can see what kind it is
                    LPCWSTR kind = L"";
                    switch (pFuncDesc->invkind)
                    {
                    case INVOKE_FUNC: kind = L"[method] "; break;
                    case INVOKE_PROPERTYGET: kind = L"[get] ";    break;
                    case INVOKE_PROPERTYPUT: kind = L"[put] ";    break;
                    case INVOKE_PROPERTYPUTREF:kind = L"[putref] "; break;
                    }
                    StringCchPrintf(szMethod, 256, L"%s%s", kind, bstrFuncName);
                }
                else
                {
                    StringCchPrintf(szMethod, 256, L"(unnamed method)");
                }

                AddTreeItem(g_hTree, hIface, szMethod);

                if (bstrFuncName)
                    SysFreeString(bstrFuncName);

                pTypeInfo->ReleaseFuncDesc(pFuncDesc);
            }

            if (bstrName)
                SysFreeString(bstrName);
        }

        pTypeInfo->ReleaseTypeAttr(pAttr);
        pTypeInfo->Release();
    }

    // expand everything by default so you see the list immediately
    ExpandAll(g_hTree, hRoot, TRUE);
    TreeView_SelectItem(g_hTree, hRoot);
}

BOOL LoadAndShow(LPCWSTR path)
{
    ITypeLib* pTypeLib = NULL;
    HRESULT hr = LoadTypeLib(path, &pTypeLib);
    if (FAILED(hr) || !pTypeLib)
    {
        MessageBox(g_hMain,
                   L"No type library found in this file.\n\n"
                   L"Only DLLs / OCXs / TLBs that contain an embedded type library can be explored.",
                   L"COM Interface Viewer",
                   MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }

    // just the filename for the root node
    LPCWSTR fileName = path;
    for (LPCWSTR p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/')
            fileName = p + 1;

    PopulateFromTypeLib(pTypeLib, fileName);
    pTypeLib->Release();

    StringCchCopy(g_szFile, MAX_PATH, path);
    SetStatusText(g_hMain, fileName);
    return TRUE;
}

// Menu and window procedures
HMENU CreateMainMenu()
{
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    HMENU hHelp = CreatePopupMenu();

    AppendMenu(hFile, MF_STRING, IDM_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenu(hFile, MF_STRING, IDM_FILE_OPEN_REG, L"Open (&no type library)...");
    AppendMenu(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFile, MF_STRING, IDM_FILE_EXIT, L"E&xit\tAlt+F4");

    AppendMenu(hView, MF_STRING, IDM_VIEW_EXPAND,   L"&Expand All");
    AppendMenu(hView, MF_STRING, IDM_VIEW_COLLAPSE, L"&Collapse All");

    AppendMenu(hHelp, MF_STRING, IDM_HELP_ABOUT, L"&About...");

    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"&Help");

    return hMenu;
}

void DoOpen()
{
    TCHAR szFile[MAX_PATH] = {0};

    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"COM Libraries (*.dll;*.ocx;*.tlb)\0*.dll;*.ocx;*.tlb\0"
                      L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"dll";

    if (GetOpenFileName(&ofn))
    {
        LoadAndShow(szFile);
    }
}

void DoOpenRegistry()
{
    TCHAR szFile[MAX_PATH] = {0};

    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hMain;
    ofn.lpstrFilter = L"COM Libraries (*.dll;*.ocx)\0*.dll;*.ocx\0"
                      L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"dll";

    if (GetOpenFileName(&ofn))
    {
        LPCWSTR fileName = szFile;
        for (LPCWSTR p = szFile; *p; ++p)
            if (*p == L'\\' || *p == L'/')
                fileName = p + 1;

        if (PopulateFromRegistry(g_hTree, szFile))
            SetStatusText(g_hMain, fileName);
        else
            SetStatusText(g_hMain, L"(no registered classes)");
    }
}

void DoAbout()
{
    MessageBox(g_hMain,
               L"COM Interface Viewer\n\n"
               L"Don't know what to put here\n"
               L"Having a nice day?\n\n"
               L"You like the GUI?",
               L"About",
               MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hTree = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            WC_TREEVIEW,
            NULL,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
            0, 0, 0, 0,
            hWnd,
            (HMENU)IDC_TREEVIEW,
            g_hInst,
            NULL);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessage(g_hTree, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetStatusText(hWnd, NULL);
        return 0;
    }

    case WM_SIZE:
    {
        if (g_hTree)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            MoveWindow(g_hTree, 0, 0, rc.right, rc.bottom, TRUE);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDM_FILE_OPEN:
            DoOpen();
            break;
        case IDM_FILE_OPEN_REG:
            DoOpenRegistry();
            break;
        case IDM_FILE_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDM_VIEW_EXPAND:
            ExpandAll(g_hTree, TreeView_GetRoot(g_hTree), TRUE);
            break;
        case IDM_VIEW_COLLAPSE:
            ExpandAll(g_hTree, TreeView_GetRoot(g_hTree), FALSE);
            break;
        case IDM_HELP_ABOUT:
            DoAbout();
            break;
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            DoOpen();
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Entry
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr))
        return 1;

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ComInterfaceViewerClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc))
    {
        CoUninitialize();
        return 1;
    }

    HMENU hMenu = CreateMainMenu();

    g_hMain = CreateWindowEx(
        0,
        L"ComInterfaceViewerClass",
        L"COM Interface Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        640, 480,
        NULL,
        hMenu,
        hInstance,
        NULL);

    if (!g_hMain)
    {
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2)
    {
        LoadAndShow(argv[1]);
    }
    if (argv)
        LocalFree(argv);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
