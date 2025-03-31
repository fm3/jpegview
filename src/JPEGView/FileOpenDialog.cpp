#include "StdAfx.h"
#include "FileOpenDialog.h"

FileOpenDialog::FileOpenDialog(LPCTSTR sInitialFileName, LPCTSTR sFileEndings) {
    COMDLG_FILTERSPEC fileTypes[] = {
        { _T("Images"), sFileEndings },
        { L"All Files", L"*.*" }
    };
    GetPtr()->SetFileTypes(_countof(fileTypes), fileTypes);
    GetPtr()->SetOptions(FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);

    if (sInitialFileName != NULL) {
        PIDLIST_ABSOLUTE pidl;
        SHILCreateFromPath(sInitialFileName, &pidl, NULL);
        IShellItem* pItem;
        SHCreateShellItem(NULL, NULL, pidl, &pItem);
        IShellItem* pParent;
        pItem->GetParent(&pParent);

        HRESULT hr = GetPtr()->SetFolder(pParent);
    }
}

CString FileOpenDialog::GetFilePathStr() {
    WCHAR szPath[MAX_PATH];
    GetFilePath(szPath, MAX_PATH);
    return CString(szPath);
}