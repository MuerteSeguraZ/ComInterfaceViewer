#pragma once

#include <windows.h>

// Scans the registry for CLSIDs whose InprocServer32 points to the given DLL
// and populates the treeview with them.
// if showDetails = TRUE then also show ThreadingModel / ProgID / etc.
BOOL PopulateFromRegistry(HWND hTree, LPCWSTR dllPath, BOOL showDetails);