; Copyright 1995-2017 Mersenne Research, Inc., all rights reserved
; Author:  George Woltman
; Email: woltman@alum.mit.edu
;
; This routine implements fast trial factoring of Mersenne numbers
;
; This only runs on 32-bit CPUs.  For x86-64, see factor64.asm
;

TITLE   factor32

	.686
	.XMM
	.MODEL	FLAT

INCLUDE	unravel.mac
INCLUDE factor32.mac

; In 32-bit mode we are so starved for registers that we are forced to
; use the stack pointer to access the asm_data.  In 64-bit mode we
; use one of the extra 8 registers.  We can't use the rsp trick because
; that violates the Window's exception handling stack unwind mechanism.

AD_BASE		EQU	<rsp+push_amt>

;
; Define the offsets into the C / assembly communication structure
;

EXPONENT		EQU	DWORD PTR [AD_BASE+0*4]
FACPASS			EQU	DWORD PTR [AD_BASE+1*4]
FACHSW			EQU	DWORD PTR [AD_BASE+2*4]
FACMSW			EQU	DWORD PTR [AD_BASE+3*4]
FACLSW			EQU	DWORD PTR [AD_BASE+4*4]
CPU_FLAGS		EQU	DWORD PTR [AD_BASE+5*4]
firstcall		EQU	DWORD PTR [AD_BASE+6*4]
;;pad 
XMM_INITVAL		EQU	DWORD PTR [AD_BASE+12*4]
XMM_INVFAC		EQU	DWORD PTR [AD_BASE+16*4]
XMM_I1			EQU	DWORD PTR [AD_BASE+20*4]
XMM_I2			EQU	DWORD PTR [AD_BASE+24*4]
XMM_F1			EQU	DWORD PTR [AD_BASE+28*4]
XMM_F2			EQU	DWORD PTR [AD_BASE+32*4]
XMM_F3			EQU	DWORD PTR [AD_BASE+36*4]
XMM_TWO_120_MODF1	EQU	DWORD PTR [AD_BASE+40*4]
XMM_TWO_120_MODF2	EQU	DWORD PTR [AD_BASE+44*4]
XMM_TWO_120_MODF3	EQU	DWORD PTR [AD_BASE+48*4]
XMM_INIT120BS		EQU	DWORD PTR [AD_BASE+52*4]
XMM_INITBS		EQU	DWORD PTR [AD_BASE+54*4]
XMM_BS			EQU	DWORD PTR [AD_BASE+56*4]
XMM_SHIFTER		EQU	DWORD PTR [AD_BASE+58*4]
TWO_TO_FACSIZE_PLUS_62	EQU	QWORD PTR [AD_BASE+122*4]
SSE2_LOOP_COUNTER	EQU	DWORD PTR [AD_BASE+124*4]

;facdists	64 DUP(0)	; 32 distances between sieve factors
facdists		EQU	DWORD PTR [AD_BASE+256*4]
facdistsoffset		EQU				256*4
;facdist32	DD	0, 0	; 32 * facdist
facdist32		EQU	DWORD PTR [AD_BASE+320*4]
;BASE		DQ	0.0	; Used in 64-bit factoring code
BASE			EQU	QWORD PTR [AD_BASE+322*4]
;FACDIFF	DQ	0.0	; Distance between 1/fac1 and 1/fac2
FACDIFF			EQU	QWORD PTR [AD_BASE+324*4]
;FACHI		DQ	0.0	; High 32 bits of trial factor
FACHI			EQU	QWORD PTR [AD_BASE+326*4]
;FACHI2		DQ	0.0	; High 32 bits of trial factor #2
FACHI2			EQU	QWORD PTR [AD_BASE+328*4]
;FACLO		DQ	0.0	; Low 32 bits of trial factor
FACLO			EQU	QWORD PTR [AD_BASE+330*4]
;FACLO2		DQ	0.0	; Low 32 bits of trial factor #2
FACLO2			EQU	QWORD PTR [AD_BASE+332*4]
;initval	DD	0.0	; Initial value for squarer (as a float)
initval			EQU	DWORD PTR [AD_BASE+334*4]
;initval_inv	DD	0.0	; 1/initval (to compute 1/fac)
initval_inv		EQU	DWORD PTR [AD_BASE+335*4]
;fachi_shf_count DD	0	; Shift count for making FACHI
fachi_shf_count		EQU	DWORD PTR [AD_BASE+336*4]
;fachi_shf_mask	DD	0	; Shift mask for making FACHI
fachi_shf_mask		EQU	DWORD PTR [AD_BASE+337*4]
;savefac2	DD	0	; The 80-bit factor being tested
savefac2		EQU	DWORD PTR [AD_BASE+338*4]
;savefac1	DD	0
savefac1		EQU	DWORD PTR [AD_BASE+339*4]
;savefac0	DD	0
savefac0		EQU	DWORD PTR [AD_BASE+340*4]

;base_int	DD	0	; Used in 64-bit factoring
base_int		EQU	DWORD PTR [AD_BASE+341*4]
;wqloop_counter	DD	0	; Number of iterations in 64-bit wqloop
wqloop_counter		EQU	DWORD PTR [AD_BASE+342*4]
;temp		DD	0
temp			EQU	DWORD PTR [AD_BASE+343*4]
;primearray	DD	0	; Array of primes and offsets
primearray		EQU	DWORD PTR [AD_BASE+344*4]
;initsieve	DD	0	; Array used to init sieve
initsieve		EQU	DWORD PTR [AD_BASE+345*4]
;initlookup	DD	0	; Lookup table into initsieve
initlookup		EQU	DWORD PTR [AD_BASE+346*4]
;sieve		DD	0	; Array of sieve bits
sieve			EQU	DWORD PTR [AD_BASE+347*4]
;primearray12	DD	0
primearray12		EQU	DWORD PTR [AD_BASE+348*4]

	;; Pentium Pro globals
;facdistsflt	DQ	32 DUP(0.0)
facdistsflt		EQU	QWORD PTR [AD_BASE+400*4]
facdistsfltoffset	EQU	400*4
;facdist_flt	DQ	0	; Distance between trial factors (32 * 120 * p)
facdist_flt		EQU	QWORD PTR [AD_BASE+464*4]
;unused		DQ	0.0, 0.0, 0.0	; Saved queued up reciprocals
;unused			EQU	QWORD PTR [AD_BASE+466*4]
;quotient2	DD	0	; The result of a 486 or PPro division
quotient2		EQU	DWORD PTR [AD_BASE+472*4]
;quotient1	DD	0
quotient1		EQU	DWORD PTR [AD_BASE+473*4]
;quotient4	DD	0	; The result of a PPro division
quotient4		EQU	DWORD PTR [AD_BASE+474*4]
;quotient3	DD	0
quotient3		EQU	DWORD PTR [AD_BASE+475*4]
;quotient6	DD	0	; The result of a PPro division
quotient6		EQU	DWORD PTR [AD_BASE+476*4]
;quotient5	DD	0
quotient5		EQU	DWORD PTR [AD_BASE+477*4]
;quotient8	DD	0	; The result of a PPro division
quotient8		EQU	DWORD PTR [AD_BASE+478*4]
;quotient7	DD	0
quotient7		EQU	DWORD PTR [AD_BASE+479*4]
;rem2		DD	0	; The remainder of a 486 or PPro squaring
rem2			EQU	DWORD PTR [AD_BASE+480*4]
;rem1		DD	0
rem1			EQU	DWORD PTR [AD_BASE+481*4]
;rem4		DD	0	; The remainder of a PPro squaring
rem4			EQU	DWORD PTR [AD_BASE+482*4]
;rem3		DD	0
rem3			EQU	DWORD PTR [AD_BASE+483*4]
;rem6		DD	0	; The remainder of a PPro squaring
rem6			EQU	DWORD PTR [AD_BASE+484*4]
;rem5		DD	0
rem5			EQU	DWORD PTR [AD_BASE+485*4]
;rem8		DD	0	; The remainder of a PPro squaring
rem8			EQU	DWORD PTR [AD_BASE+486*4]
;rem7		DD	0
rem7			EQU	DWORD PTR [AD_BASE+487*4]
;pfac2		DD	0	; The first PPro factor being tested
pfac2			EQU	DWORD PTR [AD_BASE+488*4]
;pfac1		DD	0
pfac1			EQU	DWORD PTR [AD_BASE+489*4]
;pfac4		DD	0	; The second PPro factor being tested
pfac4			EQU	DWORD PTR [AD_BASE+490*4]
;pfac3		DD	0
pfac3			EQU	DWORD PTR [AD_BASE+491*4]
;pfac6		DD	0	; The third PPro factor being tested
pfac6			EQU	DWORD PTR [AD_BASE+492*4]
;pfac5		DD	0
pfac5			EQU	DWORD PTR [AD_BASE+493*4]
;pfac8		DD	0	; The fourth PPro factor being tested
pfac8			EQU	DWORD PTR [AD_BASE+494*4]
;pfac7		DD	0
pfac7			EQU	DWORD PTR [AD_BASE+495*4]
;pneg2		DD	0	; The first PPro -factor value
pneg2			EQU	DWORD PTR [AD_BASE+496*4]
;pneg1		DD	0
pneg1			EQU	DWORD PTR [AD_BASE+497*4]
;pneg4		DD	0	; The second PPro -factor value
pneg4			EQU	DWORD PTR [AD_BASE+498*4]
;pneg3		DD	0
pneg3			EQU	DWORD PTR [AD_BASE+499*4]
;pneg6		DD	0	; The third PPro -factor value
pneg6			EQU	DWORD PTR [AD_BASE+500*4]
;pneg5		DD	0
pneg5			EQU	DWORD PTR [AD_BASE+501*4]
;pneg8		DD	0	; The fourth PPro -factor value
pneg8			EQU	DWORD PTR [AD_BASE+502*4]
;pneg7		DD	0
pneg7			EQU	DWORD PTR [AD_BASE+503*4]
;queuedpro	DD	0	; Saved count of PPro queued factors
queuedpro		EQU	DWORD PTR [AD_BASE+504*4]

	;; 80-bit factoring globals
;faclow		DD	0
faclow			EQU	DWORD PTR [AD_BASE+520*4]
;facmid		DD	0
facmid			EQU	DWORD PTR [AD_BASE+521*4]
;fachigh	DD	0
fachigh			EQU	DWORD PTR [AD_BASE+522*4]
;two_to_123_modf_lo DD	0
two_to_123_modf_lo	EQU	DWORD PTR [AD_BASE+523*4]
;two_to_123_modf_mid DD	0
two_to_123_modf_mid	EQU	DWORD PTR [AD_BASE+524*4]
;two_to_123_modf_hi DD	0
two_to_123_modf_hi	EQU	DWORD PTR [AD_BASE+525*4]
;initval64	DD	0.0
initval64		EQU	DWORD PTR [AD_BASE+526*4]
;cmpvalmask	DD	0	; The bits to test in sqexit
cmpvalmask		EQU	DWORD PTR [AD_BASE+527*4]
;cmpval		DD	0	; The value to match in sqexit
cmpval			EQU	DWORD PTR [AD_BASE+528*4]
;REMMULTS	DD	32 DUP (0.0)
REMMULTS		EQU	DWORD PTR [AD_BASE+529*4]
;FACMULTS	DD	32 DUP (0.0)
FACMULTS		EQU	DWORD PTR [AD_BASE+561*4]
;shifter	DD	0
shifter			EQU	DWORD PTR [AD_BASE+593*4]
;sqloop_counter	DD	0	; Number of iterations in 60-bit sqloop
sqloop_counter		EQU	DWORD PTR [AD_BASE+594*4]
;initstart	DD	0	; First dword in initsieve to copy
initstart		EQU	DWORD PTR [AD_BASE+595*4]
;initval1	DD	0	; Bits 33-64 of 486 initial value for squarer
initval1		EQU	DWORD PTR [AD_BASE+596*4]
;initval0	DD	0	; Bits 65-96 of initial value for squarer
initval0		EQU	DWORD PTR [AD_BASE+597*4]
;reps		DD	0
reps			EQU	DWORD PTR [AD_BASE+598*4]
;p		DD	0	; Mersenne prime being tested
p			EQU	DWORD PTR [AD_BASE+599*4]
;twop		DD	0	; Multiples of p + p
twop			EQU	DWORD PTR [AD_BASE+600*4]
;last_primearray DD	0	; Last address in primearray
last_primearray		EQU	DWORD PTR [AD_BASE+601*4]

;XMM_COMPARE_VAL1 DD	0,0,0,0
XMM_COMPARE_VAL1	EQU	DWORD PTR [AD_BASE+620*4]
;XMM_COMPARE_VAL2 DD	0,0,0,0
XMM_COMPARE_VAL2	EQU	DWORD PTR [AD_BASE+624*4]
;XMM_COMPARE_VAL3 DD	0,0,0,0
XMM_COMPARE_VAL3	EQU	DWORD PTR [AD_BASE+628*4]

SAVED_RSP		EQU	DWORD PTR [AD_BASE+650*4]

last_global		EQU	DWORD PTR [AD_BASE+700*4]

;
; Constant global variables
;

_GWDATA SEGMENT ALIGN(128) PUBLIC 'DATA'
returns6	DD	OFFSET wx1, OFFSET wx2, OFFSET wx3, OFFSET wx4
		DD	OFFSET wx5, OFFSET wx6, OFFSET wx7, OFFSET wx8
		DD	OFFSET wx9, OFFSET wx10, OFFSET wx11, OFFSET wx12
		DD	OFFSET wx13, OFFSET wx14, OFFSET wx15, OFFSET wx16
		DD	OFFSET wx17, OFFSET wx18, OFFSET wx19, OFFSET wx20
		DD	OFFSET wx21, OFFSET wx22, OFFSET wx23, OFFSET wx24
		DD	OFFSET wx25, OFFSET wx26, OFFSET wx27, OFFSET wx28
		DD	OFFSET wx29, OFFSET wx30, OFFSET wx31, OFFSET wlp5
	
	;; From here down are globals not used in 64-bit code
returns5a	DD	OFFSET vx1, OFFSET vx2, OFFSET vx3, OFFSET vx4
		DD	OFFSET vx5, OFFSET vx6, OFFSET vx7, OFFSET vx8
		DD	OFFSET vx9, OFFSET vx10, OFFSET vx11, OFFSET vx12
		DD	OFFSET vx13, OFFSET vx14, OFFSET vx15, OFFSET vx16
		DD	OFFSET vx17, OFFSET vx18, OFFSET vx19, OFFSET vx20
		DD	OFFSET vx21, OFFSET vx22, OFFSET vx23, OFFSET vx24
		DD	OFFSET vx25, OFFSET vx26, OFFSET vx27, OFFSET vx28
		DD	OFFSET vx29, OFFSET vx30, OFFSET vx31, OFFSET vlp5
ONE		DD	1.0	; The floating point constant 1.0
rems		DD	1,7,17,23,31,41,47,49,71,73,79,89,97,103,113,119
QUARTER		DD	0.25
HALF		DD	0.5
TWO		DD	2.0
FOUR		DD	4.0
BIGVAL0		DD	0.0	; For rounding to an integer
BIGVAL1		DD	0.0	; For rounding to multiple of 2^32
TWO_TO_32	DD	0.0
TWO_TO_64	DQ	0.0
TWO_TO_123	DD	0.0
TWO_TO_MINUS_59 DD	0.0
FDMULT		DD	3840.0	; 32 * 120 (to compute facdist_flt)

	;; From here down are globals used in SSE2 code
	align 16
XMM_LOWONE		DD	1,0,0,0
XMM_HIGHONE		DD	0,0,1,0
XMM_BITS28		DD	0FFFFFFFh,0,0FFFFFFFh,0
XMM_BITS30		DD	3FFFFFFFh,0,3FFFFFFFh,0

;
; More data
;

	align	32
returns5	DD	OFFSET tx1, OFFSET tx2, OFFSET tx3, OFFSET tx4
		DD	OFFSET tx5, OFFSET tx6, OFFSET tx7, OFFSET tx8
		DD	OFFSET tx9, OFFSET tx10, OFFSET tx11, OFFSET tx12
		DD	OFFSET tx13, OFFSET tx14, OFFSET tx15, OFFSET tx16
		DD	OFFSET tx17, OFFSET tx18, OFFSET tx19, OFFSET tx20
		DD	OFFSET tx21, OFFSET tx22, OFFSET tx23, OFFSET tx24
		DD	OFFSET tx25, OFFSET tx26, OFFSET tx27, OFFSET tx28
		DD	OFFSET tx29, OFFSET tx30, OFFSET tx31, OFFSET tlp5
returns4	DD	OFFSET ux1, OFFSET ux2, OFFSET ux3, OFFSET ux4
		DD	OFFSET ux5, OFFSET ux6, OFFSET ux7, OFFSET ux8
		DD	OFFSET ux9, OFFSET ux10, OFFSET ux11, OFFSET ux12
		DD	OFFSET ux13, OFFSET ux14, OFFSET ux15, OFFSET ux16
		DD	OFFSET ux17, OFFSET ux18, OFFSET ux19, OFFSET ux20
		DD	OFFSET ux21, OFFSET ux22, OFFSET ux23, OFFSET ux24
		DD	OFFSET ux25, OFFSET ux26, OFFSET ux27, OFFSET ux28
		DD	OFFSET ux29, OFFSET ux30, OFFSET ux31, OFFSET ulp5

clrtab	DD	OFFSET clr01, OFFSET clr03, OFFSET clr05, OFFSET clr07
	DD	OFFSET clr11, OFFSET clr13, OFFSET clr15, OFFSET clr17
	DD	OFFSET clr21, OFFSET clr23, OFFSET clr25, OFFSET clr27
	DD	OFFSET clr31, OFFSET clr33, OFFSET clr35, OFFSET clr37
	DD	OFFSET clr41, OFFSET clr43, OFFSET clr45, OFFSET clr47
	DD	OFFSET clr51, OFFSET clr53, OFFSET clr55, OFFSET clr57
	DD	OFFSET clr61, OFFSET clr63, OFFSET clr65, OFFSET clr67
	DD	OFFSET clr71, OFFSET clr73, OFFSET clr75, OFFSET clr77
	DD	OFFSET clr201,OFFSET clr203,OFFSET clr205,OFFSET clr207
	DD	OFFSET clr211,OFFSET clr213,OFFSET clr215,OFFSET clr217
	DD	OFFSET clr221,OFFSET clr223,OFFSET clr225,OFFSET clr227
	DD	OFFSET clr231,OFFSET clr233,OFFSET clr235,OFFSET clr237
	DD	OFFSET clr241,OFFSET clr243,OFFSET clr245,OFFSET clr247
	DD	OFFSET clr251,OFFSET clr253,OFFSET clr255,OFFSET clr257
	DD	OFFSET clr261,OFFSET clr263,OFFSET clr265,OFFSET clr267
	DD	OFFSET clr271,OFFSET clr273,OFFSET clr275,OFFSET clr277
	DD	OFFSET clr301,OFFSET clr303,OFFSET clr305,OFFSET clr307
	DD	OFFSET clr311,OFFSET clr313,OFFSET clr315,OFFSET clr317
	DD	OFFSET clr321,OFFSET clr323,OFFSET clr325,OFFSET clr327
	DD	OFFSET clr331,OFFSET clr333,OFFSET clr335,OFFSET clr337
	DD	OFFSET clr341,OFFSET clr343,OFFSET clr345,OFFSET clr347
	DD	OFFSET clr351,OFFSET clr353,OFFSET clr355,OFFSET clr357
	DD	OFFSET clr361,OFFSET clr363,OFFSET clr365,OFFSET clr367
	DD	OFFSET clr371,OFFSET clr373,OFFSET clr375,OFFSET clr377
	DD	OFFSET clr401,OFFSET clr403,OFFSET clr405,OFFSET clr407
	DD	OFFSET clr411,OFFSET clr413,OFFSET clr415,OFFSET clr417
	DD	OFFSET clr421,OFFSET clr423,OFFSET clr425,OFFSET clr427
	DD	OFFSET clr431,OFFSET clr433,OFFSET clr435,OFFSET clr437
	DD	OFFSET clr441,OFFSET clr443,OFFSET clr445,OFFSET clr447
	DD	OFFSET clr451,OFFSET clr453,OFFSET clr455,OFFSET clr457
	DD	OFFSET clr461,OFFSET clr463,OFFSET clr465,OFFSET clr467
	DD	OFFSET clr471,OFFSET clr473,OFFSET clr475,OFFSET clr477
sivinfo	DB	1, 2, 1, 2, 1, 2, 3, 1, 3, 2, 1, 2, 3
	DB	3, 1, 3, 2, 1, 3, 2, 3, 4, 2, 1, 2, 1, 2, 7
	DB	2, 3, 1, 5, 1, 3, 3, 2, 3, 3, 1, 5, 1, 2, 1
	DB	6, 6, 2, 1, 2, 3, 1, 5, 3, 3, 3, 1, 3, 2, 1
	DB	5, 7, 2, 1, 2, 7, 3, 5, 1, 2, 3, 4, 3, 3, 2
	DB	3, 4, 2, 4, 5, 1, 5, 1, 3, 2, 3, 4, 2, 1, 2
	DB	6, 4, 2, 4, 2, 3, 6, 1, 9, 3, 5, 3, 3, 1, 3
	DB	5, 3, 3, 1, 3, 3, 2, 1, 6, 5, 1, 2, 3, 3, 1
	DB	6, 2, 3, 4, 5, 4, 5, 4, 3, 3, 2, 4, 3, 2, 4
	DB	2, 7, 5, 6, 1, 5, 1, 2, 1, 5, 7, 2, 1, 2, 7
	DB	2, 1, 2, 10, 2, 4, 5, 4, 2, 3, 3, 7, 2, 3, 3
	DB	4, 3, 6, 2, 3, 1, 5, 1, 3, 5, 1, 5, 1, 3, 9
	DB	2, 1, 2, 3, 3, 4, 3, 3, 11, 1, 5, 4, 5, 3, 3
	DB	4, 6, 2, 3, 3, 1, 3, 6, 5, 9, 1, 2, 3, 1, 3
	DB	2, 1, 2, 6, 1, 3, 17, 3, 3, 4, 9, 5, 7, 2, 1
	DB	2, 3, 4, 2, 1, 3, 6, 5, 1, 2, 1, 2, 3, 6, 6
	DB	4, 6, 3, 2, 3, 4, 2, 4, 2, 7, 2, 3, 1, 2, 3
	DB	1, 3, 5, 10, 3, 2, 1, 12, 2, 1, 5, 6, 1, 5, 4
	DB	3, 3, 3, 9, 3, 2, 1, 6, 5, 6, 4, 8, 7, 3, 2
	DB	1, 2, 1, 5, 6, 3, 3, 9, 1, 8, 1, 11, 3, 4, 3
	DB	2, 1, 2, 4, 3, 5, 1, 5, 7, 5, 3, 6, 1, 2, 1
	DB	5, 6, 1, 8, 1, 3, 2, 1, 5, 4, 9, 12, 2, 3, 4
	DB	8, 1, 2, 4, 8, 1, 2, 4, 3, 3, 2, 6, 1, 11, 3
	DB	1, 3, 2, 3, 7, 3, 2, 1, 3, 2, 3, 6, 3, 3, 7
	DB	2, 3, 6, 4, 3, 2, 13, 9, 5, 4, 2, 3, 1, 3, 11
	DB	6, 1, 8, 4, 2, 6, 7, 5, 1, 2, 4, 3, 3, 2, 1
	DB	2, 3, 4, 2, 1, 3, 5, 1, 5, 4, 2, 7, 5, 6, 1
	DB	3, 2, 1, 8, 7, 2, 3, 4, 3, 2, 9, 4, 5, 3, 3
	DB	4, 5, 6, 7, 2, 3, 3, 1, 14, 1, 5, 4, 2, 7, 2
	DB	4, 6, 3, 6, 2, 3, 10, 5, 1, 8, 13, 2, 1, 6, 3
	DB	2, 6, 3, 4, 2, 4, 11, 1, 2, 1, 6, 14, 1, 3, 3
	DB	3, 2, 3, 1, 6, 2, 6, 1, 5, 1, 8, 1, 8, 3, 10
	DB	8, 4, 2, 1, 2, 1, 11, 4, 6, 3, 5, 1, 2, 3, 1
	DB	3, 5, 1, 6, 5, 1, 5, 7, 3, 2, 3, 4, 3, 3, 8
	DB	6, 1, 2, 7, 3, 2, 4, 5, 4, 3, 3, 11, 3, 1, 5
	DB	7, 2, 3, 9, 1, 5, 7, 2, 1, 5, 7, 2, 4, 9, 2
	DB	3, 1, 2, 3, 1, 6, 2, 10, 11, 6, 1, 2, 3, 3, 1
	DB	3, 11, 1, 3, 8, 3, 6, 1, 3, 6, 8, 1, 2, 3, 7
	DB	2, 1, 9, 12, 5, 3, 1, 5, 1, 5, 1, 5, 3, 1, 5
	DB	1, 5, 3, 4, 15, 5, 1, 5, 4, 3, 5, 9, 3, 6, 6
	DB	1, 9, 3, 2, 3, 3, 9, 1, 5, 7, 3, 2, 1, 2, 12
	DB	1, 6, 3, 8, 4, 3, 3, 9, 8, 1, 2, 3, 1, 3, 3
	DB	5, 3, 6, 6, 9, 1, 3, 2, 9, 4, 12, 2, 1, 2, 3
	DB	1, 6, 2, 7, 15, 5, 3, 6, 7, 3, 5, 6, 1, 2, 3
	DB	4, 3, 5, 1, 2, 7, 3, 3, 2, 3, 1, 5, 1, 8, 6
	DB	4, 9, 2, 3, 6, 1, 3, 3, 3, 14, 3, 7, 2, 4, 5
	DB	4, 6, 9, 2, 1, 2, 12, 6, 3, 1, 8, 3, 3, 7, 5
	DB	7, 2, 15, 3, 3, 3, 4, 3, 2, 1, 6, 3, 2, 1, 3
	DB	11, 3, 1, 2, 9, 1, 2, 6, 1, 3, 2, 13, 3, 3, 2
	DB	4, 5, 16, 8, 1, 3, 2, 1, 2, 1, 5, 7, 3, 2, 4
	DB	5, 3, 10, 2, 1, 3, 15, 2, 4, 5, 3, 3, 4, 3, 6
	DB	2, 3, 1, 3, 2, 3, 1, 5, 1, 8, 3, 10, 2, 6, 7
	DB	14, 3, 10, 2, 9, 4, 3, 2, 3, 7, 3, 3, 5, 1, 5
	DB	6, 4, 5, 1, 5, 4, 6, 5, 12, 1, 2, 4, 3, 2, 4
	DB	9, 5, 3, 3, 1, 3, 5, 6, 1, 5, 3, 3, 3, 4, 3
	DB	5, 3, 1, 3, 3, 3, 5, 4, 12, 3, 11, 1, 9, 2, 4
	DB	5, 15, 4, 9, 2, 1, 5, 3, 1, 3, 2, 9, 4, 6, 9
	DB	8, 3, 1, 6, 3, 5, 1, 5, 1, 3, 5, 7, 2, 12, 1
	DB	8, 1, 5, 1, 5, 10, 2, 1, 2, 4, 8, 3, 3, 1, 6
	DB	8, 4, 2, 3, 15, 1, 5, 1, 3, 2, 3, 3, 4, 3, 2
	DB	6, 3, 4, 6, 2, 7, 6, 5, 12, 3, 6, 3, 1, 11, 4
	DB	9, 5, 3, 7, 2, 1, 3, 5, 4, 3, 2, 3, 15, 7, 5
	DB	1, 6, 5, 1, 8, 1, 9, 12, 9, 3, 8, 9, 3, 1, 9
	DB	2, 3, 1, 5, 4, 5, 3, 3, 4, 2, 3, 1, 5, 1, 6
	DB	2, 3, 3, 1, 6, 2, 7, 9, 2, 3, 10, 2, 4, 3, 2
	DB	4, 2, 7, 3, 2, 7, 6, 2, 1, 15, 2, 12, 3, 3, 6
	DB	6, 7, 3, 2, 1, 2, 9, 3, 6, 4, 3, 2, 6, 1, 6
	DB	15, 8, 1, 3, 11, 7, 3, 5, 6, 3, 1, 2, 4, 5, 3
	DB	3, 12, 7, 3, 2, 4, 6, 9, 5, 1, 5, 1, 2, 3, 10
	DB	3, 2, 7, 2, 1, 2, 7, 3, 6, 12, 5, 3, 4, 5, 1
	DB	15, 2, 3, 1, 6, 2, 7, 3, 17, 6, 4, 3, 5, 1, 2
	DB	10, 5, 4, 8, 1, 5, 7, 2, 1, 6, 3, 8, 3, 4, 2
	DB	4, 2, 3, 4, 3, 3, 6, 3, 2, 3, 3, 4, 9, 2, 10
	DB	2, 6, 1, 5, 3, 1, 5, 6, 1, 2, 10, 3, 15, 3, 2
	DB	4, 5, 6, 3, 1, 14, 1, 3, 2, 1, 8, 6, 1, 3, 5
	DB	4, 12, 6, 3, 9, 3, 2, 7, 3, 2, 6, 4, 3, 6, 2
	DB	3, 6, 3, 6, 1, 8, 10, 2, 1, 5, 9, 4, 2, 7, 2
	DB	1, 3, 11, 3, 7, 3, 3, 5, 3, 1, 5, 1, 2, 1, 11
	DB	1, 2, 3, 3, 6, 3, 7, 5, 6, 3, 4, 2, 18, 7, 6
	DB	3, 2, 3, 1, 6, 3, 6, 8, 1, 5, 4, 11, 1, 6, 3
	DB	2, 3, 9, 1, 6, 3, 2, 6, 4, 3, 6, 2, 3, 6, 3
	DB	1, 6, 6, 2, 7, 3, 8, 3, 1, 5, 4, 9, 3, 17, 1
	DB	14, 1, 11, 3, 1, 5, 6, 1, 3, 2, 4, 11, 3, 1, 5
	DB	4, 2, 3, 4, 2, 6, 9, 6, 10, 2, 3, 3, 4, 2, 1
	DB	8, 6, 1, 5, 4, 5, 1, 2, 3, 7, 6, 11, 4, 14, 1
	DB	2, 10, 2, 1, 2, 7, 5, 6, 1, 6, 8, 1, 14, 4, 11
	DB	4, 2, 3, 3, 7, 2, 4, 6, 3, 3, 2, 10, 2, 9, 1
	DB	6, 3, 2, 3, 7, 9, 5, 4, 5, 16, 3, 5, 3, 3, 1
	DB	3, 8, 3, 1, 6, 3, 14, 1, 5, 4, 8, 3, 4, 3, 5
	DB	12, 10, 5, 1, 5, 1, 6, 2, 3, 10, 2, 1, 6, 9, 5
	DB	1, 5, 1, 2, 10, 8, 13, 2, 4, 3, 2, 6, 3, 4, 6
	DB	6, 3, 2, 4, 11, 1, 8, 7, 5, 3, 6, 6, 7, 3, 2
	DB	10, 2, 6, 3, 1, 3, 3, 8, 4, 11, 1, 14, 4, 3, 2
	DB	10, 2, 6, 12, 10, 2, 4, 5, 1, 8, 1, 6, 6, 17, 1
	DB	2, 3, 6, 3, 3, 4, 3, 2, 1, 3, 12, 2, 10, 5, 3
	DB	3, 7, 2, 3, 3, 1, 6, 3, 5, 1, 5, 3, 10, 2, 13
	DB	2, 1, 3, 11, 1, 12, 2, 3, 1, 2, 3, 12, 3, 4, 2
	DB	1, 17, 3, 4, 8, 6, 1, 5, 1, 5, 3, 4, 2, 4, 6
	DB	11, 3, 7, 2, 13, 2, 1, 6, 5, 4, 2, 4, 6, 2, 7
	DB	3, 8, 3, 4, 2, 3, 3, 4, 3, 5, 6, 1, 3, 3, 8
	DB	4, 3, 3, 6, 5, 1, 3, 9, 2, 3, 3, 3, 6, 9, 4
	DB	3, 5, 4, 9, 2, 7, 3, 9, 5, 4, 5, 6, 1, 3, 6
	DB	6, 18, 2, 3, 4, 2, 3, 1, 2, 9, 6, 3, 4, 3, 3
	DB	2, 9, 1, 2, 1, 12, 2, 3, 3, 7, 15, 3, 2, 3, 6
	DB	3, 10, 2, 4, 2, 4, 3, 3, 2, 15, 1, 5, 6, 4, 5
	DB	4, 12, 3, 6, 2, 7, 2, 3, 1, 14, 7, 8, 1, 6, 3
	DB	2, 10, 5, 3, 3, 3, 4, 5, 6, 7, 5, 7, 8, 7, 5
	DB	7, 3, 8, 3, 4, 3, 8, 10, 5, 1, 3, 2, 1, 2, 6
	DB	1, 5, 1, 3, 11, 3, 1, 2, 9, 4, 5, 4, 11, 1, 5
	DB	9, 7, 2, 1, 2, 9, 1, 2, 3, 4, 5, 1, 15, 2, 15
	DB	1, 5, 1, 9, 2, 9, 3, 7, 5, 1, 2, 10, 18, 3, 2
	DB	3, 7, 2, 10, 5, 7, 11, 3, 1, 15, 6, 5, 9, 1, 2
	DB	7, 3, 11, 9, 1, 6, 3, 2, 4, 2, 4, 3, 5, 1, 6
	DB	9, 5, 7, 8, 7, 2, 3, 3, 1, 3, 2, 1, 14, 1, 14
	DB	3, 1, 2, 3, 7, 2, 6, 7, 8, 7, 2, 3, 4, 3, 2
	DB	3, 3, 3, 4, 2, 4, 2, 7, 8, 4, 3, 2, 6, 4, 8
	DB	1, 5, 4, 2, 3, 13, 3, 5, 4, 2, 3, 6, 7, 15, 2
	DB	7, 11, 4, 6, 2, 3, 4, 5, 3, 7, 5, 3, 1, 5, 6
	DB	6, 7, 3, 3, 9, 5, 3, 4, 9, 2, 3, 1, 3, 5, 1
	DB	5, 4, 3, 3, 5, 1, 9, 5, 1, 6, 2, 3, 4, 5, 6
	DB	7, 6, 2, 4, 5, 3, 3, 10, 2, 7, 8, 7, 5, 4, 5
	DB	6, 1, 9, 3, 6, 5, 6, 1, 2, 1, 6, 3, 2, 4, 2
	DB	22, 2, 1, 2, 1, 5, 6, 3, 3, 7, 2, 3, 3, 3, 4
	DB	3, 18, 9, 2, 3, 1, 6, 3, 3, 3, 2, 7, 11, 6, 1
	DB	9, 5, 3, 13, 12, 2, 1, 2, 1, 2, 7, 2, 3, 3, 4
	DB	8, 6, 1, 21, 2, 1, 2, 12, 3, 3, 1, 9, 2, 7, 3
	DB	14, 9, 7, 3, 5, 6, 1, 3, 6, 15, 3, 2, 3, 3, 7
	DB	2, 1, 12, 2, 3, 3, 13, 5, 9, 3, 4, 3, 3, 15, 2
	DB	6, 6, 1, 8, 1, 3, 2, 6, 9, 1, 3, 2, 13, 6, 3
	DB	6, 2, 12, 12, 6, 3, 1, 6, 14, 4, 2, 3, 6, 1, 9
	DB	3, 2, 3, 3, 10, 8, 1, 3, 3, 9, 5, 3, 1, 2, 4
	DB	3, 3, 12, 8, 3, 4, 5, 3, 7, 11, 4, 8, 3, 1, 6
	DB	2, 1, 11, 4, 9, 17, 1, 3, 9, 2, 3, 3, 4, 5, 4
	DB	9, 3, 2, 1, 2, 4, 8, 1, 6, 6, 3, 9, 2, 3, 3
	DB	3, 1, 3, 6, 5, 10, 6, 9, 2, 3, 1, 8, 1, 5, 7
	DB	2, 15, 1, 5, 6, 1, 12, 3, 8, 4, 5, 1, 6, 11, 3
	DB	1, 8, 10, 5, 1, 6, 6, 9, 5, 6, 3, 1, 5, 1, 3
	DB	5, 9, 1, 6, 3, 2, 3, 1, 12, 14, 1, 2, 1, 5, 1
	DB	8, 6, 4, 11, 1, 3, 2, 1, 5, 3, 10, 6, 5, 4, 6
	DB	3, 3, 3, 2, 9, 1, 2, 6, 9, 1, 6, 3, 2, 1, 8
	DB	6, 6, 7, 2, 4, 9, 2, 6, 7, 3, 3, 2, 4, 3, 2
	DB	10, 6, 5, 7, 2, 1, 8, 1, 6, 15, 2, 3, 12, 10, 12
	DB	5, 4, 6, 5, 6, 3, 6, 6, 3, 4, 8, 7, 3, 2, 3
	DB	18, 10, 5, 15, 6, 1, 2, 1, 14, 6, 7, 3, 11, 4, 2
	DB	9, 3, 7, 9, 2, 3, 1, 3, 17, 9, 1, 8, 3, 9, 1
	DB	12, 2, 1, 3, 6, 3, 6, 5, 4, 3, 8, 6, 4, 5, 7
	DB	20, 3, 1, 3, 2, 6, 7, 2, 1, 2, 1, 2, 4, 3, 5
	DB	3, 3, 1, 3, 3, 3, 6, 3, 12, 5, 1, 5, 3, 6, 3
	DB	3, 7, 3, 3, 26, 10, 3, 5, 1, 5, 4, 5, 6, 6, 1
	DB	3, 2, 7, 8, 4, 6, 3, 11, 1, 5, 4, 3, 11, 1, 11
	DB	3, 4, 5, 6, 6, 1, 5, 3, 6, 1, 2, 7, 5, 1, 3
	DB	9, 2, 6, 4, 9, 6, 3, 3, 2, 3, 3, 7, 2, 1, 6
	DB	6, 2, 3, 9, 9, 6, 1, 8, 6, 4, 9, 5, 13, 2, 3
	DB	4, 3, 3, 2, 1, 5, 10, 2, 3, 4, 2, 10, 5, 1, 17
	DB	1, 2, 12, 1, 6, 6, 5, 3, 1, 6, 15, 3, 6, 8, 6
	DB	1, 11, 9, 6, 7, 5, 1, 6, 6, 2, 1, 2, 3, 6, 1
	DB	8, 9, 1, 20, 4, 8, 3, 4, 5, 1, 2, 9, 4, 5, 4
	DB	6, 2, 9, 1, 9, 5, 1, 2, 1, 2, 4, 14, 1, 3, 11
	DB	6, 3, 7, 9, 2, 3, 4, 3, 3, 5, 4, 2, 1, 9, 5
	DB	3, 10, 11, 4, 3, 15, 2, 1, 2, 9, 3, 15, 1, 2, 4
	DB	3, 2, 3, 6, 7, 17, 7, 3, 2, 1, 3, 2, 7, 2, 1
	DB	3, 14, 1, 2, 3, 4, 5, 1, 5, 1, 5, 1, 2, 15, 1
	DB	6, 6, 5, 9, 6, 7, 5, 1, 6, 3, 5, 3, 7, 6, 2
	DB	7, 2, 9, 1, 5, 4, 2, 4, 5, 6, 9, 9, 4, 3, 9
	DB	8, 7, 3, 3, 5, 7, 2, 3, 1, 6, 6, 2, 3, 3, 6
	DB	1, 8, 1, 6, 3, 2, 7, 3, 2, 1, 6, 9, 2, 18, 9
	DB	6, 6, 1, 2, 1, 2, 4, 6, 2, 18, 3, 9, 1, 6, 5
	DB	3, 6, 12, 4, 3, 3, 8, 6, 1, 9, 5, 10, 5, 1, 3
	DB	9, 2, 1, 20, 3, 1, 8, 1, 2, 4, 9, 5, 6, 3, 1
	DB	5, 4, 2, 3, 6, 1, 5, 9, 4, 3, 2, 10, 2, 3, 18
	DB	3, 1, 5, 3, 12, 3, 7, 8, 3, 9, 1, 5, 10, 5, 4
	DB	3, 2, 3, 1, 5, 1, 6, 2, 1, 2, 4, 5, 3, 6, 9
	DB	7, 6, 8, 4, 3, 8, 4, 2, 1, 3, 9, 12, 9, 5, 6
	DB	1, 2, 7, 5, 3, 3, 3, 9, 6, 1, 14, 9, 7, 8, 6
	DB	7, 12, 6, 11, 3, 1, 5, 4, 2, 1, 2, 7, 6, 3, 2
	DB	3, 7, 2, 1, 2, 15, 3, 1, 3, 5, 1, 15, 11, 1, 2
	DB	3, 4, 3, 3, 8, 6, 6, 3, 4, 2, 1, 12, 6, 2, 3
	DB	4, 3, 3, 5, 1, 3, 6, 14, 7, 3, 2, 6, 4, 3, 6
	DB	2, 3, 7, 3, 6, 5, 3, 3, 4, 3, 3, 2, 1, 2, 4
	DB	6, 2, 7, 9, 5, 1, 8, 3, 10, 3, 5, 4, 2, 15, 18
	DB	6, 4, 11, 6, 1, 3, 6, 8, 3, 3, 1, 9, 2, 13, 2
	DB	4, 9, 5, 4, 5, 3, 7, 2, 10, 11, 9, 6, 4, 14, 6
	DB	3, 3, 4, 3, 6, 12, 8, 7, 2, 7, 6, 3, 5, 6, 10
	DB	3, 2, 4, 9, 6, 9, 5, 1, 2, 10, 5, 7, 2, 3, 1
	DB	5, 12, 9, 1, 2, 10, 8, 7, 5, 7, 3, 2, 3, 10, 3
	DB	5, 3, 1, 6, 3, 15, 5, 4, 3, 2, 3, 4, 20, 1, 2
	DB	1, 6, 9, 2, 3, 4, 5, 3, 9, 9, 1, 6, 8, 4, 3
	DB	2, 3, 3, 1, 26, 7, 2, 10, 8, 1, 2, 3, 6, 1, 3
	DB	6, 6, 3, 2, 7, 5, 3, 3, 7, 5, 7, 8, 4, 3, 6
	DB	2, 4, 11, 3, 1, 9, 11, 3, 1, 9, 3, 8, 7, 5, 3
	DB	6, 1, 3, 2, 4, 9, 6, 8, 1, 2, 7, 2, 4, 6, 6
	DB	15, 8, 4, 2, 1, 3, 11, 6, 4, 5, 3, 3, 3, 7, 3
	DB	9, 5, 6, 1, 5, 1, 2, 13, 2, 6, 4, 2, 9, 4, 5
	DB	7, 8, 3, 3, 4, 5, 3, 4, 3, 6, 5, 10, 5, 4, 2
	DB	6, 13, 9, 2, 6, 9, 3, 15, 3, 4, 3, 11, 6, 1, 2
	DB	3, 3, 1, 5, 1, 2, 3, 3, 1, 3, 11, 9, 3, 9, 6
	DB	4, 6, 3, 5, 6, 1, 8, 1, 5, 1, 5, 9, 3, 10, 2
	DB	1, 3, 11, 3, 3, 9, 3, 7, 6, 8, 1, 3, 3, 2, 7
	DB	6, 2, 1, 9, 8, 18, 6, 3, 7, 14, 1, 6, 3, 6, 3
	DB	2, 1, 8, 15, 4, 12, 3, 15, 5, 1, 9, 2, 3, 6, 4
	DB	11, 1, 3, 11, 9, 1, 5, 1, 5, 15, 1, 14, 3, 7, 8
	DB	3, 10, 8, 1, 3, 2, 16, 2, 1, 2, 3, 1, 6, 2, 3
	DB	3, 6, 1, 3, 2, 3, 4, 3, 2, 10, 2, 16, 5, 4, 8
	DB	1, 11, 1, 2, 3, 4, 3, 8, 7, 2, 9, 4, 2, 10, 3
	DB	6, 6, 3, 5, 1, 5, 1, 6, 14, 6, 9, 1, 9, 5, 4
	DB	5, 24, 1, 2, 3, 4, 5, 1, 5, 15, 1, 18, 3, 5, 3
	DB	1, 9, 2, 3, 4, 8, 7, 8, 3, 7, 2, 10, 2, 3, 1
	DB	5, 6, 1, 3, 6, 3, 3, 2, 6, 1, 3, 2, 6, 3, 4
	DB	2, 1, 3, 9, 5, 3, 4, 6, 3, 11, 1, 3, 6, 9, 2
	DB	7, 3, 2, 10, 3, 8, 4, 2, 4, 11, 4, 6, 3, 3, 8
	DB	6, 9, 15, 4, 2, 1, 2, 3, 13, 2, 7, 12, 11, 3, 1
	DB	3, 5, 3, 7, 3, 3, 6, 5, 3, 1, 6, 5, 6, 4, 9
	DB	9, 5, 3, 4, 8, 3, 3, 4, 8, 10, 2, 1, 5, 1, 5
	DB	6, 3, 4, 3, 5, 10, 5, 9, 13, 2, 3, 15, 1, 2, 4
	DB	3, 6, 6, 9, 2, 4, 11, 3, 1, 6, 17, 3, 9, 6, 3
	DB	1, 14, 7, 8, 7, 2, 7, 6, 2, 3, 3, 1, 18, 2, 3
	DB	10, 6, 12, 3, 11, 1, 8, 9, 6, 6, 9, 1, 3, 3, 3
	DB	2, 3, 7, 2, 1, 11, 4, 6, 3, 5, 3, 4, 6, 9, 6
	DB	3, 5, 1, 11, 7, 3, 3, 2, 9, 3, 10, 11, 1, 6, 12
	DB	2, 9, 9, 1, 11, 1, 2, 6, 4, 6, 5, 7, 2, 1, 9
	DB	8, 19, 3, 3, 3, 6, 5, 3, 6, 4, 3, 2, 3, 7, 15
	DB	3, 5, 4, 11, 3, 4, 6, 5, 1, 5, 1, 3, 5, 1, 5
	DB	6, 9, 10, 3, 2, 4, 11, 3, 3, 15, 3, 7, 3, 6, 6
	DB	3, 5, 1, 5, 15, 1, 8, 4, 2, 1, 3, 9, 2, 1, 3
	DB	2, 13, 2, 4, 3, 5, 1, 2, 3, 4, 2, 3, 15, 6, 1
	DB	3, 3, 2, 10, 11, 4, 2, 1, 2, 36, 4, 2, 4, 11, 1
	DB	2, 7, 5, 1, 2, 10, 3, 5, 9, 3, 10, 8, 3, 4, 3
	DB	2, 10, 6, 11, 1, 2, 1, 6, 5, 9, 1, 11, 3, 9, 15
	DB	1, 5, 7, 5, 4, 8, 25, 3, 5, 4, 5, 6, 3, 9, 1
	DB	11, 3, 1, 2, 3, 4, 3, 3, 5, 9, 1, 11, 1, 8, 7
	DB	5, 3, 1, 6, 5, 10, 2, 7, 3, 2, 18, 1, 2, 3, 6
	DB	1, 2, 7, 6, 3, 2, 3, 1, 3, 2, 10, 5, 1, 5, 3
	DB	6, 1, 12, 6, 6, 3, 3, 2, 12, 1, 2, 12, 1, 3, 2
	DB	3, 4, 8, 3, 1, 5, 6, 7, 3, 17, 3, 7, 3, 2, 1
	DB	15, 11, 4, 2, 3, 4, 2, 1, 14, 1, 3, 2, 13, 9, 11
	DB	1, 3, 8, 3, 1, 8, 6, 1, 6, 2, 3, 3, 7, 5, 3
	DB	4, 6, 2, 9, 1, 5, 4, 8, 3, 3, 15, 1, 5, 9, 1
	DB	5, 4, 2, 4, 6, 12, 20, 1, 6, 5, 3, 6, 1, 6, 2
	DB	1, 2, 3, 9, 7, 6, 3, 2, 7, 15, 2, 4, 5, 4, 3
	DB	5, 9, 4, 2, 7, 8, 3, 4, 2, 3, 1, 5, 1, 6, 2
	DB	1, 2, 3, 4, 2, 3, 16, 12, 5, 4, 9, 5, 1, 3, 5
	DB	1, 2, 9, 3, 6, 1, 8, 1, 11, 3, 3, 4, 9, 2, 9
	DB	6, 4, 3, 2, 10, 3, 15, 11, 6, 1, 3, 9, 2, 31, 2
	DB	1, 6, 3, 5, 1, 6, 6, 14, 1, 2, 7, 11, 3, 1, 3
	DB	3, 5, 7, 2, 1, 5, 3, 4, 5, 7, 5, 3, 1, 6, 11
	DB	9, 4, 5, 9, 6, 1, 6, 2, 6, 1, 5, 1, 3, 9, 3
	DB	3, 17, 3, 1, 6, 2, 3, 9, 9, 1, 8, 3, 3, 4, 3
	DB	5, 9, 4, 5, 4, 5, 1, 2, 9, 13, 6, 11, 1, 2, 1
	DB	11, 3, 3, 7, 8, 3, 10, 5, 6, 1, 9, 21, 2, 12, 1
	DB	3, 5, 6, 1, 3, 5, 4, 2, 3, 6, 6, 4, 2, 3, 6
	DB	15, 10, 3, 12, 3, 5, 6, 1, 5, 10, 3, 3, 2, 6, 7
	DB	5, 9, 6, 4, 3, 6, 2, 7, 5, 1, 6, 15, 8, 1, 6
	DB	3, 2, 1, 2, 3, 13, 2, 9, 1, 2, 3, 7, 27, 3, 26
	DB	1, 8, 3, 3, 6, 13, 2, 1, 3, 11, 3, 1, 6, 6, 3
	DB	5, 9, 1, 6, 6, 5, 9, 6, 3, 4, 3, 5, 3, 4, 2
	DB	1, 2, 10, 12, 3, 3, 5, 7, 5, 1, 11, 3, 7, 5, 13
	DB	2, 9, 4, 6, 6, 5, 6, 3, 4, 8, 3, 4, 3, 3, 11
	DB	1, 5, 10, 5, 3, 22, 9, 3, 5, 1, 2, 3, 7, 2, 13
	DB	2, 1, 6, 5, 4, 2, 4, 6, 2, 6, 4, 11, 4, 3, 5
	DB	9, 3, 3, 4, 3, 6, 2, 4, 9, 5, 6, 3, 6, 1, 3
	DB	2, 1, 8, 6, 6, 7, 5, 7, 3, 5, 6, 1, 6, 3, 2
	DB	3, 1, 6, 2, 13, 3, 9, 3, 5, 3, 1, 9, 5, 4, 2
	DB	13, 5, 10, 3, 8, 10, 6, 5, 4, 5, 1, 8, 3, 10, 5
	DB	10, 2, 15, 1, 2, 4, 8, 1, 9, 2, 1, 3, 5, 9, 6
	DB	7, 9, 3, 8, 10, 3, 2, 4, 3, 2, 3, 6, 4, 5, 1
	DB	6, 3, 2, 1, 3, 5, 1, 8, 6, 7, 5, 3, 4, 3, 14
	DB	1, 3, 9, 15, 17, 1, 8, 6, 1, 9, 8, 3, 4, 5, 4
	DB	5, 4, 5, 22, 3, 3, 2, 10, 2, 1, 2, 7, 14, 4, 3
	DB	8, 7, 15, 3, 15, 2, 7, 5, 3, 3, 4, 2, 9, 6, 3
	DB	1, 11, 6, 4, 3, 6, 2, 7, 2, 3, 1, 2, 9, 10, 3
	DB	8, 19, 8, 1, 2, 3, 1, 20, 21, 7, 2, 3, 1, 12, 5
	DB	3, 1, 9, 5, 6, 1, 8, 1, 3, 8, 3, 4, 2, 1, 5
	DB	3, 4, 5, 1, 9, 8, 4, 6, 9, 6, 3, 6, 5, 3, 3
	DB	9, 6, 7, 2, 1, 5, 10, 3, 6, 3, 8, 13, 2, 9, 1
	DB	2, 16, 5, 4, 3, 2, 3, 3, 7, 3, 9, 2, 1, 9, 5
	DB	4, 5, 4, 5, 1, 2, 3, 1, 5, 21, 4, 6, 2, 3, 9
	DB	1, 8, 4, 2, 1, 5, 7, 6, 5, 10, 2, 4, 5, 19, 2
	DB	3, 1, 5, 10, 5, 6, 3, 6, 13, 6, 2, 4, 14, 4, 2
	DB	4, 12, 3, 5, 4, 3, 8, 6, 4, 5, 6, 4, 11, 3, 1
	DB	5, 1, 3, 5, 3, 3, 4, 3, 2, 7, 14, 4, 8, 9, 4
	DB	2, 3, 10, 2, 9, 3, 1, 12, 12, 3, 3, 6, 6, 2, 1
	DB	11, 1, 5, 3, 4, 6, 2, 10, 9, 3, 2, 6, 12, 3, 3
	DB	27, 4, 3, 2, 13, 18, 2, 1, 2, 13, 6, 6, 2, 3, 3
	DB	4, 6, 5, 1, 6, 8, 9, 3, 4, 3, 6, 9, 5, 1, 27
	DB	2, 1, 5, 15, 6, 4, 2, 4, 8, 7, 6, 3, 2, 3, 6
	DB	3, 1, 2, 7, 6, 2, 7, 3, 12, 3, 3, 5, 6, 6, 10
	DB	9, 3, 3, 8, 4, 2, 3, 10, 2, 16, 2, 7, 5, 1, 3
	DB	6, 8, 1, 2, 3, 6, 1, 5, 4, 3, 2, 1, 5, 7, 3
	DB	3, 6, 9, 17, 4, 5, 3, 12, 3, 1, 5, 6, 1, 15, 5
	DB	7, 6, 6, 8, 3, 3, 1, 9, 2, 3, 15, 7, 2, 3, 3
	DB	1, 3, 2, 3, 7, 3, 2, 4, 5, 6, 3, 16, 5, 4, 11
	DB	1, 5, 3, 12, 4, 2, 15, 3, 1, 6, 8, 4, 3, 2, 3
	DB	4, 8, 7, 3, 3, 2, 1, 5, 6, 1, 8, 7, 2, 1, 2
	DB	10, 9, 5, 1, 5, 3, 6, 15, 4, 9, 6, 5, 1, 3, 3
	DB	2, 6, 6, 1, 2, 6, 9, 12, 1, 5, 3, 4, 8, 4, 3
	DB	6, 5, 7, 3, 6, 3, 3, 2, 1, 12, 2, 3, 4, 3, 2
	DB	1, 2, 3, 7, 2, 4, 5, 12, 12, 6, 1, 3, 6, 11, 15
	DB	1, 3, 9, 5, 3, 3, 4, 2, 1, 3, 5, 4, 5, 3, 4
	DB	8, 3, 7, 3, 2, 12, 4, 5, 1, 6, 3, 2, 18, 1, 11
	DB	3, 4, 3, 5, 4, 3, 6, 5, 7, 5, 3, 9, 6, 1, 6
	DB	2, 13, 5, 7, 8, 9, 4, 9, 6, 6, 3, 8, 7, 12, 5
	DB	6, 4, 11, 3, 1, 5, 30, 3, 1, 2, 4, 8, 7, 5, 3
	DB	12, 3, 6, 9, 12, 1, 15, 2, 1, 6, 3, 5, 1, 2, 7
	DB	3, 8, 1, 5, 4, 11, 10, 3, 2, 16, 3, 9, 2, 1, 2
	DB	1, 2, 4, 26, 7, 11, 1, 11, 10, 5, 4, 5, 1, 3, 2
	DB	7, 2, 3, 10, 2, 3, 1, 6, 6, 3, 6, 8, 1, 6, 5
	DB	4, 2, 3, 1, 14, 6, 4, 5, 6, 1, 2, 7, 14, 4, 3
	DB	2, 1, 2, 3, 1, 6, 29, 3, 7, 5, 1, 3, 14, 16, 2
	DB	15, 4, 3, 2, 3, 6, 6, 1, 2, 3, 3, 7, 8, 4, 15
	DB	2, 1, 5, 4, 3, 2, 3, 13, 2, 6, 1, 5, 9, 6, 6
	DB	9, 1, 2, 6, 4, 6, 5, 10, 2, 4, 8, 6, 4, 3, 8
	DB	4, 5, 6, 7, 3, 2, 4, 6, 2, 10, 3, 20, 4, 8, 3
	DB	18, 1, 3, 2, 3, 1, 11, 9, 1, 5, 3, 18, 7, 6, 2
	DB	9, 4, 2, 7, 5, 1, 5, 4, 2, 1, 9, 8, 6, 7, 5
	DB	7, 3, 3, 21, 5, 3, 3, 10, 5, 4, 6, 2, 6, 9, 1
	DB	5, 7, 9, 5, 9, 4, 3, 2, 7, 3, 5, 15, 7, 3, 3
	DB	2, 6, 19, 2, 1, 2, 3, 4, 6, 5, 3, 9, 3, 25, 3
	DB	2, 3, 6, 4, 5, 16, 3, 11, 1, 5, 6, 9, 1, 3, 2
	DB	15, 4, 3, 3, 9, 5, 1, 2, 6, 10, 5, 4, 12, 5, 1
	DB	3, 11, 3, 1, 9, 5, 6, 1, 15, 9, 6, 14, 1, 3, 2
	DB	3, 7, 3, 6, 5, 4, 2, 6, 13, 5, 4, 3, 8, 1, 5
	DB	9, 7, 3, 2, 3, 7, 8, 1, 3, 2, 6, 10, 2, 10, 2
	DB	3, 6, 1, 18, 2, 3, 1, 5, 1, 11, 4, 3, 5, 6, 6
	DB	9, 7, 12, 18, 2, 10, 12, 5, 3, 1, 14, 3, 9, 4, 2
	DB	3, 4, 3, 2, 1, 6, 14, 9, 7, 8, 7, 9, 5, 4, 3
	DB	2, 3, 3, 4, 11, 6, 1, 5, 9, 3, 1, 9, 5, 1, 6
	DB	5, 9, 16, 3, 2, 3, 3, 4, 3, 3, 5, 10, 3, 6, 5
	DB	4, 5, 7, 3, 5, 7, 2, 1, 11, 9, 1, 5, 1, 2, 10
	DB	2, 1, 17, 1, 6, 3, 5, 1, 5, 9, 3, 7, 6, 6, 11
	DB	4, 3, 8, 3, 4, 2, 6, 3, 4, 2, 18, 3, 3, 10, 12
	DB	3, 6, 9, 5, 1, 5, 13, 3, 8, 4, 3, 2, 12, 9, 4
	DB	6, 6, 5, 9, 6, 1, 12, 2, 6, 9, 6, 7, 5, 1, 2
	DB	12, 6, 7, 5, 3, 1, 3, 2, 3, 13, 2, 3, 3, 1, 11
	DB	4, 9, 2, 9, 4, 2, 12, 1, 6, 6, 2, 1, 26, 1, 9
	DB	3, 2, 3, 6, 1, 3, 6, 5, 4, 2, 1, 12, 5, 1, 5
	DB	1, 6, 3, 9, 20, 3, 10, 8, 1, 6, 3, 5, 6, 1, 2
	DB	3, 7, 6, 6, 11, 3, 4, 2, 1, 8, 9, 6, 1, 3, 8
	DB	3, 1, 3, 2, 6, 15, 4, 8, 1, 9, 5, 12, 1, 3, 12
	DB	2, 1, 11, 1, 8, 1, 3, 6, 2, 9, 4, 2, 7, 2, 9
	DB	12, 3, 1, 3, 5, 1, 5, 19, 3, 5, 7, 3, 3, 12, 2
	DB	1, 6, 8, 7, 8, 6, 1, 3, 5, 13, 2, 1, 6, 3, 2
	DB	6, 4, 6, 5, 9, 3, 7, 14, 1, 3, 5, 1, 2, 7, 17
	DB	1, 3, 11, 1, 5, 7, 2, 1, 8, 4, 5, 3, 4, 5, 4
	DB	2, 3, 1, 8, 3, 3, 9, 15, 7, 3, 2, 15, 1, 5, 7
	DB	2, 10, 5, 4, 2, 4, 9, 2, 7, 3, 2, 12, 3, 3, 9
	DB	9, 1, 18, 3, 5, 7, 6, 2, 3, 1, 15, 3, 2, 1, 3
	DB	14, 10, 2, 10, 6, 12, 8, 9, 6, 7, 3, 2, 6, 16, 6
	DB	3, 5, 4, 5, 3, 9, 1, 8, 7, 3, 11, 3, 6, 1, 9
	DB	2, 4, 15, 6, 2, 6, 1, 5, 19, 11, 1, 2, 7, 3, 6
	DB	12, 2, 1, 2, 7, 6, 5, 1, 8, 3, 10, 2, 10, 11, 6
	DB	1, 2, 1, 6, 11, 12, 3, 3, 1, 3, 2, 3, 1, 5, 6
	DB	6, 3, 1, 3, 8, 4, 3, 2, 9, 6, 6, 7, 2, 6, 3
	DB	4, 3, 9, 3, 5, 6, 7, 3, 2, 4, 11, 3, 1, 14, 9
	DB	1, 9, 5, 3, 7, 5, 1, 5, 7, 3, 5, 1, 11, 3, 4
	DB	3, 8, 6, 4, 11, 1, 2, 7, 9, 6, 3, 12, 3, 5, 1
	DB	6, 11, 9, 3, 10, 3, 5, 7, 2, 1, 3, 6, 11, 7, 6
	DB	2, 3, 4, 11, 1, 5, 6, 4, 20, 1, 3, 5, 4, 2, 21
	DB	10, 2, 16, 6, 5, 3, 6, 6, 1, 5, 4, 3, 2, 4, 2
	DB	13, 9, 2, 4, 14, 3, 9, 3, 6, 1, 5, 3, 3, 7, 5
	DB	6, 7, 12, 3, 2, 10, 11, 1, 9, 2, 3, 6, 1, 8, 9
	DB	7, 3, 3, 2, 3, 4, 9, 2, 7, 15, 2, 9, 4, 5, 1
	DB	2, 4, 6, 2, 6, 9, 1, 6, 5, 1, 8, 4, 2, 15, 1
	DB	3, 14, 1, 5, 1, 9, 5, 7, 2, 13, 3, 9, 2, 10, 3
	DB	2, 4, 9, 2, 6, 13, 12, 2, 10, 11, 1, 9, 11, 1, 2
	DB	6, 1, 3, 3, 3, 2, 3, 7, 2, 12, 6, 3, 9, 1, 6
	DB	14, 7, 2, 3, 4, 11, 3, 6, 9, 4, 2, 10, 3, 2, 3
	DB	1, 9, 3, 2, 6, 6, 4, 14, 3, 4, 5, 1, 12, 6, 5
	DB	12, 4, 5, 10, 6, 3, 6, 6, 2, 7, 6, 12, 17, 9, 4
	DB	5, 3, 9, 4, 2, 4, 8, 7, 3, 2, 3, 12, 1, 3, 2
	DB	3, 1, 8, 3, 3, 10, 12, 2, 1, 2, 7, 2, 9, 1, 3
	DB	6, 2, 7, 2, 1, 9, 8, 3, 3, 1, 8, 10, 3, 3, 15
	DB	2, 4, 3, 12, 8, 3, 3, 4, 6, 15, 2, 9, 9, 4, 2
	DB	13, 5, 1, 11, 4, 5, 7, 3, 2, 9, 4, 6, 14, 1, 3
	DB	2, 6, 3, 12, 3, 4, 5, 10, 8, 4, 15, 3, 3, 2, 1
	DB	5, 7, 3, 5, 16, 11, 9, 1, 2, 1, 2, 4, 11, 4, 9
	DB	6, 14, 1, 8, 6, 9, 7, 5, 9, 6, 3, 16, 5, 7, 3
	DB	5, 1, 5, 1, 3, 11, 1, 2, 3, 4, 5, 3, 7, 3, 2
	DB	6, 15, 12, 3, 3, 4, 3, 2, 1, 2, 3, 4, 3, 3, 11
	DB	9, 4, 2, 1, 9, 3, 2, 1, 8, 9, 10, 5, 3, 3, 15
	DB	1, 6, 14, 3, 3, 3, 1, 6, 5, 4, 9, 9, 2, 4, 9
	DB	5, 1, 14, 1, 5, 7, 2, 1, 15, 6, 11, 13, 5, 4, 3
	DB	5, 4, 8, 7, 3, 3, 5, 7, 3, 2, 1, 5, 6, 1, 3
	DB	5, 4, 2, 1, 5, 13, 11, 3, 1, 6, 9, 2, 13, 2, 4
	DB	5, 3, 7, 5, 1, 9, 3, 5, 10, 3, 3, 2, 12, 1, 2
	DB	4, 3, 8, 7, 8, 9, 1, 2, 6, 1, 5, 1, 3, 6, 5
	DB	3, 3, 10, 3, 2, 3, 19, 2, 3, 6, 7, 2, 6, 4, 5
	DB	6, 6, 4, 2, 3, 7, 5, 3, 6, 1, 5, 9, 1, 9, 5
	DB	4, 5, 1, 6, 2, 7, 14, 1, 8, 1, 9, 3, 5, 3, 4
	DB	8, 7, 15, 5, 10, 3, 5, 12, 1, 14, 1, 6, 8, 3, 4
	DB	18, 2, 4, 2, 7, 6, 5, 4, 6, 2, 3, 4, 2, 3, 7
	DB	11, 4, 3, 2, 1, 5, 3, 10, 5, 4, 3, 3, 11, 9, 1
	DB	8, 3, 10, 2, 13, 2, 7, 11, 7, 2, 6, 3, 4, 2, 3
	DB	3, 13, 5, 1, 9, 9, 2, 1, 8, 1, 9, 2, 3, 4, 2
	DB	3, 6, 1, 3, 3, 14, 19, 2, 4, 8, 13, 2, 1, 5, 6
	DB	1, 5, 4, 3, 5, 6, 1, 5, 1, 12, 2, 15, 13, 3, 3
	DB	9, 3, 3, 11, 1, 5, 9, 13, 2, 9, 4, 3, 3, 6, 8
	DB	3, 4, 8, 3, 4, 8, 1, 21, 29, 4, 2, 3, 1, 2, 4
	DB	8, 3, 10, 2, 6, 6, 3, 6, 1, 5, 1, 3, 11, 1, 5
	DB	3, 4, 3, 5, 7, 3, 3, 2, 9, 4, 5, 4, 8, 7, 5
	DB	1, 5, 1, 6, 3, 2, 10, 5, 4, 26, 4, 5, 3, 1, 5
	DB	4, 5, 3, 3, 4, 5, 1, 11, 1, 2, 3, 7, 2, 1, 12
	DB	6, 2, 13, 9, 2, 3, 7, 15, 3, 2, 3, 1, 11, 4, 2
	DB	3, 1, 11, 3, 4, 8, 3, 7, 2, 3, 9, 4, 6, 3, 6
	DB	12, 15, 8, 4, 17, 4, 11, 3, 7, 5, 9, 7, 2, 6, 4
	DB	2, 18, 3, 3, 1, 5, 1, 2, 10, 3, 3, 5, 6, 3, 1
	DB	20, 4, 3, 14, 3, 1, 6, 9, 2, 12, 7, 3, 3, 5, 10
	DB	5, 7, 8, 7, 8, 3, 4, 18, 2, 6, 6, 3, 6, 25, 6
	DB	3, 2, 3, 3, 4, 3, 5, 1, 5, 1, 9, 5, 7, 8, 4
	DB	3, 2, 10, 2, 1, 5, 3, 7, 9, 5, 19, 5, 9, 1, 5
	DB	1, 6, 2, 1, 2, 7, 3, 5, 4, 20, 3, 10, 2, 6, 4
	DB	3, 17, 4, 11, 4, 6, 5, 1, 8, 21, 6, 4, 11, 4, 11
	DB	4, 3, 17, 1, 3, 2, 7, 3, 8, 1, 11, 3, 4, 12, 11
	DB	3, 1, 6, 2, 3, 7, 2, 4, 12, 2, 3, 3, 1, 11, 10
	DB	3, 2, 7, 2, 3, 3, 4, 3, 5, 3, 4, 3, 8, 7, 3
	DB	3, 11, 3, 12, 16, 3, 9, 3, 9, 5, 4, 15, 9, 3, 8
	DB	6, 3, 6, 1, 3, 2, 6, 4, 3, 11, 4, 3, 2, 7, 5
	DB	9, 10, 5, 1, 3, 2, 1, 14, 9, 1, 5, 3, 3, 3, 7
	DB	20, 12, 1, 2, 4, 6, 2, 10, 2, 16, 9, 8, 3, 18, 4
	DB	3, 2, 3, 7, 2, 3, 13, 3, 5, 7, 9, 5, 3, 3, 7
	DB	5, 3, 3, 7, 3, 12, 2, 7, 11, 4, 6, 5, 4, 6, 9
	DB	5, 9, 4, 12, 5, 4, 2, 12, 3, 9, 3, 1, 5, 15, 1
	DB	5, 1, 2, 1, 20, 1, 14, 4, 3, 3, 9, 3, 5, 7, 2
	DB	9, 15, 9, 1, 6, 15, 3, 15, 2, 9, 6, 1, 2, 7, 3
	DB	5, 3, 4, 3, 5, 6, 1, 3, 6, 5, 1, 9, 2, 10, 2
	DB	3, 7, 3, 3, 11, 3, 3, 4, 9, 9, 5, 1, 5, 1, 3
	DB	2, 3, 6, 9, 1, 5, 4, 2, 9, 1, 3, 3, 3, 5, 4
	DB	5, 3, 9, 6, 4, 6, 3, 2, 3, 7, 8, 1, 6, 2, 3
	DB	19, 3, 3, 8, 10, 14, 10, 5, 3, 3, 7, 2, 13, 2, 7
	DB	5, 9, 7, 14, 1, 2, 7, 8, 1, 14, 3, 4, 3, 17, 4
	DB	2, 9, 1, 8, 4, 3, 20, 4, 9, 2, 15, 3, 6, 1, 15
	DB	3, 5, 7, 20, 7, 5, 1, 6, 5, 4, 2, 4, 3, 3, 14
	DB	1, 2, 6, 7, 8, 4, 15, 8, 9, 1, 5, 9, 3, 16, 2
	DB	9, 3, 1, 6, 5, 9, 1, 3, 5, 7, 9, 14, 3, 4, 8
	DB	1, 2, 10, 5, 4, 9, 5, 1, 5, 4, 2, 3, 6, 3, 10
	DB	2, 1, 3, 2, 10, 5, 13, 9, 5, 1, 9, 3, 8, 7, 2
	DB	13, 2, 7, 5, 6, 7, 3, 3, 2, 7, 5, 1, 15, 9, 11
	DB	1, 8, 1, 2, 4, 3, 3, 8, 1, 3, 6, 5, 4, 6, 2
	DB	7, 2, 3, 10, 5, 6, 1, 3, 3, 2, 1, 5, 1, 15, 8
	DB	6, 10, 9, 2, 3, 1, 2, 4, 8, 7, 9, 11, 3, 1, 11
	DB	3, 3, 9, 1, 5, 18, 4, 2, 3, 10, 2, 6, 3, 7, 2
	DB	1, 14, 12, 4, 2, 3, 6, 15, 9, 16, 11, 4, 18, 3, 2
	DB	6, 1, 6, 2, 3, 10, 5, 9, 9, 4, 3, 2, 12, 4, 5
	DB	7, 3, 2, 4, 6, 8, 1, 8, 3, 4, 8, 6, 7, 5, 15
	DB	7, 2, 6, 4, 6, 3, 5, 1, 6, 14, 3, 6, 6, 10, 5
	DB	1, 5, 7, 3, 3, 15, 2, 4, 6, 2, 1, 5, 7, 2, 13
	DB	9, 6, 5, 3, 4, 2, 6, 3, 12, 9, 4, 5, 1, 6, 2
	DB	6, 6, 3, 1, 11, 1, 2, 1, 6, 8, 7, 5, 1, 8, 9
	DB	16, 2, 3, 10, 11, 4, 5, 1, 5, 3, 1, 2, 7, 3, 12
	DB	2, 4, 2, 3, 6, 6, 4, 3, 5, 6, 4, 5, 1, 5, 6
	DB	3, 6, 6, 10, 14, 10, 5, 7, 5, 4, 5, 3, 1, 2, 7
	DB	3, 3, 6, 3, 6, 5, 7, 5, 7, 8, 4, 5, 13, 2, 1
	DB	3, 2, 7, 2, 3, 6, 4, 3, 15, 9, 6, 3, 6, 8, 6
	DB	6, 1, 14, 3, 7, 5, 18, 1, 2, 3, 4, 6, 11, 9, 1
	DB	15, 9, 11, 10, 9, 5, 19, 3, 2, 1, 12, 2, 3, 3, 1
	DB	5, 3, 7, 5, 4, 2, 12, 7, 8, 7, 11, 3, 10, 5, 7
	DB	2, 6, 6, 1, 8, 4, 3, 3, 9, 2, 3, 7, 11, 3, 1
	DB	21, 8, 1, 5, 3, 1, 2, 3, 4, 5, 10, 8, 15, 4, 5
	DB	4, 5, 1, 15, 3, 3, 18, 5, 4, 8, 3, 1, 6, 14, 1
	DB	2, 3, 9, 6, 3, 4, 5, 1, 2, 25, 2, 10, 2, 15, 4
	DB	2, 3, 6, 1, 12, 2, 4, 9, 3, 2, 3, 4, 5, 1, 2
	DB	1, 20, 9, 18, 15, 15, 4, 8, 7, 3, 6, 14, 1, 11, 1
	DB	2, 6, 15, 6, 3, 1, 2, 7, 5, 1, 9, 11, 6, 9, 1
	DB	5, 9, 16, 3, 2, 1, 3, 5, 10, 6, 5, 3, 6, 10, 6
	DB	3, 2, 1, 8, 1, 8, 3, 7, 2, 1, 8, 1, 3, 8, 3
	DB	4, 2, 4, 11, 9, 4, 6, 2, 4, 3, 12, 11, 3, 1, 6
	DB	15, 3, 5, 6, 3, 1, 11, 3, 1, 6, 3, 11, 4, 6, 11
	DB	1, 5, 3, 9, 6, 1, 3, 6, 9, 3, 2, 10, 11, 4, 6
	DB	12, 8, 7, 5, 15, 9, 1, 3, 2, 7, 5, 1, 6, 5, 6
	DB	3, 1, 8, 6, 1, 3, 6, 5, 1, 5, 3, 1, 6, 6, 8
	DB	10, 5, 6, 4, 15, 5, 7, 2, 3, 4, 3, 2, 10, 9, 12
	DB	2, 6, 4, 2, 1, 12, 3, 12, 5, 1, 2, 3, 1, 3, 3
	DB	3, 2, 12, 1, 5, 6, 1, 3, 5, 4, 3, 5, 9, 1, 3
	DB	2, 10, 12, 5, 6, 1, 6, 3, 12, 2, 18, 7, 8, 4, 11
	DB	3, 4, 2, 1, 3, 11, 10, 8, 6, 9, 1, 6, 8, 3, 3
	DB	6, 3, 6, 1, 3, 6, 5, 4, 8, 4, 3, 8, 4, 6, 2
	DB	3, 3, 10, 6, 6, 2, 3, 10, 2, 6, 1, 5, 1, 3, 15
	DB	11, 3, 1, 2, 19, 5, 1, 2, 1, 11, 1, 8, 1, 3, 5
	DB	10, 3, 12, 2, 6, 7, 6, 2, 19, 5, 15, 3, 1, 6, 6
	DB	2, 3, 15, 7, 2, 4, 9, 18, 2, 3, 10, 2, 1, 6, 5
	DB	1, 3, 5, 6, 3, 6, 4, 3, 3, 12, 2, 15, 10, 3, 18
	DB	5, 1, 6, 3, 2, 4, 3, 2, 6, 4, 3, 6, 2, 3, 7
	DB	2, 10, 6, 2, 3, 9, 1, 2, 9, 1, 8, 6, 15, 3, 3
	DB	4, 20, 4, 24, 3, 8, 9, 7, 6, 3, 9, 2, 10, 5, 1
	DB	3, 5, 4, 15, 2, 6, 10, 3, 6, 3, 3, 17, 3, 3, 9
	DB	3, 4, 5, 6, 3, 4, 5, 1, 2, 12, 3, 4, 11, 3, 1
	DB	6, 3, 5, 6, 3, 12, 3, 7, 6, 18, 2, 12, 1, 5, 4
	DB	5, 3, 7, 5, 16, 2, 4, 5, 6, 13, 9, 2, 3, 10, 2
	DB	10, 3, 8, 3, 1, 15, 6, 3, 5, 1, 3, 5, 6, 4, 2
	DB	1, 3, 5, 6, 13, 11, 4, 3, 2, 7, 3, 3, 15, 2, 3
	DB	7, 2, 1, 14, 1, 3, 11, 4, 2, 9, 9, 9, 1, 6, 3
	DB	2, 10, 5, 3, 3, 7, 5, 6, 1, 6, 15, 17, 6, 4, 3
	DB	2, 1, 5, 1, 8, 6, 1, 5, 4, 9, 12, 3, 2, 6, 7
	DB	2, 4, 2, 7, 2, 3, 3, 10, 3, 2, 4, 9, 26, 1, 2
	DB	6, 4, 2, 19, 2, 13, 12, 8, 6, 3, 1, 6, 6, 8, 1
	DB	3, 3, 2, 6, 7, 8, 4, 6, 9, 8, 3, 4, 5, 3, 7
	DB	5, 6, 1, 5, 1, 2, 12, 3, 21, 12, 4, 5, 3, 3, 3
	DB	1, 6, 2, 7, 3, 3, 14, 3, 1, 5, 6, 6, 3, 10, 2
	DB	3, 7, 2, 1, 6, 5, 6, 12, 3, 4, 3, 3, 2, 12, 6
	DB	10, 8, 7, 15, 9, 3, 2, 13, 6, 2, 3, 1, 3, 2, 1
	DB	14, 4, 20, 1, 5, 4, 2, 10, 3, 9, 5, 1, 2, 22, 3
	DB	9, 6, 3, 2, 3, 1, 11, 3, 7, 15, 5, 12, 1, 5, 4
	DB	8, 9, 1, 9, 11, 4, 5, 3, 3, 7, 2, 4, 9, 2, 1
	DB	9, 9, 9, 3, 2, 12, 9, 1, 8, 3, 3, 9, 10, 8, 10
	DB	2, 7, 3, 2, 10, 9, 5, 1, 3, 5, 12, 1, 5, 12, 3
	DB	3, 12, 3, 6, 1, 14, 6, 7, 3, 3, 6, 3, 11, 6, 6
	DB	4, 18, 2, 6, 7, 2, 10, 5, 6, 12, 1, 2, 3, 6, 1
	DB	2, 1, 5, 6, 13, 3, 8, 4, 2, 4, 5, 4, 3, 17, 1
	DB	6, 8, 12, 3, 1, 5, 1, 9, 2, 4, 3, 8, 3, 1, 3
	DB	3, 3, 2, 7, 2, 10, 3, 2, 10, 3, 6, 11, 3, 1, 5
	DB	6, 1, 3, 2, 4, 6, 2, 7, 6, 5, 7, 2, 6, 13, 5
	DB	7, 2, 13, 3, 15, 2, 9, 9, 4, 3, 8, 4, 5, 7, 5
	DB	4, 5, 10, 11, 10, 8, 1, 9, 3, 2, 3, 3, 6, 1, 5
	DB	13, 2, 4, 9, 9, 3, 9, 3, 2, 3, 12, 3, 10, 17, 13
; .416 sec
	DB	5, 1, 14, 6, 4, 5, 6, 1, 3, 11, 1, 6, 8, 1, 3
	DB	3, 5, 7, 8, 10, 3, 2, 19, 3, 5, 3, 4, 8, 21, 1
	DB	3, 2, 3, 3, 3, 7, 8, 7, 2, 10, 5, 1, 2, 4, 9
	DB	5, 6, 18, 1, 5, 21, 4, 2, 10, 12, 8, 4, 11, 3, 4
	DB	2, 1, 3, 11, 3, 3, 4, 14, 1, 5, 9, 7, 3, 2, 9
	DB	4, 5, 7, 2, 6, 4, 5, 6, 7, 2, 1, 6, 6, 2, 3
	DB	9, 15, 6, 19, 3, 6, 5, 1, 9, 5, 6, 4, 2, 4, 3
	DB	2, 1, 12, 6, 9, 2, 1, 2, 1, 29, 6, 4, 12, 5, 1
	DB	2, 3, 3, 6, 1, 2, 7, 3, 3, 8, 6, 1, 2, 16, 2
	DB	12, 3, 3, 4, 5, 1, 11, 9, 6, 10, 3, 15, 2, 15, 3
	DB	1, 2, 7, 3, 2, 7, 8, 1, 6, 5, 1, 3, 6, 6, 5
	DB	3, 4, 11, 4, 6, 6, 3, 8, 3, 9, 10, 11, 9, 1, 11
	DB	1, 8, 1, 11, 7, 5, 10, 5, 16, 2, 4, 5, 3, 1, 11
	DB	3, 6, 1, 3, 2, 1, 2, 7, 6, 12, 5, 1, 6, 8, 1
	DB	2, 3, 7, 3, 5, 6, 1, 8, 7, 17, 6, 1, 3, 3, 3
	DB	2, 10, 5, 13, 6, 6, 2, 1, 2, 4, 5, 1, 2, 1, 11
	DB	3, 3, 7, 2, 9, 6, 13, 3, 5, 4, 8, 1, 2, 10, 5
	DB	3, 21, 1, 5, 3, 4, 12, 6, 3, 2, 3, 6, 1, 14, 4
	DB	6, 9, 9, 3, 23, 4, 5, 3, 7, 2, 1, 3, 2, 3, 21
	DB	4, 5, 4, 5, 1, 9, 2, 3, 6, 6, 1, 2, 10, 5, 6
	DB	6, 4, 2, 13, 9, 11, 4, 3, 8, 7, 8, 1, 9, 5, 1
	DB	3, 3, 5, 7, 2, 1, 15, 2, 1, 2, 4, 5, 3, 1, 6
	DB	8, 3, 28, 5, 1, 6, 5, 4, 6, 3, 2, 7, 5, 1, 2
	DB	4, 3, 2, 10, 3, 6, 11, 3, 16, 5, 1, 5, 6, 7, 3
	DB	14, 18, 3, 3, 1, 6, 2, 3, 3, 4, 11, 1, 9, 5, 1
	DB	3, 2, 10, 5, 4, 2, 3, 7, 9, 3, 21, 11, 1, 2, 1
	DB	14, 1, 2, 9, 3, 3, 3, 6, 1, 12, 5, 18, 3, 1, 6
	DB	5, 13, 12, 9, 8, 3, 3, 7, 12, 6, 2, 4, 3, 6, 2
	DB	4, 8, 10, 20, 13, 2, 6, 1, 3, 2, 1, 5, 7, 5, 1
	DB	2, 13, 6, 14, 1, 8, 13, 3, 5, 1, 3, 5, 3, 4, 3
	DB	3, 3, 5, 6, 3, 10, 20, 10, 2, 1, 8, 6, 3, 6, 4
	DB	2, 9, 1, 6, 5, 13, 6, 8, 1, 9, 12, 6, 2, 7, 11
	DB	10, 5, 7, 6, 2, 9, 6, 4, 5, 6, 3, 15, 7, 2, 12
	DB	3, 15, 3, 3, 1, 3, 11, 16, 3, 2, 3, 3, 10, 8, 1
	DB	5, 4, 6, 5, 1, 3, 5, 4, 8, 18, 4, 3, 2, 1, 14
	DB	1, 14, 6, 1, 5, 3, 7, 5, 3, 3, 3, 4, 3, 2, 7
	DB	9, 2, 3, 6, 1, 5, 9, 4, 15, 20, 1, 9, 2, 3, 7
	DB	9, 3, 2, 6, 3, 6, 3, 7, 5, 13, 3, 8, 1, 8, 15
	DB	1, 5, 1, 21, 3, 14, 7, 3, 5, 1, 6, 9, 6, 3, 5
	DB	6, 6, 10, 3, 2, 1, 5, 3, 6, 6, 7, 6, 17, 3, 1
	DB	6, 5, 3, 4, 3, 2, 6, 19, 3, 5, 9, 1, 14, 1, 3
	DB	6, 15, 8, 1, 5, 4, 2, 1, 8, 9, 13, 2, 3, 4, 9
	DB	11, 3, 10, 2, 3, 6, 1, 3, 6, 2, 9, 3, 1, 11, 6
	DB	4, 3, 8, 9, 15, 6, 12, 1, 5, 1, 3, 3, 2, 3, 18
	DB	7, 3, 11, 1, 29, 4, 6, 3, 5, 1, 20, 4, 3, 14, 1
	DB	2, 7, 3, 3, 9, 5, 4, 2, 7, 2, 4, 15, 2, 3, 4
	DB	3, 3, 9, 2, 1, 2, 7, 6, 9, 5, 1, 2, 6, 1, 5
	DB	4, 5, 7, 5, 9, 6, 4, 3, 5, 7, 5, 4, 11, 1, 3
	DB	11, 6, 3, 4, 6, 14, 1, 24, 6, 2, 9, 4, 5, 7, 5
	DB	7, 2, 6, 15, 12, 3, 4, 3, 2, 4, 27, 2, 1, 5, 6
	DB	4, 5, 6, 6, 9, 1, 12, 2, 4, 11, 6, 10, 2, 6, 1
	DB	6, 8, 1, 14, 1, 3, 12, 5, 1, 14, 1, 2, 10, 2, 6
	DB	3, 7, 2, 3, 7, 11, 12, 10, 2, 7, 3, 3, 5, 15, 4
	DB	5, 9, 1, 3, 3, 8, 1, 3, 3, 2, 1, 12, 2, 1, 12
	DB	5, 3, 1, 5, 1, 3, 11, 4, 2, 4, 3, 2, 9, 1, 9
	DB	2, 4, 8, 13, 2, 3, 4, 11, 10, 8, 4, 2, 3, 12, 3
	DB	7, 6, 8, 1, 6, 2, 7, 5, 1, 2, 6, 9, 16, 5, 7
	DB	12, 6, 20, 4, 17, 6, 7, 2, 9, 1, 14, 6, 10, 3, 5
	DB	1, 20, 9, 7, 6, 2, 18, 3, 1, 11, 3, 7, 5, 12, 21
	DB	1, 8, 1, 17, 4, 3, 2, 1, 2, 7, 20, 4, 6, 3, 12
	DB	9, 2, 3, 1, 3, 2, 1, 2, 1, 12, 5, 4, 3, 3, 5
	DB	7, 3, 8, 9, 7, 9, 12, 2, 3, 3, 4, 2, 10, 5, 3
	DB	6, 1, 6, 2, 7, 3, 3, 3, 2, 7, 8, 18, 7, 3, 2
	DB	7, 2, 3, 12, 4, 2, 10, 5, 7, 6, 17, 4, 5, 3, 3
	DB	3, 7, 2, 7, 6, 3, 5, 9, 7, 5, 6, 3, 1, 3, 3
	DB	14, 1, 2, 12, 3, 1, 2, 4, 8, 3, 10, 2, 1, 5, 1
	DB	5, 4, 32, 3, 4, 6, 2, 7, 6, 5, 1, 6, 3, 5, 9
	DB	12, 3, 1, 5, 4, 3, 8, 10, 2, 7, 3, 3, 6, 3, 2
	DB	3, 1, 2, 4, 11, 3, 4, 2, 1, 8, 9, 7, 3, 11, 7
	DB	5, 7, 2, 3, 1, 2, 7, 5, 6, 4, 8, 4, 5, 4, 12
	DB	20, 3, 6, 1, 3, 9, 2, 1, 2, 15, 1, 15, 2, 4, 9
	DB	6, 6, 2, 1, 2, 7, 18, 8, 9, 1, 6, 5, 3, 6, 9
	DB	1, 9, 3, 3, 11, 9, 19, 3, 5, 9, 1, 5, 4, 3, 8
	DB	12, 7, 3, 2, 3, 7, 8, 12, 3, 6, 4, 6, 5, 7, 23
	DB	1, 8, 1, 11, 3, 1, 5, 1, 5, 1, 3, 2, 10, 5, 3
	DB	15, 4, 3, 3, 2, 15, 4, 3, 3, 3, 11, 18, 1, 2, 4
	DB	3, 3, 2, 7, 6, 5, 10, 2, 1, 2, 15, 3, 7, 8, 6
	DB	15, 1, 2, 3, 4, 15, 5, 4, 17, 9, 6, 4, 11, 10, 2
	DB	7, 5, 10, 3, 2, 1, 5, 7, 2, 13, 3, 18, 6, 9, 2
	DB	4, 3, 2, 3, 1, 14, 3, 3, 12, 4, 5, 13, 3, 12, 2
	DB	4, 12, 5, 10, 2, 1, 5, 7, 8, 1, 3, 3, 2, 3, 4
	DB	9, 14, 7, 3, 8, 7, 3, 2, 3, 3, 4, 2, 1, 2, 6
	DB	1, 6, 3, 6, 14, 1, 3, 6, 5, 7, 2, 22, 3, 5, 1
	DB	6, 6, 15, 2, 6, 1, 3, 5, 6, 1, 5, 1, 5, 3, 4
	DB	5, 3, 7, 8, 4, 3, 6, 5, 1, 5, 4, 6, 5, 9, 4
	DB	2, 1, 2, 13, 3, 11, 3, 7, 5, 3, 1, 14, 3, 4, 23
	DB	3, 3, 9, 3, 3, 4, 3, 5, 9, 1, 3, 6, 9, 5, 4
	DB	6, 15, 5, 1, 5, 1, 2, 3, 9, 1, 2, 10, 6, 2, 3
	DB	4, 17, 3, 3, 12, 6, 4, 18, 8, 1, 3, 2, 1, 2, 3
	DB	10, 3, 12, 2, 1, 2, 9, 10, 3, 11, 4, 23, 9, 1, 8
	DB	10, 11, 1, 12, 11, 1, 8, 12, 10, 8, 1, 2, 4, 5, 1
	DB	5, 7, 2, 4, 9, 2, 4, 2, 7, 5, 1, 12, 8, 4, 3
	DB	8, 10, 5, 1, 3, 2, 15, 1, 8, 16, 3, 6, 5, 12, 4
	DB	6, 9, 8, 1, 6, 3, 2, 6, 3, 1, 14, 9, 1, 11, 3
	DB	3, 3, 1, 3, 8, 7, 3, 15, 8, 1, 5, 1, 2, 6, 1
	DB	6, 5, 7, 3, 5, 4, 14, 1, 18, 3, 8, 7, 2, 10, 12
	DB	3, 2, 4, 2, 9, 4, 2, 7, 2, 3, 1, 12, 8, 7, 2
	DB	13, 8, 1, 5, 16, 3, 2, 3, 6, 3, 18, 4, 6, 2, 1
	DB	2, 4, 3, 2, 10, 6, 5, 12, 6, 1, 6, 5, 3, 6, 1
	DB	3, 9, 2, 3, 3, 3, 4, 12, 3, 5, 6, 15, 7, 5, 4
	DB	6, 3, 5, 6, 1, 9, 3, 2, 4, 2, 12, 10, 2, 4, 5
; .415 sec
DB 0
IFDEF FOO
	DB	6, 4, 6, 8, 3, 7, 2, 4, 2, 9, 25, 3, 3, 2, 3
	DB	4, 3, 5, 13, 5, 3, 1, 5, 1, 5, 3, 19, 6, 2, 4
	DB	5, 10, 3, 3, 3, 9, 5, 1, 6, 8, 1, 6, 6, 2, 13
	DB	5, 3, 10, 9, 20, 6, 4, 5, 6, 1, 9, 6, 5, 1, 5
	DB	13, 2, 3, 6, 4, 2, 15, 3, 1, 3, 8, 12, 12, 9, 6
	DB	6, 4, 3, 2, 4, 5, 4, 3, 2, 10, 5, 13, 2, 12, 3
	DB	1, 6, 21, 9, 3, 2, 13, 3, 14, 3, 1, 5, 4, 3, 3
	DB	5, 4, 5, 1, 11, 1, 2, 10, 2, 3, 18, 7, 2, 10, 11
	DB	3, 7, 3, 5, 4, 2, 1, 2, 7, 9, 17, 4, 11, 7, 5
	DB	12, 3, 1, 5, 1, 3, 5, 13, 9, 5, 9, 12, 9, 1, 12
	DB	20, 1, 2, 3, 1, 3, 5, 13, 3, 6, 6, 3, 2, 18, 1
	DB	5, 6, 12, 1, 2, 4, 5, 3, 1, 2, 12, 1, 2, 18, 1
	DB	11, 7, 12, 9, 21, 3, 5, 1, 12, 8, 6, 1, 2, 1, 5
	DB	1, 5, 4, 2, 18, 4, 2, 6, 9, 3, 3, 7, 11, 1, 3
	DB	12, 3, 5, 12, 10, 11, 3, 7, 18, 14, 3, 4, 3, 12, 3
	DB	6, 14, 1, 9, 2, 1, 2, 10, 11, 4, 5, 1, 9, 2, 4
	DB	5, 7, 5, 3, 4, 3, 3, 6, 8, 6, 7, 5, 9, 1, 5
	DB	12, 12, 3, 6, 1, 11, 3, 10, 11, 1, 2, 6, 1, 3, 18
	DB	3, 11, 3, 1, 14, 6, 9, 1, 2, 7, 3, 2, 1, 5, 1
	DB	8, 1, 5, 4, 3, 5, 9, 6, 3, 7, 2, 3, 9, 6, 13
	DB	2, 3, 7, 3, 5, 6, 1, 2, 1, 5, 12, 4, 5, 16, 5
	DB	4, 5, 3, 1, 9, 6, 14, 15, 1, 9, 2, 3, 7, 3, 2
	DB	4, 11, 4, 15, 9, 5, 13, 2, 1, 11, 4, 2, 4, 3, 2
	DB	13, 2, 6, 10, 9, 3, 6, 5, 9, 1, 2, 3, 1, 6, 14
	DB	3, 10, 3, 8, 4, 3, 3, 2, 3, 10, 6, 3, 2, 10, 3
	DB	8, 3, 16, 5, 9, 1, 6, 8, 12, 3, 4, 6, 17, 3, 10
	DB	11, 1, 8, 7, 3, 2, 7, 3, 12, 15, 2, 4, 6, 3, 8
	DB	10, 5, 7, 2, 1, 8, 6, 1, 5, 4, 3, 15, 6, 5, 7
	DB	5, 4, 5, 3, 1, 2, 7, 5, 1, 5, 16, 9, 2, 4, 14
	DB	10, 2, 10, 3, 2, 15, 4, 3, 11, 9, 6, 1, 5, 6, 3
	DB	9, 3, 27, 3, 7, 2, 3, 3, 7, 12, 3, 6, 5, 6, 3
	DB	12, 6, 9, 4, 9, 2, 1, 2, 1, 11, 4, 5, 1, 6, 5
	DB	7, 3, 2, 1, 6, 23, 3, 3, 1, 3, 3, 15, 5, 4, 3
	DB	6, 2, 7, 3, 8, 6, 4, 5, 10, 9, 5, 3, 3, 6, 1
	DB	5, 7, 2, 1, 2, 12, 6, 4, 9, 2, 4, 8, 7, 6, 5
	DB	9, 6, 4, 9, 2, 7, 2, 3, 1, 11, 9, 4, 5, 13, 2
	DB	1, 12, 3, 2, 3, 6, 13, 2, 4, 5, 9, 3, 7, 5, 6
	DB	1, 6, 3, 2, 4, 6, 8, 1, 5, 3, 1, 11, 1, 2, 7
	DB	9, 5, 6, 10, 5, 3, 1, 2, 7, 6, 2, 4, 5, 1, 3
	DB	17, 3, 1, 8, 6, 4, 11, 4, 15, 5, 1, 12, 2, 7, 5
	DB	10, 8, 1, 2, 10, 5, 3, 4, 2, 4, 2, 12, 3, 7, 2
	DB	16, 2, 3, 7, 3, 2, 1, 11, 30, 1, 3, 12, 2, 7, 9
	DB	6, 9, 6, 3, 2, 15, 1, 8, 1, 11, 7, 8, 9, 3, 18
	DB	3, 9, 1, 5, 4, 2, 1, 3, 12, 9, 15, 3, 3, 17, 7
	DB	3, 26, 1, 8, 6, 7, 8, 4, 15, 2, 1, 3, 5, 6, 1
	DB	14, 3, 7, 6, 14, 9, 10, 2, 4, 3, 3, 2, 3, 4, 11
	DB	3, 12, 4, 3, 6, 6, 3, 6, 2, 10, 23, 9, 7, 5, 1
	DB	12, 2, 1, 12, 11, 7, 5, 3, 16, 6, 2, 1, 5, 1, 5
	DB	1, 2, 3, 3, 6, 6, 7, 3, 3, 14, 1, 9, 3, 3, 2
	DB	12, 1, 6, 6, 3, 2, 12, 12, 1, 3, 2, 19, 3, 5, 6
	DB	1, 6, 2, 10, 11, 6, 1, 5, 3, 9, 21, 6, 1, 3, 6
	DB	11, 7, 5, 3, 3, 1, 5, 9, 7, 2, 13, 8, 6, 4, 9
	DB	2, 1, 5, 4, 3, 2, 3, 3, 3, 7, 8, 1, 6, 3, 5
	DB	6, 1, 12, 2, 9, 1, 9, 5, 6, 10, 3, 8, 7, 3, 3
	DB	6, 8, 6, 10, 2, 4, 3, 17, 13, 5, 15, 3, 12, 3, 13
	DB	6, 8, 1, 5, 6, 1, 5, 3, 4, 21, 2, 4, 3, 2, 9
	DB	1, 3, 3, 3, 5, 4, 2, 3, 15, 6, 9, 12, 4, 2, 3
	DB	4, 5, 1, 2, 4, 8, 3, 19, 3, 3, 3, 15, 2, 7, 3
	DB	5, 4, 5, 13, 3, 9, 5, 3, 1, 24, 6, 17, 4, 6, 8
	DB	4, 3, 8, 7, 5, 7, 2, 3, 3, 7, 8, 3, 6, 1, 27
	DB	8, 3, 6, 1, 3, 17, 9, 3, 1, 9, 3, 2, 1, 12, 9
	DB	6, 2, 1, 6, 3, 3, 2, 6, 1, 5, 9, 4, 3, 2, 3
; .420 sec
	DB	9, 3, 7, 3, 5, 3, 4, 5, 1, 17, 7, 15, 3, 2, 1
	DB	24, 14, 1, 3, 5, 6, 4, 2, 1, 3, 6, 6, 3, 2, 9
	DB	3, 6, 4, 2, 1, 3, 11, 1, 2, 7, 11, 15, 3, 1, 3
	DB	2, 3, 13, 2, 7, 8, 6, 4, 2, 1, 8, 7, 14, 6, 10
	DB	2, 6, 1, 3, 6, 5, 1, 9, 2, 7, 17, 6, 7, 2, 4
	DB	9, 8, 4, 9, 2, 3, 15, 7, 8, 4, 2, 3, 10, 32, 4
	DB	2, 7, 3, 17, 1, 27, 9, 9, 2, 3, 6, 10, 3, 12, 8
	DB	3, 3, 7, 2, 1, 11, 3, 7, 6, 2, 7, 18, 5, 1, 2
	DB	7, 5, 3, 4, 8, 3, 10, 2, 7, 5, 10, 5, 9, 4, 2
	DB	1, 5, 7, 2, 10, 9, 5, 9, 1, 9, 2, 4, 11, 9, 4
	DB	2, 1, 5, 4, 3, 3, 5, 7, 8, 4, 8, 1, 2, 4, 21
	DB	2, 6, 9, 1, 11, 6, 4, 9, 9, 2, 1, 17, 1, 2, 7
	DB	6, 2, 4, 6, 2, 7, 8, 22, 3, 3, 3, 14, 15, 1, 9
	DB	6, 6, 5, 4, 11, 1, 3, 2, 9, 3, 18, 3, 4, 2, 1
	DB	2, 3, 7, 5, 3, 3, 7, 2, 1, 5, 1, 3, 2, 10, 5
	DB	6, 12, 6, 7, 5, 12, 3, 4, 5, 6, 3, 1, 3, 6, 5
	DB	1, 14, 1, 5, 9, 12, 7, 6, 9, 3, 5, 4, 5, 13, 3
	DB	2, 6, 4, 2, 3, 13, 5, 4, 5, 1, 8, 1, 5, 13, 5
	DB	1, 9, 2, 3, 10, 6, 2, 3, 4, 6, 5, 1, 9, 11, 1
	DB	5, 22, 2, 10, 5, 19, 2, 7, 6, 5, 1, 5, 1, 2, 1
	DB	5, 7, 5, 4, 3, 3, 5, 3, 9, 3, 9, 13, 15, 3, 8
	DB	6, 4, 2, 3, 7, 18, 5, 16, 11, 19, 6, 2, 7, 5, 4
	DB	2, 4, 5, 10, 5, 1, 8, 10, 30, 6, 3, 2, 9, 9, 1
	DB	6, 5, 1, 12, 3, 5, 7, 3, 2, 1, 2, 4, 3, 5, 3
	DB	3, 7, 3, 6, 3, 2, 15, 15, 9, 1, 9, 2, 9, 3, 15
	DB	3, 3, 1, 14, 7, 8, 1, 9, 5, 4, 2, 1, 2, 3, 3
	DB	4, 2, 9, 4, 6, 20, 1, 6, 3, 2, 19, 6, 3, 5, 1
	DB	3, 8, 3, 19, 2, 10, 3, 5, 1, 12, 5, 3, 1, 15, 8
	DB	3, 7, 2, 3, 13, 18, 9, 2, 4, 2, 12, 10, 2, 1, 2
	DB	6, 6, 6, 3, 19, 2, 6, 3, 6, 6, 7, 2, 6, 3, 4
	DB	15, 12, 5, 1, 9, 8, 3, 3, 1, 5, 4, 11, 3, 1, 2
	DB	7, 5, 3, 3, 1, 11, 10, 3, 15, 2, 3, 7, 2, 10, 11
	DB	6, 15, 3, 1, 9, 5, 3, 4, 5, 4, 5, 7, 5, 9, 9
	DB	4, 3, 5, 7, 2, 3, 1, 2, 4, 9, 5, 1, 3, 3, 12
	DB	2, 3, 1, 5, 6, 1, 17, 3, 13, 15, 11, 10, 3, 18, 8
	DB	6, 1, 2, 3, 3, 6, 1, 9, 11, 7, 3, 12, 2, 25, 8
	DB	1, 6, 8, 6, 4, 12, 6, 8, 9, 12, 4, 2, 3, 1, 3
	DB	6, 6, 8, 6, 9, 7, 3, 2, 1, 2, 6, 1, 5, 6, 9
	DB	1, 3, 2, 9, 3, 3, 4, 6, 2, 1, 2, 3, 1, 3, 2
	DB	1, 2, 6, 10, 2, 7, 3, 23, 4, 20, 1, 6, 8, 1, 2
	DB	4, 5, 1, 17, 10, 8, 7, 11, 4, 2, 6, 12, 1, 9, 5
	DB	12, 15, 1, 12, 5, 13, 17, 1, 2, 4, 9, 5, 1, 8, 1
	DB	2, 7, 6, 5, 9, 3, 24, 4, 2, 3, 6, 6, 7, 5, 3
	DB	3, 7, 3, 6, 6, 9, 5, 1, 12, 3, 2, 3, 3, 12, 15
	DB	7, 2, 3, 13, 2, 1, 2, 7, 2, 4, 15, 5, 4, 2, 9
	DB	6, 4, 5, 7, 14, 3, 15, 6, 16, 8, 3, 3, 4, 3, 27
	DB	3, 9, 3, 8, 3, 1, 6, 2, 12, 1, 3, 5, 1, 5, 4
	DB	5, 1, 11, 15, 3, 4, 2, 4, 2, 4, 8, 1, 5, 9, 3
	DB	7, 6, 12, 3, 6, 6, 14, 13, 3, 12, 2, 6, 3, 1, 18
	DB	2, 3, 6, 10, 9, 3, 2, 3, 9, 1, 5, 10, 9, 5, 6
	DB	7, 6, 5, 10, 11, 1, 3, 5, 1, 3, 2, 3, 3, 9, 1
	DB	18, 11, 4, 15, 5, 1, 14, 7, 5, 12, 3, 4, 5, 9, 4
	DB	2, 1, 5, 9, 3, 10, 3, 8, 7, 17, 3, 4, 5, 3, 3
	DB	3, 1, 3, 2, 1, 5, 7, 2, 6, 4, 3, 6, 2, 3, 1
	DB	6, 2, 1, 2, 4, 5, 1, 14, 1, 15, 8, 1, 3, 11, 9
	DB	7, 3, 21, 5, 1, 3, 3, 9, 5, 9, 4, 5, 6, 1, 5
	DB	19, 2, 7, 6, 2, 3, 7, 18, 6, 5, 4, 6, 5, 3, 12
	DB	6, 7, 3, 2, 7, 5, 6, 4, 8, 12, 1, 2, 1, 5, 15
	DB	7, 23, 9, 3, 9, 3, 1, 9, 6, 6, 5, 3, 21, 1, 3
	DB	3, 8, 4, 3, 8, 9, 7, 11, 4, 6, 2, 1, 2, 10, 3
	DB	5, 6, 4, 2, 12, 3, 7, 2, 1, 5, 9, 15, 4, 5, 4
	DB	3, 15, 2, 12, 1, 5, 3, 7, 8, 1, 5, 4, 5, 6, 13
	DB	6, 5, 1, 11, 4, 5, 1, 8, 12, 7, 2, 3, 3, 27, 1
	DB	2, 4, 3, 6, 5, 1, 11, 4, 3, 20, 1, 12, 8, 6, 1
	DB	3, 11, 1, 3, 6, 3, 2, 10, 3, 3, 5, 3, 4, 5, 6
	DB	1, 11, 3, 1, 5, 13, 2, 1, 3, 5, 6, 3, 10, 11, 7
	DB	2, 1, 5, 1, 21, 2, 3, 4, 3, 2, 10, 6, 17, 10, 8
	DB	3, 1, 9, 11, 1, 3, 14, 1, 2, 3, 7, 2, 10, 23, 9
	DB	4, 2, 7, 6, 5, 1, 6, 3, 12, 2, 3, 4, 11, 18, 7
	DB	3, 11, 6, 3, 7, 8, 1, 6, 14, 4, 2, 4, 3, 2, 9
	DB	4, 11, 4, 2, 3, 15, 6, 1, 9, 6, 6, 9, 6, 9, 3
	DB	2, 1, 2, 3, 7, 2, 18, 3, 27, 3, 3, 4, 5, 6, 7
	DB	2, 1, 5, 1, 3, 11, 1, 14, 7, 3, 2, 4, 18, 3, 2
	DB	3, 12, 12, 1, 2, 1, 6, 5, 19, 2, 13, 14, 1, 5, 9
	DB	21, 7, 5, 10, 8, 1, 3, 3, 3, 11, 10, 3, 5, 3, 10
	DB	2, 1, 6, 3, 5, 1, 5, 1, 11, 7, 5, 6, 6, 1, 5
	DB	7, 6, 2, 7, 2, 4, 5, 1, 9, 6, 5, 15, 1, 3, 12
	DB	5, 4, 5, 1, 5, 9, 3, 12, 3, 1, 9, 5, 1, 6, 3
	DB	3, 9, 18, 3, 2, 7, 6, 3, 5, 7, 5, 7, 15, 3, 5
	DB	3, 10, 6, 9, 11, 6, 4, 3, 2, 6, 7, 2, 21, 15, 4
	DB	14, 6, 3, 3, 3, 4, 5, 10, 6, 2, 9, 3, 7, 5, 3
	DB	6, 1, 9, 2, 4, 8, 1, 5, 7, 2, 6, 16, 3, 3, 3
	DB	2, 7, 2, 6, 4, 3, 6, 5, 4, 5, 1, 5, 4, 2, 1
	DB	2, 15, 10, 2, 3, 3, 3, 1, 9, 11, 1, 2, 7, 21, 8
; .419 sec
	DB	6, 3, 6, 1, 5, 4, 20, 15, 4, 2, 6, 4, 9, 2, 4
	DB	3, 3, 5, 3, 1, 3, 5, 1, 5, 12, 9, 9, 1, 3, 3
	DB	8, 9, 3, 4, 3, 8, 4, 8, 3, 3, 6, 3, 1, 6, 8
	DB	1, 6, 5, 1, 2, 12, 13, 5, 7, 5, 3, 18, 12, 1, 27
	DB	5, 7, 8, 3, 10, 6, 5, 3, 6, 1, 6, 2, 13, 2, 7
	DB	5, 3, 1, 2, 3, 1, 5, 1, 8, 6, 9, 9, 7, 8, 1
	DB	9, 3, 14, 1, 2, 1, 5, 6, 10, 6, 2, 3, 3, 10, 11
	DB	1, 2, 10, 8, 12, 1, 2, 3, 1, 21, 3, 2, 9, 3, 6
	DB	4, 2, 1, 3, 9, 3, 12, 2, 9, 9, 1, 5, 1, 20, 1
	DB	5, 15, 4, 9, 11, 1, 5, 4, 8, 6, 15, 4, 3, 3, 5
	DB	6, 3, 1, 9, 2, 15, 4, 5, 7, 2, 3, 4, 15, 3, 5
	DB	7, 9, 5, 9, 3, 4, 11, 4, 3, 8, 13, 11, 6, 3, 4
	DB	6, 5, 10, 5, 10, 9, 2, 6, 9, 16, 2, 1, 9, 2, 3
	DB	6, 4, 3, 3, 5, 4, 11, 6, 1, 3, 2, 7, 5, 10, 2
	DB	1, 14, 1, 5, 4, 9, 6, 3, 8, 4, 3, 5, 12, 3, 6
	DB	4, 2, 1, 2, 10, 15, 2, 6, 7, 2, 4, 2, 10, 5, 12
	DB	12, 6, 4, 5, 6, 10, 3, 2, 3, 4, 2, 10, 23, 10, 5
	DB	12, 1, 5, 3, 4, 3, 9, 5, 1, 5, 3, 7, 9, 8, 1
	DB	14, 7, 3, 2, 1, 3, 14, 1, 2, 3, 1, 6, 2, 30, 1
	DB	2, 7, 6, 6, 3, 6, 2, 10, 8, 6, 1, 9, 3, 5, 12
	DB	16, 5, 13, 2, 3, 7, 3, 2, 7, 2, 9, 7, 3, 5, 1
	DB	3, 2, 1, 5, 3, 10, 2, 12, 3, 22, 5, 1, 5, 16, 14
	DB	1, 3, 2, 6, 7, 6, 12, 5, 15, 4, 3, 3, 2, 1, 6
	DB	5, 4, 17, 7, 2, 3, 6, 1, 5, 6, 9, 4, 9, 6, 2
	DB	4, 9, 5, 7, 9, 6, 2, 6, 4, 8, 3, 6, 6, 9, 13
	DB	5, 1, 9, 6, 6, 20, 3, 10, 2, 3, 6, 1, 5, 4, 5
	DB	7, 2, 3, 7, 2, 3, 7, 3, 3, 5, 12, 6, 1, 6, 5
	DB	4, 21, 12, 5, 1, 11, 3, 1, 2, 4, 9, 2, 19, 5, 1
	DB	3, 2, 4, 3, 2, 3, 10, 6, 9, 5, 10, 2, 6, 4, 11
	DB	13, 6, 6, 2, 1, 2, 4, 8, 7, 18, 2, 3, 3, 7, 2
	DB	4, 5, 19, 12, 6, 5, 7, 2, 6, 4, 3, 2, 1, 3, 9
	DB	12, 3, 2, 16, 9, 5, 1, 9, 5, 6, 15, 4, 6, 5, 3
	DB	7, 3, 2, 4, 3, 2, 10, 21, 2, 6, 6, 3, 1, 27, 5
	DB	10, 3, 6, 3, 11, 21, 1, 2, 7, 14, 1, 11, 10, 5, 3
	DB	9, 3, 13, 9, 5, 1, 11, 3, 1, 6, 5, 3, 6, 1, 3
	DB	2, 10, 5, 6, 10, 5, 13, 2, 10, 12, 3, 14, 3, 3, 7
	DB	11, 6, 4, 15, 2, 4, 3, 2, 3, 1, 14, 3, 3, 4, 3
	DB	3, 5, 1, 5, 6, 3, 1, 3, 5, 3, 3, 4, 8, 9, 13
	DB	5, 1, 9, 3, 3, 5, 4, 3, 2, 18, 4, 5, 10, 11, 3
	DB	9, 12, 6, 7, 3, 23, 6, 1, 3, 14, 3, 3, 7, 8, 6
	DB	1, 5, 4, 8, 1, 9, 8, 7, 2, 10, 5, 3, 6, 1, 3
	DB	2, 13, 6, 2, 3, 6, 1, 9, 3, 6, 6, 3, 3, 2, 3
	DB	3, 10, 5, 1, 14, 1, 6, 14, 4, 2, 9, 4, 14, 1, 2
	DB	3, 4, 3, 5, 12, 15, 1, 6, 3, 23, 3, 6, 1, 2, 10
	DB	2, 6, 3, 12, 10, 8, 1, 5, 4, 5, 1, 5, 1, 2, 3
	DB	4, 5, 6, 7, 6, 6, 11, 3, 4, 8, 3, 1, 3, 21, 9
	DB	3, 2, 6, 6, 1, 3, 17, 7, 2, 3, 1, 2, 10, 14, 3
	DB	7, 9, 11, 1, 8, 10, 2, 3, 4, 2, 4, 11, 3, 1, 5
	DB	4, 5, 1, 2, 3, 7, 14, 3, 1, 2, 7, 11, 1, 5, 1
	DB	14, 18, 6, 7, 2, 6, 4, 3, 3, 12, 5, 19, 6, 17, 3
	DB	4, 8, 1, 9, 8, 3, 4, 2, 16, 9, 3, 8, 4, 3, 6
	DB	2, 3, 4, 6, 11, 3, 10, 12, 6, 8, 1, 5, 6, 9, 6
	DB	3, 1, 3, 6, 12, 2, 15, 3, 3, 7, 2, 1, 5, 10, 5
	DB	1, 6, 8, 4, 5, 6, 4, 3, 3, 12, 2, 3, 4, 8, 3
	DB	13, 5, 15, 1, 5, 1, 3, 2, 3, 1, 3, 12, 3, 3, 3
	DB	15, 20, 7, 2, 10, 2, 3, 6, 6, 1, 8, 7, 27, 15, 6
	DB	2, 4, 3, 11, 1, 6, 9, 9, 2, 4, 0
ENDIF

	;; Align so that other GWDATA areas are aligned on a cache line
	align 128
_GWDATA ENDS


initsize	EQU	7*11*13*17	; First 4 primes cleared in initsieve
initcount	EQU	4		; Count of primes cleared in initsieve
sievesize	EQU	00001000h	; 4KB sieve
sieveextra	EQU	00001000h	; 4KB extra bits of sieve to make
					; the sieve bit clearing faster
sievemask	EQU	0000F000h	; Mask to determine if address is
					; beyond sieve.  Requires that the
					; sieve be aligned on 64K boundary.
repcnt		EQU	4		; How many reps before returning

_TEXT	SEGMENT

; setupf (struct facasm_data *)
;	Initialize 32-bit factoring code
; Windows 32-bit (_setupf)
; Linux 32-bit (setupf)
;	Parameter ptr = [esp+4]

PROCFL	setupf
	ad_prolog 0,0,rbx,rbp,rsi,rdi

; Save p (passed in EXPONENT), compute various constants and addresses

	mov	ecx, EXPONENT
	mov	p, ecx

	lea	eax, last_global	; Addr of allocated memory
	add	eax, 0FFFFh		; Align area on a 64K boundary
	and	eax, 0FFFF0000h
	mov	primearray, eax		; Array of primes and offsets
	add	eax, 30000h
	mov	initsieve, eax		; Array used to init sieve
	add	eax, 20000h
	mov	initlookup, eax		; Lookup table into initsieve
	add	eax, 20000h
	mov	sieve, eax		; Array of sieve bits
	mov	eax, primearray
	add	eax, initcount*12
	mov	primearray12, eax

	add	ecx, ecx		; Two times p
	mov	twop, ecx

	lea	esi, facdists[32*8]
	mov	eax, 120		; Compute 120 (8 * 3 * 5) * p
	mul	p
	sub	ebx, ebx		; LSW of multiple of facdist
	sub	ecx, ecx		; MSW of multiple of facdist
	lea	edi, facdists
fdlp:	mov	[edi], ebx
	mov	[edi+4], ecx
	fild	QWORD PTR [edi]		; Convert from integer to float point
	fstp	QWORD PTR [edi][facdistsfltoffset - facdistsoffset]
	lea	edi, [edi+8]		; Bump pointers
	add	ebx, eax		; Next distance
	adc	ecx, edx
	cmp	edi, esi		; Loop 32 times
	jne	short fdlp
	mov	facdist32, ebx		; Save 32 * facdist
	mov	facdist32+4, ecx
	fild	p			; Save 32 * facdist as a float
	fmul	FDMULT
	fstp	facdist_flt

; Copy byte based prime array to double word based array

	lea	esi, sivinfo		; Source - array of bytes
	mov	edi, primearray		; Destination - array of double words
	mov	edx, 5			; Sivinfo contains primes larger than 5
	sub	eax, eax
initlp:	mov	al, [esi]
	inc	esi
	add	edx, eax
	add	edx, eax
	mov	[edi], edx
	lea	edi, [edi+12]
	and	eax, eax
	jnz	short initlp
	mov	[edi-12], eax

; Fill initsieve array with ones

	mov	eax, 0FFFFFFFFh
	mov	ecx, initsize
	mov	edi, initsieve
	rep	stosd

; Clear the bits associated with the first 4 small factors

	mov	esi, primearray		; Ptr to first small prime
	mov	edi, initsieve		; Address of initial sieve
	mov	ebx, sieve		; Used for temporary storage
ilp1:	mov	edx, [esi]		; Load small prime
	sub	eax, eax
ilp2:	btr	[edi], eax		; Clear the bit
	add	eax, edx		; Next bit #
	cmp	eax, initsize*32	; Are we past the end of the sieve
	jb	short ilp2
	sub	eax, eax		; Save bit# (zero because the first
	mov	[ebx], eax		; in initsieve was cleared for all
					; small primes)
	mov	eax, 32			; Compute 32 - 32 mod p.  This value
ilp3:	sub	eax, edx		; represents the bit# in the next
	jns	short ilp3		; word that was cleared for the
	neg	eax			; given small prime.  For example,
	mov	[ebx+4], eax		; 32 - 32 mod 13 = 7.  Thus, the 7th
	add	ebx, 8			; bit in the second initsieve word was
					; cleared by small prime 13.
	lea	esi, [esi+12]
	cmp	esi, primearray12	; Another small prime?
	jnz	short ilp1

; Fill lookup table into initsieve

	sub	edi, edi		; The first lookup points to the
	sub	eax, eax		; first entry in initsieve
ilp4:	mov	esi, initlookup		; Set lookup table entry
	mov	[esi][eax*4], edi	; Set lookup table entry
	inc	edi			; Point to next initsieve dword
	mov	esi, sieve		; Array of 32 mod p info
	mov	ebx, primearray		; Ptr to first small prime
	sub	eax, eax		; Build the index in eax
ilp5:	mov	ecx, [ebx]		; Load the small prime
	mul	ecx			; Multiply index by the small prime
	mov	edx, [esi]		; Load bit#
	add	edx, [esi+4]		; Compute bit# in next dword
ilp6:	sub	edx, ecx		; Compute bit# mod smallp
	jns	short ilp6
	add	edx, ecx
	mov	[esi], edx		; Save bit# for next pass
	add	eax, edx		; Add the bit# to the index
	lea	esi, [esi+8]
	lea	ebx, [ebx+12]
	cmp	ebx, primearray12	; Last small prime?
	jnz	short ilp5
	test	eax, eax		; Is lookup table completely built?
	jnz	short ilp4

; Set firstcall

	sub	eax, eax
	mov	firstcall, eax

; Init floating point unit and constants

	finit
	mov	temp, 32
	fild	temp			; Load scaling factor 32
	mov	temp, 60
	fild	temp			; Load scaling factor 60
	mov	temp, 12
	fild	temp			; Load 12 = 3*2^2
	fscale				; 3*2^62
	fxch	st(1)			; Two instructions to pop 60
	fstp	FACLO
	fst	BIGVAL0			; Save 3*2^62
	fscale				; 3*2^94
	fstp	BIGVAL1			; Save 3*2^94
	fstp	FACLO			; Pop 32
	fld	FOUR			; 2^2
	fmul	st, st			; 2^4
	fmul	st, st			; 2^8
	fmul	st, st			; 2^16
	fmul	st, st			; 2^32
	fst	TWO_TO_32
	fmul	st, st			; 2^64
	fstp	TWO_TO_64
	mov	temp, 123
	fild	temp			; Load scaling factor
	fld1
	fscale				; 2^123
	fstp	TWO_TO_123		; Save 2^123
	fstp	FACLO
	mov	temp, -59
	fild	temp			; Load scaling factor
	fld1
	fscale				; 2^-59
	fstp	TWO_TO_MINUS_59		; Save 2^-59
	fstp	FACLO

; Return

	ad_epilog 0,0,rbx,rbp,rsi,rdi
setupf	ENDP

; Do some of the initial squarings here.  Shift p such that the first quotient
; will be 62 bits or less.

PROCF	presq
	int_prolog 0,0,0
	mov	edx, 126
	mov	eax, FACHSW
	or	eax, eax
	jnz	short psq1
	mov	edx, 94
	mov	eax, FACMSW
psq1:	inc	edx
	shr	eax, 1
	jnz	short psq1
	mov	ecx, p
	sub	eax, eax
setlp:	shrd	eax, ecx, 1
	shr	ecx, 1
	cmp	ecx, edx
	jg	short setlp
	mov	shifter, eax		; Save unused bits in the shifter
	mov	temp, ecx		; Compute 2^ecx
	fild	temp
	fld1
	fscale
	fst	initval			; Save 2^ecx
	fld1
	fxch	st(1)
	fdivp	st(1), st
	fstp	initval_inv		; Save 2^-ecx
	sub	temp, 64		; Compute 2^(ecx-64)
	fild	temp
	fld1
	fscale
	fstp	initval64		; Save 2^ecx
	fcompp				; Pop off two scales
	sub	eax, eax		; Bits 33-64 of initval are zero if
	cmp	ecx, 64			; initval >= 2^64
	jge	short idone
	inc	eax			; Shift to get bits 33-64 of initval
	shl	eax, cl
idone:	mov	initval1, eax		; Save bits 33-64 of initval
	sub	eax, eax		; Bits 65-96 of initval are zero if
	cmp	ecx, 96			; initval >= 2^96
	jge	short idone2
	inc	eax			; Shift to get bits 65-96 of initval
	shl	eax, cl
idone2:	mov	initval0, eax		; Save bits 65-96 of initval

; Now use the CPU_FLAGS to determine which version of sieve tester to run
; First compute the value corresponding to the last bit in the sieve.

	mov	eax, sievesize * 8 * repcnt * 120; true sieve size (times 120)
	mul	p			; times distance between factors(120*p)
	add	eax, savefac2		; plus value corresponding
	adc	edx, FACMSW		; to first sieve bit
	mov	ecx, FACHSW
	adc	ecx, 0			; Add in carry and test for >= 65-bits
	jz	short not65		; Jump if not >= 65 bits
	mov	eax, OFFSET tlp80	; 80 bit all cpus version
	test	CPU_FLAGS, 200h		; Is this an SSE2 machine?
	jz	short cp1		; No, use all purpose code
	mov	eax, OFFSET tlp86	; 75-86 bit SSE2 version
	cmp	ecx, 3FFh		; Are we testing 75-bits or greater?
	ja	short cp1		; Yes, jump
	mov	eax, OFFSET tlp74	; 65-74 bit SSE2 version
	jmp	short cp1		; Yes, jump
not65:	mov	eax, OFFSET tlp64	; 64 bit all cpus version
	cmp	edx, 3FFFFFFFh		; Are we testing 63-bits or greater?
	ja	short cp1		; Yes, jump
	mov	eax, OFFSET plp		; Pentium Pro version
	test	CPU_FLAGS, 4		; Is this a Pentium Pro or better?
	jnz	short cp1		; Yes - CMOV is supported, jump
	mov	eax, OFFSET ulp		; 486 version
	test	CPU_FLAGS, 1		; Is this a 486 or AMD K6?
	jz	short cp1		; Yes RDTSC not supported, jump
	mov	eax, OFFSET tlp60	; 60 bit Pentium version
	cmp	edx, 0FFFFFFFh		; Are we testing 61-bits or greater?
	jl	short cp1		; No, jump
	mov	eax, OFFSET tlp62	; 62 bit Pentium version
cp1:	mov	esi, last_primearray	; Load address to patch
	mov	[esi+8], eax		; Store "sieve done" address

; Compute number of times we will loop in sqloop

	mov	eax, shifter
	sub	ecx, ecx
cntlp:	inc	ecx
	add	eax, eax
	jnz	short cntlp
	mov	sqloop_counter, ecx
	mov	eax, ecx
	dec	eax
	mov	wqloop_counter, eax

; Init the multipliers for 60-bit factoring code

	mov	eax, shifter
	fld	ONE			; Starting 1/fac multiplier
	fst	REMMULTS[ecx*4+4]
mlp:	add	eax, eax		; Test shifter bit
	jnc	short nodbl		; No doubling rem if bit is off
	fld	TWO			; Double the remainder
	fstp	REMMULTS[ecx*4]		; Save remainder multiplier
	fmul	FOUR			; Modify 1/fac multiplier
	fstp	FACMULTS[ecx*4]		; Store 1/fac multiplier
	fld	QUARTER			; Next 1/fac multiplier
	jmp	short mlptst
nodbl:	fld	ONE			; Don't double the remainder
	fstp	REMMULTS[ecx*4]		; Save remainder multiplier
	fstp	FACMULTS[ecx*4]		; Store 1/fac multiplier
	fld	ONE			; Next 1/fac multiplier
mlptst:	dec	ecx			; Decrement loop counter
	jnz	short mlp		; Loop if necessary
	fstp	temp			; Discard float values

	int_epilog 0,0,0
presq	ENDP

;
; Register allocations
;

fac0	equ	edi
fac1	equ	ebx			; Keep factor in edi, ebx, ecx
fac2	equ	ecx

; factor64 (struct facasm_data *)
;	Do a few sieving runs and test potential factors
; Windows 32-bit (_factor64)
; Linux 32-bit (factor64)
;	Parameter ptr = [esp+4]

PROCFL	factor64
	ad_prolog 0,0,rbx,rbp,rsi,rdi,xmm6,xmm7

; Init the FPU every iteration just to be safe

	finit

; Is this a request to test 32-bit factors?  If not, go to complicated code.

	mov	eax, FACHSW
	mov	ecx, FACMSW
	or	eax, ecx
	jnz	not32

;
; Try to find a 32-bit factor of 2**p - 1
;

	mov	ecx, p
s32lp:	add	ecx, ecx		; Shift until top bit on
	jns	short s32lp
	mov	eax, ecx
	shl	eax, 5
	mov	shifter, eax
	shr	ecx, 27
	mov	eax, 1
	shl	eax, cl
	mov	temp, eax

; First trial factor is 2p + 1

	mov	ecx, twop
	inc	ecx

; If factor = 3 or 5 mod 8, then it can't be a factor of 2**p - 1

testf:	mov	eax, ecx
	and	al, 6
	jz	short test32
	cmp	al, 6
	jnz	short nextf

; Square the number until we computed 2**p MOD factor

test32:	mov	ebx, shifter
	mov	eax, temp
	sub	edx, edx
	div	ecx
loop32:	mov	eax, edx
	mul	eax
	div	ecx
	add	ebx, ebx
	jnc	short loop32
	jz	short exit32
	add	edx, edx
	jc	short sub32
	cmp	edx, ecx
	jb	short loop32
sub32:	sub	edx, ecx
	jmp	short loop32

; Multiply remainder by two one last time (for the last carry out of shifter)
; If result = 1 mod factor, then we found a divisor of 2**p - 1

exit32:	add	edx, edx
	dec	edx
	cmp	edx, ecx
	jz	short win32

; Try next possible factor

nextf:	add	ecx, twop
	jnc	short testf

; No 32-bit factor found - return for ESC check

	mov	eax, 2			; Return for ESC check
	mov	FACMSW, 1		; Restart at 1
	jmp	done

; Divisor found, return TRUE

win32:	mov	eax, 1
	mov	FACMSW, 0
	mov	FACLSW, ecx
	jmp	done

;
; Find a factor bigger than 2^32
;

not32:

; Set number of repetitions

	mov	reps, repcnt

; Check/Set firstcall

	cmp	firstcall, 0
	jne	slp
	inc	firstcall

; Clear counts of queued factors

	mov	queuedpro, 0

; Initialize FACHI shift count and mask

	mov	fachi_shf_count, 31
	mov	fachi_shf_mask, 0FFFFFFFEh

; First trial factor is first number of the form 2kp + 1
; greater than FACMSW * 2^32

	mov	fac0, FACHSW		; Start point * 2^64
	mov	fac1, FACMSW		; Start point * 2^32
	mov	fac2, twop		; Load twop for dividing
	mov	eax, fac0		; Do a mod on the start point
	sub	edx, edx
	div	fac2
	mov	eax, fac1
	div	fac2			
	sub	eax, eax		; Now do a mod on the remainder * 2^32
	div	fac2
	sub	fac2, edx		; Subtract remainder from twop
	inc	fac2			; and add 1 to find first test factor

; Make sure we have a factor with the right modulo for this pass

	mov	esi, FACPASS		; Get the pass number (0 to 15)
	mov	ebp, 120
flp1:	mov	eax, fac0		; Do a mod 120 in three parts
	sub	edx, edx
	div	ebp
	mov	eax, fac1		; Do a mod 120
	div	ebp
	mov	eax, fac2		; Do a mod 120
	div	ebp
	cmp	edx, rems[esi*4]	; Is this the desired remainder
	jz	short flp2		; Yes, jump to flp2
	add	fac2, twop		; No, try next factor
	adc	fac1, 0
	adc	fac0, 0
	jmp	short flp1		; Loop
flp2:	mov	savefac0, fac0
	mov	savefac1, fac1
	mov	savefac2, fac2

; Loop through all the small primes determining sieve bit to clear

testp	EQU	ebp
prev	EQU	ebx
cur	EQU	ecx
bigrem	EQU	edi
litrem	EQU	esi

	mov	esi, primearray
smlp:	mov	testp, [esi]
	and	testp, testp
	jz	short smdn
	cmp	testp, p
	jne	short smok
	mov	DWORD PTR [esi], 0
	jmp	short smdn
smok:

;
; Let testp = an entry from our small primes array
; Let y = facdist mod testp
; Use Euclid's greatest common denominator algorithm to compute the number
; x such that x * y = -1 MOD testp
; We can then use x to compute the first bit in the sieve array that needs
; clearing.
;

	pusher	esi
	sub	prev, prev		; Set up: set bigrem = testp,
	mov	cur, 1			; litrem = facdist mod testp
	mov	bigrem, testp
	mov	eax, facdists+12
	sub	edx, edx
	div	bigrem
	mov	eax, facdists+8
	div	bigrem
	mov	litrem, edx
euclp:	cmp	litrem, 1		; Loop ends when litrem equals 1
	je	short eucdn
	sub	edx, edx		; Compute bigrem mod litrem
	mov	eax, bigrem
	div	litrem
	mov	bigrem, litrem
	mov	litrem, edx
	imul	cur
	sub	prev, eax
	xchg	prev, cur
	jmp	short euclp
eucdn:	neg	cur			; set x = -cur if cur was negative
	jns	short eucdn2
	add	cur, testp		; else set x = testp - cur
eucdn2:	sub	edx, edx		; Divide first factor by testp
	mov	eax, savefac0
	div	testp
	mov	eax, savefac1
	div	testp
	mov	eax, savefac2
	div	testp
	mov	eax, edx		; Multiply remainder by x
	mul	cur
	div	testp			; edx now contains the bit number!
	popper	esi			; save it for sieve clearing
	mov	[esi+4], edx
	lea	esi, [esi+12]
	jmp	short smlp
smdn:

; Use the initlookup table to determine the first dword in initsieve to copy

	mov	esi, primearray		; Ptr to first small prime
	sub	eax, eax		; Build the index in eax
lk1:	mul	DWORD PTR [esi]		; Multiply index by the small prime
	add	eax, [esi+4]		; Add the bit# to the index
	lea	esi, [esi+12]
	cmp	esi, primearray12	; Last small prime?
	jnz	short lk1
	mov	esi, initlookup		; Load the address of the initsieve
	mov	eax, [esi][eax*4]	; Load the address of the initsieve
	mov	initstart, eax		; word that clears the correct bits

; Compute the clrXX addresses

	mov	esi, primearray		; Ptr to first small prime
clr1:	mov	eax, [esi]		; The small prime
	and	eax, eax		; End of table?
	jz	short clrdn		; Yes if zero
	mov	ecx, eax		; Isolate bottom 3 bits
	and	ecx, 7
	shr	eax, 3
	mov	[esi], eax		; Save small prime / 8
	mov	ebx, [esi+4]		; The bit# to clear
	mov	edx, ebx		; Isolate bottom 3 bits
	and	edx, 7
	shr	ebx, 3
	add	ebx, sieve
	mov	[esi+4], ebx		; The first sieve address
	cmp	eax, sievesize/8	; Use the 2 or 8 unravelling
	jb	short clr2
	add	edx, 8
	cmp	eax, sievesize/2	; Use the 1 or 2 unravelling
	jb	short clr2
	add	edx, 8
	cmp	eax, sievesize		; Use the 0 or 1 unravelling
	jb	short clr2
	add	edx, 8
clr2:	lea	ecx, [edx*8][ecx-1]
	mov	ecx, clrtab[ecx*2]
	mov	[esi+8], ecx		; Store correct address
	lea	esi, [esi+12]
	jnz	short clr1
clrdn:	mov	last_primearray, esi	; Save for later patching

; Pre-square the number as much as possible.

	call	presq

; Fill sieve for the first time

	mov	edi, sieve		; EDI is the destination
	mov	eax, (sievesize+sieveextra)/4 ; EAX = Count of dwords to copy
	jmp	short slp1

; Pre-square the number as much as possible.

slp:	call	presq

;
; This is the RE-fill sieve entry point.
; Copy the bits after the sieve.  They were partially filled in earlier.
;

slp0:	mov	edi, sieve
	lea	esi, [edi+sievesize]
	mov	ecx, sieveextra / 4
	rep	movsd
	mov	eax, sievesize / 4	; count of dwords left to copy

;
; Init the sieve, the first call will fill up the sieve and sieveextra
;

slp1:	mov	edx, initstart		; load offset to first dword to copy
	mov	esi, initsieve
	lea	esi, [esi][edx*4]
	mov	ecx, initsize		; ECX is count of dwords to copy
	sub	ecx, edx
slp2:	sub	eax, ecx		; Lower count of bytes left to copy
	js	short slpdn		; Special case count going negative
	rep	movsd			; Copy the bytes
	mov	esi, initsieve		; Setup for copying next batch
	mov	ecx, initsize
	sub	edx, edx
	jmp	short slp2
slpdn:	add	ecx, eax		; Make count positive again
	add	edx, ecx
	mov	initstart, edx		; Save start position for next time
	rep	movsd			; Copy the bytes

;
; Loop through the small prime array, clearing sieve bits.  Note we don't
; care if we go past the end of the sieve, since we have sieveextra bits
; available for being sloppy.  If we increase the number of small primes,
; we need to increase sieveextra or reduce the loop unraveling here.
;

BIT0	EQU	0FEh
BIT1	EQU	0FDh
BIT2	EQU	0FBh
BIT3	EQU	0F7h
BIT4	EQU	0EFh
BIT5	EQU	0DFh
BIT6	EQU	0BFh
BIT7	EQU	07Fh

;; 8 or more bit clearings required
clear	MACRO	b0,b1,b2,b3,b4,b5,b6,b7,start,incr
	LOCAL	clrlp
;;	align	4
clrlp:	mov	al, [edi]			;; Load byte #0
	mov	dl, [edi+(start+incr)/8][ebx]	;; Load byte #1
	and	al, b0				;; Clear bit 0
	and	dl, b1				;; Clear bit 1
	mov	[edi], al			;; Store byte #0
	mov	[edi+(start+incr)/8][ebx], dl	;; Store byte #1
	lea	edi, [edi+(start+4*incr)/8][ebx*4]
	mov	al, [esi+(start+2*incr)/8]	;; Load byte #2
	mov	dl, [esi+(start+3*incr)/8][ebx]	;; Load byte #3
	and	al, b2				;; Clear bit 2
	and	dl, b3				;; Clear bit 3
	mov	[esi+(start+2*incr)/8], al	;; Store byte #2
	mov	[esi+(start+3*incr)/8][ebx], dl	;; Store byte #3
	lea	esi, [esi+(start+6*incr)/8][ebx*4]
	mov	al, [edi]			;; Load byte #4
	mov	dl, [edi+(start+5*incr)/8-(start+4*incr)/8][ebx];; Load byte #5
	and	al, b4				;; Clear bit 4
	and	dl, b5				;; Clear bit 5
	mov	[edi], al			;; Store byte #4
	mov	[edi+(start+5*incr)/8-(start+4*incr)/8][ebx], dl;; Save byte #5
	lea	edi, [edi+incr-(start+4*incr)/8][ebx*4]
	mov	al, [esi]			;; Load byte #6
	mov	dl, [esi+(start+7*incr)/8-(start+6*incr)/8][ebx];; Load byte #7
	and	al, b6				;; Clear bit 6
	and	dl, b7				;; Clear bit 7
	mov	[esi], al			;; Store byte #6
	mov	[esi+(start+7*incr)/8-(start+6*incr)/8][ebx], dl;; Save byte #7
	lea	esi, [esi+incr-(start+6*incr)/8][ebx*4]
	test	edi, sievesize		;; Past the end of the sieve?
	jz	short clrlp
	sub	edi, sievesize		;; U - Sieve address for next time
	mov	ebx, [ebp]		;; V - Load small prime divided by 8
	mov	[ebp-12+4], edi		;; U - Save sieve address for next time
	mov	edi, [ebp+4]		;; V - Load sieve address
	mov	eax, [ebp+8]		;; U - Address to jump to
	lea	ebp, [ebp+12]		;; V - Next primearray address
	lea	esi, [edi][ebx*2]	;; U - Second sieve address
	jmp	eax			;; V - Dispatch
	ENDM

;; 2 to 8 bit clearings required
clear2	MACRO	b0,b1,start,incr,clrnxt
;;	align	4
	mov	al, [edi]			;; Load byte #0
	mov	dl, [edi+(start+incr)/8][ebx]	;; Load byte #1
	and	al, b0				;; Clear bit 0
	and	dl, b1				;; Clear bit 1
	mov	[edi], al			;; Store byte #0
	mov	[edi+(start+incr)/8][ebx], dl	;; Store byte #1
	lea	edi, [edi+(start+2*incr)/8][ebx*2]
	test	edi, sievemask		;; Past the end of the sieve?
	jz	clrnxt
	sub	edi, sievesize		;; U - Sieve address for next time
	mov	[ebp-12+8], OFFSET clrnxt ;; V - Save jump addr for next time
	mov	[ebp-12+4], edi		;; U - Save sieve address for next time
	mov	ebx, [ebp]		;; V - Load small prime divided by 8
	mov	edi, [ebp+4]		;; U - Load sieve address
	mov	eax, [ebp+8]		;; V - Address to jump to
	lea	ebp, [ebp+12]		;; U - Next primearray address
	jmp	eax			;; V - Dispatch
	ENDM

;; 1 or 2 bit clearings required
clear3	MACRO	b0,start,incr,clrnxt
;;	align	4
	mov	al, [edi]		;; U - Load byte #0
	and	al, b0			;;*U - Clear bit 0
	mov	[edi], al		;;*U - Store byte #0
	lea	edi, [edi+(start+incr)/8][ebx] ;; V
	test	edi, sievemask		;; U - Past the end of the sieve?
	jz	clrnxt			;; V
	sub	edi, sievesize		;; U - Sieve address for next time
	mov	[ebp-12+8], OFFSET clrnxt ;; V - Save jump addr for next time
	mov	[ebp-12+4], edi		;; U - Save sieve address for next time
	mov	ebx, [ebp]		;; V - Load small prime divided by 8
	mov	edi, [ebp+4]		;; U - Load sieve address
	mov	eax, [ebp+8]		;; V - Address to jump to
	lea	ebp, [ebp+12]		;; U - Next primearray address
	jmp	eax			;; V - Dispatch
	ENDM

;; 0 or 1 bit clearings required
clear4	MACRO	b0,start,incr,clrnxt
	LOCAL	stay
;;	align	4
	test	edi, sievemask		;; U - Past the end of the sieve?
	jnz	short stay		;; V - Yes, stay here next dispatch
	mov	ebx, [ebp-12]		;; U - Load small prime divided by 8
	mov	al, [edi]		;; V - Load byte #0
	mov	[ebp-12+8], OFFSET clrnxt ;; U - Save jump addr for next time
	and	al, b0			;; V - Clear bit 0
	mov	[edi], al		;; U - Store byte #0
	lea	edi, [edi+(start+incr)/8][ebx] ;; V - Next bit address
stay:	sub	edi, sievesize		;; U - Sieve address for next time
	mov	eax, [ebp+8]		;; V - Address to jump to
	mov	[ebp-12+4], edi		;; U - Save sieve address for next time
	mov	edi, [ebp+4]		;; V - Load sieve address
	lea	ebp, [ebp+12]		;; U - Next primearray address
	jmp	eax			;; V - Dispatch
	ENDM

	mov	ebp, primearray12	; Ptr to first prime in array
	mov	ebx, [ebp]		; Load small prime divided by 8
	mov	edi, [ebp+4]		; Load sieve address
	mov	eax, [ebp+8]		; Address to jump to
	lea	ebp, [ebp+12]		; Next primearray address
	lea	esi, [edi][ebx*2]	; Second sieve address
	jmp	eax

clr01:	clear	BIT0,BIT1,BIT2,BIT3,BIT4,BIT5,BIT6,BIT7,0,1
clr03:	clear	BIT0,BIT3,BIT6,BIT1,BIT4,BIT7,BIT2,BIT5,0,3
clr05:	clear	BIT0,BIT5,BIT2,BIT7,BIT4,BIT1,BIT6,BIT3,0,5
clr07:	clear	BIT0,BIT7,BIT6,BIT5,BIT4,BIT3,BIT2,BIT1,0,7
clr11:	clear	BIT1,BIT2,BIT3,BIT4,BIT5,BIT6,BIT7,BIT0,1,1
clr13:	clear	BIT1,BIT4,BIT7,BIT2,BIT5,BIT0,BIT3,BIT6,1,3
clr15:	clear	BIT1,BIT6,BIT3,BIT0,BIT5,BIT2,BIT7,BIT4,1,5
clr17:	clear	BIT1,BIT0,BIT7,BIT6,BIT5,BIT4,BIT3,BIT2,1,7
clr21:	clear	BIT2,BIT3,BIT4,BIT5,BIT6,BIT7,BIT0,BIT1,2,1
clr23:	clear	BIT2,BIT5,BIT0,BIT3,BIT6,BIT1,BIT4,BIT7,2,3
clr25:	clear	BIT2,BIT7,BIT4,BIT1,BIT6,BIT3,BIT0,BIT5,2,5
clr27:	clear	BIT2,BIT1,BIT0,BIT7,BIT6,BIT5,BIT4,BIT3,2,7
clr31:	clear	BIT3,BIT4,BIT5,BIT6,BIT7,BIT0,BIT1,BIT2,3,1
clr33:	clear	BIT3,BIT6,BIT1,BIT4,BIT7,BIT2,BIT5,BIT0,3,3
clr35:	clear	BIT3,BIT0,BIT5,BIT2,BIT7,BIT4,BIT1,BIT6,3,5
clr37:	clear	BIT3,BIT2,BIT1,BIT0,BIT7,BIT6,BIT5,BIT4,3,7
clr41:	clear	BIT4,BIT5,BIT6,BIT7,BIT0,BIT1,BIT2,BIT3,4,1
clr43:	clear	BIT4,BIT7,BIT2,BIT5,BIT0,BIT3,BIT6,BIT1,4,3
clr45:	clear	BIT4,BIT1,BIT6,BIT3,BIT0,BIT5,BIT2,BIT7,4,5
clr47:	clear	BIT4,BIT3,BIT2,BIT1,BIT0,BIT7,BIT6,BIT5,4,7
clr51:	clear	BIT5,BIT6,BIT7,BIT0,BIT1,BIT2,BIT3,BIT4,5,1
clr53:	clear	BIT5,BIT0,BIT3,BIT6,BIT1,BIT4,BIT7,BIT2,5,3
clr55:	clear	BIT5,BIT2,BIT7,BIT4,BIT1,BIT6,BIT3,BIT0,5,5
clr57:	clear	BIT5,BIT4,BIT3,BIT2,BIT1,BIT0,BIT7,BIT6,5,7
clr61:	clear	BIT6,BIT7,BIT0,BIT1,BIT2,BIT3,BIT4,BIT5,6,1
clr63:	clear	BIT6,BIT1,BIT4,BIT7,BIT2,BIT5,BIT0,BIT3,6,3
clr65:	clear	BIT6,BIT3,BIT0,BIT5,BIT2,BIT7,BIT4,BIT1,6,5
clr67:	clear	BIT6,BIT5,BIT4,BIT3,BIT2,BIT1,BIT0,BIT7,6,7
clr71:	clear	BIT7,BIT0,BIT1,BIT2,BIT3,BIT4,BIT5,BIT6,7,1
clr73:	clear	BIT7,BIT2,BIT5,BIT0,BIT3,BIT6,BIT1,BIT4,7,3
clr75:	clear	BIT7,BIT4,BIT1,BIT6,BIT3,BIT0,BIT5,BIT2,7,5
clr77:	clear	BIT7,BIT6,BIT5,BIT4,BIT3,BIT2,BIT1,BIT0,7,7

clr201:	clear2	BIT0,BIT1,0,1,clr221
clr211:	clear2	BIT1,BIT2,1,1,clr231
clr221:	clear2	BIT2,BIT3,2,1,clr241
clr231:	clear2	BIT3,BIT4,3,1,clr251
clr241:	clear2	BIT4,BIT5,4,1,clr261
clr251:	clear2	BIT5,BIT6,5,1,clr271
clr261:	clear2	BIT6,BIT7,6,1,clr201
clr271:	clear2	BIT7,BIT0,7,1,clr211
clr203:	clear2	BIT0,BIT3,0,3,clr263
clr213:	clear2	BIT1,BIT4,1,3,clr273
clr223:	clear2	BIT2,BIT5,2,3,clr203
clr233:	clear2	BIT3,BIT6,3,3,clr213
clr243:	clear2	BIT4,BIT7,4,3,clr223
clr253:	clear2	BIT5,BIT0,5,3,clr233
clr263:	clear2	BIT6,BIT1,6,3,clr243
clr273:	clear2	BIT7,BIT2,7,3,clr253
clr205:	clear2	BIT0,BIT5,0,5,clr225
clr215:	clear2	BIT1,BIT6,1,5,clr235
clr225:	clear2	BIT2,BIT7,2,5,clr245
clr235:	clear2	BIT3,BIT0,3,5,clr255
clr245:	clear2	BIT4,BIT1,4,5,clr265
clr255:	clear2	BIT5,BIT2,5,5,clr275
clr265:	clear2	BIT6,BIT3,6,5,clr205
clr275:	clear2	BIT7,BIT4,7,5,clr215
clr207:	clear2	BIT0,BIT7,0,7,clr267
clr217:	clear2	BIT1,BIT0,1,7,clr277
clr227:	clear2	BIT2,BIT1,2,7,clr207
clr237:	clear2	BIT3,BIT2,3,7,clr217
clr247:	clear2	BIT4,BIT3,4,7,clr227
clr257:	clear2	BIT5,BIT4,5,7,clr237
clr267:	clear2	BIT6,BIT5,6,7,clr247
clr277:	clear2	BIT7,BIT6,7,7,clr257

clr301:	clear3	BIT0,0,1,clr311
clr311:	clear3	BIT1,1,1,clr321
clr321:	clear3	BIT2,2,1,clr331
clr331:	clear3	BIT3,3,1,clr341
clr341:	clear3	BIT4,4,1,clr351
clr351:	clear3	BIT5,5,1,clr361
clr361:	clear3	BIT6,6,1,clr371
clr371:	clear3	BIT7,7,1,clr301
clr303:	clear3	BIT0,0,3,clr333
clr313:	clear3	BIT1,1,3,clr343
clr323:	clear3	BIT2,2,3,clr353
clr333:	clear3	BIT3,3,3,clr363
clr343:	clear3	BIT4,4,3,clr373
clr353:	clear3	BIT5,5,3,clr303
clr363:	clear3	BIT6,6,3,clr313
clr373:	clear3	BIT7,7,3,clr323
clr305:	clear3	BIT0,0,5,clr355
clr315:	clear3	BIT1,1,5,clr365
clr325:	clear3	BIT2,2,5,clr375
clr335:	clear3	BIT3,3,5,clr305
clr345:	clear3	BIT4,4,5,clr315
clr355:	clear3	BIT5,5,5,clr325
clr365:	clear3	BIT6,6,5,clr335
clr375:	clear3	BIT7,7,5,clr345
clr307:	clear3	BIT0,0,7,clr377
clr317:	clear3	BIT1,1,7,clr307
clr327:	clear3	BIT2,2,7,clr317
clr337:	clear3	BIT3,3,7,clr327
clr347:	clear3	BIT4,4,7,clr337
clr357:	clear3	BIT5,5,7,clr347
clr367:	clear3	BIT6,6,7,clr357
clr377:	clear3	BIT7,7,7,clr367

clr401:	clear4	BIT0,0,1,clr411
clr411:	clear4	BIT1,1,1,clr421
clr421:	clear4	BIT2,2,1,clr431
clr431:	clear4	BIT3,3,1,clr441
clr441:	clear4	BIT4,4,1,clr451
clr451:	clear4	BIT5,5,1,clr461
clr461:	clear4	BIT6,6,1,clr471
clr471:	clear4	BIT7,7,1,clr401
clr403:	clear4	BIT0,0,3,clr433
clr413:	clear4	BIT1,1,3,clr443
clr423:	clear4	BIT2,2,3,clr453
clr433:	clear4	BIT3,3,3,clr463
clr443:	clear4	BIT4,4,3,clr473
clr453:	clear4	BIT5,5,3,clr403
clr463:	clear4	BIT6,6,3,clr413
clr473:	clear4	BIT7,7,3,clr423
clr405:	clear4	BIT0,0,5,clr455
clr415:	clear4	BIT1,1,5,clr465
clr425:	clear4	BIT2,2,5,clr475
clr435:	clear4	BIT3,3,5,clr405
clr445:	clear4	BIT4,4,5,clr415
clr455:	clear4	BIT5,5,5,clr425
clr465:	clear4	BIT6,6,5,clr435
clr475:	clear4	BIT7,7,5,clr445
clr407:	clear4	BIT0,0,7,clr477
clr417:	clear4	BIT1,1,7,clr407
clr427:	clear4	BIT2,2,7,clr417
clr437:	clear4	BIT3,3,7,clr427
clr447:	clear4	BIT4,4,7,clr437
clr457:	clear4	BIT5,5,7,clr447
clr467:	clear4	BIT6,6,7,clr457
clr477:	clear4	BIT7,7,7,clr467

;***********************************************************************
; For Pentium machines only - 60 bit factors
;***********************************************************************

;
; Check all the bits in the sieve looking for a factor to test
;

tlp60:	mov	esi, sieve
	mov	fac1, savefac1
	mov	fac2, savefac2
	sub	edx, edx
	mov	eax, [esi]
	lea	esi, [esi+4]
tx0:	test	al, 01h
	jnz	tst0
tx1:	test	al, 02h
	jnz	tst1
tx2:	test	al, 04h
	jnz	tst2
tx3:	test	al, 08h
	jnz	tst3
tx4:	test	al, 10h
	jnz	tst4
tx5:	test	al, 20h
	jnz	tst5
tx6:	test	al, 40h
	jnz	tst6
tx7:	test	al, 80h
	jnz	tst7
tx8:	test	eax, 100h
	jnz	tst8
tx9:	test	eax, 200h
	jnz	tst9
tx10:	test	eax, 400h
	jnz	tst10
tx11:	test	eax, 800h
	jnz	tst11
tx12:	test	eax, 1000h
	jnz	tst12
tx13:	test	eax, 2000h
	jnz	tst13
tx14:	test	eax, 4000h
	jnz	tst14
tx15:	test	eax, 8000h
	jnz	tst15
tx16:	test	eax, 10000h
	jnz	tst16
tx17:	test	eax, 20000h
	jnz	tst17
tx18:	test	eax, 40000h
	jnz	tst18
tx19:	test	eax, 80000h
	jnz	tst19
tx20:	test	eax, 100000h
	jnz	tst20
tx21:	test	eax, 200000h
	jnz	tst21
tx22:	test	eax, 400000h
	jnz	tst22
tx23:	test	eax, 800000h
	jnz	tst23
tx24:	test	eax, 1000000h
	jnz	tst24
tx25:	test	eax, 2000000h
	jnz	tst25
tx26:	test	eax, 4000000h
	jnz	tst26
tx27:	test	eax, 8000000h
	jnz	tst27
tx28:	test	eax, 10000000h
	jnz	short tst28
tx29:	test	eax, 20000000h
	jnz	short tst29
tx30:	test	eax, 40000000h
	jnz	short tst30
tx31:	test	eax, 80000000h
	jnz	short tst31
tlp5:	add	fac2, facdist32		; U - Add facdist * 32 to the factor
	mov	eax, [esi]		; V - Next sieve word
	adc	fac1, facdist32+4	; U - Add carry
	test	esi, sievesize		; V - End of sieve?
	lea	esi, [esi+4]		; U - Bump pointer
	jz	tx0			; V - Loop to test next sieve dword
	mov	savefac1, fac1		; Save for the restart or more sieving
	mov	savefac2, fac2

; Check repetition counter

	dec	reps
	jnz	slp0

; Return so caller can check for ESC

	mov	eax, 2			; Return for ESC check
	mov	FACMSW, fac1
	jmp	done

; Entry points for testing each bit#

tst0:	mov	dl, 0			; Test factor corresponding to bit 0
	jmp	testit0
tst28:	mov	dl, 28			; Test factor corresponding to bit 28
	jmp	short testit
tst29:	mov	dl, 29			; Test factor corresponding to bit 29
	jmp	short testit
tst30:	mov	dl, 30			; Test factor corresponding to bit 30
	jmp	short testit
tst31:	mov	dl, 31			; Test factor corresponding to bit 31
	jmp	short testit
tst27:	mov	dl, 27			; Test factor corresponding to bit 27
	jmp	short testit
tst26:	mov	dl, 26			; Test factor corresponding to bit 26
	jmp	short testit
tst25:	mov	dl, 25			; Test factor corresponding to bit 25
	jmp	short testit
tst24:	mov	dl, 24			; Test factor corresponding to bit 24
	jmp	short testit
tst23:	mov	dl, 23			; Test factor corresponding to bit 23
	jmp	short testit
tst22:	mov	dl, 22			; Test factor corresponding to bit 22
	jmp	short testit
tst21:	mov	dl, 21			; Test factor corresponding to bit 21
	jmp	short testit
tst20:	mov	dl, 20			; Test factor corresponding to bit 20
	jmp	short testit
tst19:	mov	dl, 19			; Test factor corresponding to bit 19
	jmp	short testit
tst18:	mov	dl, 18			; Test factor corresponding to bit 18
	jmp	short testit
tst17:	mov	dl, 17			; Test factor corresponding to bit 17
	jmp	short testit
tst16:	mov	dl, 16			; Test factor corresponding to bit 16
	jmp	short testit
tst15:	mov	dl, 15			; Test factor corresponding to bit 15
	jmp	short testit
tst14:	mov	dl, 14			; Test factor corresponding to bit 14
	jmp	short testit
tst13:	mov	dl, 13			; Test factor corresponding to bit 13
	jmp	short testit
tst12:	mov	dl, 12			; Test factor corresponding to bit 12
	jmp	short testit
tst11:	mov	dl, 11			; Test factor corresponding to bit 11
	jmp	short testit
tst10:	mov	dl, 10			; Test factor corresponding to bit 10
	jmp	short testit
tst9:	mov	dl, 9			; Test factor corresponding to bit 9
	jmp	short testit
tst8:	mov	dl, 8			; Test factor corresponding to bit 8
	jmp	short testit
tst7:	mov	dl, 7			; Test factor corresponding to bit 7
	jmp	short testit
tst6:	mov	dl, 6			; Test factor corresponding to bit 6
	jmp	short testit
tst5:	mov	dl, 5			; Test factor corresponding to bit 5
	jmp	short testit
tst4:	mov	dl, 4			; Test factor corresponding to bit 4
	jmp	short testit
tst3:	mov	dl, 3			; Test factor corresponding to bit 3
	jmp	short testit
tst2:	mov	dl, 2			; Test factor corresponding to bit 2
	jmp	short testit
tst1:	mov	dl, 1			; Test factor corresponding to bit 1

;
; The sieve has suggested we test this factor.  Check it out.
; OPTIMIZED FOR PENTIUMS
;
; eax = sieve word - must be preserved
; ebx = fac1 - must be preserved as fac1 for sieve bit 0
; ecx = fac2 - must be preserved as fac2 for sieve bit 0
; edx = sieve bit being tested - must return with top 24 bits zero
; esi = sieve address - must be preserved
;

; Precompute 1 / factor

testit:	add	fac2, facdists[edx*8]	; Determine factor to test
	adc	fac1, facdists+4[edx*8]	; Add carry
testit0:mov	savefac2, fac2		; U - Store factor so FPU can load it
	mov	savefac1, fac1		; V - Store factor so FPU can load it
	fild	QWORD PTR savefac2	; Load 64-bit factor (3 clocks)
	fild	savefac2		; faclo
	fld	initval			; rem^2
	fdivrp	st(2), st		; faclo, q

; The FDIV takes 39 clocks and only the last two can overlap
; with other float instructions.  This gives us 37 clocks
; to do something useful with the integer units.

	mov	edi, returns5[edx*4]	; U - Load return address
	mov	ebp, facdists[edx*8]	; V - fac2 adjustment
	pusher	edi			; U - Push return address
	pusher	eax			; V - Save sieve test register

	; Recompute original fac1 and fac2.  That is, the fac1/fac2 values
	; for the first sieve bit in EAX 
	sub	fac2, ebp		; U - Recompute original fac2
	mov	edi, facdists+4[edx*8]	; V - fac1 adjustment
	sbb	fac1, edi		; U - Recompute original fac1
	pusher	ecx			; V - Save another register

	; Compute the shift count to create floating point values
	; We'd like to use the BSF instruction but it is very slow.
	; Simulate BSF by taking advantage of the fact that factors
	; increase in size very slowly.
	mov	eax, savefac1		; U - Load fac MSW
	mov	edx, savefac2		; V - Load fac LSW
	mov	ecx, fachi_shf_count	; U - Load last shift count
	mov	ebp, fachi_shf_mask	; V - Load last shift mask
fhilp:	test	eax, ebp		; U - Is value wider than last time?
	jz	short fhiok		; V - No, jump
	add	ebp, ebp		; U - Compute new shift mask
	dec	ecx			; V - Decrement the shift count
	mov	fachi_shf_count, ecx	; U - Save the new shift count
	mov	fachi_shf_mask, ebp	; V - Save the new shift mask
	jmp	short fhilp		; V - Loop in case fachi is much wider
fhiok:

	; Compute the floating value (1-fac)/2 = -(fac-1)/2
	shld	eax, edx, cl		;5UV - Normalize (top bit always on)
	shr	eax, 8			; U - Make room for the exponent
	adc	eax, 0DE000000h		; U - Round and normalize exponent!
	 mov	edi, 80000001h		; V - Increment and change sign
	shl	ecx, 23			; U - Put shift count in exponent byte
	 mov	ebp, savefac1		; V - Copy fac MSW (will become FACHI)
	sub	eax, ecx		; U - Sub shift count float exponent
	 mov	ecx, fachi_shf_count	; V - Reload shift count

	; Compute the floating value (fac+1)/2.  Well, we don't have
	; enough free cycles to do that.  At worst, computing fac+1 will
	; cause a carry, so just increment the fac-1 float's LSB.  Note that
	; we can afford to be sloppy as cmpval is only used to quickly
	; eliminate most remainders.
	add	edi, eax		; U - Forms (fac+1)/2 float

	; Now merge the info on the two comparison values (the indented code).
	; Also, convert FACHI to floating point format.  Round FACHI up
	; if FACLO is negative.  Handle overflows by decrementing 
	; the shift count.
	add	edx, edx		; V - Set carry if FACLO is negative
	adc	ebp, 0			; U - Increment FACHI if FACLO neg.
	 xor	eax, edi		; V - The bits that are different
	shl	ebp, cl			;4UV - Normalize (top bit always on)
	jnc	short fhiok1		; UV - Did FACHI incr cause an oflow?
	mov	ebp, 80000000h		; U - The true normalized value
	dec	ecx			; V - The true shift count
fhiok1:	mov	edx, ebp		; U - Now build the IEEE float
	 xor	eax, 0FFFFFFFFh		; V - The bits that are the same
	shr	ebp, 11			; U - High 32 bits of the float
	 mov	cmpvalmask, eax		; V - Save this mask
	shl	ecx, 20			; U - Put shift count in exponent byte
	 and	eax, edi		; V - The value to compare against
	add	ebp, 43D00000h 		; U - Make the exponent byte right
	 mov	cmpval, eax		; V - Save the compare value
	shl	edx, 21			; U - Low 32 bits of the float
	sub	ebp, ecx		; V - Sub shift count float exponent
	mov	DWORD PTR FACHI, edx	; U - Store FACHI LSW
	mov	DWORD PTR FACHI+4, ebp	; V - Store FACHI MSW

	; More miscellaneous initialization
	mov	ecx, sqloop_counter	; U - Load loop counter
	sub	edx, edx		; V - Clear edx

;
; Perform a division on the initial value to get started.
;

	fld	BIGVAL0			; bigval0, faclo, q
	fld	BIGVAL1			; bigval1, bigval0, faclo, q
	fadd	st, st(3)		; qhi, bigval0, faclo, q
	fxch	st(1)			; bigval0, qhi, faclo, q
	fadd	st, st(3)		; qlo, qhi, faclo, q
	fld	FACHI			; fachi, qlo, qhi, faclo, q
	fxch	st(2)			; qhi, qlo, fachi, faclo, q
	fsub	BIGVAL1			; qhi, qlo, fachi, faclo, q
	fxch	st(1)			; qlo, qhi, fachi, faclo, q
	fsub	BIGVAL0			; qlo, qhi, fachi, faclo, q
	fld	st(2)			; fachi, qlo, qhi, fachi, faclo, q
	fmul	st, st(2)		; qhi*fhi, qlo, qhi, fhi, flo, q
	fxch	st(2)			; qhi, qlo, qhi*fhi, fhi, flo, q
	fsub	st(1), st		; qhi, qlo, qhi*fhi, fhi, flo, q
	fmul	st, st(4)		; qhi*flo, qlo, qhi*fhi, fhi, flo, q
	fxch	st(2)			; qhi*fhi, qlo, qhi*flo, fhi, flo, q
	fsubr	initval			; res, qlo, qhi*flo, fhi, flo, q
	fxch	st(1)			; qlo, res, qhi*flo, fhi, flo, q
	fmul	st(3), st		; qlo, res, qhi*flo, qlo*fhi, flo, q
					; STALL
	fxch	st(2)			; qhi*flo, res, qlo, qlo*fhi, flo, q
	fsubp	st(1), st		; res, qlo, qlo*fhi, flo, q
	fxch	st(1)			; qlo, res, qlo*fhi, flo, q
	fmul	st, st(3)		; qlo*flo, res, qlo*fhi, flo, q
					; STALL
	fxch	st(2)			; qlo*fhi, res, qlo*flo, flo, q
	fsubp	st(1), st		; res, qlo*flo, flo, q
	fxch	st(2)			; flo, qlo*flo, res, q
	fstp	FACLO			; qlo*flo, res, q
	fsubp	st(1), st		; res, q
	fxch	st(1)			; q, res
	fmul	initval_inv		; 1/fac, res
	fxch	st(1)			; res, 1/fac
	fld	FACHI			; fachi, res, 1/fac
	fld	st(1)			; res, fachi, res, 1/fac

; Square remainder and get new remainder
; The remainder is 61 bits, fac is 60 bits.
; At start of loop,
;	st(3) contains 1/fac,
;	st(2) contains remainder
;	st(1) contains FACHI
;	st(0) contains remainder
; basic algorithm:
; Let q = rem * rem * 1/fac
; Let rem = remlo * remlo - qlo * faclo +
;	    2^32 * (2 * remlo * remhi - qlo * fachi - qhi * faclo) +
;	    2^64 * (remhi * remhi - qhi * fachi)
;
; q is 61 + 61 - 60 = 62 bits
; remhi is 29 bits, remlo is 31 bits
; qhi is 30 bits, qlo is 31 bits
; fachi is 28 bits, faclo is 31 bits
;
; NOTES:
;
; The end of this loops uses some instructions that would otherwise be
; STALLs to produce two copies of the remainder.
;
; This code also delays doubling the remainder until the start of
; the next loop.  Furthermore, the remainder used to compute the quotient
; is not doubled, rather the 1/fac value is divided by four.
;
; Special thanks to Peter-Lawrence Montgomery for proving that the
; error in computing rem * rem * 1/fac will never exceed 1/2.
;

sqloop:	fmul	st, st			;1 rem^2,fhi,rem
	fld	BIGVAL1			;2 bigval1,rem^2,fhi,rem
	fxch	st(3)			;  rem,rem^2,fhi,bigval1
	fmul	REMMULTS[ecx*4+4]	;3 rem,rem^2,fhi,bigval1
	fld	BIGVAL1			;4 bigval1,rem,rem^2,fhi,bigval1
	fxch	st(2)			;  rem^2,rem,bigval1,fhi,bigval1
	fmul	st(0), st(5)		;5 q,rem,bigval1,fhi,bigval1
	fxch	st(1)			;  rem,q,bigval1,fhi,bigval1
	fadd	st(2), st(0)		;6 rem,q,rhi,fhi,bigval1
	fld	FACHI			;7 fhi,rem,q,rhi,fhi,bigval1
	fxch	st(2)			;  q,rem,fhi,rhi,fhi,bigval1
	fadd	st(5), st(0)		;8 q,rem,fhi,rhi,fhi,qhi
	fxch	st(3)			;  rhi,rem,fhi,q,fhi,qhi
	fsub	BIGVAL1			;9 rhi,rem,fhi,q,fhi,qhi
	fxch	st(3)			;  q,rem,fhi,rhi,fhi,qhi
	fadd	BIGVAL0			;10 qlo,rem,fhi,rhi,fhi,qhi
	fxch	st(5)			;   qhi,rem,fhi,rhi,fhi,qlo
	fsub	BIGVAL1			;11 qhi,rem,fhi,rhi,fhi,qlo
	fld	st(3)			;12 rhi,qhi,rem,fhi,rhi,fhi,qlo
	fmul	st, st			;13 res,qhi,rem,fhi,rhi,fhi,qlo
	fxch	st(1)			;   qhi,res,rem,fhi,rhi,fhi,qlo
	fsub	st(6), st(0)		;14 qhi,res,rem,fhi,rhi,fhi,qlo
	fmul	st(3), st(0)		;15 qhi,res,rem,qhi*fhi,rhi,fhi,qlo
	fxch	st(2)			;   rem,res,qhi,qhi*fhi,rhi,fhi,qlo
	fsub	st(0), st(4)		;16 rlo,res,qhi,qhi*fhi,rhi,fhi,qlo
	fxch	st(2)			;   qhi,res,rlo,qhi*fhi,rhi,fhi,qlo
	fmul	FACLO			;17 qhi*flo,res,rlo,qhi*fhi,rhi,fhi,qlo
	fxch	st(6)			;   qlo,res,rlo,qhi*fhi,rhi,fhi,qhi*flo
	fsub	BIGVAL0			;18 qlo,res,rlo,qhi*fhi,rhi,fhi,qhi*flo
	fxch	st(2)			;   rlo,res,qlo,qhi*fhi,rhi,fhi,qhi*flo
	fmul	st(4), st(0)		;19 rlo,res,qlo,qhi*fhi,rhi*rlo,fhi,etc
	fxch	st(3)			;   qhi*fhi,res,qlo,rlo,rhi*rlo,fhi,etc
	fsubp	st(1), st(0)		;20 res,qlo,rlo,rhi*rlo,fhi,qhi*flo
	fxch	st(1)			;   qlo,res,rlo,rhi*rlo,fhi,qhi*flo
	fmul	st(4), st(0)		;21 qlo,res,rlo,rhi*rlo,qlo*fhi,qhi*flo
	fxch	st(5)			;   qhi*flo,res,rlo,rhi*rlo,qlo*fhi,qlo
	fsubr	st(0), st(3)		;22 resmid0,res,rlo,rhi*rlo,qlo*fhi,qlo
	fxch	st(2)			;   rlo,res,resmid0,rhi*rlo,qlo*fhi,qlo
	fmul	st, st			;23 rlo*rlo,res,rm0,rhi*rlo,qlo*fhi,qlo
	fxch	st(4)			;   qlo*fhi,res,rm0,rhi*rlo,rlo*rlo,qlo
	fsubp	st(3), st(0)		;24 res,resmid0,resmid1,rlo*rlo,qlo
	fxch	st(4)			;   qlo,resmid0,resmid1,rlo*rlo,res
	fmul	FACLO			;25 qlo*flo,resmid0,resmid1,rlo*rlo,res
	fxch	st(4)			;   res,resmid0,resmid1,rlo*rlo,qlo*flo
	faddp	st(1), st(0)		;26 res,resmid1,rlo*rlo,qlo*flo
	fld	FACHI			;27 fhi,res,resmid1,rlo*rlo,qlo*flo
	fxch	st(3)			;   rlo*rlo,res,resmid1,fhi,qlo*flo
	fsubrp	st(4), st(0)		;28 res,resmid1,fhi,reslo
	faddp	st(1), st(0)		;29 res,fhi,reslo
	fxch	st(3)			;
	fmul	FACMULTS[ecx*4]		;30 Adjust 1/fac if rem should be dbl'd
	fxch	st(3)			;
	fld	st(2)			;31 reslo,res,fhi,reslo
	fadd	st(0), st(1)		;32 rem,res,fhi,reslo
	fxch	st(1)			;   res,rem,fhi,reslo
	faddp	st(3), st(0)		;33 rem,fhi,rem
	dec	ecx			;34u
	jnz	sqloop			;34v

;
; If result = 1 mod factor, then we found a divisor of 2**p - 1.
; As a quick test, compare the top 32 bits of the float using integer
; instructions.  This will quickly eliminate most values.
;

					; rem, fachi, rem, 1/fac
	fst	temp			; Store the undoubled remainder
	mov	eax, temp		; U - Load the undoubled remainder
	mov	ecx, cmpvalmask		; V - Load the value to compare against
	and	eax, ecx		; U - Only test some of the bits
	mov	ecx, cmpval		; V - Load the value to compare against
	cmp	eax, ecx		; U - Do the values match?
	je	short smaybe		; V - Yes, it may be a factor
	fcompp				; Pop two floats
	fcompp				; Pop two floats
	pop	ecx			; U - Restore sieve registers
	pop	eax			; V - Restore sieve registers
	retn				; UV - Test next factor from sieve
	; Subtract one and test if number equals factor or -factor.
smaybe:	fadd	st, st			; dbl rem, trash, trash, trash
	fxch	st(3)			; trash, trash, trash, result
	fcomp	st(0)			; Pop one trash value
	fcompp				; Pop two trash values
	fsub	ONE			; Subtract one
	fild	QWORD PTR savefac2	; factor, result
	fxch	st(1)			; result, factor
	fabs				; Take absolute value of result
	fcompp				; Does result = factor
	fstsw	ax			; Copy comparison results
	and	eax, 4000h		; Isolate C3 bit
	jnz	short winner
	pop	ecx			; Restore sieve testing register
	pop	eax			; Restore sieve testing register
	retn				; UV - Test next factor from sieve

winner:	mov	eax, savefac1		; Load MSW
	mov	FACMSW, eax
	mov	eax, savefac2		; Load LSW
	mov	FACLSW, eax
	mov	eax, 1			; Factor found!!! Return TRUE
	add	esp, 12			; pop sieve testing registers and
					; return address
	jmp	done

;***********************************************************************
; For Pentium machines only - 62 bit factors
;***********************************************************************

;
; Check all the bits in the sieve looking for a factor to test
;

	push_amt = 0
tlp62:	mov	esi, sieve
	mov	ebx, savefac1
	mov	ebp, savefac2
	sub	edx, edx
	mov	eax, [esi]
	lea	esi, [esi+4]
	mov	edi, -2			; Count of queued factors to be tested
vx0:	test	al, 01h
	jnz	vst0
vx1:	test	al, 02h
	jnz	vst1
vx2:	test	al, 04h
	jnz	vst2
vx3:	test	al, 08h
	jnz	vst3
vx4:	test	al, 10h
	jnz	vst4
vx5:	test	al, 20h
	jnz	vst5
vx6:	test	al, 40h
	jnz	vst6
vx7:	test	al, 80h
	jnz	vst7
vx8:	test	eax, 100h
	jnz	vst8
vx9:	test	eax, 200h
	jnz	vst9
vx10:	test	eax, 400h
	jnz	vst10
vx11:	test	eax, 800h
	jnz	vst11
vx12:	test	eax, 1000h
	jnz	vst12
vx13:	test	eax, 2000h
	jnz	vst13
vx14:	test	eax, 4000h
	jnz	vst14
vx15:	test	eax, 8000h
	jnz	vst15
vx16:	test	eax, 10000h
	jnz	vst16
vx17:	test	eax, 20000h
	jnz	vst17
vx18:	test	eax, 40000h
	jnz	vst18
vx19:	test	eax, 80000h
	jnz	vst19
vx20:	test	eax, 100000h
	jnz	vst20
vx21:	test	eax, 200000h
	jnz	vst21
vx22:	test	eax, 400000h
	jnz	vst22
vx23:	test	eax, 800000h
	jnz	vst23
vx24:	test	eax, 1000000h
	jnz	vst24
vx25:	test	eax, 2000000h
	jnz	vst25
vx26:	test	eax, 4000000h
	jnz	vst26
vx27:	test	eax, 8000000h
	jnz	vst27
vx28:	test	eax, 10000000h
	jnz	short vst28
vx29:	test	eax, 20000000h
	jnz	short vst29
vx30:	test	eax, 40000000h
	jnz	short vst30
vx31:	test	eax, 80000000h
	jnz	short vst31
vlp5:	add	ebp, facdist32		; U - Add facdist * 32 to the factor
	mov	eax, [esi]		; V - Next sieve word
	adc	ebx, facdist32+4	; U - Add carry
	test	esi, sievesize		; V - End of sieve?
	lea	esi, [esi+4]		; U - Bump pointer
	jz	vx0			; V - Loop to test next sieve dword
	jmp	vlpdn

; Entry points for testing each bit#

vst0:	mov	dl, 0			; Test factor corresponding to bit 0
	jmp	vestit0
vst28:	mov	dl, 28			; Test factor corresponding to bit 28
	jmp	short vestit
vst29:	mov	dl, 29			; Test factor corresponding to bit 29
	jmp	short vestit
vst30:	mov	dl, 30			; Test factor corresponding to bit 30
	jmp	short vestit
vst31:	mov	dl, 31			; Test factor corresponding to bit 31
	jmp	short vestit
vst27:	mov	dl, 27			; Test factor corresponding to bit 27
	jmp	short vestit
vst26:	mov	dl, 26			; Test factor corresponding to bit 26
	jmp	short vestit
vst25:	mov	dl, 25			; Test factor corresponding to bit 25
	jmp	short vestit
vst24:	mov	dl, 24			; Test factor corresponding to bit 24
	jmp	short vestit
vst23:	mov	dl, 23			; Test factor corresponding to bit 23
	jmp	short vestit
vst22:	mov	dl, 22			; Test factor corresponding to bit 22
	jmp	short vestit
vst21:	mov	dl, 21			; Test factor corresponding to bit 21
	jmp	short vestit
vst20:	mov	dl, 20			; Test factor corresponding to bit 20
	jmp	short vestit
vst19:	mov	dl, 19			; Test factor corresponding to bit 19
	jmp	vestit
vst18:	mov	dl, 18			; Test factor corresponding to bit 18
	jmp	short vestit
vst17:	mov	dl, 17			; Test factor corresponding to bit 17
	jmp	short vestit
vst16:	mov	dl, 16			; Test factor corresponding to bit 16
	jmp	short vestit
vst15:	mov	dl, 15			; Test factor corresponding to bit 15
	jmp	short vestit
vst14:	mov	dl, 14			; Test factor corresponding to bit 14
	jmp	short vestit
vst13:	mov	dl, 13			; Test factor corresponding to bit 13
	jmp	short vestit
vst12:	mov	dl, 12			; Test factor corresponding to bit 12
	jmp	short vestit
vst11:	mov	dl, 11			; Test factor corresponding to bit 11
	jmp	short vestit
vst10:	mov	dl, 10			; Test factor corresponding to bit 10
	jmp	short vestit
vst9:	mov	dl, 9			; Test factor corresponding to bit 9
	jmp	short vestit
vst8:	mov	dl, 8			; Test factor corresponding to bit 8
	jmp	short vestit
vst7:	mov	dl, 7			; Test factor corresponding to bit 7
	jmp	short vestit
vst6:	mov	dl, 6			; Test factor corresponding to bit 6
	jmp	short vestit
vst5:	mov	dl, 5			; Test factor corresponding to bit 5
	jmp	short vestit
vst4:	mov	dl, 4			; Test factor corresponding to bit 4
	jmp	short vestit
vst3:	mov	dl, 3			; Test factor corresponding to bit 3
	jmp	short vestit
vst2:	mov	dl, 2			; Test factor corresponding to bit 2
	jmp	short vestit
vst1:	mov	dl, 1			; Test factor corresponding to bit 1

;
; This is the Pentium version of testit for 62-bit factors.  It gathers
; 2 potential factors to be tested together to minimize processor stalls.
;
; OPTIMIZED FOR PENTIUMS
;
; eax = sieve word - must be preserved or reloaded
; ebx = fachi - must be preserved as fachi for sieve bit 0
; edx = sieve bit being tested - must return with top 24 bits zero
; edi = count of potential factors queued up - must return -2
; esi = sieve address - must be preserved
; ebp = faclo - must be preserved as faclo for sieve bit 0
;
; Total for two exponents - assuming 18 interior loops:
; 2*43 + 89 + 70*17 + 4 (mispredicted jump) + 33 = 702

;
; Precompute initval / factor
;

vestit:	add	ebp, facdists[edx*8]	; Determine factor to test
	adc	ebx, facdists+4[edx*8]	; Add carry
vestit0:mov	savefac2, ebp		;1U Save factor so FPU can load it
	mov	savefac1, ebx		;1V
	fild	QWORD PTR savefac2	;2  fac
	fild	savefac2		;3  faclo, fac
	fld	initval			;4  initval, faclo, fac
	fdivr	st(2), st(0)		;5-43 initval, faclo, quot

; The FDIV takes 39 clocks and only the last two can overlap
; with other float instructions.  This gives us 37 clocks
; to do something useful with the integer units.

	; Compute FACHI for conversion to floating point format.
	; Round FACHI up if FACLO is negative.
	add	ebp, ebp		; U - Set carry if FACLO is negative
	mov	ecx, fachi_shf_count	; V - Load last shift count
	adc	ebx, 0			; U - Increment FACHI if FACLO neg.
	mov	eax, fachi_shf_mask	; V - Load last shift mask

	; Compute the shift count to create floating point values.
	; We'd like to use the BSF instruction but it is very slow.
	; Simulate BSF by taking advantage of the fact that factors
	; increase in size very slowly.
vhilp:	test	ebx, eax		; U - Is value wider than last time?
	jz	short vhiok		; V - No, jump
	add	eax, eax		; U - Compute new shift mask
	dec	ecx			; V - Decrement the shift count
	mov	fachi_shf_count, ecx	; U - Save the new shift count
	mov	fachi_shf_mask, eax	; V - Save the new shift mask
	jmp	short vhilp		; V - Loop in case fachi is much wider

	; We now have enough information to convert FACHI
	; to floating point format.
vhiok:	shl	ebx, cl			;4UV - Normalize (top bit always on)
	mov	ebp, ebx		; U - EBP will hold the float LSW
	mov	eax, returns5a[edx*4]	; V - Load return address
	shr	ebx, 11			; U - EAX will hold the float MSW
	pusher	eax			; V - Push return address
	shl	ecx, 20			; U - Put shift count in exponent byte
	add	ebx, 43D00000h 		; V - Make the exponent byte right
	shl	ebp, 21			; U - Low 32 bits of the float
	sub	ebx, ecx		; V - Sub shift count from exponent

	; Recompute original fachi and faclo.  That is, the fachi/faclo values
	; for the first sieve bit in EAX
	mov	DWORD PTR FACHI[edi*8+16], ebp ; U - Store FACHI LSW
	mov	ebp, savefac2		; V - Reload faclo
	mov	ecx, facdists[edx*8]	; U - faclo adjustment
	mov	DWORD PTR FACHI+4[edi*8+16], ebx ; V - Store FACHI MSW
	mov	ebx, savefac1		; U - Reload fachi
	sub	ebp, ecx		; V - Recompute original faclo
	mov	ecx, facdists+4[edx*8]	; U - fachi adjustment
	mov	eax, [esi-4]		; V - Reload eax
	sbb	ebx, ecx		; U - Recompute original fachi

	inc	edi			;*U - Bump count of queued factors
	jz	short vest2		; V - Jump if enough are queued
	retn				; UV - return to sieve testing

;
; Now test the 2 factors
;

	; More miscellaneous initialization

	push_amt = 4
vest2:	mov	ecx, wqloop_counter	; U - Load loop counter
	mov	edi, -2			; V - Restore queued factor counter

; Work on initval
; At start of loop, registers contain:

					; initval,flo2,q2,initval,flo1,q1
	fld	BIGVAL1			;   for qhi
					; qhi,initval,flo2,q2,initval,flo1,q1
	fadd	st(0),st(6)		;   qhi = quot + BIGVAL1
	fld	BIGVAL0			;1  for q and qlo
					; q,qhi,initval,flo2,q2,initval,flo1,q1
	fadd	st(0),st(7)		;2  q = quot + BIGVAL0
	fxch	st(1)			; qhi,q,initval,flo2,q2,initval,flo1,q1
	fsub	BIGVAL1			;3  qhi = qhi - BIGVAL1
	fxch	st(3)			; flo2,q,initval,qhi,q2,initval,flo1,q1
	fstp	FACLO2			;4-5 Save FACLO2
					; q,initval,qhi,q2,initval,flo,q1
	fsub	BIGVAL0			;6  q = q - BIGVAL0
	fld	FACHI			;7  for qhi*fhi
					; fhi,q,initval,qhi,q2,initval,flo,q1
	fmul	st(0),st(3)		;8  qhi*fhi
					; qhi*fhi,q,initval,qhi,q2,iv,flo,q1
	fxch	st(3)			; qhi,q,initval,qhi*fhi,q2,iv,flo,q1
	fsub	st(1),st(0)		;9  qlo = q - qhi
					; qhi,qlo,initval,qhi*fhi,q2,iv,flo,q1
	fxch	st(6)			; flo,qlo,initval,qhi*fhi,q2,iv,qhi,q1
	fst	FACLO			;10-11 Save FACLO
	fmul	st(6), st(0)		;12 qhi*flo
					; flo,qlo,iv,qhi*fhi,q2,iv,qhi*flo,q1
	fxch	st(3)			; qhi*fhi,qlo,iv,flo,q2,iv,qhi*flo,q1
	fsubp	st(5),st(0)		;13 res = initval - qhi*fhi
					; qlo,initval,flo,q2,res,qhi*flo,q1
	fmul	st(2),st(0)		;14 qlo*flo
					; qlo,initval,qlo*flo,q2,res,qhi*flo,q1
	  fld	BIGVAL1			;15   for qhi
					; qhi,qlo,iv,qlo*flo,q2,res,qhi*flo,q1
	  fxch	st(6)			; qhi*flo,qlo,iv,qlo*flo,q2,res,qhi,q1
	fsubp	st(5),st(0)		;16 res -= qhi*flo
					; qlo,initval,qlo*flo,q2,res,qhi,q1
	fmul	FACHI			;17 qlo*fhi
					; qlo*fhi,initval,qlo*flo,q2,res,qhi,q1
	  fld	BIGVAL0			;18   for q and qlo
					; q,qlo*fhi,iv,qlo*flo,q2,res,qhi,q1
	  fxch	st(4)			; q2,qlo*fhi,iv,qlo*flo,q,res,qhi,q1
	  fadd	st(6),st(0)		;19   qhi = quot + BIGVAL1
	  fxch	st(1)			; qlo*fhi,q2,iv,qlo*flo,q,res,qhi,q1
	fsubp	st(5),st(0)		;20 res -= qlo*fhi
					; q2,iv,qlo*flo,q,res,qhi,q1
	  fadd	st(3),st(0)		;21   q = quot + BIGVAL0
	  fxch	st(6)			; q1,iv,qlo*flo,q,res,qhi,q2
	fmul	initval_inv		;22 1/fac = quot * initval_inv
					; 1/fac,iv,qlo*flo,q,res,qhi,q2
	fxch	st(6)			; q2,iv,qlo*flo,q,res,qhi,1/fac
					; (now on 1/fac1 is assumed on stack)
	  fld	FACHI2			;23   for qhi*fhi
					; fhi,q2,iv,qlo*flo,q,res,qhi
	  fxch	st(6)			; qhi,q2,iv,qlo*flo,q,res,fhi
	  fsub	BIGVAL1			;24   qhi = qhi - BIGVAL1
	  fxch	st(4)			; q,q2,iv,qlo*flo,qhi,res,fhi
	  fsub	BIGVAL0			;25   q = q - BIGVAL0
	  fxch	st(3)			; qlo*flo,q2,iv,q,qhi,res,fhi
	fsubp	st(5),st(0)		;26 rem = res - qlo*flo
					; q2,iv,q,qhi,rem,fhi
	fxch	st(3)			; qhi,iv,q,q2,rem,fhi
	  fmul	st(5),st(0)		;27   qhi*fhi
					; qhi,iv,q,q2,rem,qhi*fhi
	  fsub	st(2),st(0)		;28   qlo = q - qhi
					; qhi,iv,qlo,q2,rem,qhi*fhi
	  fmul	FACLO2			;29   qhi*flo
					; qhi*flo,iv,qlo,q2,rem,qhi*fhi
	  fxch	st(5)			; qhi*fhi,iv,qlo,q2,rem,qhi*flo
	  fsubp	st(1),st(0)		;30   res = initval - qhi*fhi
					; res,qlo,q2,rem,qhi*flo
	  fld	FACHI2			;31   For qlo*fhi
					; fhi,res,qlo,q2,rem,qhi*flo
	  fmul	st(0),st(2)		;32   qlo*fhi
					; qlo*fhi,res,qlo,q2,rem,qhi*flo
	  fxch	st(5)			; qhi*flo,res,qlo,q2,rem,qlo*fhi
	  fsubp	st(1),st(0)		;33   res -= qhi*flo
					; res,qlo,q2,rem,qlo*fhi
	  fxch	st(2)			; q2,qlo,res,rem,qlo*fhi
	  fmul	initval_inv		;34   1/fac = quot * initval_inv
					; 1/fac2,qlo,res,rem,qlo*fhi
	fld	st(5)			;35 quot = 1/fac
					; quot,1/fac2,qlo,res,rem,qlo*fhi
	fxch	st(2)			; qlo,1/fac2,quot,res,rem,qlo*fhi
	  fmul	FACLO2			;36   qlo*flo
					; qlo*flo,1/fac2,quot,res,rem,qlo*fhi
	  fxch	st(5)			; qlo*fhi,1/fac2,quot,res,rem,qlo*flo
	  fsubp	st(3),st(0)		;37   res -= qlo*fhi
					; 1/fac2,quot,res,rem,qlo*flo

; First iteration of vqloop - to "get the pipeline going"

	fsub	st(0), st(5)		;38 facdiff = 1/fac2 - 1/fac1
					; facdiff,quot,res,rem,qlo*flo
	fld	BIGVAL1			;39 For rhi
					; rhi,facdiff,quot,res,rem,qlo*flo
	fxch	st(5)			; qlo*flo,facdiff,quot,res,rem,rhi
	  fsubp	st(3),st(0)		;40   rem = res - qlo*flo
					; facdiff,quot,rem2,rem1,rhi
	  fxch	st(3)			; rem1,quot,rem2,facdiff,rhi
	fadd	st(4),st(0)		;41 rhi = rem + BIGVAL1
	fld	BIGVAL1			;42 for qhi
					; qhi,rem1,quot,rem2,facdiff,rhi
	fxch	st(2)			; quot,rem1,qhi,rem2,facdiff,rhi
	fmul	st(0),st(1)		;43 quot = rem * 1/fac
	fxch	st(4)			; facdiff,rem1,qhi,rem2,quot,rhi
	  fstp	FACDIFF			;44-45 store FACDIFF
					; rem1,qhi,rem2,quot,rhi
	fmul	st(3),st(0)		;46 quot = rem * rem * 1/fac
	fxch	st(4)			; rhi,qhi,rem2,quot,rem1
	fsub	BIGVAL1			;47 rhi = rhi - BIGVAL1
	fld	FACHI			;48 for qhi*fhi
					; fhi,rhi,qhi,rem2,quot,rem1
	fxch	st(4)			; quot,rhi,qhi,rem2,fhi,rem1
	fadd	st(2),st(0)		;49 qhi = quot + BIGVAL1
	fadd	BIGVAL0			;50 quot = quot + BIGVAL0
	fxch	st(5)			; rem1,rhi,qhi,rem2,fhi,quot
	fsub	st(0),st(1)		;51 rlo = rem - rhi
					; rlo,rhi,qhi,rem,fhi,quot
	fxch	st(2)			; qhi,rhi,rlo,rem,fhi,quot
	fsub	BIGVAL1			;52 qhi = qhi - BIGVAL1
	fxch	st(5)			; quot,rhi,rlo,rem,fhi,qhi
	fsub	BIGVAL0			;53 quot = quot - BIGVAL0
	fld	st(1)			;54 for res (dup rhi)
					; res,quot,rhi,rlo,rem,fhi,qhi
	fmul	st,st			;55 res = rhi^2
	fxch	st(2)			; rhi,quot,res,rlo,rem,fhi,qhi
	fadd	st,st			;56 double rhi to compute 2*rhi*rlo
					; 2*rhi,quot,res,rlo,rem,fhi,qhi
	fxch	st(6)			; qhi,quot,res,rlo,rem,fhi,2*rhi
	fmul	st(5),st(0)		;57 qhi*fhi
					; qhi,quot,res,rlo,rem,qhi*fhi,2*rhi
	fsub	st(1),st(0)		;58 qlo = quot - qhi
					; qhi,qlo,res,rlo,rem,qhi*fhi,2*rhi
	fmul	FACLO			;59 qhi*flo
					; qhi*flo,qlo,res,rlo,rem,qhi*fhi,2*rhi
	fxch	st(5)			; qhi*fhi,qlo,res,rlo,rem,qhi*flo,2*rhi
	fsubp	st(2),st(0)		;60 res -= qhi*fhi
					; qlo,res,rlo,rem,qhi*flo,2*rhi
	fxch	st(5)			; 2*rhi,res,rlo,rem,qhi*flo,qlo
	fmul	st(0),st(2)		;61 2*rhi*rlo
					; 2*rhi*rlo,res,rlo,rem,qhi*flo,qlo
	fld	FACHI			;62 For qlo*fhi
					; fhi,2*rhi*rlo,res,rlo,rem,qhi*flo,qlo
	fxch	st(3)			; rlo,2*rhi*rlo,res,fhi,rem,qhi*flo,qlo
	fmul	st,st			;63 rlo*rlo
					; rlo^2,2*rhi*rlo,res,fhi,r,qhi*flo,qlo
	fxch	st(5)			; qhi*flo,2*rhi*rlo,res,fhi,r,rlo^2,qlo
	fsubp	st(1),st(0)		;64 resmid = 2*rhi*rlo - qhi*flo
					; resmid,res,fhi,rem,rlo^2,qlo
	fxch	st(5)			; qlo,res,fhi,rem,rlo^2,resmid
	fmul	st(2),st(0)		;65 qlo*fhi
					; qlo,res,qlo*fhi,rem,rlo^2,resmid
	  fld	FACDIFF			;66   quot = 1/fac2 - 1/fac1
					; quot,qlo,res,qlo*fhi,rem,rlo^2,resmid
	  fxch	st(1)			; qlo,quot,res,qlo*fhi,rem,rlo^2,resmid
	fmul	FACLO			;67 qlo*flo
					; qlo*flo,q,res,qlo*fhi,r,rlo^2,resmid
	fxch	st(6)			; resmid,q,res,qlo*fhi,r,rlo^2,qlo*flo
	faddp	st(2),st(0)		;68 res += resmid
					; quot,res,qlo*fhi,rem,rlo^2,qlo*flo
	  fadd	st(0),st(6)		;69   quot = 1/fac2 - 1/fac1 + 1/fac1
	  fxch	st(5)			; qlo*flo,res,qlo*fhi,rem,rlo^2,quot
	fsubp	st(4),st(0)		;70 reslow = rlo*rlo - qlo*flo
					; res,qlo*fhi,rem,reslow,quot
	fsubrp	st(1),st(0)		;71 res -= qlo*fhi
					; res,rem,reslow,quot
	  fld	BIGVAL1			;72   For rhi
					; rhi,res,rem,reslow,quot
	  fxch	st(4)			; quot,res,rem,reslow,rhi
	  fmul	st(0), st(2)		;73   quot = rem * 1/fac
	  fxch	st(1)			; res,quot,rem,reslow,rhi
	faddp	st(3),st(0)		;74 res += reslow
					; quot,rem,res,rhi
	fxch	st(1)			; rem,quot,res,rhi
	  fadd	st(3), st(0)		;75   rhi = rem + BIGVAL1
					; rem,quot,res,rhi
	  fld	BIGVAL1			;76   for qhi
					; qhi,rem,quot,res,rhi
	  fxch	st(2)			; quot,rem,qhi,res,rhi
	  fmul	st(0), st(1)		;77   quot = rem * rem * 1/fac
	  fxch	st(4)			; rhi,rem,qhi,res,quot
	  fsub	BIGVAL1			;78   rhi = rhi - BIGVAL1
	  fld	FACHI2			;79   for qhi*fhi
					; fhi,rhi,rem,qhi,res,quot
	  fxch	st(5)			; quot,rhi,rem,qhi,res,fhi
	  fadd	st(3), st(0)		;80   qhi = quot + BIGVAL1
	  fadd	BIGVAL0			;81   quot = quot + BIGVAL0
	  fxch	st(2)			; rem,rhi,quot,qhi,res,fhi
	  fsub	st(0), st(1)		;82   rlo = rem - rhi
					; rlo,rhi,quot,qhi,rem,fhi
	  fxch	st(3)			; qhi,rhi,quot,rlo,rem,fhi
	  fsub	BIGVAL1			;83   qhi = qhi - BIGVAL1
	  fxch	st(2)			; quot,rhi,qhi,rlo,rem,fhi
	  fsub	BIGVAL0			;84   quot = quot - BIGVAL0
	  fld	st(1)			;85   for res (dup rhi)
					; res,quot,rhi,qhi,rlo,rem,fhi
	  fmul	st, st			;86   res = rhi^2
	  fxch	st(2)			; rhi,quot,res,qhi,rlo,rem,fhi
	  fadd	st, st			;87   double rhi to compute 2*rhi*rlo
					; 2*rhi,quot,res,qhi,rlo,rem,fhi
	  fxch	st(3)			; qhi,quot,res,2*rhi,rlo,rem,fhi
	  fmul	st(6), st(0)		;88   qhi*fhi
					; qhi,quot,res,2*rhi,rlo,rem,qhi*fhi
	  fsub	st(1), st(0)		;89   qlo = quot - qhi

; Square remainder and get new remainder.
; The remainder is between -fac and fac.  Fac is up to 62 bits.
; Basic algorithm:
; 1) Possibly double the remainder.  Computed as:
;	rem = possible_double (abs (rem)) - fac
; 2) Compute the quotient:  q = rem * rem * 1/fac.  The error will be
;    at most 3/8.
; 3) Compute the new remainder.
;	 rem = remhi * remhi - qhi * fachi +
;	       2 * remlo * remhi - qlo * fachi - qhi * faclo +
;	       remlo * remlo - qlo * faclo
;
; q is 62 + 62 - 62 = 62 bits
; remhi is 30 bits, remlo is signed 31 bits
; qhi is 30 bits, qlo is signed 31 bits
; fachi is 30 bits, faclo is signed 31 bits
;
; At start of loop, registers contain:
;	qhi2, qlo2, res2, 2*rhi2, rlo2, rem1, qhi2*fhi2, 1/fac1

vqloop:					; qhi,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fmul	FACLO2			;1    qhi*flo
					; qhi*flo,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fxch	st(6)			; qhi*fhi,qlo,res,2*rhi,rlo,rem,qhi*flo
	  fsubp	st(2), st(0)		;2    res -= qhi*fhi
					; qlo,res,2*rhi,rlo,rem,qhi*flo
	  fxch	st(2)			; 2*rhi,res,qlo,rlo,rem,qhi*flo
	  fmul	st(0), st(3)		;3    2*rhi*rlo
					; 2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	  fld	FACHI2			;4    For computing qlo*fhi
					; fhi,2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	  fxch	st(4)			; rlo,2*rhi*rlo,res,qlo,fhi,rem,qhi*flo
	  fmul	st, st			;5    rlo*rlo
					; rlo^2,2*rhi*rlo,res,qlo,fhi,r,qhi*flo
	  fxch	st(6)			; qhi*flo,2*rhi*rlo,res,qlo,fhi,r,rlo^2
	  fsubp	st(1), st		;6    resmid = 2*rhi*rlo - qhi*flo
					; resmid,res,qlo,fhi,rem,rlo^2
	  fxch	st(3)			; fhi,res,qlo,resmid,rem,rlo^2
	  fmul	st(0), st(2)		;7    qlo*fhi
					; qlo*fhi,res,qlo,resmid,rem,rlo^2
	  fxch	st(4)			; rem,res,qlo,resmid,qlo*fhi,rlo^2
	fabs				;8  rem = |rem|
	fmul	REMMULTS[ecx*4+4]	;9  rem = rem * 1-or-2
	fxch	st(3)			; resmid,res,qlo,rem,qlo*fhi,rlo^2
	  faddp	st(1), st(0)		;10   res += resmid
					; res,qlo,rem,qlo*fhi,rlo^2
	  fxch	st(1)			; qlo,res,rem,qlo*fhi,rlo^2
	  fmul	FACLO2			;11   qlo*flo
					; qlo*flo,res,rem,qlo*fhi,rlo^2
	  fxch	st(2)			; rem,res,qlo*flo,qlo*fhi,rlo^2
	fsub	FACHI			;12 rem = rem - fachi
	fxch	st(3)			; qlo*fhi,res,qlo*flo,rem,rlo^2
	  fsubp	st(1), st(0)		;13   res -= qlo*fhi
					; res,qlo*flo,rem,rlo^2
	  fxch	st(1)			; qlo*flo,res,rem,rlo^2
	  fsubp	st(3), st(0)		;14   reslow = rlo*rlo - qlo*flo
					; res,rem,reslow
	  fxch	st(1)			; rem,res,reslow
	fsub	FACLO			;15 rem = rem - faclo
	fld	st(3)			;16 quot = 1/fac
					; quot,rem,res,reslow
	fld	BIGVAL1			;17 For rhi
					; rhi,quot,rem,res,reslow
	fxch	st(4)			; reslow,quot,rem,res,rhi
	  faddp	st(3), st(0)		;18   res += reslow
					; quot,rem,res,rhi
	fmul	st(0), st(1)		;19 quot = rem * 1/fac
	fxch	st(1)			; rem,quot,res,rhi
	fadd	st(3), st(0)		;20 rhi = rem + BIGVAL1
	fld	BIGVAL1			;21 for qhi
					; qhi,rem,quot,res,rhi
	fxch	st(2)			; quot,rem,qhi,res,rhi
	fmul	st(0), st(1)		;22 quot = rem * rem * 1/fac
	fxch	st(4)			; rhi,rem,qhi,res,quot
	fsub	BIGVAL1			;23 rhi = rhi - BIGVAL1
	fld	FACHI			;24 for qhi*fhi
					; fhi,rhi,rem,qhi,res,quot
	fxch	st(5)			; quot,rhi,rem,qhi,res,fhi
	fadd	st(3), st(0)		;25 qhi = quot + BIGVAL1
	fadd	BIGVAL0			;26 quot = quot + BIGVAL0
	fxch	st(2)			; rem,rhi,quot,qhi,res,fhi
	fsub	st(0), st(1)		;27 rlo = rem - rhi
					; rlo,rhi,quot,qhi,rem,fhi
	fxch	st(3)			; qhi,rhi,quot,rlo,rem,fhi
	fsub	BIGVAL1			;28 qhi = qhi - BIGVAL1
	fxch	st(2)			; quot,rhi,qhi,rlo,rem,fhi
	fsub	BIGVAL0			;29 quot = quot - BIGVAL0
	fld	st(1)			;30 for res (dup rhi)
					; res,quot,rhi,qhi,rlo,rem,fhi
	fmul	st, st			;31 res = rhi^2
	fxch	st(2)			; rhi,quot,res,qhi,rlo,rem,fhi
	fadd	st, st			;32 double rhi to compute 2*rhi*rlo
	fxch	st(3)			; qhi,quot,res,2*rhi,rlo,rem,fhi
	fmul	st(6), st(0)		;33 qhi*fhi
					; qhi,quot,res,2*rhi,rlo,rem,qhi*fhi
	fsub	st(1), st(0)		;34 qlo = quot - qhi
					; qhi,qlo,res,2*rhi,rlo,rem,qhi*fhi
	fmul	FACLO			;35 qhi*flo
					; qhi*flo,qlo,res,2*rhi,rlo,rem,qhi*fhi
	fxch	st(6)			; qhi*fhi,qlo,res,2*rhi,rlo,rem,qhi*flo
	fsubp	st(2), st(0)		;36 res -= qhi*fhi
					; qlo,res,2*rhi,rlo,rem,qhi*flo
	fxch	st(2)			; 2*rhi,res,qlo,rlo,rem,qhi*flo
	fmul	st(0), st(3)		;37 2*rhi*rlo
					; 2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	fld	FACHI			;38 For qlo*fhi
					; fhi,2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	fxch	st(4)			; rlo,2*rhi*rlo,res,qlo,fhi,rem,qhi*flo
	fmul	st, st			;39 rlo*rlo
					; rlo^2,2*rhi*rlo,res,qlo,fhi,r,qhi*flo
	fxch	st(6)			; qhi*flo,2*rhi*rlo,res,qlo,fhi,r,rlo^2
	fsubp	st(1), st(0)		;40 resmid = 2*rhi*rlo - qhi*flo
					; resmid,res,qlo,fhi,rem,rlo^2
	fxch	st(3)			; fhi,res,qlo,resmid,rem,rlo^2
	fmul	st(0), st(2)		;41 qlo*fhi
					; qlo*fhi,res,qlo,resmid,rem,rlo^2
	fxch	st(4)			; rem,res,qlo,resmid,qlo*fhi,rlo^2
	  fabs				;42   rem = |rem|
	  fmul	REMMULTS[ecx*4+4]	;43   rem = rem * 1-or-2
	  fxch	st(3)			; resmid,res,qlo,rem,qlo*fhi,rlo^2
	faddp	st(1), st(0)		;44 res += resmid
					; res,qlo,rem,qlo*fhi,rlo^2
	fxch	st(1)			; qlo,res,rem,qlo*fhi,rlo^2
	fmul	FACLO			;45 qlo*flo
					; qlo*flo,res,rem,qlo*fhi,rlo^2
	fxch	st(2)			; rem,res,qlo*flo,qlo*fhi,rlo^2
	  fsub	FACHI2			;46   rem = rem - fachi
	  fxch	st(3)			; qlo*fhi,res,qlo*flo,rem,rlo^2
	fsubp	st(1), st(0)		;47 res -= qlo*fhi
					; res,qlo*flo,rem,rlo^2
	fxch	st(1)			; qlo*flo,res,rem,rlo^2
	fsubp	st(3), st(0)		;48 reslow = rlo*rlo - qlo*flo
					; res,rem,reslow
	fxch	st(1)			; rem,res,reslow
	  fsub	FACLO2			;49   rem = rem - faclo
	  fld	FACDIFF			;50   quot = 1/fac2 - 1/fac1
					; quot,rem,res,reslow
	  fadd	st(0), st(4)		;51   quot = 1/fac2 - 1/fac1 + 1/fac1
	  fld	BIGVAL1			;52   For rhi
					; rhi,quot,rem,res,reslow
	  fxch	st(4)			; reslow,quot,rem,res,rhi
	faddp	st(3), st(0)		;53 res += reslow
					; quot,rem,res,rhi
	  fmul	st(0), st(1)		;54   quot = rem * 1/fac
	  fxch	st(1)			; rem,quot,res,rhi
	  fadd	st(3), st(0)		;55   rhi = rem + BIGVAL1
	  fld	BIGVAL1			;56   for qhi
					; qhi,rem,quot,res,rhi
	  fxch	st(2)			; quot,rem,qhi,res,rhi
	  fmul	st(0), st(1)		;57   quot = rem * rem * 1/fac
	  fxch	st(4)			; rhi,rem,qhi,res,quot
	  fsub	BIGVAL1			;58   rhi = rhi - BIGVAL1
	  fld	FACHI2			;59   for qhi*fhi
					; fhi,rhi,rem,qhi,res,quot
	  fxch	st(5)			; quot,rhi,rem,qhi,res,fhi
	  fadd	st(3), st(0)		;60   qhi = quot + BIGVAL1
	  fadd	BIGVAL0			;61   quot = quot + BIGVAL0
	  fxch	st(2)			; rem,rhi,quot,qhi,res,fhi
	  fsub	st(0), st(1)		;62   rlo = rem - rhi
					; rlo,rhi,quot,qhi,rem,fhi
	  fxch	st(3)			; qhi,rhi,quot,rlo,rem,fhi
	  fsub	BIGVAL1			;63   qhi = qhi - BIGVAL1
	  fxch	st(2)			; quot,rhi,qhi,rlo,rem,fhi
	  fsub	BIGVAL0			;64   quot = quot - BIGVAL0
	  fld	st(1)			;65   for res (dup rhi)
					; res,quot,rhi,qhi,rlo,rem,fhi
	  fmul	st, st			;66   res = rhi^2
	  fxch	st(2)			; rhi,quot,res,qhi,rlo,rem,fhi
	  fadd	st, st			;67   double rhi to compute 2*rhi*rlo
					; 2*rhi,quot,res,qhi,rlo,rem,fhi
	  fxch	st(3)			; qhi,quot,res,2*rhi,rlo,rem,fhi
	  fmul	st(6), st(0)		;68   qhi*fhi
					; qhi,quot,res,2*rhi,rlo,rem,qhi*fhi
	  fsub	st(1), st(0)		;69   qlo = quot - qhi
	dec	ecx			;70u
	jnz	vqloop			;70v

; Finish off the computation of fac2's remainder.  Test for rem1 and
; rem2 being one (after doubling and making positive).  I think this
; could be improved by a clock or two.

					; qhi,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fmul	FACLO2			;1    qhi*flo
					; qhi*flo,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fxch	st(6)			; qhi*fhi,qlo,res,2*rhi,rlo,rem,qhi*flo
	  fsubp	st(2), st(0)		;2    res -= qhi*fhi
					; qlo,res,2*rhi,rlo,rem,qhi*flo
	  fxch	st(2)			; 2*rhi,res,qlo,rlo,rem,qhi*flo
	  fmul	st(0), st(3)		;3    2*rhi*rlo
					; 2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	  fxch	st(4)			; rem,res,qlo,rlo,2*rhi*rlo,qhi*flo
	  fld	FACLO2			;4    For computing qlo*flo
	  				; flo,rem,res,qlo,rlo,2*rhi*rlo,qhi*flo
	  fxch	st(4)			; rlo,rem,res,qlo,flo,2*rhi*rlo,qhi*flo
	  fmul	st, st			;5    rlo*rlo
	  				; rlo^2,r,res,qlo,flo,2*rhi*rlo,qhi*flo
	  fxch	st(6)			; qhi*flo,r,res,qlo,flo,2*rhi*rlo,rlo^2
	  fsubp	st(5), st		;6    resmid = 2*rhi*rlo - qhi*flo
					; rem,res,qlo,flo,resmid,rlo^2
	  fxch	st(2)			; qlo,res,rem,flo,resmid,rlo^2
	  fmul	st(3), st(0)		;7    qlo*flo
					; qlo,res,rem,qlo*flo,resmid,rlo^2
	  fxch	st(2)			; rem,res,qlo,qlo*flo,resmid,rlo^2
	fadd	st, st			;8  rem1 = rem1 * 2
	fxch	st(4)			; resmid,res,qlo,qlo*flo,rem,rlo^2
	  faddp	st(1), st(0)		;9    res += resmid
					; res,qlo,qlo*flo,rem,rlo^2
	  fxch	st(1)			; qlo,res,qlo*flo,rem,rlo^2
	  fmul	FACHI2			;10   qlo*fhi
					; qlo*fhi,res,qlo*flo,rem,rlo^2
	  fxch	st(2)			; qlo*flo,res,qlo*fhi,rem,rlo^2
	  fsubp	st(4), st(0)		;11   reslow = rlo*rlo - qlo*flo
					; res,qlo*fhi,rem,reslow
	  fxch	st(2)			; rem,qlo*fhi,res,reslow
	fsub	ONE			;12 rem1 = rem1 - 1
	fxch	st(1)			; qlo*fhi,rem,res,reslow
	  fsubp	st(2), st(0)		;13   res -= qlo*fhi
					; rem,res,reslow
	  fld	FACHI2			;14   fac2 = fachi
					; fac2,rem,res,reslow
	  fadd	FACLO2			;15   fac2 = fachi + faclo
	  fxch	st(3)			; reslow,rem,res,fac2
	  faddp	st(2), st(0)		;16   res += reslow
					; rem1,rem2,fac2
	fabs				;17 rem1 = |rem1|
	fsub	FACHI			;18 rem1 -= FACHI
	fxch	st(2)			; fac2,rem2,rem1
	  fmul	HALF			;19   fac2/2 = fac2/2
					; fac2/2,rem2,rem1
	  fxch	st(1)			; rem2,fac2/2,rem1
	  fsub	HALF			;20   rem2 = rem2 - 1/2
	  fxch	st(2)			; rem1,fac2/2,rem2
	fsub	FACLO			;21 rem1 -= FACLO
	fxch	st(3)			; 1/fac1,fac2/2,rem2,rem1
	fcomp	st			;22 pop 1/fac value
					; fac2/2,rem2,rem1
	fxch	st(1)			; rem2,fac2/2,rem1
	  fabs				;23   rem2 = |rem2|
	  fsubrp st(1),st(0)		;24   rem2 = rem2 - fac2/2
					; rem2,rem1
	  fxch	st(1)			; rem1,rem2
	fstp	temp			;25-26 Save rem1 for comparing
	mov	ecx, temp		;27
	cmp	ecx, 00000000h		;28U Compare remainder to 0.0
	je	short vin1		;28V
	  fstp	temp			;29-30 Save rem2 for comparing
	mov	ecx, temp		;31
	cmp	ecx, 00000000h		;32U Compare remainder to 0.0
	je	short vin2		;32V
	retn				;33UV - Test next factor from sieve

vin1:	fld	FACHI			; Load MSW
	fadd	FACLO			; Load LSW
	jmp	short vincom		; Join common code
vin2:	fld	FACHI2			; Load MSW
	fadd	FACLO2			; Load LSW
vincom:	fistp	QWORD PTR savefac2	; Save as an integer
	mov	eax, savefac2		; Load LSW
	mov	edx, savefac1		; Load MSW
	mov	FACLSW, eax		; Store LSW
	mov	FACMSW, edx		; Store MSW
	mov	eax, 1			; Factor found!!! Return TRUE
	add	esp, 4			; pop return address
	jmp	done

; One sieve is done, save registers and see if another
; sieve will be tested before we check for an ESC.

	push_amt = 0
vlpdn:	cmp	edi, -2			; Is a factor queued up?
	je	short vlpdn2		; No
	fld	FACHI			; Yes, go test it
	fstp	FACHI2
	fld	FACLO
	fstp	FACLO2
	fld	st(2)
	fld	st(2)
	fld	st(2)
	call	vest2
vlpdn2:	mov	savefac1, ebx		; Save for the restart or more sieving
	mov	savefac2, ebp
	dec	reps			; Check repetition counter
	jnz	slp0
	mov	eax, 2			; Return for ESC check
	mov	FACMSW, ebx
	jmp	done


;***********************************************************************
; For all machines - 64 bit factors
; Optimized for the Pentium
;***********************************************************************

;
; Check all the bits in the sieve looking for a factor to test
;

tlp64:	mov	esi, sieve
	mov	edi, -2			; Count of queued factors to be tested
	sub	edx, edx
	mov	ebp, savefac2		; Load factor corresponding to first
	mov	ebx, savefac1		; first sieve bit.  But since FILD only
	mov	savefac2, ebx		; loads signed values, remember
	mov	savefac1, edx		; savefac1 as a float and ebx/ebp
	fild	QWORD PTR savefac2	; as an addin to that float.
	mov	base_int, ebx
	sub	ebx, ebx
	fmul	TWO_TO_32
	fstp	BASE
	mov	eax, [esi]		; Load first sieve word
	lea	esi, [esi+4]
wx0:	test	al, 01h
	jnz	wst0
wx1:	test	al, 02h
	jnz	wst1
wx2:	test	al, 04h
	jnz	wst2
wx3:	test	al, 08h
	jnz	wst3
wx4:	test	al, 10h
	jnz	wst4
wx5:	test	al, 20h
	jnz	wst5
wx6:	test	al, 40h
	jnz	wst6
wx7:	test	al, 80h
	jnz	wst7
wx8:	test	eax, 100h
	jnz	wst8
wx9:	test	eax, 200h
	jnz	wst9
wx10:	test	eax, 400h
	jnz	wst10
wx11:	test	eax, 800h
	jnz	wst11
wx12:	test	eax, 1000h
	jnz	wst12
wx13:	test	eax, 2000h
	jnz	wst13
wx14:	test	eax, 4000h
	jnz	wst14
wx15:	test	eax, 8000h
	jnz	wst15
wx16:	test	eax, 10000h
	jnz	wst16
wx17:	test	eax, 20000h
	jnz	wst17
wx18:	test	eax, 40000h
	jnz	wst18
wx19:	test	eax, 80000h
	jnz	wst19
wx20:	test	eax, 100000h
	jnz	wst20
wx21:	test	eax, 200000h
	jnz	wst21
wx22:	test	eax, 400000h
	jnz	wst22
wx23:	test	eax, 800000h
	jnz	wst23
wx24:	test	eax, 1000000h
	jnz	wst24
wx25:	test	eax, 2000000h
	jnz	wst25
wx26:	test	eax, 4000000h
	jnz	wst26
wx27:	test	eax, 8000000h
	jnz	wst27
wx28:	test	eax, 10000000h
	jnz	short wst28
wx29:	test	eax, 20000000h
	jnz	short wst29
wx30:	test	eax, 40000000h
	jnz	short wst30
wx31:	test	eax, 80000000h
	jnz	short wst31
wlp5:	add	ebp, facdist32		; U - Add facdist * 32 to the factor
	mov	eax, [esi]		; V - Next sieve word
	adc	ebx, facdist32+4	; U - Add carry
	test	esi, sievesize		; V - End of sieve?
	lea	esi, [esi+4]		; U - Bump pointer
	jz	wx0			; V - Loop to test next sieve dword
	add	ebx, base_int
	jnc	wlpdn			; Jump if not past the 64-bit limit?
	mov	ebx, -1			; Indicate end of testing
	jmp	wlpdn1			; Rejoin code that returns

; Entry points for testing each bit#

wst0:	mov	dl, 0			; Test factor corresponding to bit 0
	jmp	westit0
wst28:	mov	dl, 28			; Test factor corresponding to bit 28
	jmp	short westit
wst29:	mov	dl, 29			; Test factor corresponding to bit 29
	jmp	short westit
wst30:	mov	dl, 30			; Test factor corresponding to bit 30
	jmp	short westit
wst31:	mov	dl, 31			; Test factor corresponding to bit 31
	jmp	short westit
wst27:	mov	dl, 27			; Test factor corresponding to bit 27
	jmp	short westit
wst26:	mov	dl, 26			; Test factor corresponding to bit 26
	jmp	short westit
wst25:	mov	dl, 25			; Test factor corresponding to bit 25
	jmp	short westit
wst24:	mov	dl, 24			; Test factor corresponding to bit 24
	jmp	short westit
wst23:	mov	dl, 23			; Test factor corresponding to bit 23
	jmp	short westit
wst22:	mov	dl, 22			; Test factor corresponding to bit 22
	jmp	short westit
wst21:	mov	dl, 21			; Test factor corresponding to bit 21
	jmp	short westit
wst20:	mov	dl, 20			; Test factor corresponding to bit 20
	jmp	short westit
wst19:	mov	dl, 19			; Test factor corresponding to bit 19
	jmp	short westit
wst18:	mov	dl, 18			; Test factor corresponding to bit 18
	jmp	short westit
wst17:	mov	dl, 17			; Test factor corresponding to bit 17
	jmp	short westit
wst16:	mov	dl, 16			; Test factor corresponding to bit 16
	jmp	short westit
wst15:	mov	dl, 15			; Test factor corresponding to bit 15
	jmp	short westit
wst14:	mov	dl, 14			; Test factor corresponding to bit 14
	jmp	short westit
wst13:	mov	dl, 13			; Test factor corresponding to bit 13
	jmp	short westit
wst12:	mov	dl, 12			; Test factor corresponding to bit 12
	jmp	short westit
wst11:	mov	dl, 11			; Test factor corresponding to bit 11
	jmp	short westit
wst10:	mov	dl, 10			; Test factor corresponding to bit 10
	jmp	short westit
wst9:	mov	dl, 9			; Test factor corresponding to bit 9
	jmp	short westit
wst8:	mov	dl, 8			; Test factor corresponding to bit 8
	jmp	short westit
wst7:	mov	dl, 7			; Test factor corresponding to bit 7
	jmp	short westit
wst6:	mov	dl, 6			; Test factor corresponding to bit 6
	jmp	short westit
wst5:	mov	dl, 5			; Test factor corresponding to bit 5
	jmp	short westit
wst4:	mov	dl, 4			; Test factor corresponding to bit 4
	jmp	short westit
wst3:	mov	dl, 3			; Test factor corresponding to bit 3
	jmp	short westit
wst2:	mov	dl, 2			; Test factor corresponding to bit 2
	jmp	short westit
wst1:	mov	dl, 1			; Test factor corresponding to bit 1

;
; This is the Pentium version of testit for 64-bit factors.  It gathers
; 2 potential factors to be tested together to minimize processor stalls.
;
; OPTIMIZED FOR PENTIUMS
;
; eax = sieve word - must be preserved or reloaded
; ebx = fachi - must be preserved as fachi for sieve bit 0
; edx = sieve bit being tested - must return with top 24 bits zero
; edi = count of potential factors queued up - must return -2
; esi = sieve address - must be preserved
; ebp = faclo - must be preserved as faclo for sieve bit 0
;
; Total for two exponents - assuming 18 interior loops:
; 2*46 + 100 + 82*17 + 4 (mispredicted jump) + 33 = 811.5 per exponent

;
; Precompute initval / factor
;

westit:	add	ebp, facdists[edx*8]	; Determine factor to test
	adc	ebx, facdists+4[edx*8]	; Add carry
westit0:mov	savefac2, ebp		;1U Save addin so FPU can load it
	mov	savefac1, ebx		;1V
	fild	QWORD PTR savefac2	;2  Addin = amount to add to base
	fild	savefac2		;3  faclo, addin
	fld	initval			;4  initval, faclo, addin
	fxch	st(2)			;   addin, faclo, initval
	fadd	BASE			;5  fac, faclo, initval
	fxch				;   faclo, fac, initval
	fstp	FACLO[edi*8+16]		;6-7 fac, initval
	fdivr	st(0), st(1)		;8-46 quot, initval

; The FDIV takes 39 clocks and only the last two can overlap
; with other float instructions.  This gives us 37 clocks
; to do something useful with the integer units.

	; Compute FACHI for conversion to floating point format.
	; Round FACHI up if FACLO is negative.
	add	ebp, ebp		; U - Set carry if FACLO is negative
	mov	eax, base_int		; V - For computing fac MSW
	adc	ebx, 0			; U - Increment FACHI if FACLO neg.
	add	eax, ebx		;*U - Compute fac MSW
	jnc	short whi		; V - An overflow occured 
	mov	eax, DWORD PTR TWO_TO_64+4; U - Make FACHI equal 2^64
	mov	ebx, returns6[edx*4]	; V - Load return address
	sub	ebp, ebp		; V - FACHI LSW
	push	ebx			; V - Push return address
	jmp	short whidn		;UV - Rejoin code

	; Compute the shift count to create floating point values.
	; We'd like to use the BSF instruction but it is very slow.
	; Simulate BSF by taking advantage of the fact that factors
	; increase in size very slowly.
whi:	mov	ecx, fachi_shf_count	; U - Load last shift count
	mov	ebx, fachi_shf_mask	; V - Load last shift mask
whilp:	test	eax, ebx		; U - Is value wider than last time?
	jz	short whiok		; V - No, jump
	add	ebx, ebx		; U - Compute new shift mask
	dec	ecx			; V - Decrement the shift count
	mov	fachi_shf_count, ecx	; U - Save the new shift count
	mov	fachi_shf_mask, ebx	; V - Save the new shift mask
	jmp	short whilp		; V - Loop in case fachi is much wider

	; We now have enough information to convert FACHI
	; to floating point format.
whiok:	shl	eax, cl			;4UV - Normalize (top bit always on)
	mov	ebp, eax		; U - EBP will hold the float LSW
	mov	ebx, returns6[edx*4]	; V - Load return address
	shr	eax, 11			; U - EAX will hold the float MSW
	pusher	ebx			; V - Push return address
	shl	ecx, 20			; U - Put shift count in exponent byte
	add	eax, 43D00000h 		; V - Make the exponent byte right
	shl	ebp, 21			; U - Low 32 bits of the float
	sub	eax, ecx		; V - Sub shift count from exponent

	; Recompute original fachi and faclo.  That is, the fachi/faclo values
	; for the first sieve bit in EAX 
	push_amt = 4
whidn:	mov	DWORD PTR FACHI[edi*8+16], ebp ; U - Store FACHI LSW
	mov	ebp, savefac2		; V - Reload faclo
	mov	ecx, facdists[edx*8]	; U - faclo adjustment
	mov	ebx, savefac1		; V - Reload addin fachi
	sub	ebp, ecx		; U - Recompute original faclo
	mov	ecx, facdists+4[edx*8]	; V - fachi adjustment
	sbb	ebx, ecx		; U - Recompute original fachi
	mov	DWORD PTR FACHI+4[edi*8+16], eax ; V - Store FACHI MSW

	mov	eax, [esi-4]		; U - Reload eax
	inc	edi			;*U - Bump count of queued factors
	jz	short west2		; V - Jump if enough are queued
	retn				; UV - return to sieve testing

;
; Now test the 2 factors
;

	; More miscellaneous initialization

	push_amt = 4
west2:	mov	ecx, wqloop_counter	; U - Load loop counter
	mov	edi, -2			; V - Restore queued factor counter

; Work on initval
; At start of loop, registers contain:

					; quot2,initval,quot1,initval
	fld	BIGVAL1			;   for qhi
					; qhi,quot2,initval,quot1,initval
	fld	BIGVAL0			;   for q and qlo
					; q,qhi,quot2,initval,quot1,initval
	fxch	st(1)			; qhi,q,quot2,initval,quot1,initval
	fadd	st(0),st(4)		;1  qhi = quot + BIGVAL1
	fxch	st(1)			; q,qhi,quot2,initval,quot1,initval
	fadd	st(0),st(4)		;2  q = quot + BIGVAL0
	fld	FACHI			;3  for qhi*fhi
					; fhi,q,qhi,quot2,initval,quot1,initval
	fxch	st(2)			; qhi,q,fhi,quot2,initval,quot1,initval
	fsub	BIGVAL1			;4  qhi = qhi - BIGVAL1
	fxch	st(1)			; q,qhi,fhi,quot2,initval,quot1,initval
	fsub	BIGVAL0			;5  q = q - BIGVAL0
	fld	FACHI			;6  fhi
					; fhi,q,qhi,fhi,quot2,initval,quot1,iv
	fxch	st(2)			; qhi,q,fhi,fhi,quot2,initval,quot1,iv
	fmul	st(3),st(0)		;7  qhi*fhi
					; qhi,q,fhi,qhi*fhi,quot2,iv,quot1,iv
	fsub	st(1),st(0)		;8  qlo = q - qhi
					; qhi,qlo,fhi,qhi*fhi,quot2,iv,quot1,iv
	fmul	FACLO			;9  qhi*flo
					; qhi*flo,qlo,fhi,qhi*fhi,quot2,
					;		initval,quot1,initval
	fxch	st(3)			; qhi*fhi,qlo,fhi,qhi*flo,quot2,
					;		initval,quot1,initval
	fsubp	st(7),st(0)		;10 res = initval - qhi*fhi
					; qlo,fhi,qhi*flo,quot2,iv,quot1,res
	fmul	st(1),st(0)		;11 qlo*fhi
					; qlo,qlo*fhi,qhi*flo,quot2,iv,q1,res
	  fld	BIGVAL1			;12   for qhi
					; qhi,qlo,qlo*fhi,qhi*flo,q2,iv,q1,res
	  fxch	st(3)			; qhi*flo,qlo,qlo*fhi,qhi,q2,iv,q1,res
	fsubp	st(7),st(0)		;13 res -= qhi*flo
					; qlo,qlo*fhi,qhi,quot2,iv,quot1,res
	fmul	FACLO			;14 qlo*flo
					; qlo*flo,qlo*fhi,qhi,quot2,iv,q1,res
	  fld	BIGVAL0			;15   for q and qlo
					; q,qlo*flo,qlo*fhi,qhi,quot2,iv,q1,res
	  fxch	st(2)			; qlo*fhi,qlo*flo,q,qhi,quot2,iv,q1,res
	fsubp	st(7),st(0)		;16 res -= qlo*fhi
					; qlo*flo,q,qhi,quot2,initval,quot1,res
	fxch	st(3)			; quot2,q,qhi,qlo*flo,initval,quot1,res
	  fadd	st(2),st(0)		;17   qhi = quot + BIGVAL1
	  fadd	st(1),st(0)		;18   q = quot + BIGVAL0
	  fxch	st(5)			; quot1,q,qhi,qlo*flo,initval,quot2,res
	fmul	initval_inv		;19 1/fac = quot * initval_inv
					; 1/fac,q,qhi,qlo*flo,initval,quot2,res
	fxch	st(6)			; res,q,qhi,qlo*flo,initval,quot2,1/fac
					; (now on 1/fac1 is assumed on stack)
	  fld	FACHI2			;20   for qhi*fhi
					; fhi,res,q,qhi,qlo*flo,initval,quot2
	  fxch	st(3)			; qhi,res,q,fhi,qlo*flo,initval,quot2
	  fsub	BIGVAL1			;21   qhi = qhi - BIGVAL1
	  fxch	st(2)			; q,res,qhi,fhi,qlo*flo,initval,quot2
	  fsub	BIGVAL0			;22   q = q - BIGVAL0
	  fxch	st(4)			; qlo*flo,res,qhi,fhi,q,initval,quot2
	fsubp	st(1),st(0)		;23 rem = res - qlo*flo
					; rem,qhi,fhi,q,initval,quot2
	fxch	st(1)			; qhi,rem,fhi,q,initval,quot2
	  fmul	st(2),st(0)		;24   qhi*fhi
					; qhi,rem,qhi*fhi,q,initval,quot2
	  fsub	st(3),st(0)		;25   qlo = q - qhi
					; qhi,rem,qhi*fhi,qlo,initval,quot2
	  fmul	FACLO2			;26   qhi*flo
					; qhi*flo,rem,qhi*fhi,qlo,initval,quot2
	  fxch	st(2)			; qhi*fhi,rem,qhi*flo,qlo,initval,quot2
	  fsubp	st(4),st(0)		;27   res = initval - qhi*fhi
					; rem,qhi*flo,qlo,res,quot2
	  fld	FACHI2			;28   For qlo*fhi
					; fhi,rem,qhi*flo,qlo,res,quot2
	  fmul	st(0),st(3)		;29   qlo*fhi
					; qlo*fhi,rem,qhi*flo,qlo,res,quot2
	  fxch	st(2)			; qhi*flo,rem,qlo*fhi,qlo,res,quot2
	  fsubp	st(4),st(0)		;30   res -= qhi*flo
					; rem,qlo*fhi,qlo,res,quot2
	fld	FACLO			;31 fac = faclo
					; fac,rem,qlo*fhi,qlo,res,quot2
	fxch	st(5)			; quot2,rem,qlo*fhi,qlo,res,fac
	  fmul	initval_inv		;32   1/fac = quot * initval_inv
					; 1/fac2,rem,qlo*fhi,qlo,res,fac
	  fxch	st(5)			; fac,rem,qlo*fhi,qlo,res,1/fac2
	fadd	FACHI			;33 fac = fachi + faclo
	fxch	st(3)			; qlo,rem,qlo*fhi,fac,res,1/fac2
	  fmul	FACLO2			;34   qlo*flo
					; qlo*flo,rem,qlo*fhi,fac,res,1/fac2
	  fxch	st(2)			; qlo*fhi,rem,qlo*flo,fac,res,1/fac2
	  fsubp	st(4),st(0)		;35   res -= qlo*fhi
					; rem,qlo*flo,fac,res,1/fac2

; First iteration of wqloop - to "get the pipeline going"

	  fxch	st(2)			; fac,qlo*flo,rem,res,1/fac2
	fmul	HALF			;36 fac/2 = fac / 2
					; fac/2,qlo*flo,rem,res,1/fac2
	fxch	st(4)			; 1/fac2,qlo*flo,rem,res,fac/2
	fsub	st(0), st(5)		;37 facdiff = 1/fac2 - 1/fac1
					; facdiff,qlo*flo,rem,res,fac/2
	fxch	st(2)			; rem,qlo*flo,facdiff,res,fac/2
	fabs				;38 rem = 64-bit remainder
	fsub	st(0),st(4)		;39 rem = rem - fac/2
	fld	st(5)			;40 quot = 1/fac
					; quot,rem,qlo*flo,facdiff,res,fac/2
	fxch	st(2)			; qlo*flo,rem,quot,facdiff,res,fac/2
	  fsubp	st(4),st(0)		;41   rem = res - qlo*flo
					; rem1,quot,facdiff,rem2,fac/2
	fabs				;42 rem = |rem|
	fsubp	st(4),st(0)		;43 rem = fac/2-rem (63-bit remainder!)
					; quot,facdiff,rem2,rem1
	fxch	st(1)			; facdiff,quot,rem2,rem1
	  fstp	FACDIFF			;44-45 store FACDIFF
					; quot,rem2,rem1
	fld	BIGVAL1			;46 For rhi
					; rhi,quot,rem2,rem1
	fxch	st(3)			; rem1,quot,rem2,rhi
	fmul	st(1),st(0)		;47 quot = rem * 1/fac
	fadd	st(3),st(0)		;48 rhi = rem + BIGVAL1
	fld	BIGVAL1			;49 for qhi
					; qhi,rem1,quot,rem2,rhi
	fxch	st(2)			; quot,rem1,qhi,rem2,rhi
	fmul	st(0),st(1)		;50 quot = rem * rem * 1/fac
	fxch	st(4)			; rhi,rem1,qhi,rem2,quot
	fsub	BIGVAL1			;51 rhi = rhi - BIGVAL1
	fld	FACHI			;52 for qhi*fhi
					; fhi,rhi,rem1,qhi,rem2,quot
	fxch	st(5)			; quot,rhi,rem1,qhi,rem2,fhi
	fadd	st(3),st(0)		;53 qhi = quot + BIGVAL1
	fadd	BIGVAL0			;54 quot = quot + BIGVAL0
	fxch	st(2)			; rem1,rhi,quot,qhi,rem2,fhi
	fsub	st(0),st(1)		;55 rlo = rem - rhi
					; rlo,rhi,quot,qhi,rem,fhi
	fxch	st(3)			; qhi,rhi,quot,rlo,rem,fhi
	fsub	BIGVAL1			;56 qhi = qhi - BIGVAL1
	fxch	st(2)			; quot,rhi,qhi,rlo,rem,fhi
	fsub	BIGVAL0			;57 quot = quot - BIGVAL0
	fld	st(1)			;58 for res (dup rhi)
					; res,quot,rhi,qhi,rlo,rem,fhi
	fmul	st,st			;59 res = rhi^2
	fxch	st(2)			; rhi,quot,res,qhi,rlo,rem,fhi
	fadd	st,st			;60 double rhi to compute 2*rhi*rlo
					; 2*rhi,quot,res,qhi,rlo,rem,fhi
	fxch	st(3)			; qhi,quot,res,2*rhi,rlo,rem,fhi
	fmul	st(6),st(0)		;61 qhi*fhi
					; qhi,quot,res,2*rhi,rlo,rem,qhi*fhi
	fsub	st(1),st(0)		;62 qlo = quot - qhi
					; qhi,qlo,res,2*rhi,rlo,rem,qhi*fhi
	fmul	FACLO			;63 qhi*flo
					; qhi*flo,qlo,res,2*rhi,rlo,rem,qhi*fhi
	fxch	st(6)			; qhi*fhi,qlo,res,2*rhi,rlo,rem,qhi*flo
	fsubp	st(2),st(0)		;64 res -= qhi*fhi
					; qlo,res,2*rhi,rlo,rem,qhi*flo
	fxch	st(2)			; 2*rhi,res,qlo,rlo,rem,qhi*flo
	fmul	st(0),st(3)		;65 2*rhi*rlo
					; 2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	  fld	FACLO2			;66   fac = faclo
					; fac,2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	  fadd	FACHI2			;67   fac = fachi + faclo
	  fxch	st(6)			; qhi*flo,2*rhi*rlo,res,qlo,rlo,rem,fac
	fsubp	st(1),st(0)		;68 resmid = 2*rhi*rlo - qhi*flo
					; resmid,res,qlo,rlo,rem,fac
	fxch	st(3)			; rlo,res,qlo,resmid,rem,fac
	fmul	st,st			;69 rlo*rlo
					; rlo*rlo,res,qlo,resmid,rem,fac
	fld	FACHI			;70 For qlo*fhi
					; fhi,rlo*rlo,res,qlo,resmid,rem,fac
	fmul	st(0),st(3)		;71 qlo*fhi
					; qlo*fhi,rlo^2,res,qlo,resmid,rem,fac
	fxch	st(6)			; fac,rlo^2,res,qlo,resmid,rem,qlo*fhi
	  fmul	HALF			;72   fac/2 = fac / 2
					; fac/2,rlo^2,res,qlo,resm,rem,qlo*fhi
	  fxch	st(4)			; resm,rlo^2,res,qlo,fac/2,rem,qlo*fhi
	faddp	st(2),st(0)		;73 res += resmid
					; rlo*rlo,res,qlo,fac/2,rem,qlo*fhi
	fxch	st(2)			; qlo,res,rlo*rlo,fac/2,rem,qlo*fhi
	fmul	FACLO			;74 qlo*flo
					; qlo*flo,res,rlo*rlo,fac/2,rem,qlo*fhi
	fxch	st(4)			; rem,res,rlo*rlo,fac/2,qlo*flo,qlo*fhi
	  fabs				;75   rem = 64-bit remainder
	  fsub	st(0),st(3)		;76   rem = rem - fac/2
	  fld	FACDIFF			;77   quot = 1/fac2 - 1/fac1
					; q,rem,res,rlo^2,fac/2,qlo*flo,qlo*fhi
	  fadd	st(0),st(7)		;78   quot = 1/fac2 - 1/fac1 + 1/fac1
	  fxch	st(6)			; qlo*fhi,rem,res,rlo^2,fac/2,qlo*flo,q
	fsubp	st(2),st(0)		;79 res -= qlo*fhi
					; rem,res,rlo*rlo,fac/2,qlo*flo,quot
	fxch	st(4)			; qlo*flo,res,rlo*rlo,fac/2,rem,quot
	fsubp	st(2),st(0)		;80 reslow = rlo*rlo - qlo*flo
					; res,reslow,fac/2,rem,quot
	fxch	st(3)			; rem,reslow,fac/2,res,quot
	  fabs				;81   rem = |rem|
	  fsubp	st(2),st(0)		;82   rem = 63-bit remainder!
					; reslow,rem,res,quot
	faddp	st(2),st(0)		;83 res += reslow
					; rem,res,quot
	fxch	st(2)			; quot,res,rem
	  fld	BIGVAL1			;84   For rhi
					; rhi,quot,res,rem
	  fxch	st(3)			; rem,quot,res,rhi
	  fmul	st(1), st(0)		;85   quot = rem * 1/fac
	  fadd	st(3), st(0)		;86   rhi = rem + BIGVAL1
	  fld	BIGVAL1			;87   for qhi
					; qhi,rem,quot,res,rhi
	  fxch	st(2)			; quot,rem,qhi,res,rhi
	  fmul	st(0), st(1)		;88   quot = rem * rem * 1/fac
	  fxch	st(4)			; rhi,rem,qhi,res,quot
	  fsub	BIGVAL1			;89   rhi = rhi - BIGVAL1
	  fld	FACHI2			;90   for qhi*fhi
					; fhi,rhi,rem,qhi,res,quot
	  fxch	st(5)			; quot,rhi,rem,qhi,res,fhi
	  fadd	st(3), st(0)		;91   qhi = quot + BIGVAL1
	  fadd	BIGVAL0			;92   quot = quot + BIGVAL0
	  fxch	st(2)			; rem,rhi,quot,qhi,res,fhi
	  fsub	st(0), st(1)		;93   rlo = rem - rhi
					; rlo,rhi,quot,qhi,rem,fhi
	  fxch	st(3)			; qhi,rhi,quot,rlo,rem,fhi
	  fsub	BIGVAL1			;94   qhi = qhi - BIGVAL1
	  fxch	st(2)			; quot,rhi,qhi,rlo,rem,fhi
	  fsub	BIGVAL0			;95   quot = quot - BIGVAL0
	  fld	st(1)			;96   for res (dup rhi)
					; res,quot,rhi,qhi,rlo,rem,fhi
	  fmul	st, st			;97   res = rhi^2
	  fxch	st(2)			; rhi,quot,res,qhi,rlo,rem,fhi
	  fadd	st, st			;98   double rhi to compute 2*rhi*rlo
					; 2*rhi,quot,res,qhi,rlo,rem,fhi
	  fxch	st(3)			; qhi,quot,res,2*rhi,rlo,rem,fhi
	  fmul	st(6), st(0)		;99   qhi*fhi
					; qhi,quot,res,2*rhi,rlo,rem,qhi*fhi
	  fsub	st(1), st(0)		;100  qlo = quot - qhi

; Square remainder and get new remainder.
; The remainder is between -fac and fac.  Fac is up to 64 bits.
; Basic algorithm:
; 1) Possibly double the remainder.  Computed as:
;	rem = possible_double (abs (rem)) - fac
; 2) Get a 63-bit remainder, that is min (rem, fac - rem).  Computed as:
;	rem = fac/2 - abs (abs (rem) - fac/2)
; 3) Compute the quotient:  q = rem * rem * 1/fac.  The error will be
;    at most 3/8.
; 4) Compute the new remainder.
;	 rem = remhi * remhi - qhi * fachi +
;	       2 * remlo * remhi - qlo * fachi - qhi * faclo +
;	       remlo * remlo - qlo * faclo
;
; q is 63 + 63 - 64 = 62 bits
; remhi is 31 bits, remlo is signed 31 bits
; qhi is 30 bits, qlo is signed 31 bits
; fachi is 32 bits, faclo is signed 31 bits
;
; At start of loop, registers contain:
;	qhi2, qlo2, res2, 2*rhi2, rlo2, rem1, qhi2*fhi2, 1/fac1

wqloop:					; qhi,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fmul	FACLO2			;1    qhi*flo
					; qhi*flo,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fxch	st(6)			; qhi*fhi,qlo,res,2*rhi,rlo,rem,qhi*flo
	  fsubp	st(2), st(0)		;2    res -= qhi*fhi
					; qlo,res,2*rhi,rlo,rem,qhi*flo
	  fxch	st(2)			; 2*rhi,res,qlo,rlo,rem,qhi*flo
	  fmul	st(0), st(3)		;3    2*rhi*rlo
					; 2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	  fxch	st(4)			; rem,res,qlo,rlo,2*rhi*rlo,qhi*flo
	fabs				;4  rem = |rem|
					; rem,res,qlo,rlo,2*rhi*rlo,qhi*flo
	fmul	REMMULTS[ecx*4+4]	;5  rem = rem * 1-or-2
	fxch	st(5)			; qhi*flo,res,qlo,rlo,2*rhi*rlo,rem
	  fsubp	st(4), st		;6    resmid = 2*rhi*rlo - qhi*flo
					; res,qlo,rlo,resmid,rem
	fld	FACLO			;7  fac = faclo
					; fac,res,qlo,rlo,resmid,rem
	fadd	FACHI			;8  fac = fachi + faclo
	fxch	st(3)			; rlo,res,qlo,fac,resmid,rem
	  fmul	st, st			;9    rlo*rlo
					; rlo*rlo,res,qlo,fac,resmid,rem
	  fld	FACHI2			;10   For computing qlo*fhi
					; fhi,rlo*rlo,res,qlo,fac,resmid,rem
	  fmul	st(0), st(3)		;11   qlo*fhi
					; qlo*fhi,rlo^2,res,qlo,fac,resmid,rem
	  fxch	st(4)			; fac,rlo^2,res,qlo,qlo*fhi,resmid,rem
	fsub	st(6), st(0)		;12 rem = rem - fac
	fmul	HALF			;13 fac/2 = fac / 2
					; fac/2,rlo^2,res,qlo,qlo*fhi,resm,rem
	fxch	st(5)			; resm,rlo^2,res,qlo,qlo*fhi,fac/2,rem
	  faddp	st(2), st(0)		;14   res += resmid
					; rlo*rlo,res,qlo,qlo*fhi,fac/2,rem
	  fxch	st(2)			; qlo,res,rlo*rlo,qlo*fhi,fac/2,rem
	  fmul	FACLO2			;15   qlo*flo
					; qlo*flo,res,rlo*rlo,qlo*fhi,fac/2,rem
	  fxch	st(5)			; rem,res,rlo*rlo,qlo*fhi,fac/2,qlo*flo
	fabs				;16 rem = 64-bit remainder
	fxch	st(4)			; fac/2,res,rlo*rlo,qlo*fhi,rem,qlo*flo
	fsub	st(4), st(0)		;17 rem = rem - fac/2
	fld	st(6)			;18 quot = 1/fac
					; quot,fac/2,res,rlo*rlo,qlo*fhi,
					;			rem,qlo*flo
	fxch	st(4)			; qlo*fhi,fac/2,res,rlo*rlo,quot,
					;			rem,qlo*flo
	  fsubp	st(2), st(0)		;19   res -= qlo*fhi
					; fac/2,res,rlo*rlo,quot,rem,qlo*flo
	  fxch	st(5)			; qlo*flo,res,rlo*rlo,quot,rem,fac/2
	  fsubp	st(2), st(0)		;20   reslow = rlo*rlo - qlo*flo
					; res,reslow,quot,rem,fac/2
	  fxch	st(3)			; rem,reslow,quot,res,fac/2
	fabs				;21 rem = |rem|
	fxch	st(4)			; fac/2,reslow,quot,res,rem
	fsubrp	st(4), st(0)		;22 rem = fac/2-rem (63-bit remainder!)
					; reslow,quot,res,rem
	  faddp	st(2), st(0)		;23   res += reslow
					; quot,res,rem
	fld	BIGVAL1			;24 For rhi
					; rhi,quot,res,rem
	fxch	st(3)			; rem,quot,res,rhi
	fmul	st(1), st(0)		;25 quot = rem * 1/fac
	fadd	st(3), st(0)		;26 rhi = rem + BIGVAL1
	fld	BIGVAL1			;27 for qhi
					; qhi,rem,quot,res,rhi
	fxch	st(2)			; quot,rem,qhi,res,rhi
	fmul	st(0), st(1)		;28 quot = rem * rem * 1/fac
	fxch	st(4)			; rhi,rem,qhi,res,quot
	fsub	BIGVAL1			;29 rhi = rhi - BIGVAL1
	fld	FACHI			;30 for qhi*fhi
					; fhi,rhi,rem,qhi,res,quot
	fxch	st(5)			; quot,rhi,rem,qhi,res,fhi
	fadd	st(3), st(0)		;31 qhi = quot + BIGVAL1
	fadd	BIGVAL0			;32 quot = quot + BIGVAL0
	fxch	st(2)			; rem,rhi,quot,qhi,res,fhi
	fsub	st(0), st(1)		;33 rlo = rem - rhi
					; rlo,rhi,quot,qhi,rem,fhi
	fxch	st(3)			; qhi,rhi,quot,rlo,rem,fhi
	fsub	BIGVAL1			;34 qhi = qhi - BIGVAL1
	fxch	st(2)			; quot,rhi,qhi,rlo,rem,fhi
	fsub	BIGVAL0			;35 quot = quot - BIGVAL0
	fld	st(1)			;36 for res (dup rhi)
					; res,quot,rhi,qhi,rlo,rem,fhi
	fmul	st, st			;37 res = rhi^2
	fxch	st(2)			; rhi,quot,res,qhi,rlo,rem,fhi
	fadd	st, st			;38 double rhi to compute 2*rhi*rlo
	fxch	st(3)			; qhi,quot,res,2*rhi,rlo,rem,fhi
	fmul	st(6), st(0)		;39 qhi*fhi
					; qhi,quot,res,2*rhi,rlo,rem,qhi*fhi
	fsub	st(1), st(0)		;40 qlo = quot - qhi
					; qhi,qlo,res,2*rhi,rlo,rem,qhi*fhi
	fmul	FACLO			;41 qhi*flo
					; qhi*flo,qlo,res,2*rhi,rlo,rem,qhi*fhi
	fxch	st(6)			; qhi*fhi,qlo,res,2*rhi,rlo,rem,qhi*flo
	fsubp	st(2), st(0)		;42 res -= qhi*fhi
					; qlo,res,2*rhi,rlo,rem,qhi*flo
	fxch	st(2)			; 2*rhi,res,qlo,rlo,rem,qhi*flo
	fmul	st(0), st(3)		;43 2*rhi*rlo
					; 2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	fxch	st(4)			; rem,res,qlo,rlo,2*rhi*rlo,qhi*flo
	  fabs				;44   rem = |rem|
	  fmul	REMMULTS[ecx*4+4]	;45   rem = rem * 1-or-2
	  fxch	st(5)			; qhi*flo,res,qlo,rlo,2*rhi*rlo,rem
	fsubp	st(4), st(0)		;46 resmid = 2*rhi*rlo - qhi*flo
					; res,qlo,rlo,resmid,rem
	  fld	FACLO2			;47   fac = faclo
					; fac,res,qlo,rlo,resmid,rem
	  fadd	FACHI2			;48   fac = fachi + faclo
	  fxch	st(3)			; rlo,res,qlo,fac,resmid,rem
	fmul	st, st			;49 rlo*rlo
					; rlo*rlo,res,qlo,fac,resmid,rem
	fld	FACHI			;50 fhi
					; fhi,rlo*rlo,res,qlo,fac,resmid,rem
	fmul	st(0), st(3)		;51 qlo*fhi
					; qlo*fhi,rlo^2,res,qlo,fac,resmid,rem
	fxch	st(4)			; fac,rlo^2,res,qlo,qlo*fhi,resmid,rem
	  fsub	st(6), st(0)		;52   rem = rem - fac
	  fmul	HALF			;53   fac/2 = fac / 2
					; fac/2,rlo^2,res,qlo,qlo*fhi,resm,rem
	  fxch	st(5)			; resm,rlo^2,res,qlo,qlo*fhi,fac/2,rem
	faddp	st(2), st(0)		;54 res += resmid
					; rlo*rlo,res,qlo,qlo*fhi,fac/2,rem
	fxch	st(2)			; qlo,res,rlo*rlo,qlo*fhi,fac/2,rem
	fmul	FACLO			;55 qlo*flo
					; qlo*flo,res,rlo*rlo,qlo*fhi,fac/2,rem
	fxch	st(5)			; rem,res,rlo*rlo,qlo*fhi,fac/2,qlo*flo
	  fabs				;56   rem = 64-bit remainder
	  fxch	st(4)			; fac/2,res,rlo*rlo,qlo*fhi,rem,qlo*flo
	  fsub	st(4), st(0)		;57   rem = rem - fac/2
	  fld	FACDIFF			;58   quot = 1/fac2 - 1/fac1
					; quot,fac/2,res,rlo*rlo,qlo*fhi,
					;			rem,qlo*flo
	  fadd	st(0), st(7)		;59   quot = 1/fac2 - 1/fac1 + 1/fac1
	  fxch	st(4)			; qlo*fhi,fac/2,res,rlo*rlo,quot,
					;			rem,qlo*flo
	fsubp	st(2), st(0)		;60 res -= qlo*fhi
					; fac/2,res,rlo*rlo,quot,rem,qlo*flo
	fxch	st(5)			; qlo*flo,res,rlo*rlo,quot,rem,fac/2
	fsubp	st(2), st(0)		;61 reslow = rlo*rlo - qlo*flo
					; res,reslow,quot,rem,fac/2
	fxch	st(3)			; rem,reslow,quot,res,fac/2
	  fabs				;62   rem = |rem|
	  fxch	st(4)			; fac/2,reslow,quot,res,rem
	  fsubrp st(4), st(0)		;63   rem = 63-bit remainder!
					; reslow,quot,res,rem
	faddp	st(2), st(0)		;64 res += reslow
					; quot,res,rem
	  fld	BIGVAL1			;65   For rhi
					; rhi,quot,res,rem
	  fxch	st(3)			; rem,quot,res,rhi
	  fmul	st(1), st(0)		;66   quot = rem * 1/fac
	  fadd	st(3), st(0)		;67   rhi = rem + BIGVAL1
	  fld	BIGVAL1			;68   for qhi
					; qhi,rem,quot,res,rhi
	  fxch	st(2)			; quot,rem,qhi,res,rhi
	  fmul	st(0), st(1)		;69   quot = rem * rem * 1/fac
	  fxch	st(4)			; rhi,rem,qhi,res,quot
	  fsub	BIGVAL1			;70   rhi = rhi - BIGVAL1
	  fld	FACHI2			;71   for qhi*fhi
					; fhi,rhi,rem,qhi,res,quot
	  fxch	st(5)			; quot,rhi,rem,qhi,res,fhi
	  fadd	st(3), st(0)		;72   qhi = quot + BIGVAL1
	  fadd	BIGVAL0			;73   quot = quot + BIGVAL0
	  fxch	st(2)			; rem,rhi,quot,qhi,res,fhi
	  fsub	st(0), st(1)		;74   rlo = rem - rhi
					; rlo,rhi,quot,qhi,rem,fhi
	  fxch	st(3)			; qhi,rhi,quot,rlo,rem,fhi
	  fsub	BIGVAL1			;75   qhi = qhi - BIGVAL1
	  fxch	st(2)			; quot,rhi,qhi,rlo,rem,fhi
	  fsub	BIGVAL0			;76   quot = quot - BIGVAL0
	  fld	st(1)			;77   for res (dup rhi)
					; res,quot,rhi,qhi,rlo,rem,fhi
	  fmul	st, st			;78   res = rhi^2
	  fxch	st(2)			; rhi,quot,res,qhi,rlo,rem,fhi
	  fadd	st, st			;79   double rhi to compute 2*rhi*rlo
					; 2*rhi,quot,res,qhi,rlo,rem,fhi
	  fxch	st(3)			; qhi,quot,res,2*rhi,rlo,rem,fhi
	  fmul	st(6), st(0)		;80   qhi*fhi
					; qhi,quot,res,2*rhi,rlo,rem,qhi*fhi
	  fsub	st(1), st(0)		;81   qlo = quot - qhi
	dec	ecx			;82u
	jnz	wqloop			;82v

; Finish off the computation of fac2's remainder.  Test for rem1 and
; rem2 being one (after doubling and making positive).  I think this
; could be improved by a clock or two.

					; qhi,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fmul	FACLO2			;1    qhi*flo
					; qhi*flo,qlo,res,2*rhi,rlo,rem,qhi*fhi
	  fxch	st(6)			; qhi*fhi,qlo,res,2*rhi,rlo,rem,qhi*flo
	  fsubp	st(2), st(0)		;2    res -= qhi*fhi
					; qlo,res,2*rhi,rlo,rem,qhi*flo
	  fxch	st(2)			; 2*rhi,res,qlo,rlo,rem,qhi*flo
	  fmul	st(0), st(3)		;3    2*rhi*rlo
					; 2*rhi*rlo,res,qlo,rlo,rem,qhi*flo
	  fxch	st(4)			; rem,res,qlo,rlo,2*rhi*rlo,qhi*flo
	  fld	FACLO2			;4    For computing qlo*flo
	  				; flo,rem,res,qlo,rlo,2*rhi*rlo,qhi*flo
	  fxch	st(4)			; rlo,rem,res,qlo,flo,2*rhi*rlo,qhi*flo
	  fmul	st, st			;5    rlo*rlo
	  				; rlo^2,r,res,qlo,flo,2*rhi*rlo,qhi*flo
	  fxch	st(6)			; qhi*flo,r,res,qlo,flo,2*rhi*rlo,rlo^2
	  fsubp	st(5), st		;6    resmid = 2*rhi*rlo - qhi*flo
					; rem,res,qlo,flo,resmid,rlo^2
	  fxch	st(2)			; qlo,res,rem,flo,resmid,rlo^2
	  fmul	st(3), st(0)		;7    qlo*flo
					; qlo,res,rem,qlo*flo,resmid,rlo^2
	  fxch	st(2)			; rem,res,qlo,qlo*flo,resmid,rlo^2
	fadd	st, st			;8  rem1 = rem1 * 2
	fxch	st(4)			; resmid,res,qlo,qlo*flo,rem,rlo^2
	  faddp	st(1), st(0)		;9    res += resmid
					; res,qlo,qlo*flo,rem,rlo^2
	  fxch	st(1)			; qlo,res,qlo*flo,rem,rlo^2
	  fmul	FACHI2			;10   qlo*fhi
					; qlo*fhi,res,qlo*flo,rem,rlo^2
	  fxch	st(2)			; qlo*flo,res,qlo*fhi,rem,rlo^2
	  fsubp	st(4), st(0)		;11   reslow = rlo*rlo - qlo*flo
					; res,qlo*fhi,rem,reslow
	  fxch	st(2)			; rem,qlo*fhi,res,reslow
	fsub	ONE			;12 rem1 = rem1 - 1
	fxch	st(1)			; qlo*fhi,rem,res,reslow
	  fsubp	st(2), st(0)		;13   res -= qlo*fhi
					; rem,res,reslow
	  fld	FACHI2			;14   fac2 = fachi
					; fac2,rem,res,reslow
	  fadd	FACLO2			;15   fac2 = fachi + faclo
	  fxch	st(3)			; reslow,rem,res,fac2
	  faddp	st(2), st(0)		;16   res += reslow
					; rem1,rem2,fac2
	fabs				;17 rem1 = |rem1|
	fsub	FACHI			;18 rem1 -= FACHI
	fxch	st(2)			; fac2,rem2,rem1
	  fmul	HALF			;19   fac2/2 = fac2/2
					; fac2/2,rem2,rem1
	  fxch	st(1)			; rem2,fac2/2,rem1
	  fsub	HALF			;20   rem2 = rem2 - 1/2
	  fxch	st(2)			; rem1,fac2/2,rem2
	fsub	FACLO			;21 rem1 -= FACLO
	fxch	st(3)			; 1/fac1,fac2/2,rem2,rem1
	fcomp	st			;22 pop 1/fac value
					; fac2/2,rem2,rem1
	fxch	st(1)			; rem2,fac2/2,rem1
	  fabs				;23   rem2 = |rem2|
	  fsubrp st(1),st(0)		;24   rem2 = rem2 - fac2/2
					; rem2,rem1
	  fxch	st(1)			; rem1,rem2
	fstp	temp			;25-26 Save rem1 for comparing
	mov	ecx, temp		;27
	cmp	ecx, 00000000h		;28U Compare remainder to 0.0
	je	short win1		;28V
	  fstp	temp			;29-30 Save rem2 for comparing
	mov	ecx, temp		;31
	cmp	ecx, 00000000h		;32U Compare remainder to 0.0
	je	short win2		;32V
	retn				;33UV - Test next factor from sieve

win1:	fld	FACHI			; Load MSW
	fadd	FACLO			; Load LSW
	jmp	short wincom		; Join common code
win2:	fld	FACHI2			; Load MSW
	fadd	FACLO2			; Load LSW
wincom:	fsub	ONE			; Make it fit in 63 bits
	fmul	HALF
	fistp	QWORD PTR savefac2	; Save as a 64-bit integer
	mov	eax, savefac2		; Load LSW
	mov	edx, savefac1		; Load MSW
	add	eax, eax		; Double it to 64 bits again
	adc	edx, edx
	inc	eax			; Undo the decrement
	mov	FACLSW, eax		; Store LSW
	mov	FACMSW, edx		; Store MSW
	mov	eax, 1			; Factor found!!! Return TRUE
	add	esp, 4			; pop return address
	jmp	done

; One sieve is done, save registers and see if another
; sieve will be tested before we check for an ESC.

	push_amt = 0
wlpdn:	cmp	edi, -2			; Is a factor queued up?
	je	short wlpdn2		; No
	fld	FACHI			; Yes, go test it
	fstp	FACHI2
	fld	FACLO
	fstp	FACLO2
	fld	st(1)
	fld	st(1)
	call	west2
wlpdn2:	mov	savefac1, ebx		; Save for the restart or more sieving
	mov	savefac2, ebp
	dec	reps			; Check repetition counter
	jnz	slp0
	cmp	ebx, -1			; do one more rep if FACMSW is maxed
	jne	short wlpdn1
	inc	reps
	jmp	slp0
wlpdn1:	mov	eax, 2			; Return for ESC check
	mov	FACMSW, ebx
	cmp	ebx, -1			; Check for special value
	jne	done
	mov	FACHSW, 1		; Return 2^64
	mov	FACMSW, 0
	jmp	done

;***********************************************************************
; For 486 machines only
;***********************************************************************

;
; Check all the bits in the sieve looking for a factor to test
;

ulp:	mov	esi, sieve
	mov	fac1, savefac1
	mov	fac2, savefac2
	sub	edx, edx
	mov	eax, [esi]
	lea	esi, [esi+4]
ux0:	test	al, 01h
	jnz	ust0
ux1:	test	al, 02h
	jnz	ust1
ux2:	test	al, 04h
	jnz	ust2
ux3:	test	al, 08h
	jnz	ust3
ux4:	test	al, 10h
	jnz	ust4
ux5:	test	al, 20h
	jnz	ust5
ux6:	test	al, 40h
	jnz	ust6
ux7:	test	al, 80h
	jnz	ust7
ux8:	test	eax, 100h
	jnz	ust8
ux9:	test	eax, 200h
	jnz	ust9
ux10:	test	eax, 400h
	jnz	ust10
ux11:	test	eax, 800h
	jnz	ust11
ux12:	test	eax, 1000h
	jnz	ust12
ux13:	test	eax, 2000h
	jnz	ust13
ux14:	test	eax, 4000h
	jnz	ust14
ux15:	test	eax, 8000h
	jnz	ust15
ux16:	test	eax, 10000h
	jnz	ust16
ux17:	test	eax, 20000h
	jnz	ust17
ux18:	test	eax, 40000h
	jnz	ust18
ux19:	test	eax, 80000h
	jnz	ust19
ux20:	test	eax, 100000h
	jnz	ust20
ux21:	test	eax, 200000h
	jnz	ust21
ux22:	test	eax, 400000h
	jnz	ust22
ux23:	test	eax, 800000h
	jnz	ust23
ux24:	test	eax, 1000000h
	jnz	short ust24
ux25:	test	eax, 2000000h
	jnz	short ust25
ux26:	test	eax, 4000000h
	jnz	short ust26
ux27:	test	eax, 8000000h
	jnz	short ust27
ux28:	test	eax, 10000000h
	jnz	short ust28
ux29:	test	eax, 20000000h
	jnz	short ust29
ux30:	test	eax, 40000000h
	jnz	short ust30
ux31:	test	eax, 80000000h
	jnz	short ust31
ulp5:	add	fac2, facdist32		; U - Add facdist * 32 to the factor
	mov	eax, [esi]		; V - Next sieve word
	adc	fac1, facdist32+4	; U - Add carry
	test	esi, sievesize		; V - End of sieve?
	lea	esi, [esi+4]		; U - Bump pointer
	jz	ux0			; V - Loop to test next sieve dword
	mov	savefac1, fac1		; Save for the restart or more sieving
	mov	savefac2, fac2

; Check repetition counter

	dec	reps
	jnz	slp0

; Return so caller can check for ESC

	mov	eax, 2			; Return for ESC check
	mov	FACMSW, fac1
	jmp	done

; Entry points for testing each bit#
; This scheme is faster on 486 where the above sieve testing code
; will fall through 90% of the time.

ust23:	mov	dl, 23			; Test factor corresponding to bit 23
	jmp	short uestit
ust24:	mov	dl, 24			; Test factor corresponding to bit 24
	jmp	short uestit
ust25:	mov	dl, 25			; Test factor corresponding to bit 25
	jmp	short uestit
ust26:	mov	dl, 26			; Test factor corresponding to bit 26
	jmp	short uestit
ust27:	mov	dl, 27			; Test factor corresponding to bit 27
	jmp	short uestit
ust28:	mov	dl, 28			; Test factor corresponding to bit 28
	jmp	short uestit
ust29:	mov	dl, 29			; Test factor corresponding to bit 29
	jmp	short uestit
ust30:	mov	dl, 30			; Test factor corresponding to bit 30
	jmp	short uestit
ust31:	mov	dl, 31			; Test factor corresponding to bit 31
	jmp	short uestit
ust22:	mov	dl, 22			; Test factor corresponding to bit 22
	jmp	short uestit
ust21:	mov	dl, 21			; Test factor corresponding to bit 21
	jmp	short uestit
ust20:	mov	dl, 20			; Test factor corresponding to bit 20
	jmp	short uestit
ust19:	mov	dl, 19			; Test factor corresponding to bit 19
	jmp	short uestit
ust18:	mov	dl, 18			; Test factor corresponding to bit 18
	jmp	short uestit
ust17:	mov	dl, 17			; Test factor corresponding to bit 17
	jmp	short uestit
ust16:	mov	dl, 16			; Test factor corresponding to bit 16
	jmp	short uestit
ust15:	mov	dl, 15			; Test factor corresponding to bit 15
	jmp	short uestit
ust14:	mov	dl, 14			; Test factor corresponding to bit 14
	jmp	short uestit
ust13:	mov	dl, 13			; Test factor corresponding to bit 13
	jmp	short uestit
ust12:	mov	dl, 12			; Test factor corresponding to bit 12
	jmp	short uestit
ust11:	mov	dl, 11			; Test factor corresponding to bit 11
	jmp	short uestit
ust10:	mov	dl, 10			; Test factor corresponding to bit 10
	jmp	short uestit
ust9:	mov	dl, 9			; Test factor corresponding to bit 9
	jmp	short uestit
ust8:	mov	dl, 8			; Test factor corresponding to bit 8
	jmp	short uestit
ust7:	mov	dl, 7			; Test factor corresponding to bit 7
	jmp	short uestit
ust6:	mov	dl, 6			; Test factor corresponding to bit 6
	jmp	short uestit
ust5:	mov	dl, 5			; Test factor corresponding to bit 5
	jmp	short uestit
ust4:	mov	dl, 4			; Test factor corresponding to bit 4
	jmp	short uestit
ust3:	mov	dl, 3			; Test factor corresponding to bit 3
	jmp	short uestit
ust2:	mov	dl, 2			; Test factor corresponding to bit 2
	jmp	short uestit
ust1:	mov	dl, 1			; Test factor corresponding to bit 1
	jmp	short uestit
ust0:	mov	dl, 0			; Test factor corresponding to bit 0

;
; This is the 486 version of testit.  The Pentium version is 20% slower than
; this algorithm.
;

uestit:	pusher	returns4[edx*4]		; Push return address
	pusher	esi			; Save sieve testing registers
	add	fac2, facdists[edx*8]	; Determine factor to test
	adc	fac1, facdists+4[edx*8]	; Add carry
	pusher	edx	
	pusher	eax			; Save sieve testing registers

;
; Precompute 1 / factor
;

	mov	savefac1, fac1		; Store factor so FPU can load it
	mov	savefac2, fac2		; Store factor so FPU can load it
	fld1
	fild	QWORD PTR savefac2
	fdivp	st(1), st

;
; Perform a division on the initial value to get started.
;

	fld	initval			; Load initial value
	fmul	st, st(1)		; Multiply by 1 / factor
	mov	ebp, shifter		; Load shifter
	sub	esi, esi		; LSW of initval should be zero
	mov	edi, initval1		; MSW of initval
	fistp	QWORD PTR quotient2	; Save the quotient
	mov	eax, fac2		; Compute quotient2 * fac2
	mul	quotient2
	sub	esi, eax
	sbb	edi, edx
	mov	eax, fac2		; Compute quotient1 * fac2
	imul	eax, quotient1
	sub	edi, eax
	mov	eax, fac1		; Compute quotient2 * fac1
	imul	eax, quotient2
	sub	edi, eax

;
; Square remainder (edi, esi) and get new remainder.  Amazingly, this
; code was originally written to work with unsigned remainders, but it
; works even though remainders are now signed.  This code handles
; factors up to 62 bits long.
;

uqloop:	mov	rem1, edi		; Write remainder to memory
	mov	rem2, esi		; so FPU can load it
	fild	QWORD PTR rem2		; Load remainder
	fmul	st, st			; Square the remainder
	fmul	st, st(1)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient2	; Save the quotient
	imul	edi, esi		; Compute rem2 * rem1
	mov	eax, esi		; Load rem2
	mul	eax			; Compute rem2 * rem2
	add	edi, edi		; Double rem2 * rem1
	mov	esi, eax		; Save rem2 * rem2 low
	add	edi, edx		; Add in rem2 * rem2 high
	mov	eax, fac2		; Compute quotient2 * fac2
	mul	quotient2
	sub	esi, eax
	sbb	edi, edx
	mov	eax, fac2		; Compute quotient1 * fac2
	imul	eax, quotient1
	sub	edi, eax
	mov	eax, fac1		; Compute quotient2 * fac1
	imul	eax, quotient2
	sub	edi, eax

;
; Multiply by two if necessary, test for end of squaring loop
;

	add	ebp, ebp		; One squaring completed, shift
	jnc	uqloop			; Is a mul by 2 needed?
	jz	short uqexit		; Are we done squaring?
	add	esi, esi		; Multiply remainder by 2
	adc	edi, edi
	jc	short uaddf		; Jump if remainder is negative
	sub	esi, fac2		; Sub fac so that |remainder| < factor
	sbb	edi, fac1
	jmp	uqloop
uaddf:	add	esi, fac2		; Add fac so that |remainder| < factor
	adc	edi, fac1
	jmp	uqloop

;
; Multiply remainder by two one last time (for the last carry out of shifter)
; If result = 1 mod factor, then we found a divisor of 2**p - 1
;

uqexit:	fstp	quotient2		; Pop 1 / factor from FPU
	add	esi, esi		; Double the remainder
	adc	edi, edi		; Propogate carry
	popper	eax			; Restore sieve testing register
	popper	edx

	; Handle a negative remainder, add factor and see if value is 1
	jnc	short upos		; Positive remainder?
	add	esi, fac2		; No, make remainder positive
	adc	edi, fac1
	jz	short umaybe		; Check if remainder is one
	popper	esi			; Restore sieve testing register
	sub	fac2, facdists[edx*8]	; Subtract factor increment
	sbb	fac1, facdists+4[edx*8]
	retn				; Test next factor from sieve

	push_amt = 8
	; Handle a positive remainder, subtract factor and see if value is 1
upos:	sub	esi, fac2		; No, make remainder positive
	sbb	edi, fac1
	jz	short umaybe
unext:	popper	esi			; Restore sieve testing register
	sub	fac2, facdists[edx*8]	; Subtract factor increment
	sbb	fac1, facdists+4[edx*8]
	retn				; Test next factor from sieve

	push_amt = 8
umaybe:	cmp	esi, 1
	jne	short unext

	mov	eax, 1			; Factor found!!! Return TRUE
	mov	FACMSW, fac1
	mov	FACLSW, fac2
	add	esp, 8			; pop sieve testing register
					; and testit's return address
	push_amt = 0
	jmp	done


;***********************************************************************
; For Pentium Pro machines only
;***********************************************************************

;
; Check all the bits in the sieve looking for a factor to test
;

plp:	mov	esi, sieve		; Sieve address
	sub	edi, edi		; Count of queued factors to be tested
resdata:cmp	edi, queuedpro		; Restore queued factor data
	je	short resdone
	fild	QWORD PTR pfac2[edi*8]	; Load the factor to test
	fld1
	fdivrp	st(1), st		; Compute 1 / factor
	inc	edi
	jmp	short resdata
resdone:
	fild	QWORD PTR savefac2
px0:	mov	eax, [esi]		; Load word from sieve
	lea	esi, [esi+4]		; Bump sieve address
px1:	bsf	edx, eax		; Look for a set bit
	jnz	short pestit		; Found one, go test the factor
	fadd	facdist_flt     	; Add facdist * 32 to the factor
	test	esi, sievesize		; End of sieve?
	jz	short px0		; Loop to test next sieve dword
	fistp	QWORD PTR savefac2	; Save for the restart or more sieving

; If factors are queued up, save the floating point values on the stack

	mov	queuedpro, edi		; Save count of queued factors
svdata:	and	edi, edi
	jz	short svdone
	dec	edi
	fcomp	st			; Toss precomputed reciprocal
	jmp	short svdata
svdone:

; Check repetition counter

	dec	reps
	jnz	slp0

; Return so caller can check for ESC

	mov	eax, savefac1		; Return for ESC check
	mov	FACMSW, eax
	mov	eax, 2
	jmp	done

;
; This is the Pentium Pro version of testit.  It gathers 4 potential factors
; to be tested all at once to minimize processor stalls.
;

pestit:	fld	QWORD PTR facdistsflt[edx*8] ; Determine factor to test
	fadd	st, st(1)
	fld	st(0)
	fistp	QWORD PTR pfac2[edi*8]	; Save the factor to test
	fld	st(0)
	fchs
	fistp	QWORD PTR pneg2[edi*8]	; Save the -factor to test

;
; Precompute 1 / factor
;

	fld1
	fdivrp	st(1), st
	fxch	st(1)

; Schedule integer arithmetic while fdiv finishes up.

	btr	eax, edx		; Clear the sieve bit
	inc	edi			; One more factor queued up
	cmp	edi, 4			; Have enough been queued up?
	jne	px1			; No, go test more sieve bits

;
; Now test the 4 factors
;

	pusher	esi			; Save sieve testing registers
	pusher	eax
	mov	ebp, shifter		; Load shifter

;
; Perform a modulo on the initial value to get started.
;

	fld	initval			; Load initial value
	fmul	st, st(5)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient2	; Save the quotient
	fld	initval			; Load initial value
	fmul	st, st(4)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient4	; Save the quotient
	fld	initval			; Load initial value
	fmul	st, st(3)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient6	; Save the quotient
	fld	initval			; Load initial value
	fmul	st, st(2)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient8	; Save the quotient

	mov	edi, initval1		; MSW of initval
	sub	esi, esi		; LSW of initval should be zero
	mov	ecx, initval1		; MSW of initval
	sub	ebx, ebx		; LSW of initval should be zero

	mov	eax, quotient2		; Compute quotient2 * fac2
	mul	pfac2
	sub	esi, eax
	sbb	edi, edx
	mov	eax, quotient4		; Compute quotient4 * fac4
	mul	pfac4
	sub	ebx, eax
	sbb	ecx, edx

	mov	eax, quotient1		; Compute quotient1 * fac2
	mov	edx, quotient3		; Compute quotient3 * fac4
	imul	eax, pfac2
	imul	edx, pfac4
	sub	edi, eax
	sub	ecx, edx

	mov	eax, quotient2		; Compute quotient2 * fac1
	mov	edx, quotient4		; Compute quotient4 * fac3
	imul	eax, pfac1
	imul	edx, pfac3
	sub	edi, eax
	sub	ecx, edx

	mov	rem2, esi
	mov	rem1, edi
	mov	rem4, ebx
	mov	rem3, ecx

	mov	edi, initval1		; MSW of initval
	sub	esi, esi		; LSW of initval should be zero
	mov	ecx, initval1		; MSW of initval
	sub	ebx, ebx		; LSW of initval should be zero

	mov	eax, quotient6		; Compute quotient6 * fac6
	mul	pfac6
	sub	esi, eax
	sbb	edi, edx
	mov	eax, quotient8		; Compute quotient8 * fac8
	mul	pfac8
	sub	ebx, eax
	sbb	ecx, edx

	mov	eax, quotient5		; Compute quotient5 * fac6
	mov	edx, quotient7		; Compute quotient7 * fac8
	imul	eax, pfac6
	imul	edx, pfac8
	sub	edi, eax
	sub	ecx, edx

	mov	eax, quotient6		; Compute quotient6 * fac5
	mov	edx, quotient8		; Compute quotient8 * fac7
	imul	eax, pfac5
	imul	edx, pfac7
	sub	edi, eax
	sub	ecx, edx

	mov	rem6, esi
	mov	rem5, edi
	mov	rem8, ebx
	mov	rem7, ecx

;
; Square remainders (rem1 through rem8) and get new remainders.
; This code handles factors up to 62 bits long.
;

pqloop:	fild	QWORD PTR rem2		; Load remainder
	fmul	st, st			; Square the remainder
	fmul	st, st(5)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient2	; Save the quotient
	fild	QWORD PTR rem4		; Load remainder
	fmul	st, st			; Square the remainder
	fmul	st, st(4)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient4	; Save the quotient
	fild	QWORD PTR rem6		; Load remainder
	fmul	st, st			; Square the remainder
	fmul	st, st(3)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient6	; Save the quotient
	fild	QWORD PTR rem8		; Load remainder
	fmul	st, st			; Square the remainder
	fmul	st, st(2)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient8	; Save the quotient

	mov	edi, rem1		; Load rem1
	mov	eax, rem2		; Load rem2
	imul	edi, eax		; Compute rem2 * rem1
	mul	eax			; Compute rem2 * rem2
	mov	esi, eax		; Save rem2 * rem2 low
	lea	edi, [edi*2+edx]	; Add in 2 * rem2 * rem1

	mov	ecx, rem3		; Load rem3
	mov	eax, rem4		; Load rem4
	imul	ecx, eax		; Compute rem4 * rem3
	mul	eax			; Compute rem4 * rem4
	mov	ebx, eax		; Save rem4 * rem4 low
	lea	ecx, [ecx*2+edx]	; Add in 2 * rem4 * rem3

	mov	eax, quotient2		; Compute quotient2 * fac2
	mul	pfac2
	sub	esi, eax
	sbb	edi, edx
	mov	eax, quotient4		; Compute quotient4 * fac4
	mul	pfac4
	sub	ebx, eax
	sbb	ecx, edx

	mov	eax, quotient1		; Compute quotient1 * fac2
	mov	edx, quotient3		; Compute quotient3 * fac4
	imul	eax, pfac2
	imul	edx, pfac4
	sub	edi, eax
	sub	ecx, edx

	mov	eax, quotient2		; Compute quotient2 * fac1
	mov	edx, quotient4		; Compute quotient4 * fac3
	imul	eax, pfac1
	imul	edx, pfac3
	sub	edi, eax
	sub	ecx, edx

	add	ebp, ebp		; Will a mul by 2 be needed?
	jc	pmul2			; Yes, go to that code

	mov	rem2, esi
	mov	rem1, edi
	mov	rem4, ebx
	mov	rem3, ecx

	mov	edi, rem5		; Load rem5
	mov	eax, rem6		; Load rem6
	imul	edi, eax		; Compute rem6 * rem5
	mul	eax			; Compute rem6 * rem6
	mov	esi, eax		; Save rem6 * rem6 low
	lea	edi, [edi*2+edx]	; Add in 2 * rem6 * rem5

	mov	ecx, rem7		; Load rem7
	mov	eax, rem8		; Load rem8
	imul	ecx, eax		; Compute rem8 * rem7
	mul	eax			; Compute rem8 * rem8
	mov	ebx, eax		; Save rem8 * rem8 low
	lea	ecx, [ecx*2+edx]	; Add in 2 * rem8 * rem7

	mov	eax, quotient6		; Compute quotient6 * fac6
	mul	pfac6
	sub	esi, eax
	sbb	edi, edx
	mov	eax, quotient8		; Compute quotient8 * fac8
	mul	pfac8
	sub	ebx, eax
	sbb	ecx, edx

	mov	eax, quotient5		; Compute quotient5 * fac6
	mov	edx, quotient7		; Compute quotient7 * fac8
	imul	eax, pfac6
	imul	edx, pfac8
	sub	edi, eax
	sub	ecx, edx

	mov	eax, quotient6		; Compute quotient6 * fac5
	mov	edx, quotient8		; Compute quotient8 * fac7
	imul	eax, pfac5
	imul	edx, pfac7
	sub	edi, eax
	sub	ecx, edx

	mov	rem6, esi
	mov	rem5, edi
	mov	rem8, ebx
	mov	rem7, ecx
	jmp	pqloop

;
; Multiply by two after finding the remainder
;

pmul2:	jz	pqexit			; Jmp if this is the last iteration

	mov	eax, pfac2
	mov	edx, pfac1
	add	esi, esi		; Multiply remainder by 2
	adc	edi, edi
	cmovnc	eax, pneg2
	cmovnc	edx, pneg1
	add	esi, eax		; Sub or add factor so that
	adc	edi, edx		; |remainder| < factor
	mov	rem2, esi
	mov	rem1, edi

	mov	eax, pfac4
	mov	edx, pfac3
	add	ebx, ebx		; Multiply remainder by 2
	adc	ecx, ecx
	cmovnc	eax, pneg4
	cmovnc	edx, pneg3
	add	ebx, eax		; Sub or add factor so that
	adc	ecx, edx		; |remainder| < factor
	mov	rem4, ebx
	mov	rem3, ecx

	mov	edi, rem5		; Load rem5
	mov	eax, rem6		; Load rem6
	imul	edi, eax		; Compute rem6 * rem5
	mul	eax			; Compute rem6 * rem6
	mov	esi, eax		; Save rem6 * rem6 low
	lea	edi, [edi*2+edx]	; Add in 2 * rem6 * rem5

	mov	ecx, rem7		; Load rem7
	mov	eax, rem8		; Load rem8
	imul	ecx, eax		; Compute rem8 * rem7
	mul	eax			; Compute rem8 * rem8
	mov	ebx, eax		; Save rem8 * rem8 low
	lea	ecx, [ecx*2+edx]	; Add in 2 * rem8 * rem7

	mov	eax, quotient6		; Compute quotient6 * fac6
	mul	pfac6
	sub	esi, eax
	sbb	edi, edx
	mov	eax, quotient8		; Compute quotient8 * fac8
	mul	pfac8
	sub	ebx, eax
	sbb	ecx, edx

	mov	eax, quotient5		; Compute quotient5 * fac6
	mov	edx, quotient7		; Compute quotient7 * fac8
	imul	eax, pfac6
	imul	edx, pfac8
	sub	edi, eax
	sub	ecx, edx

	mov	eax, quotient6		; Compute quotient6 * fac5
	mov	edx, quotient8		; Compute quotient8 * fac7
	imul	eax, pfac5
	imul	edx, pfac7
	sub	edi, eax
	sub	ecx, edx

	mov	eax, pfac6
	mov	edx, pfac5
	add	esi, esi		; Multiply remainder by 2
	adc	edi, edi
	cmovnc	eax, pneg6
	cmovnc	edx, pneg5
	add	esi, eax		; Sub or add factor so that
	adc	edi, edx		; |remainder| < factor
	mov	rem6, esi
	mov	rem5, edi

	mov	eax, pfac8
	mov	edx, pfac7
	add	ebx, ebx		; Multiply remainder by 2
	adc	ecx, ecx
	cmovnc	eax, pneg8
	cmovnc	edx, pneg7
	add	ebx, eax		; Sub or add factor so that
	adc	ecx, edx		; |remainder| < factor
	mov	rem8, ebx
	mov	rem7, ecx
	jmp	pqloop

;
; After doubling the remainders, see if we've found a factor.
; If result = 1 mod factor, then we found a divisor of 2**p - 1.
; The successful doubled remainder will equal factor+1 or 1-factor.
;
; Note: We first do a quick test to see if we might have a factor
; before doing a more expensive but accurate test.  The inexpensive
; test will eliminate all but 2 in 2^32 candidates.
;

pqexit:	fxch	st(4)
	fcompp				; Pop two 1 / factor from FPU
	fcompp				; Pop two 1 / factor from FPU

	lea	eax, [esi*2-1]		; Compute LSW of remainder*2-1
	cmp	eax, pfac2		; Is value equal to factor?
	je	short pwin1		; Jump if it might be
	cmp	eax, pneg2		; Is value equal to -factor?
	jne	short pq2a		; No
pwin1:	xor	eax, eax		; Set al to indicate pfac1/pfac2
	call	pwin			; Go to found-a-factor code
pq2a:	lea	eax, [ebx*2-1]		; Compute LSW of remainder*2-1
	cmp	eax, pfac4		; Is value equal to factor?
	je	short pwin2		; Jump if it might be
	cmp	eax, pneg4		; Is value equal to -factor?
	jne	short pq3a		; No
pwin2:	mov	esi, ebx		; pwin expects remainder in edi/esi
	mov	edi, ecx
	mov	al, 1			; Set al to indicate pfac3/pfac4
	call	pwin			; Go to found-a-factor code

pq3a:	mov	edi, rem5		; Load rem5
	mov	eax, rem6		; Load rem6
	imul	edi, eax		; Compute rem6 * rem5
	mul	eax			; Compute rem6 * rem6
	mov	esi, eax		; Save rem6 * rem6 low
	lea	edi, [edi*2+edx]	; Add in 2 * rem6 * rem5

	mov	ecx, rem7		; Load rem7
	mov	eax, rem8		; Load rem8
	imul	ecx, eax		; Compute rem8 * rem7
	mul	eax			; Compute rem8 * rem8
	mov	ebx, eax		; Save rem8 * rem8 low
	lea	ecx, [ecx*2+edx]	; Add in 2 * rem8 * rem7

	mov	eax, quotient6		; Compute quotient6 * fac6
	mul	pfac6
	sub	esi, eax
	sbb	edi, edx
	mov	eax, quotient8		; Compute quotient8 * fac8
	mul	pfac8
	sub	ebx, eax
	sbb	ecx, edx

	mov	eax, quotient5		; Compute quotient5 * fac6
	mov	edx, quotient7		; Compute quotient7 * fac8
	imul	eax, pfac6
	imul	edx, pfac8
	sub	edi, eax
	sub	ecx, edx

	mov	eax, quotient6		; Compute quotient6 * fac5
	mov	edx, quotient8		; Compute quotient8 * fac7
	imul	eax, pfac5
	imul	edx, pfac7
	sub	edi, eax
	sub	ecx, edx

	lea	eax, [esi*2-1]		; Compute LSW of remainder*2-1
	cmp	eax, pfac6		; Is value equal to factor?
	je	short pwin3		; Jump if it might be
	cmp	eax, pneg6		; Is value equal to -factor?
	jne	short pq4a		; No
pwin3:	mov	al, 2			; Set al to indicate pfac5/pfac6
	call	pwin			; Go to found-a-factor code
pq4a:	lea	eax, [ebx*2-1]		; Compute LSW of remainder*2-1
	cmp	eax, pfac8		; Is value equal to factor?
	je	short pwin4		; Jump if it might be
	cmp	eax, pneg8		; Is value equal to -factor?
	jne	short pret		; No
pwin4:	mov	esi, ebx		; pwin expects remainder in edi/esi
	mov	edi, ecx
	mov	al, 3			; Set edi to indicate pfac7/pfac8
	call	pwin			; Go to found-a-factor code

; No factors found, go search for more

pret:	popper	eax			; Restore sieve testing registers
	popper	esi
	sub	edi, edi		; Clear count of queued factors
	jmp	px1			; Test next factor from sieve

; We probably found a factor, test it out more thoroughly

	push_amt = 12
pwin:	xor	edx, edx		; Zero extend al
	mov	dl, al
	mov	rem2, esi		; Save remainder for FPU to load
	mov	rem1, edi
	fild	QWORD PTR rem2		; Load the remainder
	fadd	st, st			; Double the remainder
	fsub	ONE			; Subtract one
	fabs				; Take the absolute value
	fild	QWORD PTR pfac2[edx*8]	; Load the factor
	fcompp				; Compare remainder to factor
	fstsw	ax			; Copy comparison results
	and	eax, 4000h		; Isolate C3 bit
	jnz	short pfound		; Jump if equal
	retn				; Not a factor, continue searching

; We found a factor, return it

pfound:	fstp	temp			; Toss the value on the FPU stack
	mov	eax, pfac1[edx*8]	; Copy factor to fixed memory address
	mov	FACMSW, eax
	mov	eax, pfac2[edx*8]
	mov	FACLSW, eax
	mov	eax, 1			; Factor found!!! Return TRUE
	add	esp, 12			; Pop sieve testing registers
					; and the pwin return address
	push_amt = 0
	jmp	done

;***********************************************************************
; For all machines - 80 bit factors
; Not really optimized.  The bit check code is best on the Pentium Pro
;***********************************************************************

;
; Check all the bits in the sieve looking for a factor to test
;

tlp80:	mov	esi, sieve
	fild	QWORD PTR savefac1	; Load savefac0 and savefac1
	fmul	TWO_TO_32
	mov	eax, savefac2		; Copy savefac2 for loading as a QWORD
	mov	faclow, eax
	mov	facmid, 0
	fild	QWORD PTR faclow	; Load savefac2
zx0:	mov	eax, [esi]		; Load word from sieve
	lea	esi, [esi+4]		; Bump sieve address
zx1:	bsf	edx, eax		; Look for a set bit
	jnz	short zestit		; Found one, go test the factor
	fadd	facdist_flt     	; Add facdist * 32 to the factor
	test	esi, sievesize		; End of sieve?
	jz	short zx0		; Loop to test next sieve dword

; Bump savefac value

	mov	eax, savefac1		; Compute new savefac0 and savefac1
	mov	edx, savefac0
	fistp	QWORD PTR savefac2
	add	eax, savefac1
	adc	edx, 0
	mov	savefac1, eax
	mov	savefac0, edx
	fstp	temp			; Pop trash

; Check repetition counter

	dec	reps
	jnz	slp0

; Return so caller can check for ESC

	mov	FACMSW, eax
	mov	FACHSW, edx
	mov	eax, 2
	jmp	done

;
; This is the version of testit for nearly 80-bit factors.
;
; NOT OPTIMIZED
;
; eax = sieve word - must be preserved or reloaded
; edx = sieve bit being tested
; esi = sieve address - must be preserved
; st(1) = savefac0 and savefac1 of the first sieve bit
; st(0) = savefac2 + accumulated facdist_flts
;

;
; Compute the factor to test and 2^123 / factor
;

zestit:	fld	QWORD PTR facdistsflt[edx*8]
	fadd	st, st(1)
	fld	st(0)
	fistp	QWORD PTR faclow	; Save lower bits of factor
	fadd	st, st(2)		; We now have the factor to test
	fld	TWO_TO_123
	fdivrp	st(1), st		; quotient

; The FDIV takes 39 clocks and only the last two can overlap
; with other float instructions.  This gives us 37 clocks
; to do something useful with the integer units.

	btr	eax, edx		; Clear the sieve bit
	pusher	eax			; Save registers
	pusher	esi
	mov	ebp, shifter		; Load shifter
	mov	eax, savefac1		; Finish computing factor as an integer
	mov	edx, savefac0
	add	eax, facmid
	adc	edx, 0
	mov	facmid, eax
	mov	fachigh, edx

; Now precompute 2^123 mod factor

	sub	ebx, ebx		; Compute remainder in ebx/ecx/esi
	sub	ecx, ecx
	sub	esi, esi
	fld	st(0)
	fistp	QWORD PTR quotient2	; Save the quotient
	mov	eax, quotient2		; Subtract quotient * fac
	mul	faclow
	sub	esi, eax
	sbb	ecx, edx
	sbb	ebx, 0
	mov	eax, quotient2
	mul	facmid
	sub	ecx, eax
	sbb	ebx, edx
	mov	eax, quotient2
	mul	fachigh
	sub	ebx, eax

	mov	eax, quotient1
	mul	faclow
	sub	ecx, eax
	sbb	ebx, edx
	mov	eax, quotient1
	mul	facmid
	sub	ebx, eax

jns short zzz2
add esi, faclow
adc ecx, facmid
adc ebx, fachigh
zzz2:

	mov	two_to_123_modf_hi, ebx
	mov	two_to_123_modf_mid, ecx
	mov	two_to_123_modf_lo, esi

; Now compute 2^64 / factor

	fmul	TWO_TO_MINUS_59

; Work on initval.
; This is like the zqloop code except that we avoid the initial squaring.

	fld	initval64		; Compute initval / factor
	fmul	st, st(1)
	fistp	QWORD PTR quotient2	; Save the quotient
	mov	ebx, initval0
	mov	ecx, initval1
	sub	esi, esi

; Subtract out quotient * factor

	mov	eax, quotient2		; Subtract quotient * fac
	mul	faclow
	sub	esi, eax
	sbb	ecx, edx
	sbb	ebx, 0
	mov	eax, quotient2
	mul	facmid
	sub	ecx, eax
	sbb	ebx, edx
	mov	eax, quotient2
	mul	fachigh
	sub	ebx, eax

	mov	eax, quotient1
	mul	faclow
	sub	ecx, eax
	sbb	ebx, edx
	mov	eax, quotient1
	mul	facmid
	sub	ebx, eax

jns short zzz1
add esi, faclow
adc ecx, facmid
adc ebx, fachigh
zzz1:

; Square remainder (ebx, edi, esi) and get new remainder.
; This code should handle factors up to 76 bits long.
; In this code, remainder is in ebx, ecx, esi - these
; registers also double as third, fourth, and fifth word of the
; squared remainder.

zqloop:	pusher	ebp
	mov	ebp, ebx		; Compute upper 2 words of squared rem
	imul	ebp, ebp		; remhi * remhi
	lea	eax, [ebx+ebx]
	mul	ecx			; 2 * remhi * remmid
	mov	edi, eax
	add	ebp, edx
	lea	eax, [ebx+ebx]		; Compute third word of squared rem
	mul	esi			; 2 * remhi * remlo
	mov	ebx, eax
	add	edi, edx
	adc	ebp, 0
	mov	eax, ecx		; remmid * remmid
	mul	eax
	add	ebx, eax
	adc	edi, edx
	adc	ebp, 0
	shld	ebp, edi, 5		; Scale first word up by 5 bits
	and	edi, 07FFFFFFh		; Mask off upper 5 bits of second word

; Now compute the rest of the squared remainder and add in the scaled
; upper word times 2^123 mod fac.  This gives us a 4 word result and
; we can use the FPU to estimate the quotient properly.

	mov	eax, two_to_123_modf_hi	; scaled first word * high(2^123 mod f)
	mul	ebp
	add	ebx, eax
	adc	edi, edx

; Now compute the rest of the squared remainder and add in the scaled
; upper word times 2^123 mod fac.

	mov	eax, esi		; Compute fourth word of squared rem
	mul	ecx			; remlo * remmid
	mov	ecx, eax
	add	ebx, edx
	adc	edi, 0
	add	ecx, eax
	adc	ebx, edx
	adc	edi, 0
	mov	eax, two_to_123_modf_mid; scaled first word * mid(2^123 mod f)
	mul	ebp
	add	ecx, eax
	adc	ebx, edx
	adc	edi, 0

	mov	eax, esi		; Compute fifth word of squared rem
	mul	eax
	mov	esi, eax
	add	ecx, edx
	adc	ebx, 0
	adc	edi, 0
	mov	eax, two_to_123_modf_lo	; scaled first word * low(2^123 mod f)
	mul	ebp
	add	esi, eax
	adc	ecx, edx
	adc	ebx, 0
	adc	edi, 0
	popper	ebp

; Now compute the remaining bits of the quotient

	mov	rem2, ebx		; Write second and third words to
	mov	rem1, edi		; memory so FPU can load it
	fild	QWORD PTR rem2		; Load second and third words
	fmul	st, st(1)		; Multiply by 1 / factor
	fistp	QWORD PTR quotient2	; Save the quotient

; Subtract out quotient * factor.

	mov	eax, quotient2		; Subtract quotient * fac
	mul	faclow
	sub	esi, eax
	sbb	ecx, edx
	sbb	ebx, 0
	mov	eax, quotient2
	mul	facmid
	sub	ecx, eax
	sbb	ebx, edx
	mov	eax, quotient2
	mul	fachigh
	sub	ebx, eax

	mov	eax, quotient1
	mul	faclow
	sub	ecx, eax
	sbb	ebx, edx
	mov	eax, quotient1
	mul	facmid
	sub	ebx, eax

jns short zzz
add esi, faclow
adc ecx, facmid
adc ebx, fachigh
zzz:

;
; Multiply by two if necessary, test for end of squaring loop
;

	add	ebp, ebp		; One squaring completed, shift
	jnc	zqloop			; Is a mul by 2 needed?
	jz	short zqexit		; Are we done squaring?
	add	esi, esi		; Multiply remainder by 2
	adc	ecx, ecx
	adc	ebx, ebx
	jmp	zqloop
;
; Multiply remainder by two one last time (for the last carry out of shifter)
; If result = 1 mod factor, then we found a divisor of 2**p - 1
;

zqexit:	fstp	quotient2		; Pop 1 / factor from FPU
	add	esi, esi		; Double the remainder
	adc	ecx, ecx
	adc	ebx, ebx
	dec	esi			; See if remainder is factor + 1
	sub	esi, faclow
	jnz	short zloser
	sbb	ecx, facmid
	jnz	short zloser
	sbb	ebx, fachigh
	jz	short zwin
zloser:	popper	esi			; Restore sieve testing register
	popper	eax
	jmp	zx1			; Test next factor from sieve
	push_amt = 8
zwin:	mov	eax, fachigh		; Factor found!!! Return it
	mov	FACHSW, eax
	mov	eax, facmid
	mov	FACMSW, eax
	mov	eax, faclow
	mov	FACLSW, eax
	add	esp, 8			; pop sieve testing registers
	push_amt = 0
	mov	eax, 1			; return TRUE
	jmp	done

;***********************************************************************
; For SSE2 machines only - factors up to 86 bits
;***********************************************************************

;
; Check all the bits in the sieve looking for a factor to test
;

tlp74:	mov	esi, sieve
	wait
	sub	edi, edi		; Count of queued factors
	fild	QWORD PTR savefac1	; Load savefac0 and savefac1
	fmul	TWO_TO_32
	mov	eax, savefac2		; Copy savefac2 for loading as a QWORD
	mov	faclow, eax
	mov	facmid, 0
	fild	QWORD PTR faclow	; Load savefac2
ax0:	mov	eax, [esi]		; Load word from sieve
	lea	esi, [esi+4]		; Bump sieve address
ax1:	bsf	edx, eax		; Look for a set bit
	jnz	short aestit		; Found one, go test the factor
	fadd	facdist_flt     	; Add facdist * 32 to the factor
	test	esi, sievesize		; End of sieve?
	jz	short ax0		; Loop to test next sieve dword

; Bump savefac value

	mov	eax, savefac1		; Compute new savefac0 and savefac1
	mov	edx, savefac0
	fistp	QWORD PTR savefac2
	add	eax, savefac1
	adc	edx, 0
	mov	savefac1, eax
	mov	savefac0, edx
	fstp	temp			; Pop trash

; Check repetition counter

	dec	reps
	jnz	slp0

; Return so caller can check for ESC

	mov	FACMSW, eax
	mov	FACHSW, edx
	mov	eax, 2
	jmp	done

;
; This is the SSE2 version of testit for nearly 86-bit factors.
;
; eax = sieve word - must be preserved or reloaded
; edx = sieve bit being tested
; esi = sieve address - must be preserved
; edi = count of queued factors
; st(1) = savefac0 and savefac1 of the first sieve bit
; st(0) = savefac2 + accumulated facdist_flts
;

;
; Compute the factor to test and 63 most significant bits of 1 / factor
;

aestit:	fld	QWORD PTR facdistsflt[edx*8]
	fadd	st, st(1)
	fld	st(0)
	fistp	QWORD PTR faclow	; Save lower bits of factor
	fadd	st, st(2)		; We now have the factor to test
	fld	TWO_TO_FACSIZE_PLUS_62	; Constant to generate 63 bit inverse
	fdivrp	st(1), st
	fistp	QWORD PTR XMM_INVFAC[edi*8]

; Compute the factor in 30 bit chunks

	mov	ebx, savefac1		; Finish computing factor as an integer
	mov	ecx, savefac0
	mov	ebp, faclow
	add	ebx, facmid
	adc	ecx, 0
	shld	ecx, ebx, 4
	shld	ebx, ebp, 2
	and	ebp, 3FFFFFFFh
	and	ebx, 3FFFFFFFh
	mov	XMM_F3[edi*8], ebp
	mov	XMM_F2[edi*8], ebx
	mov	XMM_F1[edi*8], ecx

; Compute factor + 1 for comparing against when loop is done

	inc	ebp
	shl	ebp, 2
	adc	ebx, 0
	shr	ebp, 2
	shl	ebx, 2
	adc	ecx, 0
	shr	ebx, 2
	mov	XMM_COMPARE_VAL3[edi*8], ebp
	mov	XMM_COMPARE_VAL2[edi*8], ebx
	mov	XMM_COMPARE_VAL1[edi*8], ecx

; Do other initialization work

	btr	eax, edx		; Clear the sieve bit
	inc	edi			; Increment count of queued factors
	cmp	edi, 2			; Test count of queued factors
	jne	ax1			; Get another factor
	sub	edi, edi		; Reset count of queued factors

; Work on initval.
; This is like the aqloop code except that we avoid the initial squaring.

	sse2_fac_initval

; Square remainder and get new remainder.

	mov	ecx, SSE2_LOOP_COUNTER	; Number of times to loop
aqloop:	sse2_fac 74
	dec	ecx			; Decrement loop counter
	jnz	aqloop

;
; If result = factor + 1, then we found a divisor of 2**p - 1
;

	pcmpeqd	xmm2, XMMWORD PTR XMM_COMPARE_VAL3	; See if remainder is factor + 1
	pcmpeqd	xmm1, XMMWORD PTR XMM_COMPARE_VAL2
	pcmpeqd	xmm0, XMMWORD PTR XMM_COMPARE_VAL1
	pand	xmm2, xmm1
	pand	xmm2, xmm0
	pmovmskb ecx, xmm2
	cmp	cl, 0FFh		; See if we matched
	je	short awin1		; Yes! Factor found
	cmp	ch, 0FFh		; See if we matched
	je	short awin2		; Yes! Factor found
	jmp	ax1			; Test next factor from sieve
awin1:	mov	eax, XMM_F3		; Factor found!!! Return it
	mov	ebx, XMM_F2
	mov	ecx, XMM_F1
	jmp	short awin3
awin2:	mov	eax, XMM_F3+8		; Factor found!!! Return it
	mov	ebx, XMM_F2+8
	mov	ecx, XMM_F1+8
awin3:	shl	eax, 2
	shrd	eax, ebx, 2
	shl	ebx, 2
	shrd	ebx, ecx, 4
	shr	ecx, 4
	mov	FACLSW, eax
	mov	FACMSW, ebx
	mov	FACHSW, ecx
	mov	eax, 1			; return TRUE
	jmp	done

;
; Check all the bits in the sieve looking for a factor to test
;

tlp86:	mov	esi, sieve
	wait
	sub	edi, edi		; Count of queued factors
	fild	QWORD PTR savefac1	; Load savefac0 and savefac1
	fmul	TWO_TO_32
	mov	eax, savefac2		; Copy savefac2 for loading as a QWORD
	mov	faclow, eax
	mov	facmid, 0
	fild	QWORD PTR faclow	; Load savefac2
bx0:	mov	eax, [esi]		; Load word from sieve
	lea	esi, [esi+4]		; Bump sieve address
bx1:	bsf	edx, eax		; Look for a set bit
	jnz	short bestit		; Found one, go test the factor
	fadd	facdist_flt     	; Add facdist * 32 to the factor
	test	esi, sievesize		; End of sieve?
	jz	short bx0		; Loop to test next sieve dword

; Bump savefac value

	mov	eax, savefac1		; Compute new savefac0 and savefac1
	mov	edx, savefac0
	fistp	QWORD PTR savefac2
	add	eax, savefac1
	adc	edx, 0
	mov	savefac1, eax
	mov	savefac0, edx
	fstp	temp			; Pop trash

; Check repetition counter

	dec	reps
	jnz	slp0

; Return so caller can check for ESC

	mov	FACMSW, eax
	mov	FACHSW, edx
	mov	eax, 2
	jmp	done

;
; This is the SSE2 version of testit for nearly 86-bit factors.
;
; eax = sieve word - must be preserved or reloaded
; edx = sieve bit being tested
; esi = sieve address - must be preserved
; edi = count of queued factors
; st(1) = savefac0 and savefac1 of the first sieve bit
; st(0) = savefac2 + accumulated facdist_flts
;

;
; Compute the factor to test and 63 most significant bits of 1 / factor
;

bestit:	fld	QWORD PTR facdistsflt[edx*8]
	fadd	st, st(1)
	fld	st(0)
	fistp	QWORD PTR faclow	; Save lower bits of factor
	fadd	st, st(2)		; We now have the factor to test
	fld	TWO_TO_FACSIZE_PLUS_62	; Constant to generate 63 bit inverse
	fdivrp	st(1), st
	fistp	QWORD PTR XMM_INVFAC[edi*8]

; Compute the factor in 30 bit chunks

	mov	ebx, savefac1		; Finish computing factor as an integer
	mov	ecx, savefac0
	mov	ebp, faclow
	add	ebx, facmid
	adc	ecx, 0
	shld	ecx, ebx, 4
	shld	ebx, ebp, 2
	and	ebp, 3FFFFFFFh
	and	ebx, 3FFFFFFFh
	mov	XMM_F3[edi*8], ebp
	mov	XMM_F2[edi*8], ebx
	mov	XMM_F1[edi*8], ecx

; Compute factor + 1 for comparing against when loop is done

	inc	ebp
	shl	ebp, 2
	adc	ebx, 0
	shr	ebp, 2
	shl	ebx, 2
	adc	ecx, 0
	shr	ebx, 2
	mov	XMM_COMPARE_VAL3[edi*8], ebp
	mov	XMM_COMPARE_VAL2[edi*8], ebx
	mov	XMM_COMPARE_VAL1[edi*8], ecx

; Do other initialization work

	btr	eax, edx		; Clear the sieve bit
	inc	edi			; Increment count of queued factors
	cmp	edi, 2			; Test count of queued factors
	jne	bx1			; Get another factor
	sub	edi, edi		; Reset count of queued factors

; Work on initval.
; This is like the aqloop code except that we avoid the initial squaring.

	sse2_fac_initval

; Square remainder and get new remainder.

	mov	ecx, SSE2_LOOP_COUNTER	; Number of times to loop
bqloop:	sse2_fac 86
	dec	ecx			; Decrement loop counter
	jnz	bqloop

;
; If result = factor + 1, then we found a divisor of 2**p - 1
;

	pcmpeqd	xmm2, XMMWORD PTR XMM_COMPARE_VAL3	; See if remainder is factor + 1
	pcmpeqd	xmm1, XMMWORD PTR XMM_COMPARE_VAL2
	pcmpeqd	xmm0, XMMWORD PTR XMM_COMPARE_VAL1
	pand	xmm2, xmm1
	pand	xmm2, xmm0
	pmovmskb ecx, xmm2
	cmp	cl, 0FFh		; See if we matched
	je	awin1			; Yes! Factor found
	cmp	ch, 0FFh		; See if we matched
	je	awin2			; Yes! Factor found
	jmp	bx1			; Test next factor from sieve

;
; All done
;

done:	ad_epilog 0,0,rbx,rbp,rsi,rdi,xmm6,xmm7
factor64 ENDP

_TEXT	ENDS
END
