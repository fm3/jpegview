#pragma once
#include "StdAfx.h"

class FileOpenDialog : public CShellFileOpenDialogImpl<FileOpenDialog> {
public:
    FileOpenDialog(LPCTSTR sInitialFileName, LPCTSTR sFileEndings);
    CString GetFilePathStr();
};
