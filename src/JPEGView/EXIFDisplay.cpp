#include "StdAfx.h"
#include "EXIFDisplay.h"
#include "Helpers.h"
#include "HelpersGUI.h"
#include "NLS.h"
#include <math.h>

#define MAX_WIDTH 320
#define PREFIX_GAP 10

static LPTSTR CopyStrAlloc(LPCTSTR str) {
	if (str == NULL) {
		return NULL;
	}
	int nLen = (int)_tcslen(str);
	TCHAR* pNewStr = new TCHAR[nLen + 1];
	_tcscpy_s(pNewStr, nLen + 1, str);
	return pNewStr;
}

CEXIFDisplay::CEXIFDisplay(HWND hWnd, INotifiyMouseCapture* pNotifyMouseCapture) : CPanel(hWnd, pNotifyMouseCapture, false, true) {
	m_nGap = (int)(m_fDPIScale * 10);
	m_nTab1 = 0;
	m_nLineHeight = 0;
	m_nTitleHeight = 0;
	m_pos = CPoint(0, 0);
	m_size = CSize(0, 0);
	m_sPrefix = NULL;
	m_sTitle = NULL;
	m_sComment = NULL;
	m_nCommentHeight = 0;
	m_hTitleFont = 0;

	CURLCtrl* pLinkLocation = AddURL(ID_urlLocation, _T(""), _T(""), false);
	pLinkLocation->SetShow(false, false);
}

CEXIFDisplay::~CEXIFDisplay() {
	ClearTexts();
	if (m_hTitleFont != 0) {
		::DeleteObject(m_hTitleFont);
	}
}

void CEXIFDisplay::ClearTexts() {
	delete[] m_sPrefix;
	m_sPrefix = NULL;
	delete[] m_sTitle;
	m_sTitle = NULL;
	delete[] m_sComment;
	m_sComment = NULL;
	m_nCommentHeight = 0;
	m_nPrefixLength = 0;
	m_nTitleWidth = 0;
	std::list<TextLine>::iterator iter;
	for (iter = m_lines.begin( ); iter != m_lines.end( ); iter++ ) {
		delete[] iter->Desc;
		delete[] iter->Value;
	}
	m_lines.clear();

	CUICtrl* pLinkLocation = GetControl(CEXIFDisplay::ID_urlLocation);
	pLinkLocation->SetShow(false, false);

	RequestRepositioning();
}

void CEXIFDisplay::AddTitle(LPCTSTR sTitle) {
	delete[] m_sTitle;
	m_sTitle = CopyStrAlloc(sTitle);
}

void CEXIFDisplay::AddPrefix(LPCTSTR sPrefix) {
	delete[] m_sPrefix;
	m_sPrefix = CopyStrAlloc(sPrefix);
}

void CEXIFDisplay::SetComment(LPCTSTR sComment) {
	delete[] m_sComment;
	CString s(sComment);
	s.TrimLeft();
	s.TrimRight();
	m_sComment = (s.GetLength() == 0) ? NULL : CopyStrAlloc(s);
}

void CEXIFDisplay::SetGPSLocation(LPCTSTR sLocation, LPCTSTR sURL) {
	CURLCtrl* pLinkLocation = GetControl<CURLCtrl*>(CEXIFDisplay::ID_urlLocation);
	pLinkLocation->SetText(sLocation);
	pLinkLocation->SetURL(sURL);
	pLinkLocation->SetShow(true, false);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, LPCTSTR sValue, bool valueIsURL, bool sameLine) {
	m_lines.push_back(TextLine(CopyStrAlloc(sDescription), CopyStrAlloc(sValue), valueIsURL, sameLine));
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, double dValue, int nDigits) {
	TCHAR buffFormat[8];
	_stprintf_s(buffFormat, 8, _T("%%.%df"), nDigits);
	TCHAR buff[32];
	_stprintf_s(buff, 32, buffFormat, dValue);
	AddLine(sDescription, buff);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, int nValue){
	TCHAR buff[32];
	_stprintf_s(buff, 32, _T("%d"), nValue);
	AddLine(sDescription, buff);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, const SYSTEMTIME &time) {
	CString sTime = Helpers::SystemTimeToString(time);
	AddLine(sDescription, sTime);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, const FILETIME &time) {
	SYSTEMTIME systemTime;
	::FileTimeToSystemTime(&time, &systemTime);
	TIME_ZONE_INFORMATION tzi;
	::GetTimeZoneInformation(&tzi);
	::SystemTimeToTzSpecificLocalTime(&tzi, &systemTime, &systemTime);
	AddLine(sDescription, systemTime);
}

void CEXIFDisplay::AddLine(LPCTSTR sDescription, const Rational &number) {
	if (number.Denominator == 1) {
		AddLine(sDescription, number.Numerator);
	} else if (number.Numerator > 9) {
		TCHAR buff[32];
		if (number.Numerator * 3 < number.Denominator) {
			_stprintf_s(buff, 32, _T("1/%d"), number.Denominator / number.Numerator);
		} else {
			_stprintf_s(buff, 32, _T("%.2f"), double(number.Numerator) / number.Denominator);
		}
		AddLine(sDescription, buff);
	} else {
		TCHAR buff[32];
		_stprintf_s(buff, 32, _T("%d/%d"), number.Numerator, number.Denominator);
		AddLine(sDescription, buff);
	}
}

CRect CEXIFDisplay::PanelRect() {
	if (m_nLineHeight == 0) {
		CDC dc(::GetDC(m_hWnd));
		HelpersGUI::SelectDefaultGUIFont(dc);
		if (m_hTitleFont == 0)
			m_hTitleFont = HelpersGUI::CreateBoldFontOfSelectedFont(dc);

		if (m_hTitleFont != 0) {
			::SelectObject(dc, m_hTitleFont);
		}

		m_titleIsSingleLine = true;
		m_nTitleHeight = 0;
		m_nPrefixLength = 0;
		m_nTitleWidth = 0;
		int nTitleLength = 0;
		int nMaxLengthColumn1 = 0, nMaxLengthColumn2 = 0;
		int nLastLineLengthColumn2 = 0;
		CSize size;
		if (m_sPrefix != NULL) {
			::GetTextExtentPoint32(dc, m_sPrefix, (int)_tcslen(m_sPrefix), &size);
			nTitleLength = m_nPrefixLength = size.cx;
			m_nTitleHeight = size.cy + HelpersGUI::ScaleToScreen(9);
		}
		if (m_sTitle != NULL) {
			::GetTextExtentPoint32(dc, m_sTitle, (int)_tcslen(m_sTitle), &size);
			if (size.cx > HelpersGUI::ScaleToScreen(MAX_WIDTH)) {
				m_titleIsSingleLine = false;
				CRect rectTitle(0, 0, HelpersGUI::ScaleToScreen(MAX_WIDTH), HelpersGUI::ScaleToScreen(2));
				::DrawText(dc, m_sTitle, (int)_tcslen(m_sTitle), &rectTitle, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK | DT_WORD_ELLIPSIS);
				m_nTitleWidth = rectTitle.Width();
				nTitleLength += m_nTitleWidth;
				m_nTitleHeight = max(m_nTitleHeight, rectTitle.Height() + HelpersGUI::ScaleToScreen(9));
			} else {
				m_nTitleWidth = size.cx;
				nTitleLength += m_nTitleWidth;
				m_nTitleHeight = max(m_nTitleHeight, size.cy + HelpersGUI::ScaleToScreen(9));
			}
			int nGap = HelpersGUI::ScaleToScreen(PREFIX_GAP);
			m_nPrefixLength += nGap;
			nTitleLength += nGap;
		}

		HelpersGUI::SelectDefaultGUIFont(dc);

		int nLen1 = 0, nLen2 = 0;
		std::list<TextLine>::iterator iter;
		int nLineCountExcludingSameLineLines = 0;
		for (iter = m_lines.begin( ); iter != m_lines.end( ); iter++ ) {
			if (!(iter->SameLine)) {
				nLineCountExcludingSameLineLines += 1;
			}
			if (iter->Desc != NULL) {
				::GetTextExtentPoint32(dc, iter->Desc, (int)_tcslen(iter->Desc), &size);
				m_nLineHeight = max(m_nLineHeight, size.cy);
				nMaxLengthColumn1 = max(nMaxLengthColumn1, size.cx);
			}
			nLen2 = nLen1;
			nLen1 = 0;
			if (iter->Value != NULL) {
				::GetTextExtentPoint32(dc, iter->Value, (int)_tcslen(iter->Value), &size);
				m_nLineHeight = max(m_nLineHeight, size.cy);
				nMaxLengthColumn2 = max(nMaxLengthColumn2, size.cx);
				nLastLineLengthColumn2 = size.cx;
				nLen1 = size.cx;
			}
		}

		int nNeededWidthNoBorders = max(nTitleLength, nMaxLengthColumn1 + nMaxLengthColumn2 + m_nGap);
		



		m_size = CSize(nNeededWidthNoBorders + m_nGap*2, 
			m_nTitleHeight + nLineCountExcludingSameLineLines*m_nLineHeight + m_nGap * 2);

		if (m_sComment != NULL) {
			CRect rectComment(0, 0, m_size.cx - m_nGap*2, HelpersGUI::ScaleToScreen(200));
			::DrawText(dc, m_sComment, (int)_tcslen(m_sComment), &rectComment, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK | DT_WORD_ELLIPSIS);
			m_nCommentHeight = min(3 * m_nLineHeight, rectComment.Height());
			m_size.cy += (m_nGap >> 1) + m_nCommentHeight;
		}

		m_nTab1 = nMaxLengthColumn1 + m_nGap;
	}
	return CRect(m_pos, m_size);
}


void CEXIFDisplay::RequestRepositioning() {
	m_nLineHeight = 0;
}

void CEXIFDisplay::OnPaint(CDC & dc, const CPoint& offset) {
	CURLCtrl* pLinkLocation = GetControl<CURLCtrl*>(CEXIFDisplay::ID_urlLocation);
	bool isLinkLocationShown = pLinkLocation->IsShown();
	pLinkLocation->SetShow(false, false);

	CPanel::OnPaint(dc, offset);

	int nX = m_pos.x + offset.x;
	int nY = m_pos.y + offset.y;

	if (m_hTitleFont != 0) {
		::SelectObject(dc, m_hTitleFont);
	}
	::SetBkMode(dc, TRANSPARENT);
	::SetTextColor(dc, RGB(255, 255, 255));
	if (m_sPrefix != NULL) {
		::TextOut(dc, nX + m_nGap, nY + m_nGap, m_sPrefix, (int)_tcslen(m_sPrefix));
	}
	if (m_sTitle != NULL) {
		int nXStart = nX + m_nGap + m_nPrefixLength;
		if (m_titleIsSingleLine) {
			::TextOut(dc, nXStart, nY + m_nGap, m_sTitle, (int)_tcslen(m_sTitle));
		} else {
			CRect rectTitle(nXStart, nY + m_nGap, nXStart + m_nTitleWidth, nY + m_nGap + m_nTitleHeight);
			::DrawText(dc, m_sTitle, (int)_tcslen(m_sTitle), &rectTitle, DT_NOPREFIX | DT_WORDBREAK | DT_WORD_ELLIPSIS);
		}
	}

	::SetTextColor(dc, RGB(243, 242, 231));
	HelpersGUI::SelectDefaultGUIFont(dc);

	int nRunningY = nY + m_nTitleHeight + m_nGap;

	if (m_sComment != NULL) {
		CRect rectComment(nX + m_nGap, nRunningY, nX + m_size.cx - m_nGap, nRunningY + m_nCommentHeight);
		::DrawText(dc, m_sComment, (int)_tcslen(m_sComment), &rectComment, DT_NOPREFIX | DT_WORDBREAK | DT_WORD_ELLIPSIS);
		nRunningY += m_nCommentHeight;
		nRunningY += m_nGap >> 1;
	}

	std::list<TextLine>::iterator iter;
	for (iter = m_lines.begin( ); iter != m_lines.end( ); iter++ ) {
		if (iter->SameLine) {
			nRunningY -= m_nLineHeight;
		}
		if (iter->Desc != NULL) {
			::TextOut(dc, nX + m_nGap, nRunningY, iter->Desc, (int)_tcslen(iter->Desc));
		}
		if (iter->Value != NULL) {
			int xPos = nX + m_nGap;
			if (iter->Desc != NULL) {
				xPos += m_nTab1;
			}
			if (iter->ValueIsURL) {
				pLinkLocation->SetShow(isLinkLocationShown, false);
				pLinkLocation->SetPosition(CRect(CPoint(xPos - offset.x, nRunningY - offset.y), pLinkLocation->GetMinSize()));
				if (isLinkLocationShown) pLinkLocation->OnPaint(dc, offset);
				::SetTextColor(dc, RGB(243, 242, 231));
				HelpersGUI::SelectDefaultGUIFont(dc);
			} else {
				::TextOut(dc, xPos, nRunningY, iter->Value, (int)_tcslen(iter->Value));
			}
		}
		nRunningY += m_nLineHeight;
	}

}

void CEXIFDisplay::RepositionAll() {
	PanelRect();
}



