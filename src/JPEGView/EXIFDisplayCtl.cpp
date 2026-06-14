#include "StdAfx.h"
#include "resource.h"
#include "MainDlg.h"
#include "JPEGImage.h"
#include "EXIFDisplayCtl.h"
#include "EXIFDisplay.h"
#include "RawMetadata.h"
#include "SettingsProvider.h"
#include "HelpersGUI.h"
#include "NLS.h"

static int GetFileNameHeight(HDC dc) {
	CSize size;
	HelpersGUI::SelectDefaultFileNameFont(dc);
	::GetTextExtentPoint32(dc, _T("("), 1, &size);
	return size.cy;
}

static CString CreateGPSString(GPSCoordinate* latitude, GPSCoordinate* longitude) {
	const int BUFF_SIZE = 96;
	TCHAR buff[BUFF_SIZE];
	_stprintf_s(buff, BUFF_SIZE, _T("%s%.0f°%.0f′%.0f″ %s%.0f°%.0f′%.0f″"),
		latitude->GetReference(), latitude->Degrees, latitude->Minutes, latitude->Seconds,
		longitude->GetReference(), longitude->Degrees, longitude->Minutes, longitude->Seconds);
	return CString(buff);
}

static CString CreateGPSURL(GPSCoordinate* latitude, GPSCoordinate* longitude) {
	double lng = longitude->Degrees + longitude->Minutes / 60 + longitude->Seconds / (60 * 60);
	if (_tcsicmp(longitude->GetReference(), _T("W")) == 0)
		lng = -lng;

	double lat = latitude->Degrees + latitude->Minutes / 60 + latitude->Seconds / (60 * 60);
	if (_tcsicmp(latitude->GetReference(), _T("S")) == 0)
		lat = -lat;

	CString mapProvider = CSettingsProvider::This().GPSMapProvider();

	const int BUFF_SIZE = 32;
	TCHAR buffLat[BUFF_SIZE];
	TCHAR buffLng[BUFF_SIZE];
	_stprintf_s(buffLat, BUFF_SIZE, _T("%.5f"), lat);
	_stprintf_s(buffLng, BUFF_SIZE, _T("%.5f"), lng);

	mapProvider.Replace(_T("{lat}"), buffLat);
	mapProvider.Replace(_T("{lng}"), buffLng);

	return mapProvider;
}

CEXIFDisplayCtl::CEXIFDisplayCtl(CMainDlg* pMainDlg, CPanel* pImageProcPanel) : CPanelController(pMainDlg, false) {
	m_bVisible = CSettingsProvider::This().ShowFileInfo();
	m_nFileNameHeight = 0;
	m_pImageProcPanel = pImageProcPanel;
	m_pPanel = m_pEXIFDisplay = new CEXIFDisplay(pMainDlg->m_hWnd, this);
	m_bShowHistogram = CSettingsProvider::This().ShowHistogram();
	m_pEXIFDisplay->SetShowHistogram(m_bShowHistogram);
}

CEXIFDisplayCtl::~CEXIFDisplayCtl() {
	delete m_pEXIFDisplay;
	m_pEXIFDisplay = NULL;
}

bool CEXIFDisplayCtl::IsVisible() { 
	return CurrentImage() != NULL && m_bVisible; 
}

void CEXIFDisplayCtl::SetVisible(bool bVisible) {
	if (m_bVisible != bVisible) {
		m_bVisible = bVisible;
		InvalidateMainDlg();
	}
}

void CEXIFDisplayCtl::SetActive(bool bActive) {
	SetVisible(bActive);
}

void CEXIFDisplayCtl::SetShowHistogram(bool bShowHistogram) {
	m_bShowHistogram = bShowHistogram;
	if (m_bShowHistogram) {
		::OutputDebugString(_T("show!"));
	}
	else {
		::OutputDebugString(_T("hide!"));
	}
	m_pEXIFDisplay->SetShowHistogram(m_bShowHistogram);
	InvalidateMainDlg();
}

void CEXIFDisplayCtl::AfterNewImageLoaded() {
	m_pEXIFDisplay->ClearTexts();
	m_pEXIFDisplay->SetHistogram(NULL);
}

void CEXIFDisplayCtl::OnPrePaintMainDlg(HDC hPaintDC) {
	if (m_pMainDlg->IsShowFileName() && m_nFileNameHeight == 0) {
		m_nFileNameHeight = GetFileNameHeight(hPaintDC);
	}
	m_pEXIFDisplay->SetPosition(CPoint(m_pImageProcPanel->PanelRect().left, m_pMainDlg->IsShowFileName() ? m_nFileNameHeight + 6 : 0));
	FillEXIFDataDisplay();
	if (CurrentImage() != NULL && m_pEXIFDisplay->GetShowHistogram()) {
		m_pEXIFDisplay->SetHistogram(CurrentImage()->GetProcessedHistogram());
	}
}

CString CEXIFDisplayCtl::FormatRational(Rational rational) {
	CString sFormatted;
	if (rational.Denominator == 1) {
		sFormatted.Format(_T("%d"), rational.Numerator);
	}
	else if (rational.Numerator > 9) {
		if (rational.Numerator * 3 < rational.Denominator) {
			sFormatted.Format(_T("1/%d"), rational.Denominator/ rational.Numerator);
		}
		else {
			sFormatted.Format(_T("%g"), double(rational.Numerator) / rational.Denominator);
		}
	}
	else {
		sFormatted.Format(_T("%d/%d"), rational.Numerator, rational.Denominator);
	}
	return sFormatted;
}

void CEXIFDisplayCtl::FillEXIFDataDisplay() {
	m_pEXIFDisplay->ClearTexts();

	m_pEXIFDisplay->SetHistogram(NULL);

	CEXIFReader* pEXIFReader = NULL;
	CRawMetadata* pRawMetaData = NULL;
	if (!CurrentImage()->IsClipboardImage()) {
		pEXIFReader = CurrentImage()->GetEXIFReader();
		pRawMetaData = CurrentImage()->GetRawMetadata();
	}


	CString sPrefix, sFileTitle;
	LPCTSTR sCurrentFileName = m_pMainDlg->CurrentFileName(true);
	const CFileList* pFileList = m_pMainDlg->GetFileList();
	if (CurrentImage()->IsClipboardImage()) {
		sPrefix = sCurrentFileName;
	} else if (pFileList->Current() != NULL) {
		sPrefix.Format(_T("[%d/%d]"), pFileList->CurrentIndex() + 1, pFileList->Size());
		sFileTitle = sCurrentFileName;
		sFileTitle += Helpers::GetMultiframeIndex(m_pMainDlg->GetCurrentImage());
	}
	LPCTSTR sComment = NULL;
	m_pEXIFDisplay->AddPrefix(sPrefix);
	m_pEXIFDisplay->AddTitle(sFileTitle);



	if (pEXIFReader != NULL) {
		if (pEXIFReader->GetAcquisitionTimePresent()) {
			m_pEXIFDisplay->AddLine(NULL, pEXIFReader->GetAcquisitionTime());
		}
		else if (pEXIFReader->GetDateTimePresent()) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exif date:")), pEXIFReader->GetDateTime());
		}
		else {
			const FILETIME* pFileTime = pFileList->CurrentModificationTime();
			if (pFileTime != NULL) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modified:")), *pFileTime);
			}
		}
	}
	else if (pRawMetaData != NULL) {
		if (pRawMetaData->GetAcquisitionTime().wYear > 1985) {
			m_pEXIFDisplay->AddLine(NULL, pRawMetaData->GetAcquisitionTime());
		}
		else {
			const FILETIME* pFileTime = pFileList->CurrentModificationTime();
			if (pFileTime != NULL) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modified:")), *pFileTime);
			}
		}
	}
	else {
		const FILETIME* pFileTime = pFileList->CurrentModificationTime();
		if (pFileTime != NULL) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modified:")), *pFileTime);
		}
	}


	if (pEXIFReader != NULL) {
		if (pEXIFReader->IsGPSInformationPresent()) {
			CString sGPSLocation = CreateGPSString(pEXIFReader->GetGPSLatitude(), pEXIFReader->GetGPSLongitude());
			m_pEXIFDisplay->SetGPSLocation(sGPSLocation, CreateGPSURL(pEXIFReader->GetGPSLatitude(), pEXIFReader->GetGPSLongitude()));
			m_pEXIFDisplay->AddLine(NULL, sGPSLocation, true);

			if (pEXIFReader->IsGPSAltitudePresent()) {
				CString sFormattedAltitude;
				sFormattedAltitude.Format(_T("                                           at %g  m"), pEXIFReader->GetGPSAltitude());
				m_pEXIFDisplay->AddLine(NULL, sFormattedAltitude, 0, true);
			}
		}
	}
	else if (pRawMetaData != NULL) {
		if (pRawMetaData->IsGPSInformationPresent()) {
			CString sGPSLocation = CreateGPSString(pRawMetaData->GetGPSLatitude(), pRawMetaData->GetGPSLongitude());
			m_pEXIFDisplay->SetGPSLocation(sGPSLocation, CreateGPSURL(pRawMetaData->GetGPSLatitude(), pRawMetaData->GetGPSLongitude()));
			m_pEXIFDisplay->AddLine(NULL, sGPSLocation, true);
		}
		if (pRawMetaData->IsGPSAltitudePresent()) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Altitude:")), pRawMetaData->GetGPSAltitude(), 0);
		}
	}

	CString sFormattedSize;
	sFormattedSize.Format(_T("%d × %d"), CurrentImage()->OrigWidth(), CurrentImage()->OrigHeight());
	m_pEXIFDisplay->AddLine(NULL, sFormattedSize);

	CString secondColPadding = _T("");
	for (int i = 0; i < (max(2 * sFormattedSize.GetLength(), 18) + 5); i++) {
		secondColPadding += _T(' ');
	};

	CString sFileSize = _T("");
	if (!CurrentImage()->IsClipboardImage() && pFileList->Current() != NULL) {
		HANDLE hFile = ::CreateFile(pFileList->Current(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			__int64 fileSize = 0;
			::GetFileSizeEx(hFile, (PLARGE_INTEGER)&fileSize);
			::CloseHandle(hFile);
			if (fileSize > 0) {
				const TCHAR* units[] = { _T("Bytes"), _T("KiB"), _T("MiB"), _T("GiB") };
				double value = fileSize;
				int exponent = 0;
				while (value >= 1024 && exponent < sizeof(units) / sizeof(units[0]) - 1) {
					value /= 1024.0;
					exponent++;
				}
				sFileSize.Format(_T("%s%.1f %s"), secondColPadding, value, units[exponent]);
			}
		}
		m_pEXIFDisplay->AddLine(NULL, sFileSize, false, true);
	}



	if (pEXIFReader != NULL) {
		sComment = pEXIFReader->GetUserComment();
		if (sComment == NULL || sComment[0] == 0 || ((std::wstring)sComment).find_first_not_of(L" \t\n\r\f\v", 0) == std::wstring::npos) {
			sComment = pEXIFReader->GetImageDescription();
		}
		if (pEXIFReader->GetISOSpeedPresent()) {
			CString sFormattedIso;
			sFormattedIso.Format(_T("ISO %d"), (int)pEXIFReader->GetISOSpeed());
			m_pEXIFDisplay->AddLine(NULL, sFormattedIso);
		}
		if (pEXIFReader->GetExposureTimePresent()) {
			Rational exposure = pEXIFReader->GetExposureTime();
			CString sFormattedExposureTime;
			sFormattedExposureTime.Format(_T("%s%s sec"), secondColPadding, FormatRational(exposure));
			bool bExposureTimeSameLine = pEXIFReader->GetISOSpeedPresent();
			m_pEXIFDisplay->AddLine(NULL, sFormattedExposureTime, false, bExposureTimeSameLine);
		}
		if (pEXIFReader->GetFocalLengthPresent()) {
			CString sFormattedFocalLength;
			sFormattedFocalLength.Format(_T("%g mm"), pEXIFReader->GetFocalLength());
			m_pEXIFDisplay->AddLine(NULL, sFormattedFocalLength);
		}
		if (pEXIFReader->GetFNumberPresent()) {
			CString sFormattedFNumber;
			sFormattedFNumber.Format(_T("%s𝑓/%g"), secondColPadding, pEXIFReader->GetFNumber());
			bool bFNumberSameLine = pEXIFReader->GetFocalLengthPresent();
			m_pEXIFDisplay->AddLine(NULL, sFormattedFNumber, false, bFNumberSameLine);
		}
		if (pEXIFReader->GetExposureBiasPresent() && (pEXIFReader->GetExposureBias() < -0.00001 || pEXIFReader->GetExposureBias() > 0.00001)) {
			CString sFormattedExposureBias;
			sFormattedExposureBias.Format(_T("%+g EV"), pEXIFReader->GetExposureBias());
			m_pEXIFDisplay->AddLine(NULL, sFormattedExposureBias);
		}
		if (pEXIFReader->GetFlashFiredPresent() && pEXIFReader->GetFlashFired()) {
			bool bflashSameLine = pEXIFReader->GetExposureBiasPresent() && (pEXIFReader->GetExposureBias() < -0.00001 || pEXIFReader->GetExposureBias() > 0.00001);
			m_pEXIFDisplay->AddLine(NULL, pEXIFReader->GetFlashFired() ? CNLS::GetString(_T("Flash fired")) : CNLS::GetString(_T("No flash")), false, bflashSameLine);
		}
		if (pEXIFReader->GetCameraModelPresent()) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Camera:")), pEXIFReader->GetCameraModel());
		}
		if (pEXIFReader->GetLensModelPresent()) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Lens:")), pEXIFReader->GetLensModel());
		}
		if (pEXIFReader->GetSoftwarePresent()) {
			CString sFormattedSoftware = pEXIFReader->GetSoftware();
			if (sFormattedSoftware == _T("Adobe Photoshop Lightroom 6.0 (Windows)")) {
				sFormattedSoftware = _T("Lightroom 6");
			}
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Software:")), sFormattedSoftware);
		}
	}
	else if (pRawMetaData != NULL) {
		if (pRawMetaData->GetIsoSpeed() > 0.0) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("ISO Speed:")), (int)pRawMetaData->GetIsoSpeed());
		}
		if (pRawMetaData->GetExposureTime() > 0.0) {
			double exposureTime = pRawMetaData->GetExposureTime();
			Rational exposureRational = (exposureTime < 1.0) ? Rational(1, Helpers::RoundToInt(1.0 / exposureTime)) : Rational(Helpers::RoundToInt(exposureTime), 1);
			CString sFormattedExposureTime;
			sFormattedExposureTime.Format(_T("%s%s sec"), secondColPadding, FormatRational(exposureRational));
			bool bExposureTimeSameLine = pRawMetaData->GetIsoSpeed() > 0.0;
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exposure time:")), sFormattedExposureTime, false, bExposureTimeSameLine);
		}
		if (pRawMetaData->GetFocalLength() > 0.0) {
			CString sFormattedFocalLength;
			sFormattedFocalLength.Format(_T("%g mm"), pRawMetaData->GetFocalLength());
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Focal length:")), sFormattedFocalLength);
		}
		if (pRawMetaData->GetAperture() > 0.0) {
			CString sFormattedFNumber;
			sFormattedFNumber.Format(_T("%s𝑓/%g"), secondColPadding, pRawMetaData->GetAperture());
			bool bApertureSameLine = pRawMetaData->GetFocalLength() > 0.0;
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Aperture:")), sFormattedFNumber, false, bApertureSameLine);
		}
		if (pRawMetaData->GetManufacturer()[0] != 0) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Camera model:")), CString(pRawMetaData->GetManufacturer()) + _T(" ") + pRawMetaData->GetModel());
		}
		if (pRawMetaData->IsFlashFired()) {
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Flash fired:")), CNLS::GetString(_T("yes")));
		}
	}

	if (CurrentImage()->IsAnimation()) {
		if (CurrentImage()->NumberOfFrames() != NULL) {
			CString formattedFrameInfo;
			CString padding = "";
			if (CurrentImage()->NumberOfFrames() >= 10 && CurrentImage()->FrameIndex() < 10) {
				padding = "  ";
			}
			formattedFrameInfo.Format(_T("Frame %s%d of %d (%d ms)"), padding, CurrentImage()->FrameIndex(), CurrentImage() ->NumberOfFrames(), CurrentImage()->FrameTimeMs());
			m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Animation:")), formattedFrameInfo);
		}
	}

	if (sComment == NULL || sComment[0] == 0 || ((std::wstring)sComment).find_first_not_of(L" \t\n\r\f\v", 0) == std::wstring::npos) {
		sComment = CurrentImage()->GetJPEGComment();
	}
	if (CSettingsProvider::This().ShowJPEGComments() && sComment != NULL && sComment[0] != 0) {
		m_pEXIFDisplay->SetComment(sComment);
	}
}

