// TortureDlg.cpp : implementation file
//
// Copyright 1995-2023 Mersenne Research, Inc.  All rights reserved
//

#include "stdafx.h"
#include "Prime95.h"
#include "TortureDlg.h"

#define round_to_tenth(a)	((round((a) * 10.0)) / 10.0)

// CTortureDlg dialog

IMPLEMENT_DYNAMIC(CTortureDlg, CDialog)
CTortureDlg::CTortureDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CTortureDlg::IDD, pParent)
	, m_torture_type(4)
	, m_cores (1)
	, m_hyperthreading(TRUE)
	, m_minfft(0)
	, m_maxfft(0)
	, m_in_place(FALSE)
	, m_memory(0.0)
	, m_timefft(0)
	, m_blendmemory(0)
	, m_avx512(!(CPU_FLAGS & CPU_AVX512F))
	, m_fma3(!(CPU_FLAGS & CPU_FMA3))
	, m_avx(!(CPU_FLAGS & CPU_AVX))
	, m_sse2(!(CPU_FLAGS & CPU_SSE2))
{
}

CTortureDlg::~CTortureDlg()
{
}

void CTortureDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_CORES, m_cores);
	DDV_MinMaxUInt(pDX, m_cores, 1, HW_NUM_CORES);
	DDX_Check(pDX, IDC_HYPERTHREADING, m_hyperthreading);
	DDX_Radio(pDX, IDC_L2_CACHE, m_torture_type);
	DDX_Text(pDX, IDC_MINFFT, m_minfft);
	DDV_MinMaxInt(pDX, m_minfft, 4,
		(CPU_FLAGS & CPU_AVX512F) ? MAX_FFTLEN_AVX512 / 1024 :
		(CPU_FLAGS & CPU_FMA3) ? MAX_FFTLEN_FMA3 / 1024 :
		(CPU_FLAGS & CPU_SSE2) ? MAX_FFTLEN_SSE2 / 1024 :
					 MAX_FFTLEN / 1024);
	DDX_Text(pDX, IDC_MAXFFT, m_maxfft);
	DDV_MinMaxInt(pDX, m_maxfft, 4,
		(CPU_FLAGS & CPU_AVX512F) ? MAX_FFTLEN_AVX512 / 1024 :
		(CPU_FLAGS & CPU_FMA3) ? MAX_FFTLEN_FMA3 / 1024 :
		(CPU_FLAGS & CPU_SSE2) ? MAX_FFTLEN_SSE2 / 1024 :
					 MAX_FFTLEN / 1024);
	DDX_Check(pDX, IDC_IN_PLACE_FFT, m_in_place);
	DDX_Text(pDX, IDC_MEMORY, m_memory);
	DDV_MinMaxFloat(pDX, m_memory, 0.0, (float) round_to_tenth (0.99 * physical_memory () / 1024.0));
	DDX_Text(pDX, IDC_TIMEFFT, m_timefft);
#ifdef X86_64
	DDX_Check(pDX, IDC_AVX512, m_avx512);
	DDX_Check(pDX, IDC_FMA3, m_fma3);
#endif
	DDX_Check(pDX, IDC_AVX, m_avx);
#ifndef X86_64
	DDX_Check(pDX, IDC_SSE2, m_sse2);
#endif

	DDX_Control(pDX, IDC_CORES_TEXT, c_cores_text);
	DDX_Control(pDX, IDC_CORES, c_cores);
	DDX_Control(pDX, IDC_HYPERTHREADING, c_hyperthreading);
	DDX_Control(pDX, IDC_L3_CACHE, c_L3_cache);
	DDX_Control(pDX, IDC_L4_CACHE, c_L4_cache);
	DDX_Control(pDX, IDC_MINFFT_TEXT, c_minfft_text);
	DDX_Control(pDX, IDC_MINFFT, c_minfft);
	DDX_Control(pDX, IDC_MAXFFT_TEXT, c_maxfft_text);
	DDX_Control(pDX, IDC_MAXFFT, c_maxfft);
	DDX_Control(pDX, IDC_IN_PLACE_FFT, c_in_place_fft);
	DDX_Control(pDX, IDC_MEMORY_TEXT, c_memory_text);
	DDX_Control(pDX, IDC_MEMORY, c_memory);
	DDX_Control(pDX, IDC_TIMEFFT_TEXT, c_timefft_text);
	DDX_Control(pDX, IDC_TIMEFFT, c_timefft);
#ifdef X86_64
	DDX_Control(pDX, IDC_AVX512, c_avx512);
	DDX_Control(pDX, IDC_FMA3, c_fma3);
#endif
	DDX_Control(pDX, IDC_AVX, c_avx);
#ifndef X86_64
	DDX_Control(pDX, IDC_SSE2, c_sse2);
#endif

	if (m_torture_type != 5) {		// Custom
		// Calculate default FFT sizes
		tortureTestDefaultSizes (m_torture_type, m_cores, &m_minfft, &m_maxfft);
		if (m_minfft < 4) m_minfft = 4;
		if (m_maxfft < m_minfft) m_maxfft = m_minfft;
		if (m_maxfft > 32768) m_maxfft = 32768;
		// Assign other options
		m_in_place = (m_torture_type <= 2);		// TRUE for L2/L3/L4 cache
		m_memory = (float) (m_in_place ? 0.0 : round_to_tenth (m_blendmemory / 1024.0));
		m_timefft = (m_hyperthreading ? 6 : 3);
	}

	c_cores_text.EnableWindow (HW_NUM_CORES > 1);
	c_cores.EnableWindow (HW_NUM_CORES > 1);
	c_hyperthreading.EnableWindow (HW_NUM_CORES != HW_NUM_THREADS);
	c_L3_cache.EnableWindow (CPU_TOTAL_L3_CACHE_SIZE > 0);
	c_L4_cache.EnableWindow (CPU_TOTAL_L4_CACHE_SIZE > 0);
	c_minfft_text.EnableWindow (m_torture_type == 5);
	c_minfft.EnableWindow (m_torture_type == 5);
	c_maxfft_text.EnableWindow (m_torture_type == 5);
	c_maxfft.EnableWindow (m_torture_type == 5);
	c_in_place_fft.EnableWindow (m_torture_type == 5);
	c_memory_text.EnableWindow (m_torture_type == 5 && !m_in_place);
	c_memory.EnableWindow (m_torture_type == 5 && !m_in_place);
	c_timefft_text.EnableWindow (m_torture_type == 5);
	c_timefft.EnableWindow (m_torture_type == 5);
#ifdef X86_64
	c_avx512.EnableWindow (CPU_FLAGS & CPU_AVX512F && !m_fma3);
	c_fma3.EnableWindow (CPU_FLAGS & CPU_FMA3 && m_avx512 && !m_avx);
	c_avx.EnableWindow (CPU_FLAGS & CPU_AVX && m_fma3);
#else
	c_avx.EnableWindow (CPU_FLAGS & CPU_AVX && !m_sse2);
	c_sse2.EnableWindow (CPU_FLAGS & CPU_SSE2 && m_avx);
#endif
}


BEGIN_MESSAGE_MAP(CTortureDlg, CDialog)
	ON_EN_KILLFOCUS(IDC_CORES, OnEnKillfocusCores)
	ON_BN_CLICKED(IDC_L2_CACHE, OnBnClickedL2Cache)
	ON_BN_CLICKED(IDC_L3_CACHE, OnBnClickedL3Cache)
	ON_BN_CLICKED(IDC_L4_CACHE, OnBnClickedL4Cache)
	ON_BN_CLICKED(IDC_LARGE_FFT, OnBnClickedLargeFFT)
	ON_BN_CLICKED(IDC_BLEND, OnBnClickedBlend)
	ON_BN_CLICKED(IDC_CUSTOM, OnBnClickedCustom)
	ON_BN_CLICKED(IDC_IN_PLACE_FFT, OnBnClickedInPlaceFFT)
#ifdef X86_64
	ON_BN_CLICKED(IDC_AVX512, OnBnClickedAVX512)
	ON_BN_CLICKED(IDC_FMA3, OnBnClickedFMA3)
#endif
	ON_BN_CLICKED(IDC_AVX, OnBnClickedAVX)
#ifndef X86_64
	ON_BN_CLICKED(IDC_SSE2, OnBnClickedSSE2)
#endif
END_MESSAGE_MAP()


// CTortureDlg message handlers

void CTortureDlg::OnEnKillfocusCores()
{
	UpdateData ();
	UpdateData (0);
}

void CTortureDlg::OnBnClickedL2Cache()
{
	UpdateData ();
	UpdateData (0);
}

void CTortureDlg::OnBnClickedL3Cache()
{
	UpdateData ();
	UpdateData (0);
}

void CTortureDlg::OnBnClickedL4Cache()
{
	UpdateData ();
	UpdateData (0);
}

void CTortureDlg::OnBnClickedLargeFFT()
{
	UpdateData ();
	UpdateData (0);
}

void CTortureDlg::OnBnClickedBlend()
{
	UpdateData ();
	UpdateData (0);
}


void CTortureDlg::OnBnClickedCustom()
{
	UpdateData ();
}

void CTortureDlg::OnBnClickedInPlaceFFT()
{
	UpdateData ();
}

#ifdef X86_64
void CTortureDlg::OnBnClickedAVX512()
{
	UpdateData ();
}

void CTortureDlg::OnBnClickedFMA3()
{
	UpdateData ();
}
#endif

void CTortureDlg::OnBnClickedAVX()
{
	UpdateData ();
}

#ifndef X86_64
void CTortureDlg::OnBnClickedSSE2()
{
	UpdateData ();
}
#endif

