#pragma once

// Class that performs lossless JPEG transformations using the TJPEG library
class CJPEGLosslessTransform
{

public:

	// Results of the lossless transformation
	enum EResult {
		Success,
		ReadFileFailed,
		WriteFileFailed,
		TransformationFailed
	};

	// Lossless transformations
	enum ETransformation {
		Rotate90,
		Rotate180,
		Rotate270,
		MirrorH,
		MirrorV,
		NoTransformation, // copy without rotating (used when EXIF composition yields identity)
		Transpose,        // reflect across main diagonal (upper-left to lower-right)
		Transverse        // reflect across anti-diagonal (upper-right to lower-left)
	};

	// Performs a lossless JPEG transformation, transforming the input file and writing the result to the output file.
	// Input and output file can be identical, then the input file is overwritten by the resulting output file.
	// If bClearEXIFOrientation is true, the EXIF orientation tag in the output is reset to 1 (normal).
	static EResult PerformTransformation(LPCTSTR sInputFile, LPCTSTR sOutputFile, ETransformation transformation, bool bAllowTrim, bool bClearEXIFOrientation = false, CString* psErrorDetails = nullptr);

	// Performs a lossless JPEG crop, using the input file and writing the result to the output file.
	// Input and output file can be identical, then the input file is overwritten by the resulting output file.
	static EResult PerformCrop(LPCTSTR sInputFile, LPCTSTR sOutputFile, const CRect& cropRect, CString* psErrorDetails = nullptr);
};