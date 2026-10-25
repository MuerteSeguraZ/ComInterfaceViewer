#pragma once

#include <windows.h>
#include <oleauto.h>

// Scans the registry for CLSIDs whose InprocServer32 points to the given DLL
// and populates the treeview with them.
// if showDetails = TRUE then also show ThreadingModel / ProgID / etc.
// if showParams = TRUE then methods are shown with their full signature.
BOOL PopulateFromRegistry(HWND hTree, LPCWSTR dllPath, BOOL showDetails, BOOL showParams);

// Adds one method node under hParent.
// if showParams = TRUE then the node text is the full signature
// (return type, parameter flags/types/names, vtable offset or dispid).
void AddMethodItem(HWND hTree, HTREEITEM hParent, ITypeInfo* pTypeInfo, FUNCDESC* pFuncDesc, BOOL showParams);