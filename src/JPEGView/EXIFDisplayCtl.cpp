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
	m_pEXIFDisplay->GetControl<CButtonCtrl*>(CEXIFDisplay::ID_btnShowHideHistogram)->SetButtonPressedHandler(&OnShowHistogram, this);
	CButtonCtrl* pCloseBtn = m_pEXIFDisplay->GetControl<CButtonCtrl*>(CEXIFDisplay::ID_btnClose);
	pCloseBtn->SetButtonPressedHandler(&OnClose, this);
	pCloseBtn->SetShow(false);
	m_pEXIFDisplay->SetShowHistogram(CSettingsProvider::This().ShowHistogram());
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

	CString sFormattedSize;
	sFormattedSize.Format(_T("%d × %d"), CurrentImage()->OrigWidth(), CurrentImage()->OrigHeight());
	m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Size:")), sFormattedSize);

	CString secondColPadding = _T("");
	if (!m_pEXIFDisplay->GetShowHistogram()) {
		for (int i = 0; i < (max(2 * sFormattedSize.GetLength(), 18) + 3); i++) {
			secondColPadding += _T(' ');
		};
	}

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
		m_pEXIFDisplay->AddLine(CNLS::GetString(_T("File size:")), sFileSize, false, true);
	}

	bool bShowMoreDetails = m_pEXIFDisplay->GetShowHistogram();
	if (!CurrentImage()->IsClipboardImage()) {
		CEXIFReader* pEXIFReader = CurrentImage()->GetEXIFReader();
		CRawMetadata* pRawMetaData = CurrentImage()->GetRawMetadata();
		if (pEXIFReader != NULL) {
			sComment = pEXIFReader->GetUserComment();
			if (sComment == NULL || sComment[0] == 0 || ((std::wstring) sComment).find_first_not_of(L" \t\n\r\f\v", 0) == std::wstring::npos) {
				sComment = pEXIFReader->GetImageDescription();
			}
			if (pEXIFReader->GetAcquisitionTimePresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Acquired:")), pEXIFReader->GetAcquisitionTime());
			} else if (pEXIFReader->GetDateTimePresent()) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exif date:")), pEXIFReader->GetDateTime());
			} else {
				const FILETIME* pFileTime = pFileList->CurrentModificationTime();
				if (pFileTime != NULL) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modified:")), *pFileTime);
				}
			}

			if (pEXIFReader->GetISOSpeedPresent()) {
				CString sFormattedIso;
				sFormattedIso.Format(_T("ISO %d"), (int)pEXIFReader->GetISOSpeed());
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("ISO speed:")), sFormattedIso);
			}
			if (pEXIFReader->GetExposureTimePresent()) {
				Rational exposure = pEXIFReader->GetExposureTime();
				CString sFormattedExposureTime;
				sFormattedExposureTime.Format(_T("%s%s sec"), secondColPadding, FormatRational(exposure));
				bool bExposureTimeSameLine = pEXIFReader->GetISOSpeedPresent();
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exposure time:")), sFormattedExposureTime, false, bExposureTimeSameLine);
			}
			if (pEXIFReader->GetFocalLengthPresent()) {
				CString sFormattedFocalLength;
				sFormattedFocalLength.Format(_T("%g mm"), pEXIFReader->GetFocalLength());
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Focal length:")), sFormattedFocalLength);
			}
			if (pEXIFReader->GetFNumberPresent()) {
				CString sFormattedFNumber;
				sFormattedFNumber.Format(_T("%s𝑓/%g"), secondColPadding, pEXIFReader->GetFNumber());
				bool bFNumberSameLine = pEXIFReader->GetFocalLengthPresent();
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Aperture:")), sFormattedFNumber, false, bFNumberSameLine);
			}
			if (pEXIFReader->IsGPSInformationPresent()) {
				CString sGPSLocation = CreateGPSString(pEXIFReader->GetGPSLatitude(), pEXIFReader->GetGPSLongitude());
				m_pEXIFDisplay->SetGPSLocation(sGPSLocation, CreateGPSURL(pEXIFReader->GetGPSLatitude(), pEXIFReader->GetGPSLongitude()));
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Location:")), sGPSLocation, true);
			}
			if (bShowMoreDetails) {
				if (pEXIFReader->IsGPSAltitudePresent()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Altitude (m):")), pEXIFReader->GetGPSAltitude(), 0);
				}
				if (pEXIFReader->GetExposureBiasPresent() && (pEXIFReader->GetExposureBias() < -0.00001 || pEXIFReader->GetExposureBias() > 0.00001)) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Exposure bias (EV):")), pEXIFReader->GetExposureBias(), 2);
				}
				if (pEXIFReader->GetFlashFiredPresent()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Flash fired:")), pEXIFReader->GetFlashFired() ? CNLS::GetString(_T("yes")) : CNLS::GetString(_T("no")));
				}
				if (pEXIFReader->GetCameraModelPresent()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Camera:")), pEXIFReader->GetCameraModel());
				}
				if (pEXIFReader->GetSoftwarePresent()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Software:")), pEXIFReader->GetSoftware());
				}
			}
		}
		else if (pRawMetaData != NULL) {
			if (pRawMetaData->GetAcquisitionTime().wYear > 1985) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Acquired:")), pRawMetaData->GetAcquisitionTime());
			}
			else {
				const FILETIME* pFileTime = pFileList->CurrentModificationTime();
				if (pFileTime != NULL) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modified:")), *pFileTime);
				}
			}
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
			if (pRawMetaData->IsGPSInformationPresent()) {
				CString sGPSLocation = CreateGPSString(pRawMetaData->GetGPSLatitude(), pRawMetaData->GetGPSLongitude());
				m_pEXIFDisplay->SetGPSLocation(sGPSLocation, CreateGPSURL(pRawMetaData->GetGPSLatitude(), pRawMetaData->GetGPSLongitude()));
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Location:")), sGPSLocation, true);
			}

			if (bShowMoreDetails) {
				if (pRawMetaData->IsGPSAltitudePresent()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Altitude (m):")), pRawMetaData->GetGPSAltitude(), 0);
				}
				if (pRawMetaData->GetManufacturer()[0] != 0) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Camera model:")), CString(pRawMetaData->GetManufacturer()) + _T(" ") + pRawMetaData->GetModel());
				}
				if (pRawMetaData->IsFlashFired()) {
					m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Flash fired:")), CNLS::GetString(_T("yes")));
				}
			}
		}
		else {
			const FILETIME* pFileTime = pFileList->CurrentModificationTime();
			if (pFileTime != NULL) {
				m_pEXIFDisplay->AddLine(CNLS::GetString(_T("Modification date:")), *pFileTime);
			}
		}
	}

	if (sComment == NULL || sComment[0] == 0 || ((std::wstring)sComment).find_first_not_of(L" \t\n\r\f\v", 0) == std::wstring::npos) {
		sComment = CurrentImage()->GetJPEGComment();
	}
	if (CSettingsProvider::This().ShowJPEGComments() && sComment != NULL && sComment[0] != 0) {
		m_pEXIFDisplay->SetComment(sComment);
	}
}

bool CEXIFDisplayCtl::OnMouseMove(int nX, int nY) {
	bool bHandled = CPanelController::OnMouseMove(nX, nY);
	bool bMouseOver = m_pEXIFDisplay->PanelRect().PtInRect(CPoint(nX, nY));
	m_pEXIFDisplay->GetControl<CButtonCtrl*>(CEXIFDisplay::ID_btnClose)->SetShow(bMouseOver);
	return bHandled;
}

void CEXIFDisplayCtl::OnShowHistogram(void* pContext, int nParameter, CButtonCtrl & sender) {
	CEXIFDisplayCtl* pThis = (CEXIFDisplayCtl*)pContext;
	pThis->m_pEXIFDisplay->SetShowHistogram(!pThis->m_pEXIFDisplay->GetShowHistogram());
	pThis->m_pEXIFDisplay->RequestRepositioning();
	pThis->InvalidateMainDlg();
}

void CEXIFDisplayCtl::OnClose(void* pContext, int nParameter, CButtonCtrl & sender) {
	CEXIFDisplayCtl* pThis = (CEXIFDisplayCtl*)pContext;
	pThis->m_pMainDlg->ExecuteCommand(IDM_SHOW_FILEINFO);
}