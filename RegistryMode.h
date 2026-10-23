#pragma once

#include <windows.h>

// Scans the registry for CLSIDs whose InprocServer32 points to the given DLL
// and populates the treeview with them.
BOOL PopulateFromRegistry(HWND hTree, LPCWSTR dllPath);