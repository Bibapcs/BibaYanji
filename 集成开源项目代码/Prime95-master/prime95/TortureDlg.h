//
// Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
//

#pragma once
#include "afxwin.h"

// CTortureDlg dialog

class CTortureDlg : public CDialog
{
	DECLARE_DYNAMIC(CTortureDlg)

public:
	CTortureDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~CTortureDlg();

// Dialog Data
	enum { IDD = IDD_TORTURE };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
	UINT	m_cores;
	BOOL	m_hyperthreading;	
	int	m_torture_type;
	int	m_minfft;
	int	m_maxfft;
	BOOL	m_in_place;
	float	m_memory;		// in GB
	int	m_timefft;
	int	m_avx512;
	int	m_fma3;
	int	m_avx;
	int	m_sse2;
	CStatic	c_cores_text;
	CEdit	c_cores;
	CButton c_hyperthreading;
	CButton c_L3_cache;
	CButton c_L4_cache;
	CStatic c_minfft_text;
	CEdit	c_minfft;
	CStatic c_maxfft_text;
	CEdit	c_maxfft;
	CButton c_in_place_fft;
	CStatic c_memory_text;
	CEdit	c_memory;
	CStatic c_timefft_text;
	CEdit	c_timefft;
	CButton c_avx512;
	CButton c_fma3;
	CButton c_avx;
	CButton c_sse2;

	int	m_blendmemory;			// Default blend memory (in MB)

	afx_msg void OnEnKillfocusCores();
	afx_msg void OnBnClickedL2Cache();
	afx_msg void OnBnClickedL3Cache();
	afx_msg void OnBnClickedL4Cache();
	afx_msg void OnBnClickedLargeFFT();
	afx_msg void OnBnClickedBlend();
	afx_msg void OnBnClickedCustom();
	afx_msg void OnBnClickedInPlaceFFT();
#ifdef X86_64
	afx_msg void OnBnClickedAVX512();
	afx_msg void OnBnClickedFMA3();
#endif
	afx_msg void OnBnClickedAVX();
#ifndef X86_64
	afx_msg void OnBnClickedSSE2();
#endif
};
