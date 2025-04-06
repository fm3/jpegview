#include "StdAfx.h"
#include "JPEGProvider.h"
#include "JPEGImage.h"
#include "ImageLoadThread.h"
#include "MessageDef.h"
#include "FileList.h"
#include "ProcessParams.h"
#include "BasicProcessing.h"

#include <chrono>

int READ_AHEAD_RANGE_IN_DIRECTION = 2;
int READ_AHEAD_RANGE_OPPOSITE_DIRECTION = 1;
int READ_AHEAD_RANGE_KEEP_EXTRA = 4;

CJPEGProvider::CJPEGProvider(HWND handlerWnd, int nNumThreads, int nNumBuffers) {
	m_hHandlerWnd = handlerWnd;
	m_nNumThread = nNumThreads;
	m_nNumBuffers = nNumBuffers;
	m_nCurrentTimeStamp = 0;
	m_eOldDirection = FORWARD;
	m_pWorkThreads = new CImageLoadThread*[nNumThreads];
	for (int i = 0; i < nNumThreads; i++) {
		m_pWorkThreads[i] = new CImageLoadThread();
	}
}

CJPEGProvider::~CJPEGProvider(void) {
	for (int i = 0; i < m_nNumThread; i++) {
		delete m_pWorkThreads[i];
	}
	delete[] m_pWorkThreads;
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		delete (*iter)->Image;
		delete *iter;
	}
}

CJPEGImage* CJPEGProvider::RequestImage(CFileList* pFileList, EReadAheadDirection eDirection,
                                        LPCTSTR strFileName, int nFrameIndex, const CProcessParams & processParams, COLORREF colorTransparency,
                                        bool& bOutOfMemory, bool& bExceptionError) {

	if (strFileName == NULL) {
		bOutOfMemory = false;
		bExceptionError = false;
		return NULL;
	}

	// Search if we have the requested image already present or in progress
	CImageRequest* pRequest = FindRequest(strFileName, nFrameIndex);
	bool bDirectionChanged = eDirection != m_eOldDirection || eDirection == TOGGLE;
	bool bRemoveAlsoActiveRequests = bDirectionChanged; // if direction changed, all read-ahead requests are wrongly guessed
	bool bWasOutOfMemory = false;
	m_eOldDirection = eDirection;

	if (pRequest == NULL) {
		::OutputDebugString(_T("RequestImage cache miss for ")); ::OutputDebugString(strFileName); ::OutputDebugString(_T("\n"));
		// no request pending for this file, add to request queue and start async
		pRequest = StartNewRequest(strFileName, nFrameIndex, processParams, colorTransparency);
		
		/*
		// wait with read ahead when direction changed - maybe user just wants to re-see last image
		if (!bDirectionChanged && eDirection != NONE) {
			// start parallel if more than one thread
			StartNewRequestBundle(pFileList, eDirection, processParams, colorTransparency, 3, NULL);
		}
		*/
	}

	// wait for request if not yet ready
	if (!pRequest->Ready) {
#ifdef DEBUG
		::OutputDebugString(_T("Waiting for request: ")); ::OutputDebugString(pRequest->FileName); ::OutputDebugString(_T("\n"));
#endif
		::WaitForSingleObject(pRequest->EventFinished, INFINITE);
		GetLoadedImageFromWorkThread(pRequest);
	} else {
		CJPEGImage* pImage = pRequest->Image;
		if (pImage != NULL) {
			// make sure the initial parameters are reset as when keep params was on before they are wrong
			EProcessingFlags procFlags = processParams.ProcFlags;
			pImage->RestoreInitialParameters(strFileName, processParams.ImageProcParams, procFlags, 
				processParams.RotationParams.Rotation, processParams.Zoom, processParams.Offsets, 
				CSize(processParams.TargetWidth, processParams.TargetHeight), processParams.MonitorSize);
		}
#ifdef DEBUG
		::OutputDebugString(_T("Found in cache: ")); ::OutputDebugString(pRequest->FileName); ::OutputDebugString(_T("\n"));
#endif
	}

	// set before removing unused images!
	pRequest->InUse = true;
	pRequest->AccessTimeStamp = m_nCurrentTimeStamp++;

	if (pRequest->OutOfMemory) {
		// The request could not be satisfied because the system is out of memory.
		// Clear all memory and try again - maybe some readahead requests can be deleted
#ifdef DEBUG
		::OutputDebugString(_T("Retrying request because out of memory: ")); ::OutputDebugString(pRequest->FileName); ::OutputDebugString(_T("\n"));
#endif
		bWasOutOfMemory = true;
		if (FreeAllPossibleMemory()) {
			DeleteElement(pRequest);
			pRequest = StartRequestAndWaitUntilReady(strFileName, nFrameIndex, processParams, colorTransparency);
		}
	}

	// cleanup stuff no longer used
	RemoveUnusedImages(pFileList, eDirection, false, pRequest);
	MarkOldestRequestsAsInactive();

	// check if we shall start new requests (don't start another request if we are short of memory!)
	if (!bWasOutOfMemory) {
		StartNewPreloadRequestBundle(pFileList, eDirection, processParams, colorTransparency, pRequest);
	}

	bOutOfMemory = pRequest->OutOfMemory;
	bExceptionError = pRequest->ExceptionError;

	return pRequest->Image;
}

void CJPEGProvider::NotifyNotUsed(CJPEGImage* pImage) {
	// mark image as unused but do not remove yet from request queue
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		if ((*iter)->Image == pImage) {
			(*iter)->InUse = false;
			(*iter)->IsActive = false;
			return;
		}
	}
	// image not found in request queue - delete it as it is no longer used and will not be cached
	if (pImage != NULL) delete pImage;
}

void CJPEGProvider::ClearAllRequests() {
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		if (ClearRequest((*iter)->Image)) {
			// removed from iteration, restart iteration to remove the rest
			ClearAllRequests();
			break;
		}
	}
}

bool CJPEGProvider::FreeAllPossibleMemory() {
	bool bCouldFreeMemory = false;
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		CImageRequest* pRequest = *iter;
		if (!pRequest->InUse && pRequest->Ready) {
			DeleteElementAt(iter);
			FreeAllPossibleMemory();
			bCouldFreeMemory = true;
			break;
		}
	}
	return bCouldFreeMemory;
}

void CJPEGProvider::FileHasRenamed(LPCTSTR sOldFileName, LPCTSTR sNewFileName) {
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		if (_tcsicmp(sOldFileName, (*iter)->FileName) == 0) {
			(*iter)->FileName = sNewFileName;
		}
	}
}

bool CJPEGProvider::ClearRequest(CJPEGImage* pImage, bool releaseLockedFile) {
	if (pImage == NULL) {
		return false;
	}
	bool bErased = false;
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		if ((*iter)->Image == pImage) {
			if (releaseLockedFile) m_pWorkThreads[0]->ReleaseFile((*iter)->FileName);
			// images that are not ready cannot be removed yet
			if ((*iter)->Ready) {
				DeleteElementAt(iter);
				bErased = true;
			} else {
				(*iter)->Deleted = true;
			}

			break;
		}
	}
	return bErased;
}

void CJPEGProvider::OnImageLoadCompleted(int nHandle) {
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		if ((*iter)->Handle == nHandle) {
			GetLoadedImageFromWorkThread(*iter);
			if ((*iter)->Deleted) {
				// this request was deleted, delete image now
				ClearRequest((*iter)->Image);
			}
			break;
		}
	}
}

CJPEGProvider::CImageRequest* CJPEGProvider::FindRequest(LPCTSTR strFileName, int nFrameIndex) {
	std::list<CImageRequest*>::iterator iter;
	for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
		if (_tcsicmp((*iter)->FileName, strFileName) == 0 && (*iter)->FrameIndex == nFrameIndex && !(*iter)->Deleted) {
			return *iter;
		}
	}
	return NULL;
}

CJPEGProvider::CImageRequest* CJPEGProvider::StartRequestAndWaitUntilReady(LPCTSTR sFileName, int nFrameIndex, const CProcessParams & processParams, COLORREF colorTransparency) {
	CImageRequest* pRequest = StartNewRequest(sFileName, nFrameIndex, processParams, colorTransparency);
	::WaitForSingleObject(pRequest->EventFinished, INFINITE);
	GetLoadedImageFromWorkThread(pRequest);
	return pRequest;
}

std::list<std::tuple<LPCTSTR, int>> CJPEGProvider::GetReadAheadFileList(CFileList* pFileList, EReadAheadDirection eDirection, CImageRequest* pLastReadyRequest, int extraPerDirection) {
	// TODO priority
	std::list<std::tuple<LPCTSTR, int>> filesWithFrameIndex{};
	for (int i = 0; i < READ_AHEAD_RANGE_IN_DIRECTION + extraPerDirection; i++) {
		bool bSwitchImage = true;
		int nFrameIndex = (pLastReadyRequest != NULL) ? Helpers::GetFrameIndex(pLastReadyRequest->Image, eDirection == FORWARD, true, bSwitchImage) : 0;
		LPCTSTR sFileName = bSwitchImage ? pFileList->PeekNextPrev(i + 1, eDirection == FORWARD, eDirection == TOGGLE) : pFileList->Current();
		if (sFileName != NULL) {
			filesWithFrameIndex.push_back({ sFileName, nFrameIndex });
		}
	}
	for (int i = 0; i < READ_AHEAD_RANGE_OPPOSITE_DIRECTION + extraPerDirection; i++) {
		bool bSwitchImage = true;
		int nFrameIndex = (pLastReadyRequest != NULL) ? Helpers::GetFrameIndex(pLastReadyRequest->Image, eDirection == BACKWARD, true, bSwitchImage) : 0;
		LPCTSTR sFileName = bSwitchImage ? pFileList->PeekNextPrev(i + 1, eDirection == BACKWARD, eDirection == TOGGLE) : pFileList->Current();
		if (sFileName != NULL) {
			filesWithFrameIndex.push_back({ sFileName, nFrameIndex });
		}
	}
	return filesWithFrameIndex;
}

void CJPEGProvider::StartNewPreloadRequestBundle(CFileList* pFileList, EReadAheadDirection eDirection, const CProcessParams & processParams, COLORREF colorTransparency, void* pLastReadyRequestRaw) {
	if (pFileList == NULL) {
		return;
	}
	CImageRequest* pLastReadyRequest = reinterpret_cast<CImageRequest*>(pLastReadyRequestRaw);
	
	::OutputDebugString(_T("StartNewRequestBundle\n"));

	std::list<std::tuple<LPCTSTR, int>> filesWithFrameIndex = GetReadAheadFileList(pFileList, eDirection, pLastReadyRequest);

	for (auto [sFileName, nFrameIndex] : filesWithFrameIndex) {
		if (FindRequest(sFileName, nFrameIndex) == NULL) {
			if (GetProcessingFlag(PFLAG_NoProcessingAfterLoad, processParams.ProcFlags)) {
				// The read ahead threads need this flag to be deleted - we can speculatively process the image with good hit rate
				CProcessParams paramsCopied = processParams;
				paramsCopied.ProcFlags = SetProcessingFlag(paramsCopied.ProcFlags, PFLAG_NoProcessingAfterLoad, false);
				StartNewRequest(sFileName, nFrameIndex, paramsCopied, colorTransparency);
			} else {
				StartNewRequest(sFileName, nFrameIndex, processParams, colorTransparency);
			}
		}
	}
}

CJPEGProvider::CImageRequest* CJPEGProvider::StartNewRequest(LPCTSTR sFileName, int nFrameIndex, const CProcessParams & processParams, COLORREF colorTransparency) {
	CImageRequest* pRequest = new CImageRequest(sFileName, nFrameIndex);
	::OutputDebugString(_T("StartNewRequest: ")); ::OutputDebugString(sFileName); ::OutputDebugString(_T("\n"));
	m_requestList.push_back(pRequest);
	pRequest->HandlingThread = SearchThreadForNewRequest();
	pRequest->Handle = pRequest->HandlingThread->AsyncLoad(pRequest->FileName, nFrameIndex,
		processParams, colorTransparency, m_hHandlerWnd, pRequest->EventFinished);
	return pRequest;
}

void CJPEGProvider::GetLoadedImageFromWorkThread(CImageRequest* pRequest) {
	if (pRequest->HandlingThread != NULL) {
#ifdef DEBUG
		::OutputDebugString(_T("Finished request: ")); ::OutputDebugString(pRequest->FileName); ::OutputDebugString(_T("\n"));
#endif
		CImageData imageData = pRequest->HandlingThread->GetLoadedImage(pRequest->Handle);
		pRequest->Image = imageData.Image;
		pRequest->OutOfMemory = imageData.IsRequestFailedOutOfMemory;
		pRequest->ExceptionError = imageData.IsRequestFailedException;
		pRequest->Ready = true;
		pRequest->HandlingThread = NULL;
	}
}

CImageLoadThread* CJPEGProvider::SearchThreadForNewRequest(void) {
	int nSmallestHandle = INT_MAX;
	CImageLoadThread* pBestOccupiedThread = NULL;
	for (int i = 0; i < m_nNumThread; i++) {
		bool bFree = true;
		CImageLoadThread* pThisThread = m_pWorkThreads[i];
		std::list<CImageRequest*>::iterator iter;
		for (iter = m_requestList.begin( ); iter != m_requestList.end( ); iter++ ) {
			if ((*iter)->Handle < nSmallestHandle && (*iter)->HandlingThread != NULL) {
				nSmallestHandle = (*iter)->Handle;
				pBestOccupiedThread = (*iter)->HandlingThread;
			}
			if ((*iter)->HandlingThread == pThisThread) {
				bFree = false;
				break;
			}
		}
		if (bFree) {
			return pThisThread;
		}
	}
	// all threads are occupied, return thread working on smallest handle (will finish earliest)
	return (pBestOccupiedThread == NULL) ? m_pWorkThreads[0] : pBestOccupiedThread;
}


void CJPEGProvider::RemoveUnusedImages(CFileList* pFileList, EReadAheadDirection eDirection, bool removeAll, void* pLastReadyRequestRaw) {
	::OutputDebugString(_T("RemoveUnused\n"));
	CImageRequest* pLastReadyRequest = reinterpret_cast<CImageRequest*>(pLastReadyRequestRaw);
	int keepExtraPerDirection = 4;
	std::list<std::tuple<LPCTSTR, int>> readAheadRange = GetReadAheadFileList(pFileList, eDirection, reinterpret_cast<CImageRequest*>(pLastReadyRequest), keepExtraPerDirection);
	std::list<CImageRequest*>::iterator iter;
	auto compare = [](std::tuple<LPCTSTR, int> a, std::tuple<LPCTSTR, int> b) { return lstrcmp(std::get<0>(a), std::get<0>(b)) && std::get<1>(a) == std::get<1>(b); };
	bool bRemoved = false;
	do { // Wrapped in a loop because we have to break iterating when deleting element, as it breaks the iterator.
		bRemoved = false;
		for (iter = m_requestList.begin(); iter != m_requestList.end(); iter++) {
			bool isInReadAheadRange = false;
			for (auto [fileName, frameIndex] : readAheadRange) {
				if ((*iter)->FileName == fileName && (*iter)->FrameIndex == frameIndex) {
					isInReadAheadRange = true;
					break;
				}
			}
			bool isCurrentImage = pLastReadyRequest != NULL && ((*iter)->FileName == pLastReadyRequest->FileName && (*iter)->FrameIndex == pLastReadyRequest->FrameIndex);
			if (!isCurrentImage && !(*iter)->InUse) {
				if (removeAll || (!isInReadAheadRange && !isCurrentImage && !(*iter)->IsActive)) {
					::OutputDebugString(_T("Deleting from cache: ")); ::OutputDebugString((*iter)->FileName); ::OutputDebugString(_T("\n"));
					DeleteElementAt(iter);
					bRemoved = true;
					break;
				}
			}
		}
	} while (bRemoved); // repeat until no element was removed anymore
}

void CJPEGProvider::MarkOldestRequestsAsInactive() {
	if (m_requestList.size() >= (unsigned int)m_nNumBuffers) {
		int nFirstHandle = INT_MAX;
		CImageRequest* pFirstRequest = NULL;
		std::list<CImageRequest*>::iterator iter;
		for (iter = m_requestList.begin(); iter != m_requestList.end(); iter++) {
			if ((*iter)->IsActive) {
				// mark very old requests for removal
				if (CImageLoadThread::GetCurHandleValue() - (*iter)->Handle > m_nNumBuffers) {
					(*iter)->IsActive = false;
				}
				if ((*iter)->Handle < nFirstHandle) {
					nFirstHandle = (*iter)->Handle;
					pFirstRequest = *iter;
				}
			}
		}
		if (pFirstRequest != NULL) {
			pFirstRequest->IsActive = false;
			MarkOldestRequestsAsInactive();
		}
	}
}

void CJPEGProvider::DeleteElementAt(std::list<CImageRequest*>::iterator iteratorAt) {
	delete (*iteratorAt)->Image;
	delete *iteratorAt;
	m_requestList.erase(iteratorAt);
}

void CJPEGProvider::DeleteElement(CImageRequest* pRequest) {
	delete pRequest->Image;
	delete pRequest;
	m_requestList.remove(pRequest);
}

bool CJPEGProvider::IsDestructivelyProcessed(CJPEGImage* pImage) {
	return pImage != NULL && pImage->IsDestructivelyProcessed();
}