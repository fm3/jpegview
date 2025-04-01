#pragma once

#include "PanelController.h"
#include "EXIFReader.h"

class CEXIFDisplay;

// Implements functionality of the EXIF display panel
class CEXIFDisplayCtl : public CPanelController
{
public:
	CEXIFDisplayCtl(CMainDlg* pMainDlg, CPanel* pImageProcPanel);
	virtual ~CEXIFDisplayCtl();

	virtual float DimFactor() { return 0.5f; }

	virtual bool IsVisible();
	virtual bool IsActive() { return m_bVisible; }
	virtual bool GetShowHistogram() { return m_bShowHistogram; }

	virtual void SetVisible(bool bVisible);
	virtual void SetActive(bool bActive);
	virtual void SetShowHistogram(bool bShowHistogram);

	virtual void AfterNewImageLoaded();

	virtual bool OnMouseMove(int nX, int nY);
	virtual void OnPrePaintMainDlg(HDC hPaintDC);

	virtual CString CEXIFDisplayCtl::FormatRational(Rational rational);

private:
	bool m_bVisible;
	bool m_bShowHistogram;
	CEXIFDisplay* m_pEXIFDisplay;
	CPanel* m_pImageProcPanel;
	int m_nFileNameHeight;

	void FillEXIFDataDisplay();
	static void OnShowHistogram(void* pContext, int nParameter, CButtonCtrl & sender);
	static void OnClose(void* pContext, int nParameter, CButtonCtrl & sender);
};