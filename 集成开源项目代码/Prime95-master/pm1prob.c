/* Copied with permission from https://github.com/preda/gpuowl/pm1 on 2020-08-11 */
/* Code courtesy of Mihai Preda */
/* Modified to work on non-Mersennes and P+1 factoring (variable takeAwaybits) */

#include "common.h"
#include "pm1prob.h"

// Table of values of Dickman's "rho" function for argument from 2 in steps of 1/20.
// Was generated in SageMath: [dickman_rho(x/20.0) for x in range(40,142)]
// which can be run online at https://sagecell.sagemath.org/ or https://cocalc.com/
double rhotab[] = {
 // 2
 0.306852819440055, 0.282765004395792,
 0.260405780162154, 0.239642788276221,
 0.220357137908328, 0.202441664262192,
 0.185799461593866, 0.170342639724018,
 0.155991263872504, 0.142672445952511,
 0.130319561832251, 0.118871574006370,
 0.108272442976271, 0.0984706136794386,
 0.0894185657243129, 0.0810724181216677,
 0.0733915807625995, 0.0663384461579859,
 0.0598781159863707, 0.0539781578442059,
 // 3
 0.0486083882911316, 0.0437373330511146,
 0.0393229695406371, 0.0353240987411619,
 0.0317034445117801, 0.0284272153221808,
 0.0254647238733285, 0.0227880556511908,
 0.0203717790604077, 0.0181926910596145,
 0.0162295932432360, 0.0144630941418387,
 0.0128754341866765, 0.0114503303359322,
 0.0101728378150057, 0.00902922680011186,
 0.00800687218838523, 0.00709415486039758,
 0.00628037306181464, 0.00555566271730628,
 // 4
 0.00491092564776083, 0.00433777522517762,
 0.00382858617381395, 0.00337652538864193,
 0.00297547478958152, 0.00261995369508530,
 0.00230505051439257, 0.00202636249613307,
 0.00177994246481535, 0.00156225163688919,
 0.00137011774112811, 0.00120069777918906,
 0.00105144485543239, 0.000920078583646128,
 0.000804558644792605, 0.000703061126353299,
 0.000613957321970095, 0.000535794711233811,
 0.000467279874773688, 0.000407263130174890,
 // 5
 0.000354724700456040, 0.000308762228684552,
 0.000268578998820779, 0.000233472107922766,
 0.000202821534805516, 0.000176080503619378,
 0.000152766994802780, 0.000132456257345164,
 0.000114774196621564, 0.0000993915292610416,
 0.0000860186111205116, 0.0000744008568854185,
 0.0000643146804615109, 0.0000555638944463892,
 0.0000479765148133912, 0.0000414019237006278,
 0.0000357083490382522, 0.0000307806248038908,
 0.0000265182000840266, 0.0000228333689341654,
 // 6
 0.0000196496963539553, 0.0000169006186225834,
 0.0000145282003166539, 0.0000124820385512393,
 0.0000107183044508680, 9.19890566611241e-6,
 7.89075437420041e-6, 6.76512728089460e-6,
 5.79710594495074e-6, 4.96508729255373e-6,
 4.25035551717139e-6, 3.63670770345000e-6,
 3.11012649979137e-6, 2.65849401629250e-6,
 2.27134186228307e-6, 1.93963287719169e-6,
 1.65557066379923e-6, 1.41243351587104e-6,
 1.20442975270958e-6, 1.02657183986121e-6,
 // 7
 8.74566995329392e-7, 7.44722260394541e-7
};

// Dickman's "rho" function; rho(x) == F(1/x)
double rho(double x) {
  if (x <= 1) { return 1; }
  if (x < 2)  { return 1 - log(x); }
  x -= 2;
  ASSERTG (x >= 0);
  x *= 20;
  int pos = (int) x;
  if (pos + 1 >= sizeof(rhotab)/sizeof(rhotab[0])) { return 0; }
  
  // linear interpolation between rhotab[pos] and rhotab[pos+1]
  return rhotab[pos] + (x - pos) * (rhotab[pos + 1] - rhotab[pos]);
}

// Integrate a function from "a" to "b".
double integral(double a, double b, double caller_arg, double (*f)(double, double)) {
  int	STEPS = 20;
  double w = b - a;
  ASSERTG(w >= 0);
  if (w == 0) { return 0; }
  double step = w / STEPS;
  double sum = 0;
  double x;

  for (x = a + step * .5; x < b; x += step) { sum += (*f)(x, caller_arg); }
  return sum * step;
}

// Dickman's "F" function.
double F(double x) {
  ASSERTG(x >= 0);
  if (x <= 0) { return 0; }
  return rho(1/x);
}

// See "Asympotic Semismoothness Probabilities", E. Bach, R. Peralta, page 5.
// https://www.researchgate.net/publication/220576644_Asymptotic_semismoothness_probabilities
double G_helper_function (double t, double G_a) { return F(G_a / (1 - t)) / t; }
double G(double a, double b) { return F(a) + integral(a, b, a, &G_helper_function); }

// See "Some Integer Factorization Algorithms using Elliptic Curves", R. P. Brent, page 3.
// https://maths-people.anu.edu.au/~brent/pd/rpb102.pdf
double miu_helper_function (double t, double miu_a) { return rho(t) / (miu_a - t); }
double miu(double a, double b) { return rho(a) + integral(a - b, a - 1, a, &miu_helper_function); }

// Returns the probability of PM1(B1,B2) success for a finding a smooth factor using B1, B2 and already TFed to factoredUpTo.
// Caller must account for any smoothness bits one gets for free.  For example, Mersenne have special form 2*k*p+1 for M(p)
// so set takeAwayBits to log2(exponent) + 1 to obtain the magnitude of the "k" part.
double pm1prob(double takeAwayBits, double factoredUpTo, uint64_t B1, uint64_t B2) {

  // We split the bit-range starting from "factoredUpTo" up in slices each SLICE_WIDTH bits wide.
  const double SLICE_WIDTH = 0.25;

  // The middle point of the slice is (2^n + 2^(n+SLICE_WIDTH))/2,
  // so log2(middle) is n + log2(1 + 2^SLICE_WIDTH) - 1.
  const double MIDDLE_SHIFT = log2(1 + exp2(SLICE_WIDTH)) - 1;
  
  // The bit-size of a representative factor from the current slice.
  double bitsFactor = factoredUpTo + MIDDLE_SHIFT - takeAwayBits;

  double bitsB1 = log2((double)B1);
  double bitsB2 = log2((double)B2);
  
  double alpha = bitsFactor / bitsB1;
  double beta  = bitsB2 / bitsB1;
  
  // When the per-slice probability increment gets below EPSILON we ignore the remaining slices as insignificant.
  const double EPSILON = 1e-7;

  double sum = 0;

  double p, nSlice;
  for (p = 1, nSlice = factoredUpTo / SLICE_WIDTH + 0.5; p >= EPSILON; alpha += SLICE_WIDTH / bitsB1, nSlice += 1) {
    double pm1Prob = miu(alpha, beta);

    // The probability of "at least one" factor in the slice is p = SLICE_WIDTH/(n + SLICE_WIDTH).
    // Mapping it back through the Exponential Distribution's CDF to get the expected number of factors in that interval,
    // nFactors = -log(1-p).
    // double levelRate = log1p(SLICE_WIDTH / n);
    // Approximated through log(1-p) ~= -1/(1/p - 0.5); log(1+p) ~= 1/(1/p + 0.5) for small p.
    // Here s==SLICE_WIDTH, n=factoredUpTo: -log(1-s/(n+s))=-log(n/(n+s))=log(1+s/n)~=1/(n/s+0.5); (see "nSlice" above)
    // double levelRate = 1 / nSlice;
    
    p = pm1Prob / nSlice;

    // Normally we'd add probabilities using the rule below:
    // Probability of [independent] "A or B" is: p(A or B) == p(A) + p(B) - p(A)*p(B), i.e. sum += p * (1 - sum);
    // But we don't need to do that because we already mapped the probabilities to rates above.
    sum += p;
  }
  
  // Map back rate to probability (Exponential Distribution's CDF)
  return -expm1(-sum);
}
