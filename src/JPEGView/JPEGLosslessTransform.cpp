#include "StdAfx.h"
#include "JPEGLosslessTransform.h"
#include "Helpers.h"
#include "libjpeg-turbo\include\turbojpeg.h"

CJPEGLosslessTransform::EResult _DoTransformation(LPCTSTR sInputFile, LPCTSTR sOutputFile, tjtransform &transform, bool bClearEXIFOrientation, CString* psErrorDetails);
CJPEGLosslessTransform::EResult _DoEXIFOrientationClear(LPCTSTR sInputFile, LPCTSTR sOutputFile, CString* psErrorDetails);
unsigned char* _ReadFile(LPCTSTR sFileName, unsigned int & nLengthBytes);
bool _WriteFile(LPCTSTR sFileName, unsigned char* pBuffer, unsigned int nLengthBytes);
int _TransformationEnumToOpCode(CJPEGLosslessTransform::ETransformation transformation);
void _ClearEXIFOrientation(unsigned char* pJPEGBytes, unsigned int nLength);

// Performs a lossless JPEG transformation, transforming the input file and writing the result to the output file.
// Input and output file can be identical, then the input file is overwritten by the resulting output file.
CJPEGLosslessTransform::EResult CJPEGLosslessTransform::PerformTransformation(LPCTSTR sInputFile, LPCTSTR sOutputFile,
	CJPEGLosslessTransform::ETransformation transformation, bool bAllowTrim, bool bClearEXIFOrientation, CString* psErrorDetails) {
	// When the composed transform is identity, skip libjpeg-turbo entirely and just patch the EXIF tag.
	if (transformation == NoTransformation) {
		if (bClearEXIFOrientation) {
			return _DoEXIFOrientationClear(sInputFile, sOutputFile, psErrorDetails);
		}
		return Success;
	}

	tjtransform transform{ 0 };
	transform.op = _TransformationEnumToOpCode(transformation);
	transform.options = bAllowTrim ? TJXOPT_TRIM : TJXOPT_PERFECT;

	return _DoTransformation(sInputFile, sOutputFile, transform, bClearEXIFOrientation, psErrorDetails);
}

// Performs a lossless JPEG crop, using the input file and writing the result to the output file.
// Input and output file can be identical, then the input file is overwritten by the resulting output file.
CJPEGLosslessTransform::EResult CJPEGLosslessTransform::PerformCrop(LPCTSTR sInputFile, LPCTSTR sOutputFile, const CRect& cropRect, CString* psErrorDetails) {
	tjtransform transform{ 0 };
	transform.op = TJXOP_NONE;
	transform.options = TJXOPT_PERFECT | TJXOPT_CROP;
	transform.r.x = cropRect.left;
	transform.r.y = cropRect.top;
	transform.r.w = cropRect.Width();
	transform.r.h = cropRect.Height();

	return _DoTransformation(sInputFile, sOutputFile, transform, false, psErrorDetails);
}


// Reads the file, patches the EXIF orientation tag to 1, and writes it back without touching pixel data.
static CJPEGLosslessTransform::EResult _DoEXIFOrientationClear(LPCTSTR sInputFile, LPCTSTR sOutputFile, CString* psErrorDetails) {
	unsigned int nLength;
	unsigned char* pBytes = _ReadFile(sInputFile, nLength);
	if (pBytes == NULL) return CJPEGLosslessTransform::ReadFileFailed;
	_ClearEXIFOrientation(pBytes, nLength);
	bool bOk = _WriteFile(sOutputFile, pBytes, nLength);
	delete[] pBytes;
	return bOk ? CJPEGLosslessTransform::Success : CJPEGLosslessTransform::WriteFileFailed;
}

static CJPEGLosslessTransform::EResult _DoTransformation(LPCTSTR sInputFile, LPCTSTR sOutputFile, tjtransform &transform, bool bClearEXIFOrientation, CString* psErrorDetails) {
	CJPEGLosslessTransform::EResult eResult = CJPEGLosslessTransform::Success;

	tjhandle hTransform = tj3Init(TJINIT_TRANSFORM);

	unsigned int nNumBytesInput;
	unsigned char* pInputJPEGBytes = _ReadFile(sInputFile, nNumBytesInput);
	if (pInputJPEGBytes != NULL) {
		unsigned char* pOutputJPEGBytes = NULL;
		size_t nNumBytesOutput = 0;
		if (0 == tj3Transform(hTransform, pInputJPEGBytes, nNumBytesInput, 1, &pOutputJPEGBytes, &nNumBytesOutput, &transform) && pOutputJPEGBytes != NULL) {
			if (bClearEXIFOrientation) {
				_ClearEXIFOrientation(pOutputJPEGBytes, (unsigned int)nNumBytesOutput);
			}
			if (!_WriteFile(sOutputFile, pOutputJPEGBytes, nNumBytesOutput)) {
				eResult = CJPEGLosslessTransform::WriteFileFailed;
			}
		} else {
			if (psErrorDetails != nullptr) {
				*psErrorDetails = CString(tj3GetErrorStr(hTransform));
			}
			eResult = CJPEGLosslessTransform::TransformationFailed;
		}
		if (pOutputJPEGBytes != NULL) {
			tj3Free(pOutputJPEGBytes);
		}
	} else {
		eResult = CJPEGLosslessTransform::ReadFileFailed;
	}

	delete[] pInputJPEGBytes;

	tj3Destroy(hTransform);

	return eResult;
}

static unsigned char* _ReadFile(LPCTSTR sFileName, unsigned int & nLengthBytes) {
	const unsigned int MAX_JPEG_FILE_SIZE = 1024*1024*50; // 50 MB

	nLengthBytes = 0;
	HANDLE hFile = ::CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	long long nFileSize = Helpers::GetFileSize(hFile);
	if (nFileSize > MAX_JPEG_FILE_SIZE) {
		::CloseHandle(hFile);
		return NULL;
	}

	unsigned char *pBuffer = new(std::nothrow) unsigned char[nFileSize];
	if (pBuffer == NULL) {
		::CloseHandle(hFile);
		return NULL;
	}

	unsigned int nNumBytesRead;
	if (::ReadFile(hFile, pBuffer, nFileSize, (LPDWORD) &nNumBytesRead, NULL) && nNumBytesRead == nFileSize) {
		::CloseHandle(hFile);
		nLengthBytes = nFileSize;
		return pBuffer;
	}
	delete[] pBuffer;
	::CloseHandle(hFile);
	return NULL;
}

static bool _WriteFile(LPCTSTR sFileName, unsigned char* pBuffer, unsigned int nLengthBytes) {
	LPCTSTR sTempEnding = _T("");
	FILETIME lastWriteTime;
	bool bRestoreWriteTime = false;
	HANDLE hFile = ::CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile != INVALID_HANDLE_VALUE) {
		bRestoreWriteTime = ::GetFileTime(hFile, NULL, NULL, &lastWriteTime);
		::CloseHandle(hFile);
		sTempEnding = _T(".tmp");
	}

	// For security reasons, we never write to an existing file. So generate a tmp file if the file already exists.
	// If all writing succeeds, the existing file is finally replaced by the temporary one.
	CString sNewFileName = CString(sFileName) + sTempEnding;
	hFile = ::CreateFile(sNewFileName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return false;
	}

	unsigned int nNumBytesWritten;
	bool bOk = ::WriteFile(hFile, pBuffer, nLengthBytes, (LPDWORD) &nNumBytesWritten, NULL) && nNumBytesWritten == nLengthBytes;
	
	if (bOk && bRestoreWriteTime) {
		::SetFileTime(hFile, NULL, NULL, &lastWriteTime);
	}

	::CloseHandle(hFile);

	// If the file was written to a temporary file and it succeeded, replace now the existing file with the temporary
	if (bOk && sTempEnding[0] != 0) {
		bOk = ::DeleteFile(sFileName);
		if (bOk) {
			bOk = ::MoveFile(sNewFileName, sFileName);
		}
	} else if (!bOk) {
		// new file is only partly written or not at all, make sure it is deleted
		::DeleteFile(sNewFileName);
	}

	return bOk;
}

static int _TransformationEnumToOpCode(CJPEGLosslessTransform::ETransformation transformation) {
	switch (transformation) {
		case CJPEGLosslessTransform::Rotate90:
			return TJXOP_ROT90;
		case CJPEGLosslessTransform::Rotate270:
			return TJXOP_ROT270;
		case CJPEGLosslessTransform::Rotate180:
			return TJXOP_ROT180;
		case CJPEGLosslessTransform::MirrorH:
			return TJXOP_HFLIP;
		case CJPEGLosslessTransform::MirrorV:
			return TJXOP_VFLIP;
		case CJPEGLosslessTransform::Transpose:
			return TJXOP_TRANSPOSE;
		case CJPEGLosslessTransform::Transverse:
			return TJXOP_TRANSVERSE;
		default: // NoTransformation
			return TJXOP_NONE;
	}
}

// Finds the EXIF APP1 block in a JPEG stream and resets the orientation tag to 1 (normal).
static void _ClearEXIFOrientation(unsigned char* pJPEGBytes, unsigned int nLength) {
	if (nLength < 4 || pJPEGBytes[0] != 0xFF || pJPEGBytes[1] != 0xD8) return;

	// Walk JPEG marker segments to find APP1 (0xFF 0xE1)
	unsigned int pos = 2;
	while (pos + 4 <= nLength) {
		if (pJPEGBytes[pos] != 0xFF) break;
		unsigned char marker = pJPEGBytes[pos + 1];
		if (marker == 0xDA) break; // Start of Scan — no more header markers follow
		unsigned int segLen = ((unsigned int)pJPEGBytes[pos + 2] << 8) | pJPEGBytes[pos + 3];

		if (marker == 0xE1 && segLen >= 14 && pos + 4 + segLen <= nLength) {
			unsigned char* app1Data = pJPEGBytes + pos + 4;
			// Verify EXIF identifier "Exif\0\0"
			if (app1Data[0]=='E' && app1Data[1]=='x' && app1Data[2]=='i' && app1Data[3]=='f'
				&& app1Data[4]==0 && app1Data[5]==0) {
				unsigned char* tiff = app1Data + 6;
				bool bLE = (tiff[0] == 'I' && tiff[1] == 'I');
				// Verify TIFF magic number (42)
				unsigned short magic = bLE ? (tiff[2] | ((unsigned short)tiff[3] << 8))
				                           : (((unsigned short)tiff[2] << 8) | tiff[3]);
				if (magic != 42) break;

				unsigned int ifd0Off = bLE
					? (tiff[4] | ((unsigned int)tiff[5]<<8) | ((unsigned int)tiff[6]<<16) | ((unsigned int)tiff[7]<<24))
					: (((unsigned int)tiff[4]<<24) | ((unsigned int)tiff[5]<<16) | ((unsigned int)tiff[6]<<8) | tiff[7]);

				unsigned char* ifd0 = tiff + ifd0Off;
				unsigned int appEnd = pos + 2 + segLen;
				if (ifd0 + 2 > pJPEGBytes + appEnd) break;

				unsigned short numEntries = bLE ? (ifd0[0] | ((unsigned short)ifd0[1]<<8))
				                                : (((unsigned short)ifd0[0]<<8) | ifd0[1]);
				unsigned char* entry = ifd0 + 2;
				for (unsigned short i = 0; i < numEntries; i++) {
					if (entry + 12 > pJPEGBytes + appEnd) break;
					unsigned short tag = bLE ? (entry[0] | ((unsigned short)entry[1]<<8))
					                         : (((unsigned short)entry[0]<<8) | entry[1]);
					if (tag == 0x0112) { // Orientation tag — write value 1 (normal)
						if (bLE) { entry[8] = 1; entry[9] = 0; }
						else     { entry[8] = 0; entry[9] = 1; }
						return;
					}
					entry += 12;
				}
			}
		}
		pos += 2 + segLen;
	}
}