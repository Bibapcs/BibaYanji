/**************************************************************
 *
 *	ecm.cpp
 *
 *	ECM, P-1, and P+1 factoring routines
 *
 *	Original author:  Richard Crandall - www.perfsci.com
 *	Adapted to Mersenne numbers and optimized by George Woltman
 *	Further optimizations from Paul Zimmerman's GMP-ECM program
 *	Other important ideas courtesy of Peter Montgomery, Alex Kruppa, Mihai Preda, Pavel Atnashev.
 *
 *	c. 1997 Perfectly Scientific, Inc.
 *	c. 1998-2024 Mersenne Research, Inc.
 *	All Rights Reserved.
 *
 *************************************************************/

/* Includes */

#include "common.h"		// Included in all GIMPS sources
#include "commonb.h"
#include "commonc.h"
#include "ecm.h"
#include "exponentiate.h"
#include "pair.h"
#include "pm1prob.h"
#include "polymult.h"
#include "primenet.h"
#include <iterator>
#include <map>

/* Include much of resume.c from GMP_ECM sources */

#include "resume_gmp.c"

/* Global variables */

int	QA_IN_PROGRESS = FALSE;
int	QA_TYPE = 0;
giant	QA_FACTOR = NULL;
int	PRAC_SEARCH = 7;

/* C++14 feature we want to use, but we only require C++11 */

namespace local {
template< class Iterator >
std::reverse_iterator<Iterator> make_reverse_iterator(Iterator i)
{
    return std::reverse_iterator<Iterator>(i);
}
}

/* Forward declarations for structures used to track a Weierstrass, Edwards, and Montgomery ECM point -- needed for Dmultiple class and ecm handle */

struct ws {
	gwnum	x;		/* x or FFT(x) */
	gwnum	y;		/* y or FFT(y) */
	gwnum	z;		/* z or FFT(z) */
};

struct ed {
	gwnum	x;		/* x or FFT(x) */
	gwnum	y;		/* y or FFT(y) */
	gwnum	z;		/* z or FFT(z), if NULL implies z = 1 */
	gwnum	t;		/* Extended coordinates, see "Twisted Edwards Curves Revisited" by Huseyin Hisil, et.al. */
	bool	alternate_format; /* Coordinates are in an alternate format (see ed_funky_dbl) */
};

struct xz {
	gwnum	x;		/* x or FFT(x) */
	gwnum	z;		/* z or FFT(z) */
};

/**********************************************************************************************************************/
/*                                Manage sets of relative primes to D/2                                               */
/**********************************************************************************************************************/

/* Experimentally derived sets of D/2 relative primes.  Balancing good pairing % with faster initialization times. */
/* Data came from analyzing pairing percentage for B1=700000, B2=23100000, D=210, density = 0.29. */

/* This set of data requires some intermediate calculations during initialization. */
// The first value is the number of extra full sets, the second value is the number of extra partial sets.  The final values are the extra sets.

// For now we are not using these relp sets that require intermediate calculations during init.  Someday we should try incorporating them.
#ifdef BETTER_RELP_SETS
int16_t relp_set_hard4[]  = {4,1,1,  0,1,4,10  ,2,7};			// 69.89%
int16_t relp_set_hard4a[] = {4,0,1,  0,1,3,9   ,6};			// 69.88%
//int16_t relp_set_hard4b[] = {4,1,0, 0,1,4,9  ,2};			// 69.88%
//int16_t relp_set_hard4c[] = {4,1,1, 0,1,4,16 ,2,8};			// 69.87%
int16_t relp_set_hard5[]  = {5,1,1,  0,1,4,9,36   ,2,18};		// 77.00%
//int16_t relp_set_hard5a[] = {5,2,1, 0,1,4,10,25  ,2,6,12};		// 76.99%
//int16_t relp_set_hard5b[] = {5,2,1, 0,1,4,10,28  ,2,7,14};		// 76.97%
//int16_t relp_set_hard5c[] = {5,2,1, 0,1,4,16,64  ,2,8,32};		// 76.96%
//int16_t relp_set_hard5d[] = {5,1,1, 0,1,4,9,25   ,2,17};		// 76.95%
int16_t relp_set_hard5e[] = {5,0,1,  0,1,3,7,28   ,14};			// 76.89%
int16_t relp_set_hard6[]  = {6,2,1,  0,1,3,9,21,84  ,6,12,42};		// 82.89%
//int16_t relp_set_hard6a[] = {xxx,  0,1,4,9,36,84};			// 82.87%
//int16_t relp_set_hard6b[] = {xxx,  0,1,3,10,28,73};			// 82.87%
int16_t relp_set_hard6c[] = {6,1,1,  0,1,3,7,21,63  ,14,42};		// 82.72%
int16_t relp_set_hard6d[] = {6,0,1,  0,1,3,7,15,63  ,31};		// 82.64%
int16_t relp_set_hard7[]  = {7,4,1,  0,1,3,9,21,84,49   ,6,7,14,42,28};	// 87.70%
//int16_t relp_set_hard7a[]  = {xxx, 0,1,4,9,36,84,109};		// 87.70%
//int16_t relp_set_hard7b[] = {xxx,  0,1,3,9,21,84,129};		// 87.68%
int16_t relp_set_hard7c[] = {7,0,1,  0,1,3,7,15,31,127  ,63};		// 87.50%
//int16_t relp_set_hard8[] = {xxx,   0,1,3,9,21,84,49,129};		// 91.58%
//int16_t relp_set_hard8a[] = {xxx,  0,1,3,9,21,84,49,109};		// 91.51%
//int16_t relp_set_hard9[] = {xxx,   0,1,3,7,15,31,63,127,90};		// 94.26%
// This "typo" easy9 produced better results sometimes, like 1% better for B1=10000, B2=500000, buffers=2160
//int16_t relp_set_NOTeasy9[]  = {9,0,0,  0,1,3,7,15,31,47,63,27};
#endif

//GW I'm getting occasional reports of crashes on big pairing jobs (https://www.mersenneforum.org/showpost.php?p=643362&postcount=511).
//GW Polymult has pretty much made large pairing jobs irrelevant, so I'm capping the max relp sets at 12 in hopes that eliminates crashes.
#define MAX_RELP_SETS	12

/* This set of data requires no intermediate calculations (except for discarding the usable set -1) during initialization. */
/* That is, every D/2 set can be calculated be adding a multiple-of-D to a previous D/2 set, where the difference is also a previous D/2 set. */
/* NOTE: We might eek out a little more performance by using "negative sets".  Try building on 0,1,-3 = 61.13%.  If we do explore negative sets, */
/* beware that there are optimizations dealing with relocs near B2_start and B2_end in pair.cpp that assumes sets are predominately positive. */

int16_t relp_set_easy2[]  = {2,0,0,  0,-1};								// 33.16% 49.57%
int16_t relp_set_easy3[]  = {3,0,0,  0,1,-3};								// 61.13%
int16_t relp_set_easy4[]  = {4,0,0,  0,1,3,7};								// 69.69%
int16_t relp_set_easy5[]  = {5,0,0,  0,1,3,7,15};							// 76.74%
int16_t relp_set_easy6[]  = {6,0,0,  0,1,3,7,15,31};							// 82.51%
int16_t relp_set_easy7[]  = {7,0,0,  0,1,3,7,15,31,63};							// 87.40%
int16_t relp_set_easy8[]  = {8,0,0,  0,1,3,7,15,31,63,127};						// 91.43%
int16_t relp_set_easy9[]  = {9,0,0,  0,1,3,7,15,31,63,127,47};						// 93.13%
int16_t relp_set_easy10[] = {10,0,0,  0,1,3,6,13,27,55,111,223,51};					// 96.04%
int16_t relp_set_easy11[] = {11,0,0,  0,1,3,6,13,27,48,55,111,223,5};					// 97.36%
int16_t relp_set_easy12[] = {12,0,0,  0,1,3,5,6,13,27,48,55,111,223,96};				// 98.17%
int16_t relp_set_easy13[] = {13,0,0,  0,1,3,5,6,13,21,27,48,55,111,223,219};				// 98.70%
int16_t relp_set_easy14[] = {14,0,0,  0,1,3,5,6,13,21,27,48,55,111,219,223,209};			// 99.03%
int16_t relp_set_easy15[] = {15,0,0,  0,1,3,5,6,13,21,27,48,55,111,219,223,209,83};			// 99.25%
int16_t relp_set_easy16[] = {16,0,0,  0,1,3,5,6,13,21,27,48,55,83,111,209,219,223,201};			// 99.39%
int16_t relp_set_easy17[] = {17,0,0,  0,1,3,5,6,13,21,27,48,55,83,111,201,209,219,223,216};		// 99.48%
int16_t relp_set_easy18[] = {18,0,0,  0,1,3,5,6,13,21,27,48,55,83,111,201,209,216,219,223,231};		// 99.55%
int16_t relp_set_easy19[] = {19,0,0,  0,1,3,5,6,13,21,27,48,55,83,111,201,209,216,219,223,231,20};	// 99.59%
int16_t relp_set_easy20[] = {20,0,0,  0,1,3,5,6,13,20,21,27,48,55,83,111,201,209,216,219,223,231,222};	// 99.62%

/* Return a relp_set that will work with the given multiplier */
/* Over time we may enhance this to return different relp_sets based on the bounds and/or cost of computing intermediate sets. */

int16_t *relp_set_selection (int L) {
	if (L > MAX_RELP_SETS) L = MAX_RELP_SETS;
	if (L <= 2) return relp_set_easy2;
	if (L <= 3) return relp_set_easy3;
	if (L <= 4) return relp_set_easy4;
	if (L <= 5) return relp_set_easy5;
	if (L <= 6) return relp_set_easy6;
	if (L <= 7) return relp_set_easy7;
	if (L <= 8) return relp_set_easy8;
	if (L <= 9) return relp_set_easy9;
	if (L <= 10) return relp_set_easy10;
	if (L <= 11) return relp_set_easy11;
	if (L <= 12) return relp_set_easy12;
	if (L <= 13) return relp_set_easy13;
	if (L <= 14) return relp_set_easy14;
	if (L <= 15) return relp_set_easy15;
	if (L <= 16) return relp_set_easy16;
	if (L <= 17) return relp_set_easy17;
	if (L <= 18) return relp_set_easy18;
	if (L <= 19) return relp_set_easy19;
	return relp_set_easy20;
}

/* Return the maximum relp_set from the array of relp_sets */

int16_t get_max_relp_set (
	int16_t *relp_sets)
{
	int16_t	num_permanent_sets = *relp_sets;
	int16_t max_set;
	for (int16_t i = 0; i < num_permanent_sets; i++) {
		if (i == 0 || max_set < relp_sets[3 + i]) max_set = relp_sets[3 + i];
	}
	return (max_set);
}

// Data stored for each relative prime set to compute.  These are stored in a map sorted by relp set number
class relp_set_data {
public:
	// Constructor	
	relp_set_data (int16_t b, bool c, bool d)
		{ last_full_used_by = last_partial_used_by = b; nQx_store = base = Dmultiple = diff = 0; permanent = c; partial = d; }
	// Determine if this is the last use of a value in calculating relp data
	bool is_last_use (int16_t relp_set, int i, int num_partials, int numrels, bool inverted_access_pattern) {
		// If the relp_set being calculated matches the last-partial-usage of this set, then this is the last use
		if (relp_set == last_partial_used_by) return (TRUE);
		// If the relp_set being calculated matches neither the last-partial-usage or last-full-usage of this set, then this is not the last use
		if (relp_set != last_full_used_by) return (FALSE);
		// See if inverted access patterns cancel out
		inverted_access_pattern ^= last_partial_use_inverted;
		// Return TRUE if not accessing one of the relps that are needed in the last-partial-usage
		return ((!inverted_access_pattern && i >= num_partials) || (inverted_access_pattern && i < numrels - num_partials));
	}
	// Data
	int16_t		nQx_store;		// Where this relp_set is stored in the nQx array
	int16_t		last_full_used_by;	// The largest full relp_set that used this relp_set as base or diff
	int16_t		last_partial_used_by;	// The largest partial relp_set that used this relp_set as base or diff
	int16_t		base;			// Set will be computed by base + Dmultiple and diff
	int16_t		Dmultiple;		// Set will be computed by base + Dmultiple and diff
	int16_t		diff;			// Set will be computed by base + Dmultiple and diff
	bool		permanent;		// TRUE if this relp_set is used in creating prime pairings
	bool		partial;		// TRUE if only part of the relp_set must be calculated
	bool		last_partial_use_inverted; // TRUE if the last partial usage is in reverse order
};
// Custom sort order so that relp_sets are ordered 0,-1,1,-2,...  This mimics the order in which the sets need to be computed.
struct relpclasscomp {
	bool operator() (const int16_t &lhs, const int16_t &rhs) const {
		return (abs(lhs) < abs(rhs) || (abs(lhs) == abs(rhs) && lhs < rhs));
	}
};
using relp_set_data_map = std::map<int16_t,relp_set_data,relpclasscomp>;

// Data stored for each multiple-of-D used in calculating relp_sets.  These are stored in a sorted map.
class Dmultiple_data {
public:
	// Constructor
	Dmultiple_data (int16_t rlub, uint64_t dlub) {val.x = NULL; val.z = NULL; relp_last_used_by = rlub; Dmult_last_used_by = dlub; base = addin = diff = 0; }
	// Destructor
	~Dmultiple_data () { if (!do_not_free_val) { gwfree (gwdata, val.x); gwfree (gwdata, val.z); } }
	// Set val buffers
	void set_val_buffers (gwhandle *gw, gwnum x, bool do_not_free_flag) { gwdata = gw; val.x = x; val.z = NULL; do_not_free_val = do_not_free_flag; }
	void set_val_buffers (gwhandle *gw, gwnum x, gwnum z, bool do_not_free_flag) { gwdata = gw; val.x = x; val.z = z; do_not_free_val = do_not_free_flag; }
	// Update Dmult_last_used_by
	void set_Dmult_last_used_by (uint64_t x) { if (Dmult_last_used_by < x) Dmult_last_used_by = x; }
	// Free value if Dmult_last_used_by satisfied
	void free_if_Dmult_last_used_by (uint64_t x) {
		if (do_not_free_val) return;
		if (relp_last_used_by == 0 && Dmult_last_used_by == x) {
			gwfree (gwdata, val.x);
			gwfree (gwdata, val.z);
			val.x = val.z = NULL;
		}
	}
	// Return TRUE if relp_last_used_by is satisfied
	bool will_free_due_to_relp_last_used_by (int16_t x) {
		if (do_not_free_val) return (FALSE);
		return (relp_last_used_by == x);
	}
	// Free value if relp_last_used_by satisfied
	void free_if_relp_last_used_by (int16_t x) {
		if (will_free_due_to_relp_last_used_by (x)) {
			gwfree (gwdata, val.x);
			gwfree (gwdata, val.z);
			val.x = val.z = NULL;
		}
	}
	// Data
	int16_t		relp_last_used_by;	// The largest relp_set that needs this Dmultiple.  Zero if not needed by any relp_sets.
	uint64_t	Dmult_last_used_by;	// The largest Dmultiple calculation that needs this Dmultiple
	uint64_t	base;			// Dmultiple will be computed by base + addin and diff
	uint64_t	addin;			// Dmultiple will be computed by base + Dmultiple and diff
	uint64_t	diff;			// Dmultiple will be computed by base + Dmultiple and diff
	struct xz	val;			// Ptr to multiple-of-D value stored as a struct xz for ECM (P-1/P+1 stores the value in val.x)
	gwhandle	*gwdata;		// Handle used to free val's gwnums
	bool		do_not_free_val;	// Flag to prohibit freeing val's gwnums
};
using Dmultiple_data_map = std::map<uint64_t,Dmultiple_data>;

// Populate the relp_set_data and Dmultiple_data structures based on the relp_set that has been selected

void process_relp_sets (
	int	totrels,
	int	numrels,
	int16_t *relp_sets,
	relp_set_data_map &relp_set_map,
	Dmultiple_data_map &Dmultiple_map)
{
	int16_t	num_permanent_sets = *relp_sets++;
	int16_t num_intermediate_full_sets = *relp_sets++;
	int16_t num_intermediate_parital_sets = *relp_sets++;
	int16_t num_sets = num_permanent_sets + num_intermediate_full_sets + num_intermediate_parital_sets;

	// Create an entry for each relp_set plus the special -1 relp_set which is always calculated.
	for (int16_t i = 0; i < num_sets + 1; i++) {
		bool	permanent = (i < num_permanent_sets);
		bool	partial = (i == num_permanent_sets - 1) || (i >= num_sets - num_intermediate_parital_sets && i < num_sets);
		int16_t	set;

		// Get the relp_set #, the last iteration through this loop processes the special -1 relp_set
		if (i == num_sets) { if (relp_set_map.find(-1) != relp_set_map.end()) continue; set = -1; }
		else set = relp_sets[i];

		// Add relp_set to the map
		relp_set_map.insert ({set, {set, permanent, partial}});
	}

	// Loop through relp_sets to decide where each set is stored in the nQx array.  The nQx array indexes must match the ordering used by
	// fill_pairmap in pair.cpp.  The pairing routine sorts the relative primes.
	int	relps_in_partial_set = one_based_modulo (totrels, numrels);
	int	permanent_nQx_index = 0;
	int	temporary_nQx_index = (num_permanent_sets - 1) * numrels + relps_in_partial_set;
	for (auto this_set = relp_set_map.begin(); this_set != relp_set_map.end(); ++this_set) {
		if (this_set->second.permanent) {
			this_set->second.nQx_store = permanent_nQx_index;
			permanent_nQx_index += this_set->second.partial ? relps_in_partial_set : numrels;
		} else {
			this_set->second.nQx_store = temporary_nQx_index;
			temporary_nQx_index += this_set->second.partial ? relps_in_partial_set : numrels;
		}
	}

	// Now loop through relp_sets calculating how each will be constructed
	for (auto this_set = relp_set_map.begin(); this_set != relp_set_map.end(); ++this_set) {
		// Relp sets 0 and -1 are precomputed
		if (this_set->first == 0 || this_set->first == -1) continue;
		// Scan through smaller sets looking for a base set and diff set that lets us create this set using a Dmultiple and Lucas additions
		for (auto base_set = local::make_reverse_iterator(this_set); ; ++base_set) {		// Can use std::make_reverse_iterator in C++14
			// This relp_set and base relp_set must have the same sign
			if (((this_set->first < 0) ^ (base_set->first < 0)) == 1) continue;
			// Check if a diff_set exists such that base_set can be used to calculate this_set
			int16_t	Dmultiple = abs(this_set->first) - abs(base_set->first);
			int16_t diff = (this_set->first > 0) ? base_set->first - Dmultiple : base_set->first + Dmultiple;
			auto diff_set = relp_set_map.find(diff);
			if (diff_set == relp_set_map.end()) continue;
			// Don't build a full relp set from a partial relp set
			if (!this_set->second.partial && (base_set->second.partial || diff_set->second.partial)) continue;
			// Don't build a negative partial relp set from a partial diff relp set.  Reverse indexing into the partial diff set will not work.
			if (this_set->first < 0 && this_set->second.partial && diff_set->second.partial) continue;
			// We've found our Lucas addition, remember it
			this_set->second.base = base_set->first;
			this_set->second.Dmultiple = Dmultiple;
			this_set->second.diff = diff;

			// Base set is never accessed in inverted order
			base_set->second.last_partial_used_by = this_set->first;
			base_set->second.last_partial_use_inverted = FALSE;
			if (!this_set->second.partial) base_set->second.last_full_used_by = this_set->first;

			// Diff set might be accessed in inverted order
			diff_set->second.last_partial_used_by = this_set->first;
			diff_set->second.last_partial_use_inverted = (this_set->first < 0) ^ (diff_set->first < 0);
			if (!this_set->second.partial) diff_set->second.last_full_used_by = this_set->first;

			// Update or create Dmultiple data
			auto Dfind = Dmultiple_map.find(Dmultiple);
			if (Dfind != Dmultiple_map.end()) Dfind->second.relp_last_used_by = this_set->first;
			else Dmultiple_map.insert ({(uint64_t) Dmultiple, {this_set->first, (uint64_t) Dmultiple}});
			break;
		}
	}
}

// Process the Dmultiple_data_map, finding best way to calculate the needed D-multiples

void process_Dmult_map (
	Dmultiple_data_map &Dmultiple_map)
{

// Determine how all the multiples-of-D will be calculated

	for (auto this_Dmult = Dmultiple_map.rbegin(); this_Dmult != Dmultiple_map.rend(); ++this_Dmult) {
		if (this_Dmult->first == 1) break;

		// Create a default Lucas addition solution in case no better plan is found
		uint64_t proposed_base, proposed_addin, proposed_diff, create_1, create_2;
		if ((this_Dmult->first & 1) == 0) {	// Even multiple of D
			proposed_base = this_Dmult->first / 2;
			proposed_addin = this_Dmult->first / 2;
			proposed_diff = 0;
			create_1 = proposed_base;
			create_2 = 0;
		} else {				// Odd multiple of D
			proposed_base = this_Dmult->first / 2 + 1;
			proposed_addin = this_Dmult->first / 2;
			proposed_diff = 1;
			create_1 = proposed_base;
			create_2 = proposed_addin;
		}

		// Scan through smaller D-multiples looking for a base, addin, and diff that lets us compute this D-multiple using Lucas addition
		// We scan in the forward direction to find a doubling solution first (ell_dbl is cheaper than ell_add).
		for (auto base_Dmult = Dmultiple_map.lower_bound((this_Dmult->first+1)/2); base_Dmult->first < this_Dmult->first; base_Dmult++) {
			// Computed the needed addin and diff
			uint64_t addin = this_Dmult->first - base_Dmult->first;
			uint64_t diff = base_Dmult->first - addin;
			bool	addin_exists = (Dmultiple_map.find(addin) != Dmultiple_map.end());
			bool	diff_exists = (diff == 0 || Dmultiple_map.find(diff) != Dmultiple_map.end());
			// If we've found a suitable Lucas addition, remember it
			if (addin_exists && diff_exists) {
				proposed_base = base_Dmult->first;
				proposed_addin = addin;
				proposed_diff = diff;
				create_1 = create_2 = 0;
				break;
			}
			// If we have addin, remember diff as a possible route to computing this_Dmult
			if (addin_exists && diff < create_1) {
				proposed_base = base_Dmult->first;
				proposed_addin = addin;
				proposed_diff = diff;
				create_1 = diff;
				create_2 = 0;
			}
			// If we have diff, remember addin as a possible route to computing this_Dmult
			if (diff_exists && addin < create_1) {
				proposed_base = base_Dmult->first;
				proposed_addin = addin;
				proposed_diff = diff;
				create_1 = addin;
				create_2 = 0;
			}
		}
		// Record the best proposed solution
		this_Dmult->second.base = proposed_base;
		this_Dmult->second.addin = proposed_addin;
		this_Dmult->second.diff = proposed_diff;
		if (create_1) Dmultiple_map.insert ({create_1, {0, create_1}});		// Create new D-multiple not used by any relp set
		if (create_2) Dmultiple_map.insert ({create_2, {0, create_2}});		// Create new D-multiple not used by any relp set
		// Maintain Dmult_last_used_by
		Dmultiple_map.find (proposed_base)->second.set_Dmult_last_used_by (this_Dmult->first);
		if (proposed_diff != 0) {
			Dmultiple_map.find (proposed_addin)->second.set_Dmult_last_used_by (this_Dmult->first);
			Dmultiple_map.find (proposed_diff)->second.set_Dmult_last_used_by (this_Dmult->first);
		}
	}
}

/**********************************************************************************************************************/
/*                                ECM, P-1, and P+1 best stage 2 implementation routines                              */
/**********************************************************************************************************************/

/* Various D values that we will consider in creating an ECM, P-1 or P+1 plan */
/* Selecting the best D value is tricky.  There are (at least) 5 different factors at play) */
/* 1) The more primes in D, the higher the density of primes among the remaining relative primes, which helps pairing */
/* 2) The higher D is the less cost there is moving from D-section to D-section */
/* 3) For a given amount of memory, a higher D means lower multiplier (totrels/numrels) which hurts pairing */
/* 4) A lower first missing prime, means a higher B2 start which translates to fewer D sections (great for ECM) and more relocatables for better pairing */
/* 5) A lower second missing prime means more relocatables with multiple relocations, better for pairing */

struct D_data {
	int	D;
	int	numrels;			// Number of values less than D/2 that are relatively prime to D
	int	first_missing_prime;		// First prime that does not divide D
	int	second_missing_prime;		// Second prime that does not divide D
} D_data[] = {
	{ 2 * 3 * 5                  , 2 * 4 / 2, 7, 11 },			// 30, 4
	{ 2 * 3     * 7              , 2 * 6 / 2, 5, 11 },			// 42, 6
	{ 2 * 3 * 5               * 2, 2 * 4 / 2 * 2, 7, 11 },			// 60, 8
	{ 2 * 3 * 5               * 3, 2 * 4 / 2 * 3, 7, 11 },			// 90, 12
	{ 2 * 3 * 5               * 4, 2 * 4 / 2 * 4, 7, 11 },			// 120, 16
	{ 2 * 3     * 7           * 3, 2 * 6 / 2 * 3, 5, 11 },			// 126, 18
	{ 2 * 3 * 5               * 5, 2 * 4 / 2 * 5, 7, 11 },			// 150, 20
	{ 2 * 3 * 5 * 7              , 2 * 4 * 6 / 2, 11, 13 },			// 210, 24
	{ 2 * 3 * 5     * 11         , 2 * 4 * 10 / 2, 7, 13 },			// 330, 40
	{ 2 * 3     * 7           * 7, 2 * 6 / 2 * 7, 5, 11 },			// 294, 42
	{ 2 * 3 * 5 * 7           * 2, 2 * 4 * 6 / 2 * 2, 11, 13 },		// 420, 48
	{ 2 * 3     * 7 * 11         , 2 * 6 * 10 / 2, 5, 13 },			// 462, 60
	{ 2 * 3 * 5 * 7           * 3, 2 * 4 * 6 / 2 * 3, 11, 13 },		// 630, 72
	{ 2 * 3     * 7           *14, 2 * 6 / 2 * 14, 5, 11 },			// 588, 84
	{ 2 * 3 * 5 * 7           * 4, 2 * 4 * 6 / 2 * 4, 11, 13 },		// 840, 96
	{ 2 * 3     * 7           *16, 2 * 6 / 2 * 16, 5, 11 },			// 672, 96
	{ 2 * 3     * 7           *18, 2 * 6 / 2 * 18, 5, 11 },			// 756, 108
	{ 2 * 3 * 5 * 7           * 5, 2 * 4 * 6 / 2 * 5, 11, 13 },		// 1050, 120
	{ 2 * 3     * 7 * 11      * 2, 2 * 6 * 10 / 2 * 2, 5, 13 },		// 924, 120
	{ 2 * 3     * 7           *21, 2 * 6 / 2 * 21, 5, 11 },			// 882, 126
	{ 2 * 3 * 5 * 7           * 6, 2 * 4 * 6 / 2 * 6, 11, 13 },		// 1260, 144
	{ 2 * 3 * 5 * 7           * 7, 2 * 4 * 6 / 2 * 7, 11, 13 },		// 1470, 168
	{ 2 * 3     * 7 * 11      * 3, 2 * 6 * 10 / 2 * 3, 5, 13 },		// 1386, 180
	{ 2 * 3 * 5 * 7           * 8, 2 * 4 * 6 / 2 * 8, 11, 13 },		// 1680, 192
	{ 2 * 3 * 5 * 7           * 9, 2 * 4 * 6 / 2 * 9, 11, 13 },		// 1890, 216
	{ 2 * 3     * 7 * 11      * 4, 2 * 6 * 10 / 2 * 4, 5, 13 },		// 1848, 240
	{ 2 * 3 * 5 * 7 * 11         , 2 * 4 * 6 * 10 / 2, 13, 17 },		// 2310, 240
	{ 2 * 3 * 5 * 7      * 13    , 2 * 4 * 6 * 12 / 2, 11, 17 },		// 2730, 288
	{ 2 * 3     * 7 * 11      * 6, 2 * 6 * 10 / 2 * 6, 5, 13 },		// 2772, 360
	{ 2 * 3 * 5 * 7 * 11      * 2, 2 * 4 * 6 * 10 / 2 * 2, 13, 17 },	// 4620, 480
	{ 2 * 3 * 5     * 11 * 13    , 2 * 4 * 10 * 12 / 2, 7, 17 },		// 4290, 480
	{ 2 * 3 * 5 * 7 * 11      * 3, 2 * 4 * 6 * 10 / 2 * 3, 13, 17 },	// 6930, 720
	{ 2 * 3     * 7 * 11 * 13    , 2 * 6 * 10 * 12 / 2, 5, 17 },		// 6006, 720
	{ 2 * 3 * 5 * 7 * 11      * 4, 2 * 4 * 6 * 10 / 2 * 4, 13, 17 }		// 9240, 960
};
#define NUM_D		(sizeof(D_data)/sizeof(struct D_data))
#define MAX_D		9240
#define MAX_RELPRIMES	960

#ifdef ALTERNATE_D_DATA_FOR_POLYS_BUILT_FROM_TABLE_BELOW
struct D_data poly_D_data[] = {
	{ 2 * 3                   * 9, 1 *9, 5, 7 },			// 54, 9
	{ 2 * 3                   *11, 10  , 5, 7 },			// 66, 10
	{ 2 * 3                   *13, 12  , 5, 7 },			// 78, 12
	{ 2 * 3                   *17, 16  , 5, 7 },			// 102, 16
	{ 2 * 3                   *19, 18  , 5, 7 },			// 114, 18
	{ 2 * 3 * 5                  , 4         , 7, 11 },		// 30, 4
	{ 2 * 3 * 5               * 3, 4       *3, 7, 11 },		// 90, 12
	{ 2 * 3 * 5               * 5, 4       *5, 7, 11 },		// 150, 20
	{ 2 * 3 * 5               * 9, 4       *9, 7, 11 },		// 270, 36
	{ 2 * 3 * 5               *13, 4 * 12    , 7, 11 },		// 390, 48
	{ 2 * 3 * 5               *15, 4      *15, 7, 11 },		// 450, 60
	{ 2 * 3 * 5               *17, 4 * 16    , 7, 11 },		// 510, 64
	{ 2 * 3 * 5               *19, 4 * 18    , 7, 11 },		// 570, 72
	{ 2 * 3     * 7              , 6        , 5, 11 },		// 42, 6
	{ 2 * 3     * 7           * 3, 6      *3, 5, 11 },		// 126, 18
	{ 2 * 3     * 7           * 7, 6      *7, 5, 11 },		// 294, 42
	{ 2 * 3     * 7           * 9, 6      *9, 5, 11 },		// 378, 54
	{ 2 * 3     * 7           *13, 6 * 12   , 5, 11 },		// 546, 72
	{ 2 * 3     * 7           *17, 6 * 16   , 5, 11 },		// 714, 96
	{ 2 * 3     * 7           *19, 6 * 18   , 5, 11 },		// 798, 108
	{ 2 * 3 * 5 * 7              , 4 * 6         , 11, 13 },	// 210, 24
	{ 2 * 3 * 5 * 7           * 3, 4 * 6       *3, 11, 13 },	// 630, 72
	{ 2 * 3 * 5 * 7           * 5, 4 * 6       *5, 11, 13 },	// 1050, 120
	{ 2 * 3 * 5 * 7           * 7, 4 * 6       *7, 11, 13 },	// 1470, 168
	{ 2 * 3 * 5 * 7           * 9, 4 * 6       *9, 11, 13 },	// 1890, 216
	{ 2 * 3 * 5 * 7           *15, 4 * 6      *15, 11, 13 },	// 3150, 360
	{ 2 * 3 * 5 * 7           *17, 4 * 6 * 16    , 11, 13 },	// 3570, 384
	{ 2 * 3 * 5 * 7           *19, 4 * 6 * 18    , 11, 13 },	// 3990, 432
	{ 2 * 3     * 7 * 11         , 6 * 10        , 5, 13 },		// 462, 60
	{ 2 * 3     * 7 * 11       *3, 6 * 10      *3, 5, 13 },		// 1386, 180
	{ 2 * 3     * 7 * 11       *7, 6 * 10      *7, 5, 13 },		// 3234, 420
	{ 2 * 3     * 7 * 11       *9, 6 * 10      *9, 5, 13 },		// 4158, 540
	{ 2 * 3     * 7 * 11      *11, 6 * 10     *11, 5, 13 },		// 5082, 660
	{ 2 * 3     * 7 * 11      *17, 6 * 10 * 16   , 5, 13 },		// 7854, 960
	{ 2 * 3     * 7 * 11      *19, 6 * 10 * 18   , 5, 13 },		// 8778, 1080
	{ 2 * 3 * 5     * 11         , 4 * 10        , 7, 13 },		// 330, 40
	{ 2 * 3 * 5     * 11       *3, 4 * 10      *3, 7, 13 },		// 990, 120
	{ 2 * 3 * 5     * 11       *5, 4 * 10      *5, 7, 13 },		// 1650, 200
	{ 2 * 3 * 5     * 11       *9, 4 * 10      *9, 7, 13 },		// 2970, 360
	{ 2 * 3 * 5     * 11      *11, 4 * 10     *11, 7, 13 },		// 3630, 440
	{ 2 * 3 * 5     * 11      *15, 4 * 10     *15, 7, 13 },		// 4950, 600
	{ 2 * 3 * 5     * 11      *17, 4 * 10 * 16   , 7, 13 },		// 5610, 640
	{ 2 * 3 * 5     * 11      *19, 4 * 10 * 18   , 7, 13 },		// 6270, 720
	{ 2 * 3 * 5 * 7      * 13     , 4 * 6 * 12        , 11, 17 },	// 2730, 288
	{ 2 * 3 * 5 * 7      * 13   *3, 4 * 6 * 12      *3, 11, 17 },	// 8190, 864
	{ 2 * 3 * 5 * 7      * 13   *5, 4 * 6 * 12      *5, 11, 17 },	// 13650, 1440
	{ 2 * 3 * 5 * 7      * 13   *7, 4 * 6 * 12      *7, 11, 17 },	// 19110, 2016
	{ 2 * 3 * 5 * 7      * 13   *9, 4 * 6 * 12      *9, 11, 17 },	// 24570, 2592
	{ 2 * 3 * 5 * 7      * 13  *13, 4 * 6 * 12     *13, 11, 17 },	// 35490, 3744
	{ 2 * 3 * 5 * 7      * 13  *19, 4 * 6 * 12 * 18   , 11, 17 },	// 51870, 5184
	{ 2 * 3 * 5     * 11 * 13     , 4 * 10 * 12        , 7, 17 },	// 4290, 480
	{ 2 * 3 * 5     * 11 * 13   *3, 4 * 10 * 12      *3, 7, 17 },	// 12870, 1440
	{ 2 * 3 * 5     * 11 * 13   *5, 4 * 10 * 12      *5, 7, 17 },	// 21450, 2400
	{ 2 * 3 * 5     * 11 * 13   *9, 4 * 10 * 12      *9, 7, 17 },	// 38610, 4320
	{ 2 * 3 * 5     * 11 * 13  *11, 4 * 10 * 12     *11, 7, 17 },	// 47190, 5280
	{ 2 * 3 * 5     * 11 * 13  *13, 4 * 10 * 12     *13, 7, 17 },	// 55770, 6240
	{ 2 * 3 * 5     * 11 * 13  *15, 4 * 10 * 12     *15, 7, 17 },	// 64350, 7200
	{ 2 * 3 * 5     * 11 * 13  *19, 4 * 10 * 12 * 18   , 7, 17 },	// 81510, 8640
	{ 2 * 3 * 5 * 7 * 11          , 4 * 6 * 10        , 13, 17 },	// 2310, 240
	{ 2 * 3 * 5 * 7 * 11        *3, 4 * 6 * 10      *3, 13, 17 },	// 6930, 720
	{ 2 * 3 * 5 * 7 * 11        *5, 4 * 6 * 10      *5, 13, 17 },	// 11550, 1200
	{ 2 * 3 * 5 * 7 * 11        *7, 4 * 6 * 10      *7, 13, 17 },	// 16170, 1680
	{ 2 * 3 * 5 * 7 * 11        *9, 4 * 6 * 10      *9, 13, 17 },	// 20790, 2160
	{ 2 * 3 * 5 * 7 * 11       *11, 4 * 6 * 10     *11, 13, 17 },	// 25410, 2640
	{ 2 * 3 * 5 * 7 * 11       *15, 4 * 6 * 10     *15, 13, 17 },	// 34650, 3600
	{ 2 * 3 * 5 * 7 * 11       *19, 4 * 6 * 10 * 18   , 13, 17 },	// 43890, 4320
	{ 2 * 3 * 5 * 7 * 11 * 13     , 4 * 6 * 10 * 12   , 17, 19 },	// 30030, 2880
	{ 2 * 3 * 5 * 7 * 11 * 13   *3, 4 * 6 * 10 * 12 *3, 17, 19 },	// 90090, 8640
	{ 2 * 3 * 5 * 7 * 11 * 13   *5, 4 * 6 * 10 * 12 *5, 17, 19 },	// 150150, 14400
	{ 2 * 3 * 5 * 7 * 11 * 13   *7, 4 * 6 * 10 * 12 *7, 17, 19 },	// 210210, 20160
	{ 2 * 3 * 5 * 7 * 11 * 13   *9, 4 * 6 * 10 * 12 *9, 17, 19 },	// 270270, 25920
	{ 2 * 3 * 5 * 7 * 11 * 13  *11, 4 * 6 * 10 * 12*11, 17, 19 },	// 330330, 31680
	{ 2 * 3 * 5 * 7 * 11 * 13  *13, 4 * 6 * 10 * 12*13, 17, 19 },	// 390390, 37440
	{ 2 * 3 * 5 * 7 * 11 * 13  *15, 4 * 6 * 10 * 12*15, 17, 19 },	// 450450, 43200
	{ 2 * 3 * 5 * 7 * 11 * 13  *19, 4 * 6 * 10 * 12*18, 17, 23 },	// 570570, 51840
	{ 2 * 3 * 5 * 7 * 11 * 13  *21, 4 * 6 * 10 * 12*21, 17, 19 },	// 630630, 60480
	{ 2 * 3 * 5 * 7 * 11 * 13  *23, 4 * 6 * 10 * 12*22, 17, 19 },	// 690690, 63360
	{ 2 * 3 * 5 * 7 * 11 * 13  *25, 4 * 6 * 10 * 12*25, 17, 19 },	// 750750, 72000
	{ 2 * 3 * 5 * 7 * 11 * 13  *27, 4 * 6 * 10 * 12*27, 17, 19 },	// 810810, 77760
	{ 2 * 3 * 5 * 7 * 11 * 13  *29, 4 * 6 * 10 * 12*28, 17, 19 },	// 870870, 80640
	{ 2 * 3 * 5 * 7 * 11 * 13  *31, 4 * 6 * 10 * 12*30, 17, 19 },	// 930930, 86400
	{ 2 * 3 * 5 * 7 * 11 * 13  *37, 4 * 6 * 10 * 12*36, 17, 19 },	// 1111110, 103680
	{ 2 * 3 * 5 * 7 * 11 * 13  *41, 4 * 6 * 10 * 12*40, 17, 19 },	// 1231230, 115200
	{ 2 * 3 * 5 * 7 * 11 * 13  *43, 4 * 6 * 10 * 12*42, 17, 19 },	// 1291290, 120960
	{ 2 * 3 * 5 * 7 * 11 * 13  *47, 4 * 6 * 10 * 12*46, 17, 19 },	// 1411410, 132480
	{ 2 * 3 * 5 * 7 * 11 * 13  *53, 4 * 6 * 10 * 12*52, 17, 19 },	// 1591590, 149760
	{ 2 * 3 * 5 * 7 * 11 * 13  *59, 4 * 6 * 10 * 12*58, 17, 19 },	// 1771770, 167040
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17     , 4 * 6 * 10 * 12 * 16   , 19, 23 },	// 510510, 46080
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *3, 4 * 6 * 10 * 12 * 16 *3, 19, 23 },	// 1531530, 138240
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *5, 4 * 6 * 10 * 12 * 16 *5, 19, 23 },	// 2552550, 230400
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *7, 4 * 6 * 10 * 12 * 16 *7, 19, 23 },	// 3573570, 322560
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *9, 4 * 6 * 10 * 12 * 16 *9, 19, 23 },	// 4594590, 414720
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *11, 4 * 6 * 10 * 12 * 16*11, 19, 23 },	// 5615610, 506880
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *13, 4 * 6 * 10 * 12 * 16*13, 19, 23 },	// 6636630, 599040
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *15, 4 * 6 * 10 * 12 * 16*15, 19, 23 },	// 7657650, 691200
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *17, 4 * 6 * 10 * 12 * 16*17, 19, 23 },	// 8678670, 783360
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *25, 4 * 6 * 10 * 12 * 16*25, 19, 23 },	// 12762750, 1152000
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *35, 4 * 6 * 10 * 12 * 16*35, 19, 23 },	// 17867850, 1612800
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *45, 4 * 6 * 10 * 12 * 16*45, 19, 23 },	// 22972950, 2073600
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19     , 4 * 6 * 10 * 12 * 16 * 18   , 23, 29 }, // 9699690, 829440
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *3, 4 * 6 * 10 * 12 * 16 * 18 *3, 23, 29 }, // 29099070, 2488320
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *5, 4 * 6 * 10 * 12 * 16 * 18 *5, 23, 29 }, // 48498450, 4147200
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *7, 4 * 6 * 10 * 12 * 16 * 18 *7, 23, 29 }, // 67897830, 5806080
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *9, 4 * 6 * 10 * 12 * 16 * 18 *9, 23, 29 }, // 87297210, 7464960
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *11, 4 * 6 * 10 * 12 * 16 * 18*11, 23, 29 }, // 106696590, 9123840
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *13, 4 * 6 * 10 * 12 * 16 * 18*13, 23, 29 }, // 126095970, 10782720
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *15, 4 * 6 * 10 * 12 * 16 * 18*15, 23, 29 }, // 145495350, 12441600
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *17, 4 * 6 * 10 * 12 * 16 * 18*17, 23, 29 }, // 164894730, 14100480
};
#endif

struct D_data poly_D_data[] = {
	{ 2 * 3 * 5                  , 4         , 7, 11 },		// 30, 4
	{ 2 * 3     * 7              , 6        , 5, 11 },		// 42, 6
	{ 2 * 3                   * 9, 1 *9, 5, 7 },			// 54, 9
	{ 2 * 3                   *11, 10  , 5, 7 },			// 66, 10
	{ 2 * 3 * 5               * 3, 4       *3, 7, 11 },		// 90, 12
	{ 2 * 3                   *17, 16  , 5, 7 },			// 102, 16
	{ 2 * 3     * 7           * 3, 6      *3, 5, 11 },		// 126, 18
	{ 2 * 3 * 5               * 5, 4       *5, 7, 11 },		// 150, 20
	{ 2 * 3 * 5 * 7              , 4 * 6         , 11, 13 },	// 210, 24
	{ 2 * 3 * 5               * 9, 4       *9, 7, 11 },		// 270, 36
	{ 2 * 3 * 5     * 11         , 4 * 10        , 7, 13 },		// 330, 40
	{ 2 * 3 * 5               *13, 4 * 12    , 7, 11 },		// 390, 48
	{ 2 * 3     * 7 * 11         , 6 * 10        , 5, 13 },		// 462, 60
	{ 2 * 3 * 5               *17, 4 * 16    , 7, 11 },		// 510, 64
	{ 2 * 3 * 5 * 7           * 3, 4 * 6       *3, 11, 13 },	// 630, 72
	{ 2 * 3     * 7           *17, 6 * 16   , 5, 11 },		// 714, 96
	{ 2 * 3     * 7           *19, 6 * 18   , 5, 11 },		// 798, 108
	{ 2 * 3 * 5 * 7           * 5, 4 * 6       *5, 11, 13 },	// 1050, 120
	{ 2 * 3 * 5 * 7           * 7, 4 * 6       *7, 11, 13 },	// 1470, 168
	{ 2 * 3 * 5     * 11       *5, 4 * 10      *5, 7, 13 },		// 1650, 200
	{ 2 * 3 * 5 * 7           * 9, 4 * 6       *9, 11, 13 },	// 1890, 216
	{ 2 * 3 * 5 * 7 * 11          , 4 * 6 * 10        , 13, 17 },	// 2310, 240
	{ 2 * 3 * 5 * 7      * 13     , 4 * 6 * 12        , 11, 17 },	// 2730, 288
	{ 2 * 3 * 5 * 7           *15, 4 * 6      *15, 11, 13 },	// 3150, 360
	{ 2 * 3 * 5 * 7           *17, 4 * 6 * 16    , 11, 13 },	// 3570, 384
	{ 2 * 3 * 5 * 7           *19, 4 * 6 * 18    , 11, 13 },	// 3990, 432
	{ 2 * 3 * 5     * 11 * 13     , 4 * 10 * 12        , 7, 17 },	// 4290, 480
	{ 2 * 3 * 5     * 11      *15, 4 * 10     *15, 7, 13 },		// 4950, 600
	{ 2 * 3 * 5     * 11      *17, 4 * 10 * 16   , 7, 13 },		// 5610, 640
	{ 2 * 3 * 5 * 7 * 11        *3, 4 * 6 * 10      *3, 13, 17 },	// 6930, 720
	{ 2 * 3 * 5 * 7      * 13   *3, 4 * 6 * 12      *3, 11, 17 },	// 8190, 864
	{ 2 * 3     * 7 * 11      *19, 6 * 10 * 18   , 5, 13 },		// 8778, 1080
	{ 2 * 3 * 5 * 7 * 11        *5, 4 * 6 * 10      *5, 13, 17 },	// 11550, 1200
	{ 2 * 3 * 5 * 7      * 13   *5, 4 * 6 * 12      *5, 11, 17 },	// 13650, 1440
	{ 2 * 3 * 5 * 7 * 11        *7, 4 * 6 * 10      *7, 13, 17 },	// 16170, 1680
	{ 2 * 3 * 5 * 7      * 13   *7, 4 * 6 * 12      *7, 11, 17 },	// 19110, 2016
	{ 2 * 3 * 5 * 7 * 11        *9, 4 * 6 * 10      *9, 13, 17 },	// 20790, 2160
	{ 2 * 3 * 5     * 11 * 13   *5, 4 * 10 * 12      *5, 7, 17 },	// 21450, 2400
	{ 2 * 3 * 5 * 7      * 13   *9, 4 * 6 * 12      *9, 11, 17 },	// 24570, 2592
	{ 2 * 3 * 5 * 7 * 11       *11, 4 * 6 * 10     *11, 13, 17 },	// 25410, 2640
	{ 2 * 3 * 5 * 7 * 11 * 13     , 4 * 6 * 10 * 12   , 17, 19 },	// 30030, 2880
	{ 2 * 3 * 5 * 7 * 11       *15, 4 * 6 * 10     *15, 13, 17 },	// 34650, 3600
	{ 2 * 3 * 5 * 7      * 13  *13, 4 * 6 * 12     *13, 11, 17 },	// 35490, 3744
	{ 2 * 3 * 5 * 7 * 11       *19, 4 * 6 * 10 * 18   , 13, 17 },	// 43890, 4320
	{ 2 * 3 * 5 * 7      * 13  *19, 4 * 6 * 12 * 18   , 11, 17 },	// 51870, 5184
	{ 2 * 3 * 5     * 11 * 13  *13, 4 * 10 * 12     *13, 7, 17 },	// 55770, 6240
	{ 2 * 3 * 5     * 11 * 13  *15, 4 * 10 * 12     *15, 7, 17 },	// 64350, 7200
	{ 2 * 3 * 5 * 7 * 11 * 13   *3, 4 * 6 * 10 * 12 *3, 17, 19 },	// 90090, 8640
	{ 2 * 3 * 5 * 7 * 11 * 13   *5, 4 * 6 * 10 * 12 *5, 17, 19 },	// 150150, 14400
	{ 2 * 3 * 5 * 7 * 11 * 13   *7, 4 * 6 * 10 * 12 *7, 17, 19 },	// 210210, 20160
	{ 2 * 3 * 5 * 7 * 11 * 13   *9, 4 * 6 * 10 * 12 *9, 17, 19 },	// 270270, 25920
	{ 2 * 3 * 5 * 7 * 11 * 13  *11, 4 * 6 * 10 * 12*11, 17, 19 },	// 330330, 31680
	{ 2 * 3 * 5 * 7 * 11 * 13  *13, 4 * 6 * 10 * 12*13, 17, 19 },	// 390390, 37440
	{ 2 * 3 * 5 * 7 * 11 * 13  *15, 4 * 6 * 10 * 12*15, 17, 19 },	// 450450, 43200
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17     , 4 * 6 * 10 * 12 * 16   , 19, 23 },	// 510510, 46080
	{ 2 * 3 * 5 * 7 * 11 * 13  *19, 4 * 6 * 10 * 12*18, 17, 23 },	// 570570, 51840
	{ 2 * 3 * 5 * 7 * 11 * 13  *21, 4 * 6 * 10 * 12*21, 17, 19 },	// 630630, 60480
	{ 2 * 3 * 5 * 7 * 11 * 13  *23, 4 * 6 * 10 * 12*22, 17, 19 },	// 690690, 63360
	{ 2 * 3 * 5 * 7 * 11 * 13  *25, 4 * 6 * 10 * 12*25, 17, 19 },	// 750750, 72000
	{ 2 * 3 * 5 * 7 * 11 * 13  *27, 4 * 6 * 10 * 12*27, 17, 19 },	// 810810, 77760
	{ 2 * 3 * 5 * 7 * 11 * 13  *29, 4 * 6 * 10 * 12*28, 17, 19 },	// 870870, 80640
	{ 2 * 3 * 5 * 7 * 11 * 13  *31, 4 * 6 * 10 * 12*30, 17, 19 },	// 930930, 86400
	{ 2 * 3 * 5 * 7 * 11 * 13  *37, 4 * 6 * 10 * 12*36, 17, 19 },	// 1111110, 103680
	{ 2 * 3 * 5 * 7 * 11 * 13  *41, 4 * 6 * 10 * 12*40, 17, 19 },	// 1231230, 115200
	{ 2 * 3 * 5 * 7 * 11 * 13  *43, 4 * 6 * 10 * 12*42, 17, 19 },	// 1291290, 120960
	{ 2 * 3 * 5 * 7 * 11 * 13  *47, 4 * 6 * 10 * 12*46, 17, 19 },	// 1411410, 132480
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *3, 4 * 6 * 10 * 12 * 16 *3, 19, 23 },	// 1531530, 138240
	{ 2 * 3 * 5 * 7 * 11 * 13  *53, 4 * 6 * 10 * 12*52, 17, 19 },	// 1591590, 149760
	{ 2 * 3 * 5 * 7 * 11 * 13  *59, 4 * 6 * 10 * 12*58, 17, 19 },	// 1771770, 167040
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *5, 4 * 6 * 10 * 12 * 16 *5, 19, 23 },	// 2552550, 230400
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *7, 4 * 6 * 10 * 12 * 16 *7, 19, 23 },	// 3573570, 322560
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17   *9, 4 * 6 * 10 * 12 * 16 *9, 19, 23 },	// 4594590, 414720
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *11, 4 * 6 * 10 * 12 * 16*11, 19, 23 },	// 5615610, 506880
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *13, 4 * 6 * 10 * 12 * 16*13, 19, 23 },	// 6636630, 599040
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *15, 4 * 6 * 10 * 12 * 16*15, 19, 23 },	// 7657650, 691200
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *17, 4 * 6 * 10 * 12 * 16*17, 19, 23 },	// 8678670, 783360
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19     , 4 * 6 * 10 * 12 * 16 * 18   , 23, 29 }, // 9699690, 829440
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *25, 4 * 6 * 10 * 12 * 16*25, 19, 23 },	// 12762750, 1152000
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *35, 4 * 6 * 10 * 12 * 16*35, 19, 23 },	// 17867850, 1612800
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17  *45, 4 * 6 * 10 * 12 * 16*45, 19, 23 },	// 22972950, 2073600
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *3, 4 * 6 * 10 * 12 * 16 * 18 *3, 23, 29 }, // 29099070, 2488320
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *5, 4 * 6 * 10 * 12 * 16 * 18 *5, 23, 29 }, // 48498450, 4147200
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *7, 4 * 6 * 10 * 12 * 16 * 18 *7, 23, 29 }, // 67897830, 5806080
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19   *9, 4 * 6 * 10 * 12 * 16 * 18 *9, 23, 29 }, // 87297210, 7464960
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *11, 4 * 6 * 10 * 12 * 16 * 18*11, 23, 29 }, // 106696590, 9123840
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *13, 4 * 6 * 10 * 12 * 16 * 18*13, 23, 29 }, // 126095970, 10782720
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *15, 4 * 6 * 10 * 12 * 16 * 18*15, 23, 29 }, // 145495350, 12441600
	{ 2 * 3 * 5 * 7 * 11 * 13 * 17 * 19  *17, 4 * 6 * 10 * 12 * 16 * 18*17, 23, 29 }, // 164894730, 14100480
};
#define POLY_NUM_D		(sizeof(poly_D_data)/sizeof(struct D_data))
#define POLY_MAX_D		164894730
#define POLY_MAX_RELPRIMES	14100480

// Map a D value to the D_data index
int map_D_to_index (int D)
{
	for (int i = 0; i < NUM_D; i++) if (D_data[i].D == D) return (i);
	ASSERTG (0);
	return (0);
}
// Map a poly D value to the poly_D_data index
int map_polyD_to_index (int D)
{
	for (int i = 0; i < POLY_NUM_D; i++) if (poly_D_data[i].D == D) return (i);
	ASSERTG (0);
	return (0);
}

// Return minimum number of relprimes for a D (same as number of relative primes less than D/2)
#define map_D_to_numrels(D)	D_data[map_D_to_index(D)].numrels
// Return first_missing_prime for a poly D
#define map_polyD_to_first_missing_prime(D)	poly_D_data[map_polyD_to_index(D)].first_missing_prime

/* Structure used to pass data to and return data from ECM, P-1, P+1 stage 2 costing routines */
/* Originally this was only used for the prime pairing algorithm.  Now it also used to analyze polymult-based algorithms. */

struct common_cost_data {
	/* Data sent to cost function follows */
	gwhandle *gwdata;		/* Gwnum handle (not required) */
	pmhandle *polydata;		/* Polymult handle.  If costing a polymult must be minimally initialized for polymult_mem_required. */
	long	stage1_fftlen;		/* FFT length being used in stage 2 */
	long	stage2_fftlen;		/* FFT length being used in stage 2 */
	double	stage1_cost;		/* Cost (in stage 1 FFTs) of stage 1 */
	double	gcd_cost;		/* Cost (in stage 1 FFTs) of a GCD */
	double	modinv_cost;		/* Cost (in stage 1 FFTs) of a modular inverse */
	double	poly_compression;	/* Expected compression from polymult_preprocess for polynomials that can be compressed. */
	int	stage1_threads;		/* Number of threads used during stage 1 */
	int	stage2_threads;		/* Number of threads used during stage 2 */
	uint64_t numvals;		/* Maximum number of gwnum temporaries that can be allocated for relative prime data, pairmaps, */
					/* ECM pooling.  Additional gwnums needed by ECM/P-1/P+1 are not included in this maximum number. */
	bool	use_poly_D_data;	/* Set to TRUE if selecting from the alternate D_data designed for polymult */
	bool	centers_on_Dmultiple;	/* Set to TRUE if pairing is centered on a multiple of D (as opposed to odd multiple of D/2) */
	int	required_missing;	/* An interrupted polymult stage 2 relocated primes using this small prime.  New D must not be a multiple of this value. */
	uint64_t gap_start;		/* last_relocatable or zero (when pairmaps are split, c_start to gap_start are the remaining relocatables to pair) */
	uint64_t gap_end;		/* a.k.a C_done (when pairmaps are split, gap_end to C are the remaining primes to pair) */
	/* Data returned from cost function follows */
	double	max_pairmap_size;	/* Maximum size of the pairing map */
	int	D;			/* D value for big steps */
	int	totrels;		/* Total number of relative primes used for pairing */
	int	numrels;		/* Number of relative primes less than D / 2 */
	double	multiplier;		/* Totrels / numrels */
	int	first_missing_prime;	/* First missing prime - used to calculate B2_start */
	int16_t *relp_sets;		/* The relp_sets to use in stage 2 */
	uint64_t B1;			/* Stage 1 end */
	uint64_t B2;			/* Stage 2 end */
	uint64_t B2_start;		/* Stage 2 start */
	uint64_t numDsections;		/* Number of D sections to process */
	uint64_t max_pairmap_Dsections;	/* Number of D sections that can fit in a pairing map */
	int	numvals_consumed_by_pairmap; /* Number of gwnums no longer available due to large pairing map */
	double	est_pair_pct;		/* Estimated pairing percentage */
	double	est_numprimes;		/* Expected number of primes in stage 2 */
	double	est_numpairs;		/* Expected number of pairs in stage 2 */
	double	est_numsingles;		/* Expected number of singles in stage 2 */
	double	est_pairing_runtime;	/* Estimated stage 2 pairing cost (expressed as equivalent number of transforms) */
	double	est_init_transforms;	/* Estimated stage 2 init cost (expressed as number of transforms) */
	double	est_stage2_transforms;	/* Estimated stage 2 main loop cost (expressed as number of transforms) */
	double	est_init_polymult;	/* Estimated stage 2 init polymult cost (expressed as number of equivalent gwnum transforms) */
	double	est_stage2_polymult;	/* Estimated stage 2 main loop polymult cost (expressed as number of equivalent gwnum transforms) */
	double	est_stage2_stage1_ratio;/* Estimated stage 2 runtime / stage 1 runtime ratio */
	uint64_t stage2_numvals;	/* Returned total number of gwnum temporaries that will be used in stage 2 */
	double	stage2_cost;		/* Cost (in stage 1 FFTs) of stage 2 */
	double	total_cost;		/* Cost for stage 1, stage 2, and GCD */
	double	efficiency;		/* "Value" of B1/B2 combo vs. total_cost */
};

/* Calculate the GCD and modular inverse cost in terms of number of transforms. */
/* The costs come from the timing code running ECM on M604 and spreadsheeting. */
/* Since GCDs are single-threaded we increase for the costs for multi-threaded runs. */

void init_gcd_costs (
	double	bit_length,		// Bit length of number we are GCDing
	void	*cost_func_data)	// Cost_data structure to initialize
{
	struct common_cost_data *c = (struct common_cost_data *) cost_func_data;
	c->gcd_cost = 320.53 * log (bit_length) - 3302.0;
	if (c->gcd_cost < 100.0) c->gcd_cost = 100.0;
	c->modinv_cost = 570.16 * log (bit_length) - 6188.4;
	if (c->modinv_cost < 1.25 * c->gcd_cost) c->modinv_cost = 1.25 * c->gcd_cost;
	if (c->stage2_threads == 2) c->gcd_cost *= 1.9, c->modinv_cost *= 1.9;
	else if (c->stage2_threads == 3) c->gcd_cost *= 2.7, c->modinv_cost *= 2.7;
	else if (c->stage2_threads >= 4) c->gcd_cost *= 3.2, c->modinv_cost *= 3.2;
}

/* Select the best D value for the given the number of temporary gwnums that can be allocated.  We trade off more D steps vs. better */
/* prime pairing vs. different B2 start points using the ECM, P-1, or P+1 costing function. */
/* Returns the cost.  Cost function can return more information, such as best ECM pooling type. */

double best_stage2_impl_internal (
	uint64_t B,			/* Bound #1 */
	uint64_t C_start,		/* Starting point for bound #2 (usually B1, but can be different for save files and required multiple pairmaps) */
	uint64_t C,			/* Bound #2 */
	uint64_t max_numvals,		/* Number of gwnum temporaries that can be used.  Different than c->numvals when binary searching on numvals. */
	double	(*cost_func)(void *),	/* ECM, P-1 or P+1 costing function */
	void	*cost_func_data)	/* User-supplied data to pass to the costing function */
{
	struct common_cost_data *c = (struct common_cost_data *) cost_func_data;
	uint64_t B2_end;
	float	pairing_percentage, pairing_runtime;
	double	efficiency, best_efficiency;
	int	D, i, best_i, j;

/* Select which D_data to use */

	struct D_data *d_data;		/* Prime pairing or polymult D_data */
	int	num_d;			/* Number of D_data entries */
	if (c->use_poly_D_data) {
		d_data = poly_D_data;
		num_d = POLY_NUM_D;
	} else {
		d_data = D_data;
		num_d = NUM_D;
	}

/* Kludge to make one pass finding the best D value and a second pass to re-call the cost function using the best D. */
/* Re-calling the cost function allows us to pass back any data that P-1, P+1, or ECM may need to save. */

	efficiency = best_efficiency = -1.0;
	for (j = 0; j < 2; j++) {

/* Try various values of D until we find the best one. */

	    for (i = 0; i < num_d; i++) {

/* On second pass only cost the best D from the first pass */

		if (j == 1) {
			if (best_efficiency < 0.0) break;
			i = best_i;
		}

/* Check if this D value would require using too many gwnum temporaries */

		if (d_data[i].numrels > max_numvals) break;

/* We move the smaller primes to a composite value higher up in the B1 to B2 range to improve our pairing chances and */
/* reduce the number of D sections to process.  Calculate B2_start - the first prime that cannot be relocated higher. */

		D = d_data[i].D;
		// Compute values needed by the costing functions
		c->B1 = B;
		c->B2 = C;
		c->D = D;
		c->numrels = d_data[i].numrels;
		c->first_missing_prime = d_data[i].first_missing_prime;
		// Prime pairing "centers" on a multiple of D, thus start and end points are an odd multiple of D/2
		if (c->centers_on_Dmultiple) {
			B2_end = round_up_to_multiple_of (C - D / 2, D) + D / 2;
			c->B2_start = (uint64_t) floor ((double) (B2_end / (double) d_data[i].first_missing_prime));
			if (c->B2_start < C_start) c->B2_start = C_start;
			if (c->gap_end && c->B2_start < c->gap_end) c->B2_start = c->gap_end;
			if (c->B2_start < D / 2) break;		// Protects against next line returning negative result
			c->B2_start = round_down_to_multiple_of (c->B2_start + D / 2, D) - D / 2;
		}
		// P-1 polymult pairing "centers" on odd multiples of D/2, thus start and end points are multiples of D
		else {
			B2_end = round_up_to_multiple_of (C, D);
			c->B2_start = (uint64_t) floor ((double) (B2_end / (double) d_data[i].first_missing_prime));
			if (c->B2_start < C_start) c->B2_start = C_start;
			if (c->gap_end && c->B2_start < c->gap_end) c->B2_start = c->gap_end;
			c->B2_start = round_down_to_multiple_of (c->B2_start, D);
		}
		c->numDsections = (B2_end - c->B2_start) / D;

/* Perform pairing-specific and polymult-specific initializations */

		if (!c->use_poly_D_data) {

/* When pairmaps are split, the last_relocatable set by the first pairmap restricts which D values can now be used */

			if (c->gap_start && c->gap_start * d_data[i].first_missing_prime > B2_end) continue;

/* Estimate our prime pairing percentage and number of D sections that can fit in a pairing map */

			/* We only support relp_sets up to 20 entries */
			c->totrels = (max_numvals > MAX_RELP_SETS * d_data[i].numrels) ? MAX_RELP_SETS * d_data[i].numrels : (int) max_numvals;
			estimate_pairing (d_data[i].second_missing_prime, C_start, C, c->totrels, d_data[i].numrels, &pairing_percentage, &pairing_runtime);
			c->est_pair_pct = pairing_percentage;
			c->est_numpairs = (pairing_percentage * c->est_numprimes) / 2.0;
			c->est_numsingles = c->est_numprimes - c->est_numpairs * 2.0;
			double	est_totpairs = c->est_numpairs + c->est_numsingles;		// Also acts as estimated size of the pairing map
			c->max_pairmap_Dsections = estimate_max_pairmap_Dsections (c->max_pairmap_size, c->numDsections, est_totpairs);
			c->numvals_consumed_by_pairmap = (int)
				((est_totpairs > c->max_pairmap_size ? c->max_pairmap_size : est_totpairs) / (c->stage2_fftlen * sizeof (double)));
			// Catch cases where not enough memory for totrels and the pairing map
			if (c->totrels + c->numvals_consumed_by_pairmap > max_numvals) continue;

/* Estimate our cost of finding all the prime pairs.  These formulas came from running single threaded timings on 12K, 512K, 5760K FFTs on the */
/* same machine that did the pairing runtimes.  Thus, this converts (roughly) pairing runtimes into an equivalent cost in number-of-transforms. */
/* Squaring timings up to 512K:	1.69ms * 2^(log2(FFTsize/512K)*1.0407) */
/* Squaring timings above 512K:	1.69ms * 2^(log2(FFTsize/512K)*1.093) */

			if (c->stage2_fftlen < 512*1024)
				c->est_pairing_runtime = pairing_runtime / (0.00169 * pow(2.0,log2((double)c->stage2_fftlen/(512.0*1024.0))*1.0407)) * 2.0;
			else
				c->est_pairing_runtime = pairing_runtime / (0.00169 * pow(2.0,log2((double)c->stage2_fftlen/(512.0*1024.0))*1.093)) * 2.0;
			// Make some adjustments for multi-threaded FFTs vs. single-threaded pairing
			if (c->stage2_threads == 2) c->est_pairing_runtime *= 1.9;
			else if (c->stage2_threads == 3) c->est_pairing_runtime *= 2.7;
			else if (c->stage2_threads >= 4) c->est_pairing_runtime *= 3.2;

/* Pick the relp_sets to use.  Someday we should expand the options for which relp_sets are tried (don't limit to just the easy sets). */

			c->multiplier = (double) c->totrels / (double) c->numrels;
			c->relp_sets = relp_set_selection ((int) ceil (c->multiplier));
		}

/* When a polymult stage 2 starts, small primes are "moved" to a multiple of first_missing_prime.  When polymult stage 2 resumes, we cannot select a */
/* D value that includes the original first_missing_prime as that would miss some of these moved small primes. */

		else {
			if (c->required_missing && D % c->required_missing == 0) continue;
		}

/* Calculate the cost of this stage 2 plan */

		efficiency = (*cost_func) (cost_func_data);
//{char buf[200];sprintf (buf, "D: %d, totrels/numrels: %.2f, efficiency: %.2f (%.5f%% %.2f %.2f %.2f)\n", D, c->multiplier, efficiency,
// c->est_pair_pct*100.0,c->est_pairing_runtime,c->est_init_transforms,c->est_stage2_transforms); OutputStr(MAIN_THREAD_NUM, buf);}

/* On second pass, break out of loop after costing the best D from the first pass */

		if (j == 1) break;

/* Remember best efficiency and best D */

		if (efficiency > best_efficiency) {
			best_efficiency = efficiency;
			best_i = i;
		}

/* Break out of loop after a couple of failed attempts at improving our best (we're going in the wrong direction) */

		else {
			if (best_efficiency >= 0.0 && i > best_i + 2) break;
		}
	    }
	}

/* Return best efficiency */

	return  (efficiency);
}

/* Binary search for the best number of gwnums to allocate and the best D value to use.  Caller specifies the maximum number of gwnums */
/* that can be allocated.  We trade off more D steps vs. better prime pairing vs. different B2 start points using the ECM, P-1, or P+1 costing function. */
/* Returns the cost.  Cost function can return more information, such as best D value, B2_start, B2_end. */

double best_stage2_impl (
	uint64_t B,			/* Bound #1 */
	uint64_t C_start,		/* Starting point for bound #2 -- usually bound #1 */
	uint64_t gap_start,		/* last_relocatable or zero (when pairmaps are split, c_start to gap_start are the remaining relocatables to pair) */
	uint64_t gap_end,		/* a.k.a C_done (when pairmaps are split, gap_end to C are the remaining primes to pair) */
	uint64_t C,			/* Bound #2 */
	uint64_t numvals,		/* Number of gwnum temporaries that can be used for storing relative prime data (or other uses such as pooling) */
	double	(*cost_func)(void *),	/* ECM, P-1, or P+1 costing function */
	void	*cost_func_data)	/* User-supplied data to pass to the costing function */
{
	struct common_cost_data *c = (struct common_cost_data *) cost_func_data;
	struct best_stage2_cost {
		uint64_t numvals;
		double	efficiency;
	} best[3], midpoint;

/* Sanity check and save the gap parameters */

	if (gap_start > gap_end) gap_start = gap_end = 0;
	ASSERTG (gap_start == 0 || gap_start >= C_start);
	ASSERTG (gap_start == 0 || gap_end < C);
	c->gap_start = (gap_start > C_start ? gap_start : 0);
	c->gap_end = (gap_end > C_start ? gap_end : 0);

/* Return negative efficiency if numvals is less than D=30 needs */

	c->numvals = numvals;
	if (c->numvals < 4) return (-1.0);

/* Determine the maximum pairmap size.  The costing functions may increase the stage 2 setup costs if the pairmap must be split in chunks. */

	if (!c->use_poly_D_data) {
		int max_pairmap_size = IniGetInt (INI_FILE, "MaximumBitArraySize", 250);
		if (max_pairmap_size > 2000) max_pairmap_size = 2000;
		if (max_pairmap_size < 1) max_pairmap_size = 1;
		c->max_pairmap_size = (double) max_pairmap_size * 1000000.0;
		// Don't let the pairmap consume more than half the numvals (this may not be optimal).  Not worth investigating as polymult is the optimal choice.
		if (c->max_pairmap_size > (double) numvals / 2 * c->stage2_fftlen * sizeof (double))
			c->max_pairmap_size = (double) numvals / 2 * c->stage2_fftlen * sizeof (double);
	}

/* Estimate the number of primes between B1 and B2 */

	c->est_numprimes = primes_less_than (C) - primes_less_than (C_start);
	if (gap_start) c->est_numprimes -= primes_less_than (gap_end) - primes_less_than (gap_start);

/* When binary searching on numvals is not needed (with poly stage 2 maximum numvals is always best), then just do one call to best_stage2_impl_internal */

	if (c->use_poly_D_data) return (best_stage2_impl_internal (B, C_start, C, c->numvals, cost_func, cost_func_data));

/* Prepare for a binary search looking for the most efficienct stage 2 implementation varying the number of gwnums available for relative primes data. */

	best[0].numvals = 4;
	best[0].efficiency = best_stage2_impl_internal (B, C_start, C, best[0].numvals, cost_func, cost_func_data);
	if (numvals == 4) return (best[0].efficiency);
	best[2].numvals = c->numvals;
	best[2].efficiency = best_stage2_impl_internal (B, C_start, C, best[2].numvals, cost_func, cost_func_data);
	if (numvals == 5) return (best[2].efficiency);
	best[1].numvals = (c->numvals + 4) / 2;
	best[1].efficiency = best_stage2_impl_internal (B, C_start, C, best[1].numvals, cost_func, cost_func_data);
//for (int i = 200; i < 1685; i++) {
//char buf[200];
//double efficiency = best_stage2_impl_internal (B, C_start, C, i, cost_func, cost_func_data);
//sprintf (buf, "best %d, d: %d, efficiency: %g\n", i, c->D, efficiency);
//writeResults (buf);
//}

/* Now do a binary search */

	while (best[2].numvals - best[0].numvals > 2) {
		// If we've reached a rare localized low efficiency where the midpoint is worse than both endpoints, then pick the half with
		// the highest efficiency endpoint and hope that is the correct decision.
		if (best[1].efficiency < best[0].efficiency && best[1].efficiency < best[2].efficiency) {
			if (best[0].efficiency > best[2].efficiency) best[2].efficiency = best[1].efficiency / 2.0;
			else best[0].efficiency = best[1].efficiency / 2.0;
		}
		// If midpoint is worse than start point OR midpoint equals endpoint, make midpoint the new end point.
		if (best[1].efficiency < best[0].efficiency || best[1].efficiency == best[2].efficiency) {
			ASSERTG (best[1].efficiency >= best[2].efficiency);
			best[2] = best[1];
			best[1].numvals = (best[0].numvals + best[2].numvals) / 2;
			best[1].efficiency = best_stage2_impl_internal (B, C_start, C, best[1].numvals, cost_func, cost_func_data);
		}
		// If midpoint is worse than end point, make midpoint the new start point.
		else if (best[1].efficiency < best[2].efficiency) {
			ASSERTG (best[1].efficiency >= best[0].efficiency);
			best[0] = best[1];
			best[1].numvals = (best[0].numvals + best[2].numvals) / 2;
			best[1].efficiency = best_stage2_impl_internal (B, C_start, C, best[1].numvals, cost_func, cost_func_data);
		}
		// Work on the bigger of the lower section and upper section
		else if (best[1].numvals - best[0].numvals > best[2].numvals - best[1].numvals) {	// Work on lower section
			midpoint.numvals = (best[0].numvals + best[1].numvals) / 2;
			midpoint.efficiency = best_stage2_impl_internal (B, C_start, C, midpoint.numvals, cost_func, cost_func_data);
			if (midpoint.efficiency > best[1].efficiency || best[1].efficiency == best[2].efficiency) {	// Make middle the new end point
				best[2] = best[1];
				best[1] = midpoint;
			} else {						// Create new start point
				best[0] = midpoint;
			}
		} else {							// Work on upper section
			midpoint.numvals = (best[1].numvals + best[2].numvals) / 2;
			midpoint.efficiency = best_stage2_impl_internal (B, C_start, C, midpoint.numvals, cost_func, cost_func_data);
			if (midpoint.efficiency > best[1].efficiency) {		// Make middle the new start point
				best[0] = best[1];
				best[1] = midpoint;
			} else {						// Create new end point
				best[2] = midpoint;
			}
		}
	}

/* Redo the best stage 2 implementation.  This lets the costing function return the best data.  Return best efficiency. */

	if (best[2].efficiency > best[1].efficiency) best[1] = best[2];
//{char buf[200];
//double efficiency = best_stage2_impl_internal (B, C_start, C, best[1].numvals, cost_func, cost_func_data);
//sprintf (buf, "best %d, d: %d, efficiency: %g\n", best[1].numvals, c->D, efficiency);
//writeResults (buf);}
	return (best_stage2_impl_internal (B, C_start, C, best[1].numvals, cost_func, cost_func_data));
}

/**********************************************************************************************************************/
/*                                   ECM, P-1, and P+1 common general utility routines                                */
/**********************************************************************************************************************/

// Return the window size for the prime pairing routine.  This is user-configurable based on the size of the number being worked on.
// When totrels/numrels multiplier is large, non-windowed pairing gets very slow.  Switching off ON_THE_FLY_CAN_PAIR_TO would help.
// We need to carefully study non-windowed timings and optimizations before making non-windowed pairing the default more often.
// 2022-12-14:  Pminus1=1,2,87856183,-1,1000000,100000000 used 2.3GB of memory for non-windowed pairing.  Since pairing will only be
// used in low-memory situations, we will always use windowed pairing.  Testing showed a window size of 100 used ~65MB over and above
// the already allocated 107MB.  A window size of 350 also used ~65MB.  Thus, we'll arbitrarily use a window size of 1000.

int pair_window_size (
	double	bit_length,
	int16_t	*relp_sets)
{
//	bool acceptable_multiplier = (relp_sets[0] <= IniGetInt (INI_FILE, "MaxPairingWindowMultiplier", 8));
//	bool windowed = (!acceptable_multiplier || bit_length < IniGetInt (INI_FILE, "MaxPairingWindowExponent", 80000000));
//	return (windowed ? IniGetInt (INI_FILE, "PairingWindowSize", 1000) : 0);
	return (1000);
}

/* Set N, the number we are trying to factor */

int setN (
	int	thread_num,
	struct work_unit *w,
	giant	*N)		/* k*b^n+c as a giant */
{
	unsigned long bits, p;
	char	buf[2500];

/* Create the binary representation of the number we are factoring */
/* Allocate 5 extra words to handle any possible k value. */

	bits = (unsigned long) (w->n * log2 (w->b));
	*N = allocgiant ((bits >> 5) + 5);
	if (*N == NULL) return (OutOfMemory (thread_num));

/* This special code comes from Serge Batalov */

	if (IniGetInt (INI_FILE, "PhiExtensions", 0) &&
	    w->k == 1.0 && w->b == 2 && w->c == -1) {		/*=== this input means Phi(n,2) with n semiprime ===*/
		unsigned int i,k,q,knownSmallMers[] = {3, 5, 7, 13, 17, 19, 31, 61, 89, 107, 127, 521, 607, 1279, 2203, 2281, 3217,
						       4253, 4423, 9689, 9941, 11213, 19937, 999999999}; /* for now, just the cases where w->n = p * q, and 2^q-1 is prime */
		for (i=0; (q=knownSmallMers[i]) < w->n || q*q <= w->n; i++) if ((w->n%q) == 0) {
			giant tmp = allocgiant ((bits >> 5) + 5);
			if (!tmp) return (OutOfMemory (thread_num));
			ultog (1, tmp);
			ultog (1, *N);
			gshiftleft (w->n-w->n/q, *N);
			for (k=2; k < q; k++) {
				gshiftleft (w->n/q, tmp);
				addg (tmp, *N);
			}
			iaddg (1, *N);
			if (q != w->n/q) {
				ultog (w->b, tmp);
				power (tmp, w->n/q);
				iaddg (w->c, tmp);
				divg (tmp, *N);
			}
			free (tmp);
			/* w->minimum_fftlen = w->n; */ /*=== too late to do this here. Moved before gwsetup() --SB. */
			if (!w->known_factors || !strcmp (w->known_factors, "1")) {
				p = sprintf (buf, "M%lu", w->n/q); if(q != w->n/q) p += sprintf (buf+p, "/M%d", q);
				w->known_factors = (char *) malloc (p+1);
				memcpy (w->known_factors, buf, p+1);
			}
			return (0);
		}
        }

	if (IniGetInt (INI_FILE, "PhiExtensions", 0) &&
	    w->k == 1.0 && labs(w->c) == 1 && (w->n%3) == 0) {		/*=== this input means Phi(3,-b^(n/3)) ===*/
		giant	tmp = allocgiant ((bits >> 5) + 5);
		if (tmp == NULL) return (OutOfMemory (thread_num));
		ultog (w->b, tmp);
		power (tmp, w->n/3);
		gtog (tmp, *N);
		squareg (*N);
		if (w->c == 1) subg (tmp, *N); else addg (tmp, *N);
		iaddg (1, *N);
		free (tmp);
		/* w->minimum_fftlen = w->n; */ /*=== too late to do this here. Moved before gwsetup() --SB. */
		if (!w->known_factors) {
			p = sprintf (buf, "(%lu^%lu%+ld)", w->b, w->n/3, w->c);
			w->known_factors = (char *) malloc (p+1);
			memcpy (w->known_factors, buf, p+1);
		}
		return (0);
	}

/* Standard code for working on k*b^n+c */

	ultog (w->b, *N);
	power (*N, w->n);
	dblmulg (w->k, *N);
	iaddg (w->c, *N);

/* If we have a list of known factors then process it.  Use GMP library - much better at division than giants. */

	if (w->known_factors != NULL) {
		mpz_t	__N, __f, __r;

/* Init GMP variables, convert N to mpz format */

		mpz_init (__N);
		mpz_init (__f);
		mpz_init (__r);
		gtompz (*N, __N);

/* Process each factor */

		for (char *p = w->known_factors; ; ) {
			char	*comma, facstr[500];
			int	res;

/* Get the factor - raise error if it is less than or equal to one */

			comma = strchr (p, ',');
			if (comma != NULL) *comma = 0;
			strcpy (facstr, p);
			if (comma != NULL) *comma = ',';
			res = mpz_set_str (__f, facstr, 10);
			if (res < 0 || mpz_cmp_ui (__f, 1) <= 0) {
				char	kbnc[80];
				gw_as_string (kbnc, w->k, w->b, w->n, w->c);
				sprintf (buf, "Error parsing known factors of %s near: '%s'\n", kbnc, facstr);
				OutputStr (thread_num, buf);
				mpz_clear (__N);
				mpz_clear (__f);
				mpz_clear (__r);
				deleteWorkToDoLine (thread_num, w, FALSE);
				return (STOP_ABORT);
			}

/* Divide N by factor - then verify the factor */

			mpz_fdiv_qr (__N, __r, __N, __f);	// N = N / f, remainder r
			if (mpz_sgn (__r) != 0) {		// Is remainder non-zero
				char	kbnc[80];
				gw_as_string (kbnc, w->k, w->b, w->n, w->c);
				sprintf (buf, "%s does not divide %s\n", facstr, kbnc);
				OutputBoth (thread_num, buf);
				mpz_clear (__N);
				mpz_clear (__f);
				mpz_clear (__r);
				deleteWorkToDoLine (thread_num, w, FALSE);
				return (STOP_ABORT);
			}

/* Skip to next factor in list */

			if (comma == NULL) break;
			p = comma + 1;
		}

/* Convert N back to a giant, cleanup */

		mpztog (__N, *N);
		mpz_clear (__N);
		mpz_clear (__f);
		mpz_clear (__r);
	}

/* Return success */

	return (0);
}

/* Test if factor divides N, return TRUE if it does */

int testFactor (
	gwhandle *gwdata,
	struct work_unit *w,
	giant	f)		/* Factor to test */
{
	giant	tmp;
	int	divides_ok;

/* See if this is a valid factor */

	tmp = popg (&gwdata->gdata, f->sign + 5);	/* Allow room for mul by KARG */
	itog (w->b, tmp);
	powermod (tmp, w->n, f);
	dblmulg (w->k, tmp);
	iaddg (w->c, tmp);
	modgi (&gwdata->gdata, f, tmp);
	divides_ok = isZero (tmp);
	pushg (&gwdata->gdata, 1);
	if (!divides_ok) return (FALSE);

/* If QAing, see if we found the expected factor */

	if (QA_IN_PROGRESS) {
		tmp = popg (&gwdata->gdata, f->sign + 5);
		gtog (f, tmp);
		modg (QA_FACTOR, tmp);
		divides_ok = isZero (tmp);
		pushg (&gwdata->gdata, 1);
		if (!divides_ok) {
			char	buf[200];
			strcpy (buf, "ERROR: Factor not found. Expected ");
			gtoc (QA_FACTOR, buf+strlen(buf), 150);
			strcat (buf, "\n");
			OutputBoth (MAIN_THREAD_NUM, buf);
		}
	}

/* All done, return success */

	return (TRUE);
}

/* Add a factor to to the known factors list */

char *addNewKnownFactor (
	struct work_unit *w,		/* ECM, P-1, or P+1 work unit that found a factor */
	const char *fac)		/* Newly found factor (or NULL to simply copy known factors) */
{
	char	*new_known_factors;
	if (fac == NULL) {		/* Simply copy known factors (ECM with ContinueECM option set needs this) */
		new_known_factors = (char *) malloc (strlen (w->known_factors) + 1);
		strcpy (new_known_factors, w->known_factors);
	} else if (w->known_factors == NULL) {
		new_known_factors = (char *) malloc (strlen (fac) + 1);
		strcpy (new_known_factors, fac);
	} else {
		new_known_factors = (char *) malloc (strlen (w->known_factors) + 1 + strlen (fac) + 1);
		sprintf (new_known_factors, "%s,%s", w->known_factors, fac);
	}
	return (new_known_factors);
}

/* Generate a PRP-CF work unit after a new factor is found */

void generatePRPWorkUnit (
	struct work_unit *oldw,		/* ECM, P-1, or P+1 work unit that found a factor */
	const char *fac,		/* Newly found factor, or NULL if factor has already been added to known factors list */
	struct work_unit *neww)		/* Generated PRP work unit */
{
	// Copy work unit, much of the data will be the same
	*neww = *oldw;
	// Change fields that need changing
	neww->work_type = WORK_PRP;
	neww->assignment_uid[0] = 0;
	neww->prp_base = 3;
	neww->prp_residue_type = 5;
	neww->prp_dblchk = FALSE;
	neww->sieve_depth = 99;
	neww->tests_saved = 0;
	neww->known_factors = addNewKnownFactor (oldw, fac);
	neww->stage[0] = 0;
	neww->pct_complete = 0.0;
	neww->ra_failed = !IniGetInt (INI_FILE, "RegisterPrpAssignment", 0);		// By default do not register assignment
}

/* Do a GCD of the input value and N to see if a factor was found. */
/* The GCD is returned in factor iff a factor is found. */
/* This routine used to be interruptible and thus returns a stop_reason. */
/* Since switching to GMP's mpz code to implement the GCD this routine is no longer interruptible. */

int gcd (
	int	thread_num,
	giant	gg,
	giant	N,		/* Number we are factoring */
	giant	*factor)	/* Factor found if any */
{
	mpz_t	a, b;

/* Assume a factor will not be found */

	*factor = NULL;

/* Do the GCD */

	mpz_init (a);
	mpz_init (b);
	gtompz (gg, a);
	gtompz (N, b);
	mpz_gcd (a, a, b);

/* If a factor was found, save it in FAC */

	if (mpz_cmp_ui (a, 1) && mpz_cmp (a, b)) {
		*factor = allocgiant ((int) divide_rounding_up (mpz_sizeinbase (a, 2), 32));
		if (*factor == NULL) goto oom;
		mpztog (a, *factor);
	}

/* Cleanup and return */

	mpz_clear (a);
	mpz_clear (b);
	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (thread_num));
}

int gcd (
	gwhandle *gwdata,
	int	thread_num,
	gwnum	gg,
	giant	N,		/* Number we are factoring */
	giant	*factor)	/* Factor found if any */
{
	giant	v;
	int	rc;

/* Convert input number to binary */

	v = popg (&gwdata->gdata, ((int) gwdata->bit_length >> 5) + 10);
	if (v == NULL) goto oom;
	gwunfft (gwdata, gg, gg);		// Just in case caller partially FFTed gg
	if (gwtogiant (gwdata, gg, v)) {	// On unexpected error, return no factor found
		pushg (&gwdata->gdata, 1);
		return (0);
	}

/* Do the GCD */

	rc = gcd (thread_num, v, N, factor);
	pushg (&gwdata->gdata, 1);
	return (rc);

/* Out of memory exit path */

oom:	return (OutOfMemory (thread_num));
}

/* Test if N is a probable prime.  Compute i^(N-1) mod N for i = 3,5,7 */

int isProbablePrime (
	gwhandle *gwdata,
	giant	N)
{
	int	i, j, len, retval;
	gwnum	t1, t2;
	giant	x;

	if (isone (N)) return (TRUE);

	retval = TRUE;		/* Assume it is a probable prime */
	t1 = gwalloc (gwdata);
	len = bitlen (N);
	for (i = 3; retval && i <= 7; i += 2) {
		t2 = gwalloc (gwdata);
		dbltogw (gwdata, (double) 1.0, t1);
		dbltogw (gwdata, (double) i, t2);
		gwfft (gwdata, t2, t2);
		for (j = 1; j <= len; j++) {
			gwsquare2 (gwdata, t1, t1, 0);
			if (bitval (N, len-j)) gwmul3 (gwdata, t2, t1, t1, 0);
		}
		gwfree (gwdata, t2);
		x = popg (&gwdata->gdata, ((int) gwdata->bit_length >> 5) + 10);
		if (gwtogiant (gwdata, t1, x)) retval = FALSE;	/* Technically, prime status is unknown on an unexpected error */
		else {
			modgi (&gwdata->gdata, N, x);
			iaddg (-i, x);
			if (!isZero (x)) retval = FALSE;	/* Not a prime */
		}
		pushg (&gwdata->gdata, 1);
	}
	gwfree (gwdata, t1);
	return (retval);
}

// From Alex Kruppa, master of all things ECM, the following formula computes the value of a curve when using B2 values that are not 100 * B1.
// curve_worth = 0.11 + 0.89 * (log10(B2 / B1) / 2) ^ 1.5
// B2 = B1 * 10 ^ ((((curve_worth - 0.11) / 0.89) ^ (1 / 1.5)) * 2)
// 2023:  Adjusting formula based on research done with gmp-ecm 6.04 on various B1/B2 values
// power = 1.96617 - 0.06781 * log10(B1)
// curve_worth = 0.11343 + 0.88657 * (log10(B2 / B1) / 2) ^ power

#define kruppa_power(B1)		(1.96617 - 0.06781 * _log10((double)(B1)))
#define kruppa_adjust(B2,B1)		(0.11343 + 0.88657 * pow (_log10((double)(B2) / (double)(B1)) / 2.0, kruppa_power (B1)))
#define kruppa_unadjust(worth,B1)	(uint64_t) round((B1) * pow (10.0, pow (((worth) - 0.11343) / 0.88657, 1.0 / kruppa_power (B1)) * 2.0))

/* When a pairmap completes, we know that all relocatable primes that could be relocated to that pairmap are processed. */
/* Calculate the new first prime that needs relocating. */

uint64_t calc_new_first_relocatable (
	int	D,			/* Calculated by best_stage2_impl, best D value ("big step") */
	uint64_t C_done)		/* Pairmaps have been completed to this point */
{
	int	D_index = map_D_to_index (D);
	return (divide_rounding_up (C_done, D_data[D_index].first_missing_prime));
}

/*************************************************/
/* ECM structures and setup/termination routines */
/*************************************************/

/* Data maintained during ECM process */

#define POOL_3MULT		1	/* Modinv algorithm that takes 3 multiplies (9 FFTs) using 100% more gwnums */
#define POOL_3POINT44MULT	2	/* Modinv algorithm that takes 3.444 multiplies (10.333 FFTs) using 33% more gwnums */
#define POOL_3POINT57MULT	3	/* Modinv algorithm that takes 3.573 multiplies (10.714 FFTs) using 14% more gwnums */
#define POOL_N_SQUARED		4	/* Use O(N^2) multiplies modinv algorithm using no extra gwnums */

#define ECM_STATE_STAGE1_INIT	0	/* Selecting sigma for curve */
#define ECM_STATE_STAGE1	1	/* In middle of stage 1 */
#define ECM_STATE_MIDSTAGE	2	/* Stage 2 initialization for the first time */
#define ECM_STATE_STAGE2	3	/* In middle of stage 2 (processing a pairmap) */
#define ECM_STATE_GCD		4	/* Stage 2 GCD */

#define ECM_STAGE2_PAIRING	0	/* Old fashioned prime pairing stage 2 */
#define ECM_STAGE2_POLYMULT	1	/* FFT/polymult stage 2 */

typedef struct {
	gwhandle gwdata;	/* GWNUM handle */
	int	thread_num;	/* Worker number */
	int	stage1_threads;	/* Number of threads to use in stage 1 */
	int	stage2_threads;	/* Number of threads to use in stage 2 */
	struct work_unit *w;	/* Worktodo.txt entry */
	FILE	*gmp_ecm_file;	/* File handle when running stage 2 on GMP-ECM's stage 1 */
	uint32_t curve;		/* Curve # starting with 1 */
	uint32_t state;		/* Curve state defined above */
	uint64_t sigma;		/* Sigma for the current curve */
	uint64_t B;		/* Bound #1 (a.k.a. B1) */
	uint64_t C;		/* Bound #2 (a.k.a. B2) */
	giant	N;		/* Number being factored */
	char	*N_short_string_rep; /* Short string representation of number being factored */
	char	*N_full_string_rep; /* Full string representation of number being factored */
	giant	factor;		/* Factor found, if any */
	uint32_t sigma_type;	/* 1 for old-style Suyama sigma (Montgomery curves choose12), 0 for new-style Atkin-Morain Edwards curves, 3 = GMP-ECM-param3 */
	bool	montg_stage1;	/* TRUE for old-style Montgomery stage 1, FALSE for new-style Edward curves */
	bool	optimal_B2;	/* TRUE if we calculate optimal bound #2 given currently available memory.  FALSE for a fixed bound #2. */
	uint64_t average_B2;	/* Average Kruppa-adjusted bound #2 work done on ECM curves thusfar */
	unsigned long stage1_fftlen; /* FFT length used in stage 1 */

	gwnum	Ad4;		/* Pre-computed value used for Montgomery doubling */
	gwnum	ed_a;		/* "a" value in a twisted Edwards curve: ax^2 + y^2 = 1 + dx^2y^2 */
	gwnum	ed_d;		/* "d" value in a non-twisted or twisted Edwards curve (only used by ed_check) */
	gwnum	ed_half;	/* value 1/2 in a non-twisted or twisted Edwards curve (only used for some EXTRA_BITS values) */

	readSaveFileState read_save_file_state;	/* Manage savefile names during reading */
	writeSaveFileState write_save_file_state; /* Manage savefile names during writing */
	void	*sieve_info;	/* Prime number sieve */
	uint64_t stage1_prime;	/* Prime number being processed */

	mpz_t	stage1_exp;	/* Edwards stage 1 exponent chunk (later converted to bit array indicating when to ed_dbl vs. ed_dbl and ed_add) */
	char	*NAF_codes;	/* Edwards stage 1 NAF indexes extracted from exponent chunk and stored in a bit array */
	bool	stage1_exp_initialized;/* Edwards stage 1 exponent initialized with mpz_init */
	uint64_t stage1_start_prime; /* Edwards stage 1 first prime in the current exponent chunk */
	uint32_t stage1_exp_buffer_size; /* Edwards stage 1 buffer size (in bytes) used to build stage 1 exponent chunk */
	uint32_t stage1_bitnum;	/* Edwards stage 1 bit number being processed in the exponent chunk */
	uint32_t NAF_dictionary_size; /* Edwards stage 1 number of gwnums in the NAF dictionary */
	struct ed *NAF_dictionary; /* Edwards stage 1 array of points used in ed_add calls during NAF exponentiation */
	gwarray	NAF_gwnums;	/* Gwnums allocated for the NAF_dictionary. */

	struct xz xz;		/* The stage 1 Montgomery value being computed */
	struct ed dict_start;	/* The stage 1 Edwards value used to build the dictionary */
	struct ed e;		/* The stage 1 Edwards value being computed */
	gwnum	gg;		/* The stage 2 accumulated value */

	int32_t	pool_type;	/* Modinv algorithm type to use */
	uint32_t pool_count;	/* Count values in the modinv pool */
	uint32_t poolz_count;	/* Count values in the modinv poolz */
	gwnum	pool_modinv_value;/* Value we will eventually do a modinv on */
	gwnum	*pool_values;	/* Array of values to normalize */
	gwnum	*poolz_values;	/* Array of z values we are normalizing */
	unsigned long modinv_count; /* Stats - count of modinv calls */

	/* Stage 2 data common to prime pairing and polymult */
	int32_t	stage2_type;	/* Prime pairing or polymult stage 2 */
	uint64_t stage2_numvals;/* Number of gwnums used in stage 2 */
	uint64_t C_done;	/* Stage 2 completed thusfar (updated every D section that is completed) */
	uint32_t D;		/* Stage 2 loop increment */
	uint32_t E;		/* Number of mQx values to pool together into one stage 2 modular inverse */
	uint32_t numrels;	/* Number of relative primes less than D/2 (the number of relative primes in one full relp_set) */
	uint64_t B2_start;	/* Starting point of first D section to be processed in stage 2 (an odd multiple of D/2) */
	uint64_t numDsections;	/* Number of D sections to process in stage 2 */
	uint64_t Dsection;	/* Current D section being processed in stage 2 */
	uint64_t first_relocatable; /* First relocatable prime (same as B1 unless pairmaps must be split or mem change caused a replan) */
	uint64_t last_relocatable; /* Last relocatable prime for filling pairmaps (unless mem change causes a replan) */
	double	est_stage2_stage1_ratio; /* Estimated stage 2 runtime / stage 1 runtime ratio */
	double	pct_mem_to_use;	/* If we get memory allocation errors, we progressively try using less and less. */

	struct xz Qm, Qprevm, QD; /* Values used to calculate successive D values in stage 2 */
	struct xz QD_Eover2;	/* Normalized value used for second and later mQx blocks in two-FFT stage 2 */
	int	Qm_state;	/* For 4-FFT continuation, TRUE if Qm has been returned by mQ_next */
	gwnum	*mQx;		/* Array of calculated D values when modular inverse pooling is active */
	uint32_t mQx_count;	/* Number of values remaining in the mQx array */
	uint32_t mQx_retcount;	/* Next mQx array entry to return */
	giant	Ad4_binary;	/* Pre-computed choose12 value used in doubling converted to binary */
	giant	Qx_binary;	/* The x value of stage 1 result - ready for writing to a save file */
	giant	Qz_binary;	/* The z value of stage 1 result - ready for writing to a save file */
	giant	gg_binary;	/* The stage 2 accmulator converted to binary (during stage 2 init) */

	/* Prime pairing stage 2 data */
	int32_t	TWO_FFT_STAGE2;	/* Type of ECM stage 2 to execute */
	uint32_t totrels;	/* Number relatively prime nQx values used */
	int32_t	relp;		/* Last relative prime processed in the current D section */
	int16_t relp_sets[32];	/* The relp sets we are using in stage 2 */
	gwnum	*nQx;		/* Array of relative primes data used in stage 2 */
	uint64_t max_pairmap_Dsections;	/* Number of D sections that can fit in a pairing map */
	uint8_t	*pairmap;	/* Pairing map for prime pairs and singles in each D section */
	uint64_t pairmap_size;	/* Size of the pairing map */
	uint8_t *pairmap_ptr;	/* Pointer to the next byte to process in the pairing map */

	/* Polymult stage 2 data */
	int32_t	required_missing; /* When B2_start exceeds B1, primes are moved to first_missing_prime * prime.  If a save file is created mid-stage 2 */
				/* then resuming must pick a D value that does not eliminate multiples of first_missing_prime. */ 
	pmhandle polydata;	/* Polymult data structure */
	uint64_t poly_size;	/* Size of F(X), G(X), H(X) polynomials */
	gwarray	polyF;		/* Array of coefficients for poly F(X) */
	gwarray	polyR;		/* Array of coefficients for poly 1/F(X), coefficients need to be negated */
	gwarray	polyGH;		/* Array of coefficients for poly G(X) and H(X) */
	gwarray	polyGaux;	/* Scratch array for use in filling up poly G(X) */
	uint64_t polyGaux_size;	/* Size of the polyGaux array */
	bool	use_preallocated_norm_temps; /* TRUE when using polyGaux for all our normalization temps */
	gwnum	norm_pool_temps[5]; /* Free gwnums for normalization to use.  These come from polyGaux. */
	int	norm_pool_temps_count; /* Number of free gwnums in norm_pool_temps */
	int	Ftree_polys_in_mem; /* Number of Ftree polys to store in memory rather than on disk or rebuilding */
	gwarray polyFtree[40];	/* Saved entries from polyF tree.  Needed by scaled remainders to work down the tree. */
	gwarray	Ftree;		/* Array of gwnums used in Ftree reconstruction */

	/* Helper routine data */
//	int	num_points;	/* Number of points (multiples of D) that can be evalualted with each polymult. */
//	int	helper_count;	/* Total number of threads including main thread doing helper work */
//	int	remaining_poly2_size; /* Number of poly2 coefficients to compute */
//	gwnum	*points;	/* Array of evaluated points for helper routine to accumulate into gg for later GCD */
	int	helper_work;	/* Type of work ecm_helper should perform */
	gwarray poly1;		/* Poly passed to ecm_helper */
} ecmhandle;

/* Forward declarations */

void normalize_pool_term (ecmhandle *);
void mQ_term (ecmhandle *);
void NAF_dictionary_free (ecmhandle *);

/* Perform cleanup functions */

void ecm_mini_cleanup (
	ecmhandle *ecmdata)
{
	normalize_pool_term (ecmdata);
	free (ecmdata->mQx), ecmdata->mQx = NULL;
	free (ecmdata->Ad4_binary), ecmdata->Ad4_binary = NULL;
	free (ecmdata->Qx_binary), ecmdata->Qx_binary = NULL;
	free (ecmdata->Qz_binary), ecmdata->Qz_binary = NULL;
	free (ecmdata->gg_binary), ecmdata->gg_binary = NULL;
	if (ecmdata->stage2_type != ECM_STAGE2_POLYMULT) free (ecmdata->nQx);
	ecmdata->nQx = NULL;
	free (ecmdata->pairmap), ecmdata->pairmap = NULL;
	free (ecmdata->factor), ecmdata->factor = NULL;
	free (ecmdata->Ftree), ecmdata->Ftree = NULL;
	end_sieve (ecmdata->sieve_info), ecmdata->sieve_info = NULL;
	polymult_done (&ecmdata->polydata);
	if (ecmdata->stage1_exp_initialized) mpz_clear (ecmdata->stage1_exp), ecmdata->stage1_exp_initialized = FALSE;
	NAF_dictionary_free (ecmdata);
	gwfreeall (&ecmdata->gwdata);
	memset (&ecmdata->xz, 0, sizeof (ecmdata->xz));
	memset (&ecmdata->e, 0, sizeof (ecmdata->e));
	ecmdata->gg = NULL;
	ecmdata->Ad4 = NULL;
	ecmdata->ed_a = NULL;
	ecmdata->ed_d = NULL;
	ecmdata->ed_half = NULL;
}

void ecm_cleanup (
	ecmhandle *ecmdata)
{
	ecm_mini_cleanup (ecmdata);
	free (ecmdata->N), ecmdata->N = NULL;
	if (ecmdata->N_short_string_rep != gwmodulo_as_string (&ecmdata->gwdata)) free (ecmdata->N_short_string_rep), ecmdata->N_short_string_rep = NULL;
	if (ecmdata->N_full_string_rep != gwmodulo_as_string (&ecmdata->gwdata)) free (ecmdata->N_full_string_rep), ecmdata->N_full_string_rep = NULL;
	gwdone (&ecmdata->gwdata);
	if (ecmdata->gmp_ecm_file != NULL) fclose (ecmdata->gmp_ecm_file), ecmdata->gmp_ecm_file = NULL;
}

/**************************************************************
 *	Weierstrass ECM Functions
 **************************************************************/

/* This routine initializes an ws struct with three allocated gwnums */

bool ws_alloc (			/* Returns TRUE if successful */
	ecmhandle *ecmdata,
	struct ws *arg)
{
	arg->x = gwalloc (&ecmdata->gwdata);
	if (arg->x == NULL) return (FALSE);
	arg->y = gwalloc (&ecmdata->gwdata);
	if (arg->y == NULL) return (FALSE);
	arg->z = gwalloc (&ecmdata->gwdata);
	if (arg->z == NULL) return (FALSE);
	return (TRUE);
}

/* This routine frees a ws struct's gwnums */

void ws_free (
	ecmhandle *ecmdata,
	struct ws *arg)
{
	gwfree (&ecmdata->gwdata, arg->x), arg->x = NULL;
	gwfree (&ecmdata->gwdata, arg->y), arg->y = NULL;
	gwfree (&ecmdata->gwdata, arg->z), arg->z = NULL;
}

/* Verify a point is on the elliptic curve */

void ws_check (
	ecmhandle *ecmdata,
	struct ws *in)
{
#ifdef GDEBUG
	gwnum	t0, t1, t2;
	int	a = -8;
	int	b = -32;

	t0 = gwalloc (&ecmdata->gwdata);
	t1 = gwalloc (&ecmdata->gwdata);
	t2 = gwalloc (&ecmdata->gwdata);

	// Verify Y^2Z = X^3 + aXZ^2 + bZ^3

	gwsquare2 (&ecmdata->gwdata, in->y, t0, GWMUL_PRESERVE_S1);				// t0 = Y * Y
	gwmul3 (&ecmdata->gwdata, t0, in->z, t0, GWMUL_PRESERVE_S2);				// t0 *= Z
	gwsquare2 (&ecmdata->gwdata, in->x, t1, GWMUL_PRESERVE_S1);				// t1 = X * X
	gwmul3 (&ecmdata->gwdata, t1, in->x, t1, GWMUL_PRESERVE_S2);				// t1 *= X
	gwmul3 (&ecmdata->gwdata, in->x, in->z, t2, GWMUL_PRESERVE_S1 | GWMUL_PRESERVE_S2);	// t2 = X * Z
	gwmul3 (&ecmdata->gwdata, t2, in->z, t2, GWMUL_PRESERVE_S2);				// t2 *= Z
	gwsmallmul (&ecmdata->gwdata, a, t2);							// t2 *= a
	gwadd3 (&ecmdata->gwdata, t1, t2, t1);							// t1 = t1 + t2
	gwsquare2 (&ecmdata->gwdata, in->z, t2, GWMUL_PRESERVE_S1);				// t2 = Z * Z
	gwmul3 (&ecmdata->gwdata, t2, in->z, t2, GWMUL_PRESERVE_S2);				// t2 *= Z
	gwsmallmul (&ecmdata->gwdata, b, t2);							// t2 *= b
	gwadd3 (&ecmdata->gwdata, t1, t2, t1);							// t1 = t1 + t2
	gwsub3 (&ecmdata->gwdata, t0, t1, t0);							// t0 = t0 - t1, should be zero

	giant tmp = popg (&ecmdata->gwdata.gdata, ((unsigned long) ecmdata->gwdata.bit_length >> 5) + 5);
	gwtogiant (&ecmdata->gwdata, t0, tmp);
	ASSERTG (tmp->sign == 0);
	pushg (&ecmdata->gwdata.gdata, 1);

	gwfree (&ecmdata->gwdata, t0);
	gwfree (&ecmdata->gwdata, t1);
	gwfree (&ecmdata->gwdata, t2);
#endif
}

/* Add two Weierstrass points */
/* For our purposes, speed is not critical.  This is only used during Atkin-Morain contruction of an Edwards curve. */

void ws_add (
	ecmhandle *ecmdata,
	struct ws *in0,
	struct ws *in1,
	struct ws *out)
{
//	int	a = -8;
//	int	b = -32;

// From https://www.nayuki.io/page/elliptic-curve-point-addition-in-projective-coordinates
// Let T0 = Y0Z1
// Let T1 = Y1Z0
// Let T = T0−T1
// Let U0 = X0Z1
// Let U1 = X1Z0
// Let U = U0−U1
// Let U2 = U^2
// Let V = Z0Z1
// Let W = T^2V − U2(U0+U1)
// Let U3 = UU2
// X2 = UW
// Y2 = T(U0U2−W)−T0U3
// Z2 = U3V

	gwnum	T0, T1, T, U0, U1, U, U2, V, W, t0, U3;
	T0 = gwalloc (&ecmdata->gwdata);
	T1 = gwalloc (&ecmdata->gwdata);
	U0 = gwalloc (&ecmdata->gwdata);
	U1 = gwalloc (&ecmdata->gwdata);
	U = gwalloc (&ecmdata->gwdata);
	U2 = gwalloc (&ecmdata->gwdata);
	V = gwalloc (&ecmdata->gwdata);
	W = gwalloc (&ecmdata->gwdata);
	t0 = gwalloc (&ecmdata->gwdata);

	gwmul3 (&ecmdata->gwdata, in0->y, in1->z, T0, 0);		// T0 = Y0 * Z1
	gwmul3 (&ecmdata->gwdata, in1->y, in0->z, T1, 0);		// T1 = Y1 * Z0
	gwsub3 (&ecmdata->gwdata, T0, T1, T = T1);			// T = T0 - T1		last use of T1

	gwmul3 (&ecmdata->gwdata, in0->x, in1->z, U0, 0);		// U0 = X0 * Z1
	gwmul3 (&ecmdata->gwdata, in1->x, in0->z, U1, 0);		// U1 = X1 * Z0
	gwsub3 (&ecmdata->gwdata, U0, U1, U);				// U = U0 - U1

	gwsquare2 (&ecmdata->gwdata, U, U2, 0);				// U2 = U^2

	gwmul3 (&ecmdata->gwdata, in0->z, in1->z, V, 0);		// V = Z0 * Z1

	gwmul3 (&ecmdata->gwdata, T, T, W, 0);				// W = T^2
	gwmul3 (&ecmdata->gwdata, W, V, W, 0);				// W *= V
	gwadd3 (&ecmdata->gwdata, U0, U1, t0);				// t0 = U0 + U1		last use of U1
	gwmul3 (&ecmdata->gwdata, U2, t0, t0, 0);			// t0 = U2 * t0
	gwsub3 (&ecmdata->gwdata, W, t0, W);				// W -= t0

	gwmul3 (&ecmdata->gwdata, U, U2, U3 = U1, 0);			// U3 = U * U2

	gwmul3 (&ecmdata->gwdata, U, W, out->x, 0);			// X2 = U * W

	gwmul3 (&ecmdata->gwdata, T0, U3, out->y, 0);			// Y2 = T0 * U3
	gwmul3 (&ecmdata->gwdata, U0, U2, t0, 0);			// t0 = U0 * U2
	gwsub3 (&ecmdata->gwdata, t0, W, t0);				// t0 -= W
	gwmul3 (&ecmdata->gwdata, T, t0, t0, 0);			// t0 = T * t0
	gwsub3 (&ecmdata->gwdata, t0, out->y, out->y);			// Y2 = t0 - Y2

	gwmul3 (&ecmdata->gwdata, U3, V, out->z, 0);			// Z2 = U3 * V

	gwfree (&ecmdata->gwdata, T0);
	gwfree (&ecmdata->gwdata, T1);
	gwfree (&ecmdata->gwdata, U0);
	gwfree (&ecmdata->gwdata, U1);
	gwfree (&ecmdata->gwdata, U);
	gwfree (&ecmdata->gwdata, U2);
	gwfree (&ecmdata->gwdata, V);
	gwfree (&ecmdata->gwdata, W);
	gwfree (&ecmdata->gwdata, t0);
}

/* Double a Weierstrass point */
/* For our purposes, speed is not critical.  This is only used during Atkin-Morain contruction of an Edwards curve. */

void ws_dbl (
	ecmhandle *ecmdata,
	struct ws *in,
	struct ws *out)
{
	int	a = -8;
//	int	b = -32;

// From https://www.nayuki.io/page/elliptic-curve-point-addition-in-projective-coordinates
// Let T = 3X^2 + aZ^2
// Let U = 2YZ
// Let V = 2UXY
// Let W = T^2 − 2V
// X2 = UW
// Y2 = T(V−W)−2(UY)^2
// Z2 = U^3

	gwnum	T, U, V, W, t0;
	T = gwalloc (&ecmdata->gwdata);
	U = gwalloc (&ecmdata->gwdata);
	V = gwalloc (&ecmdata->gwdata);
	W = gwalloc (&ecmdata->gwdata);
	t0 = gwalloc (&ecmdata->gwdata);

	gwsetmulbyconst (&ecmdata->gwdata, 3);
	gwsquare2 (&ecmdata->gwdata, in->x, T, GWMUL_MULBYCONST);				// T = X^2
	gwsquare2 (&ecmdata->gwdata, in->z, t0, 0);						// t0 = Z^2
	gwsmallmul (&ecmdata->gwdata, a, t0);							// t0 *= a
	gwadd3 (&ecmdata->gwdata, T, t0, T);							// T += t0

	gwsetmulbyconst (&ecmdata->gwdata, 2);
	gwmul3 (&ecmdata->gwdata, in->y, in->z, U, GWMUL_MULBYCONST);				// U = Y * Z * 2

	gwmul3 (&ecmdata->gwdata, in->x, in->y, V, GWMUL_MULBYCONST);				// V = X * Y * 2
	gwmul3 (&ecmdata->gwdata, V, U, V, 0);							// V *= U

	gwsquare2 (&ecmdata->gwdata, T, W, 0);							// W = T^2
	gwsub3 (&ecmdata->gwdata, W, V, W);							// W -= V
	gwsub3 (&ecmdata->gwdata, W, V, W);							// W -= V

	gwmul3 (&ecmdata->gwdata, U, W, out->x, 0);						// X2 = U * W

	gwmul3 (&ecmdata->gwdata, U, in->y, out->y, 0);						// Y2 = U * Y
	gwsquare2 (&ecmdata->gwdata, out->y, out->y, GWMUL_MULBYCONST);				// Y2 = 2(Y2)^2
	gwsub3 (&ecmdata->gwdata, V, W, t0);							// t0 = V - W
	gwmul3 (&ecmdata->gwdata, T, t0, t0, 0);						// t0 = T * t0
	gwsub3 (&ecmdata->gwdata, t0, out->y, out->y);						// Y2 = t0 - Y2

	gwsquare2 (&ecmdata->gwdata, U, out->z, 0);						// Z2 = U * U
	gwmul3 (&ecmdata->gwdata, out->z, U, out->z, 0);					// Z2 *= U

	gwfree (&ecmdata->gwdata, T);
	gwfree (&ecmdata->gwdata, U);
	gwfree (&ecmdata->gwdata, V);
	gwfree (&ecmdata->gwdata, W);
	gwfree (&ecmdata->gwdata, t0);
}

/* Multiply a Weierstrass point by n */

void ws_mul (
	ecmhandle *ecmdata,
	struct ws *arg,
	uint64_t n)
{
	struct ws orig;

	// Copy the input arg for later additions
	ws_alloc (ecmdata, &orig);
	gwcopy (&ecmdata->gwdata, arg->x, orig.x);
	gwcopy (&ecmdata->gwdata, arg->y, orig.y);
	gwcopy (&ecmdata->gwdata, arg->z, orig.z);

	// Find the topmost bit in power
	uint64_t current_bit;
	for (current_bit = 0x8000000000000000ULL; n < current_bit; current_bit >>= 1);

	// Exponentiate
	for (current_bit >>= 1; current_bit; current_bit >>= 1) {
		ws_dbl (ecmdata, arg, arg);
		if (n & current_bit) ws_add (ecmdata, arg, &orig, arg);
	}
	ws_check (ecmdata, arg);

	// Free allocated memory
	ws_free (ecmdata, &orig);
}

/**************************************************************
 *	Edwards ECM Functions
 **************************************************************/

/* Macro to swap to ed structs */

#define ed_swap(a,b)	{ struct ed t; t = a; a = b; b = t; }

/* This routine initializes an ed struct with four allocated gwnums */

bool ed_alloc (			/* Returns TRUE if successful */
	ecmhandle *ecmdata,
	struct ed *arg)
{
	arg->x = gwalloc (&ecmdata->gwdata);
	if (arg->x == NULL) return (FALSE);
	arg->y = gwalloc (&ecmdata->gwdata);
	if (arg->y == NULL) return (FALSE);
	arg->z = gwalloc (&ecmdata->gwdata);
	if (arg->z == NULL) return (FALSE);
	arg->t = NULL;
	arg->alternate_format = FALSE;
	return (TRUE);
}

/* This routine frees an ed struct's gwnums */

void ed_free (
	ecmhandle *ecmdata,
	struct ed *arg)
{
	gwfree (&ecmdata->gwdata, arg->x), arg->x = NULL;
	gwfree (&ecmdata->gwdata, arg->y), arg->y = NULL;
	gwfree (&ecmdata->gwdata, arg->z), arg->z = NULL;
	gwfree (&ecmdata->gwdata, arg->t), arg->t = NULL;
}

/* Verify a point is on the Edwards elliptic curve */

bool ed_check (				/* Returns TRUE if point is on the Edwards curve */
	ecmhandle *ecmdata,
	struct ed *in)
{
	gwnum	t0, t1, t2, t3;
	bool	result;

	ASSERTG (!in->alternate_format);

	t0 = gwalloc (&ecmdata->gwdata);
	t1 = gwalloc (&ecmdata->gwdata);
	t2 = gwalloc (&ecmdata->gwdata);
	t3 = gwalloc (&ecmdata->gwdata);

	// Verify ax^2 + y^2 = 1 + dx^2y^2 or in projective: (ax^2 + y^2)z^2 = z^4 + dx^2y^2

	gwsquare2 (&ecmdata->gwdata, in->x, t0, GWMUL_PRESERVE_S1);			// t0 = X * X
	gwsquare2 (&ecmdata->gwdata, in->y, t1, GWMUL_PRESERVE_S1);			// t1 = Y * Y
	if (in->z != NULL) gwsquare2 (&ecmdata->gwdata, in->z, t2, GWMUL_PRESERVE_S1);	// t2 = Z * Z
	else dbltogw (&ecmdata->gwdata, 1.0, t2);
	gwmul3 (&ecmdata->gwdata, t0, t1, t3, 0);					// t3 = t0 * t1	(x^2y^2)
	gwmul3 (&ecmdata->gwdata, t3, ecmdata->ed_d, t3, 0);				// t3 *= d	(dx^2y^2)
	if (ecmdata->ed_a != NULL) gwmul3 (&ecmdata->gwdata, t0, ecmdata->ed_a, t0, 0);	// t0 *= a	(ax^2)
	gwadd3 (&ecmdata->gwdata, t0, t1, t0);						// t0 += t1	(ax^2 + y^2)
	gwmul3 (&ecmdata->gwdata, t0, t2, t0, 0);					// t0 *= t2	(ax^2 + y^2)z^2
	gwsquare2 (&ecmdata->gwdata, t2, t2, 0);					// t2 *= t2	(z^4)
	gwadd3 (&ecmdata->gwdata, t2, t3, t2);						// t2 += t3	(z^4 + dx^2y^2)

	gwsub3 (&ecmdata->gwdata, t0, t2, t0);						// t0 = t0 - t2, should be zero mod N
	giant tmp = popg (&ecmdata->gwdata.gdata, ((unsigned long) ecmdata->gwdata.bit_length >> 5) + 5);
	gwtogiant (&ecmdata->gwdata, t0, tmp);
	modg (ecmdata->N, tmp);
	result = (tmp->sign == 0);
	pushg (&ecmdata->gwdata.gdata, 1);

	gwfree (&ecmdata->gwdata, t0);
	gwfree (&ecmdata->gwdata, t1);
	gwfree (&ecmdata->gwdata, t2);
	gwfree (&ecmdata->gwdata, t3);

	return (result);
}

/* This routine allocates and calculates FFT(1/2) */

void create_ed_half (
	ecmhandle *ecmdata)
{
	ecmdata->ed_half = gwalloc (&ecmdata->gwdata);
	gshiftright (1, ecmdata->N);
	gianttogw (&ecmdata->gwdata, ecmdata->N, ecmdata->ed_half);
	gwsmalladd (&ecmdata->gwdata, 1, ecmdata->ed_half);
	gwfft (&ecmdata->gwdata, ecmdata->ed_half, ecmdata->ed_half);
	gshiftleft (1, ecmdata->N);		// Restore N
	iaddg (1, ecmdata->N);
}

/* Options for ed_add, ed_dbl, and even ed_extend */

#define	ED_XYZ			0x1	// Result must be in X:Y:Z coordinates (no funky alternative coordinates).  Results will be used for save files or modinv.
#define ED_RESULT_FOR_ADD	0x2	// Result will be used in ed_add next.  Implies extended coordinates.
#define ED_RESULT_FOR_DBL	0x4	// Result will be used in ed_dbl next.  Implies not extended coordinates.
#define ED_STARTNEXTFFT		0x8	// Results can be partially FFTed.  Typically, a final result or result used to create a save file should not be FFTed.
#define ED_FFT_S1		0x10	// For ed_dbl, FFT Z in first source arg.
#define ED_FFT_S1Z		0x20	// For ed_add, FFT Z in first source arg.  Second source arg is always FFTed.
#define ED_FFT_S1T		0x40	// For ed_add, FFT T in first source arg.  Second source arg is always FFTed.
#define ED_SUBTRACT		0x80	// For ed_add, subtract instead of add.
#define ED_FORCE_EXTEND		0x100	// For ed_extend, force extend computation using (or reusing) existing T buffer.
#define ED_NO_GWALLOC		0x200	// For ed_dbl and ed_add called from NAF_dictionary_init.  Use in->t as for a temporary gwnum.

/* Turn an (X:Y:Z) Edwards point into an extended (X:Y:T:Z) extended Edwards point */

void ed_extend (
	ecmhandle *ecmdata,
	struct ed *e,
	int	options)
{
	int mul_options = (options & ED_STARTNEXTFFT) ? GWMUL_STARTNEXTFFT : 0;

	ASSERTG (!e->alternate_format);

	// Check if already using extended coordinates
	if (!(options & ED_FORCE_EXTEND) && e->t != NULL) return;
	// Allocate a buffer for the new coordinate
	if (!(options & ED_FORCE_EXTEND)) e->t = gwalloc (&ecmdata->gwdata);
	// Extend (X,Y,Z) to (XZ, YZ, XY, Z^2)
	gwmul3 (&ecmdata->gwdata, e->x, e->y, e->t, GWMUL_FFT_S12 | mul_options);		// t = xy
	if (e->z != NULL) {
		gwmul3 (&ecmdata->gwdata, e->x, e->z, e->x, GWMUL_FFT_S2 | mul_options);	// x = xz
		gwmul3 (&ecmdata->gwdata, e->y, e->z, e->y, GWMUL_FFT_S2 | mul_options);	// y = yz
		gwsquare2 (&ecmdata->gwdata, e->z, e->z, mul_options);				// z = z^2
	}
}

/* Add or subtract two Edwards points (optionally twisted) */

void ed_add (
	ecmhandle *ecmdata,
	struct ed *in1,			// Can be not FFTed, partially FFTed, or fully FFTed (on return in1->x, in1->y will be FFTed)
	struct ed *in2,			// Can be not FFTed, partially FFTed, or fully FFTed (on return in2 will be FFTed)
	struct ed *out,			// Output can be same as an in1 or in2.  z,t will be allocated if needed.
	int	options)
{
	gwnum	tmp1, tmp2;
	bool	safe11 = mul_safe (&ecmdata->gwdata, 1, 1);					// T3 = E * H will be safe if E,H are unnormalized
	int	subtract = options & ED_SUBTRACT;
	int	extended = options & ED_RESULT_FOR_ADD;
	int	mul_S1Z_options = (options & ED_FFT_S1Z) ? GWMUL_FFT_S1 : 0;
	int	mul_S1T_options = (options & ED_FFT_S1T) ? GWMUL_FFT_S1 : 0;
	int	mul_options = (options & ED_STARTNEXTFFT) ? GWMUL_STARTNEXTFFT : 0;

	ASSERTG (!in1->alternate_format && !in2->alternate_format);
	//ASSERTG (safe11 || !extended || FFT_state (in1->t) == NOT_FFTed || in2->z != NULL);	// FFTed in1->t and Z2=NULL is OK but triggers a gwunfft
	//ASSERTG (safe11 || !extended || FFT_state (in2->t) == NOT_FFTed || in1->z != NULL);	// FFTed in2->t and Z1=NULL is OK but triggers a gwunfft
	ASSERTG (ecmdata->ed_a == NULL || FFT_state (ecmdata->ed_a) == FULLY_FFTed);
	ASSERTG (!(options & ED_NO_GWALLOC) || (in1 != out && in1->t != NULL && out->t != NULL));

	// Extended coordinates are required
	if (in1->t == NULL) ed_extend (ecmdata, in1, ED_STARTNEXTFFT);
	if (in2->t == NULL) ed_extend (ecmdata, in2, ED_STARTNEXTFFT);

	// Allocate temporaries and out->z (also used as a temporary)
	gwnum in1z = in1->z;						// Remember in case allocating out->z clobbers this value
	gwnum in2z = in2->z;						// Remember in case allocating out->z clobbers this value
	if (out->z == NULL) out->z = gwalloc (&ecmdata->gwdata);	// Do not use out->z until in1->z and in2->z are longer needed
	if (out->t != NULL) tmp1 = out->t;				// Do not use tmp1 until in1->t and in2->t are longer needed
	else tmp1 = gwalloc (&ecmdata->gwdata);
	if (options & ED_NO_GWALLOC) tmp2 = in1->t;
	else tmp2 = gwalloc (&ecmdata->gwdata);

	// Add algorithm from https://eprint.iacr.org/2021/1061, "Edwards curves and FFT-based multiplication" by Pavel Atnashev et.al.

	// With a normalized dictionary entry as in2, this gwmul3 will not take place
	gwnum D;
	if (in2z == NULL) D = in1->t;
	else gwmul3 (&ecmdata->gwdata, in1->t, in2z, D = tmp2, mul_S1T_options | GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT_IF(safe11 || !extended)); // D = T1 * Z2
	// With a normalized dictionary entry as in2, this gwmul3 will take place (in1z and out->z will be equal and source = dest is more efficient)
	gwnum C;
	if (in1z == NULL) C = in2->t;
	else gwmul3 (&ecmdata->gwdata, in1z, in2->t, C = out->z, mul_S1Z_options | GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT_IF(safe11 || !extended)); // C = Z1 * T2
	gwaddsub4o (&ecmdata->gwdata, D, C, tmp2, out->z, GWADD_DELAYNORM_IF(safe11 || !extended));				// E = D + C, H = D - C
        if (subtract) {
		gwswap (tmp2, out->z);			// swap E,H since C should have been negated
		gwmulmuladd5 (&ecmdata->gwdata, in1->x, in2->y, in1->y, in2->x, tmp1, GWMUL_FFT_S1234 | GWMUL_STARTNEXTFFT);	// F = X1*Y2 - Y1*[-]X2
		if (ecmdata->ed_a == NULL)
			gwmulmulsub5 (&ecmdata->gwdata, in1->y, in2->y, in1->x, in2->x, out->y, GWMUL_STARTNEXTFFT);		// G = Y1*Y2 + X1*[-]X2
		else {
			gwmul3 (&ecmdata->gwdata, in1->x, in2->x, out->x, GWMUL_STARTNEXTFFT);
			gwmulmulsub5 (&ecmdata->gwdata, in1->y, in2->y, ecmdata->ed_a, out->x, out->y, GWMUL_STARTNEXTFFT);	// G = Y1*Y2 + aX1*[-]X2
		}
        } else {
		gwmulmulsub5 (&ecmdata->gwdata, in1->x, in2->y, in1->y, in2->x, tmp1, GWMUL_FFT_S1234 | GWMUL_STARTNEXTFFT);	// F = X1*Y2 - Y1*X2
		if (ecmdata->ed_a == NULL)
			gwmulmuladd5 (&ecmdata->gwdata, in1->y, in2->y, in1->x, in2->x, out->y, GWMUL_STARTNEXTFFT);		// G = Y1*Y2 + X1*X2
		else {
			gwmul3 (&ecmdata->gwdata, in1->x, in2->x, out->x, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			gwmulmuladd5 (&ecmdata->gwdata, in1->y, in2->y, ecmdata->ed_a, out->x, out->y, GWMUL_STARTNEXTFFT);	// G = Y1*Y2 + aX1*X2
		}
	}
	if (!extended) {
		gwmul3 (&ecmdata->gwdata, tmp2, tmp1, tmp2, GWMUL_FFT_S2 | mul_options);			// X3 = E * F
		gwmul3 (&ecmdata->gwdata, tmp1, out->y, tmp1, GWMUL_FFT_S2 | mul_options);			// Z3 = F * G
		gwmul3 (&ecmdata->gwdata, out->y, out->z, out->z, mul_options);					// Y3 = G * H  (better to overwrite arg being FFTed)
		gwswap (out->z, out->y);
		gwswap (tmp2, out->x);
		gwswap (tmp1, out->z);
		out->t = NULL;
	} else {
		gwmul3 (&ecmdata->gwdata, tmp2, tmp1, out->x, GWMUL_FFT_S12 | mul_options);			// X3 = E * F
		gwmul3 (&ecmdata->gwdata, tmp1, out->y, tmp1, GWMUL_FFT_S2 | mul_options);			// Z3 = F * G
		gwmul3 (&ecmdata->gwdata, out->y, out->z, out->y, GWMUL_FFT_S2 | mul_options);			// Y3 = G * H
		gwmul3 (&ecmdata->gwdata, tmp2, out->z, tmp2, mul_options);					// T3 = E * H
		gwswap (tmp1, out->z);
		out->t = tmp2, tmp2 = NULL;
	}

	// Cleanup
	if (options & ED_NO_GWALLOC) {
		if (!extended) out->t = tmp2;
		in1->t = tmp1;
	} else {
		gwfree (&ecmdata->gwdata, tmp1);
		gwfree (&ecmdata->gwdata, tmp2);
	}
}

/* Double an Edwards point (optionally twisted) */

void ed_dbl (
	ecmhandle *ecmdata,
	struct ed *in,			// Inputs may be not FFTed, partially FFTed, or fully FFTed.  X and Y coordinates will be FFTed.
	struct ed *out,			// Output can be same as input
	int	options)
{
	gwnum	tmp1, tmp2;
	int	extended = options & ED_RESULT_FOR_ADD;
	int	mul_S1_options = (options & ED_FFT_S1) ? GWMUL_FFT_S1 : 0;
	int	mul_options = (options & ED_STARTNEXTFFT) ? GWMUL_STARTNEXTFFT : 0;
	int	can_alternate_format = !(options & ED_XYZ) && !extended;

	ASSERTG (ecmdata->ed_a == NULL || FFT_state (ecmdata->ed_a) == FULLY_FFTed);
	ASSERTG (!(options & ED_NO_GWALLOC) || (in != out && in->t != NULL && out->t != NULL && extended));

	// Get algorithm preference
static	int algo5 = 2;		// FALSE = don't use algorithm 5, TRUE = use algorithm 5, 2 = not yet initialized
static	int algo5a = 2;		// FALSE = don't use algorithm 5a, TRUE = use algorithm 5a, 2 = not yet initialized
static	int algo7 = 2;		// FALSE = don't use algorithm 7, TRUE = use algorithm 7, 2 = not yet initialized
	if (algo5 == 2) {
		algo5 = IniGetInt (INI_FILE, "EdDblAlgo5", 1);		// Default is allow algorithm 5
		algo5a = IniGetInt (INI_FILE, "EdDblAlgo5a", 1);	// Default is allow algorithm 5a (algorithm 5 with a mul by 1/2 added)
		algo7 = IniGetInt (INI_FILE, "EdDblAlgo7", 1);		// Default is allow algorithm 7 (new funky algorithm not in the literature)
	}

	// Allocate temporaries and out->z (also used as a temporary).  Set mul by const.
	gwnum inz = in->z;						// Remember in case allocating out->z clobbers this value
	if (out->z == NULL) out->z = gwalloc (&ecmdata->gwdata);	// Do not use out->z until in->z is no longer needed
	if (out->t != NULL) tmp1 = out->t, out->t = NULL;
	else tmp1 = gwalloc (&ecmdata->gwdata);
	if (options & ED_NO_GWALLOC) tmp2 = in->t;
	else tmp2 = gwalloc (&ecmdata->gwdata);
	gwsetmulbyconst (&ecmdata->gwdata, 2);

	// Select from double algorithms in https://eprint.iacr.org/2021/1061, "Edwards curves and FFT-based multiplication" by Pavel Atnashev et.al.
	// Also included is a new double algorithm, called algorithm 7 (there briefly was an algorithm 6 that was abandoned).

	// Algorithm 5 from the paper, requires EXTRA_BITS > 1.3421
	if (algo5 && squaresquareadd_safe (&ecmdata->gwdata, 0, 0, 0, 0)) {
	    if (inz == NULL) dbltogw (&ecmdata->gwdata, 2.0, out->z);
	    else gwsquare2 (&ecmdata->gwdata, inz, out->z, mul_S1_options | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// C = 2Z^2, two-dest squaring
	    if (ecmdata->ed_a == NULL) {			// Not a twisted Edwards curve
		gwsquare2 (&ecmdata->gwdata, in->x, tmp2, GWMUL_FFT_S1 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// G-H = 2X^2, two-dest squaring
		gwmul3 (&ecmdata->gwdata, in->x, in->y, tmp1, GWMUL_FFT_S2 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// E = 2XY
		gwmulmuladd5 (&ecmdata->gwdata, in->y, in->y, in->x, in->x, out->y, GWMUL_STARTNEXTFFT);		// G = Y^2 + X^2
	    } else {						// Twisted Edwards curve
		gwsquare2 (&ecmdata->gwdata, in->x, tmp2, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);				// X^2, two-dest squaring
		gwmul3 (&ecmdata->gwdata, in->x, in->y, out->x, GWMUL_FFT_S2 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// E = 2XY
		gwmulmuladd5 (&ecmdata->gwdata, ecmdata->ed_a, tmp2, in->y, in->y, out->y, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT); // G = Y^2 + aX^2
		gwmul3 (&ecmdata->gwdata, ecmdata->ed_a, tmp2, tmp2, GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);		// G-H = 2aX^2
		gwswap (tmp1, out->x);
	    }
	    if (!extended) {
		gwsubmul4 (&ecmdata->gwdata, out->y, tmp2, out->y, tmp2, GWMUL_FFT_S13 | mul_options);			// Y3 = (H=G-(G-H)) * G
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, out->y, out->x, GWMUL_FFT_S1 | mul_options);		// Z3 = (F=C-G) * G
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, tmp1, tmp1, mul_options);					// X3 = (F=C-G) * E  (better to write result to argument that needs FFTing)
		gwswap (tmp2, out->y);
		gwswap (out->x, out->z);
		gwswap (tmp1, out->x);
	    } else {
		gwsubmul4 (&ecmdata->gwdata, out->y, tmp2, out->y, out->x, GWMUL_FFT_S123 | mul_options);		// Y3 = (H=G-(G-H)) * G
		gwsubmul4 (&ecmdata->gwdata, out->y, tmp2, tmp1, tmp2, GWMUL_FFT_S3 | mul_options);			// T3 = (H=G-(G-H)) * E
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, tmp1, tmp1, GWMUL_FFT_S1 | mul_options);			// X3 = (F=C-G) * E
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, out->y, out->z, mul_options);				// Z3 = (F=C-G) * G
		gwswap (out->x, out->y);
		gwswap (tmp2, out->t);
		gwswap (tmp1, out->x);
	    }
	}

	// Algorithm 5a, requires EXTRA_BITS > 0.8150.  Nearly the same as algorithm 5.  Uses a precomputed FFT(1/2).
	else if (algo5a && squaremuladd_safe (&ecmdata->gwdata, 0, 0, 0, 0)) {
	    if (ecmdata->ed_half == NULL) create_ed_half (ecmdata);
	    if (inz == NULL) dbltogw (&ecmdata->gwdata, 2.0, out->z);
	    else gwsquare2 (&ecmdata->gwdata, inz, out->z, mul_S1_options | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// C = 2Z^2, two-dest squaring
	    if (ecmdata->ed_a == NULL) {			// Not a twisted Edwards curve
		gwsquare2 (&ecmdata->gwdata, in->x, tmp2, GWMUL_FFT_S1 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// G-H = 2X^2, two-dest squaring
		gwmul3 (&ecmdata->gwdata, in->x, in->y, tmp1, GWMUL_FFT_S2 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// E = 2XY
		gwmulmuladd5 (&ecmdata->gwdata, ecmdata->ed_half, tmp2, in->y, in->y, out->y, GWMUL_STARTNEXTFFT);	// G = Y^2 + 1/2 * 2X^2
	    } else {						// Twisted Edwards curve
		gwsquare2 (&ecmdata->gwdata, in->x, tmp2, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);				// X^2, two-dest squaring
		gwmul3 (&ecmdata->gwdata, in->x, in->y, out->x, GWMUL_FFT_S2 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// E = 2XY
		gwmul3 (&ecmdata->gwdata, ecmdata->ed_a, tmp2, tmp2, GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);		// G-H = 2aX^2
		gwmulmuladd5 (&ecmdata->gwdata, ecmdata->ed_half, tmp2, in->y, in->y, out->y, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT); // G = Y^2 + 1/2 * 2aX^2
		gwswap (tmp1, out->x);
	    }
	    if (!extended) {
		gwsubmul4 (&ecmdata->gwdata, out->y, tmp2, out->y, tmp2, GWMUL_FFT_S13 | mul_options);			// Y3 = (H=G-(G-H)) * G
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, out->y, out->x, GWMUL_FFT_S1 | mul_options);		// Z3 = (F=C-G) * G
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, tmp1, tmp1, mul_options);					// X3 = (F=C-G) * E  (better to write result to argument that needs FFTing)
		gwswap (tmp2, out->y);
		gwswap (out->x, out->z);
		gwswap (tmp1, out->x);
	    } else {
		gwsubmul4 (&ecmdata->gwdata, out->y, tmp2, out->y, out->x, GWMUL_FFT_S123 | mul_options);		// Y3 = (H=G-(G-H)) * G
		gwsubmul4 (&ecmdata->gwdata, out->y, tmp2, tmp1, tmp2, GWMUL_FFT_S3 | mul_options);			// T3 = (H=G-(G-H)) * E
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, tmp1, tmp1, GWMUL_FFT_S1 | mul_options);			// X3 = (F=C-G) * E
		gwsubmul4 (&ecmdata->gwdata, out->z, out->y, out->y, out->z, mul_options);				// Z3 = (F=C-G) * G
		gwswap (out->x, out->y);
		gwswap (tmp2, out->t);
		gwswap (tmp1, out->x);
	    }
	}

	// Algorithm 7.  Funky double of an Edwards point (twisted not supported).  Works for all EXTRA_BITS values if FFT(1) is simple.  The algorithm uses
	// 18 reads and 12 writes compared to algorithm 4 which uses 15 reads and 13 writes.  The main benefit is that GW_STARTNEXTFFT can be used for all
	// operations.  Here is a brief description of the algorithm. */
	// Using standard notation:
	//	H = Y^2 - X^2
	//	G = Y^2 + X^2
	//	E = 2XY
	//	F = 2Z^2 - G,
	// Output is:
	//	X3 = F * E
	//	Y3 = H * G
	//	Z3 = F * G
	//	T3 = H * E
	//
	// Rather than a standard input of X:Y:Z, input is FFT(2X):Y+X:2Z.  Pre-compute FFT(1/2).
	//	2H = 2(Y^2-X^2) = 2(Y+X)(Y-X) = 2(Y+X)(Y+X-2X)		mul_safe(0,1)		always safe
	//	2G = 2(Y^2+X^2) = 2(H+2X^2) = 2H+(2X)^2			squareadd_safe(0,0)	always safe except when FFT(1) is not simple
	//	4F = 4(2Z^2-G) = 2((2Z)^2-2G)				squareadd_safe(0,0)	always safe except when FFT(1) is not simple
	//	Let M = (Y+X)^2 = Y^2+2XY+X^2, E = M-G
	//	2M = 2(Y+X)^2						square_safe(0)		always safe
	// Output is:
	//	8X3 = 4F * (2E=2M-2G)					mul_safe(0,1)		always safe
	//	4(Y3 + X3) = 2H * 2G + 1/2 * 8X3			mulmuladd_safe(0,0,0,0)	always_safe
	//	8Z3 = 4F * 2G						mul_safe(0,0)		always safe
	//
	// To enter funky mode, input is X:Y:Z, output is FFT(2X):Y+X:2Z using algo 4 (alternatively (and better), ed_add could be modified to enter funky mode)
	//	A = X^2							square_safe(0,0)	always safe but cannot startnextfft
	//	E = 2XY							mul_safe(0,0)		always safe
	//	B = Y^2							square_safe(0,0)	always safe but cannot startnextfft
	//	G = B + A, H = B - A					must normalize
	//	C = 2Z^2						square_safe(0,0)	always safe
	// Output is:
	//	2X3 = 2 * (F=C-G) * E					addmul_safe(0,0,0)	always safe
	//	Y3 = H * G + 1/3 * X3					mulmuladd_safe(0,0)	always_safe
	//	2Z3 = 2 * (F=C-G) * G					addmul_safe(0,0,0)	always safe
	//
	// To exit funky mode, input is FFT(2X):Y+X:2Z, output is to X:Y:Z:T
	//	2H = 2(Y^2-X^2) = 2(Y+X)(Y-X) = 2(Y+X)(Y+X-2X)		mul_safe(0,1)		always safe
	//	2G = 2(Y^2+X^2) = 2(H+2X^2) = 2H+(2X)^2			squareadd_safe(0,0)	always safe except when FFT(1) is not simple
	//	2F = 2(2Z^2-G) = (2Z)^2-2G				squareadd_safe(0,0)	always safe except when FFT(1) is not simple
	//	Let M= (Y+X)^2 = Y^2+2XY+X^2, E = M-G
	//	2M = 2(Y+X)^2						square_safe(0)		always safe
	// Output is:
	//	4X3 = 2F * (2E=2M-2G)					mul_safe(0,1)		always safe
	//	4Y3 = 2H * 2G						mul_safe(0,0)		always_safe
	//	4Z3 = 2F * 2G						mul_safe(0,0)		always safe
	//	4T3 = 2H * (2E=2M-2G)					mul_safe(0,1)		always safe
	else if (algo7 && squareadd_safe (&ecmdata->gwdata, 0, 0, 0) && ecmdata->ed_a == NULL && inz != NULL && (in->alternate_format || can_alternate_format)) {
	    if (ecmdata->ed_half == NULL) create_ed_half (ecmdata);

	    // Enter funky mode by using algorithm 4 with an alternate ending to create FFT(2X):Y+X:2Z 
	    // Long-term it would be better if ed_add was modified to enter funky mode
	   if (!in->alternate_format) {
		gwsquare2 (&ecmdata->gwdata, inz, out->z, mul_S1_options | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);		// C = 2Z^2, two-dest squaring
		gwsquare2 (&ecmdata->gwdata, in->x, tmp2, GWMUL_FFT_S1);							// A = X^2, two-dest squaring
		gwmul3 (&ecmdata->gwdata, in->x, in->y, out->x, GWMUL_FFT_S2 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);		// E = 2XY
		gwsquare2 (&ecmdata->gwdata, in->y, out->y, 0);									// B = Y^2
		gwaddsub4o (&ecmdata->gwdata, out->y, tmp2, tmp2, out->y, GWADD_FORCE_NORMALIZE);				// G = B + A, H = B - A
		gwsubmul4 (&ecmdata->gwdata, out->z, tmp2, tmp2, tmp1, GWMUL_FFT_S12 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// 2Z3 = 2 * (F=C-G) * G
		gwsubmul4 (&ecmdata->gwdata, out->z, tmp2, out->x, out->x, GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);		// 2X3 = 2 * (F=C-G) * E
		gwmulmuladd5 (&ecmdata->gwdata, tmp2, out->y, ecmdata->ed_half, out->x, out->y, GWMUL_FFT_S4 | GWMUL_STARTNEXTFFT); // Y3+X3 = G * H + 1/2 * 2X3
		gwswap (tmp1, out->z);
		out->alternate_format = TRUE;
	    }

	   // Handle funky mode inputs
	   else {
		gwsquare2 (&ecmdata->gwdata, in->y, tmp2, GWMUL_FFT_S1 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);		// 2M = 2(Y+X)^2, two-dest squaring
		gwsubmul4 (&ecmdata->gwdata, in->y, in->x, in->y, out->y, GWMUL_FFT_S1 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT); // 2H = 2(Y+X)(Y+X-2X)
		gwmuladd4 (&ecmdata->gwdata, in->x, in->x, out->y, out->x, GWMUL_FFT_S3 | GWMUL_STARTNEXTFFT);			// 2G = (2X)^2+2H, (could save a read with asm change)
		if (can_alternate_format) {			// Stay in funky mode
		    gwmulsub4 (&ecmdata->gwdata, inz, inz, out->x, out->z, GWMUL_FFT_S3 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT); // 4F = 2((2Z)^2-2G)
		    gwmul3 (&ecmdata->gwdata, out->z, out->x, tmp1, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);				// 8Z3 = 4F * 2G
		    gwsubmul4 (&ecmdata->gwdata, tmp2, out->x, out->z, tmp2, GWMUL_STARTNEXTFFT);				// 8X3 = 4F * (2E=2M-2G)
		    gwmulmuladd5 (&ecmdata->gwdata, out->y, out->x, ecmdata->ed_half, tmp2, out->y, GWMUL_FFT_S4 | GWMUL_STARTNEXTFFT);	// 4(Y3+X3) = 2H*2G + 1/2*8X3
		    gwswap (tmp1, out->z);
		    gwswap (tmp2, out->x);
		} else {					// Exit funky mode
		    gwmulsub4 (&ecmdata->gwdata, inz, inz, out->x, out->z, GWMUL_FFT_S3 | GWMUL_STARTNEXTFFT);			// 2F = (2Z)^2-2G
		    gwmul3 (&ecmdata->gwdata, out->z, out->x, tmp1, GWMUL_FFT_S1 | mul_options);				// 4Z3 = 2F * 2G
		    gwsubmul4 (&ecmdata->gwdata, tmp2, out->x, out->z, out->z, GWMUL_FFT_S1 | mul_options);			// 4X3 = 2F * (2E=2M-2G)
		    if (extended) gwsubmul4 (&ecmdata->gwdata, tmp2, out->x, out->y, tmp2, GWMUL_FFT_S3 | mul_options);		// 4T3 = 2H * (2E=2M-2G)
		    gwmul3 (&ecmdata->gwdata, out->y, out->x, out->y, mul_options);						// 4Y3 = 2H * 2G
		    gwswap (out->z, out->x);
		    gwswap (tmp1, out->z);
		    if (extended) gwswap (tmp2, out->t);
		    out->alternate_format = FALSE;
		}
	    }
	}

	// Algorithm 4 from the paper, works with any EXTRA_BITS value.
	else {													// Algorithm 4
	    bool safe21 = mul_safe (&ecmdata->gwdata, 2, 1);								// Z3 will be safe if G & H are unnormalized
	    if (inz == NULL) dbltogw (&ecmdata->gwdata, 2.0, out->z);
	    else gwsquare2 (&ecmdata->gwdata, inz, out->z, mul_S1_options | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// C = 2Z^2, two-dest squaring
	    if (ecmdata->ed_a == NULL) {			// Not a twisted Edwards curve
		gwsquare2 (&ecmdata->gwdata, in->x, tmp2, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT_IF(safe21));		// A = X^2, two-dest squaring
	    } else {					// Twisted Edwards curve
		gwsquare2 (&ecmdata->gwdata, in->x, tmp2, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);				// A = X^2, two-dest squaring
		gwmul3 (&ecmdata->gwdata, ecmdata->ed_a, tmp2, tmp2, GWMUL_STARTNEXTFFT_IF(safe21));			// A = aX^2
	    }
	    gwmul3 (&ecmdata->gwdata, in->x, in->y, out->x, GWMUL_FFT_S2 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT);	// E = 2XY
	    gwsquare2 (&ecmdata->gwdata, in->y, out->y, GWMUL_STARTNEXTFFT_IF(safe21));					// B = Y^2
	    gwaddsub4o (&ecmdata->gwdata, out->y, tmp2, tmp2, out->y, GWADD_DELAYNORM_IF(safe21));			// G = B + A, H = B - A
	    if (!extended) {
		gwmul3 (&ecmdata->gwdata, tmp2, out->y, out->y, GWMUL_FFT_S1 | mul_options);				// Y3 = G * H
		gwsubmul4 (&ecmdata->gwdata, out->z, tmp2, tmp2, tmp1, GWMUL_FFT_S1 | mul_options);			// Z3 = (F=C-G) * G
		gwsubmul4 (&ecmdata->gwdata, out->z, tmp2, out->x, out->x, mul_options);				// X3 = (F=C-G) * E
		gwswap (tmp1, out->z);
	    } else {
		gwmul3 (&ecmdata->gwdata, tmp2, out->y, tmp1, GWMUL_FFT_S12 | mul_options);				// Y3 = G * H
		gwmul3 (&ecmdata->gwdata, out->x, out->y, out->y, GWMUL_FFT_S1 | mul_options);				// T3 = E * H
		gwsubmul4 (&ecmdata->gwdata, out->z, tmp2, out->x, out->x, GWMUL_FFT_S1 | mul_options);			// X3 = (F=C-G) * E
		gwsubmul4 (&ecmdata->gwdata, out->z, tmp2, tmp2, out->z, mul_options);					// Z3 = (F=C-G) * G
		gwswap (out->y, out->t);
		gwswap (tmp1, out->y);
	    }
	}

	// Cleanup
	if (options & ED_NO_GWALLOC)
		in->t = (tmp1 != NULL ? tmp1 : tmp2);
	else {
		gwfree (&ecmdata->gwdata, tmp1);
		gwfree (&ecmdata->gwdata, tmp2);
	}
}


/**************************************************************
 *	Montgomery ECM Functions
 **************************************************************/

/* This routine initializes an xz pair with two allocated gwnums */

__inline int alloc_xz (			/* Returns TRUE if successful */
	ecmhandle *ecmdata,
	struct xz *arg)
{
	arg->x = gwalloc (&ecmdata->gwdata);
	if (arg->x == NULL) return (FALSE);
	arg->z = gwalloc (&ecmdata->gwdata);
	if (arg->z == NULL) return (FALSE);
	return (TRUE);
}

/* This routine cleans up an xz pair with two allocated gwnums */

__inline void free_xz (
	ecmhandle *ecmdata,
	struct xz *arg)
{
	gwfree (&ecmdata->gwdata, arg->x); arg->x = NULL;
	gwfree (&ecmdata->gwdata, arg->z); arg->z = NULL;
}

/* Macro to swap to xz structs */

#define xzswap(a,b)	{ struct xz t; t = a; a = b; b = t; }

/* This routine gwcopies an xz pair */

__inline void gwcopy_xz (
	ecmhandle *ecmdata,
	struct xz *src,
	struct xz *dst)
{
	gwcopy (&ecmdata->gwdata, src->x, dst->x);
	gwcopy (&ecmdata->gwdata, src->z, dst->z);
}

/* Computes 2P=(out.x:out.z) from P=(in.x:in.z), uses the global variable Ad4. */
/* Input arguments may be in FFTed state.  Out argument can be same as input argument. */
/* Scratch xz argument can equal in but cannot equal out. */

void ell_dbl_xz_scr (
	ecmhandle *ecmdata,
	struct xz *in,		/* Input value to double */
	struct xz *out,		/* Output value */
	struct xz *scr)		// Scratch registers (only the .x gwnum is used)
{				/* 10 or 11 FFTs, 4 adds */
	gwnum	t2, t3, t4;

	ASSERTG (scr != out);

	/* If we have extra_bits to square the output of a gwadd, then we can use an algorithm that minimizes FFTs. */
	if (square_safe (&ecmdata->gwdata, 1)) {
		t3 = scr->x;
		t2 = out->x;
		t4 = out->z;

		gwsetmulbyconst (&ecmdata->gwdata, 4);
		gwmul3 (&ecmdata->gwdata, in->x, in->z, t3, GWMUL_FFT_S12 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT); /* t3 = 4*x*z */
		gwsub3o (&ecmdata->gwdata, in->x, in->z, t2, GWADD_DELAY_NORMALIZE);			/* Compute x - z */
		gwsquare2 (&ecmdata->gwdata, t2, t2, GWMUL_STARTNEXTFFT);				/* t2 = (x - z)^2 */
		gwmul3 (&ecmdata->gwdata, t2, ecmdata->Ad4, t4, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);	/* t4 = t2 * Ad4 */
		gwaddmul4 (&ecmdata->gwdata, t2, t3, t4, out->x, GWMUL_FFT_S23 | GWMUL_STARTNEXTFFT);	/* outx = (t1 = t2 + t3) * t4 */
		gwaddmul4 (&ecmdata->gwdata, t4, t3, t3, out->z, GWMUL_STARTNEXTFFT);			/* outz = (t4 + t3) * t3 */
	}

	/* This algorithm uses an extra FFT to assure that it will work even if there are no extra bits.  We've made a conscious decision to */
	/* spend an extra FFT to make ell_add as clean as possible.  Ell_adds are ten times more common than ell_dbls.  This version lets */
	/* ell_add FFT x and z without any worries. */
	else {
		int	t1_safe = addmul_safe (&ecmdata->gwdata, 2,0,0);

		t2 = out->x;
		t3 = out->z;
		t4 = scr->x;

		gwsquare2 (&ecmdata->gwdata, in->x, scr->x, GWMUL_FFT_S1);					/* x^2 */
		gwsquare2 (&ecmdata->gwdata, in->z, scr->z, GWMUL_FFT_S1);					/* z^2 */
		gwsetmulbyconst (&ecmdata->gwdata, 2);
		gwmul3 (&ecmdata->gwdata, in->x, in->z, t3, GWMUL_MULBYCONST);					/* t3 = 2*x*z */

		gwadd3o (&ecmdata->gwdata, scr->x, scr->z, t2, GWADD_DELAY_NORMALIZE);				/* x^2 + z^2 */
		gwsub3o (&ecmdata->gwdata, t2, t3, t2, GWADD_DELAYNORM_IF(t1_safe));				/* t2 = x^2 - 2xz + z^2 */
		gwadd3o (&ecmdata->gwdata, t3, t3, t3, GWADD_FORCE_NORMALIZE);					/* t3 = 4*x*z */

		gwmul3 (&ecmdata->gwdata, t2, ecmdata->Ad4, t4, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);		/* t4 = t2 * Ad4 */
		gwaddmul4 (&ecmdata->gwdata, t2, t3, t4, out->x, GWMUL_FFT_S23 | GWMUL_STARTNEXTFFT);		/* outx = (t1 = t2 + t3) * t4 */
		gwaddmul4 (&ecmdata->gwdata, t4, t3, t3, out->z, GWMUL_STARTNEXTFFT);				/* outz = (t4 + t3) * t3 */
	}
}

/* Like ell_dbl_xz_scr, but the output arguments are not partially FFTed.  The input argument is assumed to never be used again. */

void ell_dbl_xz_scr_last (
	ecmhandle *ecmdata,
	struct xz *in,		/* Input value to double */
	struct xz *out,		/* Output value */
	struct xz *scr)		// Scratch registers (only the .x gwnum is used)
{				/* 10 or 11 FFTs, 4 adds */
	gwnum	t2, t3, t4;

	ASSERTG (scr != out);

	/* If we have extra_bits to square the output of a gwadd, then we can use an algorithm that minimizes FFTs. */
	if (square_safe (&ecmdata->gwdata, 1)) {
		t3 = scr->x;
		t2 = out->x;
		t4 = out->z;

		gwsetmulbyconst (&ecmdata->gwdata, 4);
		gwmul3 (&ecmdata->gwdata, in->x, in->z, t3, GWMUL_FFT_S12 | GWMUL_MULBYCONST | GWMUL_STARTNEXTFFT); /* t3 = 4*x*z */
		gwsub3o (&ecmdata->gwdata, in->x, in->z, t2, GWADD_DELAY_NORMALIZE);			/* Compute x - z */
		gwsquare2 (&ecmdata->gwdata, t2, t2, GWMUL_STARTNEXTFFT);				/* t2 = (x - z)^2 */
		gwmul3 (&ecmdata->gwdata, t2, ecmdata->Ad4, t4, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);	/* t4 = t2 * Ad4 */
		gwaddmul4 (&ecmdata->gwdata, t2, t3, t4, out->x, GWMUL_FFT_S23);			/* outx = (t1 = t2 + t3) * t4 */
		gwaddmul4 (&ecmdata->gwdata, t4, t3, t3, out->z, 0);					/* outz = (t4 + t3) * t3 */
	}

	/* This algorithm uses an extra FFT to assure that it will work even if there are no extra bits.  We've made a conscious decision to */
	/* spend an extra FFT to make ell_add as clean as possible.  Ell_adds are ten times more common than ell_dbls.  This version lets */
	/* ell_add FFT x and z without any worries. */
	else {
		int	t1_safe = addmul_safe (&ecmdata->gwdata, 2,0,0);

		t2 = out->x;
		t3 = out->z;
		t4 = scr->x;

		gwsquare2 (&ecmdata->gwdata, in->x, scr->x, GWMUL_FFT_S1);					/* x^2 */
		gwsquare2 (&ecmdata->gwdata, in->z, scr->z, GWMUL_FFT_S1);					/* z^2 */
		gwsetmulbyconst (&ecmdata->gwdata, 2);
		gwmul3 (&ecmdata->gwdata, in->x, in->z, t3, GWMUL_MULBYCONST);					/* t3 = 2*x*z */

		gwadd3o (&ecmdata->gwdata, scr->x, scr->z, t2, GWADD_DELAY_NORMALIZE);				/* x^2 + z^2 */
		gwsub3o (&ecmdata->gwdata, t2, t3, t2, GWADD_DELAYNORM_IF(t1_safe));				/* t2 = x^2 - 2xz + z^2 */
		gwadd3o (&ecmdata->gwdata, t3, t3, t3, GWADD_FORCE_NORMALIZE);					/* t3 = 4*x*z */

		gwmul3 (&ecmdata->gwdata, t2, ecmdata->Ad4, t4, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);		/* t4 = t2 * Ad4 */
		gwaddmul4 (&ecmdata->gwdata, t2, t3, t4, out->x, GWMUL_FFT_S23);				/* outx = (t1 = t2 + t3) * t4 */
		gwaddmul4 (&ecmdata->gwdata, t4, t3, t3, out->z, 0);						/* outz = (t4 + t3) * t3 */
	}
}

/* Adds Q=(in2.x:in2.z) and R=(in1.x:in1.z) and puts the result in (out.x:out.z).  Assumes that Q-R=P or R-Q=P where P=(diff.x:diff.z). */
/* Input arguments may be in FFTed format.  Out argument can be same as any of the 3 input arguments. */
/* Scratch xz argument cannot equal in1, in2, or diff. */

void ell_add_xz_scr (
	ecmhandle *ecmdata,
	struct xz *in1,
	struct xz *in2,
	struct xz *diff,
	struct xz *out,
	struct xz *scr)		// Scratch registers
{				/* 12 FFTs, 6 adds */
	gwnum	t1, t2;
	int	options;

	ASSERTG (scr != in1 && scr != in2 && scr != diff);
	t1 = scr->z;
	t2 = scr->x;
	gwmulmulsub5 (&ecmdata->gwdata, in1->x, in2->z, in1->z, in2->x, t1, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);	/* t1 = x1z2 - z1x2 */
	gwmulmulsub5 (&ecmdata->gwdata, in1->x, in2->x, in1->z, in2->z, t2, GWMUL_STARTNEXTFFT);			/* t2 = x1x2 - z1z2 */
	gwsquare2 (&ecmdata->gwdata, t1, t1, GWMUL_STARTNEXTFFT);				/* t1 = t1^2 */
	gwsquare2 (&ecmdata->gwdata, t2, t2, GWMUL_STARTNEXTFFT);				/* t2 = t2^2 */
	options = (diff == out) ? GWMUL_STARTNEXTFFT : GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT;
	gwmul3 (&ecmdata->gwdata, t2, diff->z, t2, options);					/* t2 = t2 * zdiff (will become outx) */
	gwmul3 (&ecmdata->gwdata, t1, diff->x, t1, options);					/* t1 = t1 * xdiff (will become outz) */
	if (out != scr) xzswap (*scr, *out);
}

// Like ell_add_xz_scr except that out is not equal any input (including diff) so that scratch register can be inferred.
// This is simply a shortcut for better readability.

#define ell_add_xz(h,i1,i2,dif,o)	ell_add_xz_scr(h,i1,i2,dif,o,o);

// Like ell_add_xz_scr except that a scratch register is allocated.

int ell_add_xz_noscr (
	ecmhandle *ecmdata,
	struct xz *in1,
	struct xz *in2,
	struct xz *diff,
	struct xz *out)
{
	struct xz scr;
	if (!alloc_xz (ecmdata, &scr)) return (OutOfMemory (ecmdata->thread_num));
	ell_add_xz_scr (ecmdata, in1, in2, diff, out, &scr);
	free_xz (ecmdata, &scr);
	return (0);
}

/* Like ell_add_xz_scr but in1, in2 and diff are assumed to not be used again.  Out argument never has its forward FFT begun. */

void ell_add_xz_last (
	ecmhandle *ecmdata,
	struct xz *in1,
	struct xz *in2,
	struct xz *diff,
	struct xz *out,
	struct xz *scr)
{				/* 12 FFTs, 6 adds */
	gwnum	t1, t2;

	ASSERTG (scr != in1 && scr != in2 && scr != diff);
	t1 = scr->z;
	t2 = scr->x;
	gwmulmulsub5 (&ecmdata->gwdata, in1->x, in2->z, in1->z, in2->x, t1, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);	/* t1 = x1z2 - z1x2 */
	gwmulmulsub5 (&ecmdata->gwdata, in1->x, in2->x, in1->z, in2->z, t2, GWMUL_STARTNEXTFFT);			/* t2 = x1x2 - z1z2 */
	gwsquare2 (&ecmdata->gwdata, t1, t1, GWMUL_STARTNEXTFFT);				/* t1 = t1^2 */
	gwsquare2 (&ecmdata->gwdata, t2, t2, GWMUL_STARTNEXTFFT);				/* t2 = t2^2 */
	gwmul3 (&ecmdata->gwdata, t2, diff->z, t2, 0);						/* t2 = t2 * zdiff (will become outx) */
	gwmul3 (&ecmdata->gwdata, t1, diff->x, t1, 0);						/* t1 = t1 * xdiff (will become outz) */
	if (out != scr) xzswap (*scr, *out);
}

/* Like ell_add_xz but in2 is normalized (z value is one) */

void ell_add_xz_1norm (
	ecmhandle *ecmdata,
	struct xz *in1,		// Not normalized
	gwnum	in2,		// Normalized in2.x, in2.z is 1
	struct xz *diff,	// Not normalized
	struct xz *out)		// Output -- cannot be same as in1, in2, or diff
{				/* 12 FFTs */
	gwnum	t1, t2;

	ASSERTG (out != in1 && out->x != in2 && out->z != in2 && out != diff);

	t1 = out->z;
	gwmulsub4 (&ecmdata->gwdata, in1->z, in2, in1->x, t1, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);	/* t1 = z1x2 - x1 */
	gwsquare2 (&ecmdata->gwdata, t1, t1, GWMUL_STARTNEXTFFT);				/* t1 = t1^2 */
	gwmul3 (&ecmdata->gwdata, t1, diff->x, t1, GWMUL_STARTNEXTFFT);				/* t1 = t1 * xdiff (will become outz) */

	t2 = out->x;
	gwmulsub4 (&ecmdata->gwdata, in1->x, in2, in1->z, t2, GWMUL_STARTNEXTFFT);		/* t2 = x1x2 - z1 */
	gwsquare2 (&ecmdata->gwdata, t2, t2, GWMUL_STARTNEXTFFT);				/* t2 = t2^2 (will become outx) */
	gwmul3 (&ecmdata->gwdata, t2, diff->z, t2, 0);						/* t2 = t2 * zdiff (will become outx) */
}

/* Like ell_add_xz but in2 and diff are normalized (z values are one) */

void ell_add_xz_2norm (
	ecmhandle *ecmdata,
	struct xz *in1,		// Not normalized
	gwnum	in2,		// Normalized in2.x, in2.z is 1
	gwnum	diff,		// Normalized diff.x, diff.z is 1
	struct xz *out)		// Output -- cannot be same as in1 or in2, out->x can be same as diff
{				/* 10 FFTs */
	gwnum	t1, t2;

	ASSERTG (out != in1 && out->x != in2 && out->z != in2 && out->z != diff);

	t1 = out->z;
	gwmulsub4 (&ecmdata->gwdata, in1->z, in2, in1->x, t1, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);	/* t1 = z1x2 - x1 */
	gwsquare2 (&ecmdata->gwdata, t1, t1, GWMUL_STARTNEXTFFT);				/* t1 = t1^2 */
	gwmul3 (&ecmdata->gwdata, t1, diff, t1, GWMUL_STARTNEXTFFT);				/* t1 = t1 * xdiff (will become outz) */

	t2 = out->x;
	gwmulsub4 (&ecmdata->gwdata, in1->x, in2, in1->z, t2, GWMUL_STARTNEXTFFT);		/* t2 = x1x2 - z1 */
	gwsquare2 (&ecmdata->gwdata, t2, t2, GWMUL_STARTNEXTFFT);				/* t2 = t2^2 (will become outx) */
}

/* Like ell_add_xz but in1, in2 and diff are normalized (z values are one) */

void ell_add_xz_3norm (
	ecmhandle *ecmdata,
	gwnum	in1,		// Normalized in1.x, in1.z is 1
	gwnum	in2,		// Normalized in2.x, in2.z is 1
	gwnum	diff,		// Normalized diff.x, diff.z is 1
	struct xz *out)		// Output -- cannot be same as in1 or in2, out->x&z can be same as diff
{				/* 8 FFTs */
	gwnum	t1, t2;

	ASSERTG (out->x != in1 && out->z != in1 && out->x != in2 && out->z != in2);

	t1 = out->z;
	gwsubmul4 (&ecmdata->gwdata, in1, in2, diff, t1, GWMUL_STARTNEXTFFT);			/* t1 = (x1 - x2) * xdiff */
	gwsubmul4 (&ecmdata->gwdata, in1, in2, t1, t1, GWMUL_STARTNEXTFFT);			/* t1 = (x1 - x2) * t1 (will become outz) */

	t2 = out->x;
	// Emulate GWMUL_ADDINCONST with gwmulsub4 when necessary.  This is faster than having gwnum do the emulation and can save a gwnum.
	if (is_gwsetaddin_emulated (&ecmdata->gwdata))
		gwmulsub4 (&ecmdata->gwdata, in1, in2, ecmdata->gwdata.GW_FFT1, t2, GWMUL_STARTNEXTFFT);/* t2 = x1x2 - 1 */
	else
		gwmul3 (&ecmdata->gwdata, in1, in2, t2, GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);	/* t2 = x1x2 - 1 */
	gwsquare2 (&ecmdata->gwdata, t2, t2, GWMUL_STARTNEXTFFT);				/* t2 = t2^2 (will become outx) */
}


/* Perform an elliptic multiply using an algorithm developed by Peter Montgomery.  Basically, we try to find a near optimal Lucas */
/* chain of additions that generates the number we are multiplying by.  This minimizes the number of calls to ell_dbl and ell_add. */

/* The costing function assigns an ell_dbl call a cost of 10 and an ell_add call a cost of 12. */
/* This cost estimates the number of forward and inverse transforms performed. */

#define swap(a,b)	{uint64_t t=a;a=b;b=t;}

int lucas_cost (
	uint64_t n,
	uint64_t d)
{
	uint64_t e;//, dmod3, emod3;
	unsigned long c;

	if (d >= n || d <= n/2) return (999999999);		/* Catch invalid costings */

	c = 0;
	while (n != 1) {
	    e = n - d;
	    d = d - e;

	    c += 12;

	    while (d != e) {
		if (d < e) {
			swap (d,e);
		}
//		if (d <= e + (e >> 2)) {
//			if ((dmod3 = d%3) == 3 - (emod3 = e%3)) {
//				t = d;
//				d = (d+d-e)/3;
//				e = (e+e-t)/3;
//				c += 36;
//				continue;
//			}
//			if (dmod3 == emod3 && (d&1) == (e&1)) {
//				d = (d-e) >> 1;
//				c += 22;
//				continue;
//			}
//		}
		if (100 * d <= 296 * e) {
			d = d-e;
			c += 12;
		} else if ((d&1) == (e&1)) {
			d = (d-e) >> 1;
			c += 22;
		} else if ((d&1) == 0) {
			d = d >> 1;
			c += 22;
//		} else if ((dmod3 = d%3) == 0) {
//			d = d/3-e;
//			c += 46;
//		} else if (dmod3 == 3 - (emod3 = e%3)) {
//			d = (d-e-e)/3;
//			c += 46;
//		} else if (dmod3 == emod3) {
//			d = (d-e)/3;
//			c += 46;
		} else {
			e = e >> 1;
			c += 22;
		}
	    }
	    c += 10;
	    if (d == 1) break;
	    n = d;
	    d = (uint64_t) ((double) n * 0.6180339887498948);
	}

	return (c);
}

int lucas_mul (
	ecmhandle *ecmdata,
	struct xz *A,
	uint64_t n,
	uint64_t d,
	int	last_mul)	// TRUE if the last multiply should not use the GWMUL_STARTNEXTFFT option
{
	uint64_t e;//, dmod3, emod3;
	struct xz B, C, T;//, S

	if (!alloc_xz (ecmdata, &B)) goto oom;
	if (!alloc_xz (ecmdata, &C)) goto oom;
//      if (!alloc_xz (ecmdata, &S)) goto oom;
	if (!alloc_xz (ecmdata, &T)) goto oom;

	while (n != 1) {
	    ell_dbl_xz_scr (ecmdata, A, &B, &C);				/* B = 2*A, scratch reg = C */
										/* C = A (but we delay setting that up) */

	    e = n - d;
	    d = d - e;

	    // To save two gwcopies setting C=A, we handle the most common case for the first iteration of the following loop.
	    // I've only seen three cases that end up doing the gwcopies, n=3, n=11 and n=17.  With change to 2.96 there are a couple more.
	    if (e > d && 100 * e <= 296 * d) {
		swap (d, e);
		xzswap (*A, B);							/* swap A & B, thus diff C = B */
		ell_add_xz (ecmdata, A, &B, &B, &C);				/* B = A+B */
		xzswap (B, C);							/* C = B */
		d = d-e;
	    }
	    else if (d > e && 100 * d <= 296 * e) {
		ell_add_xz (ecmdata, A, &B, A, &C);				/* B = A+B */
		xzswap (B, C);							/* C = B */
		d = d-e;
	    } else {
		gwcopy (&ecmdata->gwdata, A->x, C.x);
		gwcopy (&ecmdata->gwdata, A->z, C.z);				/* C = A */
	    }

	    while (d != e) {
		if (d < e) {
			swap (d, e);
			xzswap (*A, B);
		}
		// These cases were in Peter Montgomery's original PRAC implementation.  I've found that we do fewer FFTs with
		// these cases removed.  Should they be added back in the if statements above for gwcopies and in lucas_cost need to be restored.
		// SHOULD RE-EVALUATE with addition of d > 4 * e check (where 4 may not be optimal)
//		if (d <= e + (e >> 2)) {
//			if ((dmod3 = d%3) == 3 - (emod3 = e%3)) {
//				ell_add_xz (ecmdata, A, &B, &C, &S);		/* S = A+B */
//				ell_add_xz (ecmdata, A, &S, &B, &T);		/* T = A+S */
//				ell_add_xz_scr (ecmdata, &S, &B, A, &B, &T);	/* B = B+S, scratch reg = T */
//				xzswap (T, *A);					/* A = T */
//				t = d;
//				d = (d+d-e)/3;
//				e = (e+e-t)/3;
//				continue;
//			}
//			if (dmod3 == emod3 && (d&1) == (e&1)) {
//				ell_add_xz_scr (ecmdata, A, &B, &C, &B, &T);	/* B = A+B, scratch reg = T */
//				ell_dbl_xz_scr (ecmdata, A, A, &T);		/* A = 2*A, scratch reg = T */
//				d = (d-e) >> 1;
//				continue;
//			}
//		}
		if (100 * d <= 296 * e) {					/* d <= 2.96 * e (Montgomery used 4.00) */
			ell_add_xz_scr (ecmdata, A, &B, &C, &C, &T);		/* B = A+B, scratch reg = T */
			xzswap (B, C);						/* C = B */
			d = d-e;
		} else if ((d&1) == (e&1)) {
			ell_add_xz_scr (ecmdata, A, &B, &C, &B, &T);		/* B = A+B, scratch reg = T */
			ell_dbl_xz_scr (ecmdata, A, A, &T);			/* A = 2*A, scratch reg = T */
			d = (d-e) >> 1;
		} else if ((d&1) == 0) {
			ell_add_xz_scr (ecmdata, A, &C, &B, &C, &T);		/* C = A+C, scratch reg = T */
			ell_dbl_xz_scr (ecmdata, A, A, &T);			/* A = 2*A, scratch reg = T */
			d = d >> 1;
		}
		// These cases were in Peter Montgomery's original PRAC implementation.  I've found that optimal addition chains
		// rarely (never?) use these rules.  Should they be added back lucas_cost needs to be restored too.
		// SHOULD RE-EVALUATE with addition of d > 4 * e check (where 4 may not be optimal)
//		else if ((dmod3 = d%3) == 0) {
//			ell_dbl_xz_scr (ecmdata, A, &S, &T);			/* S = 2*A, scratch reg = T */
//			ell_add_xz (ecmdata, A, &B, &C, &T);			/* T = A+B */
//			ell_add_xz_scr (ecmdata, &S, &T, &C, &C, &T);		/* B = S+T, scratch reg = T */
//			ell_add_xz_scr (ecmdata, &S, A, A, A, &S);		/* A = S+A, scratch reg = S */
//			xzswap (B, C);						/* C = B */
//			d = d/3-e;
//		} else if (dmod3 == 3 - (emod3 = e%3)) {
//			ell_add_xz (ecmdata, A, &B, &C, &S);			/* S = A+B */
//			ell_add_xz_scr (ecmdata, A, &S, &B, &B, &T);		/* B = A+S, scratch reg = T */
//			ell_dbl_xz_scr (ecmdata, A, &S, &T);			/* S = 2*A, scratch reg = T */
//			ell_add_xz_scr (ecmdata, &S, A, A, A, &T);		/* A = S+A, scratch reg = T */
//			d = (d-e-e)/3;
//		} else if (dmod3 == emod3) {
//			ell_add_xz (ecmdata, A, &B, &C, &T);			/* T = A+B */
//			ell_add_xz_scr (ecmdata, A, &C, &B, &C, &S);		/* C = A+C, scratch reg = S */
//			xzswap (T, B);						/* B = T */
//			ell_dbl_xz_scr (ecmdata, A, &S, &T);			/* S = 2*A, scratch reg = T */
//			ell_add_xz_scr (ecmdata, &S, A, A, A, &T);		/* A = S+A, scratch reg = T */
//			d = (d-e)/3;
//		}
		else {
			ell_add_xz_scr (ecmdata, &B, &C, A, &C, &T);		/* C = C-B, scratch reg = T */
			ell_dbl_xz_scr (ecmdata, &B, &B, &T);			/* B = 2*B, scratch reg = T */
			e = e >> 1;
		}
	    }

	    if (d == 1 && last_mul) {
		ell_add_xz_last (ecmdata, &B, A, &C, A, &T);			/* A = A+B, scratch reg = T */
		break;
	    } else {
		ell_add_xz_scr (ecmdata, &B, A, &C, A, &T);			/* A = A+B, scratch reg = T */
	    }

	    n = d;
	    d = (uint64_t) ((double) n * 0.6180339887498948);
	}
	free_xz (ecmdata, &B);
	free_xz (ecmdata, &C);
//      free_xz (ecmdata, &S);
	free_xz (ecmdata, &T);
	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}
#undef swap

/* Try a series of Lucas chains to find the cheapest. */
/* First try v = (1+sqrt(5))/2, then (2+v)/(1+v), then (3+2*v)/(2+v), */
/* then (5+3*v)/(3+2*v), etc.  Finally, execute the cheapest. */

__inline int lucas_cost_several (uint64_t n, uint64_t *d) {
	int	i, c, min;
	uint64_t testd;
	for (i = 0, testd = *d - PRAC_SEARCH / 2; i < PRAC_SEARCH; i++, testd++) {
		c = lucas_cost (n, testd);
		if (i == 0 || c < min) min = c, *d = testd;
	}
	return (min);
}

int ell_mul (
	ecmhandle *ecmdata,
	struct xz *arg,
	uint64_t n,
	int	last_mul)	// TRUE if the this is the last mul in a series and we do not want to apply the GWMUL_STARTNEXTFFT option to the result
{
	unsigned long zeros;
	int	stop_reason;

	for (zeros = 0; (n & 1) == 0; zeros++) n >>= 1;

	if (n > 1) {
		int	c, min;
		uint64_t d, mind;

		mind = (uint64_t) ceil((double) 0.6180339887498948 * n);		/*v=(1+sqrt(5))/2*/
		min = lucas_cost_several (n, &mind);

		d = (uint64_t) ceil ((double) 0.7236067977499790 * n);			/*(2+v)/(1+v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.5801787282954641 * n);			/*(3+2*v)/(2+v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6328398060887063 * n);			/*(5+3*v)/(3+2*v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6124299495094950 * n);			/*(8+5*v)/(5+3*v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6201819808074158 * n);			/*(13+8*v)/(8+5*v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6172146165344039 * n);			/*(21+13*v)/(13+8*v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6183471196562281 * n);			/*(34+21*v)/(21+13*v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6179144065288179 * n);			/*(55+34*v)/(34+21*v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6180796684698958 * n);			/*(89+55*v)/(55+34*v)*/
		c = lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		stop_reason = lucas_mul (ecmdata, arg, n, mind, zeros == 0 && last_mul);
		if (stop_reason) return (stop_reason);
	}
	while (zeros--) {
		struct xz scr;
		if (!alloc_xz (ecmdata, &scr)) return (OutOfMemory (ecmdata->thread_num));
		if (zeros || !last_mul) ell_dbl_xz_scr (ecmdata, arg, arg, &scr);
		else ell_dbl_xz_scr_last (ecmdata, arg, arg, &scr);
		free_xz (ecmdata, &scr);
	}
	return (0);
}

/**************************************************************
 *	Modular inverse functions 
 **************************************************************/

/* Computes the modular inverse of a number.  This is done using the extended GCD algorithm.  If a factor is accidentally found, it is */
/* returned in factor.  Function returns stop_reason if it was interrupted by an escape. */

int ecm_modinv (
	ecmhandle *ecmdata,
	gwnum	b)
{
	giant	v;

/* Convert input number to binary */

	v = popg (&ecmdata->gwdata.gdata, ((int) ecmdata->gwdata.bit_length >> 5) + 10);
	if (v == NULL) goto oom;
	if (gwtogiant (&ecmdata->gwdata, b, v)) {
		// On unexpected, should-never-happen error, return out-of-memory for lack of a better error message
		goto oom;
	}

#ifdef MODINV_USING_GIANTS

	int	stop_reason;

/* Let the invg code use gwnum b's memory.  This code is slower, but at least it is interruptible. */
/* Compute 1/v mod N */

	gwfree_temporarily (&ecmdata->gwdata, b);
	stop_reason = invgi (&ecmdata->gwdata.gdata, ecmdata->thread_num, ecmdata->N, v);
	if (stop_reason == GIANT_OUT_OF_MEMORY)
		stop_reason = OutOfMemory (ecmdata->thread_num);
	gwrealloc_temporarily (&ecmdata->gwdata, b);
	if (stop_reason) {
		pushg (&ecmdata->gwdata.gdata, 1);
		return (stop_reason);
	}

/* If a factor was found, save it in FAC */

	if (v->sign < 0) {
		negg (v);
		ecmdata->factor = allocgiant (v->sign);
		if (ecmdata->factor == NULL) goto oom;
		gtog (v, ecmdata->factor);
	}

/* Otherwise, convert the inverse to FFT-ready form */

	else {
		ecmdata->factor = NULL;
		gianttogw (&ecmdata->gwdata, v, b);
	}

/* Use the faster GMP library to do an extended GCD which gives us 1/v mod N */

#else
	{
	mpz_t	__v, __N, __gcd, __inv;

/* Do the extended GCD */

	mpz_init (__v);
	mpz_init (__N);
	mpz_init (__gcd);
	mpz_init (__inv);
	gtompz (v, __v);
	gtompz (ecmdata->N, __N);
	mpz_gcdext (__gcd, __inv, NULL, __v, __N);
	mpz_clear (__v);

/* If a factor was found (gcd != 1 && gcd != N), save it in FAC */

	if (mpz_cmp_ui (__gcd, 1) && mpz_cmp (__gcd, __N)) {
		ecmdata->factor = allocgiant ((int) divide_rounding_up (mpz_sizeinbase (__gcd, 2), 32));
		if (ecmdata->factor == NULL) goto oom;
		mpztog (__gcd, ecmdata->factor);
	}

/* Otherwise, convert the inverse to FFT-ready form */

	else {
		ecmdata->factor = NULL;
		if (mpz_sgn (__inv) < 0) mpz_add (__inv, __inv, __N);
		mpztog (__inv, v);
		gianttogw (&ecmdata->gwdata, v, b);
	}

/* Cleanup and return */

	mpz_clear (__gcd);
	mpz_clear (__inv);
	mpz_clear (__N);
	}
#endif

/* Clean up */

	pushg (&ecmdata->gwdata.gdata, 1);

/* Increment count and return */

	ecmdata->modinv_count++;
	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}

/* Takes a point (a,b) and multiplies it by a value such that b will be one */
/* If we accidentally find a factor it is returned in factor.  Function */
/* returns stop_reason if it was interrupted by an escape. */

int normalize (
	ecmhandle *ecmdata,
	gwnum	a,
	gwnum	b)
{
	int	stop_reason;

/* Compute the modular inverse and scale up the first input value */

	stop_reason = ecm_modinv (ecmdata, b);
	if (stop_reason) return (stop_reason);
	if (ecmdata->factor != NULL) return (0);
	gwmul3 (&ecmdata->gwdata, b, a, a, 0);
	return (0);
}

/* Initialize the normalize (modular inverse) pool. */

int normalize_pool_init (
	ecmhandle *ecmdata,
	uint64_t max_pool_size)
{
	ecmdata->pool_count = 0;
	ecmdata->poolz_count = 0;
	ecmdata->pool_values = NULL;
	ecmdata->poolz_values = NULL;
	ecmdata->pool_modinv_value = NULL;
	ecmdata->use_preallocated_norm_temps = FALSE;

/* Allocate memory for modular inverse pooling arrays of pointers to gwnums */

	ecmdata->pool_values = (gwnum *) malloc ((size_t) (max_pool_size * sizeof (gwnum)));
	if (ecmdata->pool_values == NULL) goto oom;
	if (ecmdata->pool_type == POOL_3MULT) {		// 3MULT pooling requires 100% more gwnums
		ecmdata->poolz_values = (gwnum *) malloc ((size_t) (max_pool_size * sizeof (gwnum)));
		if (ecmdata->poolz_values == NULL) goto oom;
	}
	if (ecmdata->pool_type == POOL_3POINT44MULT) {	// 3POINT44MULT pooling requires 33% plus a couple more gwnums
		ecmdata->poolz_values = (gwnum *) malloc ((size_t) ((divide_rounding_up (max_pool_size, 3) + 2) * sizeof (gwnum)));
		if (ecmdata->poolz_values == NULL) goto oom;
	}
	if (ecmdata->pool_type == POOL_3POINT57MULT) {	// 3POINT57MULT pooling requires 14% plus 5 more gwnums
		ecmdata->poolz_values = (gwnum *) malloc ((size_t) ((divide_rounding_up (max_pool_size, 7) + 5) * sizeof (gwnum)));
		if (ecmdata->poolz_values == NULL) goto oom;
	}

/* All done */

	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}

/* Free data associated with the normalize (modular inverse) pool. */

void normalize_pool_term (
	ecmhandle *ecmdata)
{
	if (ecmdata->poolz_values != NULL) {
		if (!ecmdata->use_preallocated_norm_temps) for (uint32_t i = 0; i < ecmdata->poolz_count; i++) gwfree (&ecmdata->gwdata, ecmdata->poolz_values[i]);
		free (ecmdata->poolz_values); ecmdata->poolz_values = NULL;
	}

	free (ecmdata->pool_values); ecmdata->pool_values = NULL;
	if (!ecmdata->use_preallocated_norm_temps) gwfree (&ecmdata->gwdata, ecmdata->pool_modinv_value); ecmdata->pool_modinv_value = NULL;
	ecmdata->pool_count = 0;
	ecmdata->poolz_count = 0;
}

/* Adds a point (a,b) to the list of numbers that need normalizing. */
/* This is done in such a way as to minimize the amount of memory used. */

/* This is an interesting bit of code with a variety of algorithms available.  Assuming there are N pairs to normalize, then you can: */
/* 1) Use 2*N memory and 3 multiplies per point. */
/* 2) Use 1.33*N memory and use 3.444 multiplies per point. */
/* 3) Use 1.14*N memory and use 3.573 multiplies per point. */
/* 4) Use N memory and use O(N^2) multiplies per point. */

// The new add_to_normalize_pool interface. "_ro" stands for relinquish ownership.  Caller relinquishes ownership of b.  Saves a gwcopy.
int add_to_normalize_pool_ro (
	ecmhandle *ecmdata,
	gwnum	a,		/* Number to multiply by 1/b.  This input can be in a full or partial FFTed state.  Not preserved! */
	gwnum	b)		/* Preserved, will not be FFTed in place */
{
#define normalloc(d)		if(!ecmdata->use_preallocated_norm_temps){d = gwalloc (&ecmdata->gwdata); if (d == NULL) goto oom;} \
				else {ASSERTG(ecmdata->norm_pool_temps_count > 0); d = ecmdata->norm_pool_temps[--ecmdata->norm_pool_temps_count];}
#define normfree(d)		if(!ecmdata->use_preallocated_norm_temps){gwfree (&ecmdata->gwdata, d);} \
				else {ASSERTG(ecmdata->norm_pool_temps_count < 5); ecmdata->norm_pool_temps[ecmdata->norm_pool_temps_count++] = d;}
#define allocAndFFTb(d)		{gwfft(&ecmdata->gwdata,b,b);d=b;}
#define allocAndCopyb(d)	d=b
#define copyAndFFTb(d)		{gwswap(b,d);normfree(b);}

/* Switch off the type of pooling we are going to do */

	switch (ecmdata->pool_type) {

/* Implement 3-multiply algorithm with 100% memory overhead */

	case POOL_3MULT:

/* If this is the first call allocate memory for the gwnum we use */

		if (ecmdata->pool_count == 0) {
			allocAndFFTb (ecmdata->pool_modinv_value);
		}

/* Otherwise, multiply a by the accumulated b values */

		else {
			// Accumulate previous b value (delayed so that we can use GWMUL_STARTNEXTFFT on all but last b value)
			if (ecmdata->pool_count > 1) {
				gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			}
			// Multiply a by accumulated b values
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, a, a, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// Remember b
			allocAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count++]);
		}

/* Add a to array of values to normalize */

		ecmdata->pool_values[ecmdata->pool_count++] = a;
		break;

/* Implement 3.44-multiply algorithm with 33% memory overhead */
// The basic algorithm here takes 4 (a,b) points and converts them as follows:
//	a1							b1			one copy
//	a1*b2	    a2*b1					b1*b2			7 FFTs (FFT b1, FFT b2, FFTINVFFT a1*b2, FFTINVFFT a2*b1, INVFFT b1*b2)
//	a1*b2	    a2*b1       a3,b3				b1*b2
//	a1*b2       a2*b1       a3*b4,b3*b4 a4*b3		b1*b2			7 FFTs (FFT b3, FFT b4, FFTINVFFT a3*b4, FFTINVFFT a4*b3, INVFFT b3*b4)
//	a1*b2*b3*b4 a2*b1*b3*b4 a3*b4*b1*b2 a4*b3*b1*b2		b1*b2*b3*b4		11 FFTs (FFT b1*b2, FFT b3*b4, FFTINVFFT a1* a2* a3* a4*, INVFFT b1*b2*b3*b4)
//	inv = 1/(b1*b2*b3*b4)
//	a4 *= inv									9 FFTs (FFT inv, FFTINVFFT a1* a2* a3* a4*)
//	a3 *= inv								
//	a2 *= inv
//	a1 *= inv
// The trick is that subsequent groups of 4 operate on (1,product_of_b) and 3 more (a,b) points.  For example, the steps needed for the next 3 (a,b) points:
// On 5th input:
//	let b0 = pooled_modinv
//	fft b5 to alloc poolz[0] - will become 1/(b1*b2*b3*b4)
//	pool[4] = a5*b0 will become a5/b5
//	pooled_modinv *= b5
//	b5	    a5*b0					b0*b5			5 FFTs (FFT b0, FFT b5, FFTINVFFT a5*b0, INVFFT b0*b5)
// On 6th input:
//	fft b6 to tmp1, will free later
//	pool[5] = a6 will become a6/b6
//	b5	    a5*b0       a6,b6				b0*b5
// On 7th input:
//	tmp2 = fft b7
//	pool[5] = a6*b7
//	pool[6] = a7*b6 will become a6/b6
//	tmp1 = b6*b7
//	b5       a5*b0       a6*b7,b6*b7 a7*b6			b0*b5			7 FFTs (FFT b6, FFT b7, FFTINVFFT a6*b7, FFTINVFFT a7*b6, INVFFT b6*b7)
//	poolz[0] *= b6*b7
//	pool[4] *= b6*b7
//	pool[5] *= b0*b5
//	pool[6] *= b0*b5
//	pooled_modinv *= b6*b7
//	free tmp1,tmp2
//	b5*b6*b7 a5*b0*b6*b7 a6*b7*b0*b5 a7*b6*b0*b5		b0*b5*b6*b7		10 FFTs (FFT b0*b5, FFT b6*b7, INVFFT b5*, FFTINVFFT a5* a6* a7*, INVFFT b0*b5*b6*b7)
//	inv = 1/(b0*b5*b6*b7)
//	a7 *= inv									9 FFTs (FFT inv, FFTINVFFT a5* a5* a6* a7*)
//	a6 *= inv								
//	a5 *= inv
//	poolz[0] *= inv
// Careful counting shows that each 3 additional points costs 31 FFTs

	case POOL_3POINT44MULT:

/* Handle the first call for the first set of four points.  Allocate and set pool_modinv_value (the product_of_b's) */

		if (ecmdata->pool_count == 0) {
			// FFT b1
			allocAndFFTb (ecmdata->pool_modinv_value);
		}

/* Handle the second call for the first set of four points */

		else if (ecmdata->pool_count == 1) {
			// FFT b2 (temporarily store in poolz)
			allocAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count++]);
			// a1 *= b2
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_values[0], GWMUL_STARTNEXTFFT);
			// a2 *= b1
			gwmul3 (&ecmdata->gwdata, a, ecmdata->pool_modinv_value, a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool_modinv_value *= b2 (delay this multiply so that we can use startnextfft)
			//gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
		}

/* Handle the third call for the first set of four points */

		else if (ecmdata->pool_count == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// FFT b3 (temporarily store in poolz)
			copyAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count-1]);
		}

/* Handle the fourth call for the first set of four points */

		else if (ecmdata->pool_count == 3) {
			gwnum	tmp1, tmp2;
			// FFT b4
			allocAndFFTb (tmp2);
			// a3 *= b4
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], tmp2, ecmdata->pool_values[2], GWMUL_STARTNEXTFFT);
			// a4 *= b3
			tmp1 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			gwmul3 (&ecmdata->gwdata, a, tmp1, a, GWMUL_STARTNEXTFFT);
			// tmp1 = b3*b4
			gwmul3 (&ecmdata->gwdata, tmp1, tmp2, tmp1, GWMUL_STARTNEXTFFT);
			normfree (tmp2);
			// pool[0] *= b3*b4
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], tmp1, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool[1] *= b3*b4
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], tmp1, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// pool[2] *= b1*b2
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], ecmdata->pool_modinv_value, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// a4 *= b1*b2
			gwmul3 (&ecmdata->gwdata, a, ecmdata->pool_modinv_value, a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pooled_modinv *= b3*b4 (delay this multiply so that we can use startnextfft)
			//gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, tmp1, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
		}

/* Handle the first call for a new set of three points */

		else if (ecmdata->pool_count % 3 == 1) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// FFT b5
			copyAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count-1]);
			// a5 *= pool_modinv_value
			gwmul3 (&ecmdata->gwdata, a, ecmdata->pool_modinv_value, a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool_modinv_value *= b5 (delay this multiply so that we can use startnextfft)
			//gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
		}

/* Handle the second call for a set of three points */

		else if (ecmdata->pool_count % 3 == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// FFT b6 (temporarily store in poolz)
			allocAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count++]);
		}

/* Handle the third call for a set of three points */

		else {
			gwnum	tmp1, tmp2;
			// FFT b7
			allocAndFFTb (tmp2);
			// a6 *= b7
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], tmp2, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_STARTNEXTFFT);
			// a7 *= b6
			tmp1 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			gwmul3 (&ecmdata->gwdata, a, tmp1, a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// tmp1 = b6*b7
			gwmul3 (&ecmdata->gwdata, tmp1, tmp2, tmp1, GWMUL_STARTNEXTFFT);
			normfree (tmp2);
			// poolz[0] *= b6*b7
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-2], tmp1, ecmdata->poolz_values[ecmdata->poolz_count-2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool[4] *= b6*b7
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], tmp1, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_STARTNEXTFFT);
			// pool[5] *= b0*b5
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// a7 *= b0*b5
			gwmul3 (&ecmdata->gwdata, a, ecmdata->pool_modinv_value, a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pooled_modinv *= b6*b7 (delay this so we can use startnextfft)
			// gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, tmp1, ecmdata->pool_modinv_value, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
		}

/* Add a to array of values to normalize */

		ecmdata->pool_values[ecmdata->pool_count++] = a;
		break;

/* Implement 3.573-multiply algorithm with 14% memory overhead */
// The basic algorithm here takes 8 (a,b) points and converts them as follows:
//	a1							b1		one copy
//	a1*b2	    a2*b1					b1*b2		7 FFTs (FFT b1, FFT b2, FFTINVFFT a2*b1, INVFFT b1*b2)
//	a1*b2	    a2*b1       a3,b3				b1*b2
//	a1*b2       a2*b1       a3*b4,b3*b4 a4*b3		b1*b2		7 FFTs (FFT b3, FFT b4, FFTINVFFT a3*b4, FFTINVFFT a4*b3, INVFFT b3*b4)
//	a5,b5
//	a5*b6	    a6*b5					b5*b6		7 FFTs (FFT b5, FFT b6, FFTINVFFT a5*b6, FFTINVFFT a6*b5, INVFFT b5*b6)
//	a5*b6	    a6*b5       a7,b7				b5*b6
//	a5*b6       a6*b5       a7*b8,b7*b8 a8*b7		b5*b6		7 FFTs (FFT b7, FFT b8, FFTINVFFT a7*b8, FFTINVFFT a8*b7, INVFFT b7*b8)
//		b1*b2*b3*b4	b5*b6*b7*b8					6 FFTs (FFT b1*b2, FFT b3*b4, FFT b5*b6, FFT b7*b8, 2 INVFFT)
//		b12*b5678  b34*b5678  b56*b1234  b78*b1234			6 FFTs (FFT b1*b2*b3*b4, FFT b5*b6*b7*b8, 4 INVFFT)
//	a1*b2*b345678 a2*b1*b345678 a3*b4*b125678 a4*b3*b125678			10 FFTs (FFT b345678, FFT b125678, FFTINVFFT a1* a2* a3* a4*)
//	a5*b6*b123478 a6*b5*b123478 a7*b8*b123456 a8*b7*b123456			10 FFTs (FFT b123478, FFT b123456, FFTINVFFT a5* a6* a7* a8*)
//							b1*b2*b3*b4*b5*b6*b7*b8	1 FFT (INVFFT)
//	8 multiplies by inv							17 FFTs
// The trick is that subsequent groups of 8 operate on (1,product_of_b) and 7 more (a,b) points.  For example, the steps needed for the next 7 (a,b) points:
// On 9th input:
//	let b0 = pooled_modinv
//	fft b9 to alloc poolz[0] - will become 1/(b1*b2*b3*b4*b5*b6*b7*b8)
//	b9	    a9*b0					b0*b9		5 FFTs (FFT b0, FFT b9, FFTINVFFT a9*b0, INVFFT b0*b9)
// Then 3 pairs do this:
//	a10,b10
//	a10*b11,b10*b11 a11*b10							7 FFTs (FFT b10, FFT b11, FFTINVFFT a10*b11, FFTINVFFT a11*b10, INVFFT b10*b11)
//	a12,b12
//	a12*b13,b12*b13   a13*b12						7 FFTs (FFT b12, FFT b13, FFTINVFFT a12*b13, FFTINVFFT a13*b12, INVFFT b12*b13)
//	a14,b14			
//	a14*b15,b14*b15   a15*b14						7 FFTs (FFT b14, FFT b15, FFTINVFFT a14*b15, FFTINVFFT a15*b14, INVFFT b14*b15)
// After the last pair, we do a lot of combining:
//		b0*b9*b10*b11	b12*b13*b14*b15					6 FFTs (FFT b0*b9, FFT b10*b11, FFT b12*b13, FFT b14*b15, 2 INVFFT)
//		b02*b12131415  b1011*b12131415  b1213*b091011  b1415*b091011	6 FFTs (FFT b0*b9*b10*b11, FFT b12*b13*b14*b15, 4 INVFFT)
//	b9*b10-15  a9*b0*b10-15 a10*b11*b0212-15 a11*b10*b0212-15		9 FFTs (FFT b10-15, FFT b0212-15, INVFFT b9*, FFTINVFFT a9* a10* a11*)
//	a12*b13*b0910111415 a13*b0910111415 a14*b15*b09-13 a15*b14*b09-13	10 FFTs (FFT b0910111415, FFT b09-13, FFTINVFFT a12* a13* a14* a15*)
//						b0*b9*b10*b11*b12*b13*b14*b15	1 FFT (INVFFT)
//	8 multiplies by inv							17 FFTs
// Careful counting shows that each 7 additional points costs 75 FFTs

	case POOL_3POINT57MULT:

/* Handle the first call for the first set of eight points.  Allocate and set pool_modinv_value (the product_of_b's) */

		if (ecmdata->pool_count == 0) {
			// FFT b1
			allocAndFFTb (ecmdata->pool_modinv_value);
		}

/* Handle the second call for the first set of eight points */

		else if (ecmdata->pool_count == 1) {
			// FFT b2 (temporarily store in poolz)
			allocAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count++]);
			// a1 *= b2
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_values[0], GWMUL_STARTNEXTFFT);
			// a2 *= b1
			gwmul3 (&ecmdata->gwdata, a, ecmdata->pool_modinv_value, a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool_modinv_value *= b2 (delay this multiply so that we can use startnextfft)
			//gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
		}

/* Handle the third call for the first set of eight points */

		else if (ecmdata->pool_count == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// FFT b3 (temporarily store in poolz)
			copyAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count-1]);
		}

/* Handle the fourth and sixth call for the first set of eight points */

		else if (ecmdata->pool_count == 3 || ecmdata->pool_count == 5) {
			// FFT b4 or b6
			gwnum	tmp;
			allocAndFFTb (tmp);
			// a3 *= b4 or a5 *= b6
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], tmp, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// a4 *= b3 or a6 *= b5
			gwmul3 (&ecmdata->gwdata, a, ecmdata->poolz_values[ecmdata->poolz_count-1], a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b3 *= b4 or b5 *= b6
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], tmp, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_STARTNEXTFFT);
			normfree (tmp);
		}

/* Handle the fifth and seventh call for first set of eight points */

		else if (ecmdata->pool_count == 4 || ecmdata->pool_count == 6) {
			// FFT b5 or b7 (temporarily store in poolz)
			allocAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count++]);
		}

/* Handle the eighth call for first set of eight points */

		else if (ecmdata->pool_count == 7) {
			gwnum	b7, b8, b34, b56, b78, b5678, b6vals;
			// Allocate two more temporaries
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b7 = ecmdata->poolz_values[2];
			b56 = ecmdata->poolz_values[1];
			b34 = ecmdata->poolz_values[0];
			ecmdata->poolz_count = 0;
			// FFT b8
			allocAndFFTb (b8);
			// a7 *= b8
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], b8, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b78 = b7*b8
			gwmul3 (&ecmdata->gwdata, b7, b8, b8, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			b78 = b8;
			// a8 *= b7
			gwmul3 (&ecmdata->gwdata, a, b7, a, GWMUL_STARTNEXTFFT);
			// b56 * b78
			b5678 = b7;
			gwmul3 (&ecmdata->gwdata, b56, b78, b5678, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b34 * b5678
			gwmul3 (&ecmdata->gwdata, b34, b5678, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// (a1*b2)*b345678
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], b6vals, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a2*b1)*b345678
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], b6vals, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// b12 * b5678
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b5678, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a3*b4)*b125678
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], b6vals, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a4*b3)*b125678
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[3], b6vals, ecmdata->pool_values[3], GWMUL_STARTNEXTFFT);
			// b12 * b34
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b34, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// b78 * b1234
			gwmul3 (&ecmdata->gwdata, b78, ecmdata->pool_modinv_value, b6vals, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a5*b6)*b123478
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[4], b6vals, ecmdata->pool_values[4], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a6*b5)*b123478
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[5], b6vals, ecmdata->pool_values[5], GWMUL_STARTNEXTFFT);
			// b56 * b1234
			gwmul3 (&ecmdata->gwdata, b56, ecmdata->pool_modinv_value, b6vals, GWMUL_STARTNEXTFFT);
			// (a7*b8)*b123456
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[6], b6vals, ecmdata->pool_values[6], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a8*b7)*b123456
			gwmul3 (&ecmdata->gwdata, a, b6vals, a, GWMUL_STARTNEXTFFT);
			// b1*b2*b3*b4*b5*b6*b7*b8 (delay this multiply so that we can use startnextfft)
			//gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b5678, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			ecmdata->poolz_values[0] = b5678;
			ecmdata->poolz_count = 1;
			// Free temps
			normfree (b34);
			normfree (b56);
			normfree (b78);
			normfree (b6vals);
		}

/* Handle the first call for a new set of seven points */

		else if (ecmdata->pool_count % 7 == 1) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// FFT b9
			copyAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count-1]);
			// a9 *= pool_modinv_value
			gwmul3 (&ecmdata->gwdata, a, ecmdata->pool_modinv_value, a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool_modinv_value *= b9 (delay this multiply so that we can use startnextfft)
			//gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
		}

/* Handle the second call for a set of seven points */

		else if (ecmdata->pool_count % 7 == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// FFT b10 (temporarily store in poolz)
			allocAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count++]);
		}

/* Handle the third and fifth call for a set of seven points */

		else if (ecmdata->pool_count % 7 == 3 || ecmdata->pool_count % 7 == 5) {
			// FFT b11 or b13
			gwnum	tmp;
			allocAndFFTb (tmp);
			// a10 *= b11 or a12 *= b13
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], tmp, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// a11 *= b10 or a13 *= b12
			gwmul3 (&ecmdata->gwdata, a, ecmdata->poolz_values[ecmdata->poolz_count-1], a, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b10 *= b11 or b12 *= b13
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], tmp, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_STARTNEXTFFT);
			normfree (tmp);
		}

/* Handle the fourth and sixth call for a set of seven points */

		else if (ecmdata->pool_count % 7 == 4 || ecmdata->pool_count % 7 == 6) {
			// FFT b12 or b14 (temporarily store in poolz)
			allocAndFFTb (ecmdata->poolz_values[ecmdata->poolz_count++]);
		}

/* Handle the last call for a set of seven points */

		else {
			gwnum	b14, b15, b1011, b1213, b1415, b12131415, b6vals;
			// Allocate two more temporaries
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b14 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			b1213 = ecmdata->poolz_values[ecmdata->poolz_count-2];
			b1011 = ecmdata->poolz_values[ecmdata->poolz_count-3];
			ecmdata->poolz_count -= 3;
			// FFT b15
			allocAndFFTb (b15);
			// a14 *= b15
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], b15, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b1415 = b14*b15
			gwmul3 (&ecmdata->gwdata, b14, b15, b15, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			b1415 = b15;
			// a15 *= b14
			gwmul3 (&ecmdata->gwdata, a, b14, a, GWMUL_STARTNEXTFFT);
			// b1213 * b1415
			b12131415 = b14;
			gwmul3 (&ecmdata->gwdata, b1213, b1415, b12131415, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b1011 * b12131415
			gwmul3 (&ecmdata->gwdata, b1011, b12131415, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b9*b101112131415
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], b6vals, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a9*b0)*b101112131415
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-6], b6vals, ecmdata->pool_values[ecmdata->pool_count-6], GWMUL_STARTNEXTFFT);
			// b09 * b12131415
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b12131415, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a10*b11)*b0912131415
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-5], b6vals, ecmdata->pool_values[ecmdata->pool_count-5], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a11*b10)*b0912131415
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-4], b6vals, ecmdata->pool_values[ecmdata->pool_count-4], GWMUL_STARTNEXTFFT);
			// b09 * b1011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b1011, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// b1415 * b091011
			gwmul3 (&ecmdata->gwdata, b1415, ecmdata->pool_modinv_value, b6vals, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a12*b13)*b0910111415
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-3], b6vals, ecmdata->pool_values[ecmdata->pool_count-3], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a13*b12)*b0910111415
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], b6vals, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_STARTNEXTFFT);
			// b1213 * b091011
			gwmul3 (&ecmdata->gwdata, b1213, ecmdata->pool_modinv_value, b6vals, GWMUL_STARTNEXTFFT);
			// (a14*b15)*b0910111213
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], b6vals, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a15*b14)*b0910111213
			gwmul3 (&ecmdata->gwdata, a, b6vals, a, GWMUL_STARTNEXTFFT);
			// b0*b9*b10*b11*b12*b13*b14*b15 (delay this multiply so that we can use startnextfft)
			//gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b12131415, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			ecmdata->poolz_values[ecmdata->poolz_count++] = b12131415;
			// Free temps
			normfree (b1011);
			normfree (b1213);
			normfree (b1415);
			normfree (b6vals);
		}

/* Add a to array of values to normalize */

		ecmdata->pool_values[ecmdata->pool_count++] = a;
		break;

/* Implement N^2 algorithm using only 1 extra gwnum */
/* The algorithm costs (in FFTs):	(N+3)^2 - 13 + modinv_cost	*/
/* To find the break even point:					*/
/*	(N+3)^2 + modinv_cost = 2 * ((N/2+3)^2 + modinv_cost)		*/
/*	(N+3)^2 + modinv_cost = (N/2+3)^2 * 2 + 2 * modinv_cost		*/
/*	(N+3)^2 = (N/2+3)^2 * 2 + modinv_cost				*/
/*	N^2 + 6*N + 9 = (N^2/4 + 3*N + 9) * 2 + modinv_cost		*/
/*	N^2 + 6*N + 9 - (N^2/4 + 3*N + 9) * 2 = modinv_cost		*/
/*	N^2/2 - 9 = modinv_cost						*/
/*	N = sqrt(2 * (modinv_cost + 9))					*/
/* If modinv_cost = 4000, breakeven is 89.5				*/

	case POOL_N_SQUARED:

/* If this is the first call allocate memory for the gwnum we use */

		if (ecmdata->pool_count == 0) {
			allocAndCopyb (ecmdata->pool_modinv_value);
		}

/* Otherwise, multiply a by the accumulated b values and multiply all previous a's by this b */

		else {
			gwnum	tmp;
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, a, a, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			allocAndFFTb (tmp);
			for (uint32_t i = 0; i < ecmdata->pool_count; i++)
				gwmul3 (&ecmdata->gwdata, tmp, ecmdata->pool_values[i], ecmdata->pool_values[i], GWMUL_STARTNEXTFFT);
			gwmul3 (&ecmdata->gwdata, tmp, ecmdata->pool_modinv_value, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			normfree (tmp);
		}

/* Add a to array of values to normalize */

		ecmdata->pool_values[ecmdata->pool_count++] = a;
		break;
	}
	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}

/* Takes each point from add_to_normalize_pool and normalizes it. */
/* If we accidentally find a factor, it is returned in factor. */
/* Return stop_reason if there is a user interrupt. */

int normalize_pool (
	ecmhandle *ecmdata)
{
	int	stop_reason;

// Redefine normfree.  We do not need to track freed up gwnums when temps have been pre-allocated.
#undef normfree
#define normfree(d)		if(!ecmdata->use_preallocated_norm_temps){gwfree (&ecmdata->gwdata, d);}

/* For 3-mult pooling, accumulate last b value (was delayed so that we can use GWMUL_STARTNEXTFFT on all but the last pooled b value) */

	if (ecmdata->pool_type == POOL_3MULT && ecmdata->pool_count > 1) {
		gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, ecmdata->pool_modinv_value, 0);
	}

/* For 3.44-mult pooling finish off a partial group of four.  Accumulate last b value (was delayed so that we can use */
/* GWMUL_STARTNEXTFFT on all but the last pooled b value). */

	if (ecmdata->pool_type == POOL_3POINT44MULT) {

		/* Handle having only two of first set of four points. */
		if (ecmdata->pool_count == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[0], ecmdata->pool_modinv_value, 0);
			normfree (ecmdata->poolz_values[0]);
			ecmdata->poolz_count = 0;
		}

		/* Handle having only three of first set of four points. */
		/* Do much of the work that would have happened if the fourth point had been added to the pool. */
		else if (ecmdata->pool_count == 3) {
			gwnum	b3 = ecmdata->poolz_values[0];
			ecmdata->poolz_count = 0;
			// pool[0] *= b3
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], b3, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool[1] *= b3
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], b3, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// pool[2] *= b1*b2
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], ecmdata->pool_modinv_value, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pooled_modinv *= b3
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b3, ecmdata->pool_modinv_value, 0);
			normfree (b3);
		}

		/* Handle having only the first point in a set of three additional points */
		else if (ecmdata->pool_count > 4 && ecmdata->pool_count % 3 == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, ecmdata->pool_modinv_value, 0);
		}

		/* Handle having only two of an additional set of three points */
		/* Do much of the work that would have happened if the last point had been added to the pool */
		else if (ecmdata->pool_count > 4 && ecmdata->pool_count % 3 == 0) {
			gwnum	b6 = ecmdata->poolz_values[--ecmdata->poolz_count];
			// poolz[0] *= b6
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], b6, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pool[4] *= b6
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], b6, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_STARTNEXTFFT);
			// pool[5] *= b0*b5
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// pooled_modinv *= b6
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b6, ecmdata->pool_modinv_value, 0);
			normfree (b6);
		}

		/* Handle having a complete set of points */
		else if (ecmdata->pool_count >= 4 && ecmdata->pool_count % 3 == 1) {
			// Do the delayed multiply for pool_modinv
			ecmdata->poolz_count--;
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count], ecmdata->pool_modinv_value, 0);
			normfree (ecmdata->poolz_values[ecmdata->poolz_count]);
		}
	}

/* For 3.57-mult pooling finish off a partial group of eight.  Accumulate last b value (was delayed so that we can use */
/* GWMUL_STARTNEXTFFT on all but the last pooled b value). */

	if (ecmdata->pool_type == POOL_3POINT57MULT) {

		/* Handle having only two of first set of eight points */
		if (ecmdata->pool_count == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[0], ecmdata->pool_modinv_value, 0);
			normfree (ecmdata->poolz_values[0]);
			ecmdata->poolz_count = 0;
		}

		/* Handle having only three of first set of eight points */
		else if (ecmdata->pool_count == 3) {
			gwnum	b3;
			// Load values temporarily stored in poolz
			b3 = ecmdata->poolz_values[0];
			ecmdata->poolz_count = 0;
			// (a1*b2)*b3
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], b3, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a2*b1)*b3
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], b3, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// a3*b12
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], ecmdata->pool_modinv_value, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b1*b2*b3
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b3, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b3);
		}

		/* Handle having only four of first set of eight points */
		else if (ecmdata->pool_count == 4) {
			gwnum	b34;
			// Load values temporarily stored in poolz
			b34 = ecmdata->poolz_values[0];
			ecmdata->poolz_count = 0;
			// (a1*b2)*b34
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], b34, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a2*b1)*b34
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], b34, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// (a3*b4)*b12
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], ecmdata->pool_modinv_value, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a4*b3)*b12
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[3], ecmdata->pool_modinv_value, ecmdata->pool_values[3], GWMUL_STARTNEXTFFT);
			// b1*b2*b3*b4
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b34, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b34);
		}

		/* Handle having only five of first set of eight points */
		else if (ecmdata->pool_count == 5) {
			gwnum	b34, b5, b6vals;
			// Allocate more temporaries
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b5 = ecmdata->poolz_values[1];
			b34 = ecmdata->poolz_values[0];
			ecmdata->poolz_count = 0;
			// b34 * b5
			gwmul3 (&ecmdata->gwdata, b34, b5, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// (a1*b2)*b345
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], b6vals, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a2*b1)*b345
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], b6vals, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// b12 * b5
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b5, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a3*b4)*b125
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], b6vals, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a4*b3)*b125
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[3], b6vals, ecmdata->pool_values[3], GWMUL_STARTNEXTFFT);
			// b12 * b34
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b34, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// a5*b1234
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[4], ecmdata->pool_modinv_value, ecmdata->pool_values[4], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b1*b2*b3*b4*b5
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b5, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b34);
			normfree (b5);
			normfree (b6vals);
		}

		/* Handle having only six of first set of eight points */
		else if (ecmdata->pool_count == 6) {
			gwnum	b34, b56, b6vals;
			// Allocate more temporaries
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b56 = ecmdata->poolz_values[1];
			b34 = ecmdata->poolz_values[0];
			ecmdata->poolz_count = 0;
			// b34 * b56
			gwmul3 (&ecmdata->gwdata, b34, b56, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// (a1*b2)*b3456
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], b6vals, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a2*b1)*b3456
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], b6vals, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// b12 * b56
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b56, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a3*b4)*b1256
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], b6vals, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a4*b3)*b1256
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[3], b6vals, ecmdata->pool_values[3], GWMUL_STARTNEXTFFT);
			// b12 * b34
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b34, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// (a5*b6)*b1234
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[4], ecmdata->pool_modinv_value, ecmdata->pool_values[4], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a6*b5)*b1234
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[5], ecmdata->pool_modinv_value, ecmdata->pool_values[5], GWMUL_STARTNEXTFFT);
			// b1*b2*b3*b4*b5*b6
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b56, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b34);
			normfree (b56);
			normfree (b6vals);
		}

		/* Handle having seven of first set of eight points. */
		/* Do much of the work that would have happened if the fourth point had been added to the pool. */
		else if (ecmdata->pool_count == 7) {
			gwnum	b7, b34, b56, b567, b6vals;
			// Allocate more temporaries
			normalloc (b567);
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b7 = ecmdata->poolz_values[2];
			b56 = ecmdata->poolz_values[1];
			b34 = ecmdata->poolz_values[0];
			ecmdata->poolz_count = 0;
			// b56 * b7
			gwmul3 (&ecmdata->gwdata, b56, b7, b567, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b34 * b567
			gwmul3 (&ecmdata->gwdata, b34, b567, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// (a1*b2)*b34567
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[0], b6vals, ecmdata->pool_values[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a2*b1)*b34567
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[1], b6vals, ecmdata->pool_values[1], GWMUL_STARTNEXTFFT);
			// b12 * b567
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b567, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a3*b4)*b12567
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[2], b6vals, ecmdata->pool_values[2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a4*b3)*b12567
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[3], b6vals, ecmdata->pool_values[3], GWMUL_STARTNEXTFFT);
			// b12 * b34
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b34, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// b7 * b1234
			gwmul3 (&ecmdata->gwdata, b7, ecmdata->pool_modinv_value, b6vals, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a5*b6)*b12347
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[4], b6vals, ecmdata->pool_values[4], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a6*b5)*b12347
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[5], b6vals, ecmdata->pool_values[5], GWMUL_STARTNEXTFFT);
			// b56 * b1234
			gwmul3 (&ecmdata->gwdata, b56, ecmdata->pool_modinv_value, b6vals, GWMUL_STARTNEXTFFT);
			// a7*b123456
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[6], b6vals, ecmdata->pool_values[6], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b1*b2*b3*b4*b5*b6*b7
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b567, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b34);
			normfree (b56);
			normfree (b7);
			normfree (b567);
			normfree (b6vals);
		}

		/* Handle having only the first point in a set of seven additional points */
		else if (ecmdata->pool_count > 8 && ecmdata->pool_count % 7 == 2) {
			// Do the delayed multiply for pool_modinv
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], ecmdata->pool_modinv_value, ecmdata->pool_modinv_value, 0);
		}

		/* Handle having only two of an additional set of seven points */
		else if (ecmdata->pool_count > 8 && ecmdata->pool_count % 7 == 3) {
			gwnum	b10;
			// Load values temporarily stored in poolz
			b10 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			ecmdata->poolz_count -= 1;
			// b9*b10
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], b10, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a9*b0)*b10
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], b10, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_STARTNEXTFFT);
			// a10*b09
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b0*b9*b10
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b10, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b10);
		}

		/* Handle having only three of an additional set of seven points */
		else if (ecmdata->pool_count > 8 && ecmdata->pool_count % 7 == 4) {
			gwnum	b1011;
			// Load values temporarily stored in poolz
			b1011 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			ecmdata->poolz_count -= 1;
			// b9*b1011
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], b1011, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a9*b0)*b1011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-3], b1011, ecmdata->pool_values[ecmdata->pool_count-3], GWMUL_STARTNEXTFFT);
			// (a10*b11)*b09
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a11*b10)*b09
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_STARTNEXTFFT);
			// b0*b9*b10*b11
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b1011, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b1011);
		}

		/* Handle having only four of an additional set of seven points */
		else if (ecmdata->pool_count > 8 && ecmdata->pool_count % 7 == 5) {
			gwnum	b1011, b12, b6vals;
			// Allocate temporaries
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b12 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			b1011 = ecmdata->poolz_values[ecmdata->poolz_count-2];
			ecmdata->poolz_count -= 2;
			// b1011 * b12
			gwmul3 (&ecmdata->gwdata, b1011, b12, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b9*b101112
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], b6vals, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a9*b0)*b101112
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-4], b6vals, ecmdata->pool_values[ecmdata->pool_count-4], GWMUL_STARTNEXTFFT);
			// b09 * b12
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b12, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a10*b11)*b0912
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-3], b6vals, ecmdata->pool_values[ecmdata->pool_count-3], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a11*b10)*b0912
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], b6vals, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_STARTNEXTFFT);
			// b09 * b1011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b1011, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// a12*b091011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b0*b9*b10*b11*b12
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b12, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b1011);
			normfree (b12);
			normfree (b6vals);
		}

		/* Handle having only five of an additional set of seven points */
		else if (ecmdata->pool_count > 8 && ecmdata->pool_count % 7 == 6) {
			gwnum	b1011, b1213, b6vals;
			// Allocate temporaries
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b1213 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			b1011 = ecmdata->poolz_values[ecmdata->poolz_count-2];
			ecmdata->poolz_count -= 2;
			// b1011 * b1213
			gwmul3 (&ecmdata->gwdata, b1011, b1213, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b9*b10111213
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], b6vals, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a9*b0)*b10111213
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-5], b6vals, ecmdata->pool_values[ecmdata->pool_count-5], GWMUL_STARTNEXTFFT);
			// b09 * b1213
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b1213, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a10*b11)*b091213
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-4], b6vals, ecmdata->pool_values[ecmdata->pool_count-4], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a11*b10)*b091213
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-3], b6vals, ecmdata->pool_values[ecmdata->pool_count-3], GWMUL_STARTNEXTFFT);
			// b09 * b1011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b1011, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// (a12*b13)*b091011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a13*b12)*b091011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], ecmdata->pool_modinv_value, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_STARTNEXTFFT);
			// b0*b9*b10*b11*b12*b13
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b1213, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b1011);
			normfree (b1213);
			normfree (b6vals);
		}

		/* Handle having only six of an additional set of seven points */
		/* Do much of the work that would have happened if the seventh point had been added to the pool. */
		else if (ecmdata->pool_count > 8 && ecmdata->pool_count % 7 == 0) {
			gwnum	b14, b1011, b1213, b121314, b6vals;
			// Allocate temporaries
			normalloc (b121314);
			normalloc (b6vals);
			// Load values temporarily stored in poolz
			b14 = ecmdata->poolz_values[ecmdata->poolz_count-1];
			b1213 = ecmdata->poolz_values[ecmdata->poolz_count-2];
			b1011 = ecmdata->poolz_values[ecmdata->poolz_count-3];
			ecmdata->poolz_count -= 3;
			// b1213 * b14
			gwmul3 (&ecmdata->gwdata, b1213, b14, b121314, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b1011 * b121314
			gwmul3 (&ecmdata->gwdata, b1011, b121314, b6vals, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// b9*b1011121314
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count-1], b6vals, ecmdata->poolz_values[ecmdata->poolz_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a9*b0)*b1011121314
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-6], b6vals, ecmdata->pool_values[ecmdata->pool_count-6], GWMUL_STARTNEXTFFT);
			// b09 * b121314
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b121314, b6vals, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			// (a10*b11)*b09121314
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-5], b6vals, ecmdata->pool_values[ecmdata->pool_count-5], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a11*b10)*b09121314
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-4], b6vals, ecmdata->pool_values[ecmdata->pool_count-4], GWMUL_STARTNEXTFFT);
			// b09 * b1011
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b1011, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			// b14 * b091011
			gwmul3 (&ecmdata->gwdata, b14, ecmdata->pool_modinv_value, b6vals, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a12*b13)*b09101114
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-3], b6vals, ecmdata->pool_values[ecmdata->pool_count-3], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// (a13*b12)*b09101114
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-2], b6vals, ecmdata->pool_values[ecmdata->pool_count-2], GWMUL_STARTNEXTFFT);
			// b1213 * b091011
			gwmul3 (&ecmdata->gwdata, b1213, ecmdata->pool_modinv_value, b6vals, GWMUL_STARTNEXTFFT);
			// a14*b0910111213
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_values[ecmdata->pool_count-1], b6vals, ecmdata->pool_values[ecmdata->pool_count-1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			// b0*b9*b10*b11*b12*b13*b14
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, b121314, ecmdata->pool_modinv_value, 0);
			// Free temps
			normfree (b14);
			normfree (b1011);
			normfree (b1213);
			normfree (b121314);
			normfree (b6vals);
		}

		/* Handle having a complete set of points */
		else if (ecmdata->pool_count >= 8 && ecmdata->pool_count % 7 == 1) {
			// Do the delayed multiply for pool_modinv
			ecmdata->poolz_count--;
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count], ecmdata->pool_modinv_value, 0);
			normfree (ecmdata->poolz_values[ecmdata->poolz_count]);
		}
	}

/* Compute the modular inverse */

//GW: ecm_modinv calls popg which does a gwalloc.  Use one of the pre-allocated and normfreed buffers instead.
	gwunfft (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->pool_modinv_value);
	stop_reason = ecm_modinv (ecmdata, ecmdata->pool_modinv_value);
	if (stop_reason) return (stop_reason);
	if (ecmdata->factor != NULL) goto exit;

/* Now invert each value.  Switch off the type of pooling we are doing. */

	switch (ecmdata->pool_type) {

/* Implement 3 multiply algorithm */

	case POOL_3MULT:
		for (uint32_t i = ecmdata->pool_count-1; ; i--) {
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->pool_values[i], ecmdata->pool_values[i], GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			if (i == 0) break;
			ecmdata->poolz_count--;
			gwmul3 (&ecmdata->gwdata, ecmdata->poolz_values[ecmdata->poolz_count], ecmdata->pool_modinv_value, ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			normfree (ecmdata->poolz_values[ecmdata->poolz_count]);
		}
		break;

/* Implement 3.44 multiply algorithm */

	case POOL_3POINT44MULT:
		for (uint32_t i = ecmdata->pool_count-1; ; i--) {
			// Invert one pooled value
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->pool_values[i], ecmdata->pool_values[i], GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			if (i == 0) break;
			if (i < 4 || i % 3 != 1) continue;
			// Compute inverse for next group of 4
			ecmdata->poolz_count--;
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			normfree (ecmdata->poolz_values[ecmdata->poolz_count]);
		}
		break;

/* Implement 3.57 multiply algorithm */

	case POOL_3POINT57MULT:
		for (uint32_t i = ecmdata->pool_count-1; ; i--) {
			// Invert one pooled value
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->pool_values[i], ecmdata->pool_values[i], GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			if (i == 0) break;
			if (i < 8 || i % 7 != 1) continue;
			// Compute inverse for next group of 8
			ecmdata->poolz_count--;
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->poolz_values[ecmdata->poolz_count], ecmdata->pool_modinv_value, GWMUL_STARTNEXTFFT);
			normfree (ecmdata->poolz_values[ecmdata->poolz_count]);
		}
		break;

/* Implement N^2 algorithm */

	case POOL_N_SQUARED:
		for (uint32_t i = 0; i < ecmdata->pool_count; i++)
			gwmul3 (&ecmdata->gwdata, ecmdata->pool_modinv_value, ecmdata->pool_values[i], ecmdata->pool_values[i], GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
		break;
	}

/* Cleanup and reinitialize */

exit:	ecmdata->pool_count = 0;
	ecmdata->poolz_count = 0;
	normfree (ecmdata->pool_modinv_value); ecmdata->pool_modinv_value = NULL;
	ecmdata->norm_pool_temps_count = 0;
	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}

 
/**************************************************************
 *	Other ECM functions 
 **************************************************************/

/* From R. P. Brent, priv. comm. 1996 and https://eprint.iacr.org/2008/016.pdf section 7.5
Let s > 5 be a pseudo-random seed (called $\sigma$ in the Tech. Report),

	u/v = (s^2 - 5)/(4s)

Then starting point is (x_1, y_1) where

	x_1 = (u/v)^3
	y_1 = (s^2 - 1)(s^2 - 25)(s^4 - 25)/(v^3)
and
	A = (v-u)^3(3u+v)/(4u^3 v) - 2
	B = u/(v^3)
where Montgomery curve is defined as By^2 = x^3 + Ax^2 + x
*/
int choose12 (
	ecmhandle *ecmdata,
	struct xz *xz,		/* Return the curve's Montgomery starting point here */
	struct ed *e)		/* Return the curve's Edward starting point here */
{
	int	stop_reason;
	bool	gen_edwards = !ecmdata->montg_stage1;	// TRUE if twisted Edwards curve should also be generated
	ASSERTG (gen_edwards || e == NULL);		// Can't generate Edwards start point without generating Edwards curve

	/* Compute u,v (sometimes called alpha,beta in the literature) */
	gwnum u, v, vPu, vMu, u2, vP3u, vM3u, u3, v3, An, Ad;
	u = gwalloc (&ecmdata->gwdata);	if (u == NULL) goto oom;
	v = gwalloc (&ecmdata->gwdata);	if (v == NULL) goto oom;
	u64togw (&ecmdata->gwdata, ecmdata->sigma, v);
	gwsquare2 (&ecmdata->gwdata, v, u, 0);					/* s^2 */
	gwsmalladd (&ecmdata->gwdata, -5, u);					/* u = s^2 - 5 */
	gwsmallmul (&ecmdata->gwdata, 4.0, v);					/* v = 4*s */

	/* Gen some needed values -- especially those made by addition (before we start FFTing values) */
	vPu = gwalloc (&ecmdata->gwdata); if (vPu == NULL) goto oom;
	vMu = gwalloc (&ecmdata->gwdata); if (vMu == NULL) goto oom;
	gwaddsub4o (&ecmdata->gwdata, v, u, vPu, vMu, GWADD_GUARANTEED_OK);	/* v+u, v-u */
	u2 = gwalloc (&ecmdata->gwdata); if (u2 == NULL) goto oom;
	gwadd3o (&ecmdata->gwdata, u, u, u2, GWADD_GUARANTEED_OK);		/* 2u */

	vP3u = gwalloc (&ecmdata->gwdata); if (vP3u == NULL) goto oom;
	gwadd3o (&ecmdata->gwdata, vPu, u2, vP3u, GWADD_GUARANTEED_OK);		/* 3u+v */
	vM3u = NULL;
	if (gen_edwards) {
		vM3u = gwalloc (&ecmdata->gwdata); if (vM3u == NULL) goto oom;
		gwsub3o (&ecmdata->gwdata, vMu, u2, vM3u, GWADD_GUARANTEED_OK);	/* v-3u */
	}
	if (e != NULL) {
		gwnum twoV1 = u2;									/* 2V1 = u2 = 2(s^2 - 5), u2 no longer used */
		gwsmalladd (&ecmdata->gwdata, 10, u);							/* s^2 + 5 */
		gwmul3 (&ecmdata->gwdata, twoV1, u, twoV1, GWMUL_PRESERVE_S2 | GWMUL_STARTNEXTFFT);	/* 2V1 *= s^2 + 5 = 2(s^4 - 25) */
		gwsmalladd (&ecmdata->gwdata, -6, u);							/* s^2 - 1 */
		gwmul3 (&ecmdata->gwdata, twoV1, u, twoV1, GWMUL_PRESERVE_S2 | GWMUL_STARTNEXTFFT);	/* 2V1 = 2(s^2 - 1)(s^4 - 25) */
		gwsmalladd (&ecmdata->gwdata, -24, u);							/* s^2 - 25 */
		gwmul3 (&ecmdata->gwdata, twoV1, u, twoV1, GWMUL_PRESERVE_S2 | GWMUL_STARTNEXTFFT);	/* 2V1 = 2(s^2 - 1)(s^2 - 25)(s^4 - 25) */
		gwsmalladd (&ecmdata->gwdata, 20, u);							/* Restore u = s^2 - 5 */
	}

	/* More needed intermediate values */
	u3 = gwalloc (&ecmdata->gwdata); if (u3 == NULL) goto oom;
	v3 = gwalloc (&ecmdata->gwdata); if (v3 == NULL) goto oom;
	gwsquare2 (&ecmdata->gwdata, u, u3, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
	gwmul3 (&ecmdata->gwdata, u, u3, u3, 0);					/* u^3 (also Mongtgomery starting x) */
	gwsquare2 (&ecmdata->gwdata, v, v3, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
	gwmul3 (&ecmdata->gwdata, v, v3, v3, 0);					/* v^3 (also Mongtgomery starting z) */

	/* Now for Montgomery A */
	gwmul3 (&ecmdata->gwdata, vMu, vP3u, vP3u, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);	/* (v-u)(3u+v) */
	gwsquare2 (&ecmdata->gwdata, vMu, vMu, GWMUL_STARTNEXTFFT);			/* (v-u)^2 */
	gwmul3 (&ecmdata->gwdata, vMu, vP3u, vP3u, 0);					/* An = (v-u)^3(3u+v), this is also Edwards a. */
	An = vP3u;									/* Rename for readability */
	if (gen_edwards) {								/* Copy An before normalize destroys it */
		ecmdata->ed_a = gwalloc (&ecmdata->gwdata); if (ecmdata->ed_a == NULL) goto oom;
		gwfft (&ecmdata->gwdata, An, ecmdata->ed_a);
	}
	Ad = vMu;									/* vMu is no longer used */
	gwsetmulbyconst (&ecmdata->gwdata, 4);
	gwmul3 (&ecmdata->gwdata, u3, v, Ad, GWMUL_MULBYCONST);				/* Ad = 4u^3v, A + 2 = An/Ad */

	/* Normalize so that An is one, thus Ad is 1/(A + 2) */
	stop_reason = normalize (ecmdata, Ad, An);					// This destroys An
	if (stop_reason) return (stop_reason);
	gwfree (&ecmdata->gwdata, An);

	/* For extra speed, precompute Ad * 4, a.k.a 4/(A + 2) */
	gwsmallmul (&ecmdata->gwdata, 4.0, Ad);

	/* Even more speed, save FFT of Ad4 */
	gwfft (&ecmdata->gwdata, Ad, Ad);
	ecmdata->Ad4 = Ad;

/* Now generate the rest of the twisted Edwards curve */

	if (gen_edwards) {
		gwmul3 (&ecmdata->gwdata, vPu, vM3u, vM3u, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);	/* (v+u)(v-3u) */
		gwsquare2 (&ecmdata->gwdata, vPu, vPu, GWMUL_STARTNEXTFFT);			/* (v+u)^2 */
		gwmul3 (&ecmdata->gwdata, vPu, vM3u, vM3u, GWMUL_STARTNEXTFFT);			/* d = (v+u)^3(v-3u) */
		ecmdata->ed_d = vM3u;
	}

/* Now generate the twisted Edward curve's starting point */

	if (e != NULL) {
		gwnum yz = vPu;								/* vPu no longer used */
		gwnum twoV1 = u2;							/* Previously computed 2V1 */

		ed_alloc (ecmdata, e);

		gwmul3 (&ecmdata->gwdata, u, v, e->x, GWMUL_STARTNEXTFFT);		/* x = uv */
		gwaddsub4o (&ecmdata->gwdata, u3, v3, yz, e->y, GWADD_GUARANTEED_OK);	/* u^3+v^3, u^3-v^3 */

		gwmul3 (&ecmdata->gwdata, e->x, yz, e->x, GWMUL_STARTNEXTFFT);		/* x *= u^3+v^3 */
		gwmul3 (&ecmdata->gwdata, e->y, twoV1, e->y, GWMUL_STARTNEXTFFT);	/* y *= 2V1 */
		gwmul3 (&ecmdata->gwdata, twoV1, yz, e->z, GWMUL_STARTNEXTFFT);		/* z = 2V1 * (u^3+v^3) */

		ASSERTG (ed_check (ecmdata, e));
	}

/* Return Montgomery starting point */

	if (xz != NULL) {
		ASSERTG (xz->x == NULL && xz->z == NULL);
		gwswap (xz->x, u3);	/* x = u^3 */
		gwswap (xz->z, v3);	/* z = v^3 */
	}

/* Clean up temporaries */

	gwfree (&ecmdata->gwdata, u);
	gwfree (&ecmdata->gwdata, v);
	gwfree (&ecmdata->gwdata, vPu);
	gwfree (&ecmdata->gwdata, u2);
	gwfree (&ecmdata->gwdata, u3);
	gwfree (&ecmdata->gwdata, v3);
	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}

/* Create a random Edwards curve using Atkin-Morain construction.  See section 7.1 of https://eprint.iacr.org/2008/016.pdf for details. */

int choose_atkin_morain (
	ecmhandle *ecmdata,
	struct ed *e)		/* Return the curve's Edward starting point here */
{
	// Create a starting Weierstrass point of (12,40) on the curve T^2 = S^3 - 8S - 32
	struct ws w;
	ws_alloc (ecmdata, &w);
	u64togw (&ecmdata->gwdata, 12, w.x);
	u64togw (&ecmdata->gwdata, 40, w.y);
	u64togw (&ecmdata->gwdata, 1, w.z);

	// Multiply point by random 64-bit value (sigma)
	ws_mul (ecmdata, &w, ecmdata->sigma);

#ifdef STRAIGHT_FORWARD
	// Normalize the point to get point named (s,t) in the literature
	ecm_modinv (ecmdata, w.z);				// Inverse of w.z
	gwmul3 (&ecmdata->gwdata, w.z, w.x, w.x, 0);		// Normalize w.x
	gwmul3 (&ecmdata->gwdata, w.z, w.y, w.y, 0);		// Normalize w.y
	gwnum s = w.x;
	gwnum t = w.y;

	// Compute alpha from the literature.  a = ((t + 25)/(s - 9) + 1)^-1.  Simplifying, a = (s-9) / ((t+25)+(s-9)).
	gwnum a = gwalloc (&ecmdata->gwdata);
	gwnum tmp = gwalloc (&ecmdata->gwdata);
	gwsetmulbyconst (&ecmdata->gwdata, 2);
	gwcopy (&ecmdata->gwdata, s, a);
	gwsmalladd (&ecmdata->gwdata, -9, a);				// s-9
	gwcopy (&ecmdata->gwdata, t, tmp);
	gwsmalladd (&ecmdata->gwdata, 25, tmp);				// t+25
	gwadd (&ecmdata->gwdata, a, tmp);				// ((t+25)+(s-9))
	normalize (ecmdata, a, tmp);					// a = (s-9) / ((t+25)+(s-9))

	// Compute beta from the literature.  b = 2a(4a+1) / (8a^2 - 1)
	gwnum b = gwalloc (&ecmdata->gwdata);
	gwcopy (&ecmdata->gwdata, a, b);
	gwsmallmul (&ecmdata->gwdata, 4, b);				// 4a
	gwsmalladd (&ecmdata->gwdata, 1, b);				// 4a+1
	gwmul3 (&ecmdata->gwdata, a, b, b, GWMUL_MULBYCONST);		// 2a(4a+1)
	gwsquare2 (&ecmdata->gwdata, a, tmp, 0);			// a^2
	gwsmallmul (&ecmdata->gwdata, 8, tmp);				// 8a^2
	gwsmalladd (&ecmdata->gwdata, -1, tmp);				// 8a^2-1
	normalize (ecmdata, b, tmp);					// b = 2a(4a+1) / (8a^2 - 1)

	// Compute d from the literature.  d = (2(2b - 1)^2 - 1)/(2b - 1)^4
	ecmdata->ed_d = gwalloc (&ecmdata->gwdata);
	gwnum b2m1 = gwalloc (&ecmdata->gwdata);
	gwadd3o (&ecmdata->gwdata, b, b, b2m1, GWADD_FORCE_NORMALIZE);	// 2b, make (2b-1)^2 safe
	gwsmalladd (&ecmdata->gwdata, -1, b2m1);			// 2b-1		(save for later)
	gwsquare2 (&ecmdata->gwdata, b2m1, tmp, GWMUL_PRESERVE_S1);	// (2b-1)^2
	gwadd3 (&ecmdata->gwdata, tmp, tmp, ecmdata->ed_d);		// 2(2b-1)^2
	gwsmalladd (&ecmdata->gwdata, -1, ecmdata->ed_d);		// 2(2b-1)^2-1
	gwsquare2 (&ecmdata->gwdata, tmp, tmp, 0);			// (2b-1)^4
	normalize (ecmdata, ecmdata->ed_d, tmp);			// d = (2(2b - 1)^2 - 1)/(2b - 1)^4

	// Now compute starting point from the literature.
	// x = (2b − 1)(4b − 3)/(6b − 5)
	// y = (2b - 1)(t^2 + 50t - 2s^3 + 27s^2 - 104)/((t + 3s - 2)(t + s + 16))
	if (e != NULL) {
		ed_alloc (ecmdata, e);
		gwadd3o (&ecmdata->gwdata, b2m1, b2m1, e->x, GWADD_FORCE_NORMALIZE); // 4b-2
		gwsmalladd (&ecmdata->gwdata, -1, e->x);			// 4b-3
		gwadd3o (&ecmdata->gwdata, b2m1, e->x, e->z, GWADD_FORCE_NORMALIZE); // 6b-4
		gwsmalladd (&ecmdata->gwdata, -1, e->z);			// 6b-5
		gwmul3 (&ecmdata->gwdata, b2m1, e->x, e->x, GWMUL_STARTNEXTFFT);// (2b-1)(4b-3)
		gwsquare2 (&ecmdata->gwdata, t, e->y, GWMUL_PRESERVE_S1);	// t^2
		gwcopy (&ecmdata->gwdata, t, tmp);
		gwsmallmul (&ecmdata->gwdata, 50, tmp);				// 50t
		gwadd3 (&ecmdata->gwdata, e->y, tmp, e->y);			// t^2 + 50t
		gwsquare2 (&ecmdata->gwdata, s, tmp, GWMUL_PRESERVE_S1 | GWMUL_STARTNEXTFFT); // s^2
		gwmul3 (&ecmdata->gwdata, s, tmp, tmp, GWMUL_PRESERVE_S1 | GWMUL_MULBYCONST); // 2s^3
		gwsub3 (&ecmdata->gwdata, e->y, tmp, e->y);			// t^2 + 50t - 2s^3
		gwsquare2 (&ecmdata->gwdata, s, tmp, GWMUL_PRESERVE_S1);	// s^2
		gwsmallmul (&ecmdata->gwdata, 27, tmp);				// 27s^2
		gwadd3 (&ecmdata->gwdata, e->y, tmp, e->y);			// t^2 + 50t - 2s^3 + 27s^2
		gwsmalladd (&ecmdata->gwdata, -104, e->y);			// t^2 + 50t - 2s^3 + 27s^2 - 104
		gwmul3 (&ecmdata->gwdata, b2m1, e->y, e->y, GWMUL_STARTNEXTFFT);// (2b-1)(t^2 + 50t - 2s^3 + 27s^2 - 104)
		gwnum z2 = b2m1;
		gwcopy (&ecmdata->gwdata, t, z2);				// t
		gwcopy (&ecmdata->gwdata, s, tmp);				// s
		gwsmallmul (&ecmdata->gwdata, 3, tmp);				// 3s
		gwadd3o (&ecmdata->gwdata, z2, tmp, z2, GWADD_FORCE_NORMALIZE); // t + 3s, make (t + 3s - 2)(t + s + 16) safe
		gwsmalladd (&ecmdata->gwdata, -2, z2);				// t + 3s - 2
		gwadd3 (&ecmdata->gwdata, t, s, tmp);				// t + s
		gwsmalladd (&ecmdata->gwdata, 16, tmp);				// t + s + 16
		gwmul3 (&ecmdata->gwdata, z2, tmp, z2, GWMUL_STARTNEXTFFT);	// (t + 3s - 2)(t + s + 16)

		gwmul3 (&ecmdata->gwdata, e->x, z2, e->x, 0);
		gwmul3 (&ecmdata->gwdata, e->y, e->z, e->y, 0);
		gwmul3 (&ecmdata->gwdata, e->z, z2, e->z, 0);
		ed_extend (ecmdata, e, 0);

		ASSERTG (ed_check (ecmdata, e));
	}

	// Cleanup
	ws_free (ecmdata, &w);
	gwfree (&ecmdata->gwdata, tmp);
	gwfree (&ecmdata->gwdata, b2m1);

#else

	// Same formulas as above, but do only one modular inverse.
	// Leave s,t unnormalized.  Adopt syntax sN for s numerator and sD for s denominator.  Same for t, a, b, d.

	// Compute alpha from the literature.  a = ((t + 25)/(s - 9) + 1)^-1.  Simplifying, a = (s-9) / ((t+25)+(s-9)).
	//     (sN - 9sD)    (tN + 25tD)   (sN - 9sD)    (sN - 9sD)               sD                  (sN - 9sD)      aN
	// a = ---------- / (----------- + ----------) = ---------- * (------------------------) = ---------------- = --	(tD equals sD)
	//         sD            tD            sD            sD        (tN + 25sD) + (sN - 9sD)    (tN + sN + 16sD)   aD
	gwnum sN = w.x, tN = w.y, sD = w.z;
	gwnum aN = gwalloc (&ecmdata->gwdata);
	gwnum aD = gwalloc (&ecmdata->gwdata);
	gwnum tmp = gwalloc (&ecmdata->gwdata);
	gwcopy (&ecmdata->gwdata, sD, tmp);
	gwsmallmul (&ecmdata->gwdata, 9, tmp);					// 9sD
	gwsub3o (&ecmdata->gwdata, sN, tmp, aN, GWADD_FORCE_NORMALIZE);		// aN = sN - 9sD
	gwcopy (&ecmdata->gwdata, sD, tmp);
	gwsmallmul (&ecmdata->gwdata, 16, tmp);					// 16sD
	gwadd3o (&ecmdata->gwdata, sN, tmp, aD, GWADD_DELAY_NORMALIZE);		// sN + 16sD
	gwadd3o (&ecmdata->gwdata, aD, tN, aD, GWADD_FORCE_NORMALIZE);		// aD = tN + sN + 16sD

	// Compute beta from the literature.  b = 2a(4a+1) / (8a^2 - 1).  We won't need a after computing b.
	//     2aN   (4aN + aD)        aD^2        (8aN^2 + 2aNaD)   bN
	// b = --- * ---------- * -------------- = --------------- = --
	//      aD       aD       (8aN^2 - aD^2)   (8aN^2 - aD^2)    bD
	gwnum bN = gwalloc (&ecmdata->gwdata);
	gwnum bD = gwalloc (&ecmdata->gwdata);
	gwsetmulbyconst (&ecmdata->gwdata, 8);
	gwsquare2 (&ecmdata->gwdata, aN, bD, GWMUL_FFT_S1 | GWMUL_MULBYCONST);	// 8aN^2
	gwsetmulbyconst (&ecmdata->gwdata, 2);
	gwmul3 (&ecmdata->gwdata, aN, aD, tmp, GWMUL_FFT_S2 | GWMUL_MULBYCONST);// 2aNaD
	gwadd3o (&ecmdata->gwdata, bD, tmp, bN, GWADD_DELAY_NORMALIZE);		// bN = 8aN^2 + 2aNaD
	gwsquare2 (&ecmdata->gwdata, aD, tmp, 0);				// aD^2
	gwsub3o (&ecmdata->gwdata, bD, tmp, bD, GWADD_FORCE_NORMALIZE);		// bD = 8aN^2 - aD^2

	// Compute 2b - 1 which will come in handy computing d and the starting point.  We won't need b after computing 2b - 1.
	gwnum b2m1N, b2m1D;
	gwadd3o (&ecmdata->gwdata, bN, bN, bN, GWADD_DELAY_NORMALIZE);		// 2bN
	gwsub3o (&ecmdata->gwdata, bN, bD, b2m1N = bN, GWADD_FORCE_NORMALIZE);	// b2m1N = 2bN - bD
	b2m1D = bD;

	// Compute d from the literature.  d = (2(2b - 1)^2 - 1)/(2b - 1)^4 = (2 * b2m1^2 - 1) / b2m1^4
	//     (2 * b2m1N^2 - b2m1D^2)   b2m1D^4   (2 * b2m1N^2 - b2m1D^2) * b2m1D^2   dN
	// d = ----------------------- * ------- = --------------------------------- = --
	//           b2m1D^2             b2m1N^4             b2m1N^4                   dD
	gwnum dN = aN, dD = aD;
	gwsquare2 (&ecmdata->gwdata, b2m1D, tmp, GWMUL_PRESERVE_S1);		// b2m1D^2
	gwsquare2 (&ecmdata->gwdata, b2m1N, dD, GWMUL_PRESERVE_S1);		// b2m1N^2
	gwadd3o (&ecmdata->gwdata, dD, dD, dN, GWADD_DELAY_NORMALIZE);		// 2*b2m1N^2
	gwsub3o (&ecmdata->gwdata, dN, tmp, dN, GWADD_FORCE_NORMALIZE);		// 2*b2m1N^2 - b2m1D^2
	gwmul3 (&ecmdata->gwdata, dN, tmp, dN, GWMUL_STARTNEXTFFT);		// dN = (2 * b2m1N^2 - b2m1D^2) * b2m1D^2
	gwsquare2 (&ecmdata->gwdata, dD, dD, GWMUL_STARTNEXTFFT);		// dD = b2m1N^4

	// To save a normalize in ed_to_Montgomery, emulate choose12 which computes 4/(A+2).  From the literature, A+2 = 4/(1-d) = 4dD / (dD-dN).
	// Thus we want compute ecmdata->Ad4 = (dD-dN)/dD.
	ecmdata->Ad4 = gwalloc (&ecmdata->gwdata);
	gwsub3o (&ecmdata->gwdata, dD, dN, ecmdata->Ad4, GWADD_MUL_INPUT);

	// Compute modular inverse of sD and dD.  This will be used to normalize either s, t, and d.  Leave b2m1 unnormalized.
	gwnum	inv_sD, inv_dD, inv_sDdD, s, t;
	gwmul3 (&ecmdata->gwdata, sD, dD, tmp, GWMUL_FFT_S12);						// sD * dD
	ecm_modinv (ecmdata, inv_sDdD = tmp);								// 1 / (sD * dD)
	gwmul3 (&ecmdata->gwdata, inv_sDdD, dD, inv_sD = dD, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);	// 1/sD = dD * 1 / (sD * dD)
	gwmul3 (&ecmdata->gwdata, inv_sDdD, sD, inv_dD = sD, GWMUL_STARTNEXTFFT);			// 1/dD = sD * 1 / (sD * dD)
	gwmul3 (&ecmdata->gwdata, dN, inv_dD, ecmdata->ed_d = dN, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);	// Edwards curve parameter d = dN / dD
	gwfft (&ecmdata->gwdata, ecmdata->ed_d, ecmdata->ed_d);
	gwmul3 (&ecmdata->gwdata, ecmdata->Ad4, inv_dD, ecmdata->Ad4, GWMUL_STARTNEXTFFT);		// Montgomery curve variable Ad4 = (dD-dN) / dD
	gwfft (&ecmdata->gwdata, ecmdata->Ad4, ecmdata->Ad4);
	gwmul3 (&ecmdata->gwdata, sN, inv_sD, s = sN, 0);						// s = sN / sD
	gwmul3 (&ecmdata->gwdata, tN, inv_sD, t = tN, 0);						// t = tN / sD, tD equals sD

	// Now compute starting point from the literature.  Using normalized inputs, x = (2b − 1)(4b − 3)/(6b − 5) = b2m1 * (2*b2m1 − 1) / (3*b2m1 − 1),
	// and y = (2b - 1)(t^2 + 50t - 2s^3 + 27s^2 - 104)/((t + 3s - 2)(t + s + 16))
	// With normalized s and t, and unnormalized b2m1 inputs:
	//     b2m1N   (2*b2m1N − b2m1D)          b2m1D          b2m1N    (2*b2m1N − b2m1D)    xN
	// x = ----- * ----------------- * ------------------- = ----- * ------------------- = --
	//     b2m1D         b2m1D         (3*b2m1N − 2*b2m1D)   b2m1D   (3*b2m1N − 2*b2m1D)   xD
	//
	//     b2m1N   (tN^2 + 50tN - 2sN^3 + 27sN^2 - 104)               sD                 b2m1N   (tN^2 + 50tN - 2sN^3 + 27sN^2 - 104)   yN
	// y = ----- * ------------------------------------ * ---------------------------- = ----- * ------------------------------------ = --
	//     b2m1D                   sD                     (tN + 3sN - 2)(tN + sN + 16)   b2m1D        (tN + 3sN - 2)(tN + sN + 16)      yD
	if (e != NULL) {
		gwnum xN, xD, yN, yD, tmp2;
		ed_alloc (ecmdata, e);
		xN = e->x; yN = e->y; xD = e->z; yD = sD; tmp2 = dD;
		// Compute xN, xD (delay b2m1N / b2m1D to later)
		gwadd3o (&ecmdata->gwdata, b2m1N, b2m1N, xN, GWADD_DELAY_NORMALIZE);		// 2 * b2m1N
		gwsub3o (&ecmdata->gwdata, xN, b2m1D, xN, GWADD_FORCE_NORMALIZE);		// xN = 2 * b2m1N - b2m1D
		gwadd3o (&ecmdata->gwdata, b2m1N, xN, xD, GWADD_DELAY_NORMALIZE);		// 3 * b2m1N - b2m1D
		gwsub3o (&ecmdata->gwdata, xD, b2m1D, xD, GWADD_FORCE_NORMALIZE);		// xD = 3 * b2m1N - 2*b2m1D
		// Compute yD (delay b2m1N / b2m1D to later)
		gwadd3 (&ecmdata->gwdata, t, s, tmp);						// t + s
		gwadd3o (&ecmdata->gwdata, tmp, s, yD, GWADD_DELAY_NORMALIZE);			// t + 2s
		gwadd3o (&ecmdata->gwdata, yD, s, yD, GWADD_FORCE_NORMALIZE);			// t + 3s, make (t + 3s - 2)(t + s + 16) safe
		gwsmalladd (&ecmdata->gwdata, -2, yD);						// t + 3s - 2
		gwsmalladd (&ecmdata->gwdata, 16, tmp);						// t + s + 16
		gwmul3 (&ecmdata->gwdata, yD, tmp, yD, GWMUL_STARTNEXTFFT);			// yD = (t + 3s - 2)(t + s + 16)
		// Compute yN (delay b2m1N / b2m1D to later)
		gwsquare2 (&ecmdata->gwdata, t, yN, GWMUL_PRESERVE_S1);				// t^2
		gwsquare2 (&ecmdata->gwdata, s, tmp, GWMUL_FFT_S1);				// s^2
		gwmul3 (&ecmdata->gwdata, s, tmp, tmp2, GWMUL_PRESERVE_S2 | GWMUL_MULBYCONST);	// 2s^3
		gwsub3o (&ecmdata->gwdata, yN, tmp2, yN, GWADD_DELAY_NORMALIZE);		// t^2 - 2s^3
		gwsmallmul (&ecmdata->gwdata, 50, t);						// 50t
		gwadd3o (&ecmdata->gwdata, yN, t, yN, GWADD_DELAY_NORMALIZE);			// t^2 + 50t - 2s^3
		gwsmallmul (&ecmdata->gwdata, 27, tmp);						// 27s^2
		gwadd3o (&ecmdata->gwdata, yN, tmp, yN, GWADD_FORCE_NORMALIZE);			// t^2 + 50t - 2s^3 + 27s^2
		gwsmalladd (&ecmdata->gwdata, -104, yN);					// yN = t^2 + 50t - 2s^3 + 27s^2 - 104
		// Compute e->x, e->y, e->z (delay b2m1N / b2m1D to later)
		gwmul3 (&ecmdata->gwdata, xN, yD, e->x = xN, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
		gwmul3 (&ecmdata->gwdata, yN, xD, e->y = yN, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
		gwmul3 (&ecmdata->gwdata, xD, yD, e->z = xD, GWMUL_STARTNEXTFFT);
		// Handle common b2m1N / b2m1D
		gwmul3 (&ecmdata->gwdata, b2m1N, e->x, e->x, GWMUL_FFT_S1);
		gwmul3 (&ecmdata->gwdata, b2m1N, e->y, e->y, 0);
		gwmul3 (&ecmdata->gwdata, b2m1D, e->z, e->z, 0);
		// Sanity check
		ASSERTG (ed_check (ecmdata, e));
	}

	// Cleanup
	ws_free (ecmdata, &w);
	gwfree (&ecmdata->gwdata, tmp);
	gwfree (&ecmdata->gwdata, b2m1N);
	gwfree (&ecmdata->gwdata, b2m1D);
	gwfree (&ecmdata->gwdata, dD);
#endif

	return (0);
}

/* From GMP-ECM's README file for -param=3.  A = 4*s/2^32-2, x0 = 2. */
int choose_gmp_ecm_param3 (
	ecmhandle *ecmdata)
{
	gwnum	An, Ad;
	int	stop_reason;

	/* Montgomery A + 2 = An/Ad */
	An = gwalloc (&ecmdata->gwdata);	if (An == NULL) goto oom;
	Ad = gwalloc (&ecmdata->gwdata);	if (Ad == NULL) goto oom;
	u64togw (&ecmdata->gwdata, ecmdata->sigma, An);
	gwsmallmul (&ecmdata->gwdata, 4.0, An);					/* An = 4*s */
	u64togw (&ecmdata->gwdata, 1ULL << 32, Ad);				/* Ad = 2^32 */

	/* Normalize so that An is one, thus Ad is 1/(A + 2) */
	stop_reason = normalize (ecmdata, Ad, An);				// This destroys An
	if (stop_reason) return (stop_reason);
	gwfree (&ecmdata->gwdata, An);

	/* For extra speed, precompute Ad * 4, a.k.a 4/(A + 2) */
	gwsmallmul (&ecmdata->gwdata, 4.0, Ad);

	/* Even more speed, save FFT of Ad4 */
	gwfft (&ecmdata->gwdata, Ad, Ad);
	ecmdata->Ad4 = Ad;

	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}

/* Convert from Edwards to Montgomery.  Edwards memory is freed. */

void ed_to_Montgomery (
	ecmhandle *ecmdata)
{

/* If the Montgomery A value has not been computed, do so now.  See section 3 of https://eprint.iacr.org/2008/013.pdf and */
/* https://en.wikipedia.org/wiki/Montgomery_curve .  A = 2(a + d)/(a - d).  Our Montgomery arithmetic code wants 4/(A + 2). */
/* A+2 = 2(a+d)/(a-d) + 2(a-d)/(a-d) = (2a+2d+2a-2d)/(a-d) = 4a/(a-d) */

	ASSERTG (ecmdata->Ad4 != NULL);			// choose12 or choose_atkin_morain should have computed Ad4
	//if (ecmdata->Ad4 == NULL) {
	//	gwnum An = gwalloc (&ecmdata->gwdata);
	//	gwnum Ad = gwalloc (&ecmdata->gwdata);

		/* Compute An = numerator, Ad = denominator */
	//	if (ecmdata->ed_a == NULL) dbltogw (&ecmdata->gwdata, 1.0, An);
	//	else gwunfft (&ecmdata->gwdata, ecmdata->ed_a, An);
	//	gwunfft (&ecmdata->gwdata, ecmdata->ed_d, Ad);
	//	gwsub3 (&ecmdata->gwdata, An, Ad, Ad);					// (a-d)
	//	gwsmallmul (&ecmdata->gwdata, 4, An);					// 4a, A + 2 = An/Ad

		/* Copy code from choose12.  Normalize so that An is one, thus Ad is 1/(A + 2) */
	//	int stop_reason = normalize (ecmdata, Ad, An);
		/* For extra speed, precompute Ad * 4, a.k.a 4/(A + 2) */
	//	gwsmallmul (&ecmdata->gwdata, 4, Ad);
		/* Even more speed, save FFT of Ad4 */
	//	gwfft (&ecmdata->gwdata, Ad, Ad);
	//	ecmdata->Ad4 = Ad;

	//	gwfree (&ecmdata->gwdata, An);
	//}

/* Convert the Edwards point.  From wikipedia: Montgomery x coordinate, u, equals (1+y)/(1-y).  But Edwards coordinates are in projective format, thus */
/* u = (1+y/z)/(1-y/z) = ((z+y)/z)/((z-y)/z) = (z+y)/(z-y).  Or in our Montgomery xz format, x = z+y, z = z-y. */

	if (ecmdata->e.x != NULL) {			// Resuming from a save file in stage 2 does not have a point to convert, fall through to freeing memory
		ASSERTG (ecmdata->xz.x == NULL);	// Something is wrong if overwriting xz
		alloc_xz (ecmdata, &ecmdata->xz);
		gwaddsub4 (&ecmdata->gwdata, ecmdata->e.z, ecmdata->e.y, ecmdata->xz.x, ecmdata->xz.z);
	}

/* Free edwards memory */

	ed_free (ecmdata, &ecmdata->dict_start);
	ed_free (ecmdata, &ecmdata->e);
	gwfree (&ecmdata->gwdata, ecmdata->ed_a), ecmdata->ed_a = NULL;
	gwfree (&ecmdata->gwdata, ecmdata->ed_d), ecmdata->ed_d = NULL;
	gwfree (&ecmdata->gwdata, ecmdata->ed_half), ecmdata->ed_half = NULL;
}

// Compute curve parameters for a Montgomery or Edwards curve

int init_curve (
	ecmhandle *ecmdata)
{
	int	stop_reason;

	// GMP-ECM parameterization 3 (prime95 does stage 2 only)
	if (ecmdata->sigma_type == 3) {
		stop_reason = choose_gmp_ecm_param3 (ecmdata);
	}
	// Montgomery curve
	else if (ecmdata->sigma_type == 1) {
		if (ecmdata->state != ECM_STATE_STAGE1_INIT)		// Resuming from a save file
			stop_reason = choose12 (ecmdata, NULL, NULL);
		else if (ecmdata->montg_stage1)				// New Montgomery curve with Montgomery stage 1
			stop_reason = choose12 (ecmdata, &ecmdata->xz, NULL);
		else							// New Montgomery curve, twisted Edwards stage 1
			stop_reason = choose12 (ecmdata, NULL, &ecmdata->e);
	}
	// Edwards curve
	else {
		if (ecmdata->state == ECM_STATE_STAGE1_INIT)		// Starting a new curve
			stop_reason = choose_atkin_morain (ecmdata, &ecmdata->e);
		else							// Resuming curve from a save file
			stop_reason = choose_atkin_morain (ecmdata, NULL);
		if (ecmdata->montg_stage1)
			ed_to_Montgomery (ecmdata);			// Montgomery stage 1 or 2, convert start point (if any) and free Edwards memory
	}
	return (stop_reason);
}

/* Recursively compute big exponent used in Edwards stage 1.  Include an extra 12 or 16 for Suyama or Atkin-Morain constructed curves. */

void ecm_calc_exp_internal (
	void	*sieve_info,	/* Sieve to use calculating primes */
	mpz_t	g,		/* Variable to accumulate multiplied small primes */
	uint64_t B1,		/* ECM stage 1 bound */
	uint64_t *p,		/* Variable to fetch next small prime into */
	uint64_t len)		/* Bits to accumulate (approximate) */
{

/* Use recursion to compute the exponent.  This will perform better because mpz_mul will be handling arguments of equal size. */

	if (len >= 1024) {
		mpz_t	x;
		ecm_calc_exp_internal (sieve_info, g, B1, p, len >> 1);
		mpz_init (x);
		mpz_set_ui (x, 1);
		ecm_calc_exp_internal (sieve_info, x, B1, p, len >> 1);
		mpz_mul (g, x, g);
		mpz_clear (x);
		return;
	}

/* Find all the primes in the range and use as many powers as possible */

	for ( ; *p <= B1 && mpz_sizeinbase (g, 2) < len; *p = sieve (sieve_info)) {
		uint64_t val, max;
		val = *p;
		max = B1 / *p;
		while (val <= max) val *= *p;
		if (sizeof (unsigned int) == 4 && val > 0xFFFFFFFF) {
			mpz_t	mpz_val;
			mpz_init_set_d (mpz_val, (double) val);		/* Works for B1 up to 2^53 */
			mpz_mul (g, g, mpz_val);
			mpz_clear (mpz_val);
		} else
			mpz_mul_ui (g, g, (unsigned int) val);
	}
}

void ecm_calc_exp (
	void	*sieve_info,	/* Sieve to use calculating primes */
	mpz_t	g,		/* Variable to accumulate multiplied small primes */
	uint64_t B1,		/* ECM stage 1 bound */
	uint64_t *p,		/* Variable to fetch next small prime into */
	uint64_t bufsize)	/* Desired limit on size of mpz_t generated (in bytes) */
{

/* See if the desired maximum buffer size limits our ability to reach the B1 target.  Primes generate about 1.44*B1 bits. */

	uint64_t start_prime = *p;
	uint64_t end_prime = B1;
	uint64_t expected_bits = (end_prime - start_prime) * 144/100;
	uint64_t bufsize_in_bits = bufsize * 8;
	// If fully satisfying B1, trim bufsize down for equal length recursion (but big enough to always work)
	if (expected_bits <= bufsize_in_bits) bufsize_in_bits = (end_prime - start_prime) * 150/100;
	// If satisfying B1 requires 2 batches, split range in half
	else if (expected_bits <= 2*bufsize_in_bits) bufsize_in_bits = expected_bits / 2;

/* For ECM, include 12 or 16 for Suyama or Atkin-Morain curve construction. */

	if (start_prime != 2) mpz_set_ui (g, 1);
	else mpz_set_ui (g, 48);			/* Least common multiple of 12 and 16 */

/* Use recursion to compute the exponent.  This will perform better because mpz_mul will be handling arguments of equal size. */

	ecm_calc_exp_internal (sieve_info, g, B1, p, bufsize_in_bits);
}

// Normalize part of the dictionary
bool dict_normalize (
	ecmhandle *ecmdata,
	uint32_t first_index,		// Index of first dictionary entry to normalize
	uint32_t last_index,		// Index of last dictionary entry to normalize
	gwnum	**addr_of_next_available_gwnum)
{
	gwnum *next_available_gwnum = *addr_of_next_available_gwnum;

	// Resuming from a save file will give us a first dictionary entry that is already normalized.  Adjust for that.
	if (first_index == 0 && ecmdata->NAF_dictionary[0].z == NULL) first_index = 1;

	// Use remainder of the pre-allocated NAF_gwnums for temporaries
	gwarray temps = next_available_gwnum;
	ASSERTG (temps + last_index - first_index + 1 <= ecmdata->NAF_gwnums + 3 * ecmdata->NAF_dictionary_size);

	// Loop multiplying all the numbers together into temps
	gwswap (ecmdata->NAF_dictionary[first_index].z, temps[0]);
	for (uint32_t i = 1; i <= last_index - first_index; i++) {
		int options = (i == last_index - first_index) ? 0 : GWMUL_STARTNEXTFFT;
		gwmul3 (&ecmdata->gwdata, temps[i-1], ecmdata->NAF_dictionary[first_index+i].z, temps[i], GWMUL_FFT_S12 | options);
		ASSERTG (ecmdata->NAF_dictionary[first_index+i].t == NULL);
	}

	// Invert the multiplied numbers
	gwnum big_inverse = temps[last_index - first_index];
	if (ecm_modinv (ecmdata, big_inverse)) return (FALSE);

	// Work backwards normalizing each dictionary entry
	for (uint32_t i = last_index - first_index; i; i--) {
		gwmul3 (&ecmdata->gwdata, big_inverse, temps[i-1], temps[i-1], GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);			// Compute this inverse
		gwmul3 (&ecmdata->gwdata, big_inverse, ecmdata->NAF_dictionary[first_index+i].z, big_inverse, GWMUL_STARTNEXTFFT);	// Compute next big inverse
		// Normalize x and y
		gwmul3 (&ecmdata->gwdata, ecmdata->NAF_dictionary[first_index+i].x, temps[i-1], ecmdata->NAF_dictionary[first_index+i].x, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
		gwmul3 (&ecmdata->gwdata, ecmdata->NAF_dictionary[first_index+i].y, temps[i-1], ecmdata->NAF_dictionary[first_index+i].y, GWMUL_STARTNEXTFFT);
		// Free z
		*--next_available_gwnum = ecmdata->NAF_dictionary[first_index+i].z, ecmdata->NAF_dictionary[first_index+i].z = NULL;
	}
	// First entry's inverse is big_inverse
	gwmul3 (&ecmdata->gwdata, ecmdata->NAF_dictionary[first_index].x, big_inverse, ecmdata->NAF_dictionary[first_index].x, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
	gwmul3 (&ecmdata->gwdata, ecmdata->NAF_dictionary[first_index].y, big_inverse, ecmdata->NAF_dictionary[first_index].y, GWMUL_STARTNEXTFFT);
	// Swap first z back and free z
	gwswap (ecmdata->NAF_dictionary[first_index].z, temps[0]);
	*--next_available_gwnum = ecmdata->NAF_dictionary[first_index].z, ecmdata->NAF_dictionary[first_index].z = NULL;

	// Return new next_available_gwnum
	*addr_of_next_available_gwnum = next_available_gwnum;
	return (TRUE);
}

/* Prepare for NAF exponentiation.  Convert the exponent to a bit array indicating which doublings will require an add.  Build the NAF dictionary. */

bool NAF_dictionary_init (
	ecmhandle *ecmdata,
	int	*first_NAF_index,	// First NAF index to use to get the exponentiation started
	uint64_t *num_doublings)	// Number of doubling that NAF exponentiation will need to perform
{
	// Allocate the dictionary and gwnums
	ecmdata->NAF_dictionary = (struct ed *) malloc (ecmdata->NAF_dictionary_size * sizeof (struct ed));
	if (ecmdata->NAF_dictionary == NULL) return (FALSE);
	ecmdata->NAF_gwnums = gwalloc_array (&ecmdata->gwdata, 3 * ecmdata->NAF_dictionary_size);
	if (ecmdata->NAF_gwnums == NULL) return (FALSE);
	memset (ecmdata->NAF_dictionary, 0, ecmdata->NAF_dictionary_size * sizeof (struct ed));
	gwnum	*next_available_gwnum = ecmdata->NAF_gwnums;

// Build the dictionary

	// Copy start point to first dictionary entry.  Free starting point.
	ecmdata->NAF_dictionary[0].x = *next_available_gwnum++;
	ecmdata->NAF_dictionary[0].y = *next_available_gwnum++;
	ecmdata->NAF_dictionary[0].t = *next_available_gwnum++;
	gwcopy (&ecmdata->gwdata, ecmdata->dict_start.x, ecmdata->NAF_dictionary[0].x);
	gwcopy (&ecmdata->gwdata, ecmdata->dict_start.y, ecmdata->NAF_dictionary[0].y);
	if (ecmdata->dict_start.z != NULL) {
		ecmdata->NAF_dictionary[0].z = *next_available_gwnum++;
		gwcopy (&ecmdata->gwdata, ecmdata->dict_start.z, ecmdata->NAF_dictionary[0].z);
	}
	ed_free (ecmdata, &ecmdata->dict_start);
			
	// Double first entry (use last dictionary entry as a temporary)
	struct ed *two = &ecmdata->NAF_dictionary[ecmdata->NAF_dictionary_size-1];
	two->x = *next_available_gwnum++;
	two->y = *next_available_gwnum++;
	two->z = *next_available_gwnum++;
	two->t = *next_available_gwnum++;
	ed_dbl (ecmdata, &ecmdata->NAF_dictionary[0], two, ED_NO_GWALLOC | ED_FFT_S1 | ED_XYZ | ED_RESULT_FOR_ADD | ED_STARTNEXTFFT);

	// Loop making next dictionary entry two more than previous entry.
	// Do this while only using 3 gwnums for every dictionary entry!
	ed_extend (ecmdata, &ecmdata->NAF_dictionary[0], ED_FORCE_EXTEND);
	for (uint32_t i = 1; i < ecmdata->NAF_dictionary_size; i++) {
		// Allocate gwnums for next dictionary entry
		if (i != ecmdata->NAF_dictionary_size-1) {
			ecmdata->NAF_dictionary[i].x = *next_available_gwnum++;
			ecmdata->NAF_dictionary[i].y = *next_available_gwnum++;
			ecmdata->NAF_dictionary[i].z = *next_available_gwnum++;
			ecmdata->NAF_dictionary[i].t = *next_available_gwnum++;
		}
		// Next dictionary entry is two more than previous entry
		int options = (i == ecmdata->NAF_dictionary_size / 2 || i == ecmdata->NAF_dictionary_size - 1) ? 0 : ED_RESULT_FOR_ADD;
		ed_add (ecmdata, &ecmdata->NAF_dictionary[i-1], two, &ecmdata->NAF_dictionary[i], options | ED_NO_GWALLOC | ED_FFT_S1Z | ED_XYZ | ED_STARTNEXTFFT);
		// Switch back from extended coordinates.  Extended coordinates will be generated again after normalization.
		*--next_available_gwnum = ecmdata->NAF_dictionary[i-1].t, ecmdata->NAF_dictionary[i-1].t = NULL;
		if (options == 0) *--next_available_gwnum = ecmdata->NAF_dictionary[i].t, ecmdata->NAF_dictionary[i].t = NULL;
		// 50% of the way through the build, normalize the dictionary.  This will free up enough gwnums to safely do the rest of the dictionary.
		// Also normalize two to save a few transforms for the remaining dictionary entries.
		if (i == ecmdata->NAF_dictionary_size / 2) {
			*--next_available_gwnum = two->t, two->t = NULL;	// Exit extended coordinates, will re-extend after normalization
			ed_swap (*two, ecmdata->NAF_dictionary[i+1]);		// Put two next in the array
			if (!dict_normalize (ecmdata, 0, i+1, &next_available_gwnum)) return (FALSE);
			ed_swap (*two, ecmdata->NAF_dictionary[i+1]);		// Put two back at the end of the array
			// Re-extend by force i-th entry and two
			ecmdata->NAF_dictionary[i].t = *next_available_gwnum++;
			ed_extend (ecmdata, &ecmdata->NAF_dictionary[i], ED_FORCE_EXTEND);
			two->t = *next_available_gwnum++;
			ed_extend (ecmdata, two, ED_FORCE_EXTEND);
		}
	}

	// Normalize the remaining 50% of the dictionary, this will make adding dictionary entries one transform quicker
	if (!dict_normalize (ecmdata, ecmdata->NAF_dictionary_size / 2 + 1, ecmdata->NAF_dictionary_size - 1, &next_available_gwnum)) return (FALSE);

	// Use rest of NAF_gwnums to compute extended coordinates
	for (uint32_t i = 0; i < ecmdata->NAF_dictionary_size; i++) {
		ASSERTG (ecmdata->NAF_dictionary[i].t == NULL && ecmdata->NAF_dictionary[i].z == NULL);
		ASSERTG (next_available_gwnum < ecmdata->NAF_gwnums + 3 * ecmdata->NAF_dictionary_size);
		ecmdata->NAF_dictionary[i].t = *next_available_gwnum++;
		ed_extend (ecmdata, &ecmdata->NAF_dictionary[i], ED_FORCE_EXTEND);
	}

// Allocate a buffer for the NAF_codes

	uint64_t len = mpz_sizeinbase (ecmdata->stage1_exp, 2);
	ecmdata->NAF_codes = (char *) malloc ((size_t) divide_rounding_up (len, 8));
	memset (ecmdata->NAF_codes, 0, (size_t) divide_rounding_up (len, 8));

// The optimal scenario is for the first NAF code to consume as many most-significant bits as possible.  This will reduce the number of doublings
// the exponentiation code needs to perform.

	uint64_t bitnum = len - 1;
	int max_dictionary_value = 2 * ecmdata->NAF_dictionary_size - 1;
	int first_NAF_value;
	for (int tmp = 0; tmp * 2 + 1 <= max_dictionary_value; bitnum--) {
		tmp *= 2;
		if (mpz_tstbit64 (ecmdata->stage1_exp, bitnum)) {
			mpz_clrbit64 (ecmdata->stage1_exp, bitnum);	// Clear bit in case there is a carry later on that alters first_NAF_value
			tmp++;
			first_NAF_value = tmp;
			*num_doublings = bitnum;
		}
		if (bitnum == 0) break;
	}

// Generate the NAF codes working up from the least significant bit

	int NAF_value_under_construction = 0;
	int NAF_value_addin = 1;
	int carry = 0;
	for (bitnum = 0; bitnum < *num_doublings; bitnum++) {
		// Combine carry from negative NAF code and the current exponent bit
		int this_bit = carry + (mpz_tstbit64 (ecmdata->stage1_exp, bitnum) ? 1 : 0);
		carry = this_bit / 2;
		this_bit = this_bit & 1;
		// Flag the start of a new NAF value.  This is where a double and add operation must take place during NAF exponentiation.
		if (NAF_value_under_construction == 0 && this_bit) mpz_setbit64 (ecmdata->stage1_exp, bitnum);
		else mpz_clrbit64 (ecmdata->stage1_exp, bitnum);
		// Continue construction of the NAF value
		if (this_bit) NAF_value_under_construction += NAF_value_addin;
		if (NAF_value_under_construction == 0) continue;
		NAF_value_addin *= 2;
		// Check if there is another bit and whether getting another bit is required to complete the NAF value
		if (bitnum != *num_doublings - 1) {
			if (NAF_value_addin < max_dictionary_value) continue;
			if (NAF_value_under_construction <= max_dictionary_value && NAF_value_addin - NAF_value_under_construction <= max_dictionary_value) continue;
		}
		// We have a completed NAF value, turn it into a code and output it
		int NAF_code;
		if (NAF_value_under_construction <= max_dictionary_value) NAF_code = (NAF_value_under_construction - 1) / 2;
		else NAF_code = (NAF_value_addin - NAF_value_under_construction - 1) / 2 + ecmdata->NAF_dictionary_size, carry = 1;
		for (uint64_t write_bitnum = bitnum; NAF_value_addin >>= 1; write_bitnum--) {
			if (NAF_code & NAF_value_addin) bitset (ecmdata->NAF_codes, write_bitnum);
		}
		// Setup to construct next NAF value
		NAF_value_under_construction = 0;
		NAF_value_addin = 1;
	}
	// If there is a carry, the first NAF value needs to be changed
	if (carry) for (first_NAF_value = first_NAF_value + 1; (first_NAF_value & 1) == 0; first_NAF_value /= 2) *num_doublings = *num_doublings + 1;
	*first_NAF_index = (first_NAF_value - 1) / 2;
	// Success
	return (TRUE);
}

void NAF_code (			// Return the NAF code for the doubling at bit i
	ecmhandle *ecmdata,
	uint64_t i,		// bit number
	int	*NAF_index,	// Returned index into NAF dictionary
	bool	*subtract)	// Returned subtract vs. add boolean
{
	int NAF_code = 0;
	for (int addin = 1; addin < (int) (2*ecmdata->NAF_dictionary_size); addin <<= 1, i++) {
		if (bittst (ecmdata->NAF_codes, i)) NAF_code += addin;
	}
	if (NAF_code < (int) ecmdata->NAF_dictionary_size) *NAF_index = NAF_code, *subtract = FALSE;
	else *NAF_index = NAF_code - ecmdata->NAF_dictionary_size, *subtract = TRUE;
	ASSERTG (*NAF_index < (int) ecmdata->NAF_dictionary_size);
}

void NAF_dictionary_free (
	ecmhandle *ecmdata)
{
	free (ecmdata->NAF_codes), ecmdata->NAF_codes = NULL;
	gwfree_array (&ecmdata->gwdata, ecmdata->NAF_gwnums), ecmdata->NAF_gwnums = NULL;
	free (ecmdata->NAF_dictionary), ecmdata->NAF_dictionary = NULL;
}

uint32_t NAF_best_size (	// Return the best size for the dictionary
	ecmhandle *ecmdata)
{
	uint64_t explen = mpz_sizeinbase (ecmdata->stage1_exp, 2);

// The "optimal" dictionary size occurs when the cost of adding one entry to the dictionary entry equals the savings from reduced ed_adds.
// Initializing a dictionary and FFTing x,y,t costs 1 ed_add with 50% chance two is normalized, 3 muls to normalize z, 2 muls to norm x and y,
// 1 mul for ed_extend, and one forward transform of t.  The ed_add costs 16 transforms half the time and 14 transforms the other half.  The 6 muls
// cost 11 transforms.  For a total of 15 + 11 + 1 = 27 transforms.  The number of ed_adds is explen / (log2 (dictionary_size) + 3).  Each ed_add
// costs 13 transforms plus 1 transform for the ed_dbl_for_add.  NOTE: Twisted Edward curves cost two more transforms.

// Binary search for best dictionary size

#define evalcost(x,s)	{ x.size = s; x.totcost = x.size * single_init_cost + (double) explen / (log2 (x.size) + 3.0) * single_ed_add_cost; }
	double	single_init_cost = (ecmdata->ed_a == NULL) ? 27.0 : 29.0;
	double	single_ed_add_cost = (ecmdata->ed_a == NULL) ? 14.0 : 16.0;
	struct { uint32_t size; double totcost; } best[3], midpoint;
	evalcost (best[0], 1);
	evalcost (best[1], 1000);
	evalcost (best[2], 1000000);
	while (best[2].size - best[0].size > 2) {
		// Work on the bigger of the lower section and upper section
		if (best[1].size - best[0].size > best[2].size - best[1].size) {	// Work on lower section
			evalcost (midpoint, (best[0].size + best[1].size) / 2);
			if (midpoint.totcost < best[1].totcost) {			// Make middle the new end point
				best[2] = best[1];
				best[1] = midpoint;
			} else {							// Create new start point
				best[0] = midpoint;
			}
		} else {								// Work on upper section
			evalcost (midpoint, (best[1].size + best[2].size) / 2);
			if (midpoint.totcost < best[1].totcost) {			// Make middle the new start point
				best[0] = best[1];
				best[1] = midpoint;
			} else {							// Create new end point
				best[2] = midpoint;
			}
		}
	}
#undef evalcost
	return (best[1].size);
}

/* Print message announcing the start of this curve */

void curve_start_msg (
	ecmhandle *ecmdata)
{
	char	buf[120];

	// Replace gwnum's "A n-bit number" text with something a bit more meaningful
	if (ecmdata->N_short_string_rep == NULL) {
		ecmdata->N_short_string_rep = ecmdata->N_full_string_rep = gwmodulo_as_string (&ecmdata->gwdata);
		if (ecmdata->N_short_string_rep[0] == 'A') {
			mpz_t	N;
			int	len;
			mpz_init (N);
			gtompz (ecmdata->N, N);
			ecmdata->N_full_string_rep = (char *) malloc (mpz_sizeinbase (N, 10) + 2);
			mpz_get_str (ecmdata->N_full_string_rep, 10, N);
			len = (int) strlen (ecmdata->N_full_string_rep);
			ecmdata->N_short_string_rep = (char *) malloc (50);
			if (len <= 20) strcpy (ecmdata->N_short_string_rep, ecmdata->N_full_string_rep);
			else sprintf (ecmdata->N_short_string_rep, "%.10s...%s (%d digits)", ecmdata->N_full_string_rep, ecmdata->N_full_string_rep + len - 10, len);
			mpz_clear (N);
		}
	}

	if (ecmdata->curve != 1) OutputStr (ecmdata->thread_num, "\n");
	sprintf (buf, "%s ECM curve #%" PRIu32, ecmdata->N_short_string_rep, ecmdata->curve);
	title (ecmdata->thread_num, buf);

	sprintf (buf, "ECM on %s: %s curve #%" PRIu32 " with s=%" PRIu64 ", B1=%" PRIu64 ", ",
		 ecmdata->N_short_string_rep, ecmdata->sigma_type == 3 ? "GMP-ECM-param3" : ecmdata->sigma_type == 1 ? "Montgomery" : "Edwards",
		 ecmdata->curve, ecmdata->sigma, ecmdata->B);
	if (!ecmdata->optimal_B2 || ecmdata->state >= ECM_STATE_STAGE2) sprintf (buf + strlen (buf), "B2=%" PRIu64 "\n", ecmdata->C);
	else sprintf (buf + strlen (buf), "B2=TBD\n");
	OutputStr (ecmdata->thread_num, buf);
}

/* These routines manage the computing of Q^m (m stands for multiple of D) in stage 2 */

int mQ_preinit (
	ecmhandle *ecmdata,
	Dmultiple_data_map &Dmultiple_map)	// Map used to track the Q^(multiples-of-D) that need to be computed during stage 2 init
{
	uint64_t m;				// The first multiple-of-D needed by stage 2
	Dmultiple_data_map::iterator it_Dmult;

/* Calculate the first needed multiple of D (the first D section in stage 2) */

	m = divide_rounding_up (ecmdata->B2_start, ecmdata->D) + ecmdata->Dsection;
	ASSERTG (m > 1); // Would require special code to the collision between QD and Qprevm

/* The two multiples of D needed for a string of ell_adds are Q^mD and Q^(m+1)D. */
/* Schedule their computation using buffers for the D multiples allocated here. */

	if (!alloc_xz (ecmdata, &ecmdata->Qprevm)) goto oom;
	it_Dmult = Dmultiple_map.find (m);
	if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({m, {0, m}}).first;
	it_Dmult->second.set_val_buffers (&ecmdata->gwdata, ecmdata->Qprevm.x, ecmdata->Qprevm.z, TRUE);

	if (!alloc_xz (ecmdata, &ecmdata->Qm)) goto oom;
	it_Dmult = Dmultiple_map.find (m+1);
	if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({m+1, {0, m+1}}).first;
	it_Dmult->second.set_val_buffers (&ecmdata->gwdata, ecmdata->Qm.x, ecmdata->Qm.z, TRUE);

/* If needed, precompute QD^(E/2) before computing the first batch of Q^multiples-of-D values. */
/* We will need QD^(E/2) to compute subsequent batches of mQx values. */

	if (ecmdata->TWO_FFT_STAGE2 && ecmdata->Dsection + ecmdata->E < ecmdata->numDsections) {
		if (!alloc_xz (ecmdata, &ecmdata->QD_Eover2)) goto oom;
		it_Dmult = Dmultiple_map.find ((uint64_t)(ecmdata->E/2));
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({(uint64_t)(ecmdata->E/2), {0,(uint64_t)(ecmdata->E/2)}}).first;
		it_Dmult->second.set_val_buffers (&ecmdata->gwdata, ecmdata->QD_Eover2.x, ecmdata->QD_Eover2.z, TRUE);
	}

/* All done */

	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}
int mQ_init (
	ecmhandle *ecmdata)
{
	int	stop_reason;

/* Init the arrays and compute first pool of normalized mQx values */

	if (ecmdata->TWO_FFT_STAGE2) {

/* Allocate the mQx pool array */

//GW: Great place to use gwarray!
		ecmdata->mQx = (gwnum *) malloc (ecmdata->E * sizeof (gwnum));
		if (ecmdata->mQx == NULL) goto oom;
		for (uint32_t i = 0; i < ecmdata->E; i++) ecmdata->mQx[i] = NULL;

/* Batch up a bunch of Q^m values and normalize them. */
// MEMPEAK occurs at mQx_count = E-2:  5 + nQx + E + pooling_cost (7 for QD,Qm,Qprevm,QD_Eover2.x + nQx + E-2 + pooling_cost)

		for (ecmdata->mQx_count = 0; ecmdata->mQx_count < ecmdata->E; ecmdata->mQx_count++) {
			struct xz nextQm;

			// If no more Qm's need to be calculated, break out of loop once we've used up all the Qm values else set nextQm to NULL
			if (ecmdata->mQx_count + 2 >= ecmdata->E ||
			    ecmdata->Dsection + ecmdata->mQx_count + 2 >= ecmdata->numDsections) {
				if (ecmdata->Qprevm.x == NULL) break;
				nextQm.x = NULL;
				nextQm.z = NULL;
			}

			// Allocate memory for nextQm, compute next multiple of D
			else {
				nextQm.x = gwalloc (&ecmdata->gwdata);
				if (nextQm.x == NULL) return (OutOfMemory (ecmdata->thread_num));
				nextQm.z = gwalloc (&ecmdata->gwdata);
				if (nextQm.z == NULL) return (OutOfMemory (ecmdata->thread_num));
				// Compute next multiple of D
				ell_add_xz (ecmdata, &ecmdata->Qm, &ecmdata->QD, &ecmdata->Qprevm, &nextQm);
			}

			// Before discarding Qprevm add it to the normalize pool
			ecmdata->mQx[ecmdata->mQx_count] = ecmdata->Qprevm.x;
			stop_reason = add_to_normalize_pool_ro (ecmdata, ecmdata->mQx[ecmdata->mQx_count], ecmdata->Qprevm.z);
			if (stop_reason) return (stop_reason);

			// Shuffle data along
			ecmdata->Qprevm = ecmdata->Qm;
			ecmdata->Qm = nextQm;

			// Check for user requesting a stop, mem settings changed, etc.
			stop_reason = stopCheck (ecmdata->thread_num);
			if (stop_reason) return (stop_reason);
		}

		// Normalize the entire pool (E + 1 values), freeing some gwnums (amount depends on pool type)
		stop_reason = normalize_pool (ecmdata);
		if (stop_reason) return (stop_reason);
		if (ecmdata->factor != NULL) return (0);
		gwfree_cached (&ecmdata->gwdata);
		mallocFreeForOS ();

		// QD is no longer needed
		free_xz (ecmdata, &ecmdata->QD);

		// Return first entry in the mQx array
		ecmdata->mQx_retcount = 0;
	}

/* When mQ_next calls ell_add_xz_3norm, we may need the constant one to avoid an emulation of gwsetaddin.  The gwnum code may also need */
/* the constant one for implementing gwmuladd4.  This code makes sure we do not allocate two instances of the constant one. */

	if (ecmdata->TWO_FFT_STAGE2 && ecmdata->Dsection + ecmdata->mQx_count < ecmdata->numDsections) {
		if (is_gwsetaddin_emulated (&ecmdata->gwdata)) {
			gwuser_init_FFT1 (&ecmdata->gwdata);
		} else {
			gwsetaddin (&ecmdata->gwdata, -1);
		}
	}

/* Initialize Qm_state for mQ_next to use as it sees fit */

	ecmdata->Qm_state = 0;

/* All done */

	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (ecmdata->thread_num));
}
int mQ_next (
	ecmhandle *ecmdata,
	gwnum	*retx,
	gwnum	*retz)
{
	int	stop_reason;

/* The non-normalized 4FFT case - multiply the last Q^m value by Q^D to get the next Q^m value */

	if (!ecmdata->TWO_FFT_STAGE2) {
		// On first call return the Q^mD value calculated by mQ_init and stored in Qprevm
		if (ecmdata->Qm_state == 0) {
			ecmdata->Qm_state = 1;
			*retx = ecmdata->Qprevm.x;
			*retz = ecmdata->Qprevm.z;
		}
		// On second call return the Q^(m+1)D value calculated by mQ_init and stored in Qm
		else if (ecmdata->Qm_state == 1) {
			ecmdata->Qm_state = 2;
			*retx = ecmdata->Qm.x;
			*retz = ecmdata->Qm.z;
		}
		// Compute next Q^(m+i)D value and return it from Qm
		else {
			stop_reason = ell_add_xz_noscr (ecmdata, &ecmdata->Qm, &ecmdata->QD, &ecmdata->Qprevm, &ecmdata->Qprevm);
			if (stop_reason) return (stop_reason);
			xzswap (ecmdata->Qm, ecmdata->Qprevm);
			*retx = ecmdata->Qm.x;
			*retz = ecmdata->Qm.z;
		}
		// Return success
		return (0);
	}

/* The normalized 2FFT case - if mQx pool is empty, compute next batch of Q^m values and normalize them.  Then return them one at a time. */
/* Obviously retz need not be returned since it is always one. */

	if (ecmdata->mQx_count == 0) {
		ASSERTG (ecmdata->E % 2 == 0);

		// Calculate new mQx_count
		if (ecmdata->numDsections - ecmdata->Dsection > ecmdata->E) ecmdata->mQx_count = ecmdata->E;
		else ecmdata->mQx_count = (int) (ecmdata->numDsections - ecmdata->Dsection);

		// Loop computing two mQx values
		// MEMPEAK occurs at i=E/2-1: 4 + nQx + E + pooling_cost (QD_Eover2.x, i*2, nextQm, nextQm2, gg, nQx, pooling_cost)

		for (uint32_t i = 0; i < ecmdata->E/2; i++) {
			struct xz nextQm, nextQm2;

			// If no more Qm's need to be calculated, break out of loop
			if (i >= ecmdata->mQx_count) break;

			// Allocate memory for nextQm
			nextQm.x = gwalloc (&ecmdata->gwdata);
			if (nextQm.x == NULL) return (OutOfMemory (ecmdata->thread_num));
			nextQm.z = ecmdata->mQx[i];

			// Calc next mQx[i] = mQx[i+E/2] + QD*E/2, diff mQx[i]
			ell_add_xz_3norm (ecmdata, ecmdata->mQx[i+ecmdata->E/2], ecmdata->QD_Eover2.x, ecmdata->mQx[i], &nextQm);

			// Compute mQx[i+E/2] if it will be needed
			if (i + ecmdata->E/2 < ecmdata->mQx_count) {
				// Allocate memory for nextQm2
				nextQm2.x = ecmdata->mQx[i+ecmdata->E/2];
				nextQm2.z = gwalloc (&ecmdata->gwdata);
				if (nextQm.z == NULL) return (OutOfMemory (ecmdata->thread_num));

				// Calc next mQx[i+E/2] = next mQx[i] + QD*E/2, diff mQx[i+E/2]
				ell_add_xz_2norm (ecmdata, &nextQm, ecmdata->QD_Eover2.x, ecmdata->mQx[i+ecmdata->E/2], &nextQm2);

				// Add nextQm2 to normalize pool
				ecmdata->mQx[i+ecmdata->E/2] = nextQm2.x;
				stop_reason = add_to_normalize_pool_ro (ecmdata, ecmdata->mQx[i+ecmdata->E/2], nextQm2.z);
				if (stop_reason) return (stop_reason);
			}

			// Add nextQm to normalize pool
			ecmdata->mQx[i] = nextQm.x;
			stop_reason = add_to_normalize_pool_ro (ecmdata, ecmdata->mQx[i], nextQm.z);
			if (stop_reason) return (stop_reason);

			// Check for user requesting a stop, mem settings changed, etc.
			stop_reason = stopCheck (ecmdata->thread_num);
			if (stop_reason) return (stop_reason);
		}

		// Normalize the entire pool, freeing some gwnums (amount depends on pool type)
		stop_reason = normalize_pool (ecmdata);
		if (stop_reason) return (stop_reason);
		if (ecmdata->factor != NULL) return (0);
		gwfree_cached (&ecmdata->gwdata);
		mallocFreeForOS ();

		// Return first entry in the mQx array
		ecmdata->mQx_retcount = 0;
	}

	/// Return next entry from the pool
	*retx = ecmdata->mQx[ecmdata->mQx_retcount++];
	ecmdata->mQx_count--;
	return (0);
}
void mQ_term (
	ecmhandle *ecmdata)
{
	free_xz (ecmdata, &ecmdata->Qm);
	free_xz (ecmdata, &ecmdata->Qprevm);
	free_xz (ecmdata, &ecmdata->QD);
	free_xz (ecmdata, &ecmdata->QD_Eover2);
	if (ecmdata->mQx != NULL) {
		for (uint32_t i = 0; i < ecmdata->E; i++) gwfree (&ecmdata->gwdata, ecmdata->mQx[i]);
		free (ecmdata->mQx); ecmdata->mQx = NULL;
	}
}

// Alternative mQ_preinit/mQ_init/mQ_next/mQ_term routines for polymult ECM that returns batches of mQx values.  The tmps needed by normalize are pre-allocated
// in polyGaux.  Upon completion of a batch, caller saves part of the batch in polyGaux to make the next batch a little bit faster.
int mQ_preinit_array (
	ecmhandle *ecmdata,
	Dmultiple_data_map &Dmultiple_map)	// Map used to track the Q^(multiples-of-D) that need to be computed during stage 2 init
{
	uint64_t m;				// The first multiple-of-D needed by stage 2
	Dmultiple_data_map::iterator it_Dmult;

/* Calculate the first needed multiple of D (the first D section in stage 2) */

	m = divide_rounding_up (ecmdata->B2_start, ecmdata->D) + ecmdata->Dsection;
	ASSERTG (m > 1); // Would require special code to the collision between QD and Qprevm

/* The first two multiples of D needed for a string of ell_adds are Q^mD and Q^(m+1)D. */
/* Schedule their computation using buffers for the D multiples allocated here. */

	ecmdata->Qprevm.x = ecmdata->polyGaux[0];
	ecmdata->Qprevm.z = gwalloc (&ecmdata->gwdata);
	if (ecmdata->Qprevm.z == NULL) return (OutOfMemory (ecmdata->thread_num));
	it_Dmult = Dmultiple_map.find (m);
	if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({m, {0, m}}).first;
	it_Dmult->second.set_val_buffers (&ecmdata->gwdata, ecmdata->Qprevm.x, ecmdata->Qprevm.z, TRUE);

	ecmdata->Qm.x = ecmdata->polyGaux[1];
	ecmdata->Qm.z = gwalloc (&ecmdata->gwdata);
	if (ecmdata->Qm.z == NULL) return (OutOfMemory (ecmdata->thread_num));
	it_Dmult = Dmultiple_map.find (m+1);
	if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({m+1, {0, m+1}}).first;
	it_Dmult->second.set_val_buffers (&ecmdata->gwdata, ecmdata->Qm.x, ecmdata->Qm.z, TRUE);

/* If needed, precompute QD^(polyGaux_size/2) before computing the first batch of Q^multiples-of-D values. */
/* We will need QD^(polyGaux_size/2) to compute subsequent batches of mQx values. */

	if (!alloc_xz (ecmdata, &ecmdata->QD_Eover2)) return (OutOfMemory (ecmdata->thread_num));
	it_Dmult = Dmultiple_map.find ((uint64_t)(ecmdata->polyGaux_size/2));
	if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({(uint64_t)(ecmdata->polyGaux_size/2), {0,(uint64_t)(ecmdata->polyGaux_size/2)}}).first;
	it_Dmult->second.set_val_buffers (&ecmdata->gwdata, ecmdata->QD_Eover2.x, ecmdata->QD_Eover2.z, TRUE);

/* All done */

	return (0);
}
int mQ_init_array (
	ecmhandle *ecmdata)
{
	int	stop_reason;

/* Batch up several Q^m values and normalize them.  Makes calculating remainder of the first batch a tiny bit faster. */

	for (int i = 2; i < ecmdata->polyGaux_size; i++) {
		struct xz nextQm;

		// Allocate memory for nextQm, compute next multiple of D
		nextQm.x = ecmdata->polyGaux[i];
		nextQm.z = gwalloc (&ecmdata->gwdata);
		if (nextQm.z == NULL) return (OutOfMemory (ecmdata->thread_num));

		// Compute next multiple of D
		ell_add_xz (ecmdata, &ecmdata->Qm, &ecmdata->QD, &ecmdata->Qprevm, &nextQm);
		// Before discarding Qprevm add it to the normalize pool
		stop_reason = add_to_normalize_pool_ro (ecmdata, ecmdata->Qprevm.x, ecmdata->Qprevm.z);
		if (stop_reason) return (stop_reason);

		// Shuffle data along
		ecmdata->Qprevm = ecmdata->Qm;
		ecmdata->Qm = nextQm;

		// Check for user requesting a stop, mem settings changed, etc.
		stop_reason = stopCheck (ecmdata->thread_num);
		if (stop_reason) return (stop_reason);
	}

	// Add last two entries to the normalize pool
	stop_reason = add_to_normalize_pool_ro (ecmdata, ecmdata->Qprevm.x, ecmdata->Qprevm.z);
	if (stop_reason) return (stop_reason);
	ecmdata->Qprevm.z = NULL;
	stop_reason = add_to_normalize_pool_ro (ecmdata, ecmdata->Qm.x, ecmdata->Qm.z);
	if (stop_reason) return (stop_reason);
	ecmdata->Qm.z = NULL;

	// Normalize the entire pool, freeing some gwnums (amount depends on pool type)
	stop_reason = normalize_pool (ecmdata);
	if (stop_reason) return (stop_reason);
	if (ecmdata->factor != NULL) return (0);
	gwfree_cached (&ecmdata->gwdata);
	mallocFreeForOS ();

	// From here on out, use polyGaux for all our normalize temporaries
	ecmdata->use_preallocated_norm_temps = TRUE;

	// QD is no longer needed
	free_xz (ecmdata, &ecmdata->QD);

	// Return first entry in the mQx array
	ecmdata->mQx_retcount = 0;

/* When mQ_next calls ell_add_xz_3norm, we may need the constant one to avoid an emulation of gwsetaddin.  The gwnum code may also need */
/* the constant one for implementing gwmuladd4.  This code makes sure we do not allocate two instances of the constant one. */

	if (is_gwsetaddin_emulated (&ecmdata->gwdata)) {
		gwuser_init_FFT1 (&ecmdata->gwdata);
	} else {
		gwsetaddin (&ecmdata->gwdata, -1);
	}

/* Initialize Qm_state for mQ_next_array to use as it sees fit */

	ecmdata->Qm_state = 0;

/* All done */

	return (0);
}
int mQ_next_array (
	ecmhandle *ecmdata,
	gwnum	*mQx_array)		// Array to return mQx values (polyG)
{
	int	next_Gaux = 0;		// Next polyGaux entry to use when a gwnum temp is needed
	int	stop_reason;

//GW!!! Multithread?!!  Access to modinv pool will be tricky
/* Polymult ECM peak memory usage occurs in this routine.  There are several options for computing next batch of mQx values. */
//GW:  It might pay to double the number of modinvs, halving normalize pool memory requirements and making more 8 and 10 FFT ell_adds possible */

	// On first call, the first G(X) values to return are already computed and normalized in polyGaux.
	// On subsequent calls, the last G(X) values from the previous batch are normalized in polyGaux.  In either case, move the values to polyG.
	// The movement is quirky for subsequent calls when polyGaux_size is odd (we must make sure we are properly overwriting polyG values).
	if (ecmdata->Qm_state == 0) {
		for (uint64_t i = 0; i < ecmdata->polyGaux_size; i++) gwswap (mQx_array[i], ecmdata->polyGaux[i]);
	} else {
		for (uint64_t i = 0; i < ecmdata->polyGaux_size; i++) gwswap (mQx_array[i], ecmdata->polyGaux[(i + (ecmdata->polyGaux_size & 1)) % ecmdata->polyGaux_size]);
	}

	// Loop computing chains of mQx values
	uint64_t initial_i = (ecmdata->Qm_state == 0 ? ecmdata->polyGaux_size : 0);
	for (uint64_t i = initial_i; i < initial_i + ecmdata->polyGaux_size / 2; i++) {
		struct xz prevQm, Qm;

		// On first call, set prevQm and Qm to the already computed values that were stored in polyGaux
		if (ecmdata->Qm_state == 0) {
			prevQm.x = mQx_array[i - 2 * (ecmdata->polyGaux_size / 2)];
			prevQm.z = NULL;
			Qm.x = mQx_array[i - (ecmdata->polyGaux_size / 2)];
			Qm.z = NULL;
		}
		// On subsequent calls, set prevQm and Qm to the last G(x) values from the previous batch that were stored in polyGaux
		// We will be overwriting values in the mQx_array.
		else {
			prevQm.x = mQx_array[i];
			prevQm.z = NULL;
			Qm.x = mQx_array[i + (ecmdata->polyGaux_size / 2)];
			Qm.z = NULL;
		}

		// Loop computing values in this chain
		for (uint64_t j = i; j < ecmdata->poly_size; j += ecmdata->polyGaux_size / 2) {
			struct xz nextQm;

			// Compute next value in the chain
			nextQm.x = mQx_array[j];
			if (ecmdata->norm_pool_temps_count) nextQm.z = ecmdata->norm_pool_temps[--ecmdata->norm_pool_temps_count];
			else nextQm.z = ecmdata->polyGaux[next_Gaux++];
			ASSERTG (next_Gaux <= ecmdata->polyGaux_size);

			if (Qm.z == NULL) ell_add_xz_3norm (ecmdata, Qm.x, ecmdata->QD_Eover2.x, prevQm.x, &nextQm);
			else if (prevQm.z == NULL) ell_add_xz_2norm (ecmdata, &Qm, ecmdata->QD_Eover2.x, prevQm.x, &nextQm);
			else ell_add_xz_1norm (ecmdata, &Qm, ecmdata->QD_Eover2.x, &prevQm, &nextQm);

			// Before discarding Qprevm add it to the normalize pool
			if (prevQm.z != NULL) {
				// Normalize could need a temporary.  If we have a temp available add it to the norm_pool_temps.
				if (ecmdata->norm_pool_temps_count < 1 && next_Gaux < ecmdata->polyGaux_size)
					ecmdata->norm_pool_temps[ecmdata->norm_pool_temps_count++] = ecmdata->polyGaux[next_Gaux++];
				stop_reason = add_to_normalize_pool_ro (ecmdata, prevQm.x, prevQm.z);
				if (stop_reason) return (stop_reason);
			}

			// Shuffle data along
			prevQm = Qm;
			Qm = nextQm;
		}

		// Add last two entries to the normalize pool
		if (prevQm.z != NULL) {
			// Normalize could need a temporary.  If we have a temp available add it to the norm_pool_temps.
			if (ecmdata->norm_pool_temps_count < 1 && next_Gaux < ecmdata->polyGaux_size)
				ecmdata->norm_pool_temps[ecmdata->norm_pool_temps_count++] = ecmdata->polyGaux[next_Gaux++];
			stop_reason = add_to_normalize_pool_ro (ecmdata, prevQm.x, prevQm.z);
			if (stop_reason) return (stop_reason);
		}
		if (Qm.z != NULL) {
			// Normalize could need a temporary.  If we have a temp available add it to the norm_pool_temps.
			if (ecmdata->norm_pool_temps_count < 1 && next_Gaux < ecmdata->polyGaux_size)
				ecmdata->norm_pool_temps[ecmdata->norm_pool_temps_count++] = ecmdata->polyGaux[next_Gaux++];
			stop_reason = add_to_normalize_pool_ro (ecmdata, Qm.x, Qm.z);
			if (stop_reason) return (stop_reason);
		}
	}
   
	// Normalize the entire pool.  Normalize could need two more temporaries.  If we have temps available add them to the norm_pool_temps.
	while (ecmdata->norm_pool_temps_count < 2 && next_Gaux < ecmdata->polyGaux_size)
		ecmdata->norm_pool_temps[ecmdata->norm_pool_temps_count++] = ecmdata->polyGaux[next_Gaux++];
	stop_reason = normalize_pool (ecmdata);
	if (stop_reason) return (stop_reason);
	if (ecmdata->factor != NULL) return (0);

	// Set flag indicating this is not the first mQ_next_array call
	ecmdata->Qm_state = 1;

	// All done
	return (0);
}
void mQ_term_array (
	ecmhandle *ecmdata)
{
	gwfree (&ecmdata->gwdata, ecmdata->Qprevm.z), ecmdata->Qprevm.z = NULL;
	gwfree (&ecmdata->gwdata, ecmdata->Qm.z), ecmdata->Qm.z = NULL;
	free_xz (ecmdata, &ecmdata->QD);
	free_xz (ecmdata, &ecmdata->QD_Eover2);
}

/* Record the amount of memory being used by this thread.  Until we get to stage 2, ECM uses 9 gwnums (x, z, AD4, 6 for ell_mul). */

void ecm_stage1_memory_usage (
	int	thread_num,
	ecmhandle *ecmdata)
{
	if (ecmdata->montg_stage1) set_memory_usage (thread_num, 0, cvt_gwnums_to_mem (&ecmdata->gwdata, 9));
	else set_memory_usage (thread_num, 0, cvt_gwnums_to_mem (&ecmdata->gwdata, ecmdata->NAF_dictionary_size * 3 + 8));
}

/* Figure out the maximum safe poly2 size, where "safe" means "safe from gwnum roundoff errors" */

uint64_t max_safe_poly2_size (
	gwhandle *gwdata,
	uint64_t poly1_size,
	uint64_t desired_poly2_size)
{
	// Let user adjust the safety margin for poly multiplications
	double safety_adjust = IniGetFloat (INI_FILE, "PolySafetyMargin", (float) 0.0);
	// Is the desired size safe?  If so, return the desired poly2 size
	if (gw_passes_safety_margin (gwdata, polymult_safety_margin (poly1_size, desired_poly2_size) + safety_adjust)) return (desired_poly2_size);
	// Binary search for the maximum possible poly2 size (with poly1 size as a minimum)
	if (!gw_passes_safety_margin (gwdata, polymult_safety_margin (poly1_size, poly1_size) + safety_adjust)) return (0);
	uint64_t known_safe_size = poly1_size;
	while (desired_poly2_size - known_safe_size >= 2) {
		uint64_t midpoint = (known_safe_size + desired_poly2_size) / 2;
		if (gw_passes_safety_margin (gwdata, polymult_safety_margin (poly1_size, midpoint) + safety_adjust)) known_safe_size = midpoint;
		else desired_poly2_size = midpoint;
	}
	return (known_safe_size);
}

/* Cost out a stage 2 plan with the given D, normalize_pool algorithm, and 2 vs. 4 FFT stage 2 setting. */

struct ecm_stage2_cost_data {
	/* Cost data common to ECM, P-1, P+1 */
	struct common_cost_data c;
	/* ECM specific data sent to cost function follows */
	int	impl;		/* Many possible implementations.  2 vs. 4 FFT, N^2 or several 3-or-more-mult poolings. */
	bool	saving_Ftree_to_disk; /* TRUE if the two Ftree rows are saved to disk rather than memory */
	/* ECM specific data returned from cost function follows */
	int	stage2_type;	/* Prime pairing vs. polymult */
	int	pool_type;	/* Modular inverse pooling implementation */
	int	TWO_FFT_STAGE2;	/* 2 vs. 4 FFT implementation */
	int	E;		/* In 2-FFT stage 2, number of D sections pooled for a single modular inverse */
	int	Ftree_polys_in_mem; /* If there is excess memory available, we can save Ftree polys in memory rather than on disk or rebuilding. */
};

double ecm_stage2_cost (
	void	*data)		/* ECM specific costing data */
{
	struct ecm_stage2_cost_data *cost_data = (struct ecm_stage2_cost_data *) data;
	int	e;
	int	beste;		/* The best E value when using mQx pooling */
	double	numgcdsections;
	double	cost, cost_modinv;

/* Prime pairing and poly mult stage 2 requires completely different cost functions. */
/* First estimate the stage 2 costs using prime pairing. */

	if (cost_data->stage2_type == ECM_STAGE2_PAIRING) {

/* MQ_init requires B2_start is larger than D */

	    if (cost_data->c.B2_start <= cost_data->c.D) return (-1.0);

/* Define constants for the number of transforms for various operations. */
/* The ELL_ADD and ELL_DBL costs assume FFT inputs and adds 2 transforms for FFTing the outputs */
/* The pooling costs assume the two inputs are FFTed (saving two transforms) AND does not add one transform for the cost of FFTing the output! */

#define ELL_ADD_COST		12
#define ELL_ADD_2NORM_COST	10
#define ELL_ADD_3NORM_COST	8
#define ELL_DBL_COST		11
#define	AVG_ELL_ADD_NORM_COST	((ELL_ADD_3NORM_COST + ELL_ADD_2NORM_COST) / 2)
#define	AVG_ELL_DBLADD_COST	((ELL_DBL_COST + ELL_ADD_COST) / 2)
#define N_SQUARED_POOL_COST(n)	(((double)((n)+3)) * ((double)((n)+3)) - 13.0)
#define MULT3_POOL_COST(n)	((double)(n) * (9-2))
#define MULT3POINT44_POOL_COST(n) ((double)(n) * (10.3333333333333-2.0))
#define MULT3POINT57_POOL_COST(n) ((double)(n) * (10.7142857142857-2.0))
#define NQX_SETUP_COST(fft4)	(((fft4) || cost_data->c.D % 3 != 0) ? \
				 1.0 * ELL_ADD_COST + 2.0 * ELL_DBL_COST + ((double) (cost_data->c.D / 2 - 1)) * ELL_ADD_COST : \
				 5.0 * ELL_ADD_COST + 3.0 * ELL_DBL_COST + ((double) (cost_data->c.D / 6 - 2)) * 2.0 * ELL_ADD_COST)

/* Cost (in FFTs) of a 4FFT stage 2 using this D		*/
/* The cost will be:						*/
/*	multiplier * D / 4 ell_add_xzs + pool_cost (totrels) +	*/
/*	totrels (each normalized nQx must be FFTed) +		*/
/*	(C-B)/D ell_add_xzs +					*/
/*	#primes*4						*/
/* The memory consumed will be:					*/
/*	9 + totrels gwnums in main stage 2 loop if N^2 pooling	*/
/* or	8 + totrels*2 gwnums in nQx setup if 3N pooling		*/
/* or	8 + totrels*1.33+2 gwnums in nQx setup if 3.44N pooling */
/* or	8 + totrels*1.14+5 gwnums in nQx setup if 3.57N pooling */

//GW:  what about doing the pool in chunks O(n^2) grows quickly!  There is a best "e" value
	    if (cost_data->impl == 0) {
		cost_data->c.est_init_transforms = NQX_SETUP_COST (TRUE) + N_SQUARED_POOL_COST (cost_data->c.totrels);  // Cost of nQx calculations
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections * ELL_ADD_COST;
		cost_modinv = cost_data->c.modinv_cost;
		cost_data->TWO_FFT_STAGE2 = FALSE;
		cost_data->pool_type = POOL_N_SQUARED;
		cost_data->E = 0;
		cost_data->c.stage2_numvals = 9 + cost_data->c.totrels;
	    }

	    if (cost_data->impl == 1) {
		// We need 8 + totrels * 2 available gwnums for the nQx setup.  Return negative efficiency if there aren't enough temporaries available.
		if (8 + cost_data->c.totrels * 2 + cost_data->c.numvals_consumed_by_pairmap > cost_data->c.numvals) return (-1.0);
		cost_data->c.est_init_transforms = NQX_SETUP_COST (TRUE) + MULT3_POOL_COST (cost_data->c.totrels);  // Cost of nQx calculations
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections * ELL_ADD_COST;
		cost_modinv = cost_data->c.modinv_cost;
		cost_data->TWO_FFT_STAGE2 = FALSE;
		cost_data->pool_type = POOL_3MULT;
		cost_data->E = 0;
		cost_data->c.stage2_numvals = 8 + cost_data->c.totrels * 2;
	    }

	    if (cost_data->impl == 2) {
		// We need 8 + totrels * 1.33 + 2 available gwnums for the nQx setup.  Return negative efficiency if there aren't enough temporaries available.
		if (8 + cost_data->c.totrels * 4 / 3 + 2 + cost_data->c.numvals_consumed_by_pairmap > cost_data->c.numvals) return (-1.0);
		cost_data->c.est_init_transforms = NQX_SETUP_COST (TRUE) + MULT3POINT44_POOL_COST (cost_data->c.totrels);  // Cost of nQx calculations
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections * ELL_ADD_COST;
		cost_modinv = cost_data->c.modinv_cost;
		cost_data->TWO_FFT_STAGE2 = FALSE;
		cost_data->pool_type = POOL_3POINT44MULT;
		cost_data->E = 0;
		cost_data->c.stage2_numvals = 8 + cost_data->c.totrels * 4 / 3 + 2;
	    }

	    if (cost_data->impl == 3) {
		// We need 8 + totrels * 1.14 + 5 available gwnums for the nQx setup.  Return negative efficiency if there aren't enough temporaries available.
		if (8 + cost_data->c.totrels * 8 / 7 + 5 + cost_data->c.numvals_consumed_by_pairmap > cost_data->c.numvals) return (-1.0);
		cost_data->c.est_init_transforms = NQX_SETUP_COST (TRUE) + MULT3POINT57_POOL_COST (cost_data->c.totrels);  // Cost of nQx calculations
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections * ELL_ADD_COST;
		cost_modinv = cost_data->c.modinv_cost;
		cost_data->TWO_FFT_STAGE2 = FALSE;
		cost_data->pool_type = POOL_3POINT57MULT;
		cost_data->E = 0;
		cost_data->c.stage2_numvals = 8 + cost_data->c.totrels * 8 / 7 + 5;
	    }

/* Cost (in FFTs) of a 2FFT stage 2 using this D								*/
/* The cost will be:												*/
/*	multiplier * D / 6 ell_add_xzs + pool_cost (totrels) +							*/
/*	totrels (each normalized nQx must be FFTed) +								*/
/*	first pool cost: E ell_add_xzs + pool_cost (E+1) +							*/
/*	(numgcdsections-1) * subsequent pool cost:  E/2 ell_add_3norms + E/2 ell_add_2norms + pool_cost (E) +	*/
/*	(C-B)/D (each normalized mQx must be FFTed) +								*/
/*	(C-B)/D/E * (pool_cost (e) + modinv_cost) +								*/
/*	#primes*2												*/
/* The memory consumed will be:											*/
/*	max (12 + totrels + 1,         5 + totrels + e + 1) gwnums if N^2 pooling				*/
/* or	max (12 + totrels * 2,         5 + totrels + e * 2) gwnums if 3N pooling				*/
/* or	max (12 + totrels * 4 / 3 + 2, 5 + totrels + e * 4 / 3 + 2) gwnums if 3.44N pooling			*/
/* or	max (12 + totrels * 8 / 7 + 5, 5 + totrels + e * 8 / 7 + 5) gwnums if 3.57N pooling			*/

/* Since computing mQx costs 12*E for the first mQx pool and just 9*E for subsequent mQx pools, we want to maximize the number of mQx pools as long */
/* as the savings from reducing E in the first pool exceeds the cost of a modular inverse.  The best number of mQx pools is the maximum value satisfying */
/*	(#pools)(#pools-1) < (3 * numDsections / modinv_cost), we turn this into a quadratic equation: */
/*	pools^2 - pools - 3  * numDsections / modinv_cost = 0, and solve using the quadratic formula (-b +/- sqrt (b^2 - 4ac)) / 2a */
/* From #pools value, generate the smallest even E value that creates the desired number of mQx pools. */

	    if (cost_data->impl >= 4) {
		numgcdsections = floor ((1.0 + sqrt (1.0 + 12.0 * (double) cost_data->c.numDsections / cost_data->c.modinv_cost)) / 2.0);
		beste = (int) ceil ((double) cost_data->c.numDsections / numgcdsections);
		if ((beste & 1) && numgcdsections > 1.0) beste++;
	    }

	    if (cost_data->impl == 4) {
		int	max_n_squared_e = (int) sqrt (2 * (cost_data->c.modinv_cost + 9)); // Maximum sensible value for E using O(N^2) pooling
		// We need 13 + totrels + 1 available gwnums for the nQx setup.  Return negative efficiency if there aren't enough temporaries available.
		if (13 + cost_data->c.totrels + 1 + cost_data->c.numvals_consumed_by_pairmap > cost_data->c.numvals) return (-1.0);
		// In mQ_init, we use 5 + nQx gwnums for other purposes. In stage 2 main loop, we use 4 + nQx gwnums for other purposes.
		// Pooling cost is just one.  Figure out the maximum even E value we can use.
		e = (cost_data->c.numvals - 5 - cost_data->c.totrels - 1) & ~1;
		if (e <= 0) return (-1.0);
		// If e exceeds the maximum sensible value for N^2 pooling, use the maximum sensible E value
		if (e > max_n_squared_e) e = max_n_squared_e;
		// If this lets us select beste do so.  Otherwise, pick the smallest even E that generates the same number of pools as using the maximum E.
		if (e >= beste) e = beste;
		else {
			numgcdsections = ceil ((double) cost_data->c.numDsections / (double) e);
			e = (int) ceil ((double) cost_data->c.numDsections / numgcdsections);
			if (e & 1) e++;
		}
		// Cost out this E value
		cost_data->c.est_init_transforms = NQX_SETUP_COST (FALSE) +					// Cost of nQx below D setup
						   N_SQUARED_POOL_COST (cost_data->c.totrels) +			// pool cost for nQx
						   e * ELL_ADD_COST + N_SQUARED_POOL_COST (e+1);		// Cost of first GCD section in mQ_init
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections +			// Cost of FFTing each normalized mQx value
			(numgcdsections - 1.0) * (e * AVG_ELL_ADD_NORM_COST + N_SQUARED_POOL_COST (e));		// Cost of remaining GCD sections
		cost_modinv = cost_data->c.modinv_cost * (1 + numgcdsections);
		cost_data->TWO_FFT_STAGE2 = TRUE;
		cost_data->pool_type = POOL_N_SQUARED;
		cost_data->E = e;
		cost_data->c.stage2_numvals = _intmax (12 + cost_data->c.totrels + 1, 5 + cost_data->c.totrels + e + 1);
	    }

	    if (cost_data->impl == 5) {
		// We need 12 + totrels * 2 available gwnums for the nQx setup.  Return negative efficiency if there aren't enough temporaries available.
		if (12 + cost_data->c.totrels * 2 + cost_data->c.numvals_consumed_by_pairmap > cost_data->c.numvals) return (-1.0);
		// In mQ_init, we use 5 + nQx gwnums for other purposes.  In stage 2 main loop, we use 4 + nQx gwnums for other purposes.
		// Figure out the maximum even E value we can use.
		e = ((cost_data->c.numvals - 5 - cost_data->c.totrels) / 2) & ~1;
		// If this lets us select beste do so.  Otherwise, pick the smallest even E that generates the same number of pools as using the maximum E.
		if (e >= beste) e = beste;
		else {
			numgcdsections = ceil ((double) cost_data->c.numDsections / (double) e);
			e = (int) ceil ((double) cost_data->c.numDsections / numgcdsections);
			if (e & 1) e++;
		}
		// Cost out this E value
		cost_data->c.est_init_transforms = NQX_SETUP_COST (FALSE) +					// Cost of nQx below D setup
						   MULT3_POOL_COST (cost_data->c.totrels) +			// pool cost for nQx
						   e * ELL_ADD_COST + MULT3_POOL_COST (e+1);			// Cost of first GCD section in mQ_init
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections +			// Cost of FFTing each normalized mQx value
			(numgcdsections - 1.0) * (e * AVG_ELL_ADD_NORM_COST + MULT3_POOL_COST (e));		// Cost of remaining GCD sections
		cost_modinv = cost_data->c.modinv_cost * (1 + numgcdsections);
		cost_data->TWO_FFT_STAGE2 = TRUE;
		cost_data->pool_type = POOL_3MULT;
		cost_data->E = e;
		cost_data->c.stage2_numvals = _intmax (12 + cost_data->c.totrels * 2, 5 + cost_data->c.totrels + e * 2);
	    }

	    if (cost_data->impl == 6) {
		// We need 12 + totrels * 4 / 3 + 2 available gwnums for the nQx setup.  Return negative efficiency if there aren't enough temporaries available.
		if (12 + cost_data->c.totrels * 4 / 3 + 2 + cost_data->c.numvals_consumed_by_pairmap > cost_data->c.numvals) return (-1.0);
		// In mQ_init, we use 5 + nQx gwnums for other purposes.  In stage 2 main loop, we use 4 + nQx gwnums for other purposes.
		// Figure out the maximum even E value we can use.
		e = ((cost_data->c.numvals - 5 - cost_data->c.totrels - 2) * 3 / 4) & ~1;
		// If this lets us select beste do so.  Otherwise, pick the smallest even E that generates the same number of pools as using the maximum E.
		if (e >= beste) e = beste;
		else {
			numgcdsections = ceil ((double) cost_data->c.numDsections / (double) e);
			e = (int) ceil ((double) cost_data->c.numDsections / numgcdsections);
			if (e & 1) e++;
		}
		// Cost out this E value
		cost_data->c.est_init_transforms = NQX_SETUP_COST (FALSE) +					// Cost of nQx below D setup
						   MULT3POINT44_POOL_COST (cost_data->c.totrels) +		// pool cost for nQx
						   e * ELL_ADD_COST + MULT3POINT44_POOL_COST (e+1);		// Cost of first GCD section in mQ_init
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections +			// Cost of FFTing each normalized mQx value
			(numgcdsections - 1.0) * (e * AVG_ELL_ADD_NORM_COST + MULT3POINT44_POOL_COST (e));	// Cost of remaining GCD sections
		cost_modinv = cost_data->c.modinv_cost * (1 + numgcdsections);
		cost_data->TWO_FFT_STAGE2 = TRUE;
		cost_data->pool_type = POOL_3POINT44MULT;
		cost_data->E = e;
		cost_data->c.stage2_numvals = _intmax (12 + cost_data->c.totrels * 4 / 3 + 2, 5 + cost_data->c.totrels + e * 4 / 3 + 2);
	    }

	    if (cost_data->impl == 7) {
		// We need 12 + totrels * 8 / 7 + 5 available gwnums for the nQx setup.  Return negative efficiency if there aren't enough temporaries available.
		if (12 + cost_data->c.totrels * 8 / 7 + 5 + cost_data->c.numvals_consumed_by_pairmap > cost_data->c.numvals) return (-1.0);
		// In mQ_init, we use 5 + nQx gwnums for other purposes.  In stage 2 main loop, we use 4 + nQx gwnums for other purposes.
		// Figure out the maximum even E value we can use.
		e = ((cost_data->c.numvals - 5 - cost_data->c.totrels - 5) * 7 / 8) & ~1;
		// If this lets us select beste do so.  Otherwise, pick the smallest even E that generates the same number of pools as using the maximum E.
		if (e >= beste) e = beste;
		else {
			numgcdsections = ceil ((double) cost_data->c.numDsections / (double) e);
			e = (int) ceil ((double) cost_data->c.numDsections / numgcdsections);
			if (e & 1) e++;
		}
		// Cost out this E value
		cost_data->c.est_init_transforms = NQX_SETUP_COST (FALSE) +					// Cost of nQx below D setup
						   MULT3POINT57_POOL_COST (cost_data->c.totrels) +		// pool cost for nQx
						   e * ELL_ADD_COST + MULT3POINT57_POOL_COST (e+1);		// Cost of first GCD section in mQ_init
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections +			// Cost of FFTing each normalized mQx value
			(numgcdsections - 1.0) * (e * AVG_ELL_ADD_NORM_COST + MULT3POINT57_POOL_COST (e));	// Cost of remaining GCD sections
		cost_modinv = cost_data->c.modinv_cost * (1 + numgcdsections);
		cost_data->TWO_FFT_STAGE2 = TRUE;
		cost_data->pool_type = POOL_3POINT57MULT;
		cost_data->E = e;
		cost_data->c.stage2_numvals = _intmax (12 + cost_data->c.totrels * 8 / 7 + 5, 5 + cost_data->c.totrels + e * 8 / 7 + 5);
	    }

/* We've only calculated the cost of the relprimes less than D.  For the relative primes above D, we perform one ell_add for each rel_prime to calculate. */
/* We assume relp set -1 is not used, which means there are totrels - numrels relative primes above D to calculate. */

	    cost_data->c.est_init_transforms += (cost_data->c.totrels - cost_data->c.numrels) * ELL_ADD_COST;		// Cost for relprimes above D

/* Any intermediate relp_sets also cost one luc_add for each relprime.  Partial intermediate sets also must be accounted for. */

	    cost_data->c.est_init_transforms +=
		cost_data->c.numrels * cost_data->c.relp_sets[1] * ELL_ADD_COST +					  // Cost for full intermediate relp_sets
		one_based_modulo (cost_data->c.totrels, cost_data->c.numrels) * cost_data->c.relp_sets[2] * ELL_ADD_COST; // Cost for partial intermediate relp_sets

/* Each Dmultiple costs either an ell_dbl or ell_add.  Here we have to guess how many Dmultiples are needed.  The most expensive D-multiple is likely the */
/* calculation of B2_start -- 2*log2(B2_start/D).  In addition, we'll assume one more D-multiple for each relp_set. */

	    cost_data->c.est_init_transforms += (2.0 * log2((double)((cost_data->c.B2_start + cost_data->c.D / 2)) / cost_data->c.D) + cost_data->c.multiplier) * AVG_ELL_DBLADD_COST;

/* Each normalized nQx value will be FFTed once */

	    cost_data->c.est_stage2_transforms += cost_data->c.totrels;

/* Finally, each prime pair and prime single costs one or two 2-FFT multiplies */

	    cost_data->c.est_stage2_transforms += (cost_data->c.est_numpairs + cost_data->c.est_numsingles) * (cost_data->impl >= 4 ? 2.0 : 4.0);

/* Compute the total cost including pairing runtime and modular inverses */
/* The costs calculated above certainly won't be correct for all current and future CPUs.  Let the attentive user correct the stage 2 cost by */
/* monitoring the estimated stage 2 / stage 1 runtime ratio vs. the actual stage 2 / stage 1 runtime. */

	    cost = (cost_data->c.est_init_transforms + cost_data->c.est_stage2_transforms) * IniGetFloat (INI_FILE, "EcmTransformCost", (float) 1.0);
	    cost += cost_data->c.est_pairing_runtime * IniGetFloat (INI_FILE, "EcmPairRatioAdjust", 1.0);
	    cost += cost_modinv;
	}

/* Polymult stage 2 cost using all numvals */

	else {
		uint64_t poly_size;
		double	num_polys, mem_used_by_a_gwnum, mem_saved_by_compression, mem_used_by_polymult;

		// If this FFT size cannot safely support multiplying two polys together return negative efficiency.
		poly_size = cost_data->c.numrels;
		if (max_safe_poly2_size (cost_data->c.gwdata, poly_size, poly_size) < poly_size) return (-1.0);

		// A small optimization to reduce time spent evaluating cost of different poly sizes.  If poly_size < 8, assume old pairing code will be faster.
		if (poly_size < 8) return (-1.0);
		// If doubling a poly size is safe and 16 polys will fit in memory, then assume some bigger poly will be better.  Return negative cost.
		// This optimization did not work when testing generic mod ECM on F7 and F11 with 8GB memory and a small B1 of 8000.
		// if (16 * 2*poly_size < cost_data->c.numvals && max_safe_poly2_size (cost_data->c.gwdata, 2*poly_size, 2*poly_size) >= 2*poly_size) return (-1.0);

//GW: this minimum may or may not be right, we do require at least 4 polyFtree levels
if (cost_data->c.numvals < 6*32) return (-1.0);

/* Loop increasing B2_start based on the fact that numDsections will be a multiple of poly_size.  In essence this gives a free increase in the B2 endpoint. */

		uint64_t num_polyG = divide_rounding_up (cost_data->c.numDsections, poly_size);
		if (num_polyG < 2) num_polyG = 2; // Limitation of current code
		cost_data->c.numDsections = num_polyG * poly_size;
		if (cost_data->c.gap_end == 0) {
			for ( ; ; ) {
				uint64_t B2_end = cost_data->c.B2_start + cost_data->c.numDsections * cost_data->c.D;
				uint64_t B2_start = round_down_to_multiple_of (B2_end / cost_data->c.first_missing_prime, cost_data->c.D);
				if (cost_data->c.B2_start >= B2_start) break;
				cost_data->c.B2_start = B2_start;
			}
		}

		// Stage 2 polymult needs 4 polys (polyF, polyR, polyGH) plus two Ftree polys unless they are saved to disk plus 1/7 for modinv temps.
		// NOTE: Though not implemented yet, if num_polyGs == 1 polyH is not needed (and maybe the 1/7 for modinv temps too)
		cost_data->pool_type = POOL_3POINT57MULT;
		num_polys = (cost_data->saving_Ftree_to_disk ? 4.0 : 6.0) + 1.0 / 7.0;
		if (num_polyG == 1) num_polys -= 1.0;

		// Adjust numvals used in stage 2 to account for polymult memory and savings from compressing polyF and polyR.
		// NOTE: Though not implemented yet, if num_polyGs == 1 polyF and polyR are not compressed
		cost_data->c.stage2_numvals = (int) (num_polys * poly_size) + 1;		//GW: plus several? (QDEover2, gg, 5 for modinv temps, others?)
		mem_used_by_a_gwnum = (double) array_gwnum_size (cost_data->c.gwdata);
		mem_saved_by_compression = 2 * (double) poly_size * mem_used_by_a_gwnum * (1.0 - cost_data->c.poly_compression);
		if (num_polyG == 1) mem_saved_by_compression = 0.0;
		mem_used_by_polymult = (double) polymult_mem_required (cost_data->c.polydata, poly_size, poly_size, POLYMULT_INVEC1_MONIC);
		cost_data->c.stage2_numvals += (int) floor ((mem_used_by_polymult - mem_saved_by_compression) / mem_used_by_a_gwnum + 0.5);
		if (cost_data->c.stage2_numvals > cost_data->c.numvals) return (-1.0);

/* Estimating the cost of a polymult is difficult.  See P-1 costing function for explanation of the code below. */

		double	polymult_cost;		// Cost of a polymult (per coefficient) compared to a pass 1 transform
		polymult_cost = 2.145 + 0.58 * log2 ((double) poly_size / 105.0);
		double L2_cache_size = (CPU_NUM_L2_CACHES > 0 ? CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES : 0) * 1024.0;
		double polymult_mem_per_thread = mem_used_by_polymult / cost_data->c.stage2_threads;
		if (polymult_mem_per_thread >= 8 * L2_cache_size) polymult_cost *= 2.05;
		else if (polymult_mem_per_thread > L2_cache_size) polymult_cost *= 1.0 + 1.05 * (polymult_mem_per_thread - L2_cache_size) / (7 * L2_cache_size);
		polymult_cost *= IniGetFloat (INI_FILE, "EcmPolymultCostAdjust", 1.0);

/* Obviously no prime pairing costs */

		cost_data->c.est_pairing_runtime = 0.0;

/* Cost of building initial nQx values and calculating each D value */
//GW: Lifted from impl==7, check that new mQ_init behaves the same way
		cost_data->c.est_init_transforms = NQX_SETUP_COST (FALSE) +					// Cost of nQx below D setup
						   MULT3POINT57_POOL_COST (cost_data->c.numrels + 1);		// pool cost for nQx and QD
		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections +			// Cost of FFTing each normalized mQx value
			num_polyG * (poly_size * AVG_ELL_ADD_NORM_COST + MULT3POINT57_POOL_COST (poly_size));	// Cost of computing multiples of D
		cost_modinv = cost_data->c.modinv_cost * (1 + num_polyG);

/* Building polyF requires log2(poly_size) polymults with inverse FFT and FFT and on each output poly coefficient (always assume input coefficients are FFTed) */

		cost_data->c.est_init_polymult = log2 ((double) poly_size) * poly_size * polymult_cost;
		cost_data->c.est_init_transforms += log2 ((double) poly_size) * poly_size * 2.0;

/* Building polyR requires log2(poly_size) iterations doing two progressively larger polymults with an FFT and inverse FFT on each poly coefficient */
/* That's two size 1 polymults, two size 2 polymults, ..., two size poly_size/2 polymults.  That's equivalent to two size poly_size polymults. */

		cost_data->c.est_init_polymult += 2 * (poly_size*2) * polymult_cost;
		cost_data->c.est_init_transforms += 2 * (poly_size*2) * 2.0;

/* Building polyG occurs num_polyG times.  Each requires log2(poly_size) polymults with an FFT and inverse FFT on each poly coefficient */

		cost_data->c.est_stage2_polymult = num_polyG * log2 ((double) poly_size) * poly_size * polymult_cost;
		cost_data->c.est_stage2_transforms += num_polyG * log2 ((double) poly_size) * poly_size * 2.0;

/* Building polyH occurs num_polyG-1 times.  Each requires three poly_size times poly_size polymults with an FFT and inverse FFT on each poly coefficient */
/* There are fewer output gwnum transforms thanks to POLYMULT_MULHI and POLYMULT_MULLO. */

		cost_data->c.est_stage2_polymult += (num_polyG-1) * 3.0 * (double) (poly_size*2) * polymult_cost;
		cost_data->c.est_stage2_transforms += (num_polyG-1) * (double) (poly_size*2 + poly_size + poly_size) * 2;

/* Building initial scaled remainders requires one poly_size times poly_size polymult with an FFT and inverse FFT on each poly coefficient */
/* There are fewer output gwnum transforms thanks to POLYMULT_MULHI. */

		cost_data->c.est_stage2_polymult += (double) (poly_size*2) * polymult_cost;
		cost_data->c.est_stage2_transforms += (double) poly_size * 2.0;

/* Rebuilding F(X) tree requires up to (log2(poly_size) - 2) polymults on poly_size coefficients. */
/* If excess memory is available, it can be used to save Ftree polys.  This will save on the costs of rebuilding F(X). */
/* Saving (and restoring) two Ftree rows to/from disk requires gwtobinary and binarytogw on two polys.  This timed as equal to 13.2 transforms per gwnum (b=2) */
/* and 687 transforms (b!=2) on a single-threaded FMA machine (ignoring disk read/write time).  For lack of a better place to record this cost, add it to cost_modinv. */

		int	num_Ftree_polys, Ftree_polys_in_mem, more_Ftree_polys_in_mem, total_Ftree_polys_in_mem, needing_rebuild;
		num_Ftree_polys = (int) ceil (log2 ((double) poly_size));
		Ftree_polys_in_mem = (cost_data->saving_Ftree_to_disk ? 0 : 2);
		more_Ftree_polys_in_mem = (int) divide_rounding_down (cost_data->c.numvals - cost_data->c.stage2_numvals, poly_size);
//gw		if (Ftree_polys_in_mem + more_Ftree_polys_in_mem > num_Ftree_polys) more_Ftree_polys_in_mem = num_Ftree_polys - Ftree_polys_in_mem;
//gw: saving the 0/1 row will be tricky!
if (Ftree_polys_in_mem + more_Ftree_polys_in_mem > num_Ftree_polys - 1) more_Ftree_polys_in_mem = num_Ftree_polys - Ftree_polys_in_mem - 1;
		total_Ftree_polys_in_mem = Ftree_polys_in_mem + more_Ftree_polys_in_mem;
		cost_data->Ftree_polys_in_mem = total_Ftree_polys_in_mem;
		cost_data->c.stage2_numvals += more_Ftree_polys_in_mem * poly_size;
		if (total_Ftree_polys_in_mem < 2) cost_modinv += (cost_data->c.gwdata->b == 2 ? 13.2 : 687.0) * cost_data->c.stage2_threads * (2 - total_Ftree_polys_in_mem) * poly_size;

/* Rebuilding F(X) tree requires (log2(poly_size) - Ftree_polys_in_mem_or_on_disk) polymults on poly_size coefficients */

		needing_rebuild = num_Ftree_polys - intmax (total_Ftree_polys_in_mem, 2);
		if (needing_rebuild > 0) {
			cost_data->c.est_stage2_polymult += needing_rebuild * poly_size * polymult_cost;
			cost_data->c.est_stage2_transforms += needing_rebuild * poly_size * 2.0;
		}

/* Moving down the scaled remainder tree requires log2(polysize) iterations of two full-size times half-size polymults. */
/* There are fewer output gwnum transforms thanks to POLYMULT_MULHI. */

		cost_data->c.est_stage2_polymult += log2 ((double) poly_size) * (3 * poly_size) * polymult_cost;
		cost_data->c.est_stage2_transforms += log2 ((double) poly_size) * poly_size * 2.0;

/* Finally we multiply all the H(X) coefficients together */

		cost_data->c.est_stage2_transforms += poly_size * 2.0;

/* Compute the total cost including modular inverses.  Right now the computed cost except modinv is in terms of stage2_fftlen. */

		cost = (cost_data->c.est_init_transforms + cost_data->c.est_stage2_transforms) * IniGetFloat (INI_FILE, "EcmTransformCost", (float) 1.0);
		cost += (cost_data->c.est_init_polymult + cost_data->c.est_stage2_polymult) * IniGetFloat (INI_FILE, "EcmPolyRatioAdjust", 1.0);
		cost *= (double) cost_data->c.stage2_fftlen / (double) cost_data->c.stage1_fftlen;	// Convert to stage1_fftlen cost
		cost += cost_modinv;
	}

/* Save the final costs fudged by an auto-adjusting value.  If there are more stage 2 threads than stage 1, adjust the cost down.  Yes, the cost in terms */
/* of transforms is not reduced by extra threads, but the time spent in stage 2 is reduced.  The user of Stage2ExtraThreads expects the reduced time spent */
/* in stage 2 to result in a larger optimal B2.  Reducing the cost will make this happen. */

	cost *= IniGetFloat (INI_FILE, "EcmStage2RatioAdjust", 1.0);
	cost *= (double) cost_data->c.stage1_threads / (double) cost_data->c.stage2_threads;
	cost_data->c.stage2_cost = cost;						// Raw cost of stage 2
	cost_data->c.est_stage2_stage1_ratio = cost / cost_data->c.stage1_cost;		// Estimated time of stage 2 relative to stage 1
	cost_data->c.total_cost = cost_data->c.stage1_cost + cost_data->c.stage2_cost + cost_data->c.gcd_cost;

/* Calculate and return the efficiency which is ECM curve "value" divided by total cost of running the curve */

	cost_data->c.efficiency = kruppa_adjust (cost_data->c.B2_start + cost_data->c.numDsections * cost_data->c.D, cost_data->c.B1) / cost_data->c.total_cost;
	return (cost_data->c.efficiency);
}

/* Given a number of temporaries derived from a memory limit, choose best value for D, 2 or 4 FFT stage 2, and best algorithm for normalize_pool. */
/* Returns the cost (in FFTs) of implementing stage 2 as well as other cost data needed to implement stage 2 */

double ecm_stage2_impl_given_numvals (
	ecmhandle *ecmdata,
	uint64_t numvals,				/* Number of gwnum temporaries available */
	int	forced_stage2_type,			/* 0 = cost pairing, 1 = cost poly, 99 = cost both */
	struct ecm_stage2_cost_data *return_cost_data)	/* Returned extra data from ECM costing function */
{
	double	efficiency, best_efficiency;	/* Best efficiency for each of the 16 possible stage 2 implementations */
	struct ecm_stage2_cost_data cost_data;	/* Extra data passed to and returned from ECM costing function */

// The cost of stage 1 (in FFTs) is about 25.55 * B1 (measured at 25.55 for B1=250000 for Montgomery and 21.95 for Edwards with dictionary size 512).

	if (ecmdata->sigma_type) cost_data.c.stage1_cost = 25.55 * (double) ecmdata->B;
	else cost_data.c.stage1_cost = 21.95 * (double) ecmdata->B;

/* Compute the expected compression of poly1 using polymult_preprocess.  Default compression is usually 1.6%.  Some FFT sizes may have more padding */
/* and thus more compression.  If poly compression option is set we'll get another 12.5%. */

	cost_data.c.poly_compression = (double) gwfftlen (&ecmdata->gwdata) * (double) sizeof (double) / (double) array_gwnum_size (&ecmdata->gwdata);
	if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == 2) cost_data.c.poly_compression *= 0.875;
	if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == 0) cost_data.c.poly_compression = 1.0;
//GW: factor this compressed memory into numvals!

/* Whether Ftree rows can be stored on disk may make a significant difference */

	cost_data.saving_Ftree_to_disk = IniGetInt (INI_FILE, "EcmSaveFtreeToDisk", 0);

/* Find the least costly stage 2 plan looking at the four combinations of 2 vs. 4 FFT and pool type N^2 vs. 3-MULT vs. 3.44-MULT vs. 3.57-MULT. */

	best_efficiency = -1.0;
	cost_data.c.numvals = numvals;
	cost_data.c.use_poly_D_data = FALSE;
	cost_data.c.centers_on_Dmultiple = TRUE;
	cost_data.c.required_missing = ecmdata->required_missing;
	cost_data.c.gwdata = &ecmdata->gwdata;
	cost_data.c.polydata = &ecmdata->polydata;
	cost_data.c.stage1_fftlen = ecmdata->stage1_fftlen;
	cost_data.c.stage2_fftlen = gwfftlen (&ecmdata->gwdata);
	cost_data.c.stage1_threads = ecmdata->stage1_threads;
	cost_data.c.stage2_threads = ecmdata->stage2_threads;
	init_gcd_costs (ecmdata->gwdata.bit_length, &cost_data);
	for (int stage2_type = 0; stage2_type <= 1; stage2_type++) {	// Prime pairing vs. polymult

/* Check which stage 2 types we are to cost - mainly used for QA/debugging */

	    if (forced_stage2_type != 99 && forced_stage2_type != stage2_type) continue;

/* Loop through all possible implementations */

	    for (int impl = 0; impl <= 7; impl++) {			// Eight possible implementations

/* For polymult only cost out 3.57-MULT modinv */

		if (stage2_type == 1) impl = 7;

/* Check for QA'ing a specific ECM implementation type */

		if (QA_TYPE != 0 && QA_TYPE != impl + 1) continue;

/* Cost out an ECM stage 2 implementation.  2 vs. 4 FFT, N^2 vs. 3-MULT vs. 3.44-MULT vs. 3.57-MULT pooling.  All implementations must account for 9 gwnum */
/* temporaries required by the main stage 2 loop (6 for computing mQx, gg, 2 for ell_add_xz_noscr temps).  Keep track of the best implementation. */

		cost_data.stage2_type = (stage2_type == 0 ? ECM_STAGE2_PAIRING : ECM_STAGE2_POLYMULT);
		cost_data.impl = impl;
		cost_data.c.use_poly_D_data = (stage2_type == 1);
		efficiency = best_stage2_impl (ecmdata->B, ecmdata->first_relocatable, ecmdata->last_relocatable, ecmdata->C_done, ecmdata->C,
					       numvals - 9, &ecm_stage2_cost, &cost_data);
		if (efficiency > best_efficiency) {
			best_efficiency = efficiency;
			*return_cost_data = cost_data;
		}
	    }
//GW:  play with e = num modinvs.  That is, break the N_SQUARED pool in half or thirds, etc.
//			do we need a binary search on breaking up the 3N pooling into multiple segments?
//	is this irrelevant in polymult era?
	}

/* Return our best implementation */

	return (best_efficiency);
}

/* Choose most effective B2 for an ECM curve given number of gwnums we are allowed to allocate */

int ecm_choose_B2 (
	ecmhandle *ecmdata,
	uint64_t numvals,
	int	forced_stage2_type,		/* 0 = cost pairing, 1 = cost poly, 99 = cost both */
	char	*optimal_B2_msg)		/* Message to be output if this optimal B2 selection is used */
{
	int	max_B2mult;
	struct ecm_stage2_cost_data cost_data;	/* Extra data passed to ECM costing function */
	struct ecm_stage2_efficiency {
		int	i;
		int	actual_i;
		double	efficiency;
	} best[3];

// Assume that if we have 200 numvals or more that polymult will be better than prime pairing

	if (forced_stage2_type == 99 && numvals >= 200) forced_stage2_type = 1;

// Macro to evaluate a curve's efficiency (curve's Kruppa value / curve's cost)

	max_B2mult = IniGetInt (INI_FILE, "MaxOptimalB2Multiplier", (int) (numvals < 100000 ? numvals * 100 : 10000000));
#define kruppa(x,B2mult)	x.i = B2mult; \
				if (x.i > max_B2mult) x.i = max_B2mult; \
				ecmdata->C = x.i * ecmdata->B; \
				x.efficiency = ecm_stage2_impl_given_numvals (ecmdata, numvals, forced_stage2_type, &cost_data); \
				if (x.efficiency < 0.0) x.actual_i = x.i; \
				else x.actual_i = (int) ((cost_data.c.B2_start + cost_data.c.numDsections * cost_data.c.D) / ecmdata->B);

/* Look for the best B2 which is likely between 50 * B1 and 150 * B1 when prime pairing.  With polymult best B2 is apt to be much larger. */
/* If optimal is not between these bounds, don't worry we'll locate the optimal spot anyway. */

	kruppa (best[0], 50);
	kruppa (best[1], best[0].actual_i * 5);
	kruppa (best[2], best[1].actual_i * 5);
	if (best[0].efficiency < 0.0 && best[1].efficiency < 0.0 && best[2].efficiency < 0.0) return (FALSE);

/* Handle case where midpoint has worse efficiency than the start point.  The search code requires best[1] is better than best[0] and best[2]. */
/* There is an edge case where max_safe_poly_size returns a very small value and there is no case where best[1] is better than best[0]. */
/* Thus, the test for best[0].i > 1.  We'll eventually use a larger FFT size with a larger safe poly_size. */

	while (best[0].efficiency > best[1].efficiency && best[0].i > 1) {
		best[2] = best[1];
		best[1] = best[0];
		kruppa (best[0], best[0].i / 2);
	}

/* Handle case where midpoint has worse efficiency than the end point */
/* The search code requires best[1] is better than best[0] and best[2] */

	while (best[2].efficiency > best[1].efficiency && best[2].i < max_B2mult) {
		best[0] = best[1];
		best[1] = best[2];
		kruppa (best[2], best[1].i * 5);
	}

/* Find the best B2.  We use a binary-like search to speed things up (new in version 30.3b3). */

	for ( ; ; ) {
		struct ecm_stage2_efficiency midpoint;

		ASSERTG (best[1].efficiency >= best[0].efficiency);
		ASSERTG (best[1].efficiency >= best[2].efficiency);

		// Quit when both gaps are too small to have a midpoint
		int	lower_gap = best[1].i - best[0].actual_i;
		int	upper_gap = best[2].i - best[1].actual_i;
		if (lower_gap < 2 && upper_gap < 2) break;

		// Work on the bigger of the lower section and upper section
		if (lower_gap > upper_gap) {					// Work on lower section
			kruppa (midpoint, best[0].actual_i + lower_gap / 2);
			if (midpoint.efficiency > best[1].efficiency) {		// Make middle the new end point
				best[2] = best[1];
				best[1] = midpoint;
			} else {						// Create new start point
				best[0] = midpoint;
			}
		} else {							// Work on upper section
			kruppa (midpoint, best[1].actual_i + upper_gap / 2);
			if (midpoint.efficiency > best[1].efficiency) {		// Make middle the new start point
				best[0] = best[1];
				best[1] = midpoint;
			} else {						// Create new end point
				best[2] = midpoint;
			}
		}
	}

/* Return the best B2 */

	ecmdata->C = best[1].actual_i * ecmdata->B;
	sprintf (optimal_B2_msg, "Optimal B2 is %d*B1 = %" PRIu64 ".  ", best[1].actual_i, ecmdata->C);

/* Return success */

	return (TRUE);
}

/* Choose the best implementation of ECM stage 2 given the current memory settings.  We choose the best values for D, 2 or 4 FFT stage 2, and best */
/* algorithm for normalize_pool that reduces the number of multiplications with the current memory constraints.  Return the cost (in terms of gwnum */
/* transforms) of the selected stage 2 implementation. */

double ecm_stage2_impl (
	ecmhandle *ecmdata,
	unsigned int memory,		/* Available memory is in MB */
	int	forced_stage2_type,	/* 0 = cost pairing, 1 = cost poly, 99 = cost both */
	char	*msgbuf)		/* Messages to output if we end up using this implementation plan */
{
	uint64_t numvals;					/* Number of gwnums we can allocate */
	struct ecm_stage2_cost_data cost_data;			/* Data passed to and returned from ECM costing function */

/* Figure out how many gwnum values fit in our memory limit. */

	numvals = cvt_mem_to_array_gwnums_adj (&ecmdata->gwdata, memory, 0.0);
	if (numvals < 13) numvals = 13;

/* Set first_relocatable for future best_stage2_impl calls that cost prime pairing. */
/* Override B2 with optimal B2 based on amount of memory available. */

	if (ecmdata->state == ECM_STATE_MIDSTAGE) {
		// Note: differs from P-1 and P+1 code in that "more C" is not supported
		ecmdata->first_relocatable = ecmdata->C_done = ecmdata->B;
		ecmdata->last_relocatable = 0;
		if (ecmdata->optimal_B2 && !ecm_choose_B2 (ecmdata, numvals, forced_stage2_type, msgbuf + strlen (msgbuf))) return (0.0);
	}
	if (ecmdata->state >= ECM_STATE_STAGE2 && ecmdata->stage2_type == ECM_STAGE2_POLYMULT) {
		ecmdata->first_relocatable = ecmdata->C_done > ecmdata->B ? ecmdata->C_done : ecmdata->B;
		ecmdata->last_relocatable = 0;
	}

/* If are continuing from a save file that was in stage 2, check to see if we currently have enough memory to continue with the save file's */
/* stage 2 implementation.  Also check if we now have significantly more memory available and stage 2 is not near complete such that a new */
/* stage 2 implementation might give us a faster stage 2 completion. */

//GW: These are rather arbitrary heuristics
	if (ecmdata->state >= ECM_STATE_STAGE2 &&				// Continuing a stage 2 save file and
	    ecmdata->stage2_type == ECM_STAGE2_PAIRING &&			// prime pairing and
	    ecmdata->pairmap != NULL &&						// we have a pairmap and
	    numvals >= ecmdata->stage2_numvals &&				// we have enough memory and
	    forced_stage2_type != 1 &&						// not forcing polymult stage 2
	    (numvals < ecmdata->stage2_numvals * 2 ||				// less than twice as much memory now available or
	     ecmdata->Dsection >= ecmdata->numDsections / 2))			// stage 2 more than half done
		return (0);							// Use old plan

/* If we are contemplating ditching the save file pairmap, figure out which non-relocatable primes are definitely included in the stage 2 */
/* accumulator.  Set C_done appropriately, but do not change first_relocatable as there is no guarantee which relocatables are in the accumulator. */

	if (ecmdata->state == ECM_STATE_STAGE2 && ecmdata->stage2_type == ECM_STAGE2_PAIRING) {
		int max_relp_set = get_max_relp_set (ecmdata->relp_sets);
		if (ecmdata->Dsection > max_relp_set) ecmdata->C_done = ecmdata->B2_start + (ecmdata->Dsection - max_relp_set) * ecmdata->D;
	}

/* Find the least costly stage 2 plan.  It is possible no plan will be found (user wants polymult only and there are no safe polys for this FFT length). */
/* Try various values of D until we find the best one. */

	cost_data.c.total_cost = 0.0;						// Special value indicating no workable plan found
	ecm_stage2_impl_given_numvals (ecmdata, numvals, forced_stage2_type, &cost_data);
	sprintf (msgbuf + strlen (msgbuf), "Actual B2 will be %" PRIu64 ".  Curve is worth %.2f B2=100*B1 curves.\n",
		 cost_data.c.B2_start + cost_data.c.numDsections * cost_data.c.D,
		 kruppa_adjust (cost_data.c.B2_start + cost_data.c.numDsections * cost_data.c.D, ecmdata->B));

/* If are continuing from a save file that was in stage 2 and the new plan doesn't look significant better than the old plan, then */
/* we use the old plan and its partially completed pairmap. */

	if (ecmdata->state >= ECM_STATE_STAGE2 &&				// Continuing a stage 2 save file and
	    ecmdata->stage2_type == ECM_STAGE2_PAIRING &&			// prime pairing and
	    ecmdata->pairmap != NULL &&						// we have a pairmap and
	    forced_stage2_type != 1 &&						// not forcing polymult stage 2
	    numvals >= ecmdata->stage2_numvals &&				// We have enough memory and
	    (cost_data.c.total_cost == 0.0 ||					// there is no new plan
	     cost_data.c.stage2_numvals < ecmdata->stage2_numvals * 2))		// new plan does not use significantly more memory
		return (1.0);							// Use old plan (without costing it)

/* If are continuing from a save file that was in stage 2, toss the save file's pair map. */

	if (ecmdata->state >= ECM_STATE_STAGE2) {
		free (ecmdata->pairmap);
		ecmdata->pairmap = NULL;
		ecmdata->pairmap_size = 0;
	}

/* Return zero efficiency to indicate no stage 2 plan found (forcing polymult and nothing passes safety margins) */

	if (cost_data.c.total_cost == 0.0) return (0.0);

/* Set all the variables needed for this stage 2 plan */

	ecmdata->stage2_numvals = cost_data.c.stage2_numvals;
	ecmdata->numrels = cost_data.c.numrels;
	ecmdata->pool_type = cost_data.pool_type;
	ecmdata->D = cost_data.c.D;
	ecmdata->B2_start = cost_data.c.B2_start;
	ecmdata->numDsections = cost_data.c.numDsections;
	ecmdata->Dsection = 0;									//GW: old ecm code did not do this.  did i break pairing?
	ecmdata->stage2_type = cost_data.stage2_type;
	if (ecmdata->stage2_type == ECM_STAGE2_PAIRING) {
		ecmdata->TWO_FFT_STAGE2 = cost_data.TWO_FFT_STAGE2;
		ecmdata->E = cost_data.E;
		ecmdata->totrels = (int) cost_data.c.totrels;
		memcpy (ecmdata->relp_sets, cost_data.c.relp_sets, sizeof (ecmdata->relp_sets));
		ecmdata->max_pairmap_Dsections = cost_data.c.max_pairmap_Dsections;
		if (ecmdata->state < ECM_STATE_STAGE2 || ecmdata->last_relocatable > ecmdata->B2_start) ecmdata->last_relocatable = ecmdata->B2_start;
	} else {
		ecmdata->poly_size = cost_data.c.numrels;
		ecmdata->Ftree_polys_in_mem = cost_data.Ftree_polys_in_mem;
	}

/* Output data regarding our cost estimates so user can make adjustments when our estimates are wildly off the mark */

	// Output additional debugging upon request
	if (IniGetInt (INI_FILE, "Stage2Estimates", 0)) {
		if (ecmdata->stage2_type == ECM_STAGE2_PAIRING)
			sprintf (msgbuf + strlen (msgbuf), "Est. pair%%: %5.2f, init transforms: %.0f, main loop transforms: %.0f\n",
				 cost_data.c.est_pair_pct * 100.0, cost_data.c.est_init_transforms, cost_data.c.est_stage2_transforms);
		else
			sprintf (msgbuf + strlen (msgbuf), "Est. init transforms: %.0f, main loop transforms: %.0f, init poly cost: %.0f, main loop poly cost: %.0f\n",
				 cost_data.c.est_init_transforms, cost_data.c.est_stage2_transforms, cost_data.c.est_init_polymult, cost_data.c.est_stage2_polymult);
	}
	// Output estimated runtime of stage 2 vs. stage 1.  The better the accuracy, the better our deduced optimal B2 value and the
	// better we can judge pairing vs. polymult crossover.  User can create ratio adjustments if necessary.
	sprintf (msgbuf + strlen (msgbuf), "Estimated stage 2 vs. stage 1 runtime ratio: %.3f\n", cost_data.c.est_stage2_stage1_ratio);
	ecmdata->est_stage2_stage1_ratio = cost_data.c.est_stage2_stage1_ratio;

	// Return stage 2 plan's efficiency
	return (cost_data.c.efficiency);
}

/* Routines to create and read save files for an ECM factoring job */

#define ECM_MAGICNUM	0x1725bcd9
//#define ECM_VERSION	2			// Version 30.4.  Better pairing using relative primes above D/2. */
//#define ECM_VERSION	3			// Version 30.7.  Better pairing with relp_sets, compressed pairing map. */
//#define ECM_VERSION	4			// Version 30.10.  Polymult. */
//#define ECM_VERSION	5			// Version 30.13.  Sigma uint64_t. */
#define ECM_VERSION	6			// Version 30.19.  Edwards curves. */

void ecm_save (
	ecmhandle *ecmdata)
{
	int	fd;
	struct work_unit *w = ecmdata->w;
	uint32_t sum = 0;

/* Create the intermediate file */

	fd = openWriteSaveFile (&ecmdata->write_save_file_state);
	if (fd < 0) return;

/* Write the file header */

	if (! write_header (fd, ECM_MAGICNUM, ECM_VERSION, w)) goto writeerr;

/* Write the file data */

	if (! write_uint32 (fd, ecmdata->curve, &sum)) goto writeerr;
	if (! write_uint64 (fd, ecmdata->average_B2, NULL)) goto writeerr;
	if (! write_uint32 (fd, ecmdata->state, &sum)) goto writeerr;
	if (! write_uint64 (fd, ecmdata->sigma, NULL)) goto writeerr;
	if (! write_uint64 (fd, ecmdata->B, &sum)) goto writeerr;
	if (! write_uint64 (fd, ecmdata->C, &sum)) goto writeerr;
	if (! write_uint32 (fd, ecmdata->sigma_type, &sum)) goto writeerr;
	if (! write_uint32 (fd, ecmdata->montg_stage1, &sum)) goto writeerr;

/* Write the stage-specific data */

	if (ecmdata->state == ECM_STATE_STAGE1) {
		if (ecmdata->montg_stage1) {
			if (! write_uint64 (fd, ecmdata->stage1_prime, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->xz.x, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->xz.z, &sum)) goto writeerr;
		} else {
			ASSERTG (!ecmdata->e.alternate_format);
			if (! write_uint64 (fd, ecmdata->stage1_start_prime, &sum)) goto writeerr;
			if (! write_uint32 (fd, ecmdata->stage1_exp_buffer_size, &sum)) goto writeerr;
			if (! write_uint32 (fd, ecmdata->stage1_bitnum, &sum)) goto writeerr;
			if (! write_uint32 (fd, ecmdata->NAF_dictionary_size, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->NAF_dictionary[0].x, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->NAF_dictionary[0].y, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->e.x, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->e.y, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->e.z, &sum)) goto writeerr;
		}
	}

	else if (ecmdata->state == ECM_STATE_MIDSTAGE) {
		int32_t tmp;
		if (! write_giant (fd, ecmdata->Qx_binary, &sum)) goto writeerr;
		tmp = (ecmdata->Qz_binary != NULL);
		if (! write_int32 (fd, tmp, &sum)) goto writeerr;
		if (tmp && ! write_giant (fd, ecmdata->Qz_binary, &sum)) goto writeerr;
		tmp = (ecmdata->gg_binary != NULL);
		if (! write_int32 (fd, tmp, &sum)) goto writeerr;
		if (tmp && ! write_giant (fd, ecmdata->gg_binary, &sum)) goto writeerr;
	}

	// Save everything necessary to restart stage 2 without calling ecm_stage2_impl again
	else if (ecmdata->state == ECM_STATE_STAGE2) {
		if (! write_int32 (fd, ecmdata->stage2_type, &sum)) goto writeerr;
		if (ecmdata->stage2_type != ECM_STAGE2_POLYMULT) {	// Prime pairing stage 2
			uint64_t remaining_pairmap_size;
			if (! write_uint64 (fd, ecmdata->stage2_numvals, &sum)) goto writeerr;
			if (! write_uint32 (fd, ecmdata->totrels, &sum)) goto writeerr;
			if (! write_uint32 (fd, ecmdata->D, &sum)) goto writeerr;
			if (! write_uint32 (fd, ecmdata->E, &sum)) goto writeerr;
			if (! write_int32 (fd, ecmdata->TWO_FFT_STAGE2, &sum)) goto writeerr;
			if (! write_int32 (fd, ecmdata->pool_type, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->first_relocatable, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->last_relocatable, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->B2_start, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->C_done, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->numDsections, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->Dsection, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->max_pairmap_Dsections, &sum)) goto writeerr;
			if (! write_int32 (fd, ecmdata->relp, &sum)) goto writeerr;
			if (! write_array (fd, (char *) ecmdata->relp_sets, 32 * sizeof (int16_t), &sum)) goto writeerr;
			// Output the truncated pairmap
			remaining_pairmap_size = ecmdata->pairmap_size - (ecmdata->pairmap_ptr - ecmdata->pairmap);
			if (! write_uint64 (fd, remaining_pairmap_size, &sum)) goto writeerr;
			if (! write_array (fd, (char *) ecmdata->pairmap_ptr, (size_t) remaining_pairmap_size, &sum)) goto writeerr;
			// Output the gwnums
//GW: safe to free Qx_binary and write_gwnum nQx[0]??
			if (! write_giant (fd, ecmdata->Qx_binary, &sum)) goto writeerr;
			if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->gg, &sum)) goto writeerr;
		} else {						// Polymult stage 2
			// Output how much of stage 2 is complete and the gwnums
			int32_t tmp;
			if (! write_int32 (fd, ecmdata->required_missing, &sum)) goto writeerr;
			if (! write_uint64 (fd, ecmdata->C_done, &sum)) goto writeerr;
//GW: safe to free Qx_binary and write_gwnum nQx[0]??
			if (! write_giant (fd, ecmdata->Qx_binary, &sum)) goto writeerr;
			ASSERTG (ecmdata->gg_binary == NULL || ecmdata->gg == NULL);
			tmp = (ecmdata->gg_binary != NULL || ecmdata->gg != NULL);
			if (! write_int32 (fd, tmp, &sum)) goto writeerr;
			if (ecmdata->gg_binary != NULL && ! write_giant (fd, ecmdata->gg_binary, &sum)) goto writeerr;
			if (ecmdata->gg != NULL && ! write_gwnum (fd, &ecmdata->gwdata, ecmdata->gg, &sum)) goto writeerr;
		}
	}

	else if (ecmdata->state == ECM_STATE_GCD) {
		if (! write_gwnum (fd, &ecmdata->gwdata, ecmdata->gg, &sum)) goto writeerr;
	}


/* Write the checksum, we're done */

	if (! write_checksum (fd, sum)) goto writeerr;

	closeWriteSaveFile (&ecmdata->write_save_file_state, fd);
	return;

/* An error occurred.  Close and delete the current file. */

writeerr:
	deleteWriteSaveFile (&ecmdata->write_save_file_state, fd);
}

int ecm_old_restore (			/* For version 25 save files */
	ecmhandle *ecmdata,
	int	fd,
	uint32_t filesum)
{
	double	sigma;
	uint32_t state, sum = 0;
	uint64_t savefile_B, unused64;

/* Read the file data */

	if (! read_uint32 (fd, &state, &sum)) goto readerr;
	if (! read_uint32 (fd, &ecmdata->curve, &sum)) goto readerr;
	if (! read_double (fd, &sigma, NULL)) goto readerr;
	ecmdata->sigma = (uint64_t) sigma;
	if (! read_uint64 (fd, &savefile_B, &sum)) goto readerr;
	if (! read_uint64 (fd, &ecmdata->stage1_prime, &sum)) goto readerr;
	if (! read_uint64 (fd, &unused64, &sum)) goto readerr;	// C_processed

/* Handle the should-never-happen case where we have a save file with a smaller bound #1 than the bound #1 we are presently working on. */
/* Restart the curve (and curve counts) from scratch. */

	if (savefile_B < ecmdata->B) {
		OutputStr (ecmdata->thread_num, "ECM save file created with smaller B1 value.  Save file cannot be used.\n");
		goto readerr;
	}

// Convert old state values into new state values.  Restart stage 2 for curves that were in the middle of stage 2. */

	if (state == 0) {
		ecmdata->state = ECM_STATE_STAGE1;
		if (! alloc_xz (ecmdata, &ecmdata->xz)) goto readerr;
		if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->xz.x, &sum)) goto readerr;
		if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->xz.z, &sum)) goto readerr;
	}
	if (state == 1) {
		ecmdata->state = ECM_STATE_MIDSTAGE;
		ecmdata->Qx_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
		if (ecmdata->Qx_binary == NULL) goto readerr;
		if (! read_giant (fd, ecmdata->Qx_binary, &sum)) goto readerr;
		ecmdata->gg_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
		if (ecmdata->gg_binary == NULL) goto readerr;
		if (! read_giant (fd, ecmdata->gg_binary, &sum)) goto readerr;
		OutputStr (ecmdata->thread_num, "Old ECM save file was in stage 2.  Restarting stage 2 from scratch.\n");
		free (ecmdata->gg_binary), ecmdata->gg_binary = NULL;
	}

/* Old save files did not store average B2, assume all the curves were run with the current B2 */

	ecmdata->average_B2 = ecmdata->C;

/* Read and compare the checksum */

	if (filesum != sum) goto readerr;
	_close (fd);
	return (TRUE);

/* An error occurred.  Cleanup and return FALSE. */

readerr:
	_close (fd);
	return (FALSE);
}

int ecm_restore (			/* For version 30.4 and later save files */
	ecmhandle *ecmdata)
{
	int	fd;
	struct work_unit *w = ecmdata->w;
	uint32_t version, sum = 0, filesum;
	uint64_t savefile_B;

/* Open the intermediate file */

	fd = _open (ecmdata->read_save_file_state.current_filename, _O_BINARY | _O_RDONLY);
	if (fd < 0) goto err;

/* Read the file header */

	if (! read_magicnum (fd, ECM_MAGICNUM)) goto readerr;
	if (! read_header (fd, &version, w, &filesum)) goto readerr;
	if (version == 0 || version > ECM_VERSION) goto readerr;
	if (version == 1) return (ecm_old_restore (ecmdata, fd, filesum));

/* Read the file data */

	if (! read_uint32 (fd, &ecmdata->curve, &sum)) goto readerr;
	if (! read_uint64 (fd, &ecmdata->average_B2, NULL)) goto readerr;
	if (! read_uint32 (fd, &ecmdata->state, &sum)) goto readerr;
	if (version < 5) {
		double	sigma;
		if (! read_double (fd, &sigma, NULL)) goto readerr;
		ecmdata->sigma = (uint64_t) sigma;
	} else {
		if (! read_uint64 (fd, &ecmdata->sigma, NULL)) goto readerr;
	}
	if (! read_uint64 (fd, &savefile_B, &sum)) goto readerr;
	if (! read_uint64 (fd, &ecmdata->C, &sum)) goto readerr;
	if (version < 6) {
		ecmdata->sigma_type = 1;
		ecmdata->montg_stage1 = TRUE;
	} else {
		if (! read_uint32 (fd, &ecmdata->sigma_type, &sum)) goto readerr;
		uint32_t tmp;
		if (! read_uint32 (fd, &tmp, &sum)) goto readerr;
		ecmdata->montg_stage1 = tmp;
	}
		
/* Handle the case where we have a save file with a smaller bound #1 than the bound #1 we are presently working on. */
/* Restart the curve (and curve counts) from scratch. */

	if (savefile_B < ecmdata->B) {
		OutputStr (ecmdata->thread_num, "ECM save file created with smaller B1 value.  Save file cannot be used.\n");
		goto readerr;
	}

/* Handle the case where we are doing stage 2 only, use the B1 value from the save file */

	if (ecmdata->B == 0) ecmdata->B = savefile_B;

/* Pre-version 30.10 save files cannot continue stage 2 */

	if (version == 2 && ecmdata->state == ECM_STATE_STAGE2) {
		OutputStr (ecmdata->thread_num, "Old ECM save file was in stage 2.  Save file cannot be used.\n");
		goto readerr;
	}

/* Read state dependent data */

	if (ecmdata->state == ECM_STATE_STAGE1) {
		if (ecmdata->montg_stage1) {
			if (! read_uint64 (fd, &ecmdata->stage1_prime, &sum)) goto readerr;
			if (! alloc_xz (ecmdata, &ecmdata->xz)) goto readerr;
			if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->xz.x, &sum)) goto readerr;
			if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->xz.z, &sum)) goto readerr;
		} else {
			if (! read_uint64 (fd, &ecmdata->stage1_start_prime, &sum)) goto readerr;
			if (! read_uint32 (fd, &ecmdata->stage1_exp_buffer_size, &sum)) goto readerr;
			if (! read_uint32 (fd, &ecmdata->stage1_bitnum, &sum)) goto readerr;
			if (! read_uint32 (fd, &ecmdata->NAF_dictionary_size, &sum)) goto readerr;
			if (! ed_alloc (ecmdata, &ecmdata->dict_start)) goto readerr;
			if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->dict_start.x, &sum)) goto readerr;
			if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->dict_start.y, &sum)) goto readerr;
			gwfree (&ecmdata->gwdata, ecmdata->dict_start.z), ecmdata->dict_start.z = NULL;
			if (! ed_alloc (ecmdata, &ecmdata->e)) goto readerr;
			if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->e.x, &sum)) goto readerr;
			if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->e.y, &sum)) goto readerr;
			if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->e.z, &sum)) goto readerr;
		}
	}

	else if (ecmdata->state == ECM_STATE_MIDSTAGE) {
		int32_t	tmp;

		ecmdata->Qx_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
		if (ecmdata->Qx_binary == NULL) goto readerr;
		if (! read_giant (fd, ecmdata->Qx_binary, &sum)) goto readerr;

		if (! read_int32 (fd, &tmp, &sum)) goto readerr;
		if (tmp) {
			ecmdata->Qz_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
			if (ecmdata->Qz_binary == NULL) goto readerr;
			if (! read_giant (fd, ecmdata->Qz_binary, &sum)) goto readerr;
		}

		if (! read_int32 (fd, &tmp, &sum)) goto readerr;
		if (tmp) {
			ecmdata->gg_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
			if (ecmdata->gg_binary == NULL) goto readerr;
			if (! read_giant (fd, ecmdata->gg_binary, &sum)) goto readerr;
		}
	}

	else if (ecmdata->state == ECM_STATE_STAGE2) {
		int32_t	tmp;
		if (! read_int32 (fd, &ecmdata->stage2_type, &sum)) goto readerr;
		if (ecmdata->stage2_type != ECM_STAGE2_POLYMULT) {	// Prime pairing stage 2
			if (! read_uint64 (fd, &ecmdata->stage2_numvals, &sum)) goto readerr;
			if (! read_uint32 (fd, &ecmdata->totrels, &sum)) goto readerr;
			if (! read_uint32 (fd, &ecmdata->D, &sum)) goto readerr;
			ecmdata->numrels = map_D_to_numrels (ecmdata->D);
			if (! read_uint32 (fd, &ecmdata->E, &sum)) goto readerr;
			if (! read_int32 (fd, &ecmdata->TWO_FFT_STAGE2, &sum)) goto readerr;
			if (! read_int32 (fd, &ecmdata->pool_type, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->first_relocatable, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->last_relocatable, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->B2_start, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->C_done, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->numDsections, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->Dsection, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->max_pairmap_Dsections, &sum)) goto readerr;
			if (! read_int32 (fd, &ecmdata->relp, &sum)) goto readerr;
			if (! read_array (fd, (char *) ecmdata->relp_sets, 32 * sizeof (int16_t), &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->pairmap_size, &sum)) goto readerr;
			ecmdata->pairmap = (uint8_t *) malloc ((size_t) ecmdata->pairmap_size);
			if (ecmdata->pairmap == NULL) goto readerr;
			if (! read_array (fd, (char *) ecmdata->pairmap, (size_t) ecmdata->pairmap_size, &sum)) goto readerr;
			ecmdata->pairmap_ptr = ecmdata->pairmap;
			ecmdata->Qx_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
			if (ecmdata->Qx_binary == NULL) goto readerr;
			if (! read_giant (fd, ecmdata->Qx_binary, &sum)) goto readerr;
			ecmdata->gg_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
			if (ecmdata->gg_binary == NULL) goto readerr;
			if (! read_giant (fd, ecmdata->gg_binary, &sum)) goto readerr;
		} else {						// Polynomial stage 2
			if (! read_int32 (fd, &ecmdata->required_missing, &sum)) goto readerr;
			if (! read_uint64 (fd, &ecmdata->C_done, &sum)) goto readerr;
			ecmdata->Qx_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
			if (ecmdata->Qx_binary == NULL) goto readerr;
			if (! read_giant (fd, ecmdata->Qx_binary, &sum)) goto readerr;
			if (! read_int32 (fd, &tmp, &sum)) goto readerr;
			if (tmp) {
				ecmdata->gg_binary = allocgiant (((int) ecmdata->gwdata.bit_length >> 5) + 10);
				if (ecmdata->gg_binary == NULL) goto readerr;
				if (! read_giant (fd, ecmdata->gg_binary, &sum)) goto readerr;
			}
		}
	}

	else if (ecmdata->state == ECM_STATE_GCD) {
		ecmdata->gg = gwalloc (&ecmdata->gwdata);
		if (ecmdata->gg == NULL) goto readerr;
		if (! read_gwnum (fd, &ecmdata->gwdata, ecmdata->gg, &sum)) goto readerr;
	}

/* Read and compare the checksum */

	if (filesum != sum) goto readerr;
	_close (fd);
	return (TRUE);

/* An error occurred.  Cleanup and return FALSE. */

readerr:
	free_xz (ecmdata, &ecmdata->xz);
	free (ecmdata->Qx_binary), ecmdata->Qx_binary = NULL;
	free (ecmdata->Qz_binary), ecmdata->Qz_binary = NULL;
	free (ecmdata->gg_binary), ecmdata->gg_binary = NULL;
	_close (fd);
err:
	return (FALSE);
}


/* Helper routine for multithreading ECM stage 2 */

#define ECM_POLYF_LEVEL_ZERO	1
#define ECM_POLYG_LEVEL_ZERO	2
#define ECM_BUILD_GCDVAL	3

void ecm_helper (
	int	helper_num,	// 0 = main thread, 1+ = helper thread num
	gwhandle *gwdata,	// Single-threaded, thread-safe gwdata (probably cloned) to use
	void	*info)
{
	ecmhandle *ecmdata = (ecmhandle *) info;

/* Combine 2, 3, or 4 length-1 polys in polyF */

	if (ecmdata->helper_work == ECM_POLYF_LEVEL_ZERO) {
		pmhandle pmdata;
		void	*plan2 = NULL;
		void	*plan3 = NULL;
		void	*plan4 = NULL;

		// Initialize a polymult structure that uses the cloned gwdata
		polymult_init (&pmdata, gwdata);

		// Loop working on length 2, 3, or 4 polys
		gwarray source_poly = ecmdata->poly1;
		gwarray dest_poly = ecmdata->polyF;
		int	log2_num_polys = (int) ceil (log2 ((double) ecmdata->poly_size));	// Number of Ftree levels with two or more polys

//GW:  Bring back the saving of length-2 polys.
//GW:  Though this is more cache friendly than separate tree levels
		for ( ; ; ) {
			int	counter, poly_base, next_poly_base, num_polys;

			// Compute location (floor(i*poly_size/num_polys)) and number of polys
			counter = (int) atomic_fetch_incr (ecmdata->polydata.helper_counter);
			poly_base = (int) ((counter * 4 * ecmdata->poly_size) >> log2_num_polys);
			if (poly_base >= ecmdata->poly_size) break;
			next_poly_base = (int) (((counter + 1) * 4 * ecmdata->poly_size) >> log2_num_polys);
			num_polys = next_poly_base - poly_base;

			// Combine two, three, or four length-1 polys
			ASSERTG (num_polys >= 2 && num_polys <= 4);
			if (num_polys == 2) {
				int options = POLYMULT_INVEC1_MONIC_NEGATE | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan2 != NULL) pmdata.plan = plan2, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &source_poly[poly_base], 1, &source_poly[poly_base+1], 1, &dest_poly[poly_base], 2, options);
				plan2 = pmdata.plan;
			}
			else if (num_polys == 3) {
				int options = POLYMULT_INVEC1_MONIC_NEGATE | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan2 != NULL) pmdata.plan = plan2, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &source_poly[poly_base], 1, &source_poly[poly_base+1], 1, &dest_poly[poly_base], 2, options);
				plan2 = pmdata.plan;
				options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan3 != NULL) pmdata.plan = plan3, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &dest_poly[poly_base], 2, &source_poly[poly_base+2], 1, &dest_poly[poly_base], 3, options);
				plan3 = pmdata.plan;
			} else {
				int options = POLYMULT_INVEC1_MONIC_NEGATE | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan2 != NULL) pmdata.plan = plan2, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &source_poly[poly_base], 1, &source_poly[poly_base+1], 1, &dest_poly[poly_base], 2, options);
				options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &source_poly[poly_base+2], 1, &source_poly[poly_base+3], 1, &dest_poly[poly_base+2], 2, options);
				plan2 = pmdata.plan;
				options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan4 != NULL) pmdata.plan = plan4, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &dest_poly[poly_base], 2, &dest_poly[poly_base+2], 2, &dest_poly[poly_base], 4, options);
				plan4 = pmdata.plan;
			}
		}

		// Free saved plans
		free (plan2);
		free (plan3);
		free (plan4);
	}

/* Combine 2, 3, or 4 length-1 polys in polyG */

	if (ecmdata->helper_work == ECM_POLYG_LEVEL_ZERO) {
		pmhandle pmdata;
		void	*plan2 = NULL;
		void	*plan3 = NULL;
		void	*plan4 = NULL;

		// Initialize a polymult structure that uses the cloned gwdata
		polymult_init (&pmdata, gwdata);

		// Loop working on length 2, 3, or 4 polys
		gwarray polyG = ecmdata->polyGH;
		uint64_t num_in_polyG = ecmdata->poly_size - ecmdata->polyGaux_size;		// Number of G(X) values in polyG vs. polyGaux
		int	log2_num_polys = (int) ceil (log2 ((double) ecmdata->poly_size));	// Number of Ftree levels with two or more polys

		for ( ; ; ) {
			int	counter, poly_base, next_poly_base, num_polys;

			// Compute location (floor(i*poly_size/num_polys)) and number of polys
			counter = (int) atomic_fetch_incr (ecmdata->polydata.helper_counter);
			poly_base = (int) ((counter * 4 * ecmdata->poly_size) >> log2_num_polys);
			if (poly_base >= ecmdata->poly_size) break;
			next_poly_base = (int) (((counter + 1) * 4 * ecmdata->poly_size) >> log2_num_polys);
			num_polys = next_poly_base - poly_base;

//GW: For the very first polyG, which is written to polyH. we could preserve the entire polyG array for an even faster MQ_next.  We'd need another and bigger Dover2 value.
//GW:  Also consider using extra, otherwise unusable, gwnums to increase polyGAux size.  This affects normalize pool size too.
//GW: can we figure out how to multi-thread MQ_next?

			// Sometimes preserve the input from polyG in polyGaux (needed by mQ_next_array)
			gwnum	poly1, poly2, poly3, poly4;
			poly1 = polyG[poly_base];
			if (poly_base >= num_in_polyG) gwswap (polyG[poly_base], ecmdata->polyGaux[poly_base - num_in_polyG]);
			poly2 = polyG[poly_base+1];
			if (poly_base+1 >= num_in_polyG) gwswap (polyG[poly_base+1], ecmdata->polyGaux[poly_base+1 - num_in_polyG]);
			if (num_polys > 2) {
				poly3 = polyG[poly_base+2];
				if (poly_base+2 >= num_in_polyG) gwswap (polyG[poly_base+2], ecmdata->polyGaux[poly_base+2 - num_in_polyG]);
			}
			if (num_polys > 3) {
				poly4 = polyG[poly_base+3];
				if (poly_base+3 >= num_in_polyG) gwswap (polyG[poly_base+3], ecmdata->polyGaux[poly_base+3 - num_in_polyG]);
			}

			// Combine the two, three, or four length-1 polys
			ASSERTG (num_polys >= 2 && num_polys <= 4);
			if (num_polys == 2) {
				int options = POLYMULT_INVEC1_MONIC_NEGATE | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan2 != NULL) pmdata.plan = plan2, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &poly1, 1, &poly2, 1, &polyG[poly_base], 2, options);
				plan2 = pmdata.plan;
			}
			else if (num_polys == 3) {
				int options = POLYMULT_INVEC1_MONIC_NEGATE | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan2 != NULL) pmdata.plan = plan2, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &poly1, 1, &poly2, 1, &polyG[poly_base], 2, options);
				plan2 = pmdata.plan;
				options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan3 != NULL) pmdata.plan = plan3, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &polyG[poly_base], 2, &poly3, 1, &polyG[poly_base], 3, options);
				plan3 = pmdata.plan;
			} else {
				int options = POLYMULT_INVEC1_MONIC_NEGATE | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan2 != NULL) pmdata.plan = plan2, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &poly1, 1, &poly2, 1, &polyG[poly_base], 2, options);
				options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &poly3, 1, &poly4, 1, &polyG[poly_base+2], 2, options);
				plan2 = pmdata.plan;
				options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC | POLYMULT_NEXTFFT | POLYMULT_SAVE_PLAN;
				if (plan4 != NULL) pmdata.plan = plan4, options |= POLYMULT_USE_PLAN;
				polymult (&pmdata, &polyG[poly_base], 2, &polyG[poly_base+2], 2, &polyG[poly_base], 4, options);
				plan4 = pmdata.plan;
			}
		}

		// Free saved plans
		free (plan2);
		free (plan3);
		free (plan4);
	}

/* Use multithreading to build gg (the final value to be GCDed).  This is important in the cases where gwnum is using a single-pass FFT and no multi-threading. */

	if (ecmdata->helper_work == ECM_BUILD_GCDVAL) {
		gwnum	accumulator = NULL;
		gwnum	*polyH = &ecmdata->polyGH[ecmdata->poly_size];

		// Loop multiplying points from the output poly
		for ( ; ; ) {
			// Get one of the output polyH gwnums.  We accumulate here and later multiply with gg.
			int j = (int) atomic_fetch_incr (ecmdata->polydata.helper_counter);

			// If the other helper threads have completed processing of the output poly, then we're done
			if (j >= ecmdata->poly_size) break;

			// Unfft the gwnum
			if (polymult_must_unfft (gwdata, polyH[j])) gwunfft2 (gwdata, polyH[j], polyH[j], GWMUL_STARTNEXTFFT);

			// Either set the accumulator or multiply with the accumulator
			if (accumulator == NULL) accumulator = polyH[j];
			else gwmul3 (gwdata, polyH[j], accumulator, accumulator, GWMUL_STARTNEXTFFT);
		}

		// Return the FFTed accumulator in polyG
		if (accumulator != NULL) gwfft (gwdata, accumulator, accumulator);
		ecmdata->polyGH[helper_num] = accumulator;
	}
}


/**************************************************************
 *
 *	Main ECM Function
 *
 **************************************************************/

int ecm (
	int	thread_num,
	struct PriorityInfo *sp_info,	/* SetPriority information */
	struct work_unit *w)
{
	ecmhandle ecmdata;
	uint64_t next_prime, last_output, last_output_t, dictionary_memory;
	double	one_over_B, output_frequency, output_title_frequency;
	double	base_pct_complete, one_relp_pct;
	int	i;
	unsigned int memused;
	char	filename[32], ftree_filename[40], buf[255], JSONbuf[4000], fft_desc[200];
	int	res, stop_reason, delayed_stop_reason, stage, first_iter_msg;
	int	msglen, continueECM, prpAfterEcmFactor;
	char	*str, *msg;
	double	stage1_timer, stage2_timer, timers[10];
	bool	near_fft_limit, saving, default_sigma_type;
	double	allowable_maxerr;
	int	maxerr_restart_count = 0;
	unsigned long maxerr_fftlen = 0;
	relp_set_data_map relp_set_map;
	Dmultiple_data_map Dmultiple_map;

/* Clear pointers to allocated memory */

	str = NULL;
	msg = NULL;

/* Begin initializing ECM data structure */
/* MQ_init used to require that B is at least 120 (4 times the minimum D) */
/* Choose a default value for the second bound if none was specified */

	memset (&ecmdata, 0, sizeof (ecmhandle));
	ecmdata.thread_num = thread_num;
	ecmdata.stage1_threads = get_worker_num_threads (thread_num, HYPERTHREAD_LL);
	ecmdata.stage2_threads = ecmdata.stage1_threads + IniGetInt (INI_FILE, "Stage2ExtraThreads", 0);
	ecmdata.w = w;
	ecmdata.B = (uint64_t) w->B1;
	ecmdata.C = (uint64_t) w->B2;
	if (ecmdata.B < 120 && w->gmp_ecm_file == NULL) {
		OutputStr (thread_num, "Using minimum bound #1 of 120\n");
		ecmdata.B = 120;
	}
	if (ecmdata.C == 0) ecmdata.C = ecmdata.B * 100;
	if (ecmdata.C <= ecmdata.B) ecmdata.C = ecmdata.B;
	if (IniGetInt (INI_FILE, "GmpEcmHook", 0)) ecmdata.C = ecmdata.B;
	ecmdata.pct_mem_to_use = 1.0;				// Use as much memory as we can unless we get allocation errors

/* Decide if we will calculate an optimal B2 when stage 2 begins */

	ecmdata.optimal_B2 = (!QA_IN_PROGRESS && ecmdata.C == 100 * ecmdata.B && IniGetInt (INI_FILE, "ECMBestB2", 1));

/* Little known option to use higher bounds than assigned by PrimeNet */

	if (!QA_IN_PROGRESS && !IniGetInt (INI_FILE, "GmpEcmHook", 0)) {
		int	mult = IniGetInt (INI_FILE, "ECMBoundsMultiplier", 1);
		if (mult < 1) mult = 1;
		if (mult > 20) mult = 20;
		ecmdata.B *= mult;
		ecmdata.C *= mult;
	}

/* Open the GMP-ECM stage 1 resume file.  Position to the proper curve. */

restart:
	if (w->gmp_ecm_file != NULL) {
		ecmdata.gmp_ecm_file = fopen (w->gmp_ecm_file, "r");
		if (ecmdata.gmp_ecm_file == NULL) {
			sprintf (buf, "File named '%s' could not be opened.\n", w->gmp_ecm_file);
			OutputStr (thread_num, buf);
			stop_reason = STOP_FILE_IO_ERROR;
			goto exit;
		}
		// If requested, skip N lines from the GMP-ECM resume file
		for (i = 0; i < (int) w->skip_curves; i++) skip_resumefile_line (ecmdata.gmp_ecm_file);
	}

/* Compute the number we are factoring (ECMSTAGE2N= gets N from the GMP-ECM file) */

	if (w->n) {
		stop_reason = setN (thread_num, w, &ecmdata.N);
		if (stop_reason) goto exit;
	} else {
		mpz_t n;
		mpz_init (n);
		if (!peek_resumefile_line (ecmdata.gmp_ecm_file, n)) {
			sprintf (buf, "Could not find line containing N= in file '%s'.\n", w->gmp_ecm_file);
			OutputStr (thread_num, buf);
			stop_reason = STOP_FILE_IO_ERROR;
			goto exit;
		}
		ecmdata.N = allocgiant ((int) divide_rounding_up (mpz_sizeinbase (n, 2), 32));
		if (ecmdata.N == NULL) goto oom;
		mpztog (n, ecmdata.N);
		mpz_clear (n);
	}

/* Other initialization */

	PRAC_SEARCH = IniGetInt (INI_FILE, "PracSearch", 7);
	if (PRAC_SEARCH < 1) PRAC_SEARCH = 1;
	if (PRAC_SEARCH > 50) PRAC_SEARCH = 50;

/* Time the giants squaring and multiply code in order to select the */
/* best crossover points.  This should only be done in the release code */
/* (optimized giants library). */

/*#define TIMING1*/
#ifdef TIMING1
clear_timers (timers, sizeof (timers) / sizeof (timers[0]));
if (w->n == 598) {
int i, j;
giant	x, y, z, a, m;
#define TESTSIZE	200
RDTSC_TIMING = 12;
x = allocgiant(TESTSIZE); y = allocgiant (2*TESTSIZE);
z = allocgiant (2*TESTSIZE), a = allocgiant (2*TESTSIZE);
m = allocgiant (TESTSIZE);
srand ((unsigned) time (NULL));
for (i = 0; i < TESTSIZE; i++) {
	x->n[i] = (rand () << 17) + rand ();
	m->n[i] = (rand () << 17) + rand ();
}
x->n[TESTSIZE-1] &= 0x00FFFFFF;
m->n[TESTSIZE-1] &= 0x00FFFFFF;
for (i = TESTSIZE; i >= 10; i--) {
	x->sign = i;
	m->sign = i;
	setmulmode (GRAMMAR_MUL);
	for (j = 0; j < 10; j++) {
		gtog (x, y);
		start_timer (timers, 0);
		if (B&1) mulg (m, y);
		else squareg (y);
		end_timer (timers, 0);
		if (timers[1] == 0 || timers[1] > timers[0]) timers[1] = timers[0];
		timers[0] = 0;
	}
	setmulmode (KARAT_MUL);
	for (j = 0; j < 10; j++) {
		gtog (x, z);
		start_timer (timers, 0);
		if (B&1) mulg (m, z);
		else squareg (z);
		end_timer (timers, 0);
		if (timers[2] == 0 || timers[2] > timers[0]) timers[2] = timers[0];
		timers[0] = 0;
	}
	setmulmode (FFT_MUL);
	for (j = 0; j < 10; j++) {
		gtog (x, a);
		start_timer (timers, 0);
		if (B&1) mulg (m, a);
		else squareg (a);
		end_timer (timers, 0);
		if (timers[3] == 0 || timers[3] > timers[0]) timers[3] = timers[0];
		timers[0] = 0;
	}
	sprintf (buf, "Size: %ld  G: ", i);
	print_timer (timers, 1, buf, TIMER_MS | TIMER_CLR);
	strcat (buf, ", K: ");
	print_timer (timers, 2, buf, TIMER_MS | TIMER_CLR);
	strcat (buf, ", F: ");
	print_timer (timers, 3, buf, TIMER_MS | TIMER_NL | TIMER_CLR);
	OutputBoth (thread_num, buf);
	if (gcompg (y, z) != 0)
		i--;
	if (gcompg (y, a) != 0)
		i--;
	Sleep (100);
}
return 0;
}

/* This code lets us time various giants FFT squarings and multiplies */

if (w->n == 601) {
clear_timers (timers, sizeof (timers) / sizeof (timers[0]));
int i, j;
giant	x, a, m;
#define TESTSIZE2	260000
RDTSC_TIMING = 12;
x = allocgiant(TESTSIZE2);
a = allocgiant (2*TESTSIZE2);
m = allocgiant (TESTSIZE2);
srand ((unsigned) time (NULL));
for (i = 0; i < TESTSIZE2; i++) {
	x->n[i] = (rand () << 17) + rand ();
	m->n[i] = (rand () << 17) + rand ();
}
x->n[TESTSIZE2-1] &= 0x00FFFFFF;
m->n[TESTSIZE2-1] &= 0x00FFFFFF;
for (i = 30; i < TESTSIZE2/2; i<<=1) {
	x->sign = i;
	m->sign = i;
	setmulmode (FFT_MUL);
	for (j = 0; j < 10; j++) {
		gtog (x, a);
		start_timer (timers, 0);
		if (B&1) mulg (m, a);
		else squareg (a);
		end_timer (timers, 0);
		if (timers[3] == 0 || timers[3] > timers[0]) timers[3] = timers[0];
		timers[0] = 0;
	}
	sprintf (buf, "Size: %ld  , F: ", i);
	print_timer (timers, 3, buf, TIMER_NL | TIMER_CLR | TIMER_MS);
	OutputStr (thread_num, buf);
	Sleep (100);
}
return 0;
}
#endif

/* Include timing code when building the debug version of prime95 */

#ifdef GDEBUG
if (w->n == 600) {
gwhandle gwdata;
void *workbuf;
int j, min_test, max_test, test, cnt, NUM_X87_TESTS, NUM_SSE2_TESTS, NUM_AVX_TESTS, NUM_AVX512_TESTS;
#define timeit(a,n,w) (((void**)a)[0]=w,((uint32_t*)a)[2]=n,gwtimeit(a))

clear_timers (timers, sizeof (timers) / sizeof (timers[0]));
gwinit (&gwdata); gwdata.cpu_flags &= ~CPU_AVX512F;
gwsetup (&gwdata, 1.0, 2, 10000000, -1);
workbuf = (void *) aligned_malloc (400000000, 4096);
memset (workbuf, 0, 400000000);
RDTSC_TIMING = 2;
min_test = IniGetInt (INI_FILE, "MinTest", 0);
max_test = IniGetInt (INI_FILE, "MaxTest", min_test);
NUM_X87_TESTS = timeit (gwdata.asm_data, -1, NULL);
NUM_SSE2_TESTS = timeit (gwdata.asm_data, -2, NULL);
NUM_AVX_TESTS = timeit (gwdata.asm_data, -3, NULL);
NUM_AVX512_TESTS = timeit (gwdata.asm_data, -4, NULL);
//SetThreadPriority (CURRENT_THREAD, THREAD_PRIORITY_TIME_CRITICAL);
for (j = 0; j < NUM_X87_TESTS + NUM_SSE2_TESTS + NUM_AVX_TESTS + NUM_AVX512_TESTS; j++) {
	cnt = 0;
	test = (j < NUM_X87_TESTS ? j :
		j < NUM_X87_TESTS + NUM_SSE2_TESTS ? 1000 + j - NUM_X87_TESTS :
		j < NUM_X87_TESTS + NUM_SSE2_TESTS + NUM_AVX_TESTS ? 2000 + j - NUM_X87_TESTS - NUM_SSE2_TESTS :
			3000 + j - NUM_X87_TESTS - NUM_SSE2_TESTS - NUM_AVX_TESTS);
	if (test == 3000 && CPU_FLAGS & CPU_AVX512F) {
		gwdone (&gwdata);
		gwinit (&gwdata);
		gwsetup (&gwdata, 1.0, 2, 10000000, -1);
	}
	if (min_test && (test < min_test || test > max_test)) continue;
	if (! (CPU_FLAGS & CPU_SSE2) && test >= 1000) break;
	if (! (CPU_FLAGS & CPU_AVX) && test >= 2000) break;
	if (! (CPU_FLAGS & CPU_AVX512F) && test >= 3000) break;
for (i = 1; i <= 50; i++) {
	start_timer (timers, 0);
	timeit (gwdata.asm_data, test, workbuf);
	end_timer (timers, 0);
	if (timers[1] == 0 || timers[1] > timers[0]) timers[1] = timers[0];
	if (i > 1 && timers[0] < 3.0 * timers[1]) {
		if (timers[0] > 1.5 * timers[1])
			i++;
		timers[2] += timers[0];
		cnt++;
	}
	timers[0] = 0;
}
sprintf (buf, "Test %d: ", test);
print_timer (timers, 1, buf, TIMER_CLR);
timers[2] /= cnt;
strcat (buf, ", avg: ");
print_timer (timers, 2, buf, TIMER_NL | TIMER_CLR);
OutputBoth (thread_num, buf);
}
aligned_free (workbuf);
gwdone (&gwdata);
if (min_test) exit (0);
return 0;
}
#endif

#ifdef TIMING604
if (w->n == 604) {
	gmp_randstate_t rstate;
	int	exp;

	clear_timers (timers, sizeof (timers) / sizeof (timers[0]));
	gmp_randinit_default (rstate);
	for (exp = 100000; exp < 300000000; exp *= 2) {
		gwhandle gwdata;
		giant	N;
		gwnum	a, b, c;
		int	j;
		mpz_t	r;

		gwinit (&gwdata);
		gwsetup (&gwdata, 1.0, 2, exp, -1);

		a = gwalloc (&gwdata);
		b = gwalloc (&gwdata);
		c = gwalloc (&gwdata);
		N = popg (&gwdata.gdata, ((int) gwdata.bit_length >> 5) + 10);
		mpz_init (r);
		mpz_urandomb (r, rstate, exp);
		mpztog (r, N);
		gianttogw (&gwdata, N, a);
		mpz_urandomb (r, rstate, exp);
		mpztog (r, N);
		gianttogw (&gwdata, N, b);
		mpz_urandomb (r, rstate, exp);
		mpztog (r, N);
		gianttogw (&gwdata, N, c);
		mpz_clear (r);

		start_timer (timers, 0);
		for (j = 0; j < 100; j++) {
			gwsquare2 (&gwdata, c, c, GWMUL_STARTNEXTFFT);
		}
		end_timer (timers, 0);
		sprintf (buf, "Exp: %d, 100 squares: ", exp);
		print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
		OutputBoth (thread_num, buf);

		start_timer (timers, 0);
		for (j = 0; j < 1; j++) {
			mpz_t	__v, __N, __gcd;
			giant	v;
			v = popg (&gwdata.gdata, ((int) gwdata.bit_length >> 5) + 10);
			gwtogiant (&gwdata, b, v);
			/* Do the GCD */
			mpz_init (__v);
			mpz_init (__N);
			mpz_init (__gcd);
			gtompz (v, __v);
			gtompz (N, __N);
			mpz_gcd (__gcd, __v, __N);
			mpz_clear (__gcd);
			mpz_clear (__v);
			mpz_clear (__N);
			pushg (&gwdata.gdata, 1);
		}
		end_timer (timers, 0);
		sprintf (buf, "Exp: %d, GCD: ", exp);
		print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
		OutputBoth (thread_num, buf);

		start_timer (timers, 0);
		for (j = 0; j < 1; j++) {
			giant	v;
			mpz_t	__v, __N, __gcd, __inv;
			v = popg (&gwdata.gdata, ((int) gwdata.bit_length >> 5) + 10);
			gwtogiant (&gwdata, b, v);
			/* Do the extended GCD */
			mpz_init (__v);
			mpz_init (__N);
			mpz_init (__gcd);
			mpz_init (__inv);
			gtompz (v, __v);
			gtompz (N, __N);
			mpz_gcdext (__gcd, __inv, NULL, __v, __N);
			mpz_clear (__v);
			if (mpz_sgn (__inv) < 0) mpz_add (__inv, __inv, __N);
			mpztog (__inv, v);
			gianttogw (&gwdata, v, b);
			mpz_clear (__gcd);
			mpz_clear (__inv);
			mpz_clear (__N);
			pushg (&gwdata.gdata, 1);
		}
		end_timer (timers, 0);
		sprintf (buf, "Exp: %d, modinv: ", exp);
		print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
		OutputBoth (thread_num, buf);
		gwdone (&gwdata);
	}
	return (0);
}
#endif

/* Init filenames */

	tempFileName (w, filename);
	uniquifySaveFile (thread_num, filename);
	sprintf (ftree_filename, "%s.ftree", filename);

/* Init the random number generator */

	srand ((unsigned) time (NULL));

/* Perform setup functions.  This includes deciding how big an FFT to use, allocating memory, calling the FFT setup code, etc. */

/* Setup the gwnum assembly code */

	gwinit (&ecmdata.gwdata);
	if (IniGetInt (INI_FILE, "UseLargePages", 0)) gwset_use_large_pages (&ecmdata.gwdata);
	if (HYPERTHREAD_LL) sp_info->normal_work_hyperthreading = TRUE, gwset_will_hyperthread (&ecmdata.gwdata, 2);
	gwset_bench_cores (&ecmdata.gwdata, HW_NUM_CORES);
	gwset_bench_workers (&ecmdata.gwdata, NUM_WORKERS);
	if (ERRCHK) gwset_will_error_check (&ecmdata.gwdata);
	gwset_num_threads (&ecmdata.gwdata, ecmdata.stage1_threads);
	gwset_thread_callback (&ecmdata.gwdata, SetAuxThreadPriority);
	gwset_thread_callback_data (&ecmdata.gwdata, sp_info);
	gwset_safety_margin (&ecmdata.gwdata, IniGetFloat (INI_FILE, "ExtraSafetyMargin", 0.0));
	gwset_larger_fftlen_count (&ecmdata.gwdata, maxerr_restart_count < 3 ? maxerr_restart_count : 3);
	gwset_minimum_fftlen (&ecmdata.gwdata, w->minimum_fftlen);
	gwset_use_spin_wait (&ecmdata.gwdata, IniGetInt (INI_FILE, "SpinWait", 0));
	if (w->n) res = gwsetup (&ecmdata.gwdata, w->k, w->b, w->n, w->c);
	else res = gwsetup_general_mod_giant (&ecmdata.gwdata, ecmdata.N);
	if (res) {
		sprintf (buf, "Cannot initialize FFT code, errcode=%d\n", res);
		OutputBoth (thread_num, buf);
		return (STOP_FATAL_ERROR);
	}
	gwerror_checking (&ecmdata.gwdata, ERRCHK);
	ecmdata.stage1_fftlen = gwfftlen (&ecmdata.gwdata);

/* More random initializations */

	stage1_timer = stage2_timer = 0.0;
	last_output = last_output_t = ecmdata.modinv_count = 0;
	gw_clear_fft_count (&ecmdata.gwdata);
	first_iter_msg = TRUE;
	calc_output_frequencies (&ecmdata.gwdata, &output_frequency, &output_title_frequency);
	dictionary_memory = (uint64_t) IniGetInt (INI_FILE, "DictionaryMemory", 256) << 20;	// Default to 256MiB dictionary memory

/* Optionally do a probable prime test */

	if (IniGetInt (INI_FILE, "ProbablePrimeTest", 0) && isProbablePrime (&ecmdata.gwdata, ecmdata.N)) {
		sprintf (buf, "%s is a probable prime\n", ecmdata.N_short_string_rep);
		OutputStr (thread_num, buf);
	}

/* Output a startup message */

	gwfft_description (&ecmdata.gwdata, fft_desc);
	sprintf (buf, "\nUsing %s\n", fft_desc);
	OutputStr (thread_num, buf);
	sprintf (buf, "%5.3f bits-per-word below FFT limit\n", (ecmdata.gwdata.EXTRA_BITS - EB_GWMUL_SAVINGS) / 2.0);
	OutputStr (thread_num, buf);

/* If we are near the maximum exponent this fft length can test, then we will roundoff check all multiplies */

	near_fft_limit = exponent_near_fft_limit (&ecmdata.gwdata);
	gwerror_checking (&ecmdata.gwdata, ERRCHK || near_fft_limit);

/* Figure out the maximum round-off error we will allow.  By default this is 28/64 when near the FFT limit and 26/64 otherwise. */
/* We've found that this default catches errors without raising too many spurious error messages.  The user can override this default. */
/* Since ell_dbl and ell_add may aggressively push EXTRA_BITS, assume we can always get large-ish round off errors */

	allowable_maxerr = IniGetFloat (INI_FILE, "MaxRoundoffError", (float) 0.4375);

/* Check for a continuation file.  Limit number of backup files we try to read in case there is an error deleting bad save files. */

	readSaveFileStateInit (&ecmdata.read_save_file_state, thread_num, filename);
	writeSaveFileStateInit (&ecmdata.write_save_file_state, filename, 0);
	for ( ; ; ) {
		if (! saveFileExists (&ecmdata.read_save_file_state)) {
			/* If there were save files, they are all bad.  Report a message and temporarily abandon the work unit.  We do this in hopes that we */
			/* can successfully read one of the bad save files at a later time.  This sounds crazy, but has happened when OSes get in a funky state. */
			if (ecmdata.read_save_file_state.a_non_bad_save_file_existed) {
				OutputBoth (thread_num, ALLSAVEBAD_MSG);
				return (0);
			}
			/* No save files existed, start from scratch. */
			break;
		}

/* Read in the save file.  If the save file is no good ecm_restore will have deleted it.  Loop trying to read a backup save file. */

		if (! ecm_restore (&ecmdata)) {
			/* Close and rename the bad save file */
			saveFileBad (&ecmdata.read_save_file_state);
			continue;
		}

/* If resuming from a GMP-ECM stage 2 run, make sure we are on the right line in the GMP-ECM file */

		if (w->gmp_ecm_file != NULL) {
			fclose (ecmdata.gmp_ecm_file);
			ecmdata.gmp_ecm_file = fopen (w->gmp_ecm_file, "r");
			if (ecmdata.gmp_ecm_file == NULL) {
				sprintf (buf, "File named '%s' could not be opened.\n", w->gmp_ecm_file);
				OutputStr (thread_num, buf);
				stop_reason = STOP_FILE_IO_ERROR;
				goto exit;
			}
			/* Skip past the curve we are processing, the save file has all the information we need */
			for (i = 0; i < (int) (w->skip_curves + ecmdata.curve); i++) skip_resumefile_line (ecmdata.gmp_ecm_file);
		}

/* Output a startup message */

		curve_start_msg (&ecmdata);

/* Continue in the middle of stage 1 */

		if (ecmdata.state == ECM_STATE_STAGE1) {
			stop_reason = init_curve (&ecmdata);
			if (stop_reason) goto exit;
			if (ecmdata.factor != NULL) goto bingo;				// Can't happen
			if (!ecmdata.montg_stage1 && !ed_check (&ecmdata, &ecmdata.e)) {
				saveFileBad (&ecmdata.read_save_file_state);		/* Close and rename the bad save file */
				goto ed_err;
			}
			goto restart1;
		}

/* Continue if between stage 1 and stage 2 */

		if (ecmdata.state == ECM_STATE_MIDSTAGE) {
			goto restart3;
		}

/* We've finished stage 1, resume stage 2.  The save file contained normalized Q and possibly the GCD accumulator. */

		if (ecmdata.state == ECM_STATE_STAGE2) {
			goto restart3;
		}

/* We've finished stage 2, but haven't done the GCD yet.  Place accumulator in gg, leave normalized Q in xz.x. */

		ASSERTG (ecmdata.state == ECM_STATE_GCD);
		goto restart4;
	}

/* Unless a save file indicates otherwise, we are testing our first curve */

	ecmdata.curve = 1;
	ecmdata.average_B2 = 0;

/* Loop processing the requested number of ECM curves */

restart0:

/* If running stage 2 using data from a GMP-ECM stage 1 resume file, read in the stage 1 results, go to stage 2. */

	if (w->gmp_ecm_file != NULL) {
		mpz_t	x, n, sigma;
		int	param;
		double	b1;
		mpz_inits (x, n, sigma, 0);
		if (!read_resumefile_line (ecmdata.thread_num, ecmdata.gmp_ecm_file, x, n, sigma, &param, &b1)) {
			w->curves_to_do = ecmdata.curve - 1;
			goto no_more_curves;
		}
		if (param == 0) ecmdata.sigma_type = 1;
		else if (param == 3) ecmdata.sigma_type = 3;
		else {
			sprintf (buf, "Unsupported GMP-ECM param=%d\n", param);
			OutputStr (ecmdata.thread_num, buf);
			stop_reason = STOP_FILE_IO_ERROR;
			goto exit;
		}
		mpz_get_str (buf, 10, sigma);
		ecmdata.sigma = atoll (buf);
		ecmdata.B = (uint64_t) b1;
		ecmdata.state = ECM_STATE_MIDSTAGE;
		ecmdata.Qx_binary = allocgiant (((int) ecmdata.gwdata.bit_length >> 5) + 10);
		if (ecmdata.Qx_binary == NULL) goto oom;
		mpztog (x, ecmdata.Qx_binary);
		mpz_clears (x, n, sigma, 0);
		curve_start_msg (&ecmdata);
		stop_reason = init_curve (&ecmdata);
		if (stop_reason) goto exit;
		goto restart3;
	}

/* Init for stage 1 */

	ecmdata.state = ECM_STATE_STAGE1_INIT;
	ecmdata.C_done = 0;
	ecmdata.pct_mem_to_use = 1.0;				// Use as much memory as we can unless we get allocation errors
	ecm_stage1_memory_usage (thread_num, &ecmdata);
	last_output = last_output_t = ecmdata.modinv_count = 0;
	gw_clear_fft_count (&ecmdata.gwdata);
	ASSERTG (ecmdata.xz.x == NULL && ecmdata.xz.z == NULL);
	ASSERTG (ecmdata.e.x == NULL && ecmdata.e.y == NULL && ecmdata.e.z == NULL);
	ASSERTG (ecmdata.gg == NULL);

/* Choose curve with order divisible by 16 and choose a point (x/z) on said curve. */

	do {
		uint32_t hi, lo;
		ecmdata.sigma = ((uint64_t) (rand () & 0x1F)) << 48;
		ecmdata.sigma += ((uint64_t) (rand () & 0xFFFF)) << 32;
		if (CPU_FLAGS & CPU_RDTSC) rdtsc (&hi, &lo);
		ecmdata.sigma += lo ^ hi ^ ((uint32_t) rand () << 16);
	} while (ecmdata.sigma <= 5);
	if (w->curve > 5) {
		ecmdata.sigma = w->curve;
		w->curves_to_do = 1;
	}
	default_sigma_type = (IniGetInt (INI_FILE, "DictionarySize", (int) (dictionary_memory / (3 * gwnum_size (&ecmdata.gwdata)))) < 4) ? 1 : 0;
	ecmdata.sigma_type = IniGetInt (INI_FILE, "MontgSigma", default_sigma_type);
	ecmdata.montg_stage1 = IniGetInt(INI_FILE, "MontgStage1", ecmdata.sigma_type == 1);
	curve_start_msg (&ecmdata);
	stop_reason = init_curve (&ecmdata);
	if (stop_reason) goto exit;
	if (ecmdata.factor != NULL) goto bingo;
	ecmdata.stage1_prime = 0;				// Last prime processed = none
	ecmdata.stage1_bitnum = 0;				// Last Edwards bit processed = none
	ecmdata.state = ECM_STATE_STAGE1;
	sprintf (w->stage, "C%" PRIu32 "S1", ecmdata.curve);
	w->pct_complete = 0.0;
	start_timer_from_zero (&stage1_timer, 0);

/* The stage 1 restart point */

restart1:
	one_over_B = 1.0 / (double) ecmdata.B;
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);

/* The old-style Montgomery stage 1 */

	if (ecmdata.montg_stage1) {
		ecm_stage1_memory_usage (thread_num, &ecmdata);
		unsigned long SQRT_B = (unsigned long) sqrt ((double) ecmdata.B);
		// We guess the max sieve prime for stage 2.  If optimal B2 is less 256 * B, then max sieve prime will be less than 16 * sqrt(B).
		// If our guess is wrong, that's no big deal -- sieve code is smart enough to handle it.
		stop_reason = start_sieve_with_limit (thread_num, ecmdata.stage1_prime + 1, 16 * SQRT_B, &ecmdata.sieve_info);
		if (stop_reason) goto exit;
		for (ecmdata.stage1_prime = sieve (ecmdata.sieve_info); ecmdata.stage1_prime <= ecmdata.B; ecmdata.stage1_prime = next_prime) {
			int	count;

			next_prime = sieve (ecmdata.sieve_info);

/* Test for user interrupt, save files, and error checking */

			stop_reason = stopCheck (thread_num);
			saving = testSaveFilesFlag (thread_num);

/* Count the number of prime powers where prime^n <= B */

			if (ecmdata.stage1_prime <= SQRT_B) {
				uint64_t mult, max;
				count = 0;
				max = ecmdata.B / ecmdata.stage1_prime;
				for (mult = ecmdata.stage1_prime; ; mult *= ecmdata.stage1_prime) {
					count++;
					if (mult > max) break;
				}
			} else
				count = 1;

/* Adjust the count of prime powers.  I'm not sure, but I think choose12 means we should include 2 extra twos and 1 extra 3. */

			if (ecmdata.stage1_prime <= 3) count += (ecmdata.stage1_prime == 2) ? 2 : 1;

/* Apply the proper number of primes */

			for ( ; count; count--) {
				int last_mul = (count == 1 && (saving || stop_reason || next_prime > ecmdata.B));
				int oom_stop_reason = ell_mul (&ecmdata, &ecmdata.xz, ecmdata.stage1_prime, last_mul);
				if (oom_stop_reason) {	// Out of memory should be the only reason ell_mul fails.  Won't happen, but handle it gracefully anyway
					stop_reason = oom_stop_reason;
					goto exit;
				}
			}

/* Calculate stage 1 percent complete */

			w->pct_complete = ecmdata.stage1_prime * one_over_B;

/* Output the title every so often */

			if (first_iter_msg ||
			    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
				sprintf (buf, "%.*f%% of %s ECM curve %" PRIu32 " stage 1",
					 (int) PRECISION, trunc_percent (w->pct_complete), ecmdata.N_short_string_rep, ecmdata.curve);
				title (thread_num, buf);
				last_output_t = gw_get_fft_count (&ecmdata.gwdata);
			}

/* Print a message every so often */

			if (first_iter_msg ||
			    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
				sprintf (buf, "%s curve %" PRIu32 " stage 1 at prime %" PRIu64 " [%.*f%%].",
					 ecmdata.N_short_string_rep, ecmdata.curve, ecmdata.stage1_prime, (int) PRECISION, trunc_percent (w->pct_complete));
				end_timer (timers, 0);
				if (first_iter_msg) {
					strcat (buf, "\n");
				} else {
					strcat (buf, " Time: ");
					print_timer (timers, 0, buf, TIMER_NL);
				}
				if (ecmdata.stage1_prime != 2)
					OutputStr (thread_num, buf);
				start_timer_from_zero (timers, 0);
				last_output = gw_get_fft_count (&ecmdata.gwdata);
				first_iter_msg = FALSE;
			}

/* Check for errors */

			if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;

/* Write a save file when the user interrupts the calculation and every DISK_WRITE_TIME minutes. */

			if (stop_reason || saving) {
				ecm_save (&ecmdata);
				if (stop_reason) goto exit;
			}
		}
	}

/* Edwards stage 1 */

	else {

/* Stage 1 pre-calculates an exponent that is the product of small primes.  The default maximum exponent size is 40MB (B1 of roughly 222 million). */
/* Reports of GMP having difficulty with products larger than 2^32 bits leads us to cap maximum exponent size at 128MB. */

		// Perform required initialization if not resuming from a save file
		if (ecmdata.stage1_bitnum == 0) {
			ecmdata.stage1_start_prime = 2;
			ecmdata.stage1_exp_buffer_size = IniGetInt (INI_FILE, "ECMStage1ExpBufferSize", 40);
			if (ecmdata.stage1_exp_buffer_size < 1) ecmdata.stage1_exp_buffer_size = 1;
			if (ecmdata.stage1_exp_buffer_size > 128) ecmdata.stage1_exp_buffer_size = 128;
			ecmdata.stage1_exp_buffer_size <<= 20;
		}

		stop_reason = start_sieve_with_limit (thread_num, ecmdata.stage1_start_prime, (uint32_t) sqrt ((double) ecmdata.B), &ecmdata.sieve_info);
		if (stop_reason) goto exit;
		ecmdata.stage1_prime = sieve (ecmdata.sieve_info);	// Get first prime from sieve

		mpz_init (ecmdata.stage1_exp), ecmdata.stage1_exp_initialized = TRUE;

/* Process primes into one big exponent.  We may not be able to process all primes in one blast, so loop in case multiple exponent chunks are needed. */

		for ( ; ; ) {
			int	NAF_index;
			uint32_t best_size;
			uint64_t num_doublings;
			double	chunk_size, chunk_size_over_num_doublings;

/* Create the big exponent and init the dictionary */

			ecmdata.stage1_start_prime = ecmdata.stage1_prime;	// Remember stage1_exp's starting prime for saving to a file
			ecm_calc_exp (ecmdata.sieve_info, ecmdata.stage1_exp, ecmdata.B, &ecmdata.stage1_prime, ecmdata.stage1_exp_buffer_size);
			if (mpz_eq_ui (ecmdata.stage1_exp, 1)) break;
			best_size = NAF_best_size (&ecmdata);
			if (ecmdata.stage1_bitnum == 0) {			// Dictionary size cannot be changed when resuming from a save file
				ecmdata.NAF_dictionary_size = (uint32_t) (dictionary_memory / (3 * gwnum_size (&ecmdata.gwdata)));
				if (ecmdata.NAF_dictionary_size > best_size) ecmdata.NAF_dictionary_size = best_size;
				ecmdata.NAF_dictionary_size = IniGetInt (INI_FILE, "DictionarySize", ecmdata.NAF_dictionary_size);  // User override
				if (ecmdata.NAF_dictionary_size < 4) ecmdata.NAF_dictionary_size = 4;		// Not sure what the minimum is, 2 crashes
			}
			sprintf (buf, "%sxponent length is %" PRIu64 ", best dictionary size is %" PRIu32 ", actual dictionary size is %" PRIu32 "\n",
				 ecmdata.stage1_prime <= ecmdata.B ? "Partial e" : "E",
				 (uint64_t) mpz_sizeinbase (ecmdata.stage1_exp, 2), best_size, ecmdata.NAF_dictionary_size);
			OutputStr (thread_num, buf);
			ecm_stage1_memory_usage (thread_num, &ecmdata);
			if (ecmdata.stage1_bitnum == 0) ed_swap (ecmdata.e, ecmdata.dict_start);
			NAF_dictionary_init (&ecmdata, &NAF_index, &num_doublings);
			if (ecmdata.factor != NULL) goto bingo;			// Highly unlikely that the modinv in dictionary init found a factor

/* Init variables used in calculating percent complete. */

			chunk_size = (double) ((ecmdata.stage1_prime < ecmdata.B ? ecmdata.stage1_prime : ecmdata.B) - ecmdata.stage1_start_prime);
			chunk_size_over_num_doublings = chunk_size / (double) num_doublings;

/* Process each bit in the big exponent */

			while (ecmdata.stage1_bitnum < num_doublings) {

/* Unless resuming from a save file, use first NAF_index to initialize the starting Edwards point.  This avoids gwcopys. */

				struct ed *ed_dbl_src = &ecmdata.e;		// Address of the Edwards point to double (usually ecmdata.e)
				if (ecmdata.stage1_bitnum == 0) {
					if (!ed_alloc (&ecmdata, &ecmdata.e)) goto oom;
					ed_dbl_src = &ecmdata.NAF_dictionary[NAF_index];
				}

/* Either double point or double point and add from dictionary */

				if (! mpz_tstbit64 (ecmdata.stage1_exp, num_doublings - ecmdata.stage1_bitnum - 1)) {
					stop_reason = 0;
					saving = 0;
					gwerror_checking (&ecmdata.gwdata, ERRCHK || near_fft_limit || ((ecmdata.stage1_bitnum & 127) == 64));
					int options = (ecmdata.stage1_bitnum+1 == num_doublings) ? ED_XYZ : ED_STARTNEXTFFT;
					ed_dbl (&ecmdata, ed_dbl_src, &ecmdata.e, ED_RESULT_FOR_DBL | options);
				} else {
					bool	subtract;
					gwerror_checking (&ecmdata.gwdata, ERRCHK || near_fft_limit || ((ecmdata.stage1_bitnum & 127) == 64));
					ed_dbl (&ecmdata, ed_dbl_src, &ecmdata.e, ED_RESULT_FOR_ADD | ED_STARTNEXTFFT);
					stop_reason = stopCheck (thread_num);
					saving = testSaveFilesFlag (thread_num);
					gwerror_checking (&ecmdata.gwdata, stop_reason || saving || ERRCHK || near_fft_limit);
					NAF_code (&ecmdata, num_doublings - ecmdata.stage1_bitnum - 1, &NAF_index, &subtract);
					int options = (stop_reason || saving || ecmdata.stage1_bitnum+1 == num_doublings) ? ED_XYZ : ED_STARTNEXTFFT;
					if (subtract) options |= ED_SUBTRACT;
					ed_add (&ecmdata, &ecmdata.e, &ecmdata.NAF_dictionary[NAF_index], &ecmdata.e, ED_RESULT_FOR_DBL | options);
				}

/* Calculate our stage 1 percentage complete */

				ecmdata.stage1_bitnum++;
				w->pct_complete = (ecmdata.stage1_start_prime + ecmdata.stage1_bitnum * chunk_size_over_num_doublings) * one_over_B;

/* Output the title every so often */

				if (first_iter_msg ||
				    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
					sprintf (buf, "%.*f%% of %s ECM curve %" PRIu32 " stage 1",
						 (int) PRECISION, trunc_percent (w->pct_complete), ecmdata.N_short_string_rep, ecmdata.curve);
					title (thread_num, buf);
					last_output_t = gw_get_fft_count (&ecmdata.gwdata);
				}

/* Print a message every so often */

				if (first_iter_msg ||
				    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
					sprintf (buf, "%s curve %" PRIu32 " stage 1 at exponent bit %" PRIu32 " [%.*f%%].",
						 ecmdata.N_short_string_rep, ecmdata.curve, ecmdata.stage1_bitnum,
						 (int) PRECISION, trunc_percent (w->pct_complete));
					end_timer (timers, 0);
					if (first_iter_msg) {
						strcat (buf, "\n");
					} else {
						strcat (buf, " Time: ");
						print_timer (timers, 0, buf, TIMER_NL);
					}
					if (ecmdata.stage1_bitnum != 2) OutputStr (thread_num, buf);
					start_timer_from_zero (timers, 0);
					last_output = gw_get_fft_count (&ecmdata.gwdata);
					first_iter_msg = FALSE;
				}

/* Check for errors */

				if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;

/* Write a save file when the user interrupts the calculation and every DISK_WRITE_TIME minutes. */

				if (stop_reason || saving) {
					if (!ed_check (&ecmdata, &ecmdata.e)) goto ed_err;
					ecm_save (&ecmdata);
					if (stop_reason) goto exit;
				}
			}

/* Free the NAF dictionary */

			NAF_dictionary_free (&ecmdata);

/* Are we done?  If not, prepare for the next looping. */

			if (ecmdata.stage1_prime > ecmdata.B) break;
			ecmdata.stage1_bitnum = 0;
		}
		mpz_clear (ecmdata.stage1_exp), ecmdata.stage1_exp_initialized = FALSE;
		ecmdata.stage1_bitnum = 0;

/* Validate the final point */

		if (!ed_check (&ecmdata, &ecmdata.e)) goto ed_err;

/* Convert stage 1 end point to Montgomery for stage 2 */

		ed_to_Montgomery (&ecmdata);
		ecmdata.montg_stage1 = TRUE;		// Tell ecm_save to save the Montgomery point
	}

/* Stage 1 complete */

	end_timer (timers, 0);
	end_timer (timers, 1);
	if (stage1_timer != 0.0) end_timer (&stage1_timer, 0);
	sprintf (buf, "Stage 1 complete. %" PRIu64 " transforms, %lu modular inverses. Total time: ", gw_get_fft_count (&ecmdata.gwdata), ecmdata.modinv_count);
	print_timer (timers, 1, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	last_output = last_output_t = ecmdata.modinv_count = 0;
	gw_clear_fft_count (&ecmdata.gwdata);

/* Print out round off error */

	if (ERRCHK) {
		sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&ecmdata.gwdata));
		OutputStr (thread_num, buf);
		gw_clear_maxerr (&ecmdata.gwdata);
	}

/* If we aren't doing a stage 2, then check to see if we found a factor. */
/* If we are doing a stage 2, then the stage 2 init will do this GCD for us. */

	if (ecmdata.C <= ecmdata.B) {
skip_stage_2:	start_timer_from_zero (timers, 0);
		if (ecmdata.xz.z != NULL) stop_reason = gcd (&ecmdata.gwdata, thread_num, ecmdata.xz.z, ecmdata.N, &ecmdata.factor);
		else stop_reason = gcd (thread_num, ecmdata.Qz_binary, ecmdata.N, &ecmdata.factor);
		if (stop_reason) {
			ecm_save (&ecmdata);
			goto exit;
		}
		end_timer (timers, 0);
		strcpy (buf, "Stage 1 GCD complete. Time: ");
		print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
		OutputStr (thread_num, buf);
		if (ecmdata.factor != NULL) goto bingo;

/* Alexander Kruppa wrote this code to normalize and output the x value along with N and sigma so that it can be used in Paul Zimmermann's */
/* superior GMP-ECM implementation of stage 2. */

		if (IniGetInt (INI_FILE, "GmpEcmHook", 0)) {
			char	*msg, *buf;
			int	msglen, i, leadingzeroes;
			giant	gx;
			const char *hex = "0123456789ABCDEF";

			stop_reason = normalize (&ecmdata, ecmdata.xz.x, ecmdata.xz.z);
			if (stop_reason) goto exit;

			gx = popg (&ecmdata.gwdata.gdata, ((int) ecmdata.gwdata.bit_length >> 5) + 10);
			if (gx == NULL) goto oom;
			if (gwtogiant (&ecmdata.gwdata, ecmdata.xz.x, gx)) goto oom;  // Unexpected error, return oom for lack of a better error message
			modgi (&ecmdata.gwdata.gdata, ecmdata.N, gx);

			msglen = ecmdata.N->sign * 8 + 5;
			buf = (char *) malloc (msglen + msglen + 80);
			if (buf == NULL) goto oom;
			strcpy (buf, "N=0x");
			msg = buf + strlen (buf);
			leadingzeroes = 1; /* Still eat leading zeroes? */
			for (i = 0; i < ecmdata.N->sign * 8; i++) {
				char nibble = ( ecmdata.N->n[ecmdata.N->sign - 1 - i/8] >> ((7-i%8)*4)) & 0xF;
				if (nibble != 0) leadingzeroes = 0;
				if (!leadingzeroes) *msg++ = hex[nibble];
			}
			strcpy (msg, "; QX=0x");
			msg = msg + strlen (msg);
			leadingzeroes = 1;
			for (i = 0; i < gx->sign * 8; i++) {
				char nibble = ( gx->n[gx->sign - 1 - i/8] >> ((7-i%8)*4)) & 0xF;
				if (nibble != 0) leadingzeroes = 0;
				if (!leadingzeroes) *msg++ = hex[nibble];
			}
			strcpy (msg, "; SIGMA=");
			msg = msg + strlen (msg);
			sprintf (msg, "%" PRIu64 "\n", ecmdata.sigma);
			writeResults (buf);
			free (buf);
			pushg (&ecmdata.gwdata.gdata, 1);
		}

/* Now do the next ECM curve */

		goto more_curves;
	}

/*
   Stage 2:  We support two types of stage 2's here.  One uses less memory and uses fewer extended GCDs, but is slower in accumulating
   each found prime.  Thanks to Richard Crandall and Paul Zimmermann for letting me liberally use their code and ideas here.
   x, z: coordinates of Q at the beginning of stage 2
*/

/* Convert xz to binary.  This is what restart3 expects when resuming from a save file.  We'll likely need to convert to binary anyway when we switch FFT sizes. */

	ecmdata.Qx_binary = allocgiant (((int) ecmdata.gwdata.bit_length >> 5) + 10);
	if (ecmdata.Qx_binary == NULL) goto oom;
	gwtogiant (&ecmdata.gwdata, ecmdata.xz.x, ecmdata.Qx_binary);
	ecmdata.Qz_binary = allocgiant (((int) ecmdata.gwdata.bit_length >> 5) + 10);
	if (ecmdata.Qz_binary == NULL) goto oom;
	gwtogiant (&ecmdata.gwdata, ecmdata.xz.z, ecmdata.Qz_binary);
	free_xz (&ecmdata, &ecmdata.xz);

/* Change state to between stage 1 and 2 */

	ecmdata.state = ECM_STATE_MIDSTAGE;
	sprintf (w->stage, "C%" PRIu32 "S2", ecmdata.curve);
	w->pct_complete = 0.0;
	start_timer_from_zero (&stage2_timer, 0);

/* Some users have requested that a save file be written here.  Sometimes the vast amount of stage 2 memory allocated makes their machine thrash which */
/* may require a reboot.  Upon resumption, the tail end of stage 1 calculations then need to be repeated.  For now, we'll make this the default behavior. */

	if (IniGetInt (INI_FILE, "SaveBeforeStage2", 1)) ecm_save (&ecmdata);

/* No restrictions on polymult D value */

	ecmdata.required_missing = 0;

/* Entry point for continuing stage 2 from a save file.  When continuing from a stage 2 save file, we have either a normalized or unnormalized xz value */
/* and possibly a GCD accumulator.  These values come in binary format.  There also may be restrictions on the D value selected. */

restart3:
	ASSERTG (ecmdata.xz.x == NULL && ecmdata.xz.x == NULL && ecmdata.gg == NULL);
	Dmultiple_map.clear ();			// Map should already be clear
	relp_set_map.clear ();			// Map should already be clear

/* Make sure we will have enough memory to run stage 2 at some time.  We need at least 13 gwnums in stage 2 main loop. */

	unsigned long min_memory;
	min_memory = cvt_gwnums_to_mem (&ecmdata.gwdata, 13.0);
	if ((int) max_mem (thread_num) < min_memory) {
		sprintf (buf, "Skipping stage 2 due to insufficient memory -- %luMB needed.\n", min_memory);
		OutputStr (thread_num, buf);
		goto skip_stage_2;
	}

/* Output message, change window title */

	start_timer_from_zero (timers, 0);
	sprintf (buf, "%s ECM curve %" PRIu32 " stage 2 init", ecmdata.N_short_string_rep, ecmdata.curve);
	title (thread_num, buf);

/* Cost several FFT sizes!  Polymult may require several EXTRA_BITS to safely accumulate products in FFT space. */

replan:	unsigned long best_fftlen;
	unsigned int memory;
	int	forced_stage2_type, best_stage2_type, best_fails;
	double	best_efficiency, best_poly_efficiency;
	char	msgbuf[2000];

	forced_stage2_type = IniGetInt (INI_FILE, "ForceECMStage2Type", 99);		// 0 = pairing, 1 = poly, 99 = not forced (either)
	memory = 0;
	best_fftlen = 0;
	best_fails = 0;
	best_poly_efficiency = 0.0;
	msgbuf[0] = 0;
	for (bool found_best = FALSE; ; ) {

/* Initialize the polymult library (needed for calling polymult_mem_required).  Let user override polymult tuning parameters. */

		polymult_init (&ecmdata.polydata, &ecmdata.gwdata);
		polymult_set_max_num_threads (&ecmdata.polydata, ecmdata.stage2_threads);
		polymult_default_tuning (&ecmdata.polydata,
					 IniGetInt (INI_FILE, "PolymultCacheSize", CPU_NUM_L2_CACHES > 0 ? CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES : 256),
					 IniGetInt (INI_FILE, "PolymultCacheSize2", CPU_NUM_L3_CACHES > 0 ? CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES : 6144));
		ecmdata.gwdata.scramble_arrays = (char) IniGetInt (INI_FILE, "PolymultScramble", ecmdata.gwdata.scramble_arrays);
		ecmdata.polydata.strided_writes_end = IniGetInt64 (INI_FILE, "PolymultStridedWritesEnd", ecmdata.polydata.strided_writes_end);
		ecmdata.polydata.streamed_stores_start = IniGetInt64 (INI_FILE, "PolymultStreamStores", 0); // Default is always stream stores because of POLYMULT_NOUNFFT
		gwset_polymult_safety_margin (&ecmdata.gwdata, 0.0);		// If needed, reset polymult safety margin while calculating new poly size

/* When recovering from a roundoff error, only cost FFT lengths larger than the FFT length that generated the roundoff error. */

		if (gwfftlen (&ecmdata.gwdata) > maxerr_fftlen) {
			double efficiency;

/* Calculate the amount of memory we can use in stage 2.  We must have 1MB for a pairing map + a minimum of 13 temporaries (D = 30) -- 6 for mQ_next */
/* computations (Qm, Qprevm, QD), 4 for nQx, gg, 2 for ell_add_xz_noscr temps.  If not continuing from a stage 2 save file then assume 250 temporaries */
/* (value pulled from thin air, hopefully allows D = 210, multiplier = 5) and a few MB for a pairing map will provide us with reasonable execution speed. */
/* Otherwise, we desire enough memory to use the save file's pairing map. */

			if (memory == 0) {
				unsigned int min_memory, desired_memory;	/* Memory is in MB */
				min_memory = 1 + cvt_gwnums_to_mem (&ecmdata.gwdata, 13);
				if (ecmdata.state < ECM_STATE_STAGE2 || ecmdata.pairmap == NULL) desired_memory = 3 + cvt_gwnums_to_mem (&ecmdata.gwdata, 250);
				else desired_memory = (unsigned int) (ecmdata.pairmap_size >> 20) + cvt_gwnums_to_mem (&ecmdata.gwdata, ecmdata.stage2_numvals);
				stop_reason = avail_mem (thread_num, min_memory, desired_memory, &memory);
				if (stop_reason) {
					if (ecmdata.state == ECM_STATE_MIDSTAGE) ecm_save (&ecmdata);
					goto exit;
				}

/* Factor in the multiplier that we set to less than 1.0 when we get unexpected memory allocation errors. */
/* Make sure we can still allocate minimum number of temporaries. */

				memory = (unsigned int) (ecmdata.pct_mem_to_use * (double) memory);
				if (memory < min_memory) {
					stop_reason = avail_mem_not_sufficient (thread_num, min_memory, desired_memory);
					if (ecmdata.state == ECM_STATE_MIDSTAGE) ecm_save (&ecmdata);
					goto exit;
				}

/* Output a message telling us how much memory is available */

				sprintf (buf, "Available memory is %uMB.\n", memory);
				OutputStr (thread_num, buf);
			}

/* Choose the best plan implementation given the current FFT length and available memory.  We can skip costing prime pairing for larger FFT lengths. */
/* The returned efficiency is in terms of stage 1 FFT length. */

			efficiency = ecm_stage2_impl (&ecmdata, memory, found_best ? best_stage2_type : best_fftlen ? 1 : forced_stage2_type, msgbuf + strlen (msgbuf));
			// If using pairmap from a save file, don't cost any other options
			if (ecmdata.pairmap != NULL) break;

/* Multiply efficiency by ten trillion solely for pretty output in case PolyVerbose is set. */

			efficiency *= 1.0e13;
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
if (efficiency == 0.0) sprintf (buf, "FFT: %d no stage 2 plan works\n", (int) gwfftlen (&ecmdata.gwdata));
else sprintf (buf, "FFT: %d, B2: %" PRIu64 "/%" PRIu64 ", numvals: %" PRIu64 "/%d, poly: %" PRIu64 ", efficiency: %.0f\n", (int) gwfftlen (&ecmdata.gwdata), ecmdata.C,
		ecmdata.B2_start + ecmdata.numDsections * ecmdata.D,
		ecmdata.stage2_numvals, (int) cvt_mem_to_gwnums_adj (&ecmdata.gwdata, memory, 0.0), ecmdata.poly_size, efficiency);
OutputStr (thread_num, buf); }

			// If we've found our best stage 2 implementation, break
			if (found_best) break;

			// Remember the best efficiency found thusfar
			if (efficiency != 0.0 && (best_fftlen == 0 || efficiency > best_efficiency)) {
				best_fftlen = gwfftlen (&ecmdata.gwdata);
				best_stage2_type = ecmdata.stage2_type;
				best_efficiency = efficiency;
			}

			// Remember the most efficient poly found thusfar
			if (ecmdata.stage2_type == ECM_STAGE2_POLYMULT && efficiency > best_poly_efficiency) {
				best_poly_efficiency = efficiency;
				best_fails = 0;
			}
		}

		// If forced_stage2_type is prime pairing, then we do not need to try larger FFT lengths
		if (best_fftlen && forced_stage2_type == 0) found_best = TRUE;

		// Small optimization: Skip costing larger FFT lengths if there are less than 60 numvals
		if (best_fftlen && (double) memory * 1000000.0 / (double) array_gwnum_size (&ecmdata.gwdata) < 60.0) found_best = TRUE;

		// If we fail to get a new best efficient poly twice in a row, then we've found our best poly plan
//GW: Likely costing overkill
		if (best_poly_efficiency != 0.0) best_fails++;
		if (best_fails == 3) found_best = TRUE;

		// Switch to next larger or confirmed best FFT length
		for ( ; ; ) {
			unsigned long next_fftlen = gwfftlen (&ecmdata.gwdata) + 1;  // Try next larger FFT length
			if (next_fftlen < maxerr_fftlen) next_fftlen = maxerr_fftlen;
			if (found_best) next_fftlen = best_fftlen;
			msgbuf[0] = 0;
			// Detect when no FFT length change is necessary (e.g. prime pairing selected and memory is very tight)
			if (next_fftlen == gwfftlen (&ecmdata.gwdata)) break;
			// Save Ad4, terminate current FFT size.
			if (ecmdata.Ad4 != NULL) {
				ecmdata.Ad4_binary = allocgiant (((int) ecmdata.gwdata.bit_length >> 5) + 10);
				if (ecmdata.Ad4_binary == NULL) goto oom;
				gwtogiant (&ecmdata.gwdata, ecmdata.Ad4, ecmdata.Ad4_binary);
				ecmdata.Ad4 = NULL;
			}
			polymult_done (&ecmdata.polydata);
			gwdone (&ecmdata.gwdata);
			// Re-init gwnum with a larger FFT
			gwinit (&ecmdata.gwdata);
			if (next_fftlen != best_fftlen) gwset_information_only (&ecmdata.gwdata);
			if (IniGetInt (INI_FILE, "UseLargePages", 0)) gwset_use_large_pages (&ecmdata.gwdata);
			if (HYPERTHREAD_LL) sp_info->normal_work_hyperthreading = TRUE, gwset_will_hyperthread (&ecmdata.gwdata, 2);
			gwset_bench_cores (&ecmdata.gwdata, HW_NUM_CORES);
			gwset_bench_workers (&ecmdata.gwdata, NUM_WORKERS);
			if (ERRCHK) gwset_will_error_check (&ecmdata.gwdata);
			gwset_num_threads (&ecmdata.gwdata, ecmdata.stage2_threads);
			gwset_thread_callback (&ecmdata.gwdata, SetAuxThreadPriority);
			gwset_thread_callback_data (&ecmdata.gwdata, sp_info);
			gwset_safety_margin (&ecmdata.gwdata, IniGetFloat (INI_FILE, "ExtraSafetyMargin", 0.0));
			gwset_minimum_fftlen (&ecmdata.gwdata, next_fftlen);
			gwset_using_polymult (&ecmdata.gwdata);
			gwset_use_spin_wait (&ecmdata.gwdata, IniGetInt (INI_FILE, "SpinWait", 0));
			if (w->n) res = gwsetup (&ecmdata.gwdata, w->k, w->b, w->n, w->c);
			else res = gwsetup_general_mod_giant (&ecmdata.gwdata, ecmdata.N);
			if (res == GWERROR_TOO_LARGE && best_fftlen) {
				found_best = TRUE;
				continue;
			}
			if (res) {
				sprintf (buf, "Cannot initialize FFT code, errcode=%d\n", res);
				OutputBoth (thread_num, buf);
				stop_reason = STOP_FATAL_ERROR;
				goto exit;
			}
			gwerror_checking (&ecmdata.gwdata, ERRCHK || near_fft_limit);
			if (gwfftlen (&ecmdata.gwdata) != ecmdata.stage1_fftlen) {
				char	fft_desc[200];
				gwfft_description (&ecmdata.gwdata, fft_desc);
				sprintf (msgbuf, "Switching to %s\n", fft_desc);
			}
			break;
		}
	}

// Output the FFT switching and stage 2 planning messages accmulated above

	OutputStr (thread_num, msgbuf);

// Restore the xz.x, xz.z, and Ad4 values.  Don't delete the Qx and Qz binaries - we'll use them to create save files.

	if (!alloc_xz (&ecmdata, &ecmdata.xz)) goto lowmem;
	gianttogw (&ecmdata.gwdata, ecmdata.Qx_binary, ecmdata.xz.x);
	if (ecmdata.Qz_binary == NULL) dbltogw (&ecmdata.gwdata, 1.0, ecmdata.xz.z);
	else gianttogw (&ecmdata.gwdata, ecmdata.Qz_binary, ecmdata.xz.z);
	if (ecmdata.Ad4 == NULL) {
		if (ecmdata.Ad4_binary != NULL) {
			ecmdata.Ad4 = gwalloc (&ecmdata.gwdata);
			if (ecmdata.Ad4 == NULL) goto lowmem;
			gianttogw (&ecmdata.gwdata, ecmdata.Ad4_binary, ecmdata.Ad4);
			free (ecmdata.Ad4_binary), ecmdata.Ad4_binary = NULL;
			gwfft (&ecmdata.gwdata, ecmdata.Ad4, ecmdata.Ad4);
		} else {
			stop_reason = init_curve (&ecmdata);
			if (stop_reason) goto possible_lowmem;
		}
	}

/* For historical reasons, swap the stage 1 xz value to QD */
//GW: change this?

	xzswap (ecmdata.xz, ecmdata.QD);

/* Record the amount of memory this thread will be using in stage 2 */

	memused = cvt_array_gwnums_to_mem (&ecmdata.gwdata, ecmdata.stage2_numvals);
	memused += 2 * (int) ecmdata.gwdata.bit_length >> 23;
	memused += (int) (ecmdata.pairmap_size >> 20);
	// To dodge possible infinite loop if ecm_stage2_impl allocates too much memory (it shouldn't), decrease the percentage of memory we are allowed to use
	// Beware that replaning may allocate a larger pairing map
	if (set_memory_usage (thread_num, MEM_VARIABLE_USAGE, memused)) {
		ecmdata.pct_mem_to_use *= 0.99;
		free (ecmdata.pairmap); ecmdata.pairmap = NULL;
		goto replan;
	}

/* If doing an FFT/polymult stage 2, go do that.  Otherwise, use old-fashioned prime pairing stage 2 */

	if (ecmdata.stage2_type == ECM_STAGE2_POLYMULT) goto ecm_polymult;

/*---------------------------------------------------------------------
|	Traditional prime pairing stage 2
+---------------------------------------------------------------------*/

/* We won't be using polymult in prime pairing stage 2 */

	polymult_done (&ecmdata.polydata);

/* Output a useful message regarding memory usage */

	sprintf (buf, "Using %uMB of memory.\n", memused);
	OutputStr (thread_num, buf);

/* Create a new map of (hopefully) close-to-optimal prime pairings */

	if (ecmdata.pairmap == NULL) {
		int fill_window = pair_window_size (ecmdata.gwdata.bit_length, ecmdata.relp_sets);
		gwfree_internal_memory (&ecmdata.gwdata);
		stop_reason = fill_pairmap (ecmdata.thread_num, &ecmdata.sieve_info, ecmdata.D, fill_window,0,0,0,
					    ecmdata.totrels, ecmdata.relp_sets+3, ecmdata.first_relocatable, ecmdata.last_relocatable,
					    ecmdata.B2_start, ecmdata.C, ecmdata.max_pairmap_Dsections, &ecmdata.pairmap, &ecmdata.pairmap_size);
		if (stop_reason) goto exit;
		ecmdata.pairmap_ptr = ecmdata.pairmap;
		ecmdata.Dsection = 0;
		ecmdata.relp = -1;			// Indicates last relp was prior to first Dsection
	}

/* Do preliminary processing of the relp_sets */

	process_relp_sets (ecmdata.totrels, ecmdata.numrels, ecmdata.relp_sets, relp_set_map, Dmultiple_map);

/* Output a useful message regarding memory usage and ECM stage 2 implementation */

	if (ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_3MULT)
		sprintf (buf, "Stage 2 uses %uMB of memory, 2 FFTs per prime pair, 3-mult modinv pooling, pool size %d.\n", memused, ecmdata.E);
	else if (ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_3POINT44MULT)
		sprintf (buf, "Stage 2 uses %uMB of memory, 2 FFTs per prime pair, 3.44-mult modinv pooling, pool size %d.\n", memused, ecmdata.E);
	else if (ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_3POINT57MULT)
		sprintf (buf, "Stage 2 uses %uMB of memory, 2 FFTs per prime pair, 3.57-mult modinv pooling, pool size %d.\n", memused, ecmdata.E);
	else if (ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_N_SQUARED)
		sprintf (buf, "Stage 2 uses %uMB of memory, 2 FFTs per prime pair, N^2 modinv pooling, pool size %d.\n", memused, ecmdata.E);
	else if (!ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_3MULT)
		sprintf (buf, "Stage 2 uses %uMB of memory, 4 FFTs per prime pair, 3-mult modinv pooling.\n", memused);
	else if (!ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_3POINT44MULT)
		sprintf (buf, "Stage 2 uses %uMB of memory, 4 FFTs per prime pair, 3.44-mult modinv pooling.\n", memused);
	else if (!ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_3POINT57MULT)
		sprintf (buf, "Stage 2 uses %uMB of memory, 4 FFTs per prime pair, 3.57-mult modinv pooling.\n", memused);
	else if (!ecmdata.TWO_FFT_STAGE2 && ecmdata.pool_type == POOL_N_SQUARED)
		sprintf (buf, "Stage 2 uses %uMB of memory, 4 FFTs per prime pair, N^2 modinv pooling.\n", memused);
	OutputStr (thread_num, buf);

/* Initialize variables for second stage.  Ideally we fill up the nQx array with Q^relprime normalized with only one modular inverse. */

	// Calculate the percent completed by previous pairmaps
	base_pct_complete = (double) (ecmdata.B2_start - ecmdata.last_relocatable) / (double) (ecmdata.C - ecmdata.last_relocatable);
	// Calculate the percent completed by each relative prime in this pairmap
	one_relp_pct = (1.0 - base_pct_complete) / (double) (ecmdata.numDsections * ecmdata.totrels);
	// Calculate the percent completed by previous pairmaps and the current pairmap
	w->pct_complete = base_pct_complete + (double) (ecmdata.Dsection * ecmdata.totrels) * one_relp_pct;

/* Allocate nQx array of pointers to relative prime gwnums.  Allocate an array large enough to hold x & z values for all relp sets. */
/* This is much more than we will need once stage 2 init completes. */

	int	num_relp_sets;		// Total number of relp_sets including intermediate relp_sets
	num_relp_sets = (int) relp_set_map.size();
	ecmdata.nQx = (gwnum *) malloc (num_relp_sets * ecmdata.numrels * 2 * sizeof (gwnum));
	if (ecmdata.nQx == NULL) goto lowmem;

/* Allocate some of the memory for modular inverse pooling */

	stop_reason = normalize_pool_init (&ecmdata, ecmdata.stage2_numvals);
	if (stop_reason) goto possible_lowmem;

/* Calculate some handy values for computing the first two relp sets: (0,-1) */

	int	z_offset;		// Offset in nQx array from .x value to .z value
	int	set_minus1_nQx_index;	// Index into nQx array for the -1 relp set
	z_offset = num_relp_sets * ecmdata.numrels;
	set_minus1_nQx_index = relp_set_map.find(-1)->second.nQx_store;
	// Map nth relp to either set 0 nQx index or set -1 nQx index
	#define nqxmap(relp)	((relp) < ecmdata.numrels ? (relp) : set_minus1_nQx_index + (relp) - ecmdata.numrels)

/* We have two approaches for computing nQx, if memory is really tight we compute every odd multiple of x. */
/* Otherwise, if D is divisible by 3 we compute the 1,5 mod 6 multiples which reduces initialization cost by up to 33%. */

	if (!ecmdata.TWO_FFT_STAGE2 || ecmdata.D % 3 != 0) {
		struct xz t1, t2, t3, t4;
		struct xz *Q1, *Q2, *Q3, *Qi, *Qiminus2, *Qscratch;
		int	have_QD;
		uint32_t totrels;

/* Allocate memory and init values for computing nQx.  We need Q^1, Q^2, Q^3. */

		if (!alloc_xz (&ecmdata, &t1)) goto lowmem;
		if (!alloc_xz (&ecmdata, &t2)) goto lowmem;
		if (!alloc_xz (&ecmdata, &t3)) goto lowmem;
		t4.x = NULL; t4.z = NULL;					// Only needed if Q2 is needed after QD is calculated

/* Compute Q^2 and place in ecmdata.QD.  Q^2 will be replaced by Q^D later on in this loop.  Remember Q^1 as the first nQx value. */

		xzswap (t1, ecmdata.QD);					// Move Q^1 from QD to t1
		Q1 = &t1;							// Q^1
		Q2 = &ecmdata.QD;
		ell_dbl_xz_scr (&ecmdata, Q1, Q2, &t3);				// Q2 = 2 * Q1, scratch = t3
		ecmdata.nQx[0] = Q1->x; ecmdata.nQx[z_offset] = Q1->z; totrels = 1;

/* Compute Q^3 */

		Q3 = &t2;
		ell_add_xz_scr (&ecmdata, Q2, Q1, Q1, Q3, &t3);			// Q3 = Q2 + Q1 (diff Q1), scratch = t3
		ASSERTG (ecmdata.D % 3 == 0);					// We have to add Q3 to nQx array if we ever support this

/* Compute the rest of the nQx values (Q^i for i >= 3) */
/* MEMPEAK: 9 + nQx + 1 (AD4, QD.x&z, 6 for t1 through t3, nQx vals, and 1 for modinv_value assuming N^2 pooling) */
/* BUT! If using the minimum number of totrels (e.g. 4, D=30), two of the nQx values are sharing space with Qiminus2.  Reducing MEMPEAK to --- */
/* MEMPEAK: 7 + nQx + 1 (AD4, 6 for t1 through t3, nQx vals, and 1 for modinv_value assuming N^2 pooling) */

		Qi = Q3;
		Qiminus2 = Q1;
		Qscratch = &t3;
		have_QD = FALSE;
		for (i = 3; ; i += 2) {

/* Compute Q^D which we will need in mQ_init.  We need two different ways to do this based on whether D is 0 or 2 mod 4. */

			if (i + i == ecmdata.D) {
				ASSERTG (Q2 == &ecmdata.QD);
				// In tightest memory case we can overwrite Q2 as it will no longer be needed.  Otherwise, move Q2 to fourth temporary.
				if (totrels != ecmdata.totrels) {
					t4 = ecmdata.QD; Q2 = &t4;				// Move Q2 out of ecmdata.QD
					if (!alloc_xz (&ecmdata, &ecmdata.QD)) goto lowmem;	// Allocate QD
				}
				ell_dbl_xz_scr (&ecmdata, Qi, &ecmdata.QD, Qscratch);		// QD = 2 * Qi
				have_QD = TRUE;
			}
			if (i + i-2 == ecmdata.D) {
				ASSERTG (Q2 == &ecmdata.QD);
				// In tightest memory case we can overwrite Q2 as it will no longer be needed.  Otherwise, move Q2 to fourth temporary.
				if (totrels != ecmdata.totrels) {
					t4 = ecmdata.QD; Q2 = &t4;				// Move Q2 out of ecmdata.QD
					if (!alloc_xz (&ecmdata, &ecmdata.QD)) goto lowmem;	// Allocate QD
				}
				ell_add_xz (&ecmdata, Qi, Qiminus2, Q2, &ecmdata.QD);		// QD = Qi + Qi-2, diff 2
				have_QD = TRUE;
			}

/* Break out of loop when we have all our nQx values less than D */

			if (have_QD && (totrels == ecmdata.totrels || totrels == ecmdata.numrels * 2)) break;

/* Get next odd value, but don't destroy Qiminus2 in case it is an nQx value. */

			ell_add_xz (&ecmdata, Qi, Q2, Qiminus2, Qscratch);			// Next odd value, save in scratch
			if (relatively_prime (i-2, ecmdata.D)) {
				if (ecmdata.totrels <= ecmdata.numrels * 2) {
					stop_reason = add_to_normalize_pool_ro (&ecmdata, Qiminus2->x, Qiminus2->z);
					if (stop_reason) goto possible_lowmem;
				}
				if (!alloc_xz (&ecmdata, Qiminus2)) goto lowmem;
			}
			xzswap (*Qiminus2, *Qi);
			xzswap (*Qi, *Qscratch);
			if (relatively_prime (i+2, ecmdata.D)) {
				ecmdata.nQx[nqxmap(totrels)] = Qi->x;
				ecmdata.nQx[nqxmap(totrels) + z_offset] = Qi->z;
				totrels++;
			}

/* Check for errors, mem settings changed, etc. */

			if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}

/* Free Q2 if it was moved to t4 */

		free_xz (&ecmdata, &t4);

/* If needed, add any of the last computed values to the normalization pool */

		for (i = 1; i <= 2; i++) {
			struct xz *needs_work;
			if (t1.x == ecmdata.nQx[nqxmap(totrels-i)]) needs_work = &t1;
			else if (t2.x == ecmdata.nQx[nqxmap(totrels-i)]) needs_work = &t2;
			else if (t3.x == ecmdata.nQx[nqxmap(totrels-i)]) needs_work = &t3;
			else continue;
			if (ecmdata.totrels <= ecmdata.numrels * 2) {
				stop_reason = add_to_normalize_pool_ro (&ecmdata, needs_work->x, needs_work->z);
				if (stop_reason) goto possible_lowmem;
			}
			needs_work->x = NULL;
			needs_work->z = NULL;
		}

/* Free memory used in computing nQx values */

		free_xz (&ecmdata, &t1);
		free_xz (&ecmdata, &t2);
		free_xz (&ecmdata, &t3);
	}

/* This is the faster 1,5 mod 6 nQx initialization */

	else {
		struct xz t1, t2, t3, t4, t5, t6;
		struct xz *Q1, *Q2, *Q3, *Q5, *Q6, *Q7, *Q11, *Qnext_i;
		struct {
			struct xz *Qi;
			struct xz *Qi_minus6;
			int	Qi_is_relp;
			int	Qi_minus6_is_relp;
		} Q1mod6, Q5mod6, *curr, *notcurr;
		int	have_QD;
		uint32_t totrels;

/* Allocate memory and init values for computing nQx.  We need Q^1, Q^5, Q^6, Q^7, Q^11. */
/* We also need Q^4 to compute Q^D in the middle of the nQx init loop. */

		if (!alloc_xz (&ecmdata, &t1)) goto lowmem;
		if (!alloc_xz (&ecmdata, &t2)) goto lowmem;
		if (!alloc_xz (&ecmdata, &t3)) goto lowmem;
		if (!alloc_xz (&ecmdata, &t4)) goto lowmem;
		if (!alloc_xz (&ecmdata, &t5)) goto lowmem;

/* Compute Q^2 and place in ecmdata.  Q^2 will be replaced by Q^D later on in this loop. */

		ell_dbl_xz_scr (&ecmdata, &ecmdata.QD, &t1, &t5);		// Q2 = 2 * Q1, scratch = t5
		xzswap (t1, ecmdata.QD);					// Move Q^1 from QD to t1
		Q1 = &t1;							// Q^1
		Q2 = &ecmdata.QD;

/* Compute Q^5, Q^6, Q^7, Q^11 */

		Q3 = &t2;
		ell_add_xz (&ecmdata, Q2, Q1, Q1, Q3);				// Q3 = Q2 + Q1 (diff Q1)

		Q5 = &t3;
		ell_add_xz (&ecmdata, Q3, Q2, Q1, Q5);				// Q5 = Q3 + Q2 (diff Q1)

		Q6 = Q3;
		ell_dbl_xz_scr (&ecmdata, Q3, Q6, &t5);				// Q6 = 2 * Q3, scratch = t5, Q3 no longer needed

		Q7 = &t4;
		ell_add_xz (&ecmdata, Q6, Q1, Q5, Q7);				// Q7 = Q6 + Q1 (diff Q5)

		Q11 = &t5;
		ell_add_xz (&ecmdata, Q6, Q5, Q1, Q11);				// Q11 = Q6 + Q5 (diff Q1)

/* Init structures used in the loop below */

		Q1mod6.Qi_minus6 = Q1;
		Q1mod6.Qi_minus6_is_relp = 1;
		Q5mod6.Qi_minus6 = Q5;
		Q5mod6.Qi_minus6_is_relp = relatively_prime (5, ecmdata.D);
		Q1mod6.Qi = Q7;
		Q1mod6.Qi_is_relp = relatively_prime (7, ecmdata.D);
		Q5mod6.Qi = Q11;
		Q5mod6.Qi_is_relp = relatively_prime (11, ecmdata.D);

/* For relprimes, we remember the .x value in the nQx pool.  In the loop below, if totrels <= numrels * 2 we will relinquish ownership of the .z value */
/* when we call add_to_normalize_pool.  If totrels > numrels * 2, we remember the .z value in the nQx pool.  After computing the relprimes below D */
/* we'll use the saved x and z values to compute nQx values above D. */

		ecmdata.nQx[0] = Q1->x; ecmdata.nQx[z_offset] = Q1->z; totrels = 1;
		if (Q5mod6.Qi_minus6_is_relp) { ecmdata.nQx[totrels] = Q5->x; ecmdata.nQx[totrels+z_offset] = Q5->z; totrels++; }
		if (Q1mod6.Qi_is_relp) { ecmdata.nQx[totrels] = Q7->x; ecmdata.nQx[totrels+z_offset] = Q7->z; totrels++; }
		if (Q5mod6.Qi_is_relp) { ecmdata.nQx[totrels] = Q11->x; ecmdata.nQx[totrels+z_offset] = Q11->z; totrels++; }

//GW: For ecmdata.totrels between 2*numrels and 3*numrels, we could normalize some of the .x/.z pairs
#define normalize_or_free(xz,is_relp)	{ if (is_relp) { if (ecmdata.totrels <= ecmdata.numrels * 2) { \
								stop_reason = add_to_normalize_pool_ro (&ecmdata, xz->x, xz->z); \
								if (stop_reason) goto possible_lowmem; } \
							 xz->x = NULL; xz->z = NULL; is_relp = FALSE; } \
					  else free_xz (&ecmdata, xz); }

/* Compute the rest of the nQx values below D (Q^i for i >= 7) */
/* MEMPEAK: 15 (AD4, QD.x&z, 12 for t1 through t6) + nQx + pool_cost(nQx) */
/* But at least 1 nQx and 1 poolz value are still in t1 through t6, so that reduces mempeak by at least two. */

		Qnext_i = &t6;
		have_QD = FALSE;
		for (uint32_t i = 7, i_gap = 4; ; i += i_gap, i_gap = 6 - i_gap) {
			int	next_i_is_relp;

/* Point to the i, i-6 pair to work on */

			curr = (i_gap == 4) ? &Q1mod6 : &Q5mod6;

/* See if we are about to compute the next relative prime */

			next_i_is_relp = (relatively_prime (i+6, ecmdata.D) && totrels < ecmdata.totrels);

/* If so, and if it is the last relprime then free up the some resources to keep maximum number of temporaries to a minimum. */
/* This is of marginal utility as memory peak will usually occur in mQ_init. */
/* Also catch the case where we are using the dead minimum number of relative primes - the first i after D/2 is the last i. */

			if ((next_i_is_relp && have_QD && (totrels+1 == ecmdata.totrels || totrels+1 == ecmdata.numrels * 2)) ||
			    (i+6 > ecmdata.D/2 && ecmdata.totrels == ecmdata.numrels)) {
				notcurr = (curr == &Q5mod6) ? &Q1mod6 : &Q5mod6;
				if (have_QD) normalize_or_free (notcurr->Qi, notcurr->Qi_is_relp);
				normalize_or_free (notcurr->Qi_minus6, notcurr->Qi_minus6_is_relp);
			}

/* Get next i value.  Don't destroy Qi_minus6 in case it is an nQx value. */
/* If totrels <= numrels * 2, our peak memory usage occurs here.  In the penultimate case, assume two of Qi, Qi_minus6, other Qi, other Qi_minus6 are relp */
/* MEMPEAK for penultimate i value: 12 + nQx + pool_cost (AD4, QD.x&z, Qi, Q6, Qi_minus6, Qnext_i, other Qi, other Qi_minus6 + nQx-3 + pool_cost(nQx-3)) */
/* MEMPEAK for last i value: 10 + nQx + pool_cost (AD4, QD.x&z, Qi, Q6, Qi_minus6, Qnext_i + nQx-1 + pool_cost(nQx-1)) */

			if (!alloc_xz (&ecmdata, Qnext_i)) goto lowmem;
			ell_add_xz (&ecmdata, curr->Qi, Q6, curr->Qi_minus6, Qnext_i);		// Next_Qi = Qi + Q6 (diff Q{i-6})

/* We no longer need Qi_minus6.  Normalize it, save it for later calculating nQx values above D, or free it */

			normalize_or_free (curr->Qi_minus6, curr->Qi_minus6_is_relp);

/* Shuffle i values along */

			{
				struct xz *shuffle;
				shuffle = curr->Qi_minus6;
				curr->Qi_minus6 = curr->Qi;
				curr->Qi_minus6_is_relp = curr->Qi_is_relp;
				curr->Qi = Qnext_i;
				curr->Qi_is_relp = next_i_is_relp;
				Qnext_i = shuffle;
			}

/* Compute Q^D which we will need in mQ_init.  Do this with a single ell_add_xz call when we reach two values that are 2 or 4 apart that add to D. */

			if ((i+6) + (i+i_gap) == ecmdata.D) {
				struct xz tmp;
				struct xz *Qgap = Q2;
				ASSERTG (Qgap == &ecmdata.QD);
				if (!alloc_xz (&ecmdata, &tmp)) goto lowmem;
				if (6 - i_gap == 4) ell_dbl_xz_scr (&ecmdata, Q2, Qgap, &tmp);		  // Qgap = Q4 = 2 * Q2, Q2 no longer needed
				ell_add_xz_scr (&ecmdata, Q1mod6.Qi, Q5mod6.Qi, Qgap, &ecmdata.QD, &tmp); // QD = i+6 + i+i_gap (diff Qgap), Qgap no longer needed
				free_xz (&ecmdata, &tmp);
				have_QD = TRUE;
			}

/* If the newly computed i is a relative prime, then 1) if all relprimes are below D, remember .x for a later add_to_nomalize, */
/* or 2) if there are relprimes above D, remember .x and .z in the nQx array for a later calculation of nQx values above D. */

			if (next_i_is_relp) {
				ecmdata.nQx[nqxmap(totrels)] = curr->Qi->x;
				ecmdata.nQx[nqxmap(totrels) + z_offset] = curr->Qi->z;
				totrels++;
			}

/* Break out of loop when we have all our nQx values less than D */

			if (have_QD && (totrels == ecmdata.totrels || totrels == ecmdata.numrels * 2)) break;

/* Check for errors, user abort, restart for mem changed, etc. */

			if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}

/* Free memory used in computing nQx values */

		normalize_or_free (Q1mod6.Qi, Q1mod6.Qi_is_relp);
		normalize_or_free (Q1mod6.Qi_minus6, Q1mod6.Qi_minus6_is_relp);
		normalize_or_free (Q5mod6.Qi, Q5mod6.Qi_is_relp);
		normalize_or_free (Q5mod6.Qi_minus6, Q5mod6.Qi_minus6_is_relp);
		free_xz (&ecmdata, Q6);
	}
	#undef nqxmap
	#undef normalize_or_free

/* Add Q^D computed above to the D-multiples map */

	{
		auto it_Dmult = Dmultiple_map.find (1);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({1, {0, 1}}).first;
		it_Dmult->second.set_val_buffers (&ecmdata.gwdata, ecmdata.QD.x, ecmdata.QD.z, TRUE);
	}

/* Init for computing Q^(multiples of D).  We simply add the starting multiples of D that need to be computed to the D-multiples map. */

	stop_reason = mQ_preinit (&ecmdata, Dmultiple_map);
	if (stop_reason) goto possible_lowmem;

// Calculate all the needed Q^(multiples-of-D).  This is needed even if multiplier<=2 case (Q^mD and Q^(m+1)D are needed) so we must be memory conscious.

	process_Dmult_map (Dmultiple_map);
	for (auto this_Dmult = Dmultiple_map.begin(); this_Dmult != Dmultiple_map.end(); ++this_Dmult) {
		// Ignore the already computed Q^D
		if (this_Dmult->first == 1) continue;
		// If necessary, allocate buffers for this Q^multiple-of-D
		if (this_Dmult->second.val.x == NULL) {
			struct xz tmp;
			if (!alloc_xz (&ecmdata, &tmp)) goto lowmem;
			this_Dmult->second.set_val_buffers (&ecmdata.gwdata, tmp.x, tmp.z, FALSE);
		}
		// Compute using ell_dbl if diff is zero
		auto it_base = Dmultiple_map.find (this_Dmult->second.base);
		if (this_Dmult->second.diff == 0) {
			struct xz tmp;
			if (!alloc_xz (&ecmdata, &tmp)) goto lowmem;
			ell_dbl_xz_scr (&ecmdata, &it_base->second.val, &this_Dmult->second.val, &tmp);
			free_xz (&ecmdata, &tmp);
		}
		// Compute using ell_add if diff is non-zero
		else {
			auto it_addin = Dmultiple_map.find (this_Dmult->second.addin);
			auto it_diff = Dmultiple_map.find (this_Dmult->second.diff);
			ell_add_xz (&ecmdata, &it_base->second.val, &it_addin->second.val, &it_diff->second.val, &this_Dmult->second.val);
			// Free addin, diff if no longer needed
			it_addin->second.free_if_Dmult_last_used_by (this_Dmult->first);
			it_diff->second.free_if_Dmult_last_used_by (this_Dmult->first);
		}
		// Free base if no longer needed
		it_base->second.free_if_Dmult_last_used_by (this_Dmult->first);
		// Check for errors, user abort, restart for mem changed, etc. */
		if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;
		stop_reason = stopCheck (thread_num);
		if (stop_reason) goto possible_lowmem;
	}

/* For the 2-FFT stage 2, mQ_preinit wants the QD^(E/2) value normalized.  Do so now that we've computed the value. */

	if (ecmdata.QD_Eover2.x != NULL) {
		stop_reason = add_to_normalize_pool_ro (&ecmdata, ecmdata.QD_Eover2.x, ecmdata.QD_Eover2.z);
		if (stop_reason) goto possible_lowmem;
		ecmdata.QD_Eover2.z = NULL;
	}

/* There will be no more ell_dbl calls, so free Ad4 */

	gwfree (&ecmdata.gwdata, ecmdata.Ad4);
	ecmdata.Ad4 = NULL;

/* We're about to reach peak memory usage, free any gwnum internal memory */

	gwfree_internal_memory (&ecmdata.gwdata);

/* Compute relp sets other than 0,-1 */
/* MEMPEAK in this code is tricky to calculate.  We do free D-multiples and intermediate relps as soon as possible.  We also normalize as soon as possible. */
/* Peak should occur at last relp calculation where we have up to 6 extra gwnums in play -- 2 for Qbase (1 if base is a permanent set), 2 for Qdiff (1 if */
/* diff is a permanent set), 2 for D-multiple, 1 for Qnew.z), I believe either base or diff must be a permanent set.  Giving us this: */
/* 2FFT MEMPEAK: 14 + nQx gwnums (6 extra gwnums, 8 for computing mQx, QD_Eover2, nQx values) + pooling cost */
/* 4FFT MEMPEAK: 12 + nQx gwnums (6 extra gwnums, 6 for computing mQx, nQx values) + pooling cost */

	int	num_partials;
	num_partials = one_based_modulo (ecmdata.totrels, ecmdata.numrels);
	// Loop over every relp_set we need to calculate
	for (auto it_relp_set = relp_set_map.begin (); it_relp_set != relp_set_map.end (); ++it_relp_set) {
		// Relp sets 0 and -1 are already computed
		if (it_relp_set->first == 0 || it_relp_set->first == -1) continue;
		// Find the base relp_set and diff relp_set needed to create this relp_set
		auto it_base_relp_set = relp_set_map.find (it_relp_set->second.base);
		auto it_diff_relp_set = relp_set_map.find (it_relp_set->second.diff);
		// Find the Dmultiple needed to create this relp_set
		auto it_Dmultiple = Dmultiple_map.find (it_relp_set->second.Dmultiple);
		// Determine if the diff relps are accessed in reverse order
		bool	diff_access_inverted = (it_base_relp_set->first < 0) ^ (it_diff_relp_set->first < 0);
		// Loop over all relative primes below D/2 that need calculating in this relp_set (backwards is better for peak memory)
		for (i = (it_relp_set->second.partial ? num_partials : ecmdata.numrels) - 1; i >= 0; i--) {
			struct xz Qbase, Qdiff, Qnew;
			int	ni;

			// Recreate the base struct xz from the two base nQx entries
			ni = it_base_relp_set->second.nQx_store + i;
			Qbase.x = ecmdata.nQx[ni];
			Qbase.z = ecmdata.nQx[ni + z_offset];
			// Recreate the diff struct xz from the two diff nQx entries
			// If diff has a different sign bit than base, then we need to index the diff relp_set in reverse order
			ni = it_diff_relp_set->second.nQx_store;
			if (!diff_access_inverted) ni += i; else ni += (ecmdata.numrels-1)-i;
			Qdiff.x = ecmdata.nQx[ni];
			Qdiff.z = ecmdata.nQx[ni + z_offset];

			// Set flags if Qbase and/or Qdiff will be freed because they are not used in any more relp_set calculations
			bool Qbase_last_use = it_base_relp_set->second.is_last_use (it_relp_set->first, i, num_partials, ecmdata.numrels, FALSE);
			bool Qdiff_last_use = it_diff_relp_set->second.is_last_use (it_relp_set->first, i, num_partials, ecmdata.numrels, diff_access_inverted);
			bool Dmultiple_to_be_freed = (i == 0 && it_Dmultiple->second.will_free_due_to_relp_last_used_by (it_relp_set->first));

			// Allocate the new relp value
//GW: We could reuse about-to-be-freed Qbase or Qdiff like we do for P-1 and P+1.  However peak memory usage is unlikely to be affected
			if (!alloc_xz (&ecmdata, &Qnew)) goto lowmem;

			// Compute the new relp value
			ell_add_xz (&ecmdata, &Qbase, &it_Dmultiple->second.val, &Qdiff, &Qnew);	// Qnew = base + Dmult, diff e.g. 47+30 or 29+30

			// Add the relp to the nQx array.
			ni = it_relp_set->second.nQx_store + i;
			ecmdata.nQx[ni] = Qnew.x;
			ecmdata.nQx[ni + z_offset] = Qnew.z;

			// If this nQx value will not be used in further relp_set calculations, then normalize it
			if (it_relp_set->second.is_last_use (it_relp_set->first, i, num_partials, ecmdata.numrels, FALSE)) {
				stop_reason = add_to_normalize_pool_ro (&ecmdata, Qnew.x, Qnew.z);
				if (stop_reason) goto possible_lowmem;
			}

			// If base is not needed for further relp_set calculations, then free or add_to_normalize Qbase
			if (Qbase_last_use) {
				if (it_base_relp_set->second.permanent) {
					stop_reason = add_to_normalize_pool_ro (&ecmdata, Qbase.x, Qbase.z);
					if (stop_reason) goto possible_lowmem;
				} else {
					free_xz (&ecmdata, &Qbase);
				}
			}

			// If diff is not needed for further relp_set calculations, then free or add_to_normalize Qdiff
			if (Qdiff_last_use) {
				if (it_diff_relp_set->second.permanent) {
					stop_reason = add_to_normalize_pool_ro (&ecmdata, Qdiff.x, Qdiff.z);
					if (stop_reason) goto possible_lowmem;
				} else {
					free_xz (&ecmdata, &Qdiff);
				}
			}

			// Free the Dmultiple if this is the last time it will be used
			if (Dmultiple_to_be_freed) {
				it_Dmultiple->second.free_if_relp_last_used_by (it_relp_set->first);
			}

			// Check for errors, user abort, restart for mem changed, etc. */
			if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}
	}

/* Clear the maps used to build relp sets */

	Dmultiple_map.clear ();
	relp_set_map.clear ();

/* Normalize all the nQx values */
/* 2FFT MEMPEAK: 7 + nQx gwnums (6 for computing mQx, QD_Eover2.x, nQx values) + pooling cost */
/* 4FFT MEMPEAK: 6 + nQx gwnums (6 for computing mQx, nQx values) + pooling cost */

	stop_reason = normalize_pool (&ecmdata);
	if (stop_reason) goto possible_lowmem;

/* If we found a factor, we're done */

	if (ecmdata.factor != NULL) goto bingo;

/* We now have a normalized Qx value that we can write to a save file */

//GW: Is nQx[0] FFTed?  Can we convert to binary prior to nQx[0] being FFTed?
	if (ecmdata.Qz_binary != NULL) {
		ASSERTG (ecmdata.Qx_binary != NULL);
		gwtogiant (&ecmdata.gwdata, ecmdata.nQx[0], ecmdata.Qx_binary);
		free (ecmdata.Qz_binary), ecmdata.Qz_binary = NULL;
	}

/* Init code that computes Q^m, where m is the first D section we are working on */
/* 2FFT MEMPEAK: 5 + nQx + E + pooling cost (see mQ_init) + another one for gg if resuming a stage 2 */
/* 4FFT MEMPEAK: 6 + nQx (6 for computing mQx, nQx) + another one for gg if resuming a stage 2 */

	stop_reason = mQ_init (&ecmdata);
	if (stop_reason) goto possible_lowmem;

/* Now init the accumulator unless this value was read from a continuation file */
/* 2FFT MEMUSED: 2 + nQx + E (QD_Eover2.x, gg, nQx, mQx) */
/* 4FFT MEMUSED: 7 + nQx gwnums (6 for computing mQx, gg, nQx values) */

	ASSERTG (ecmdata.gg == NULL);
	ecmdata.gg = gwalloc (&ecmdata.gwdata);
	if (ecmdata.gg == NULL) goto lowmem;
	if (ecmdata.gg_binary != NULL) {
		gianttogw (&ecmdata.gwdata, ecmdata.gg_binary, ecmdata.gg);
		free (ecmdata.gg_binary), ecmdata.gg_binary = NULL;
	} else
		dbltogw (&ecmdata.gwdata, 1.0, ecmdata.gg);

/* Last chance to create a save file that doesn't lock us into a particular stage 2 implementation.  Check for user requesting a stop. */

	stop_reason = stopCheck (thread_num);
	if (stop_reason) goto possible_lowmem;

/* Initialization of stage 2 complete */

	sprintf (buf, "%.*f%% of %s ECM curve %" PRIu32 " stage 2 (using %uMB)",
		 (int) PRECISION, trunc_percent (w->pct_complete), ecmdata.N_short_string_rep, ecmdata.curve, memused);
	title (thread_num, buf);

	end_timer (timers, 0);
	sprintf (buf, "Stage 2 init complete. %" PRIu64 " transforms, %lu modular inverses. Time: ", gw_get_fft_count (&ecmdata.gwdata), ecmdata.modinv_count);
	print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&ecmdata.gwdata);
	ecmdata.modinv_count = 0;

/* Now do stage 2 */

/* We do prime pairing with each loop iteration handling the range m-Q to m+Q where m is a multiple of D and Q is the */
/* Q-th relative prime to D.  Totrels is often much larger than the number of relative primes less than D.  This Preda */
/* optimization (originally from Montgomery) provides us with many more chances to find a prime pairing. */

/* Accumulate (mQx - nQx)(mQz + nQz) - mQx mQz + nQx nQz.		*/
/* Since nQz = 1, we have (the 4 FFT per prime continuation)		*/
/*		== (mQx - nQx)(mQz + 1) - mQx mQz + nQx			*/
/*		== mQx mQz - nQx mQz + mQx - nQx - mQx mQz + nQx	*/
/*		== mQx - nQx mQz					*/
/* If mQz also = 1 (the 2 FFT per prime continuation) then we accumulate*/
/*		== mQx - nQx						*/

	gwnum	mQx, mQz, t1;
	ecmdata.state = ECM_STATE_STAGE2;
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);
	last_output = last_output_t = ecmdata.modinv_count = 0;
	ecmdata.Dsection--, ecmdata.relp += ecmdata.totrels;		// Funky hack to make sure mQ_next is called on first time through this loop
	t1 = NULL;
	for ( ; ; ) {

/* Get next pairing from the pairmap */

		ecmdata.relp += next_pair (&ecmdata.pairmap_ptr);

/* Make a quick check for end of the pairmap and no more pairmaps needed (i.e. we're done with stage 2) */

		if (ecmdata.relp >= (int32_t) ecmdata.totrels && ecmdata.Dsection + ecmdata.relp / ecmdata.totrels >= ecmdata.numDsections) break;

/* Move to next D section when appropriate */

		while (ecmdata.relp >= (int32_t) ecmdata.totrels) {
			ecmdata.Dsection++;
			ecmdata.relp -= ecmdata.totrels;

/* Compute this Q^m value */
/* 2FFT MEMUSED: 2 + nQx + E gwnums (QD_Eover2.x, gg, nQx, E normalized D values) */
/* 2FFT MEMPEAK: 4 + nQx + E + pooling cost (see mQ_next) */
/* 4FFT MEMUSED: 7 + nQx gwnums (6 for computing mQx, gg, nQx) */
/* 4FFT MEMPEAK: 9 + nQx (2 for ell_add temporaries) */

			gwfree (&ecmdata.gwdata, t1);			/* Free 4 FFT temporary so that mQ_next can use it */
			stop_reason = mQ_next (&ecmdata, &mQx, &mQz);
			if (stop_reason) {
				// In case stop_reason is out-of-memory, free some up before calling ecm_save.
				mQ_term (&ecmdata);
				ecm_save (&ecmdata);
				goto exit;
			}
			if (ecmdata.factor != NULL) goto bingo;

/* 4 FFT implementation requires another temporary.  Allocate it here (after mQ_next has freed its 2 ell_add_xz_noscr temporaries) */

			if (!ecmdata.TWO_FFT_STAGE2) {
				t1 = gwalloc (&ecmdata.gwdata);
				if (t1 == NULL) goto oom;
			}
		}

/* Check for end of pairing map.  If so, generate next pairing map. */

		if (ecmdata.pairmap_ptr == ecmdata.pairmap + ecmdata.pairmap_size) {
			ASSERTG (ecmdata.relp == 0);
			ecmdata.C_done = ecmdata.B2_start + ecmdata.Dsection * ecmdata.D;
			ecmdata.first_relocatable = calc_new_first_relocatable (ecmdata.D, ecmdata.C_done);
			int fill_window = pair_window_size (ecmdata.gwdata.bit_length, ecmdata.relp_sets);
			gwfree_internal_memory (&ecmdata.gwdata);
			stop_reason = fill_pairmap (ecmdata.thread_num, &ecmdata.sieve_info, ecmdata.D, fill_window,0,0,0,
						    ecmdata.totrels, ecmdata.relp_sets+3, ecmdata.first_relocatable, ecmdata.last_relocatable,
						    ecmdata.C_done, ecmdata.C, ecmdata.max_pairmap_Dsections, &ecmdata.pairmap, &ecmdata.pairmap_size);
			if (stop_reason) {
//GW:				is save possible here with no pairmap generated?
				ecm_save (&ecmdata);
				goto exit;
			}
			ecmdata.pairmap_ptr = ecmdata.pairmap;
			ecmdata.relp = -1;
			continue;
		}

/* Check for ESC or save file timer going off */

		saving = testSaveFilesFlag (thread_num);
		stop_reason = stopCheck (thread_num);

/* 2 FFT per prime continuation - deals with all normalized values */

		if (ecmdata.TWO_FFT_STAGE2) {
			gwsubmul4 (&ecmdata.gwdata, mQx, ecmdata.nQx[ecmdata.relp], ecmdata.gg, ecmdata.gg, (!stop_reason && !saving) ? GWMUL_STARTNEXTFFT : 0);
		}

/* 4 FFT per prime continuation - deals with only nQx values normalized */

		else {
			gwmul3 (&ecmdata.gwdata, ecmdata.nQx[ecmdata.relp], mQz, t1, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			gwsubmul4 (&ecmdata.gwdata, mQx, t1, ecmdata.gg, ecmdata.gg, (!stop_reason && !saving) ? GWMUL_STARTNEXTFFT : 0);
		}

/* Calculate stage 2 percentage. */

		w->pct_complete = base_pct_complete + (double) (ecmdata.Dsection * ecmdata.totrels + ecmdata.relp) * one_relp_pct;

/* Check for errors */

		if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;

/* Output the title every so often */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s ECM curve %" PRIu32 " stage 2 (using %uMB)",
				 (int) PRECISION, trunc_percent (w->pct_complete), ecmdata.N_short_string_rep, ecmdata.curve, memused);
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&ecmdata.gwdata);
		}

/* Print a message every so often */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s curve %" PRIu32 " stage 2 at B2=%" PRIu64 " [%.*f%%].",
				 ecmdata.N_short_string_rep, ecmdata.curve, ecmdata.B2_start + ecmdata.Dsection * ecmdata.D + ecmdata.D / 2,
				 (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, " Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&ecmdata.gwdata);
			first_iter_msg = FALSE;
		}

/* Write a save file when the user interrupts the calculation and every DISK_WRITE_TIME minutes. */

		stop_reason = stopCheck (thread_num);
		if (stop_reason || testSaveFilesFlag (thread_num)) {
			ecm_save (&ecmdata);
			if (stop_reason) goto exit;
		}
	}
	gwfree (&ecmdata.gwdata, t1);

/* Move nQx[0] to xz.x in case GCD code calls ecm_save.  Free lots of other stage 2 data. */
/* Set mem usage so that other high memory workers can resume. */

	mQ_term (&ecmdata);
	normalize_pool_term (&ecmdata);
	ecmdata.xz.x = ecmdata.nQx[0];
	for (uint32_t i = 1; i < ecmdata.totrels; i++) gwfree (&ecmdata.gwdata, ecmdata.nQx[i]);
	free (ecmdata.nQx); ecmdata.nQx = NULL;
	free (ecmdata.pairmap); ecmdata.pairmap = NULL;

/* All done except for the final GCD, go do it. */

	goto stage2_done;

/*---------------------------------------------------------------------
|	Polymult stage 2!  See
|		FFT extension for algebraic-group factorization algorithms
|		by Richard P. Brent, Alexander Kruppa, and Paul Zimmermann
|	as well as
|		UCLA dissertation
|		by Peter Montgomery
|	and
|		SCALED REMAINDER TREES
|		by Daniel J. Bernstein
+---------------------------------------------------------------------*/

ecm_polymult:
	start_timer_from_zero (timers, 1);

// Polymult stage 2 does not need a prime sieve

//GW: should we end the sieve - it might be re-usable in next curve  (do we call start sieve with corrent ending p value?)
	end_sieve (ecmdata.sieve_info), ecmdata.sieve_info = NULL;

/* Output a useful message regarding memory usage */

	sprintf (buf, "Using %uMB of memory.  D: %d, degree-%" PRIu64 " polynomials.  Ftree polys in memory: %d\n",
		 memused, ecmdata.D, ecmdata.poly_size, ecmdata.Ftree_polys_in_mem);
	OutputStr (thread_num, buf);

// We must outer loop at least twice, otherwise H(X) will be monic -- the current descent of Ftree code can't handle that.
//GW:  Someday allow ecmdata.numDsections to be ecmdata.poly_size -- saves alloc mem for H(X)

/* Allocate the nQx array of pointers to relative prime gwnums.  We must allocate the array with one call because Windows-or-malloc has some kind */
/* of performance issue where freeing 100,000 individually allocated nQx gwnums was taking 6 minutes on my laptop. */
//GW: use gwalloc_array in prime pairing too?  Make this code common with prime pairing?  Multithread using polymult tools?

	ecmdata.nQx = gwalloc_array (&ecmdata.gwdata, ecmdata.numrels);
	if (ecmdata.nQx == NULL) goto lowmem;

/* Allocate some of the memory for modular inverse pooling */

//GW: Are there costs to calling this with too large a value?  Not very much.  Malloc the pool on-demand rather than upfront?
	stop_reason = normalize_pool_init (&ecmdata, ecmdata.poly_size * 2);	// GW: Shouldn't be more than a handful above poly_size
	if (stop_reason) goto possible_lowmem;

/* This is a fast 1,5 mod 6 nQx initialization */

	start_timer_from_zero (timers, 0);
	ASSERTG (ecmdata.D % 3 == 0);
	{
		struct xz t1, *Q1, *Q2, *Q3, *Q5, *Q6, *Q7, *Q11;
		struct {
			struct xz Qi;
			struct xz Qi_minus6;
			int	Qi_is_relp;
			int	Qi_minus6_is_relp;
		} Q1mod6, Q5mod6, *curr;
		int	i_gap, have_QD;
		uint32_t totrels = 0;

#define Qmodhi_init(Qmod,xz,is_relp)	{ xz = &(Qmod)->Qi; \
					  if (!alloc_xz (&ecmdata, xz)) goto lowmem; \
					  (Qmod)->Qi_is_relp = is_relp; \
					  if ((Qmod)->Qi_is_relp) { gwfree (&ecmdata.gwdata, xz->x); xz->x = ecmdata.nQx[totrels++]; } }
#define Qmodlo_init(Qmod,xz,is_relp)	{ xz = &(Qmod)->Qi_minus6; \
					  if (!alloc_xz (&ecmdata, xz)) goto lowmem; \
					  (Qmod)->Qi_minus6_is_relp = is_relp; \
					  if ((Qmod)->Qi_minus6_is_relp) { gwfree (&ecmdata.gwdata, xz->x); xz->x = ecmdata.nQx[totrels++]; } }
#define normalize_or_free(xz,is_relp)	{ if (is_relp) { stop_reason = add_to_normalize_pool_ro (&ecmdata, (xz)->x, (xz)->z); \
							 if (stop_reason) goto possible_lowmem; \
							 (xz)->x = NULL; (xz)->z = NULL; is_relp = FALSE; } \
					  else free_xz (&ecmdata, xz); }

/* Allocate memory and init values for computing nQx.  We need Q^1, Q^5, Q^6, Q^7, Q^11. */

		Qmodlo_init (&Q1mod6, Q1, 1);
		Qmodhi_init (&Q1mod6, Q7, relatively_prime (7, ecmdata.D));
		Qmodlo_init (&Q5mod6, Q5, relatively_prime (5, ecmdata.D));
		Qmodhi_init (&Q5mod6, Q11, relatively_prime (11, ecmdata.D));
		if (!alloc_xz (&ecmdata, &t1)) goto lowmem;
		Q6 = Q3 = &t1;

/* Compute Q^2 and place in ecmdata.QD.  Q^2 will be replaced by Q^D later on.  Compute Q^3, Q^5, Q^6, Q^7, Q^11. */

		gwcopy_xz (&ecmdata, &ecmdata.QD, Q1);
		ell_dbl_xz_scr (&ecmdata, Q1, Q2 = &ecmdata.QD, Q11);		// Q2 (stored in ecmdata.QD) = 2 * Q1, scratch = Q11
		ell_add_xz (&ecmdata, Q2, Q1, Q1, Q3);				// Q3 = Q2 + Q1 (diff Q1)
		ell_add_xz (&ecmdata, Q3, Q2, Q1, Q5);				// Q5 = Q3 + Q2 (diff Q1)
		ell_dbl_xz_scr (&ecmdata, Q3, Q6, Q11);				// Q6 = 2 * Q3, scratch = Q11, Q3 no longer needed
		ell_add_xz (&ecmdata, Q6, Q1, Q5, Q7);				// Q7 = Q6 + Q1 (diff Q5)
		ell_add_xz (&ecmdata, Q6, Q5, Q1, Q11);				// Q11 = Q6 + Q5 (diff Q1)

/* Compute the rest of the nQx values below D (Q^i for i >= 7) */

		have_QD = FALSE;
		for (i = 7, i_gap = 4; ; i += i_gap, i_gap = 6 - i_gap) {
			struct xz Qnext_i;
			int	next_i_is_relp;

/* Point to the i, i-6 pair to work on */

			curr = (i_gap == 4) ? &Q1mod6 : &Q5mod6;

/* Allocate memory to compute the next Q^i */

			if (!alloc_xz (&ecmdata, &Qnext_i)) goto lowmem;
			next_i_is_relp = (relatively_prime (i+6, ecmdata.D) && totrels < ecmdata.numrels);
			if (next_i_is_relp) { gwfree (&ecmdata.gwdata, Qnext_i.x); Qnext_i.x = ecmdata.nQx[totrels++]; }

/* Get next i value.  Don't destroy Qi_minus6 in case it is an nQx value. */

			ell_add_xz (&ecmdata, &curr->Qi, Q6, &curr->Qi_minus6, &Qnext_i);	// Next_Qi = Qi + Q6 (diff Q{i-6})

/* We no longer need Qi_minus6.  Normalize it or free it */

			normalize_or_free (&curr->Qi_minus6, curr->Qi_minus6_is_relp);

/* Shuffle i values along */

			curr->Qi_minus6 = curr->Qi;
			curr->Qi_minus6_is_relp = curr->Qi_is_relp;
			curr->Qi = Qnext_i;
			curr->Qi_is_relp = next_i_is_relp;

/* Compute Q^D which we will need in mQ_init.  Do this with a single ell_add_xz call when we reach two values that are 2 or 4 apart that add to D. */

			if ((i+6) + (i+i_gap) == ecmdata.D) {
				struct xz tmp;
				struct xz *Qgap = Q2;
				ASSERTG (Qgap == &ecmdata.QD);
				if (!alloc_xz (&ecmdata, &tmp)) goto lowmem;
				if (6 - i_gap == 4) ell_dbl_xz_scr (&ecmdata, Q2, Qgap, &tmp);		  // Qgap = Q4 = 2 * Q2, Q2 no longer needed
				ell_add_xz_scr (&ecmdata, &Q1mod6.Qi, &Q5mod6.Qi, Qgap, &ecmdata.QD, &tmp); // QD = i+6 + i+i_gap (diff Qgap), Qgap no longer needed
				free_xz (&ecmdata, &tmp);
				have_QD = TRUE;
			}

/* Break out of loop when we have all our nQx values less than D */

			if (have_QD && totrels == ecmdata.numrels) break;

/* Check for errors, user abort, restart for mem changed, etc. */

			if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}

/* Free memory used in computing nQx values */

		normalize_or_free (&Q1mod6.Qi, Q1mod6.Qi_is_relp);
		normalize_or_free (&Q1mod6.Qi_minus6, Q1mod6.Qi_minus6_is_relp);
		normalize_or_free (&Q5mod6.Qi, Q5mod6.Qi_is_relp);
		normalize_or_free (&Q5mod6.Qi_minus6, Q5mod6.Qi_minus6_is_relp);
		free_xz (&ecmdata, Q6);
	}
	#undef Qmodhi_init
	#undef Qmodlo_init
	#undef normalize_or_free

/* Add Q^D computed above to the D-multiples map */

	{
		auto it_Dmult = Dmultiple_map.find (1);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({1, {0, 1}}).first;
		it_Dmult->second.set_val_buffers (&ecmdata.gwdata, ecmdata.QD.x, ecmdata.QD.z, TRUE);
	}

/* Allocate scratch space for computing mQx values that will be written to G(X). */

//GW: formula might be different for other 1%7 vaules  (and we don't know why + 5 does not work!
//GW: round up to multiple of num_threads when we implement multithreading?
	ecmdata.polyGaux_size = divide_rounding_up (ecmdata.poly_size - 1, 7) + 6;	// Assumes 3.57MULT normalization
	ecmdata.polyGaux = gwalloc_array (&ecmdata.gwdata, ecmdata.polyGaux_size);
	if (ecmdata.polyGaux == NULL) goto lowmem;

/* Init for computing Q^(multiples of D).  This adds the needed starting multiples of D to the D-multiples map. */

	stop_reason = mQ_preinit_array (&ecmdata, Dmultiple_map);
	if (stop_reason) goto possible_lowmem;

// Calculate all the needed Q^(multiples-of-D)

	process_Dmult_map (Dmultiple_map);
	for (auto this_Dmult = Dmultiple_map.begin(); this_Dmult != Dmultiple_map.end(); ++this_Dmult) {
		// Ignore the already computed Q^D
		if (this_Dmult->first == 1) continue;
		// If necessary, allocate buffers for this Q^multiple-of-D
		if (this_Dmult->second.val.x == NULL) {
			struct xz tmp;
			if (!alloc_xz (&ecmdata, &tmp)) goto lowmem;
			this_Dmult->second.set_val_buffers (&ecmdata.gwdata, tmp.x, tmp.z, FALSE);
		}
		// Compute using ell_dbl if diff is zero
		auto it_base = Dmultiple_map.find (this_Dmult->second.base);
		if (this_Dmult->second.diff == 0) {
			struct xz tmp;
			if (!alloc_xz (&ecmdata, &tmp)) goto lowmem;
			ell_dbl_xz_scr (&ecmdata, &it_base->second.val, &this_Dmult->second.val, &tmp);
			free_xz (&ecmdata, &tmp);
		}
		// Compute using ell_add if diff is non-zero
		else {
			auto it_addin = Dmultiple_map.find (this_Dmult->second.addin);
			auto it_diff = Dmultiple_map.find (this_Dmult->second.diff);
			ell_add_xz (&ecmdata, &it_base->second.val, &it_addin->second.val, &it_diff->second.val, &this_Dmult->second.val);
			// Free addin, diff if no longer needed
			it_addin->second.free_if_Dmult_last_used_by (this_Dmult->first);
			it_diff->second.free_if_Dmult_last_used_by (this_Dmult->first);
		}
		// Free base if no longer needed
		it_base->second.free_if_Dmult_last_used_by (this_Dmult->first);
		// Check for errors, user abort, restart for mem changed, etc. */
		if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;
		stop_reason = stopCheck (thread_num);
		if (stop_reason) goto possible_lowmem;
	}
	Dmultiple_map.clear ();

/* For subsequent polyGs, mQ_preinit wants the QD^(polyGaux_size/2) value normalized.  Do so now that we've computed the value. */

	if (ecmdata.QD_Eover2.x != NULL) {
		stop_reason = add_to_normalize_pool_ro (&ecmdata, ecmdata.QD_Eover2.x, ecmdata.QD_Eover2.z);
		if (stop_reason) goto possible_lowmem;
		ecmdata.QD_Eover2.z = NULL;
	}

/* There will be no more ell_dbl calls, so free Ad4 */

	gwfree (&ecmdata.gwdata, ecmdata.Ad4);
	ecmdata.Ad4 = NULL;
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
end_timer (timers, 0);
sprintf (buf, "nQx complete.  Time: ");
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf); gw_clear_maxerr (&ecmdata.gwdata);
start_timer_from_zero (timers, 0);
}

/* We're about to reach peak memory usage, free any gwnum internal memory */

//GW: ???
	gwfree_internal_memory (&ecmdata.gwdata);

/* Init code that computes Q^m, where m is the first D section we are working on.  This normalizes the normalize_pool. */

	stop_reason = mQ_init_array (&ecmdata);
	if (stop_reason) goto possible_lowmem;
	if (ecmdata.factor != NULL) goto bingo;

/* We now have a normalized Qx value that we can write to a save file */

//GW: Is nQx[0] FFTed?  Can we convert to binary prior to nQx[0] being FFTed?
	if (ecmdata.Qz_binary != NULL) {
		ASSERTG (ecmdata.Qx_binary != NULL);
		gwtogiant (&ecmdata.gwdata, ecmdata.nQx[0], ecmdata.Qx_binary);
		free (ecmdata.Qz_binary), ecmdata.Qz_binary = NULL;
	}

/* Last chance to create a save file that doesn't lock us into a particular stage 2 implementation.  Check for user requesting a stop. */

	stop_reason = stopCheck (thread_num);
	if (stop_reason) goto possible_lowmem;

//	bool use_polymult_multithreading;		// TRUE if we should use polymult multithreading instead of gwnum multithreading
//GW: unused
//	use_polymult_multithreading = ecmdata.polydata.num_threads > 1 && ecmdata.poly_size > ecmdata.polydata.num_threads &&
//				      (int) gwfftlen (&ecmdata.gwdata) <= IniGetInt (INI_FILE, "PolyThreadingFFTlength", 256) * 1024;
//	if (use_polymult_multithreading) {
//		pm1data.helper_count = pm1data.polydata.num_threads;
//		pm1data.polydata.helper_callback = &pm1_helper;
//		pm1data.polydata.helper_callback_data = &pm1data;
//	} else {
//		pm1data.helper_count = 1;
//	}
//	ecmdata.helper_count = ecmdata.polydata.num_threads;

// Open file for saving up to two Ftree rows

	int	fd;
	if (ecmdata.Ftree_polys_in_mem < 2) {
		fd = _open (ftree_filename, _O_CREAT | _O_TRUNC | _O_BINARY | _O_RDWR, CREATE_FILE_ACCESS);
//GW				if (fd < 0) goto ???  switch to memory with smaller poly??? 
	}

// Multiply small monic polys into one big monic poly.  This is done mostly in-place in polyF, but initial data comes from nQx.

	ecmdata.polyF = gwalloc_array (&ecmdata.gwdata, ecmdata.poly_size);
	if (ecmdata.polyF == NULL) goto lowmem;

	// Increase safety margin for proper polymult operation
	gwset_polymult_safety_margin (&ecmdata.gwdata, polymult_safety_margin (ecmdata.poly_size, ecmdata.poly_size));

	// Work our way up the poly F tree combining size 1 polys, then size 2, then size 4, until there is only one big poly
	// To handle non-power-of-2 poly sizes, we need to uniformly space "odd-sized" polys over 2^log2_num_polys
	int num_tree_levels;
	num_tree_levels = (int) ceil(log2((double)ecmdata.poly_size));	// Number of Ftree levels with two or more polys
	ASSERTG (num_tree_levels >= 4);
	for (int tree_level = 0; tree_level < num_tree_levels; tree_level++) {
		int log2_num_polys = num_tree_levels - tree_level;

		// The default scenario is reading data from nQx or polyF.  The polyF pointer may change if this Ftree row is saved to memory.
		gwarray	source_poly = (tree_level == 0 ? ecmdata.nQx : ecmdata.polyF);

		// Two rows from F tree must be saved either to disk or memory for later rebuilding of the full Ftree
		// If excess memory is available, we can save even more Ftree levels to memory.
		if (tree_level == 0 || log2_num_polys == 3 ||
		    (ecmdata.Ftree_polys_in_mem >= 3 && tree_level >= num_tree_levels - ecmdata.Ftree_polys_in_mem + 2) ||
		    (ecmdata.Ftree_polys_in_mem >= 5 && tree_level >= num_tree_levels - ecmdata.Ftree_polys_in_mem + 1)) {

			// Save an Ftree "row" to disk
			if (ecmdata.Ftree_polys_in_mem == 0 || (ecmdata.Ftree_polys_in_mem == 1 && tree_level == 0)) {
				int array_size = (int) ceil (ecmdata.gwdata.bit_length);
				array_size = divide_rounding_up (array_size, 32);
				uint32_t *array = (uint32_t *) malloc (array_size * sizeof (uint32_t));
				if (array == NULL) goto lowmem;
				gwnum tmp = gwalloc (&ecmdata.gwdata);
				if (tmp == NULL) { free (array); goto lowmem; }
				for (int i = 0; i < ecmdata.poly_size; i++) {
					long	size;
//GW: if poly is already FFTed then we are wasting some CPU cylces here.  We could alloc a gwnum and unFFT into that before gwtobinary (wasteful if gwnum is not ffted)
//	hmmm,  F or nQx may be FFTed - that would be unfortunate.  Need option to save it raw/binary? or maybe even preprocessed (slicing is an issue)?
					gwunfft (&ecmdata.gwdata, source_poly[i], tmp);
					size = gwtobinary (&ecmdata.gwdata, tmp, array, array_size);
//GW: if (size <0) ???
					memset (array + size, 0, (array_size - size) * sizeof (uint32_t));
					_write (fd, array, array_size * sizeof (uint32_t));
				}
				free (array);
				gwfree (&ecmdata.gwdata, tmp);
//explore whether multithreaded version would have cloned gwdata memory allocation issues in gwtobinary						
//   must deal with i/o errors
//gw:   unlink on exit/error
			}

			// Save an Ftree row to memory
			else {
				if (tree_level == 0) {		// Save row at nQx
					ecmdata.polyFtree[0] = ecmdata.nQx;
				} else {			// Save row at polyF
					ecmdata.polyFtree[tree_level] = ecmdata.polyF;
					ecmdata.polyF = gwalloc_array (&ecmdata.gwdata, ecmdata.poly_size);
					if (ecmdata.polyF == NULL) goto lowmem;
				}
			}
		}

start_timer_from_zero (timers, 0);
		// Special case tree level 0.  Combine two, three, or four length-1 polynomials using special (faster) len1 polymult routines.
		if (tree_level == 0) {

			// Set up ecm_helper arguments
			ecmdata.polydata.helper_callback = &ecm_helper;
			ecmdata.polydata.helper_callback_data = &ecmdata;
			ecmdata.helper_work = ECM_POLYF_LEVEL_ZERO;
			ecmdata.poly1 = source_poly;			// Input poly

			// Multithreaded combining of up to four length-1 polys
			polymult_launch_helpers (&ecmdata.polydata);	// Launch helper threads

			// Skip a tree level!
			tree_level++;
		}

		// Work on pairs of polynomials.  Combine them using polymult.
		else {
			uint64_t poly_base_calculator = 0;	// Accumulator that lets us calculate floor (i*ecmdata.poly_size/num_polys)
			int	poly_base = 0;			// PolyF array index
			void	*plan1 = NULL;
			void	*plan2 = NULL;
			while (poly_base < ecmdata.poly_size) {
				int	poly1_base, poly1_size, poly2_base, poly2_size;

				// Compute location and size of the two polys
				poly1_base = poly_base;
				poly_base_calculator += ecmdata.poly_size;
				poly2_base = (int) (poly_base_calculator >> log2_num_polys);
				poly_base_calculator += ecmdata.poly_size;
				poly_base = (int) (poly_base_calculator >> log2_num_polys);
				poly1_size = poly2_base - poly1_base;
				poly2_size = poly_base - poly2_base;

				// Combine the two polys
				void **plan_addr = (poly1_size == poly2_size) ? &plan1 : &plan2;
				int options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC | POLYMULT_NO_UNFFT | POLYMULT_SAVE_PLAN;
				if (*plan_addr != NULL) ecmdata.polydata.plan = *plan_addr, options |= POLYMULT_USE_PLAN;
				if (poly1_size <= poly2_size)
					polymult (&ecmdata.polydata, &source_poly[poly1_base], poly1_size, &source_poly[poly2_base], poly2_size,
						  &ecmdata.polyF[poly1_base], poly1_size + poly2_size, options);
				else
					polymult (&ecmdata.polydata, &source_poly[poly2_base], poly2_size, &source_poly[poly1_base], poly1_size,
						  &ecmdata.polyF[poly1_base], poly2_size + poly1_size, options);
				*plan_addr = ecmdata.polydata.plan;
			}

			// Free saved plans
			free (plan1);
			free (plan2);

			// Multithreaded unfft poly coefficients
//GW: BUG - if saving this polyF to disk, only unfft result (or better yet unfft / save to disk / fft)
			poly_unfft_fft_coefficients (&ecmdata.polydata, ecmdata.polyF, ecmdata.poly_size);
		}
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
end_timer (timers, 0);
double poly_size = ecmdata.poly_size / pow (2.0, log2_num_polys);
if (poly_size < 2.0) sprintf (buf, "Round off: %.10g, avg poly_size: %g, EB: %g, SM: %g, Time:", gw_get_maxerr (&ecmdata.gwdata), poly_size, ecmdata.gwdata.EXTRA_BITS, ecmdata.gwdata.safety_margin);
else sprintf (buf, "Round off: %.10g, avg poly_size: %g, Time: ", gw_get_maxerr (&ecmdata.gwdata), poly_size);
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf); gw_clear_maxerr (&ecmdata.gwdata);
start_timer_from_zero (timers, 0);
}
		stop_reason = stopCheck (thread_num);
		if (stop_reason) goto possible_lowmem;
	}

// Free nQx array (if it was not saved in polyFtree)

	if (ecmdata.Ftree_polys_in_mem < 2) {
		gwfree_array (&ecmdata.gwdata, ecmdata.nQx); ecmdata.nQx = NULL;
	}

// Use Newton's method to compute 1/F(X), starting with an initial estimate of 1 - first_non_monic_term_of_F(X).

	// Allocate reciprocal of F(X) poly in one big array.
	ecmdata.polyR = gwalloc_array (&ecmdata.gwdata, ecmdata.poly_size);
	if (ecmdata.polyR == NULL) goto lowmem;

	// F_recip always has coefficients that need negating!  First term for polyR is copied from polyF.
	gwcopy (&ecmdata.gwdata, ecmdata.polyF[ecmdata.poly_size-1], ecmdata.polyR[ecmdata.poly_size-1]);

	// We don't repeat recip-like calculations, so temporarily disable caching new polymult sin/cos data.
	ecmdata.polydata.twiddle_cache_additions_disabled = TRUE;

	// Compute 1/F(X) roughly doubling the number of terms each iteration
	for (uint64_t terms = 2; terms < ecmdata.poly_size; ) {	// Terms is the number of coefficients computed in F_recip (including the monic one term)
		// Determine if we are doubling the number of terms or doubling minus one.
		uint64_t terms_to_compute;
		for (terms_to_compute = ecmdata.poly_size + 1; terms_to_compute > 2 * terms; terms_to_compute = (terms_to_compute + 1) / 2);
		terms_to_compute -= terms;
		// Let g0 = current_recip.  Compute F(x)*g0.  Let e = F(x)*g0 - 1 (simply remove "monic flag" from the result)
//GW: It isn't legal to index into a preprocessed poly, so polyF cannot be preprocessed.  This could be overcome for a minor gain.
		polymult2 (&ecmdata.polydata, &ecmdata.polyF[ecmdata.poly_size-(terms+terms_to_compute-1)], terms+terms_to_compute-1,
			   &ecmdata.polyR[ecmdata.poly_size-(terms-1)], terms-1,
			   ecmdata.polyR, terms_to_compute,		// Only return terms_to_compute
			   NULL, 0, terms-1,				// No FMA, no circular, don't return least significant terms-1 coefficients
			   POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_MULMID | POLYMULT_NEXTFFT);
		// Compute new_recip = g0 - g0*e
		polymult (&ecmdata.polydata, &ecmdata.polyR[ecmdata.poly_size-(terms-1)], terms-1,
			  ecmdata.polyR, terms_to_compute,
			  &ecmdata.polyR[ecmdata.poly_size-(terms-1)-terms_to_compute], terms_to_compute,	// Only return hi-order terms_to_compute
			  POLYMULT_INVEC1_MONIC_NEGATE | POLYMULT_MULHI | POLYMULT_NEXTFFT);
		// Update count of terms computed
		terms = terms + terms_to_compute;
	}
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
end_timer (timers, 0);
sprintf (buf, "PolyR built.  Time: ");
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf); gw_clear_maxerr (&ecmdata.gwdata);
start_timer_from_zero (timers, 0);
}

	// Resume caching new polymult sin/cos data
	ecmdata.polydata.twiddle_cache_additions_disabled = FALSE;

// Compress and pre-transpose F(X) -- will reduce mem usage by maybe 1.6% without compression plus another 12.5% with compression.
//GW: Compress F now??? or find a way to allow indexing into compressed F above??
//GW: We could allow this by having polymult planner auto detect too many coefficients passed in for a MULHI or MULLO operation (and reduce poly fft size accordingly)
//GW: or we could pass in a flag INVEC1_PREPROCESSED

//GW: if num_polyGs == 1, dont preprocess
	if (IniGetInt (INI_FILE, "ECMPolyCompress", 1)) {
		int options = POLYMULT_INVEC1_MONIC;
		if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == -1) options |= POLYMULT_PRE_FFT;			// Hidden option (uses more memory)
		if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == -2) options |= POLYMULT_PRE_FFT | POLYMULT_PRE_COMPRESS;// Hidden option (uses more memory)
		if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == 2) options |= POLYMULT_PRE_COMPRESS;
		gwarray tmp = polymult_preprocess (&ecmdata.polydata, ecmdata.polyF, ecmdata.poly_size, ecmdata.poly_size, ecmdata.poly_size*2, options);
		gwfree_array (&ecmdata.gwdata, ecmdata.polyF);
		ecmdata.polyF = tmp;
	}

// Compress and pre-transpose reciprocal of F(X) -- will reduce mem usage by maybe 1.6% without compression plus another 12.5% with compression.

//GW: if num_polyGs == 1, dont preprocess
	if (IniGetInt (INI_FILE, "ECMPolyCompress", 1)) {
		//GW:  On first outer_loop polyR is multiplied with a MONIC polyH -- is that a problem??  It turns out no.  We should document this in polmult.h
		//GW:  It is better to NOT include the monic designation on poly2 as that includes the invec1's monic one in the FFT data.
		int options = POLYMULT_INVEC1_MONIC_NEGATE;
		if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == -1) options |= POLYMULT_PRE_FFT;			// Hidden option (uses more memory)
		if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == -2) options |= POLYMULT_PRE_FFT | POLYMULT_PRE_COMPRESS;// Hidden option (uses more memory)
		if (IniGetInt (INI_FILE, "ECMPolyCompress", 1) == 2) options |= POLYMULT_PRE_COMPRESS;
		gwarray tmp = polymult_preprocess (&ecmdata.polydata, ecmdata.polyR, ecmdata.poly_size, ecmdata.poly_size, ecmdata.poly_size*2, options);
		gwfree_array (&ecmdata.gwdata, ecmdata.polyR);
		ecmdata.polyR = tmp;
	}
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
end_timer (timers, 0);
sprintf (buf, "Poly compress.  Time: ");
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf); gw_clear_maxerr (&ecmdata.gwdata);
start_timer_from_zero (timers, 0);
}

/* We're nearing peak memory usage, free any cached gwnums */

//GW: DANGER, DANGER - freeing internal memory can delete gwnums (GW_FFT1, GW_RANDOM, etc.) that clones are pointing to!  gwnum handles this!
	gwfree_internal_memory (&ecmdata.gwdata);
	mallocFreeForOS ();

// Allocate space for G(X) and H(X) polys in one big array

//GW: if num_polyGs == 1, dont need polyH (or polyH == polyG)
	ecmdata.polyGH = gwalloc_array (&ecmdata.gwdata, 2*ecmdata.poly_size);
	if (ecmdata.polyGH == NULL) goto lowmem;
	gwnum *polyG, *polyH;
	polyG = &ecmdata.polyGH[0];
	polyH = &ecmdata.polyGH[ecmdata.poly_size];

/* Initialization of stage 2 complete */

	sprintf (buf, "%.*f%% of %s ECM curve %" PRIu32 " stage 2 (using %uMB)",
		 (int) PRECISION, trunc_percent (w->pct_complete), ecmdata.N_short_string_rep, ecmdata.curve, memused);
	title (thread_num, buf);

	end_timer (timers, 1);
	sprintf (buf, "Stage 2 init complete. %" PRIu64 " transforms, %lu modular inverses. Time: ", gw_get_fft_count (&ecmdata.gwdata), ecmdata.modinv_count);
	print_timer (timers, 1, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&ecmdata.gwdata);
	ecmdata.modinv_count = 0;

// Calculate the percent completed by previous stage 2 efforts

	base_pct_complete = (double) (ecmdata.B2_start - ecmdata.last_relocatable) /
			    (double) (ecmdata.B2_start + ecmdata.numDsections * ecmdata.D - ecmdata.last_relocatable);

// Loop over all the D-sections between B1 and B2

	ecmdata.state = ECM_STATE_STAGE2;
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);
	last_output = last_output_t = 0;
	delayed_stop_reason = 0;
	for (int outer_loop_counter = 1; ; outer_loop_counter++) {

// Compute G(X).  mQ_next calculations require some temporary memory to do normalizations, the polyGaux array is used for this.  PolyGaux is 1/7th the size
// of polyG.  Subsequent mQ_next_array calls use the values returned by the previous mQ_next_array call to improve speed.  Alas, polyG data is overwritten.
// The memory required to save the entire polyG array is better used to choose larger poly sizes.  However, we can save 1/7th of polyG in polyGaux to get some
// of the improved speeds.  To avoid unnecessary gwcopys, 6/7 of polyG is filled in and the caller must get the last 1/7th from polyGaux.

//GW: Figure out a way to multithread this!  Each thread with its own normalize pool (since modinv) is not multithreaded?
		stop_reason = mQ_next_array (&ecmdata, polyG);
		if (stop_reason) {
			// If we've run out of memory, or we haven't made much progress in stage 2, then save stage 1 state and exit
			if (stop_reason == STOP_OUT_OF_MEM || outer_loop_counter <= 2) {
				ecm_save (&ecmdata);
				goto exit;
			}
			if (delayed_stop_reason == 0) OutputStr (thread_num, "Stopping.  Please be patient.\n");
			delayed_stop_reason = stop_reason;
			goto polydown;
		}
		if (ecmdata.factor != NULL) goto bingo;

// Multiply small monic polys of G(X) into one big monic poly.
// To handle non-power-of-2 poly sizes, we uniformly space "odd-sized" polys over 2^log2_num_polys.

		// Special case tree level 0.  Combine two, three, or four length-1 polynomials using special (faster) len1 polymult routines.

		// Set up ecm_helper arguments
		ecmdata.polydata.helper_callback = &ecm_helper;
		ecmdata.polydata.helper_callback_data = &ecmdata;
		ecmdata.helper_work = ECM_POLYG_LEVEL_ZERO;

		// Multithreaded combining of up to four length-1 polys
		polymult_launch_helpers (&ecmdata.polydata);	// Launch helper threads

		// Do remaining tree levels (we've done two) until there is just one big poly
		for (int log2_num_polys = num_tree_levels - 2; log2_num_polys >= 1; log2_num_polys--) {
			uint64_t poly_base_calculator = 0;	// Accumulator that lets us calculate floor (i*ecmdata.poly_size/num_polys)
			int	poly_base = 0;			// PolyG array index
			void	*plan1 = NULL;
			void	*plan2 = NULL;

//GW: keep poly_sizes within one for P-1 too,  also use special len1 for P-1?

			// Work on pairs of polynomials.  We combine them in-place using polymult.
			while (poly_base < ecmdata.poly_size) {
				int	poly1_base, poly1_size, poly2_base, poly2_size;

				// Compute location and size of the two polys
				poly1_base = poly_base;
				poly_base_calculator += ecmdata.poly_size;
				poly2_base = (int) (poly_base_calculator >> log2_num_polys);
				poly_base_calculator += ecmdata.poly_size;
				poly_base = (int) (poly_base_calculator >> log2_num_polys);
				poly1_size = poly2_base - poly1_base;
				poly2_size = poly_base - poly2_base;

				// The first outer loop iteration we write the final result to H(X) rather than G(X)
				if (outer_loop_counter == 1 && log2_num_polys == 1) {
					int options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC | POLYMULT_NO_UNFFT;
					polymult (&ecmdata.polydata, &polyG[poly1_base], poly1_size, &polyG[poly2_base], poly2_size,
						  polyH, poly1_size + poly2_size, options);
					break;
				}
				// Combine the two polys
				void **plan_addr = (poly1_size == poly2_size) ? &plan1 : &plan2;
				int options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC | POLYMULT_NO_UNFFT | POLYMULT_SAVE_PLAN;
				if (*plan_addr != NULL) ecmdata.polydata.plan = *plan_addr, options |= POLYMULT_USE_PLAN;
				if (poly1_size <= poly2_size)
					polymult (&ecmdata.polydata, &polyG[poly1_base], poly1_size, &polyG[poly2_base], poly2_size,
						  &polyG[poly1_base], poly1_size + poly2_size, options);
				else
					polymult (&ecmdata.polydata, &polyG[poly2_base], poly2_size, &polyG[poly1_base], poly1_size,
						  &polyG[poly1_base], poly2_size + poly1_size, options);
				*plan_addr = ecmdata.polydata.plan;
			}

			// Free saved plans
			free (plan1);
			free (plan2);

			// Multithreaded unfft poly coefficients
			poly_unfft_fft_coefficients (&ecmdata.polydata, outer_loop_counter > 1 || log2_num_polys > 1 ? polyG : polyH, ecmdata.poly_size);

			// Periodic checks for user stop request
			stop_reason = stopCheck (thread_num);
			if (stop_reason) {
				// If we've run out of memory, or we haven't made much progress in stage 2, then save stage 1 state and exit
				if (stop_reason == STOP_OUT_OF_MEM || outer_loop_counter <= 2) {
					ecm_save (&ecmdata);
					goto exit;
				}
				if (delayed_stop_reason == 0) OutputStr (thread_num, "Stopping.  Please be patient.\n");
				delayed_stop_reason = stop_reason;
				goto polydown;
			}
		}
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
strcpy (buf, "PolyG built.  Time: ");
end_timer (timers, 0);
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf);
start_timer_from_zero (timers, 0);
}

// First loop iteration do nothing.  On subsequent loop iterations, H(X) = G(X) * H(X) mod F(X).  The first time we do this H(X) is monic.
// It might appear that doing just one loop could be advantageous -- we wouldn't need memory for an H(X) poly.  However, increasing the D
// value has both extra costs (polyF, polyR, scaled remainder costs all go up) and savings (G(X) and H(X) costs go down due to fewer D values).
// The happy balance seems to be around four iterations of the outer loop.

		if (outer_loop_counter > 1) {
			// Compute tmp = G(X) * H(X).  We can store tmp in polyGH.
 			polymult (&ecmdata.polydata, polyG, ecmdata.poly_size, polyH, ecmdata.poly_size, ecmdata.polyGH, 2*ecmdata.poly_size,
				  POLYMULT_INVEC1_MONIC | (outer_loop_counter == 2 ? POLYMULT_INVEC2_MONIC : 0) | POLYMULT_NEXTFFT);

			// Compute quot = tmp * 1/F(X).  The upper half of GH happens to be polyH.  Store the quotient in upper half of GH.
			polymult (&ecmdata.polydata, polyH, ecmdata.poly_size, ecmdata.polyR, ecmdata.poly_size, polyH, ecmdata.poly_size,
				  (outer_loop_counter == 2 ? POLYMULT_INVEC1_MONIC : 0) | POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_MULHI | POLYMULT_NEXTFFT);

			// Compute H(X) = tmp - quot * F(X).  Tmp is in polyG, quotient is in polyH.
			polymult_fma (&ecmdata.polydata, polyH, ecmdata.poly_size, ecmdata.polyF, ecmdata.poly_size, polyH, ecmdata.poly_size, polyG,
				      POLYMULT_INVEC2_MONIC | POLYMULT_FNMADD | POLYMULT_MULLO | POLYMULT_NEXTFFT);
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
strcpy (buf, "PolyH built.  Time: ");
end_timer (timers, 0);
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf);
start_timer_from_zero (timers, 0);
}
		}

if (ecmdata.Dsection==0 && IniGetInt (INI_FILE, "PolyVerbose", 0)) {
sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&ecmdata.gwdata));
OutputStr (thread_num, buf); gw_clear_maxerr (&ecmdata.gwdata);}

/* Test for errors */

		if (gw_test_for_error (&ecmdata.gwdata) || gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) goto err;

// Keep track of the sections processed thusfar

		ecmdata.Dsection += ecmdata.poly_size;
		if (ecmdata.Dsection >= ecmdata.numDsections && ecmdata.B2_start + ecmdata.Dsection * ecmdata.D >= ecmdata.C) break;

/* Calculate stage 2 percentage. */

		w->pct_complete = base_pct_complete + (1.0 - base_pct_complete) * (double) ecmdata.Dsection / (double) ecmdata.numDsections;

/* Output the title every so often */

//GW: The * 3 is a total guess to compensate for time spent in polymult
#define ITER_FUDGE 3
		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) * ITER_FUDGE >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s ECM stage 2 (using %uMB)",
				 (int) PRECISION, trunc_percent (w->pct_complete), ecmdata.N_short_string_rep, memused);
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&ecmdata.gwdata) * ITER_FUDGE;
		}

/* Write out a message every now and then */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&ecmdata.gwdata) * ITER_FUDGE >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s stage 2 at B2=%" PRIu64 " [%.*f%%]",
				 ecmdata.N_short_string_rep, ecmdata.B2_start + ecmdata.Dsection * ecmdata.D, (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, ".  Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&ecmdata.gwdata) * ITER_FUDGE;
			first_iter_msg = FALSE;
		}

/* Periodicly write a save file */

		if (testSaveFilesFlag (thread_num)) {
			ecm_save (&ecmdata);
			saving = FALSE;
		}

/* If we are delaying stopping until we've worked down the poly tree, don't do more polyG outer loops */

		if (delayed_stop_reason) break;
	}

/* Now work on H(X) down the F(X) tree using Bernstein's scaled remainders paper */

	// Multiply H(X) by 1/F(X) to make scaled remainders
polydown:
	polymult (&ecmdata.polydata, polyH, ecmdata.poly_size, ecmdata.polyR, ecmdata.poly_size,
		  polyH, ecmdata.poly_size, POLYMULT_INVEC2_MONIC_NEGATE | POLYMULT_MULHI | POLYMULT_NEXTFFT);
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
strcpy (buf, "H(X) scaled.  Time: ");
end_timer (timers, 0);
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf);
start_timer_from_zero (timers, 0);
}

	// Work down the F(X) tree in a memory efficient way.  This involves recomputing the Ftree.

	// Free up memory.  PolyF and PolyR may have been compressed and thus their gwnums cannot be recycled, they must be freed.
	// The polyGH and polyGaux arrays have been co-mingled using gwswap.  They must be freed together.  We can't do that as polyH is still in use.
	gwfree_array (&ecmdata.gwdata, ecmdata.polyF), ecmdata.polyF = NULL;
	gwfree_array (&ecmdata.gwdata, ecmdata.polyR), ecmdata.polyR = NULL;
	// Now allocate all the gwnums we will need below and gather all our free gwnums into one big array for use below.
	uint64_t gwnums_needed;
	gwnums_needed = (num_tree_levels - 3) * divide_rounding_up (ecmdata.poly_size, 8);	// Pass 2 needs a 1/8th sized poly for each level
	if (gwnums_needed < 3 * ecmdata.poly_size) gwnums_needed = 3 * ecmdata.poly_size;	// Pass 1 needs 3 full-sized polys
//GW: Allocate enough gwnums or match the previous mem peak.  Verify we've calculated this right.
//GW: When we combine a length 0 and length 1 poly (no-op) we copy the gwnum -- we're likely overcounting the needed gwnums in pass 2
	ecmdata.polyF = gwalloc_array (&ecmdata.gwdata, gwnums_needed - (ecmdata.poly_size + ecmdata.polyGaux_size));
	if (ecmdata.polyF == NULL) goto lowmem;
	ecmdata.Ftree = (gwarray) malloc ((size_t) (gwnums_needed * sizeof (gwnum)));
	if (ecmdata.Ftree == NULL) goto lowmem;
//GW: if num_polyGs == 1, polyG and polyH are the same  -- allocate a bigger polyF and don't copy polyG?
	memcpy (ecmdata.Ftree, polyG, (size_t) (ecmdata.poly_size * sizeof (gwnum)));
	memcpy (ecmdata.Ftree + ecmdata.poly_size, ecmdata.polyGaux, (size_t) (ecmdata.polyGaux_size * sizeof (gwnum)));
	memcpy (ecmdata.Ftree + ecmdata.poly_size + ecmdata.polyGaux_size, ecmdata.polyF,
		(size_t) ((gwnums_needed - (ecmdata.poly_size + ecmdata.polyGaux_size)) * sizeof (gwnum)));

	// Use two passes.  The first pass does only 3 levels operating on the entire H(X) poly.  The second pass does the remaining levels operating
	// on 1/8th of H(X) at a time.
	for (int pass = 1; pass <= 2; pass++) {
	    int num_levels_this_pass, log2_num_slices;
	    if (pass == 1) num_levels_this_pass = 3, log2_num_slices = 0;
	    else num_levels_this_pass = num_tree_levels - 3, log2_num_slices = 3;

	    // Operate on each slice of H(X)
	    int	slice_base = 0;			// H(X) array index
	    for (int slice = 0; slice < (1 << log2_num_slices); slice++) {
		int	slice_size, next_slice_base;

		// Calculate size of this slice
		next_slice_base = (int) (((slice + 1) * ecmdata.poly_size) >> log2_num_slices);
		slice_size = next_slice_base - slice_base;

		// Re-build this slice of Ftree working up the tree combining polys
		for (int level = 0; level < num_levels_this_pass; level++) {
			int	tree_level = level + (pass == 1 ? num_tree_levels - 3 : 0);
			gwarray polyF = ecmdata.Ftree + level * slice_size;

			// Read in polyF from a saved location
//GW: Problem? the len1-or-len2 Ftree level was not saved
			if (level == 0 || (ecmdata.Ftree_polys_in_mem >= 3 && tree_level >= num_tree_levels - ecmdata.Ftree_polys_in_mem + 2) ||
					  (ecmdata.Ftree_polys_in_mem >= 5 && tree_level >= num_tree_levels - ecmdata.Ftree_polys_in_mem + 1)) {
				// Read base level polyF from disk
				if (ecmdata.Ftree_polys_in_mem == 0 || (ecmdata.Ftree_polys_in_mem == 1 && tree_level == 0)) {
					int	array_size;
					uint32_t *array;
					int64_t	offset;
					array_size = (int) ceil (ecmdata.gwdata.bit_length);
					array_size = divide_rounding_up (array_size, 32);
					array = (uint32_t *) malloc (array_size * sizeof (uint32_t));
					if (array == NULL) goto lowmem;
					offset = (pass == 1 ? (int64_t) ecmdata.poly_size * array_size * sizeof (uint32_t) : 0);
					_lseeki64 (fd, offset + (int64_t) slice * slice_size * array_size * sizeof (uint32_t), SEEK_SET);
					for (int i = 0; i < slice_size; i++) {
						_read (fd, array, array_size * sizeof (uint32_t));
						binarytogw (&ecmdata.gwdata, array, array_size, polyF[i]);
					}
					free (array);
//explore whether multithreaded version would have cloned gwdata memory allocation issues in binarytogw						
//   must deal with i/o errors
//gw:   unlink on exit/error
				}
				// Copy polyF from memory
				else {
//GW: Why copy?  Can't we use the poly where it sits?
					memcpy (polyF, &ecmdata.polyFtree[tree_level][slice_base], slice_size * sizeof (gwnum));
				}
				continue;
			}

			// Work on pairs of polynomials from the previous level.  Combine them in-place using polymult.
			gwarray	prev_polyF = polyF - slice_size;
			int	log2_num_polys = num_levels_this_pass - (level-1) + log2_num_slices;		// Number of polyF polys in previous level
			uint64_t poly_base_calculator;	// Accumulator that lets us calculate floor (i*ecmdata.poly_size/num_polys)
			int	poly_base = 0;
			void	*plan1 = NULL;
			void	*plan2 = NULL;
			poly_base_calculator = ((uint64_t) slice << (log2_num_polys - log2_num_slices)) * ecmdata.poly_size;
			while (poly_base < slice_size) {
				int	poly1_base, poly1_size, poly2_base, poly2_size;

				// Compute location and size of the two polys
				poly1_base = poly_base;
				poly_base_calculator += ecmdata.poly_size;
				poly2_base = (int) (poly_base_calculator >> log2_num_polys) - slice_base;
				poly_base_calculator += ecmdata.poly_size;
				poly_base = (int) (poly_base_calculator >> log2_num_polys) - slice_base;
				poly1_size = poly2_base - poly1_base;
				poly2_size = poly_base - poly2_base;

				// Combining a length 0 poly and length 1 poly is real easy - move the length 1 poly to polyF
				if (poly1_size == 0 || poly2_size == 0) { gwswap (polyF[poly1_base], prev_polyF[poly1_base]); continue; }

				// Combine the two polys
				void **plan_addr = (poly1_size == poly2_size) ? &plan1 : &plan2;
				int options = POLYMULT_INVEC1_MONIC | POLYMULT_INVEC2_MONIC | POLYMULT_NO_UNFFT | POLYMULT_SAVE_PLAN;
				if (*plan_addr != NULL) ecmdata.polydata.plan = *plan_addr, options |= POLYMULT_USE_PLAN;
				if (poly1_size <= poly2_size) {
					if (poly1_size == 1) options |= POLYMULT_INVEC1_NEGATE;
					if (poly2_size == 1) options |= POLYMULT_INVEC2_NEGATE;
					polymult (&ecmdata.polydata, &prev_polyF[poly1_base], poly1_size, &prev_polyF[poly2_base], poly2_size,
						  &polyF[poly1_base], poly1_size + poly2_size, options);
				} else {
					if (poly2_size == 1) options |= POLYMULT_INVEC1_NEGATE;
					polymult (&ecmdata.polydata, &prev_polyF[poly2_base], poly2_size, &prev_polyF[poly1_base], poly1_size,
						  &polyF[poly1_base], poly2_size + poly1_size, options);
				}
				*plan_addr = ecmdata.polydata.plan;
			}
			free (plan1);
			free (plan2);
			poly_unfft_fft_coefficients (&ecmdata.polydata, polyF, slice_size);
		}
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
if (slice == 0) {
strcpy (buf, "PolyF up.  Time: ");
end_timer (timers, 0); timers[0] *= 1 << log2_num_slices;
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf);
start_timer_from_zero (timers, 0);
}
}

		// Now work down this slice of Ftree
		for (int level = num_levels_this_pass - 1; level >= 0; level--) {
			gwarray polyF = ecmdata.Ftree + level * slice_size;
			int	log2_num_polys = num_levels_this_pass - level + log2_num_slices;		// Number of polyF polys in this level
			uint64_t poly_base_calculator;	// Accumulator that lets us calculate floor (i*ecmdata.poly_size/num_polys)
			int	poly_base = 0;
			void	*plan1 = NULL;
			void	*plan2 = NULL;
			void	*plan3 = NULL;
			poly_base_calculator = ((uint64_t) slice << (log2_num_polys - log2_num_slices)) * ecmdata.poly_size;
			while (poly_base < slice_size) {
				int	poly1_base, poly1_size, poly2_base, poly2_size;

				// Compute location and size of the two polys
				poly1_base = poly_base;
				poly_base_calculator += ecmdata.poly_size;
				poly2_base = (int) (poly_base_calculator >> log2_num_polys) - slice_base;
				poly_base_calculator += ecmdata.poly_size;
				poly_base = (int) (poly_base_calculator >> log2_num_polys) - slice_base;
				poly1_size = poly2_base - poly1_base;
				poly2_size = poly_base - poly2_base;

				// Splitting a length 1 poly into a length 0 poly and length 1 poly is real easy - do nothing to H(X)
				if (poly1_size == 0 || poly2_size == 0) continue;

				// Left half of pair times saved right F(X)
				polymult_arg a[2];		// Description of 2 polymult_several arguments
				a[0].invec2 = &polyF[poly2_base];		// Second input poly
				a[0].invec2_size = poly2_size;			// Size of the second input polynomial
				a[0].outvec = &polyH[slice_base+poly1_base];	// Output poly
				a[0].outvec_size = poly1_size;			// Size of the output polynomial
				a[0].fmavec = NULL;
				a[0].circular_size = poly1_size + poly2_size;	// Poly result modulo (X^circular_size - 1)
				a[0].first_mulmid = 0;
				a[0].options = (poly2_size == 1 ? POLYMULT_INVEC2_NEGATE : 0) + POLYMULT_INVEC2_MONIC | POLYMULT_CIRCULAR | POLYMULT_MULHI;

				// Right half of pair times saved left F(X)
				a[1].invec2 = &polyF[poly1_base];		// Second input poly
				a[1].invec2_size = poly1_size;			// Size of the second input polynomial
				a[1].outvec = &polyH[slice_base+poly2_base];	// Output poly
				a[1].outvec_size = poly2_size;			// Size of the output polynomial
				a[1].fmavec = NULL;
				a[1].circular_size = poly1_size + poly2_size;	// Poly result modulo (X^circular_size - 1)
				a[1].first_mulmid = 0;
				a[1].options = (poly1_size == 1 ? POLYMULT_INVEC2_NEGATE : 0) + POLYMULT_INVEC2_MONIC | POLYMULT_CIRCULAR | POLYMULT_MULHI;

				void **plan_addr = (poly1_size == poly2_size) ? &plan1 : (poly1_size < poly2_size) ? &plan2 : &plan3;
				int options = POLYMULT_NO_UNFFT | POLYMULT_SAVE_PLAN;
				if (*plan_addr != NULL) ecmdata.polydata.plan = *plan_addr, options |= POLYMULT_USE_PLAN;
				polymult_several (&ecmdata.polydata, &polyH[slice_base+poly1_base], poly1_size + poly2_size, a, 2, options);
				*plan_addr = ecmdata.polydata.plan;
			}
			// Free plans
			free (plan1);
			free (plan2);
			free (plan3);
			// Don't unfft the last polyH row.  Do that in ecm_helper when multiplying the polyH coefficients together.
			if (pass == 1 || level > 0) poly_unfft_fft_coefficients (&ecmdata.polydata, &polyH[slice_base], slice_size);
		}
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
if (slice == 0) {
strcpy (buf, "PolyF down.  Time: ");
end_timer (timers, 0); timers[0] *= 1 << log2_num_slices;
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf);
start_timer_from_zero (timers, 0);
}
}
		slice_base = next_slice_base;
	    }
	}

//GW: Make sure error paths close fd, free Ftree
	if (ecmdata.Ftree_polys_in_mem < 2) {
		_chsize_s (fd, 0);		// On a google drive, deleting a big file does not free disk space.  So truncate it before deleting.
		_close (fd);
		_unlink (ftree_filename);
	}
	free (ecmdata.Ftree), ecmdata.Ftree = NULL;

// Multiply the coefficients in polyH together for a later GCD.  We launch single-threaded helpers to do this because the gwnum FFT size may not be
// large enough to support multi-threading.  The results are stored in the first coefficients of polyG.

start_timer_from_zero (timers, 0);
	ecmdata.polydata.helper_callback = &ecm_helper;
	ecmdata.polydata.helper_callback_data = &ecmdata;
	ecmdata.helper_work = ECM_BUILD_GCDVAL;
	memset (&ecmdata.polyGH[0], 0, ecmdata.stage2_threads * sizeof (gwnum));	// Needed only for ensuring a reproducible stage 2 result
	polymult_launch_helpers (&ecmdata.polydata);	// Launch helper threads

// Multiply together the output of each ecm_helper thread above.  Hopefully using gwnum's multi-threading.

	for (int i = 1; i < ecmdata.stage2_threads; i++) {
		if (ecmdata.polyGH[i] == NULL) continue;
		if (ecmdata.polyGH[0] == NULL) ecmdata.polyGH[0] = ecmdata.polyGH[i];
		else gwmul3 (&ecmdata.gwdata, ecmdata.polyGH[0], ecmdata.polyGH[i], ecmdata.polyGH[0],
			     (i != ecmdata.stage2_threads - 1 || ecmdata.gg_binary != NULL) ? GWMUL_STARTNEXTFFT : 0);
	}

/* Now multiply in the accumulator read from a save file */

ASSERTG (ecmdata.gg == NULL);
	if (ecmdata.gg_binary != NULL) {
//GW: Can't we find a spare gwnum here rather than a gwalloc?
		gwnum tmp = gwalloc (&ecmdata.gwdata);
		if (tmp == NULL) goto oom;
		gianttogw (&ecmdata.gwdata, ecmdata.gg_binary, tmp);
		free (ecmdata.gg_binary), ecmdata.gg_binary = NULL;
		gwmul3 (&ecmdata.gwdata, ecmdata.polyGH[0], tmp, ecmdata.polyGH[0], 0);
		gwfree (&ecmdata.gwdata, tmp);
	}
	ecmdata.gg = ecmdata.polyGH[0];
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
strcpy (buf, "gg = mul H(X).  Time: ");
end_timer (timers, 0);
print_timer (timers, 0, buf, TIMER_NL);
OutputStr (thread_num, buf);
start_timer_from_zero (timers, 0);
}

/* Set required_missing for continuation from a new C_done value.  Set C and C_done for the delayed or final save file and the Kruppa adjustment. */

	if (ecmdata.B2_start > ecmdata.C_done) ecmdata.required_missing = map_polyD_to_first_missing_prime (ecmdata.D);
	ecmdata.C_done = ecmdata.B2_start + ecmdata.Dsection * ecmdata.D;
	ecmdata.C = ecmdata.B2_start + ecmdata.numDsections * ecmdata.D;

/* If we've delayed stopping until gg is calculated, then create the save file and exit */

	if (delayed_stop_reason) {
		ecm_save (&ecmdata);
		stop_reason = delayed_stop_reason;
		goto exit;
	}

/* Cleanup and free poly gwnums before GCD.  GCD can use significant amounts of memory. */

	polymult_done (&ecmdata.polydata);
	gwfree_array (&ecmdata.gwdata, ecmdata.polyF), ecmdata.polyF = NULL;
	gwfree_array (&ecmdata.gwdata, ecmdata.polyR), ecmdata.polyR = NULL;
//GW: BUG!!!!  can't free until after the GCD
//	gwfree_array (&ecmdata.gwdata, ecmdata.polyGH), ecmdata.polyGH = NULL;
//	gwfree_array (&ecmdata.gwdata, ecmdata.polyGaux), ecmdata.polyGaux = NULL;

	mQ_term_array (&ecmdata);
	normalize_pool_term (&ecmdata);

/* Compute the new Kruppa-adjusted B2 work completed.  This is needed when curves are run with different optimal B2 values due to */
/* changing available memory.  The Primenet server expects just one B2 value representing the work done. */

stage2_done:
	{
		double total_B2 = 0.0;
		if (ecmdata.average_B2 > 0) total_B2 = (ecmdata.curve - 1) * kruppa_adjust (ecmdata.average_B2, ecmdata.B);
		total_B2 += kruppa_adjust (ecmdata.C, ecmdata.B);
		ecmdata.average_B2 = kruppa_unadjust (total_B2 / ecmdata.curve, ecmdata.B);
	}

/* Stage 2 is complete.  Free memory so that other high memory workers can begin -- assume we will start stage 1 on another curve. */

	gwfree_internal_memory (&ecmdata.gwdata);
	mallocFreeForOS ();
	ecm_stage1_memory_usage (thread_num, &ecmdata);					// Let other high memory workers resume
	end_timer (timers, 1);
	end_timer (&stage2_timer, 0);
	sprintf (buf, "Stage 2 complete. %" PRIu64 " transforms, %lu modular inverses. Total time: ", gw_get_fft_count (&ecmdata.gwdata), ecmdata.modinv_count);
	print_timer (timers, 1, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	last_output = last_output_t = ecmdata.modinv_count = 0;
	gw_clear_fft_count (&ecmdata.gwdata);

/* Adjust fudge factor for stage2 vs. stage1 runtime ratio.  We use a rolling average. */

	if (stage1_timer != 0.0 && stage2_timer != 0.0 && IniGetInt (INI_FILE, "EcmStage2AutoAdjust", 1)) {
		double correct_ratio = stage2_timer / stage1_timer;
		double current_adjustment = IniGetFloat (INI_FILE, "EcmStage2RatioAdjust", 1.0);
		double correct_adjustment = correct_ratio / ecmdata.est_stage2_stage1_ratio * current_adjustment;
		// Sanity check.  Estimation code should not be off by a factor of 3.
		if (correct_adjustment > 3.0) correct_adjustment = 3.0;
		if (correct_adjustment < 0.333) correct_adjustment = 0.333;
		// Set new adjustment for next time
		IniWriteFloat (INI_FILE, "EcmStage2RatioAdjust", (float) (0.9 * current_adjustment + 0.1 * correct_adjustment));
	}

/* Print out round off error */

	if (ERRCHK) {
		sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&ecmdata.gwdata));
		OutputStr (thread_num, buf);
		gw_clear_maxerr (&ecmdata.gwdata);
	}

/* See if we got lucky! */

restart4:
	ecmdata.state = ECM_STATE_GCD;
	sprintf (w->stage, "C%" PRIu32 "S2", ecmdata.curve);
	w->pct_complete = 1.0;
	start_timer_from_zero (timers, 0);
	stop_reason = gcd (&ecmdata.gwdata, thread_num, ecmdata.gg, ecmdata.N, &ecmdata.factor);
	if (stop_reason) {
		ecm_save (&ecmdata);
		goto exit;
	}
	end_timer (timers, 0);
	strcpy (buf, "Stage 2 GCD complete. Time: ");
	print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	if (ecmdata.factor != NULL) goto bingo;

/* Check if more curves need to be done */

more_curves:
	ecm_mini_cleanup (&ecmdata);		// Free all memory except N and gwdata
	mallocFreeForOS ();
	ecmdata.curve++;
	if (w->curves_to_do == 0 || ecmdata.curve <= w->curves_to_do) {
		// If necessary, switch back to the stage 1 FFT length
		if (gwfftlen (&ecmdata.gwdata) != ecmdata.stage1_fftlen || ecmdata.stage1_threads != ecmdata.stage2_threads) {
			char	fft_desc[200];
			gwdone (&ecmdata.gwdata);
			gwinit (&ecmdata.gwdata);
			if (IniGetInt (INI_FILE, "UseLargePages", 0)) gwset_use_large_pages (&ecmdata.gwdata);
			if (HYPERTHREAD_LL) sp_info->normal_work_hyperthreading = TRUE, gwset_will_hyperthread (&ecmdata.gwdata, 2);
			gwset_bench_cores (&ecmdata.gwdata, HW_NUM_CORES);
			gwset_bench_workers (&ecmdata.gwdata, NUM_WORKERS);
			if (ERRCHK) gwset_will_error_check (&ecmdata.gwdata);
			gwset_num_threads (&ecmdata.gwdata, ecmdata.stage1_threads);
			gwset_thread_callback (&ecmdata.gwdata, SetAuxThreadPriority);
			gwset_thread_callback_data (&ecmdata.gwdata, sp_info);
			gwset_safety_margin (&ecmdata.gwdata, IniGetFloat (INI_FILE, "ExtraSafetyMargin", 0.0));
			gwset_minimum_fftlen (&ecmdata.gwdata, w->minimum_fftlen);
			gwset_using_polymult (&ecmdata.gwdata);
			gwset_use_spin_wait (&ecmdata.gwdata, IniGetInt (INI_FILE, "SpinWait", 0));
			if (w->n) res = gwsetup (&ecmdata.gwdata, w->k, w->b, w->n, w->c);
			else res = gwsetup_general_mod_giant (&ecmdata.gwdata, ecmdata.N);
			if (res) {
				sprintf (buf, "Cannot initialize FFT code, errcode=%d\n", res);
				OutputBoth (thread_num, buf);
				stop_reason = STOP_FATAL_ERROR;
				goto exit;
			}
			gwerror_checking (&ecmdata.gwdata, ERRCHK || near_fft_limit);
			gwfft_description (&ecmdata.gwdata, fft_desc);
			sprintf (msgbuf, "Switching back to %s\n", fft_desc);
			OutputStr (thread_num, msgbuf);
		} else
			gwsetaddin (&ecmdata.gwdata, 0);	// This should not be necessary, just in case there is code still using the old gwnum interface
		goto restart0;
	}

/* If we've finished processing an unlimited number of curves from a GMP-ECM resume file, set the number of curves we actually processed */

no_more_curves:
	if (w->curves_to_do == 0) w->curves_to_do = ecmdata.curve - 1;

/* Output line to results file indicating the number of curves run */

	sprintf (buf, "%s completed %u ECM %s curve%s, B1=%" PRIu64 ",%s B2=%" PRIu64 ", Wi%d: %08lX\n",
		 ecmdata.N_short_string_rep, w->curves_to_do, ecmdata.sigma_type == 3 ? "GMP-ECM param3" : ecmdata.sigma_type == 1 ? "Montgomery" : "Edwards",
		 w->curves_to_do == 1 ? "" : "s", ecmdata.B, ecmdata.optimal_B2 ? " average" : "", ecmdata.average_B2,
		 PORT, SEC5 (w->n, ecmdata.B, ecmdata.average_B2));
	OutputStr (thread_num, buf);
	formatMsgForResultsFile (buf, w);
	writeResults (buf);

/* Format a JSON version of the result.  An example follows: */
/* {"status":"NF", "exponent":45581713, "worktype":"ECM", "b1":50000, "b2":5000000, */
/* "curves":5, "fft-length":5120, "security-code":"39AB1238", */
/* "program":{"name":"prime95", "version":"29.5", "build":"8"}, "timestamp":"2019-01-15 23:28:16", */
/* "user":"gw_2", "cpu":"work_computer", "aid":"FF00AA00FF00AA00FF00AA00FF00AA00"} */

	strcpy (JSONbuf, "{\"status\":\"NF\"");
	if (w->n) JSONaddExponent (JSONbuf, w);
	else if (strlen (ecmdata.N_full_string_rep) < 2000) sprintf (JSONbuf+strlen(JSONbuf), ", \"N\":%s", ecmdata.N_full_string_rep);
	else sprintf (JSONbuf+strlen(JSONbuf), ", \"N\":\"%s\"", ecmdata.N_short_string_rep);
	strcat (JSONbuf, ", \"worktype\":\"ECM\"");
	sprintf (JSONbuf+strlen(JSONbuf), ", \"b1\":%" PRIu64 ", \"b2\":%" PRIu64, ecmdata.B, ecmdata.average_B2);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"curves\":%u", w->curves_to_do);
	if (ecmdata.sigma_type == 3) sprintf (JSONbuf+strlen(JSONbuf), ", \"GMP-ECM-param3\":{}");
	if (ecmdata.sigma_type == 0) sprintf (JSONbuf+strlen(JSONbuf), ", \"Edwards\":{}");
	sprintf (JSONbuf+strlen(JSONbuf), ", \"fft-length\":%lu", ecmdata.stage1_fftlen);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"security-code\":\"%08lX\"", SEC5 (w->n, ecmdata.B, ecmdata.average_B2));
	JSONaddProgramTimestamp (JSONbuf);
	JSONaddExponentKnownFactors (JSONbuf, w);
	JSONaddUserComputerAID (JSONbuf, w);
	strcat (JSONbuf, "}");
	if (IniGetInt (INI_FILE, "OutputJSON", 1)) writeResultsJSON (JSONbuf);

/* Send ECM completed message to the server.  Although don't do it for puny B1 values. */

	if (!QA_IN_PROGRESS && (ecmdata.B >= 50000 || IniGetInt (INI_FILE, "SendAllFactorData", 0))) {
		struct primenetAssignmentResult pkt;
		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		strcpy (pkt.assignment_uid, w->assignment_uid);
		truncated_strcpy (pkt.message, sizeof (pkt.message), buf);
		pkt.result_type = PRIMENET_AR_ECM_NOFACTOR;
		pkt.k = w->k;
		pkt.b = w->b;
		pkt.n = w->n;
		pkt.c = w->c;
		pkt.B1 = ecmdata.B;
		pkt.B2 = ecmdata.average_B2;
		pkt.curves = w->curves_to_do;
		pkt.fftlen = ecmdata.stage1_fftlen;
		pkt.done = TRUE;
		strcpy (pkt.JSONmessage, JSONbuf);
		spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
	}

/* Delete the save file */

	unlinkSaveFiles (&ecmdata.write_save_file_state);

/* Free memory and return */

	stop_reason = STOP_WORK_UNIT_COMPLETE;
exit:	Dmultiple_map.clear ();
	relp_set_map.clear ();
	ecm_cleanup (&ecmdata);
	free (str);
	free (msg);
	return (stop_reason);

/* Low or possibly low on memory in stage 2 init, create save file, reduce memory settings, and try again */

lowmem:	stop_reason = OutOfMemory (thread_num);
possible_lowmem:
	if (ecmdata.state == ECM_STATE_MIDSTAGE) ecm_save (&ecmdata);
	if (stop_reason != STOP_OUT_OF_MEM) goto exit;
	Dmultiple_map.clear ();
	relp_set_map.clear ();
	ecm_cleanup (&ecmdata);
	OutputBoth (thread_num, "Memory allocation error.  Trying again using less memory.\n");
	ecmdata.pct_mem_to_use *= 0.8;
	goto restart;

/* We've run out of memory.  Print error message and exit. */

oom:	stop_reason = OutOfMemory (thread_num);
	goto exit;

/* Print a message, we found a factor! */

bingo:	stage = (ecmdata.state > ECM_STATE_MIDSTAGE) ? 2 : (ecmdata.state > ECM_STATE_STAGE1_INIT) ? 1 : 0;
	sprintf (buf, "ECM found a factor in curve #%" PRIu32 ", stage #%d\n", ecmdata.curve, stage);
	writeResults (buf);
	sprintf (buf, "Sigma=%" PRIu64 ", B1=%" PRIu64 ", B2=%" PRIu64 ".\n", ecmdata.sigma, ecmdata.B, ecmdata.C);
	writeResults (buf);

/* Allocate memory for the string representation of the factor and for */
/* a message.  Convert the factor to a string. */ 

	msglen = ecmdata.factor->sign * 10 + (int) strlen (ecmdata.N_full_string_rep) + 400;
	str = (char *) malloc (msglen);
	if (str == NULL) goto oom;
	msg = (char *) malloc (msglen);
	if (msg == NULL) goto oom;
	gtoc (ecmdata.factor, str, msglen);

/* Validate the factor we just found */

	if (!testFactor (&ecmdata.gwdata, w, ecmdata.factor)) {
		sprintf (msg, "ERROR: Bad factor for %s found: %s\n", ecmdata.N_short_string_rep, str);
		OutputBoth (thread_num, msg);
		OutputStr (thread_num, "Restarting ECM curve from scratch.\n");
		continueECM = TRUE;
		ecmdata.curve--;
		goto error_restart;
	}

/* Output the validated factor */

	sprintf (msg, "%s has a factor: %s (ECM curve %" PRIu32 ", B1=%" PRIu64 ", B2=%" PRIu64 ")\n",
		 ecmdata.N_full_string_rep, str, ecmdata.curve, ecmdata.B, ecmdata.C);
	OutputBoth (thread_num, msg);

/* Format a JSON version of the result.  An example follows: */
/* {"status":"F", "exponent":45581713, "worktype":"ECM", "factors":["430639100587696027847"], */
/* "b1":50000, "b2":5000000, "sigma":"123456789123456", "stage":2 */
/* "curves":5, "average-b2":5127843, "fft-length":5120, "security-code":"39AB1238", */
/* "program":{"name":"prime95", "version":"29.5", "build":"8"}, "timestamp":"2019-01-15 23:28:16", */
/* "user":"gw_2", "cpu":"work_computer", "aid":"FF00AA00FF00AA00FF00AA00FF00AA00"} */

	strcpy (JSONbuf, "{\"status\":\"F\"");
	if (w->n) JSONaddExponent (JSONbuf, w);
	else if (strlen (ecmdata.N_full_string_rep) < 2000) sprintf (JSONbuf+strlen(JSONbuf), ", \"N\":%s", ecmdata.N_full_string_rep);
	else sprintf (JSONbuf+strlen(JSONbuf), ", \"N\":\"%s\"", ecmdata.N_short_string_rep);
	strcat (JSONbuf, ", \"worktype\":\"ECM\"");
	sprintf (JSONbuf+strlen(JSONbuf), ", \"factors\":[\"%s\"]", str);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"b1\":%" PRIu64 ", \"b2\":%" PRIu64, ecmdata.B, ecmdata.C);
	if (ecmdata.sigma_type == 3) sprintf (JSONbuf+strlen(JSONbuf), ", \"GMP-ECM-param3\":{\"sigma\":%" PRIu64 "}", ecmdata.sigma);
	else if (ecmdata.sigma_type == 0) sprintf (JSONbuf+strlen(JSONbuf), ", \"Edwards\":{\"sigma\":%" PRIu64 "}", ecmdata.sigma);
	else sprintf (JSONbuf+strlen(JSONbuf), ", \"sigma\":%" PRIu64, ecmdata.sigma);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"stage\":%d", stage);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"curves\":%" PRIu32, ecmdata.curve);
	if (ecmdata.optimal_B2 && ecmdata.average_B2 != ecmdata.C) sprintf (JSONbuf+strlen(JSONbuf), ", \"average-b2\":%" PRIu64, ecmdata.average_B2);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"fft-length\":%lu", ecmdata.stage1_fftlen);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"security-code\":\"%08lX\"", SEC5 (w->n, ecmdata.B, ecmdata.C));
	JSONaddProgramTimestamp (JSONbuf);
	JSONaddExponentKnownFactors (JSONbuf, w);
	JSONaddUserComputerAID (JSONbuf, w);
	strcat (JSONbuf, "}");
	if (IniGetInt (INI_FILE, "OutputJSON", 1)) writeResultsJSON (JSONbuf);

/* See if the cofactor is prime and set flag if we will be continuing ECM */

	continueECM = IniGetInt (INI_FILE, "ContinueECM", 0);
	if (ecmdata.curve == w->curves_to_do) continueECM = FALSE;
	prpAfterEcmFactor = IniGetInt (INI_FILE, "PRPAfterECMFactor", bitlen (ecmdata.N) < 100000 && w->n && (w->k != 1.0 || w->b != 2 || w->c != -1));
	if (prpAfterEcmFactor || continueECM) divg (ecmdata.factor, ecmdata.N);
	if (prpAfterEcmFactor && isProbablePrime (&ecmdata.gwdata, ecmdata.N)) {
		OutputBoth (thread_num, "Cofactor is a probable prime!\n");
		continueECM = FALSE;
	}

/* Send assignment result to the server.  To avoid flooding the server with small factors from users needlessly redoing */
/* factoring work, make sure the factor is more than 67 bits or so. */

	if (!QA_IN_PROGRESS && (strlen (str) >= 21 || IniGetInt (INI_FILE, "SendAllFactorData", 0))) {
		struct primenetAssignmentResult pkt;
		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		strcpy (pkt.assignment_uid, w->assignment_uid);
		truncated_strcpy (pkt.message, sizeof (pkt.message), msg);
		pkt.result_type = PRIMENET_AR_ECM_FACTOR;
		pkt.k = w->k;
		pkt.b = w->b;
		pkt.n = w->n;
		pkt.c = w->c;
		truncated_strcpy (pkt.factor, sizeof (pkt.factor), str);
		pkt.B1 = ecmdata.B;
		pkt.B2 = ecmdata.average_B2;
		pkt.curves = ecmdata.curve;
		pkt.stage = stage;
		pkt.fftlen = ecmdata.stage1_fftlen;
		pkt.done = !continueECM;
		strcpy (pkt.JSONmessage, JSONbuf);
		spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
	}

/* If continuing ECM, subtract the curves we just reported from the worktodo count of curves to run. */
/* Also add the new factor to the known factors list.  Write the new data to worktodo file. */

	if (continueECM) {
		w->curves_to_do -= ecmdata.curve;
		char *new_known_factors = addNewKnownFactor (w, str);
		free (w->known_factors);
		w->known_factors = new_known_factors;
		stop_reason = updateWorkToDoLine (thread_num, w);
		if (stop_reason) goto exit;
		unlinkSaveFiles (&ecmdata.write_save_file_state);
		ecmdata.curve = 0;
		ecmdata.average_B2 = 0;
	}

/* Create a PRP work unit to immediately test the Mersenne cofactor */

	if (USE_PRIMENET && w->k == 1.0 && w->b == 2 && w->c == -1 && strlen (str) >= 21 && (int) w->n < IniGetInt (INI_FILE, "MaxAutoPRPExponent", 5000000)) {
		struct work_unit w_prp;
		generatePRPWorkUnit (w, continueECM ? NULL : str, &w_prp);
		// Add work unit before or after this work unit
		w_prp.next = w;
		stop_reason = addWorkToDoLine (thread_num, &w_prp, continueECM ? ADD_BEFORE_SPECIFIC : ADD_AFTER_SPECIFIC);
		if (stop_reason) goto exit;
		// Return to do the PRP on the cofactor before continuing with more ECM
		if (continueECM) {
			stop_reason = STOP_PREV_WORK_UNIT;
			goto exit;
		}
	}

/* Free memory */

	free (str); str = NULL;
	free (msg); msg = NULL;
	free (ecmdata.factor); ecmdata.factor = NULL;

	clear_timer (timers, 0);

/* Since we found a factor, we likely performed much fewer curves than expected.  Make sure we do not update the rolling average with this inaccurate data. */

	if (!continueECM) {
		unlinkSaveFiles (&ecmdata.write_save_file_state);
		stop_reason = STOP_WORK_UNIT_COMPLETE;
		invalidateNextRollingAverageUpdate ();
		goto exit;
	}

/* Do more curves despite finding a factor */

	goto more_curves;

/* Output an error message saying we are restarting.  Sleep five minutes before restarting from last save file. */

	// Edwards error (failed ed_check).
ed_err:	OutputBoth (thread_num, "Hardware error?  Point no longer on the Edwards curve.  Restarting from last save file.\n");
	goto error_restart;

	// GWNUM error
err:	if (gw_get_maxerr (&ecmdata.gwdata) > allowable_maxerr) {
		sprintf (buf, "Possible roundoff error (%.8g), backtracking to last save file and using larger FFT.\n", gw_get_maxerr (&ecmdata.gwdata));
		OutputStr (thread_num, buf);
		maxerr_restart_count++;
		maxerr_fftlen = gwfftlen (&ecmdata.gwdata);
	} else {
		OutputBoth (thread_num, "SUMOUT error occurred.\n");
		stop_reason = SleepFive (thread_num);
		if (stop_reason) goto exit;
	}

	// Restart
error_restart:
	Dmultiple_map.clear ();
	relp_set_map.clear ();
	ecm_cleanup (&ecmdata);
	free (str); str = NULL;
	free (msg); msg = NULL;
	goto restart;
}

/* Read a file of ECM tests to run as part of a QA process */
/* The format of this file is: */
/*	k, n, c, sigma, B1, B2_end, factor */
/* Use Advanced/Time 9991 to run the QA suite */

int ecm_QA (
	int	thread_num,
	struct PriorityInfo *sp_info)	/* SetPriority information */
{
	FILE	*fd;

/* Set the title */

	title (thread_num, "QA");

/* Open QA file */

	fd = fopen ("qa_ecm", "r");
	if (fd == NULL) {
		OutputStr (thread_num, "File named 'qa_ecm' could not be opened.\n");
		return (STOP_FILE_IO_ERROR);
	}

/* Loop until the entire file is processed */

	QA_TYPE = 0;
	for ( ; ; ) {
		struct work_unit w;
		double	k;
		unsigned long b, n, B1, B2;
		signed long c;
		char	fac_str[80];
		uint64_t sigma;
		int	stop_reason;

/* Read a line from the file */

		n = 0;
		(void) fscanf (fd, "%lf,%lu,%lu,%ld,%" PRIu64 ",%lu,%lu,%s\n", &k, &b, &n, &c, &sigma, &B1, &B2, fac_str);
		if (n == 0) break;

/* If b is 1, set QA_TYPE */

		if (b == 1) {
			QA_TYPE = c;
			continue;
		}

/* Convert the factor we expect to find into a "giant" type */

		QA_FACTOR = allocgiant ((int) strlen (fac_str));
		ctog (fac_str, QA_FACTOR);

/* Do the ECM */

		memset (&w, 0, sizeof (w));
		w.work_type = WORK_ECM;
		w.k = k;
		w.b = b;
		w.n = n;
		w.c = c;
		w.B1 = B1;
		w.B2 = B2;
		w.curves_to_do = 1;
		w.curve = sigma;
		QA_IN_PROGRESS = TRUE;
		stop_reason = ecm (0, sp_info, &w);
		QA_IN_PROGRESS = FALSE;
		free (QA_FACTOR);
		if (stop_reason != STOP_WORK_UNIT_COMPLETE) {
			fclose (fd);
			return (stop_reason);
		}
	}

/* Cleanup */

	fclose (fd);
	return (0);
}

/**************************************************************
 *	P-1 and P+1 functions
 **************************************************************/

/* Handy macros for Lucas doubling and adding */

#define luc_dbl(h,s,d)			gwsquare2 (h, s, d, GWMUL_FFT_S1 | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT)
#define luc_add(h,s1,s2,diff,d)		gwmulsub4 (h, s1, s2, diff, d, GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT)
#define luc_add_last(h,s1,s2,diff,d)	gwmulsub4 (h, s1, s2, diff, d, GWMUL_FFT_S12)

/**************************************************************
 *	P-1 Functions
 **************************************************************/

/* Data maintained during P-1 process */

#define PM1_STATE_STAGE0	0	/* In stage 1, computing 3^exp using a precomputed mpz exp */
#define PM1_STATE_STAGE1	1	/* In stage 1, processing larger primes */
#define PM1_STATE_MIDSTAGE	2	/* Between stage 1 and stage 2 */
#define PM1_STATE_STAGE2	3	/* In middle of stage 2 (processing a pairmap) */
#define PM1_STATE_GCD		4	/* Stage 2 GCD */
#define PM1_STATE_DONE		5	/* P-1 job complete */

#define PM1_STAGE2_PAIRING	0	/* Old fashioned prime pairing stage 2 */
#define PM1_STAGE2_POLYMULT	1	/* FFT/polymult stage 2 */

typedef struct {
	gwhandle gwdata;	/* GWNUM handle */
	int	thread_num;	/* Worker number */
	int	stage1_threads;	/* Number of threads to use in stage 1 */
	int	stage2_threads;	/* Number of threads to use in stage 2 */
	struct work_unit *w;	/* Worktodo.txt entry */
	int	state;		/* One of the states listed above */
	uint64_t B;		/* Bound #1 (a.k.a. B1) */
	uint64_t C;		/* Bound #2 (a.k.a. B2) */
	uint64_t interim_B;	/* B1 we are currently calculating (equals B except when finishing stage 1 from a save file using a different B1). */
	uint64_t interim_C;	/* B2 we are currently calculating (equals C except when finishing stage 2 a save file using different B2) */
	uint64_t B_done;	/* We have completed calculating 3^e to this bound #1 */
	uint64_t C_done;	/* Stage 2 completed thusfar (updated every D section that is completed) */
	uint64_t first_C_start;	/* First stage 2 starting point (equals B except when worktodo.txt specifies starting point for bound #2). */
	int	optimal_B2;	/* TRUE if we calculate optimal bound #2 given currently available memory.  FALSE for a fixed bound #2. */
	uint64_t max_stage0_prime; /* Maximum small prime that can be used in stage 0 exponent calculation. */
	uint64_t stage0_bitnum;	/* Bit number in stage 0 exponent to process next */
	readSaveFileState read_save_file_state;	/* Manage savefile names during reading */
	writeSaveFileState write_save_file_state; /* Manage savefile names during writing */
	void	*sieve_info;	/* Prime number sieve */
	uint64_t stage1_prime;	/* Prime number being processed */
	unsigned long stage1_fftlen; /* FFT length used in stage 1 */

	gwnum	x;		/* The stage 1 value being computed */
	gwnum	gg;		/* The stage 2 accumulated value.  Will be GCD'ed at stage 2 completion. */

	/* Stage 2 data common to prime pairing and polymult */
	gwnum	invx;		/* For polymult stage 2, 1/x needed during stage 2 init */
	giant	x_binary;	/* The stage 1 result - ready for writing to a save file */
	giant	invx_binary;	/* The stage 1 result inverse - ready for writing to a save file */
	giant	gg_binary;	/* The stage 2 accmulator converted to binary (during stage 2 init) */
	int	stage2_type;	/* Prime pairing or polymult stage 2 */
	uint64_t stage2_numvals; /* Number of gwnums used in stage 2 */
	int	D;		/* Stage 2 loop size */
	int	numrels;	/* Number of relative primes less than D/2 (the number of relative primes in one full relp_set) */
	gwnum	*nQx;		/* Array of relprime data or polymult coefficients used in stage 2 */
	double	est_stage2_stage1_ratio; /* Estimated stage 2 runtime / stage 1 runtime ratio */
	double	pct_mem_to_use;	/* If we get memory allocation errors in stage 2 init, we progressively try using less and less memory. */
	uint64_t B2_start;	/* Starting point of first D section to be processed in stage 2 (an odd multiple of D/2) */
	uint64_t numDsections;	/* Number of D sections to process in stage 2 */
	uint64_t Dsection;	/* Current D section being processed in stage 2 */
	uint64_t first_relocatable; /* First relocatable prime (same as B1 unless pairmaps must be split or mem change caused a replan) */
	uint64_t last_relocatable; /* Last relocatable prime for filling pairmaps (unless mem change causes a replan) */

	/* Prime pairing stage 2 data */
	gwnum	V;		/* V_1 in a stage 2 Lucas sequence */
	gwnum	Vn;		/* V_n in a stage 2 Lucas sequence */
	gwnum	Vn1;		/* V_{n+1} in a stage 2 Lucas sequence */
	int	totrels;	/* Number relatively prime nQx values used */
	uint64_t max_pairmap_Dsections;	/* Number of D sections that can fit in a pairing map */
	uint8_t	*pairmap;	/* Pairing map for prime pairings in each D section */
	uint64_t pairmap_size;	/* Size of the pairing map */
	uint8_t *pairmap_ptr;	/* Pointer to the next byte to process in the pairing map */
	int16_t relp_sets[32];	/* The relp sets we are using in stage 2 */
	int	relp;		/* Last relative prime processed in the current D section */

	/* Polymult stage 2 data */
	int32_t	required_missing; /* When B2_start exceeds B1, primes are moved to first_missing_prime * prime.  If a save file is created mid-stage 2 */
				/* then resuming must pick a D value that does not eliminate multiples of first_missing_prime. */ 
	pmhandle polydata;	/* Polymult data structure */
	uint64_t poly1_size;	/* Size of the first RLP poly */
	uint64_t poly2_size;	/* Size of the second RLP that evaluates poly #1 at multiple points */
	uint64_t num_points;	/* Number of points (multiples of D) that can be evalualted with each polymult. */
	gwnum	diff1;		/* Used in finite differences to compute poly #2 coefficients */
	gwnum	r_squared;	/* Used in finite differences to compute poly #2 coefficients */
	gwnum	r_2helper;	/* r^(2*helper_count).  Used in multi-threaded finite differences to compute poly #2 coefficients */
	gwnum	r_2helper2;	/* r^(2*helper_count^2).  Used in multi-threaded finite differences to compute poly #2 coefficients */
	int	helper_work;	/* Type of work helper routine should perform */
	int	helper_count;	/* Total number of threads including main thread doing helper work */
	gwnum	*poly2;		/* Array of poly2 coefficients for helper routine to compute */
	uint64_t remaining_poly2_size; /* Number of poly2 coefficients to compute */
	gwnum	*points;	/* Array of evaluated points for helper routine to accumulate into gg for later GCD */
} pm1handle;

/* Perform cleanup functions. */

void pm1_cleanup (
	pm1handle *pm1data)
{

/* Free memory */

	free (pm1data->x_binary), pm1data->x_binary = NULL;
	free (pm1data->invx_binary), pm1data->invx_binary = NULL;
	free (pm1data->gg_binary), pm1data->gg_binary = NULL;
	free (pm1data->nQx), pm1data->nQx = NULL;
	free (pm1data->pairmap), pm1data->pairmap = NULL;
	polymult_done (&pm1data->polydata);
	gwdone (&pm1data->gwdata);
	pm1data->x = NULL;
	pm1data->gg = NULL;
	end_sieve (pm1data->sieve_info), pm1data->sieve_info = NULL;
}

/* Computes the modular inverse of a number.  This is done using the extended GCD algorithm.  If a factor is accidentally found, it is */
/* returned in factor.  Function returns stop_reason if there is an allocation error. */

int pm1_modinv (
	pm1handle *pm1data,
	gwnum	b,
	giant	N,
	giant	&factor)
{
	giant	v;

/* Convert input number to binary */

	v = popg (&pm1data->gwdata.gdata, ((int) pm1data->gwdata.bit_length >> 5) + 10);
	if (v == NULL) goto oom;
	if (gwtogiant (&pm1data->gwdata, b, v)) {
		// On unexpected, should-never-happen error, return out-of-memory for lack of a better error message
		goto oom;
	}

/* Use the faster GMP library to do an extended GCD which gives us 1/v mod N */

	{
	mpz_t	__v, __N, __gcd, __inv;

/* Do the extended GCD */

	mpz_init (__v);
	mpz_init (__N);
	mpz_init (__gcd);
	mpz_init (__inv);
	gtompz (v, __v);
	gtompz (N, __N);
	mpz_gcdext (__gcd, __inv, NULL, __v, __N);
	mpz_clear (__v);

/* If a factor was found (gcd != 1 && gcd != N), save it in FAC */

	if (mpz_cmp_ui (__gcd, 1) && mpz_cmp (__gcd, __N)) {
		factor = allocgiant ((int) divide_rounding_up (mpz_sizeinbase (__gcd, 2), 32));
		if (factor == NULL) goto oom;
		mpztog (__gcd, factor);
	}

/* Otherwise, convert the inverse to FFT-ready form */

	else {
		factor = NULL;
		if (mpz_sgn (__inv) < 0) mpz_add (__inv, __inv, __N);
		mpztog (__inv, v);
		gianttogw (&pm1data->gwdata, v, b);
	}

/* Cleanup and return */

	mpz_clear (__gcd);
	mpz_clear (__inv);
	mpz_clear (__N);
	}

/* Clean up */

	pushg (&pm1data->gwdata.gdata, 1);

/* Return */

	return (0);

/* Out of memory exit path */

oom:	return (OutOfMemory (pm1data->thread_num));
}

/* Routines to create and read save files for a P-1 factoring job */

#define PM1_MAGICNUM	0x317a394b
//#define PM1_VERSION	2				/* Changed in 29.4 build 7 -- corrected calc_exp bug */
//#define PM1_VERSION	3				/* Changed in 29.8 build 8.  Configurable calc_exp max exponent */
//#define PM1_VERSION	4				/* Complete overhaul in 30.4.  New stage 2 code with Preda optimizations */
//#define PM1_VERSION	5				/* Stage 2 overhaul in 30.7.  P+1 style stage 2, pairing with relp_sets, compressed pairing map */
//#define PM1_VERSION	6				/* Version 30.8 pre-beta.  Max_stage0_prime and stage0_bitnum stored as uint64_t. */
//#define PM1_VERSION	7				/* Stage 2 overhaul in 30.8.  Polymult. */
#define PM1_VERSION	8				/* 30.10 added required_missing */

void pm1_save (
	pm1handle *pm1data)
{
	int	fd;
	struct work_unit *w = pm1data->w;
	uint32_t sum = 0;

/* Create the intermediate file */

	fd = openWriteSaveFile (&pm1data->write_save_file_state);
	if (fd < 0) return;

/* Write the file header */

	if (!write_header (fd, PM1_MAGICNUM, PM1_VERSION, w)) goto writeerr;

/* Write the file data */

	if (! write_int (fd, pm1data->state, &sum)) goto writeerr;

	if (pm1data->state == PM1_STATE_STAGE0) {
		if (! write_uint64 (fd, pm1data->interim_B, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->max_stage0_prime, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->stage0_bitnum, &sum)) goto writeerr;
	}

	else if (pm1data->state == PM1_STATE_STAGE1) {
		if (! write_uint64 (fd, pm1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->interim_B, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->stage1_prime, &sum)) goto writeerr;
	}

	else if (pm1data->state == PM1_STATE_MIDSTAGE) {
		if (! write_uint64 (fd, pm1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->C_done, &sum)) goto writeerr;
	}

	// Save everything necessary to restart stage 2 without calling pm1_stage2_impl again
	else if (pm1data->state == PM1_STATE_STAGE2) {
		uint64_t remaining_pairmap_size;
		if (! write_uint64 (fd, pm1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->C_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->interim_C, &sum)) goto writeerr;
		if (! write_int (fd, pm1data->stage2_type, &sum)) goto writeerr;
//GW: ECM has a much cleaner save file for polymult stage 2!  Clean this up?
		if (! write_int (fd, pm1data->D, &sum)) goto writeerr;
		if (! write_int (fd, pm1data->numrels, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->B2_start, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->numDsections, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->Dsection, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->first_relocatable, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->last_relocatable, &sum)) goto writeerr;
		if (pm1data->stage2_type == PM1_STAGE2_PAIRING) {
			if (! write_int (fd, (int) pm1data->stage2_numvals, &sum)) goto writeerr;
			if (! write_int (fd, pm1data->totrels, &sum)) goto writeerr;
			if (! write_uint64 (fd, pm1data->max_pairmap_Dsections, &sum)) goto writeerr;
			if (! write_int (fd, pm1data->relp, &sum)) goto writeerr;
			if (! write_array (fd, (char *) pm1data->relp_sets, 32 * sizeof (int16_t), &sum)) goto writeerr;
			// Output the truncated pairmap
			remaining_pairmap_size = pm1data->pairmap_size - (pm1data->pairmap_ptr - pm1data->pairmap);
			if (! write_uint64 (fd, remaining_pairmap_size, &sum)) goto writeerr;
			if (! write_array (fd, (char *) pm1data->pairmap_ptr, (size_t) remaining_pairmap_size, &sum)) goto writeerr;
		} else {
			if (! write_int32 (fd, pm1data->required_missing, &sum)) goto writeerr;
		}
	}

	else if (pm1data->state == PM1_STATE_GCD) {
		if (! write_uint64 (fd, pm1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->C_done, &sum)) goto writeerr;
	}

	else if (pm1data->state == PM1_STATE_DONE) {
		if (! write_uint64 (fd, pm1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pm1data->C_done, &sum)) goto writeerr;
	}

/* Write the gwnum values used in stage 1 and stage 2.  There are occasions where x or gg may be in a partially FFTed state. */

	// Save stage 1's x value from either gwnum x or the compressed x_binary.
	// For backward compatibility with version 4 and 5 save files, output a we-have-x flag.
	{
		int have_x = TRUE;
		if (! write_int (fd, have_x, &sum)) goto writeerr;
		if (pm1data->x_binary != NULL) { if (! write_giant (fd, pm1data->x_binary, &sum)) goto writeerr; }
		else { if (! write_gwnum (fd, &pm1data->gwdata, pm1data->x, &sum)) goto writeerr; }
	}

	// Save stage 1's 1/x value that allows us to do either a prime pairing or polymult stage 2.
	if (pm1data->state >= PM1_STATE_MIDSTAGE && pm1data->state <= PM1_STATE_STAGE2) {
		if (! write_giant (fd, pm1data->invx_binary, &sum)) goto writeerr;
	}

	// Save the stage 2 accumulator
	if (pm1data->state >= PM1_STATE_MIDSTAGE && pm1data->state <= PM1_STATE_GCD) {
		int have_gg = (pm1data->gg != NULL || pm1data->gg_binary != NULL);
		if (! write_int (fd, have_gg, &sum)) goto writeerr;
		if (have_gg && pm1data->gg_binary != NULL && !write_giant (fd, pm1data->gg_binary, &sum)) goto writeerr;
		if (have_gg && pm1data->gg != NULL && !write_gwnum (fd, &pm1data->gwdata, pm1data->gg, &sum)) goto writeerr;
	}

/* In case we're at peak memory usage, free the cached gwnums that write_gwnum allocated (one for gwunfft and one for gwtobinary result) */

	gwfree_cached (&pm1data->gwdata);
	mallocFreeForOS ();

/* Write the checksum, we're done */

	if (! write_checksum (fd, sum)) goto writeerr;

	closeWriteSaveFile (&pm1data->write_save_file_state, fd);
	return;

/* An error occurred.  Close and delete the current file. */

writeerr:
	deleteWriteSaveFile (&pm1data->write_save_file_state, fd);
}

/* Read a version 25 through version 30.3 save file */

int pm1_old_restore (
	pm1handle *pm1data,
	int	fd,
	unsigned long version,
	uint32_t filesum)
{
	uint32_t sum = 0;
	unsigned long state, bitarray_len, unused;
	uint64_t processed, B_done, B, C_done, unused64;

/* Read the first part of the save file, much will be ignored but must be read for backward compatibility */

	if (! read_long (fd, &state, &sum)) goto readerr;
	if (version == 2) pm1data->max_stage0_prime = 13333333;		// The hardwired value prior to version 29.8 build 8
	else {
		unsigned long tmp;
		if (! read_long (fd, &tmp, &sum)) goto readerr;
		pm1data->max_stage0_prime = tmp;
	}
	if (! read_uint64 (fd, &B_done, &sum)) goto readerr;
	if (! read_uint64 (fd, &B, &sum)) goto readerr;
	if (! read_uint64 (fd, &C_done, &sum)) goto readerr;
	if (! read_uint64 (fd, &unused64, &sum)) goto readerr;		// C_start
	if (! read_uint64 (fd, &unused64, &sum)) goto readerr;		// C_done
	if (! read_uint64 (fd, &processed, &sum)) goto readerr;
	if (! read_long (fd, &unused, &sum)) goto readerr;		// D
	if (! read_long (fd, &unused, &sum)) goto readerr;		// E
	if (! read_long (fd, &unused, &sum)) goto readerr;		// rels_done
	if (! read_long (fd, &bitarray_len, &sum)) goto readerr;
	if (bitarray_len) {
		char *bitarray = (char *) malloc (bitarray_len);
		if (bitarray == NULL) goto readerr;
		if (! read_array (fd, bitarray, (size_t) bitarray_len, &sum)) goto readerr;
		free (bitarray);
	}
	if (! read_uint64 (fd, &unused64, &sum)) goto readerr;		// bitarray_first_number
	if (! read_long (fd, &unused, &sum)) goto readerr;		// pairs_set
	if (! read_long (fd, &unused, &sum)) goto readerr;		// pairs_done

/* Depending on the state, some of the values read above are not meaningful. */
/* In stage 0, only B and processed (bit number) are meaningful. */
/* In stage 1, only B_done, B, and processed (prime) are meaningful. */
/* In stage 2, only B_done is useful.  We cannot continue an old stage 2. */
/* When done, only B_done and C_done are meaningful. */

	if (state == 3) {				// PM1_STATE_STAGE0
		pm1data->state = PM1_STATE_STAGE0;
		pm1data->interim_B = B;
		pm1data->stage0_bitnum = (unsigned long) processed;
		// Version 29.4 build 7 changed the calc_exp algorithm which invalidates earlier save files that are in stage 0
		if (version == 1) {
			OutputBoth (pm1data->thread_num, "P-1 save file incompatible with this program version.  Restarting stage 1 from the beginning.\n");
			goto readerr;
		}
	} else if (state == 0) {			// PM1_STATE_STAGE1
		pm1data->state = PM1_STATE_STAGE1;
		pm1data->B_done = B_done;			//GW: set this to processed???
		pm1data->interim_B = B;
		pm1data->stage1_prime = processed;
	} else if (state == 1) {			// PM1_STATE_STAGE2
		pm1data->state = PM1_STATE_STAGE1;
		pm1data->stage1_prime = B_done;
		pm1data->interim_B = B_done;
		pm1data->B_done = B_done;
		pm1data->C_done = B_done;
		// Current stage 2 code is incompatible with older save files
		OutputBoth (pm1data->thread_num, "Cannot continue stage 2 from old P-1 save file.  Restarting stage 2 from the beginning.\n");
	} else if (state == 2) {			// PM1_STATE_DONE
		pm1data->state = PM1_STATE_DONE;
		pm1data->B_done = B_done;
		pm1data->C_done = C_done;
	}

/* Read the gwnum values */

	pm1data->x = gwalloc (&pm1data->gwdata);
	if (pm1data->x == NULL) goto readerr;
	if (! read_gwnum (fd, &pm1data->gwdata, pm1data->x, &sum)) goto readerr;

	pm1data->gg = NULL;
	if (state == 1) {
		pm1data->gg = gwalloc (&pm1data->gwdata);
		if (pm1data->gg == NULL) goto readerr;
		if (! read_gwnum (fd, &pm1data->gwdata, pm1data->gg, &sum)) goto readerr;
	}

/* Read and compare the checksum */

	if (filesum != sum) goto readerr;
	_close (fd);

/* All done */

	return (TRUE);

/* An error occurred.  Cleanup and return. */

readerr:
	_close (fd);
	return (FALSE);
}


/* Read a save file */

int pm1_restore (			/* For version 30.4 and later save files */
	pm1handle *pm1data)
{
	int	fd;
	struct work_unit *w = pm1data->w;
	uint32_t version, sum = 0, filesum;

/* Open the intermediate file */

	fd = _open (pm1data->read_save_file_state.current_filename, _O_BINARY | _O_RDONLY);
	if (fd < 0) goto err;

/* Read the file header */

	if (! read_magicnum (fd, PM1_MAGICNUM)) goto readerr;
	if (! read_header (fd, &version, w, &filesum)) goto readerr;
	if (version < 1 || version > PM1_VERSION) goto readerr;
	if (version < 4) return (pm1_old_restore (pm1data, fd, version, filesum));

/* Read the first part of the save file */

	if (! read_int (fd, &pm1data->state, &sum)) goto readerr;

/* Read state dependent data */

	if (pm1data->state == PM1_STATE_STAGE0) {
		if (! read_uint64 (fd, &pm1data->interim_B, &sum)) goto readerr;
		if (version <= 5) {
			unsigned long tmp;
			if (! read_long (fd, &tmp, &sum)) goto readerr;
			pm1data->max_stage0_prime = tmp;
			if (! read_long (fd, &tmp, &sum)) goto readerr;
			pm1data->stage0_bitnum = tmp;
		} else {
			if (! read_uint64 (fd, &pm1data->max_stage0_prime, &sum)) goto readerr;
			if (! read_uint64 (fd, &pm1data->stage0_bitnum, &sum)) goto readerr;
		}
	}

	else if (pm1data->state == PM1_STATE_STAGE1) {
		if (! read_uint64 (fd, &pm1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->interim_B, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->stage1_prime, &sum)) goto readerr;
		// Some early 30.8 builds set state to PM1_STATE_STAGE1 rather than PM1_STATE_DONE when a factor was found in stage 1.  Correct this.
		if (pm1data->interim_B < pm1data->B_done) {
			pm1data->state = PM1_STATE_DONE;
			if (pm1data->C_done < pm1data->B) pm1data->C_done = pm1data->B;
		}
	}

	else if (pm1data->state == PM1_STATE_MIDSTAGE) {
		if (version <= 6) {
			OutputBoth (pm1data->thread_num, "P-1 save file incompatible with this program version.  Restarting stage 1 from the beginning.\n");
			goto readerr;
		}
		if (! read_uint64 (fd, &pm1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->C_done, &sum)) goto readerr;
	}

	else if (pm1data->state == PM1_STATE_STAGE2) {
		if (version <= 6) {
			OutputBoth (pm1data->thread_num, "P-1 save file incompatible with this program version.  Restarting stage 1 from the beginning.\n");
			goto readerr;
		}
		if (! read_uint64 (fd, &pm1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->C_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->interim_C, &sum)) goto readerr;
		if (! read_int (fd, &pm1data->stage2_type, &sum)) goto readerr;
		if (! read_int (fd, &pm1data->D, &sum)) goto readerr;
		if (! read_int (fd, &pm1data->numrels, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->B2_start, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->numDsections, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->Dsection, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->first_relocatable, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->last_relocatable, &sum)) goto readerr;
		if (pm1data->stage2_type == PM1_STAGE2_PAIRING) {
			int	temp;
			if (! read_int (fd, &temp, &sum)) goto readerr;
			pm1data->stage2_numvals = temp;
			if (! read_int (fd, &pm1data->totrels, &sum)) goto readerr;
			if (! read_uint64 (fd, &pm1data->max_pairmap_Dsections, &sum)) goto readerr;
			if (! read_int (fd, &pm1data->relp, &sum)) goto readerr;
			if (! read_array (fd, (char *) pm1data->relp_sets, 32 * sizeof (int16_t), &sum)) goto readerr;
			if (! read_uint64 (fd, &pm1data->pairmap_size, &sum)) goto readerr;
			pm1data->pairmap = (uint8_t *) malloc ((size_t) pm1data->pairmap_size);
			if (pm1data->pairmap == NULL) goto readerr;
			if (! read_array (fd, (char *) pm1data->pairmap, (size_t) pm1data->pairmap_size, &sum)) goto readerr;
			pm1data->pairmap_ptr = pm1data->pairmap;
		} else {
			if (version <= 7) {		// Version 30.8 did not write this field (a bug).  Compute it assuming stage 2 was no
				pm1data->required_missing = map_polyD_to_first_missing_prime (pm1data->D);
			} else {
				if (! read_int32 (fd, &pm1data->required_missing, &sum)) goto readerr;
			}
		}
	}

	else if (pm1data->state == PM1_STATE_GCD) {
		if (! read_uint64 (fd, &pm1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->C_done, &sum)) goto readerr;
	}

	else if (pm1data->state == PM1_STATE_DONE) {
		if (! read_uint64 (fd, &pm1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pm1data->C_done, &sum)) goto readerr;
	}

/* Read the gwnum value used in stage 1 */

	{
		int	have_x;
		if (version == 4) have_x = TRUE;				// 30.4 save files
		else if (! read_int (fd, &have_x, &sum)) goto readerr;		// 30.7 save files
		if (have_x) {
			pm1data->x_binary = allocgiant (((int) pm1data->gwdata.bit_length >> 5) + 10);
			if (pm1data->x_binary == NULL) goto readerr;
			if (! read_giant (fd, pm1data->x_binary, &sum)) goto readerr;
			if (pm1data->state < PM1_STATE_MIDSTAGE || pm1data->state == PM1_STATE_DONE) {
				pm1data->x = gwalloc (&pm1data->gwdata);
				if (pm1data->x == NULL) goto readerr;
				gianttogw (&pm1data->gwdata, pm1data->x_binary, pm1data->x);
				free (pm1data->x_binary), pm1data->x_binary = NULL;
			}
		}
	}

/* Read stage 2's 1/x value */

	if (version >= 6 && pm1data->state >= PM1_STATE_MIDSTAGE && pm1data->state <= PM1_STATE_STAGE2) {
			pm1data->invx_binary = allocgiant (((int) pm1data->gwdata.bit_length >> 5) + 10);
			if (pm1data->invx_binary == NULL) goto readerr;
			if (! read_giant (fd, pm1data->invx_binary, &sum)) goto readerr;
	}

/* Read stage 2 accumulator gwnum */

	if (pm1data->state >= PM1_STATE_MIDSTAGE && pm1data->state <= PM1_STATE_GCD) {
		int	have_gg;
		if (version == 4) have_gg = TRUE;				// 30.4 save files
		else if (! read_int (fd, &have_gg, &sum)) goto readerr;		// 30.7 save files
		if (have_gg) {
			pm1data->gg_binary = allocgiant (((int) pm1data->gwdata.bit_length >> 5) + 10);
			if (pm1data->gg_binary == NULL) goto readerr;
			if (! read_giant (fd, pm1data->gg_binary, &sum)) goto readerr;
		}
	}

/* Read and compare the checksum */

	if (filesum != sum) goto readerr;
	_close (fd);

/* All done */

	return (TRUE);

/* An error occurred.  Cleanup and return. */

readerr:
	_close (fd);
	free (pm1data->x_binary), pm1data->x_binary = NULL;
	gwfree (&pm1data->gwdata, pm1data->x), pm1data->x = NULL;
	free (pm1data->invx_binary), pm1data->invx_binary = NULL;
	free (pm1data->gg_binary), pm1data->gg_binary = NULL;
err:
	return (FALSE);
}

/* Compute the cost (in squarings) of a particular P-1 stage 2 implementation. */

struct pm1_stage2_cost_data {
	/* Cost data common to ECM, P-1, P+1 */
	struct common_cost_data c;
	/* P-1 specific data sent to cost function follows */
	int	stage2_type;			// Prime pairing vs. polymult
	double	sieve_depth;			// How much trial factoring has been done
	double	takeAwayBits;			// Bits we get for free in smoothness of P-1 factor
	/* P-1 specific data returned from cost function follows */
	uint64_t poly2_size;			// Size of second poly (poly1_size is same as numrels)
	double	factor_probability;		// Chance of finding a factor as returned by pm1prob
};

double pm1_stage2_cost (
	void	*data)		/* P-1 specific costing data */
{
	struct pm1_stage2_cost_data *cost_data = (struct pm1_stage2_cost_data *) data;
	double	cost;

/* Stage 2 starts with computing V using two gwmuls (4 transforms) and a modinv.  The modinv cost will be added later. */

	cost_data->c.est_init_transforms = 4.0;

/* First estimate the stage 2 costs using prime pairing */

	if (cost_data->stage2_type == PM1_STAGE2_PAIRING) {

/* No polymult costs when pairing */

		cost_data->c.est_init_polymult = 0.0;
		cost_data->c.est_stage2_polymult = 0.0;

/* For the relative primes below D there are 2 luc_add calls for each increment of 6 in the nQx setup loop (plus 6 luc_dbls/luc_adds for setup). */
/* Computing VD is another luc_dbl and luc_add.  For the relative primes above D, we perform one luc_add for each rel_prime to calculate. */
/* We assume relp set -1 is not used, which means there are totrels - numrels relative primes above D to calculate.  Also, each nQx value will */
/* be FFTed once but this usually happens as a by-product of the luc_add calls.  Each luc_add/luc_dbl is 2 transforms. */

		cost_data->c.est_init_transforms += 6.0 * 2.0;						// Setup costs, 6 luc_adds at 2 transforms each
		cost_data->c.est_init_transforms += (double) (cost_data->c.D / 6 - 2) * 2.0 * 2.0;	// From 12 to D stepping by 6 is 2 luc_adds at 2 transforms each
		cost_data->c.est_init_transforms += 2.0 * 2.0;						// Cost to calculate VD, 1 luc_add and 1 luc_dbl
		cost_data->c.est_init_transforms += (cost_data->c.totrels - cost_data->c.numrels) * 2.0; // Cost for relprimes above D

/* Any intermediate relp_sets also cost one luc_add for each relprime.  Partial intermediate sets also must be accounted for. */

		cost_data->c.est_init_transforms +=
			cost_data->c.numrels * cost_data->c.relp_sets[1] * 2.0 +					 // Cost for full intermediate relp_sets
			one_based_modulo (cost_data->c.totrels, cost_data->c.numrels) * cost_data->c.relp_sets[2] * 2.0; // Cost for partial intermediate relp_sets

/* Each Dmultiple costs either a luc_dbl or luc_add.  Here we have to guess how many Dmultiples are needed.  The most expensive D-multiple is likely the */
/* calculation of B2_start -- 2*log2(B2_start/D).  In addition, we'll assume one more D-multiple for each relp_set. */

		cost_data->c.est_init_transforms += (2.0 * log2((double)((cost_data->c.B2_start + cost_data->c.D / 2) / cost_data->c.D)) + cost_data->c.multiplier) * 2.0;

/* Start main loop cost with one luc_add for each D section */

		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections * 2.0;

/* Each nQx value will be FFTed once, but most nQx values are FFTed during the computation of other nQx values.  We'll assume one */
/* relp_set of nQx values will need this half squaring. */

		cost_data->c.est_stage2_transforms += cost_data->c.numrels;

/* Finally, each prime pair and prime single costs one 2-FFT multiply */

		cost_data->c.est_stage2_transforms += (cost_data->c.est_numpairs + cost_data->c.est_numsingles) * 2.0;

/* Begin computing the total stage 2 cost */
/* Stage 2 FFT multiplications seem to be at least 20% slower than the squarings in pass 1.  This is likely due to several factors, */
/* including gwmult reading more data than gwsquare, worse L2/L3 cache behavior, and perhaps more startnextfft opportunities. */
/* Nov, 2009: On my Macbook Pro, with exponents around 45M and using 800MB memory, stage 2 transforms measured at 40% slower. */
/* Sept. 2021: With gwsubmul4 implemented in assembly for AVX and later, we re-ran the timings using 4 threads. */
/* According to time_gwnum.cpp, gwsubmul4 is 30% slower than gwsquare on an AVX-512 machine and 47% slower on some FMA machines. */
/* It is important to get the estimated pairing stage 2 cost and the estimated polymult stage 2 cost accurate so that we choose the pairing vs. polymult */
/* crossover correctly.  The default adjustment was determined by a typical P-1 on an exponent near 108 million using 6.5GB of memory on a i5-6500 and */
/* a Skylake-X box. */

		cost = cost_data->c.est_init_transforms + cost_data->c.est_stage2_transforms;
		cost *= IniGetFloat (INI_FILE, "Pm1TransformCost", (float) 1.502);

/* Add in the cost of prime pairing */

		cost += cost_data->c.est_pairing_runtime;

/* The adjustment above certainly won't be correct for all current and future CPUs.  Let the attentive user correct the stage 2 cost by */
/* monitoring the estimated stage 2 / stage 1 runtime ratio vs. the actual stage 2 / stage 1 runtime. */

		cost *= IniGetFloat (INI_FILE, "Pm1PairRatioAdjust", 1.0);

/* Return data P-1 implementation will need */
/* Stage 2 memory is totrels gwnums for the nQx array, 4 gwnums for V,Vn,Vn1,gg values. */

		cost_data->c.stage2_numvals = cost_data->c.totrels + 4;
	}

/* Estimate stage 2 costs using polymult and all available numvals */

	else {
		uint64_t poly1_size, poly2_size, min_poly2_size, max_poly2_size, acceptable_poly2_size, unacceptable_poly2_size;
		double	mem_used_by_a_gwnum, mem_saved_by_compression, mem_used_by_polymult;

/* No pairing costs when using polymult */

		cost_data->c.est_pairing_runtime = 0.0;

/* Stage 2 init costs are based on poly sizes.  Determine the maximum safe poly2 size that fits in memory. */

		poly1_size = cost_data->c.numrels;
		if (cost_data->c.gwdata != NULL) {
			mem_used_by_a_gwnum = (double) array_gwnum_size (cost_data->c.gwdata);
			max_poly2_size = max_safe_poly2_size (cost_data->c.gwdata, poly1_size*2, cost_data->c.numvals);
		} else {
			mem_used_by_a_gwnum = (double) cost_data->c.stage2_fftlen * sizeof (double) * 1.016;
			max_poly2_size = cost_data->c.numvals;
		}
		min_poly2_size = 2*poly1_size;
		if (min_poly2_size > max_poly2_size) return (-1.0);

		// Binary search for largest poly2_size that does not use too much memory
		mem_saved_by_compression = (double) poly1_size * mem_used_by_a_gwnum * (1.0 - cost_data->c.poly_compression);
		acceptable_poly2_size = min_poly2_size - 1;
		unacceptable_poly2_size = max_poly2_size + 1;
		while (unacceptable_poly2_size - acceptable_poly2_size >= 2) {
			uint64_t midpoint = (acceptable_poly2_size + unacceptable_poly2_size) / 2;
			mem_used_by_polymult = (double) polymult_mem_required (cost_data->c.polydata, poly1_size, midpoint, POLYMULT_INVEC1_MONIC_RLP | POLYMULT_CIRCULAR);
			if (poly1_size + midpoint + floor ((mem_used_by_polymult - mem_saved_by_compression) / mem_used_by_a_gwnum + 0.5) <= cost_data->c.numvals)
				acceptable_poly2_size = midpoint;		// Acceptable poly2 size
			else
				unacceptable_poly2_size = midpoint;		// Unacceptable poly2 size
		}
		if (acceptable_poly2_size < min_poly2_size) return (-1.0); 
		poly2_size = acceptable_poly2_size;
		mem_used_by_polymult = (double) polymult_mem_required (cost_data->c.polydata, poly1_size, poly2_size, POLYMULT_INVEC1_MONIC_RLP | POLYMULT_CIRCULAR);

		// Calc num poly1 points evaluated by each poly2
		uint64_t num_points = poly2_size - (2*poly1_size+1) + 1;
		if (num_points < 1) return (-1.0);

/* Loop increasing B2_start based on the fact that numDsections will be a multiple of num_points.  In essence this gives a free increase in the B2 endpoint. */

		uint64_t num_polyevals = divide_rounding_up (cost_data->c.numDsections, num_points);
		cost_data->c.numDsections = num_polyevals * num_points;
		if (cost_data->c.gap_end == 0) {
			for ( ; ; ) {
				uint64_t B2_end = cost_data->c.B2_start + cost_data->c.numDsections * cost_data->c.D;
				uint64_t B2_start = round_down_to_multiple_of (B2_end / cost_data->c.first_missing_prime, cost_data->c.D);
				if (cost_data->c.B2_start >= B2_start) break;
				cost_data->c.B2_start = B2_start;
			}
		}

/* Estimating the cost of a polymult is a nightmare.  When working on small numbers the poly sizes are large -- gwnum squarings are likely one pass FFTs */
/* operating from the CPU caches in pass 1, poly sizes are likely quite large requiring 2 passes and operating out of main memory.  When working on large */
/* numbers, gwnum squarings are two passes operating out of main memory while poly sizes are quite small (possibly one pass) and operating mostly out of */
/* the CPU caches.  What follows is a crude attempt to compensate for the above - guess a polymult cost based on the number of input gwnums.  We could */
/* further compensate for stage 2 init polymults writing to all input gwnums, whereas stage 2 main loop only writes to about 2/3 of the input gwnums. */

		// The default polymult costs come from P-1 on two exponents and two machines (Skylake-X and i5-6500).
		// A big exponent, M108889691 using 3.25GB, 6.5GB, and 57GB memory.  All poly FFTs should fit in the L2 cache.
		//	D: 90, 12x52 poly cost = 1.7
		//	D: 210, 24x105 poly cost = 2.145
		//	D: 1470, 168x925 poly cost = 3.95
		// A small exponent, M80051, using 3.25GB and 6.5GB.  Poly FFTs will not fit in the L2 cache.
		//	D: 150150, 14400x68338 poly cost = 15.394
		//	D: 330330, 31680x134268 poly cost = 17.02

		// I could not find a decent formula that fits the above data.  If the poly FFT does not fit in the L2 caches, then polymult does more
		// reading/writing from/to the slower L3 cache and main memory.  So, I assume that once the poly FFT does not fit in the L2 cache there is a
		// jump in poly costs.  Which led to the formula below.  

		// Polymult FFT memory consumption is poly2 size * 2 (need FFT mem for poly1 and poly2) * 2 (real and complex values) * 32-or-64
		// (for AVX or AVX-512 vector size).  Ramp up the cost slowly as we exceed the L2 cache.  The ramping formula is pulled out of thin air.

		// Use the 24x105 poly cost as a baseline and increment cost 0.58 for every doubling of the poly size
		double	polymult_cost;		// Cost of a polymult compared to a pass 1 transform
		polymult_cost = 2.145 + 0.58 * log2 ((double) poly2_size / 105.0);

		// Roughly double the poly cost when we exceed the L2 cache.
		double L2_cache_size = (CPU_NUM_L2_CACHES > 0 ? CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES : 0) * 1024.0;
		double polymult_mem_per_thread = mem_used_by_polymult / cost_data->c.stage2_threads;
		if (polymult_mem_per_thread >= 8 * L2_cache_size) polymult_cost *= 2.05;
		else if (polymult_mem_per_thread > L2_cache_size) polymult_cost *= 1.0 + 1.05 * (polymult_mem_per_thread - L2_cache_size) / (7 * L2_cache_size);
		polymult_cost *= IniGetFloat (INI_FILE, "Pm1PolymultCostAdjust", 1.0);

/* For the relative primes below D there are 2 luc_add calls for each increment of 6 in the nQx setup loop (plus 2 luc_dbls/luc_adds for setup). */
/* We perform one luc_add for each rel_prime to calculate. */

		cost_data->c.est_init_transforms += 4.0 * 2.0;						// V2 thru V6 setup costs, 2 luc_dbls/luc_adds at 2 FFTs each
		cost_data->c.est_init_transforms += (double) (cost_data->c.D / 4.0 / 6.0) * 2.0 * 2.0;	// Up to D/4 stepping by 6, two i values at 1 luc_add each
		cost_data->c.est_init_transforms += (cost_data->c.numrels) * 2.0;			// One luc_dbl for each relprimes

/* Building poly #1 requires log2(numrels) polymults with an FFT and inverse FFT on each poly coefficient */

		cost_data->c.est_init_polymult = log2 ((double) cost_data->c.numrels) * cost_data->c.numrels * polymult_cost;
		cost_data->c.est_init_transforms += log2 ((double) cost_data->c.numrels) * cost_data->c.numrels * 2.0;

/* Calculation of various constants, for exponentiate calls assume 1 squaring (2 transforms) plus 0.5 muls (1 transform on average) per bit. */

		cost_data->c.est_init_transforms += log2 ((double)(cost_data->c.D / 2)) * 3.0;		// Calc r
		cost_data->c.est_init_transforms += 2.0;						// Calc r^2
		cost_data->c.est_init_transforms += log2 ((double)(cost_data->c.D / 2)) * 3.0;		// Calc invr
		cost_data->c.est_init_transforms += 2.0;						// Calc r^-2
		cost_data->c.est_init_transforms += log2 ((double) cost_data->c.numrels * (double) cost_data->c.numrels / 2.0) * 3.0; // Calc r^(j^2)
		cost_data->c.est_init_transforms += (double) cost_data->c.numrels * 3.0 * 2.0;		// Three multiplies for each poly #1 coefficient
		cost_data->c.est_init_transforms += cost_data->c.numrels * 1.0;				// Compress poly #1 (about 0.5 transforms per coefficient)
		cost_data->c.est_init_transforms += log2 ((double) cost_data->c.B2_start / ((double) cost_data->c.D / 2.0)) * 3.0; // Calc e
		cost_data->c.est_init_transforms += log2 ((double) cost_data->c.numrels * 2.0) * 3.0 + 2.0; // Calc first diff1

/* Main loop requires two multiplies (four transforms) for every poly #2 coefficient.  Well the first is free and second is just two transforms. */

		cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections * 4.0 - 6.0;

/* Add the polymult cost */

		cost_data->c.est_stage2_polymult = (double) num_polyevals * (double) (poly1_size + poly2_size) * polymult_cost;

/* Each D section will have one polymult output coefficient that must be inverse FFTed (one transform) then multiplied into gg (three transforms). */

		cost_data->c.est_stage2_transforms += (double) cost_data->c.numDsections * 4.0;

/* Stage 2 memory is poly sizes plus 3 gwnums for diff1,r^2,gg.  Return the poly2_size. */

		cost_data->c.stage2_numvals = poly1_size + poly2_size + (int) floor ((mem_used_by_polymult - mem_saved_by_compression) / mem_used_by_a_gwnum + 0.5) + 3;
		cost_data->poly2_size = poly2_size;

/* Begin computing the total stage 2 cost. */
/* Stage 2 FFT multiplications are at least 20% slower than the squarings in pass 1.  This is likely due to several factors, including gwmult */
/* reading more data than gwsquare, worse L2 cache behavior, and perhaps more startnextfft opportunities.  Increase the stage 2 cost so that we get */
/* more accurate stage 2 / stage 1 ratios.  The default adjustment was determined on Skylake-X and i5-6500 CPUs doing P-1 on an exponent near 108 million. */

		cost = (cost_data->c.est_init_transforms + cost_data->c.est_stage2_transforms) * IniGetFloat (INI_FILE, "Pm1TransformCost", (float) 1.502);

/* Add in the cost of the polymults. */
/* The calculations above certainly won't be correct for all current and future CPUs.  Let the attentive user correct the stage 2 cost by */
/* monitoring the estimated stage 2 / stage 1 runtime ratio vs. the actual stage 2 / stage 1 runtime. */

		cost += (cost_data->c.est_init_polymult + cost_data->c.est_stage2_polymult) * IniGetFloat (INI_FILE, "Pm1PolyRatioAdjust", 1.0);
		cost *= (double) cost_data->c.stage2_fftlen / (double) cost_data->c.stage1_fftlen;	// Convert to stage1_fftlen cost
	}

/* Add in the modinv cost for computing the initial V value */

	cost += cost_data->c.modinv_cost;

/* Save the final costs fudged by an auto-adjusting value.  If there are more stage 2 threads than stage 1, adjust the cost down.  Yes, the cost in terms */
/* of transforms is not reduced by extra threads, but the time spent in stage 2 is reduced.  The user of Stage2ExtraThreads expects the reduced time spent */
/* in stage 2 to result in a larger optimal B2.  Reducing the cost will make this happen. */

	cost *= IniGetFloat (INI_FILE, "Pm1Stage2RatioAdjust", 1.0);
	cost *= (double) cost_data->c.stage1_threads / (double) cost_data->c.stage2_threads;
	cost_data->c.stage2_cost = cost;						// Raw cost of stage 2
	cost_data->c.est_stage2_stage1_ratio = cost / cost_data->c.stage1_cost;		// Estimated time of stage 2 relative to stage 1
	cost_data->c.total_cost = cost_data->c.stage1_cost + cost_data->c.stage2_cost + cost_data->c.gcd_cost;

/* Calculate and return the efficiency which is the P-1 "value" divided by total cost of running P-1.  If TF depth known "value" is the factor probability, */
/* otherwise use ECM's kruppa_adjust to assign an estimated "value" of the P-1 run. */

	if (cost_data->sieve_depth != 0.0) {
		cost_data->factor_probability = pm1prob (cost_data->takeAwayBits, cost_data->sieve_depth, cost_data->c.B1,
							 cost_data->c.B2_start + cost_data->c.numDsections * cost_data->c.D);
		cost_data->c.efficiency = cost_data->factor_probability;
	} else {
		cost_data->factor_probability =	0.0;		// We cannot accurately compute factor probability without a sieve depth
		cost_data->c.efficiency = (double) kruppa_adjust (cost_data->c.B2_start + cost_data->c.numDsections * cost_data->c.D, cost_data->c.B1);
	}
	cost_data->c.efficiency /= cost_data->c.total_cost;
	return (cost_data->c.efficiency);
}

/* Given a number of temporaries derived from a memory limit, choose best algorithm and best value for D. */
/* Returns the cost (in squarings) of implementing stage 2 as well as other cost data needed to implement stage 2 */

double pm1_stage2_impl_given_numvals (
	pm1handle *pm1data,
	uint64_t numvals,				/* Number of gwnum temporaries available */
	int	forced_stage2_type,			/* 0 = cost pairing, 1 = cost poly, 99 = cost both */
	struct pm1_stage2_cost_data *return_cost_data)	/* Returned extra data from P-1 costing function */
{
	double	efficiency, best_efficiency;	/* Best efficiency for each of the 4 possible stage 2 implementations */
	struct pm1_stage2_cost_data cost_data;	/* Extra data passed to and returned from P-1 costing function */

/* The cost of stage 1 (in FFTs) is about 1.44 * B1 squarings.  Stage2 cost is adjusted later because stage 2 multiplies are costlier than stage 1 squarings. */

	cost_data.c.stage1_cost = (double) pm1data->B * 1.44 * 2.0;

/* Find the least costly stage 2 plan looking at the two available algorithms */

	best_efficiency = -1.0;
	cost_data.c.required_missing = pm1data->required_missing;
	cost_data.c.gwdata = &pm1data->gwdata;
	cost_data.c.polydata = &pm1data->polydata;
	cost_data.c.stage1_fftlen = pm1data->stage1_fftlen;
	cost_data.c.stage2_fftlen = gwfftlen (&pm1data->gwdata);
	cost_data.c.stage1_threads = pm1data->stage1_threads;
	cost_data.c.stage2_threads = pm1data->stage2_threads;
	init_gcd_costs (pm1data->gwdata.bit_length, &cost_data);
	cost_data.sieve_depth = pm1data->w->sieve_depth;
	cost_data.takeAwayBits = isMersenne (pm1data->w->k, pm1data->w->b, pm1data->w->n, pm1data->w->c) ? log2 (pm1data->w->n) + 1.0 :
				 isGeneralizedFermat (pm1data->w->k, pm1data->w->b, pm1data->w->n, pm1data->w->c) ? log2 (pm1data->w->n) : 0.0;

/* Compute the expected compression of poly1 using polymult_preprocess.  Default compression is usually 1.6%.  Some FFT sizes may have more padding */
/* and thus more compression.  If poly compression option is set we'll get another 12.5%. */

	cost_data.c.poly_compression = (double) gwfftlen (&pm1data->gwdata) * (double) sizeof (double) / (double) array_gwnum_size (&pm1data->gwdata);
	if (IniGetInt (INI_FILE, "Poly1Compress", 2) == 2) cost_data.c.poly_compression *= 0.875;
	if (IniGetInt (INI_FILE, "Poly1Compress", 2) == 0) cost_data.c.poly_compression = 1.0;

/* Find the most efficienct stage 2 plan looking at the two available algorithms */

	for (int stage2_type = 0; stage2_type <= 1; stage2_type++) {	// Prime pairing vs. polymult

/* Check for QA'ing a specific P-1 implementation type */

		if (QA_TYPE != 0 && QA_TYPE != stage2_type + 1) continue;

/* Check which stage 2 types we are to cost - mainly used for QA/debugging */

		if (forced_stage2_type != 99 && forced_stage2_type != stage2_type) continue;

/* Cost out a P-1 stage 2 implementation.  Keep track of the best implementation. */
/* Try various values of D until we find the best one.  Pairing requires V,Vn,Vn1,gg gwnums, polymult requires diff1,r^2,gg gwnums. */
/* Polymult does not need to look at the reduced numvals costs -- no binary search needed (at least if we want to sail on past B2) */

		cost_data.stage2_type = (stage2_type == 0 ? PM1_STAGE2_PAIRING : PM1_STAGE2_POLYMULT);
		cost_data.c.use_poly_D_data = (stage2_type == 1);
		cost_data.c.centers_on_Dmultiple = (stage2_type != 1);
		efficiency = best_stage2_impl (pm1data->B, pm1data->first_relocatable, pm1data->last_relocatable, pm1data->C_done, pm1data->C,
					       numvals - (stage2_type == 0 ? 4 : 3), &pm1_stage2_cost, &cost_data);
		if (efficiency > best_efficiency) {
			best_efficiency = efficiency;
			*return_cost_data = cost_data;
		}
	}

/* Return our best implementation */

	return (best_efficiency);
}

/* Choose the most effective B2 for a P-1 run with a fixed B1 given the number of gwnums we are allowed to allocate. */
/* That is, find the B2 such that investing a fixed cost in either a larger B1 or B2 results in the same increase in chance of finding a factor. */

int pm1_choose_B2 (
	pm1handle *pm1data,
	uint64_t numvals,
	int	forced_stage2_type,		/* 0 = cost pairing, 1 = cost poly, 99 = cost both */
	char	*optimal_B2_msg)		/* Message to be output if this optimal B2 selection is used */
{
	int	max_B2mult;
	struct pm1_stage2_cost_data cost_data;	/* Extra data passed to P-1 costing function */
	struct pm1_stage2_efficiency {
		int	i;
		int	actual_i;
		int	D;			/* D used for this B2 (zero for prime pairing) */
		uint64_t B2;			/* Actual B2 -- polymult often chooses a larger B2 value */
		double	B2_cost;		/* Cost of stage 2 in transforms */
		double	fac_pct;		/* Percentage chance of finding a factor */
		double	efficiency;
		double	compared_to_next_i;	/* Best i occurs when comparison to next i approaches zero (no difference between investing time in B1 or B2) */
	} best[3];
	double	saved_sieve_depth;
	double	takeAwayBits;			/* Bits we get for free in smoothness of P-1 factor */

// Assume that if we have 150 numvals or more that polymult will be better than prime pairing

	if (forced_stage2_type == 99 && numvals >= 150) forced_stage2_type = 1;

// Since Kruppa_adjust cannot be used to compare "curves" with different B1 values, we'll just optimize based on an assumed TF level of 67 bits

	saved_sieve_depth = pm1data->w->sieve_depth;
	if (saved_sieve_depth == 0.0) pm1data->w->sieve_depth = 67.0;

// Cost out a B2 value.  Then compare it to the next larger B2 to determine if investing more time in larger B1 or larger B2 would be better.
	max_B2mult = IniGetInt (INI_FILE, "MaxOptimalB2Multiplier", (int) (numvals < 100000 ? numvals * 100 : 10000000));
#define p1eval(x,B2mult)	x.i = B2mult; if (x.i > max_B2mult) x.i = max_B2mult; \
				pm1data->C = x.i * pm1data->B; \
				x.efficiency = pm1_stage2_impl_given_numvals (pm1data, numvals, forced_stage2_type, &cost_data); \
				if (x.efficiency > 0.0) { \
				  x.B2 = cost_data.c.B2_start + cost_data.c.numDsections * cost_data.c.D; \
				  x.actual_i = (int) (x.B2 / pm1data->B); \
				  x.B2_cost = cost_data.c.stage2_cost; \
				  x.fac_pct = cost_data.factor_probability; \
				  x.D = (cost_data.stage2_type == PM1_STAGE2_POLYMULT) ? cost_data.c.D : 0; \
				  /* To see if we're near the point where we're ambivalent to increasing B1 vs. B2, compare this fac pct */ \
				  /* to a run where B2 is reduced 1% and the CPU savings is used to increase B1. */ \
				  uint64_t altB1 = pm1data->B + (uint64_t) (0.01 * cost_data.c.stage2_cost / (1.44 * 2.0)); \
				  uint64_t altB2 = altB1 + (uint64_t) (0.99 * (x.B2 - pm1data->B)); \
				  x.compared_to_next_i = pm1prob (takeAwayBits, pm1data->w->sieve_depth, altB1,altB2) - cost_data.factor_probability; \
				  x.compared_to_next_i = - fabs (x.compared_to_next_i); \
				} else x.actual_i = x.i, x.compared_to_next_i = -100.0;

// Return TRUE if x is better than y.
// Each D value has its own breakeven point.  Higher D values are always more efficient than lower D values.
// Prime pairing also has its own breakeven point.
#define p1compare(x,y)	(((x.D && y.D) && (x.D > y.D || (x.D == y.D && x.compared_to_next_i > y.compared_to_next_i))) ||	\
			 ((x.D == 0 || y.D == 0) && (x.compared_to_next_i > y.compared_to_next_i)))

/* Look for the best B2 which is likely between 5*B1 and 10000*B1.  If optimal is not between these bounds, don't worry we'll locate the optimal spot anyway. */

	takeAwayBits = isMersenne (pm1data->w->k, pm1data->w->b, pm1data->w->n, pm1data->w->c) ? log2 (pm1data->w->n) + 1.0 :
		       isGeneralizedFermat (pm1data->w->k, pm1data->w->b, pm1data->w->n, pm1data->w->c) ? log2 (pm1data->w->n) : 0.0;
	p1eval (best[0], 5);
	p1eval (best[1], best[0].actual_i * 50);
	p1eval (best[2], best[1].actual_i * 50);
	if (best[0].efficiency < 0.0 && best[1].efficiency < 0.0 && best[2].efficiency < 0.0) return (FALSE);

/* Handle case where midpoint is worse than the start point.  The search code requires best[1] is better than best[0] and best[2]. */
/* To be safe, like ECM catch cases where no suitable best[1] exists. */

	while (p1compare (best[0], best[1]) && best[0].i + 2 != best[2].i) {
		best[2] = best[1];
		p1eval (best[1], (best[0].i + best[2].i) / 2);
	}

/* Handle case where midpoint is worse than the end point */
/* The search code requires best[1] is better than best[0] and best[2] */

	while (!p1compare (best[1], best[2]) && best[2].i < max_B2mult) {
		best[0] = best[1];
		best[1] = best[2];
		p1eval (best[2], best[1].i * 2);
	}

/* Find the best B2.  We use a binary-like search to speed things up (new in version 30.3b3). */

	for ( ; ; ) {
		struct pm1_stage2_efficiency midpoint;

		// Quit when both gaps are too small to have a midpoint
		int	lower_gap = best[1].i - best[0].actual_i;
		int	upper_gap = best[2].i - best[1].actual_i;
		if (lower_gap < 2 && upper_gap < 2) break;

		// Work on the bigger of the lower section and upper section
		if (lower_gap > upper_gap) {					// Work on lower section
			p1eval (midpoint, best[0].actual_i + lower_gap / 2);
			if (p1compare (midpoint, best[1])) {			// Make middle the new end point
				best[2] = best[1];
				best[1] = midpoint;
			} else {						// Create new start point
				best[0] = midpoint;
			}
		} else {							// Work on upper section
			p1eval (midpoint, best[1].actual_i + upper_gap / 2);
			if (!p1compare (best[1], midpoint)) {			// Make middle the new start point
				best[0] = best[1];
				best[1] = midpoint;
			} else {						// Create new end point
				best[2] = midpoint;
			}
		}
	}

/* Return the best B2 and message to output */

	pm1data->C = best[1].actual_i * pm1data->B;
	if (saved_sieve_depth == 0.0) {
		sprintf (optimal_B2_msg, "With unknown amount of trial factoring done, guessing optimal B2 is %d*B1 = %" PRIu64 ".\n", best[1].actual_i, pm1data->C);
	} else {
		sprintf (optimal_B2_msg, "With trial factoring done to 2^%g, optimal B2 is %d*B1 = %" PRIu64 ".\n", saved_sieve_depth, best[1].actual_i, pm1data->C);
		sprintf (optimal_B2_msg + strlen (optimal_B2_msg), "If no prior P-1, chance of a new factor is %.3g%%\n", best[1].fac_pct * 100.0);
	}

/* Restore the sieve depth */

	pm1data->w->sieve_depth = saved_sieve_depth;

/* Return success */

	return (TRUE);
}
#undef p1eval
#undef B1increase
#undef p1compare

/* Choose the best implementation of P-1 stage 2 given the current memory settings. */
/* We choose the best value for D that reduces the number of multiplications with the current memory constraints. */
/* Return the cost (in terms of stage 1 squarings) of the selected stage 2 implementation. */

double pm1_stage2_impl (
	pm1handle *pm1data,
	unsigned int memory,		/* Available memory is in MB */
	int	forced_stage2_type,	/* 0 = cost pairing, 1 = cost poly, 99 = cost both */
	char	*msgbuf)		/* Messages to output if we end up using this implementation plan */
{
	uint64_t numvals;		/* Number of gwnums we can allocate */
	struct pm1_stage2_cost_data cost_data;
	bool	B2_was_chosen = FALSE;

/* Compute the number of gwnum temporaries we can allocate.  Assume the binary values of x and 1/x needed for save file creation consume about a half a gwnum. */

	numvals = cvt_mem_to_array_gwnums_adj (&pm1data->gwdata, memory, -0.5);
	if (numvals < 8) numvals = 8;
	if (QA_TYPE) numvals = QA_TYPE;			/* Optionally override numvals for QA purposes */

/* Set first_relocatable for future best_stage2_impl calls that cost prime pairing. */
/* Override B2 with optimal B2 based on amount of memory available. */

	if (pm1data->state == PM1_STATE_MIDSTAGE) {
		if (pm1data->C_done == pm1data->B) {
			pm1data->first_relocatable = pm1data->first_C_start;
			pm1data->last_relocatable = 0;
			if (pm1data->optimal_B2) {
				if (!pm1_choose_B2 (pm1data, numvals, forced_stage2_type, msgbuf + strlen (msgbuf))) return (0.0);
				B2_was_chosen = TRUE;
			}
		} else {
			pm1data->first_relocatable = pm1data->C_done;
			pm1data->last_relocatable = 0;
		}
	}
	if (pm1data->state >= PM1_STATE_STAGE2 && pm1data->stage2_type == PM1_STAGE2_POLYMULT) {
		pm1data->first_relocatable = pm1data->C_done > pm1data->B ? pm1data->C_done : pm1data->B;
		pm1data->last_relocatable = 0;
	}

/* If are continuing from a save file that was in stage 2, check to see if we currently have enough memory to continue with the save file's */
/* stage 2 implementation.  Also check if we now have significantly more memory available and stage 2 is not near complete such that a new */
/* stage 2 implementation might give us a faster stage 2 completion. */

//GW: These are rather arbitrary heuristics
	if (pm1data->state >= PM1_STATE_STAGE2 &&				// Continuing a stage 2 save file and
	    pm1data->stage2_type == PM1_STAGE2_PAIRING &&			// prime pairing and
	    pm1data->pairmap != NULL &&						// we have a pairmap and
	    numvals >= pm1data->stage2_numvals &&				// we have enough memory and
	    forced_stage2_type != 1 &&						// not forcing polymult stage 2
	    (numvals < pm1data->stage2_numvals * 2 ||				// less than twice as much memory now available or
	     pm1data->Dsection >= pm1data->numDsections / 2))			// stage 2 more than half done
		return (1.0);							// Use old plan (without costing it)

/* If we are contemplating ditching the save file pairmap, figure out which non-relocatable primes are definitely included in the stage 2 */
/* accumulator.  Set C_done appropriately, but do not change first_relocatable as there is no guarantee which relocatables are in the accumulator. */

	if (pm1data->state == PM1_STATE_STAGE2 && pm1data->Dsection) {
		int max_relp_set = (pm1data->stage2_type == PM1_STAGE2_PAIRING) ? get_max_relp_set (pm1data->relp_sets) : 0;
		if (pm1data->Dsection > max_relp_set) pm1data->C_done = pm1data->B2_start + (pm1data->Dsection - max_relp_set) * pm1data->D;
	}

/* Find the least costly stage 2 plan.  It is possible no plan will be found (user wants polymult only and there are no safe polys for this FFT length). */
/* Try various values of D until we find the best one.  Pairing requires V,Vn,Vn1,gg gwnums, polymult requires diff1,r^2,gg gwnums. */

	cost_data.c.total_cost = 0.0;						// Special value indicating no workable plan found
	pm1_stage2_impl_given_numvals (pm1data, numvals, forced_stage2_type, &cost_data);

/* If we are continuing from a save file that was in stage 2 and the new plan doesn't look significant better than the old plan, then */
/* we use the old plan and its partially completed pairmap. */

	if (pm1data->state >= PM1_STATE_STAGE2 &&				// Continuing a stage 2 save file and
	    pm1data->stage2_type == PM1_STAGE2_PAIRING &&			// prime pairing and
	    pm1data->pairmap != NULL &&						// we have a pairmap and
	    forced_stage2_type != 1 &&						// not forcing polymult stage 2
	    numvals >= pm1data->stage2_numvals &&				// we have enough memory and
	    (cost_data.c.total_cost == 0.0 ||					// there is no new plan
	     cost_data.c.stage2_numvals < pm1data->stage2_numvals * 2))		// or new plan does not use significantly more memory
		return (1.0);							// Use old plan (without costing it)

/* If we are continuing from a save file that was in stage 2, toss the save file's pairing map. */

	if (pm1data->state >= PM1_STATE_STAGE2) {
		free (pm1data->pairmap);
		pm1data->pairmap = NULL;
		pm1data->pairmap_size = 0;
	}

/* Return zero efficiency to indicate no stage 2 plan found (forcing polymult and nothing passes safety margins) */

	if (cost_data.c.total_cost == 0.0) return (0.0);

/* Set all the variables needed for this stage 2 plan */

	pm1data->stage2_numvals = cost_data.c.stage2_numvals;
	pm1data->D = cost_data.c.D;
	pm1data->numrels = cost_data.c.numrels;
	pm1data->B2_start = cost_data.c.B2_start;
	pm1data->numDsections = cost_data.c.numDsections;
	pm1data->Dsection = 0;
	if (pm1data->state == PM1_STATE_MIDSTAGE || pm1data->last_relocatable > pm1data->B2_start)
		pm1data->last_relocatable = (pm1data->B2_start > pm1data->C_done ? pm1data->B2_start : pm1data->C_done);
	pm1data->stage2_type = cost_data.stage2_type;
	if (pm1data->stage2_type == PM1_STAGE2_PAIRING) {
		pm1data->totrels = cost_data.c.totrels;
		memcpy (pm1data->relp_sets, cost_data.c.relp_sets, sizeof (pm1data->relp_sets));
		pm1data->max_pairmap_Dsections = cost_data.c.max_pairmap_Dsections;
	} else {
		pm1data->poly1_size = cost_data.c.numrels;
		pm1data->poly2_size = cost_data.poly2_size;
	}

/* Output data regarding our cost estimates so user can make adjustments when our estimates are wildly off the mark */

	// Output additional debugging upon request
	if (IniGetInt (INI_FILE, "Stage2Estimates", 0)) {
		if (pm1data->stage2_type == PM1_STAGE2_PAIRING)
			sprintf (msgbuf + strlen (msgbuf), "Est. pair%%: %5.2f, init transforms: %.0f, main loop transforms: %.0f\n",
				 cost_data.c.est_pair_pct * 100.0, cost_data.c.est_init_transforms, cost_data.c.est_stage2_transforms);
		else
			sprintf (msgbuf + strlen (msgbuf), "Est. init transforms: %.0f, main loop transforms: %.0f, init poly cost: %.0f, main loop poly cost: %.0f\n",
				 cost_data.c.est_init_transforms, cost_data.c.est_stage2_transforms, cost_data.c.est_init_polymult, cost_data.c.est_stage2_polymult);
	}
	// Output estimated runtime of stage 2 vs. stage 1.  The better the accuracy, the better our deduced optimal B2 value and the
	// better we can judge pairing vs. polymult crossover.  User can create ratio adjustments if necessary.
	sprintf (msgbuf + strlen (msgbuf), "Estimated stage 2 vs. stage 1 runtime ratio: %.3f\n", cost_data.c.est_stage2_stage1_ratio);
	pm1data->est_stage2_stage1_ratio = cost_data.c.est_stage2_stage1_ratio;

	// Return stage 2 plan's efficiency -- this is fine for PFactor and Pminus1 where the target B2 is given.  When choosing optimal B2 a different
	// metric is needed.  KingKurly noticed that Pminus1=1,2,1894787,-1,25000000,0,69 using 10GB of memory was choosing a poor B2 value.
	// FFT: 102400, B2: 125000000/125016810, numvals: 459/13411, poly: 64x400, efficiency: 10631
	// FFT: 114688, B2: 90525000000/90538918470, numvals: 11791/11791, poly: 2880x9307, efficiency: 9342
	// With trial factoring done to 2^69, optimal B2 is 5*B1 = 125000000.
	// The problem is the caller is comparing how efficiently factors are found rather than maximizing the chance of finding a factor.  Unlike ECM where
	// one can run another maximally efficient curve, in P-1 we want to make the most of our one chance at running P-1.
	return (B2_was_chosen ? cost_data.factor_probability : cost_data.c.efficiency);
}


/* Recursively compute exp used in initial 3^exp calculation of a P-1 factoring run.  We include 2*n in exp, since P-1 factoring */
/* benefits from Mersenne number factors being 1 mod 2n.  Generalized Fermats also benefit in P-1 factoring. */

void calc_exp (
	void	*sieve_info,	/* Sieve to use calculating primes */
	double	k,		/* K in K*B^N+C */
	unsigned long b,	/* B in K*B^N+C */
	unsigned long n,	/* N in K*B^N+C */
	signed long c,		/* C in K*B^N+C */
	mpz_t	g,		/* Variable to accumulate multiplied small primes */
	uint64_t B1,		/* P-1 stage 1 bound */
	uint64_t *p,		/* Variable to fetch next small prime into */
	uint64_t lower,
	uint64_t upper)
{
	uint64_t len;

/* Compute the number of result bits we are to calculate */

	len = upper - lower;

/* Use recursion to compute the exponent.  This will perform better because mpz_mul will be handling arguments of equal size. */

	if (len >= 1024) {
		mpz_t	x;
		calc_exp (sieve_info, k, b, n, c, g, B1, p, lower, lower + (len >> 1));
		mpz_init (x);
		calc_exp (sieve_info, k, b, n, c, x, B1, p, lower + (len >> 1), upper);
		mpz_mul (g, x, g);
		mpz_clear (x);
		return;
	}

/* For Mersenne numbers, 2^n-1, make sure we include 2n in the calculated exponent (since factors */
/* are of the form 2kn+1).  For generalized Fermat numbers, b^n+1 (n is a power of 2), make sure n */
/* is included in the calculated exponent as factors are of the form kn+1 (actually forum posters */
/* have pointed out that Fermat numbers should include 4n and generalized Fermat should include 2n). */
/* Heck, maybe other forms may also need n included, so just always include 2n -- it is very cheap. */

	if (lower == 0) mpz_set_ui (g, 2*n);
	else mpz_set_ui (g, 1);

/* Find all the primes in the range and use as many powers as possible */

	for ( ; *p <= B1 && mpz_sizeinbase (g, 2) < len; *p = sieve (sieve_info)) {
		uint64_t val, max;
		val = *p;
		max = B1 / *p;
		while (val <= max) val *= *p;
		if (sizeof (unsigned int) == 4 && val > 0xFFFFFFFF) {
			mpz_t	mpz_val;
			mpz_init_set_d (mpz_val, (double) val);		/* Works for B1 up to 2^53 */
			mpz_mul (g, g, mpz_val);
			mpz_clear (mpz_val);
		} else
			mpz_mul_ui (g, g, (unsigned int) val);
	}
}

/* Recursively compute more exp used in computing stage_0_result^exp of a P-1 factoring run */

void calc_exp2 (
	void	*sieve_info,	/* Sieve to use calculating primes */
	mpz_t	g,		/* Variable to accumulate multiplied small primes */
	uint64_t B1_completed,	/* P-1 stage 1 bound already completed */
	uint64_t B1,		/* P-1 stage 1 bound */
	uint64_t *p,		/* Variable to fetch next small prime into */
	uint64_t len)		/* Approximate number of bits to gather */
{

/* Use recursion to compute the exponent.  This should perform better because mpz_mul will be handling arguments of equal size. */

	if (len >= 256) {
		mpz_t	x;
		calc_exp2 (sieve_info, g, B1_completed, B1, p, len >> 1);
		mpz_init (x);
		calc_exp2 (sieve_info, x, B1_completed, B1, p, len >> 1);
		mpz_mul (g, x, g);
		mpz_clear (x);
		return;
	}

/* Find all the primes in the range and use as many powers as possible */

	mpz_set_ui (g, 1);
	for ( ; *p <= B1 && mpz_sizeinbase (g, 2) < len; *p = sieve (sieve_info)) {
		uint64_t no_more_vals_above_here = B1 / *p;
		for (uint64_t val = *p; ; val *= *p) {
			if (val > B1_completed) {
				if (sizeof (unsigned int) == 4 && *p > 0xFFFFFFFF) {
					mpz_t	mpz_val;
					mpz_init_set_d (mpz_val, (double) *p);		/* Works for B1 up to 2^53 */
					mpz_mul (g, g, mpz_val);
					mpz_clear (mpz_val);
				} else
					mpz_mul_ui (g, g, (unsigned int) *p);
			}
			if (val > no_more_vals_above_here) break;
		}
	}
}

/* Helper routine for multithreading P-1 stage 2 */

#define PM1_COMPUTE_POLY2	1
#define PM1_ACCUMULATING_GG	2

void pm1_helper (
	int	helper_num,	// 0 = main thread, 1+ = helper thread num
	gwhandle *gwdata,	// Single-threaded, thread-safe gwdata (probably cloned) to use
	void	*info)
{
	pm1handle *pm1data = (pm1handle *) info;

// Generate the next poly #2 coefficients using differences.  The highest coefficient and next diff1 is already computed.

	if (pm1data->helper_work == PM1_COMPUTE_POLY2) {
	    // The following code was originally written assuming each helper thread would do a chunk of work.  However, we
	    // cannot rely on the OS starting every helper thread.  Use atomic helper_counter to make sure all the work gets done.
	    for ( ; ; ) {
		helper_num = (int) atomic_fetch_incr (pm1data->polydata.helper_counter);
		if (helper_num >= pm1data->helper_count) break;
//GW:  Rename helper_count to numpoly2blks.  rename helper_num to blknum.
//GW: Would we thread better with more numerous smaller work chunks?  Can we avoid the single-threaded computation of diff1 values prior to calling PM1_COMPUTE_POLY2?
		gwnum diff1 = pm1data->poly2[(pm1data->remaining_poly2_size - 1 - helper_num) % pm1data->helper_count];	// The pre-computed dist1 is stored here
		gwnum diff2 = pm1data->r_2helper2;		// r^(2*helper_count^2)
//GW: handle no work to do case (more helpers than work to do)?
		for (uint64_t j = pm1data->remaining_poly2_size - 1 - helper_num; ; j -= pm1data->helper_count) {
			// Compute next poly2 coefficient
			gwmul3 (gwdata, pm1data->poly2[j+pm1data->helper_count], diff1, pm1data->poly2[j], GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			if (j < pm1data->helper_count) break;
			// Compute next diff1, save the very last diff1 for next polymult
			if (j == pm1data->helper_count) gwmul3 (gwdata, diff1, diff2, pm1data->diff1, GWMUL_STARTNEXTFFT), diff1 = pm1data->diff1;
			else gwmul3 (gwdata, diff1, diff2, diff1, GWMUL_STARTNEXTFFT);
		}
	    }
	}

/* Accumulate output poly results into gg for later GCD */

	else if (pm1data->helper_work == PM1_ACCUMULATING_GG) {
		gwnum	accumulator = NULL;

		// Loop multiplying points from the output poly
		for ( ; ; ) {
			// Get one of the output poly's gwnums.  We will accumulate here and later multiply with gg.
			uint64_t point = atomic_fetch_incr (pm1data->polydata.helper_counter);

			// If the other helper threads have completed processing of the output poly, then we're done
			if (point >= pm1data->num_points) break;

			// Unfft the gwnum
			if (polymult_must_unfft (gwdata, pm1data->points[point]))
				gwunfft2 (gwdata, pm1data->points[point], pm1data->points[point], GWMUL_STARTNEXTFFT);

			// Either set the accumulator or multiply with the accumulator
			if (accumulator == NULL) accumulator = pm1data->points[point];
			else gwmul3 (gwdata, pm1data->points[point], accumulator, accumulator, GWMUL_STARTNEXTFFT);
		}

		// Now apply the accumulator to gg
		if (accumulator != NULL) {
			gwfft (gwdata, accumulator, accumulator);
			gwmutex_lock (&pm1data->polydata.poly_mutex);
			gwmul3 (gwdata, accumulator, pm1data->gg, pm1data->gg, GWMUL_STARTNEXTFFT);
//GW: (j == num_points-1 && (saving || stop_reason)) ? 0 : GWMUL_STARTNEXTFFT);
//GW: re-check for saving/stop prior to last gg multiply?   
			gwmutex_unlock (&pm1data->polydata.poly_mutex);
		}
	}
}


/*****************************************************************************/
/*                         Main P-1 routine				     */
/*****************************************************************************/

int pminus1 (
	int	thread_num,
	struct PriorityInfo *sp_info,	/* SetPriority information */
	struct work_unit *w)
{
	pm1handle pm1data;
	giant	N;		/* Number being factored */
	giant	factor;		/* Factor found, if any */
	mpz_t	exp;
	int	exp_initialized;
	uint64_t stage0_limit;
	unsigned int memused;
	int	i, stage1_batch_size, stage1_mem, stage1_temps;
	unsigned long len;
	int	maxerr_restart_count = 0;
	unsigned long maxerr_fftlen = 0;
	char	filename[32], buf[255], JSONbuf[4000], testnum[100];
	int	pminus1_base, res, stop_reason, saving, near_fft_limit, echk;
	double	one_over_len, one_over_B, base_pct_complete, one_relp_pct;
	uint64_t last_output, last_output_t, last_output_r;
	double	allowable_maxerr, output_frequency, output_title_frequency;
	int	first_iter_msg;
	int	msglen;
	char	*str, *msg;
	double	stage1_timer, stage2_timer, timers[2];
	relp_set_data_map relp_set_map;
	Dmultiple_data_map Dmultiple_map;

/* Output a blank line to separate multiple P-1 runs making the result more readable */

	OutputStr (thread_num, "\n");

/* Clear pointers to allocated memory (so common error exit code knows what to free) */

	N = NULL;
	factor = NULL;
	str = NULL;
	msg = NULL;
	exp_initialized = FALSE;

/* Begin initializing P-1 data structure */
/* Choose a default value for the second bound if none was specified */

	pminus1_base = (w->b != 3 ? 3 : 5);
	memset (&pm1data, 0, sizeof (pm1handle));
	pm1data.thread_num = thread_num;
	pm1data.stage1_threads = get_worker_num_threads (thread_num, HYPERTHREAD_LL);
	pm1data.stage2_threads = pm1data.stage1_threads + IniGetInt (INI_FILE, "Stage2ExtraThreads", 0);
	pm1data.w = w;
	pm1data.B = (uint64_t) w->B1;
	pm1data.C = (uint64_t) w->B2;
	if (pm1data.B < 90) {
		OutputStr (thread_num, "Using minimum bound #1 of 90\n");
		pm1data.B = 90;
	}
	if (pm1data.C == 0) pm1data.C = pm1data.B * 100;
	if (pm1data.C < pm1data.B) pm1data.C = pm1data.B;
	pm1data.first_C_start = (uint64_t) w->B2_start;
	if (pm1data.first_C_start < pm1data.B) pm1data.first_C_start = pm1data.B;
	pm1data.pct_mem_to_use = 1.0;				// Use as much memory as we can unless we get allocation errors

/* Decide if we will calculate an optimal B2 when stage 2 begins.  We do this by default for P-1 work where we know how much TF has been done and like ECM */
/* we do this for P-1 work where TF is not known and B2 = 100*B1.  You might think WORK_PFACTOR should also set optimal_B2, but PFACTOR optimizes PRP test */
/* time saved whereas optimal_B2 maximizes factors found over time. */

	pm1data.optimal_B2 = (!QA_IN_PROGRESS && w->work_type == WORK_PMINUS1 && pm1data.first_C_start == pm1data.B && IniGetInt (INI_FILE, "Pminus1BestB2", 1) &&
			      (w->sieve_depth > 50 || (w->sieve_depth == 0 && pm1data.C == 100 * pm1data.B)));
	if (pm1data.optimal_B2 && pm1data.C <= pm1data.B) pm1data.C = 100 * pm1data.B;	// A guess to use for calling start_sieve_with_limit

/* Compute the number we are factoring */

	stop_reason = setN (thread_num, w, &N);
	if (stop_reason) goto exit;

/* Output startup message, but only if work type is P-1.  Pfactor work type has already output a startup message. */

	gw_as_string (testnum, w->k, w->b, w->n, w->c);
	sprintf (buf, "%s P-1", testnum);
	title (thread_num, buf);
	if (w->work_type == WORK_PMINUS1) {
		if (pm1data.C <= pm1data.B)
			sprintf (buf, "P-1 on %s with B1=%" PRIu64 "\n", testnum, pm1data.B);
		else if (pm1data.optimal_B2)
			sprintf (buf, "P-1 on %s with B1=%" PRIu64 ", B2=TBD\n", testnum, pm1data.B);
		else
			sprintf (buf, "P-1 on %s with B1=%" PRIu64 ", B2=%" PRIu64 "\n", testnum, pm1data.B, pm1data.C);
		OutputStr (thread_num, buf);
		if (w->sieve_depth > 0.0 && !pm1data.optimal_B2) {
			double prob = guess_pminus1_probability (w);
			sprintf (buf, "Chance of finding a new factor is an estimated %.3g%%\n", prob * 100.0);
			OutputStr (thread_num, buf);
		}
	}

/* Init filename.  This is a little kludgy as we want to generate a P-1 save file that does not conflict with an LL or PRP save file name. */
/* Both save files can exist at the same time when stage 2 is delayed waiting for more memory. */

restart:
	tempFileName (w, filename);
	filename[0] = 'm';

/* Perform setup functions.  This includes decding how big an FFT to use, allocating memory, calling the FFT setup code, etc. */

/* Setup the assembly code */

	gwinit (&pm1data.gwdata);
	gwset_maxmulbyconst (&pm1data.gwdata, pminus1_base);
	if (IniGetInt (INI_FILE, "UseLargePages", 0)) gwset_use_large_pages (&pm1data.gwdata);
	if (HYPERTHREAD_LL) sp_info->normal_work_hyperthreading = TRUE, gwset_will_hyperthread (&pm1data.gwdata, 2);
	gwset_bench_cores (&pm1data.gwdata, HW_NUM_CORES);
	gwset_bench_workers (&pm1data.gwdata, NUM_WORKERS);
	if (ERRCHK) gwset_will_error_check (&pm1data.gwdata);
	else gwset_will_error_check_near_limit (&pm1data.gwdata);
	gwset_num_threads (&pm1data.gwdata, pm1data.stage1_threads);
	gwset_thread_callback (&pm1data.gwdata, SetAuxThreadPriority);
	gwset_thread_callback_data (&pm1data.gwdata, sp_info);
	gwset_safety_margin (&pm1data.gwdata, IniGetFloat (INI_FILE, "ExtraSafetyMargin", 0.0));
	gwset_larger_fftlen_count (&pm1data.gwdata, maxerr_restart_count < 3 ? maxerr_restart_count : 3);
	gwset_minimum_fftlen (&pm1data.gwdata, w->minimum_fftlen);
	gwset_using_polymult (&pm1data.gwdata);
	gwset_use_spin_wait (&pm1data.gwdata, IniGetInt (INI_FILE, "SpinWait", 0));
	res = gwsetup (&pm1data.gwdata, w->k, w->b, w->n, w->c);
	if (res) {
		sprintf (buf, "Cannot initialize FFT code, errcode=%d\n", res);
		OutputBoth (thread_num, buf);
		return (STOP_FATAL_ERROR);
	}
	gwerror_checking (&pm1data.gwdata, ERRCHK);
	pm1data.stage1_fftlen = gwfftlen (&pm1data.gwdata);

/* More miscellaneous initializations */

	stage1_timer = stage2_timer = 0.0;
	last_output = last_output_t = last_output_r = 0;
	gw_clear_fft_count (&pm1data.gwdata);
	first_iter_msg = TRUE;
	calc_output_frequencies (&pm1data.gwdata, &output_frequency, &output_title_frequency);
	clear_timers (timers, 2);

/* Output message about the FFT length chosen */

	{
		char	fft_desc[200];
		gwfft_description (&pm1data.gwdata, fft_desc);
		sprintf (buf, "Using %s\n", fft_desc);
		OutputStr (thread_num, buf);
	}

/* If we are near the maximum exponent this fft length can test, then we will roundoff check all multiplies */

	near_fft_limit = exponent_near_fft_limit (&pm1data.gwdata);
	gwerror_checking (&pm1data.gwdata, ERRCHK || near_fft_limit);

/* Figure out the maximum round-off error we will allow.  By default this is 28/64 when near the FFT limit and 26/64 otherwise. */
/* We've found that this default catches errors without raising too many spurious error messages.  The user can override this default. */
/* Since stage 2 may aggressively push EXTRA_BITS with gwsubmul4, assume we can always get large-ish round off errors */

	allowable_maxerr = IniGetFloat (INI_FILE, "MaxRoundoffError", (float) 0.4375);

/* Check for a save file and read the save file.  If there is an error */
/* reading the file then restart the P-1 factoring job from scratch. */
/* Limit number of backup files we try */
/* to read in case there is an error deleting bad save files. */

	readSaveFileStateInit (&pm1data.read_save_file_state, thread_num, filename);
	writeSaveFileStateInit (&pm1data.write_save_file_state, filename, 0);
	for ( ; ; ) {
		if (! saveFileExists (&pm1data.read_save_file_state)) {
			/* If there were save files, they are all bad.  Report a message */
			/* and temporarily abandon the work unit.  We do this in hopes that */
			/* we can successfully read one of the bad save files at a later time. */
			/* This sounds crazy, but has happened when OSes get in a funky state. */
			if (pm1data.read_save_file_state.a_non_bad_save_file_existed) {
				OutputBoth (thread_num, ALLSAVEBAD_MSG);
				return (0);
			}
			/* No save files existed, start from scratch. */
			break;
		}

		if (!pm1_restore (&pm1data)) {
			/* Close and rename the bad save file */
			saveFileBad (&pm1data.read_save_file_state);
			continue;
		}

/* Handle stage 0 save files.  If the B values do not match we use the bound from the save file for now.  Later we'll do more B if necessary. */

		if (pm1data.state == PM1_STATE_STAGE0) {
			if (pm1data.interim_B > pm1data.B) {
				pm1data.B = pm1data.interim_B;
				sprintf (buf, "Ignoring suggested B1 value, using B1=%" PRIu64 " from the save file\n", pm1data.B);
				OutputStr (thread_num, buf);
			}
			goto restart0;
		}

/* Handle stage 1 save files.  If the save file that had a higher B1 target then we can reduce the target B1 to the desired B1. */

		if (pm1data.state == PM1_STATE_STAGE1) {
			if (pm1data.interim_B > pm1data.B) pm1data.interim_B = pm1data.B;
			goto restart1;
		}

/* Handle between stages save files */

		if (pm1data.state == PM1_STATE_MIDSTAGE) {
			if (pm1data.B > pm1data.B_done) {
				free (pm1data.gg_binary), pm1data.gg_binary = NULL;
				goto more_B;
			}
			goto restart3b;
		}

/* Handle stage 2 save files */

		if (pm1data.state == PM1_STATE_STAGE2) {

/* If B is larger than the one in the save file, then do more stage 1 processing.  Since this is very upsetting to */
/* an LL/PRP tester that has already begun stage 2 only do this for the non-LL/PRP tester. */

			if (pm1data.B > pm1data.B_done && w->work_type == WORK_PMINUS1) {
				free (pm1data.gg_binary), pm1data.gg_binary = NULL;
				free (pm1data.pairmap), pm1data.pairmap = NULL;
				goto more_B;
			}

/* If B is different than the one in the save file, then use the one in the save file rather than discarding all the work done thusfar in stage 2. */

			if (pm1data.B != pm1data.B_done) {
				pm1data.B = pm1data.B_done;
				sprintf (buf, "Ignoring suggested B1 value, using B1=%" PRIu64 " from the save file\n", pm1data.B);
				OutputStr (thread_num, buf);
			}

/* If LL testing and bound #2 has changed then use the original bound #2. */
/* If explicit P-1 testing and bound #2 is larger in the save file then use the original bound #2. */
/* The user doing explicit P-1 testing that wants to discard the stage 2 work he has done thusfar */
/* and reduce the stage 2 bound must manually delete the save file. */

			if ((w->work_type != WORK_PMINUS1 && pm1data.C != pm1data.interim_C) ||
			    (w->work_type == WORK_PMINUS1 && pm1data.C < pm1data.interim_C)) {
				pm1data.C = pm1data.interim_C;
				sprintf (buf, "Ignoring suggested B2 value, using B2=%" PRIu64 " from the save file\n", pm1data.C);
				OutputStr (thread_num, buf);
			}

/* Resume stage 2 */

			if (pm1data.optimal_B2) {
				pm1data.C = pm1data.interim_C;
				sprintf (buf, "Resuming P-1 in stage 2 with B2=%" PRIu64 "\n", pm1data.interim_C);
				OutputStr (thread_num, buf);
			}
			goto restart3b;
		}

/* Handle stage 2 GCD save files */

		if (pm1data.state == PM1_STATE_GCD) {
			if (pm1data.optimal_B2) pm1data.C = pm1data.C_done;
			goto restart4;
		}

/* Handle case where we have a completed save file (the PM1_STATE_DONE state) */
/* Note: if first_C_start != B then the user is using the undocumented feature of doing stage 2 in pieces.  Assume he knows what he is doing. */

		ASSERTG (pm1data.state == PM1_STATE_DONE);
		if (pm1data.B > pm1data.B_done) goto more_B;
		pm1data.B = pm1data.B_done;
		if (pm1data.C > pm1data.C_done) {
			pm1data.state = PM1_STATE_STAGE1;		// We're not in mid-stage until invx is calculated
			if (pm1data.first_C_start == pm1data.B) pm1data.first_C_start = pm1data.C_done;
			if (pm1data.C_done > pm1data.B) {
				sprintf (buf, "Resuming P-1 in stage 2 with B2 from %" PRIu64 " to %" PRIu64 "\n", pm1data.C_done, pm1data.C);
				OutputStr (thread_num, buf);
			}
			goto restart3a;
		}

/* The save file indicates we've tested to these bounds already */

		sprintf (buf, "%s already tested to B1=%" PRIu64 " and B2=%" PRIu64 ".\n",
			 gwmodulo_as_string (&pm1data.gwdata), pm1data.B_done, pm1data.C_done);
		OutputBoth (thread_num, buf);
		goto done;
	}

/* Start this P-1 run from scratch starting with x = 3.  Batalov reported P-1 issues doing P-1 on 3^524288+1.  The final result of stage 1 is one */
/* because 3^(2^20) mod 3^524288+1 is one.  So, for base 3 numbers we will start with x = 5. */

	strcpy (w->stage, "S1");
	w->pct_complete = 0.0;
	start_timer_from_zero (&stage1_timer, 0);
	pm1data.state = PM1_STATE_STAGE0;
	pm1data.interim_B = pm1data.B;
	pm1data.stage0_bitnum = 0;
	pm1data.x = gwalloc (&pm1data.gwdata);
	if (pm1data.x == NULL) goto oom;
	dbltogw (&pm1data.gwdata, pminus1_base, pm1data.x);

/* Stage 0 pre-calculates an exponent that is the product of small primes.  Our default uses only small */
/* primes below 250,000,000 (roughly 375 million bits).  This is configurable starting in version 29.8 build 8. */
/* Reports of GMP having trouble with products larger than 2^32 bits leads us to cap MaxStage0Prime at 2000. */

	pm1data.max_stage0_prime = (uint64_t) IniGetInt (INI_FILE, "MaxStage0Prime", 250);
	if (pm1data.max_stage0_prime < 1) pm1data.max_stage0_prime = 1;
	if (pm1data.max_stage0_prime > 2000) pm1data.max_stage0_prime = 2000;
	pm1data.max_stage0_prime *= 1000000;

/* First restart point.  Compute the big exponent (product of small primes).  Then compute 3^exponent. */
/* The exponent always contains 2*p.  We only use primes below (roughly) max_stage0_prime.  The rest of the */
/* exponentiation will be done one prime at a time in the second part of stage 1. */
/* This stage uses 2 transforms per exponent bit. */

restart0:
	pm1data.B_done = 0;
	set_memory_usage (thread_num, 0, cvt_gwnums_to_mem (&pm1data.gwdata, 1));
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);
	stop_reason = start_sieve_with_limit (thread_num, 2, (uint32_t) sqrt ((double) pm1data.B), &pm1data.sieve_info);
	if (stop_reason) goto exit;
	pm1data.stage1_prime = sieve (pm1data.sieve_info);
	stage0_limit = (pm1data.interim_B > pm1data.max_stage0_prime) ? pm1data.max_stage0_prime : pm1data.interim_B;
	mpz_init (exp);  exp_initialized = TRUE;
	calc_exp (pm1data.sieve_info, w->k, w->b, w->n, w->c, exp, pm1data.interim_B, &pm1data.stage1_prime, 0, stage0_limit * 3 / 2);

/* Find number of bits, ignoring the most significant bit.  Init variables used in calculating percent complete. */

	len = (unsigned long) mpz_sizeinbase (exp, 2) - 1;
	one_over_len = 1.0 / (double) len;
	if (pm1data.stage1_prime < pm1data.B) one_over_len *= (double) pm1data.stage1_prime / (double) pm1data.B;

/* Now take the exponent and raise x to that power */

	gwsetmulbyconst (&pm1data.gwdata, pminus1_base);
	while (pm1data.stage0_bitnum < len) {

/* Set various flags.  They control whether error-checking or the next FFT can be started. */

		stop_reason = stopCheck (thread_num);
		saving = testSaveFilesFlag (thread_num);
		echk = stop_reason || saving || ERRCHK || near_fft_limit || ((pm1data.stage0_bitnum & 127) == 64);
		gwerror_checking (&pm1data.gwdata, echk);

/* Either square x or square x and multiply it by three. */

#ifndef SERVER_TESTING
		int options = (!stop_reason && !saving && pm1data.stage0_bitnum+1 != len) ? GWMUL_STARTNEXTFFT : 0;
		if (mpz_tstbit64 (exp, len - pm1data.stage0_bitnum - 1)) options |= GWMUL_MULBYCONST;
		int option = (!stop_reason && !saving && pm1data.stage0_bitnum+1 != len) ? GWMUL_STARTNEXTFFT : 0;
		if (pm1data.stage0_bitnum < 30) gwmul3_carefully (&pm1data.gwdata, pm1data.x, pm1data.x, pm1data.x, options);
		else gwsquare2 (&pm1data.gwdata, pm1data.x, pm1data.x, options);
#endif

/* Test for an error */

		if (gw_test_for_error (&pm1data.gwdata) || gw_get_maxerr (&pm1data.gwdata) > allowable_maxerr) goto err;

/* Calculate our stage 1 percentage complete */

		pm1data.stage0_bitnum++;
		w->pct_complete = (double) pm1data.stage0_bitnum * one_over_len;

/* Output the title every so often */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s P-1 stage 1",
				 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pm1data.gwdata));
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&pm1data.gwdata);
		}

/* Every N squarings, output a progress report */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s stage 1 is %.*f%% complete.",
				 gwmodulo_as_string (&pm1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, " Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			if (pm1data.stage0_bitnum > 1)
				OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&pm1data.gwdata);
			first_iter_msg = FALSE;
		}

/* Every N squarings, output a progress report to the results file */

		if ((ITER_OUTPUT_RES != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output_r + 2 * ITER_OUTPUT_RES) ||
		    (NO_GUI && stop_reason)) {
			sprintf (buf, "%s stage 1 is %.*f%% complete.\n",
				 gwmodulo_as_string (&pm1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			writeResults (buf);
			last_output_r = gw_get_fft_count (&pm1data.gwdata);
		}

/* Check for escape and/or if its time to write a save file */

		if (stop_reason || saving) {
			pm1_save (&pm1data);
			if (stop_reason) goto exit;
		}
	}

/* Do stage 0 cleanup */

	gwerror_checking (&pm1data.gwdata, ERRCHK || near_fft_limit);
	mpz_clear (exp), exp_initialized = FALSE;
	end_timer (timers, 0);
	end_timer (timers, 1);

/* Set up sieving tart point for next section */

	pm1data.stage1_prime--;				// Stage1_prime was not included by calc_exp.  Back up one for next sieve call.

/* Do the larger primes of stage 1.  This stage uses 2+ transforms per exponent bit using a sliding window exponentiate.  Proceed until interim_B is finished. */

restart1:
	strcpy (w->stage, "S1");
	one_over_B = 1.0 / (double) pm1data.B;
	w->pct_complete = (double) pm1data.stage1_prime * one_over_B;
	start_timer (timers, 0);
	start_timer (timers, 1);
	pm1data.state = PM1_STATE_STAGE1;
	stop_reason = start_sieve_with_limit (thread_num, pm1data.stage1_prime, (uint32_t) sqrt((double) pm1data.B), &pm1data.sieve_info);
	if (stop_reason) goto exit;

/* Loop doing small batches of primes until interim_B is finished.  The batch size needs to be limited in size as exponentiate is uninterruptible. */
/* Default batch size is based on FFT length (on my aging Broadwell CPUs I get about 200000 squarings for a single core in 3 seconds on a 4K FFT, */
/* since exponentiation also does some multiplies I chose 150000 4K FFT squarings as the default).  For efficiency, a minimum batch size of 40 is */
/* the default (if that's too slow one might should consider different work!).  A default of 40 should give calc_exp2 room to generate a temp that is */
/* less than 64 bits which exponentiate can handle without any temporaries. */

	stage1_batch_size = (int) ((double) IniGetInt (INI_FILE, "Stage1Batch4KMults", 150000) * pm1data.stage1_threads / (pm1data.gwdata.FFTLEN / 4096.0));
	if (stage1_batch_size < IniGetInt (INI_FILE, "Stage1BatchSizeMin", 40)) stage1_batch_size = IniGetInt (INI_FILE, "Stage1BatchSizeMin", 40);

/* Most likely we are working on small exponents where 100MB of memory gives us all the temporaries we could possibly want. */

	stage1_mem = IniGetInt (INI_FILE, "Stage1Memory", 100);		// Get amount of memory in MB we are allowed to allocate in stage 1
	stage1_temps = (int) cvt_mem_to_gwnums (&pm1data.gwdata, stage1_mem);
	if (stage1_temps > IniGetInt (INI_FILE, "Stage1MaxTemps", 512)) stage1_temps = IniGetInt (INI_FILE, "Stage1MaxTemps", 512);
	set_memory_usage (thread_num, 0, cvt_gwnums_to_mem (&pm1data.gwdata, stage1_temps));

	// Loop processing small batches of primes
	for (pm1data.stage1_prime = sieve (pm1data.sieve_info); pm1data.stage1_prime < pm1data.interim_B; ) {

		// Test for user interrupt, save files, and error checking
		stop_reason = stopCheck (thread_num);
		saving = testSaveFilesFlag (thread_num);
		echk = stop_reason || saving || ERRCHK || near_fft_limit || ((pm1data.stage1_prime & 127) == 127);
		gwerror_checking (&pm1data.gwdata, echk);

		// Calc a new exponent for exponentiate.  This is our small batch.  Batch sizes should be smallish as they cannot be interrupted.
		mpz_init (exp);  exp_initialized = TRUE;
		calc_exp2 (pm1data.sieve_info, exp, pm1data.B_done, pm1data.interim_B, &pm1data.stage1_prime, stop_reason ? stage1_batch_size/10 : stage1_batch_size);

		// Do some exponentiating
		exponentiate_mpz_limited_temps (&pm1data.gwdata, pm1data.x, exp, stage1_temps);
		mpz_clear (exp), exp_initialized = FALSE;

/* Test for an error */

		if (gw_test_for_error (&pm1data.gwdata) || gw_get_maxerr (&pm1data.gwdata) > allowable_maxerr) goto err;

/* Calculate our stage 1 percentage complete */

		w->pct_complete = (double) pm1data.stage1_prime * one_over_B;

/* Output the title every so often */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s P-1 stage 1",
				 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pm1data.gwdata));
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&pm1data.gwdata);
		}

/* Every N squarings, output a progress report */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s stage 1 is %.*f%% complete.",
				 gwmodulo_as_string (&pm1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, " Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&pm1data.gwdata);
			first_iter_msg = FALSE;
		}

/* Every N squarings, output a progress report to the results file */

		if ((ITER_OUTPUT_RES != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output_r + 2 * ITER_OUTPUT_RES) ||
		    (NO_GUI && stop_reason)) {
			sprintf (buf, "%s stage 1 is %.*f%% complete.\n",
				 gwmodulo_as_string (&pm1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			writeResults (buf);
			last_output_r = gw_get_fft_count (&pm1data.gwdata);
		}

/* Check for escape and/or if its time to write a save file */

		if (stop_reason || saving) {
			pm1_save (&pm1data);
			if (stop_reason) goto exit;
		}
	}
	pm1data.B_done = pm1data.interim_B;
	end_timer (timers, 0);

/* Check for the rare case where we need to do even more stage 1.  This happens using a save file created with a smaller bound #1. */

	if (pm1data.B > pm1data.B_done) {
more_B:		if (pm1data.x_binary != NULL) {			// pm1_restore can return x as binary -- convert to a gwnum
			ASSERTG (pm1data.x == NULL);
			pm1data.x = gwalloc (&pm1data.gwdata);
			if (pm1data.x == NULL) goto oom;
			gianttogw (&pm1data.gwdata, pm1data.x_binary, pm1data.x);
			free (pm1data.x_binary), pm1data.x_binary = NULL;
		}
		pm1data.interim_B = pm1data.B;
		pm1data.stage1_prime = 1;
		//GW - pct_complete resets to 0 because of setting stage1_prime to 2
		goto restart1;
	}
	pm1data.C_done = pm1data.B;

/* Make sure end result is not in FFTed state */

	gwunfft (&pm1data.gwdata, pm1data.x, pm1data.x);

/* Stage 1 complete, print a message */

	end_timer (timers, 1);
	if (stage1_timer != 0.0) end_timer (&stage1_timer, 0);
	sprintf (buf, "%s stage 1 complete. %" PRIu64 " transforms. Total time: ", gwmodulo_as_string (&pm1data.gwdata), gw_get_fft_count (&pm1data.gwdata));
	print_timer (timers, 1, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&pm1data.gwdata);

/* Print out round off error */

	if (ERRCHK) {
		sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&pm1data.gwdata));
		OutputStr (thread_num, buf);
		gw_clear_maxerr (&pm1data.gwdata);
	}

/* Check to see if we found a factor - do GCD (x-1, N).  We only do this if there is no stage 2.  If there is a stage 2 we use a trick */
/* to do the GCD as a by-product of computing V = x + 1/x */

	strcpy (w->stage, "S1");
	w->pct_complete = 1.0;
	if (pm1data.C <= pm1data.B) {
		if (w->work_type != WORK_PMINUS1) OutputStr (thread_num, "Starting stage 1 GCD - please be patient.\n");
		start_timer_from_zero (timers, 0);
		gwsmalladd (&pm1data.gwdata, -1, pm1data.x);
		stop_reason = gcd (&pm1data.gwdata, thread_num, pm1data.x, N, &factor);
		gwsmalladd (&pm1data.gwdata, 1, pm1data.x);
		if (stop_reason) {
			pm1_save (&pm1data);
			goto exit;
		}
		end_timer (timers, 0);
		strcpy (buf, "Stage 1 GCD complete. Time: ");
		print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
		OutputStr (thread_num, buf);
		if (factor != NULL) goto bingo;
		// No factor found and no second stage
		pm1data.state = PM1_STATE_DONE;
		goto msg_and_exit;
	}

/*
   Stage 2:  Use ideas from Crandall, Montgomery, Silverman, Zimmermann, Kruppa, Preda, and Atnashev on each prime below C.
   This code is more efficient the more memory you can give it.
   Inputs: x, the value at the end of stage 1
*/

/* This is the entry point when using the save file from a completed P-1 run to go to a new bound #2. */

restart3a:
	strcpy (w->stage, "S1");
	w->pct_complete = 1.0;
	start_timer_from_zero (timers, 0);

/* Initialize variables for second stage. */
/* To compute V = x + 1/x a modular inverse is required.  We use V in a P+1-style stage 2 which uses one multiply instead of two to go from */
/* D section to D section.  Since we are doing a modular inverse, we can also do the stage 1 GCD here for minimal cost (two multiplies). */
/* Compute y = x*(x-1).  Compute 1/y, this will find any stage 1 factors.  If no factors found compute 1/x = (x-1) * 1/y. */

	pm1data.invx = gwalloc (&pm1data.gwdata);
	if (pm1data.invx == NULL) goto oom;

	// If user wants to run stage 2 even if stage 1 would find a factor, then don't do the multiply x-1 trick.
	if (!QA_IN_PROGRESS && IniGetInt (INI_FILE, "Stage1GCD", 1) == -1) {
		gwcopy (&pm1data.gwdata, pm1data.x, pm1data.invx);
		stop_reason = pm1_modinv (&pm1data, pm1data.invx, N, factor);
		if (factor != NULL) goto bingo;
		if (stop_reason) goto exit;
	}

	// Otherwise, the common case: compute stage 1 GCD cheaply
	else {
		gwnum x_minus_1 = gwalloc (&pm1data.gwdata);
		if (x_minus_1 == NULL) goto oom;
		gwcopy (&pm1data.gwdata, pm1data.x, x_minus_1);
		gwsmalladd (&pm1data.gwdata, -1, x_minus_1);
		gwmul3 (&pm1data.gwdata, pm1data.x, x_minus_1, pm1data.invx, GWMUL_PRESERVE_S1 | GWMUL_FFT_S2);	// x * (x-1)
		stop_reason = pm1_modinv (&pm1data, pm1data.invx, N, factor);					// 1 / (x * (x-1))
		if (factor != NULL) goto bingo;
		if (stop_reason) goto exit;
		gwmul3 (&pm1data.gwdata, x_minus_1, pm1data.invx, pm1data.invx, 0);				// (x-1) * 1 / (x * (x-1)), which is 1/x
		gwfree (&pm1data.gwdata, x_minus_1);
	}

/* Inversion to 1/x format complete */

	end_timer (timers, 0);
	sprintf (buf, "Inversion of stage 1 result complete. %" PRIu64 " transforms, 1 modular inverse. Time: ", gw_get_fft_count (&pm1data.gwdata));
	print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&pm1data.gwdata);

/* Convert x and invx to binary for saving to a file (also more compact than a gwnum) */

	pm1data.x_binary = allocgiant (((int) pm1data.gwdata.bit_length >> 5) + 10);
	if (pm1data.x_binary == NULL) goto oom;
	gwtogiant (&pm1data.gwdata, pm1data.x, pm1data.x_binary);
	pm1data.invx_binary = allocgiant (((int) pm1data.gwdata.bit_length >> 5) + 10);
	if (pm1data.invx_binary == NULL) goto oom;
	gwtogiant (&pm1data.gwdata, pm1data.invx, pm1data.invx_binary);

/* Stage 1 is now complete */

	pm1data.state = PM1_STATE_MIDSTAGE;
	strcpy (w->stage, "S2");
	w->pct_complete = 0.0;
	start_timer_from_zero (&stage2_timer, 0);

/* Some users have requested that a save file be written here.  Sometimes the vast amount of stage 2 memory allocated makes their machine thrash which */
/* may require a reboot.  Upon resumption, the tail end of stage 1 calculations then need to be repeated.  For now, we'll make this the default behavior. */

	if (IniGetInt (INI_FILE, "SaveBeforeStage2", 1)) pm1_save (&pm1data);

/* No restrictions on polymult D value */

	pm1data.required_missing = 0;

/* Restart here when in the middle of stage 2.  Initialize gg to x-1 in case the user opted to skip the GCD after stage 1. */

restart3b:
	ASSERTG (pm1data.gg == NULL);
	if (pm1data.gg_binary == NULL) {
		pm1data.gg_binary = allocgiant (((int) pm1data.gwdata.bit_length >> 5) + 10);
		if (pm1data.gg_binary == NULL) goto oom;
		gtog (pm1data.x_binary, pm1data.gg_binary);
		sladdg (-1, pm1data.gg_binary);
	}

/* Test if we will ever have enough memory to do stage 2 based on the maximum available memory. */
/* Our minimum working set is prime pairing: one gwnum for gg, 4 for nQx, 3 for eQx. */

	unsigned long min_memory;
	min_memory = cvt_gwnums_to_mem (&pm1data.gwdata, 8.5);
	if (max_mem (thread_num) < min_memory) {
		sprintf (buf, "Insufficient memory to ever run stage 2 -- %luMB needed.\n", min_memory);
		OutputStr (thread_num, buf);
		pm1data.C = pm1data.B_done;
		goto restart4;
	}

/* Loop here when prime pairing completes to a B2 from a save file but the B2 in worktodo.txt is even higher. */

more_C:	start_timer_from_zero (timers, 0);
	sprintf (buf, "%s P-1 stage 2 init", gwmodulo_as_string (&pm1data.gwdata));
	title (thread_num, buf);

/* Clear flag indicating we need to restart if the maximum amount of memory changes. */
/* Prior to this point we allow changing the optimal bounds of a Pfactor assignment. */

	clear_restart_if_max_memory_change (thread_num);

/* Cost several FFT sizes!  Polymult may require several EXTRA_BITS to safely accumulate products in FFT space. */

replan:	unsigned long best_fftlen;
	unsigned int memory;
	int	forced_stage2_type, best_stage2_type, best_fails;
	double	best_efficiency, best_poly_efficiency;
	char	msgbuf[2000];

	forced_stage2_type = IniGetInt (INI_FILE, "ForceP1Stage2Type", 99);		// 0 = pairing, 1 = poly, 99 = not forced (either)
	memory = 0;
	best_fftlen = 0;
	best_fails = 0;
	best_poly_efficiency = 0.0;
	msgbuf[0] = 0;
//GW: Can we get here (old save files) with V set and one of x or invx not set?  If so, switching is an issue unless we convert V to binary.
	for (bool found_best = FALSE; ; ) {

/* Initialize the polymult library (needed for calling polymult_mem_required).  Let user override polymult tuning parameters. */

		polymult_init (&pm1data.polydata, &pm1data.gwdata);
		polymult_set_max_num_threads (&pm1data.polydata, pm1data.stage2_threads);
		polymult_default_tuning (&pm1data.polydata,
					 IniGetInt (INI_FILE, "PolymultCacheSize", CPU_NUM_L2_CACHES > 0 ? CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES : 256),
					 IniGetInt (INI_FILE, "PolymultCacheSize2", CPU_NUM_L3_CACHES > 0 ? CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES : 6144));
		pm1data.gwdata.scramble_arrays = (char) IniGetInt (INI_FILE, "PolymultScramble", pm1data.gwdata.scramble_arrays);
		pm1data.polydata.strided_writes_end = IniGetInt64 (INI_FILE, "PolymultStridedWritesEnd", pm1data.polydata.strided_writes_end);
		pm1data.polydata.streamed_stores_start = IniGetInt64 (INI_FILE, "PolymultStreamStores", 0); // Default is always stream stores because of POLYMULT_NOUNFFT
		gwset_polymult_safety_margin (&pm1data.gwdata, 0.0);		// If needed, reset polymult safety margin while calculating new poly size

/* When recovering from a roundoff error, only cost FFT lengths larger than the FFT length that generated the roundoff error. */

		if (gwfftlen (&pm1data.gwdata) > maxerr_fftlen) {
			double efficiency;

/* Calculate the amount of memory we can use in stage 2.  We must have 1MB for a pairing map + a minimum of 8.5 temporaries (D = 30). */
/* If not continuing from a stage 2 save file then assume 144 temporaries (D = 210, multiplier = 5) and a few MB for a pairing map will */
/* provide us with a reasonable execution speed.  Otherwise, we desire enough memory to use the save file's pairing map. */

			if (memory == 0) {
				unsigned int min_memory, desired_memory;	/* Memory is in MB */
				min_memory = 1 + cvt_gwnums_to_mem (&pm1data.gwdata, 8.5);
				if (pm1data.state < PM1_STATE_STAGE2 || pm1data.pairmap == NULL) desired_memory = 3 + cvt_gwnums_to_mem (&pm1data.gwdata, 144);
				else desired_memory = (unsigned int) (pm1data.pairmap_size >> 20) + cvt_gwnums_to_mem (&pm1data.gwdata, pm1data.stage2_numvals);
				stop_reason = avail_mem (thread_num, min_memory, desired_memory, &memory);
				if (stop_reason) {
					if (pm1data.state == PM1_STATE_MIDSTAGE) pm1_save (&pm1data);
					goto exit;
				}

/* Factor in the multiplier that we set to less than 1.0 when we get unexpected memory allocation errors. */
/* Make sure we can still allocate minimum number of temporaries. */

				memory = (unsigned int) (pm1data.pct_mem_to_use * (double) memory);
				if (memory < min_memory) {
					stop_reason = avail_mem_not_sufficient (thread_num, min_memory, desired_memory);
					if (pm1data.state == PM1_STATE_MIDSTAGE) pm1_save (&pm1data);
					goto exit;
				}

/* Output a message telling us how much memory is available */

				sprintf (buf, "Available memory is %uMB.\n", memory);
				OutputStr (thread_num, buf);
			}

/* Choose the best plan implementation given the current FFT length and available memory.  We can skip costing prime pairing for larger FFT lengths. */
/* The returned cost is independent of FFT length.  Adjust cost upwards for larger FFT lengths. */

			efficiency = pm1_stage2_impl (&pm1data, memory, found_best ? best_stage2_type : best_fftlen ? 1 : forced_stage2_type, msgbuf + strlen (msgbuf));

			// If using pairmap from a save file, don't cost any other options
			if (pm1data.pairmap != NULL) break;

/* Multiply efficiency by ten trillion solely for pretty output in case PolyVerbose is set. */

			efficiency *= 1.0e13;
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
if (efficiency == 0.0) sprintf (buf, "FFT: %d no stage 2 plan works\n", (int) gwfftlen (&pm1data.gwdata));
else sprintf (buf, "FFT: %d, B2: %" PRIu64 "/%" PRIu64 ", numvals: %" PRIu64 "/%d, poly: %" PRIu64 "x%" PRIu64 ", efficiency: %.0f\n", (int) gwfftlen (&pm1data.gwdata),
	      pm1data.C, pm1data.B2_start + pm1data.numDsections * pm1data.D,
	      pm1data.stage2_numvals, (int) cvt_mem_to_gwnums_adj (&pm1data.gwdata, memory, -0.5), pm1data.poly1_size, pm1data.poly2_size, efficiency);
OutputStr (thread_num, buf); }

			// If we've found our best stage 2 implementation, break
			if (found_best) break;

			// Remember the best efficiency found thusfar
			if (efficiency != 0.0 && (best_fftlen == 0 || efficiency > best_efficiency)) {
				best_fftlen = gwfftlen (&pm1data.gwdata);
				best_stage2_type = pm1data.stage2_type;
				best_efficiency = efficiency;
			}

			// Remember the most efficient poly found thusfar
			if (pm1data.stage2_type == PM1_STAGE2_POLYMULT && efficiency > best_poly_efficiency) {
				best_poly_efficiency = efficiency;
				best_fails = 0;
			}
		}

		// If forced_stage2_type is prime pairing, then we do not need to try larger FFT lengths
		if (best_fftlen && forced_stage2_type == 0) found_best = TRUE;

		// Small optimization: Skip costing larger FFT lengths if there are less than 60 numvals
		if (best_fftlen && (double) memory * 1000000.0 / (double) array_gwnum_size (&pm1data.gwdata) < 60.0) found_best = TRUE;

		// If we fail to get a new best efficient poly twice in a row, then we've found our best poly plan
//GW: Likely costing overkill
		if (best_poly_efficiency != 0.0) best_fails++;
		if (best_fails == 3) found_best = TRUE;

		// Switch to next larger or confirmed best FFT length
		for ( ; ; ) {
			unsigned long next_fftlen = gwfftlen (&pm1data.gwdata) + 1;  // Try next larger FFT length
			if (next_fftlen < maxerr_fftlen) next_fftlen = maxerr_fftlen;
			if (found_best) next_fftlen = best_fftlen;
			msgbuf[0] = 0;
			// Detect when no FFT length change is necessary (e.g. prime pairing selected and memory is very tight)
			if (next_fftlen == gwfftlen (&pm1data.gwdata)) break;
			// Terminate current FFT size
			polymult_done (&pm1data.polydata);
			gwdone (&pm1data.gwdata);
			pm1data.x = NULL;
			pm1data.invx = NULL;
			// Re-init gwnum with a larger FFT
			gwinit (&pm1data.gwdata);
			if (next_fftlen != best_fftlen) gwset_information_only (&pm1data.gwdata);
			if (IniGetInt (INI_FILE, "UseLargePages", 0)) gwset_use_large_pages (&pm1data.gwdata);
			if (HYPERTHREAD_LL) sp_info->normal_work_hyperthreading = TRUE, gwset_will_hyperthread (&pm1data.gwdata, 2);
			gwset_bench_cores (&pm1data.gwdata, HW_NUM_CORES);
			gwset_bench_workers (&pm1data.gwdata, NUM_WORKERS);
			if (ERRCHK) gwset_will_error_check (&pm1data.gwdata);
			else gwset_will_error_check_near_limit (&pm1data.gwdata);
			gwset_num_threads (&pm1data.gwdata, pm1data.stage2_threads);
			gwset_thread_callback (&pm1data.gwdata, SetAuxThreadPriority);
			gwset_thread_callback_data (&pm1data.gwdata, sp_info);
			gwset_minimum_fftlen (&pm1data.gwdata, next_fftlen);
			gwset_using_polymult (&pm1data.gwdata);
			gwset_use_spin_wait (&pm1data.gwdata, IniGetInt (INI_FILE, "SpinWait", 0));
			res = gwsetup (&pm1data.gwdata, w->k, w->b, w->n, w->c);
			if (res == GWERROR_TOO_LARGE && best_fftlen) {
				found_best = TRUE;
				continue;
			}
			if (res) {
				sprintf (buf, "Cannot initialize FFT code, errcode=%d\n", res);
				OutputBoth (thread_num, buf);
				stop_reason = STOP_FATAL_ERROR;
				goto exit;
			}
			gwerror_checking (&pm1data.gwdata, ERRCHK || near_fft_limit);
			if (gwfftlen (&pm1data.gwdata) != pm1data.stage1_fftlen) {
				char	fft_desc[200];
				gwfft_description (&pm1data.gwdata, fft_desc);
				sprintf (msgbuf, "Switching to %s\n", fft_desc);
			}
			break;
		}
	}

// Output the FFT switching and stage 2 planning messages accmulated above

	OutputStr (thread_num, msgbuf);

// Restore the x and invx values.  Costing different FFT lengths may have deleted these gwnums.

	if (pm1data.x == NULL) {
		pm1data.x = gwalloc (&pm1data.gwdata);
		if (pm1data.x == NULL) goto oom;
		gianttogw (&pm1data.gwdata, pm1data.x_binary, pm1data.x);
	}
	if (pm1data.invx == NULL) {
		pm1data.invx = gwalloc (&pm1data.gwdata);
		if (pm1data.invx == NULL) goto oom;
		gianttogw (&pm1data.gwdata, pm1data.invx_binary, pm1data.invx);
	}

// Compute V = x + 1/x

	pm1data.V = gwalloc (&pm1data.gwdata);
	if (pm1data.V == NULL) goto oom;
	gwadd3o (&pm1data.gwdata, pm1data.x, pm1data.invx, pm1data.V, GWADD_SQUARE_INPUT);

/* Set addin for future lucas_dbl and lucas_add calls */ 

	gwsetaddin (&pm1data.gwdata, -2);

/* Record the amount of memory this thread will be using in stage 2 */

	memused = cvt_array_gwnums_to_mem (&pm1data.gwdata, pm1data.stage2_numvals);
	memused += 2 * (int) pm1data.gwdata.bit_length >> 23;
	memused += (int) (pm1data.pairmap_size >> 20);
	// To dodge possible infinite loop if pm1_stage2_impl allocates too much memory (it shouldn't), decrease the percentage of memory we are allowed to use
	// Beware that replaning may allocate a larger pairing map
	if (set_memory_usage (thread_num, MEM_VARIABLE_USAGE, memused)) {
		pm1data.pct_mem_to_use *= 0.99;
		free (pm1data.pairmap); pm1data.pairmap = NULL;
		goto replan;
	}

/* If doing an FFT/polymult stage 2, go do that.  Otherwise, use old-fashioned prime pairing stage 2 */

	if (pm1data.stage2_type == PM1_STAGE2_POLYMULT) goto pm1_polymult;

/*---------------------------------------------------------------------
|	Traditional prime pairing stage 2
+---------------------------------------------------------------------*/

/* We won't be using polymult in prime pairing stage 2 */

	polymult_done (&pm1data.polydata);

/* Output a useful message regarding memory usage */

	sprintf (buf, "Using %uMB of memory.\n", memused);
	OutputStr (thread_num, buf);

/* Free the stage 1 value, it is no longer needed.  Free 1/x, it too is not needed in a prime pairing stage 2. */

	gwfree (&pm1data.gwdata, pm1data.x), pm1data.x = NULL;
	gwfree (&pm1data.gwdata, pm1data.invx), pm1data.invx = NULL;

/* Create a new map of (hopefully) close-to-optimal prime pairings */

	if (pm1data.pairmap == NULL) {
		int fill_window = pair_window_size (pm1data.gwdata.bit_length, pm1data.relp_sets);
		gwfree_internal_memory (&pm1data.gwdata);
		stop_reason = fill_pairmap (pm1data.thread_num, &pm1data.sieve_info, pm1data.D, fill_window,0,0,0,
					    pm1data.totrels, pm1data.relp_sets+3, pm1data.first_relocatable, pm1data.last_relocatable,
					    pm1data.B2_start, pm1data.C, pm1data.max_pairmap_Dsections, &pm1data.pairmap, &pm1data.pairmap_size);
		if (stop_reason) goto exit;
		pm1data.pairmap_ptr = pm1data.pairmap;
		pm1data.Dsection = 0;
		pm1data.relp = -1;			// Indicates last relp was prior to first Dsection
	}

/* Do preliminary processing of the relp_sets */

	process_relp_sets (pm1data.totrels, pm1data.numrels, pm1data.relp_sets, relp_set_map, Dmultiple_map);

/* Initialize variables for second stage */

	// Calculate the percent completed by previous pairmaps
	base_pct_complete = ((double) pm1data.B2_start - (double) pm1data.last_relocatable) / ((double) pm1data.C - (double) pm1data.last_relocatable);
	// Calculate the percent completed by each relative prime in this pairmap
	one_relp_pct = (1.0 - base_pct_complete) / (double) (pm1data.numDsections * pm1data.totrels);
	// Calculate the percent completed by previous pairmaps and the current pairmap
	w->pct_complete = base_pct_complete + (double) (pm1data.Dsection * pm1data.totrels) * one_relp_pct;

/* Allocate nQx array of pointers to relative prime gwnums.  Allocate an array large enough to hold values for all relp sets. */
/* This is more than we will need once stage 2 init completes. */

	int	num_relp_sets;		// Total number of relp_sets including intermediate relp_sets
	num_relp_sets = (int) relp_set_map.size();
	pm1data.nQx = (gwnum *) malloc (num_relp_sets * pm1data.numrels * sizeof (gwnum));
	if (pm1data.nQx == NULL) goto lowmem;

/* Calculate some handy values for computing the first two relp sets: (0,-1) */

	int	set_minus1_nQx_index;	// Index into nQx array for the -1 relp set
	set_minus1_nQx_index = relp_set_map.find(-1)->second.nQx_store;
	// Map nth relp to either set 0 nQx index or set -1 nQx index
	#define nqxmap(relp)	((relp) < pm1data.numrels ? (relp) : set_minus1_nQx_index + (relp) - pm1data.numrels)

/* A fast 1,5 mod 6 nQx initialization for relative primes less than D */

	ASSERTG (pm1data.D % 3 == 0);
	{
		gwnum	t1, t2, t3, t4, t5;
		gwnum	V1, V2, V3, V5, V6, V7, V11;
		struct {
			gwnum	Vi;
			gwnum	Vi_minus6;
			int	Vi_is_relp;
			int	Vi_minus6_is_relp;
		} V1mod6, V5mod6, *curr, *notcurr;
		int	i_gap, totrels, have_VD;

/* Allocate memory and init values for computing nQx.  We need V_1, V_5, V_6, V_7, V_11. */
/* We also need V_2 or V_4 to compute V_D in the middle of the nQx init loop. */
/* NOTE:  By loop's end, V_D is stored in pm1data.V */

		t1 = gwalloc (&pm1data.gwdata);
		if (t1 == NULL) goto lowmem;
		t2 = gwalloc (&pm1data.gwdata);
		if (t2 == NULL) goto lowmem;
		t3 = gwalloc (&pm1data.gwdata);
		if (t3 == NULL) goto lowmem;
		t4 = gwalloc (&pm1data.gwdata);
		if (t4 == NULL) goto lowmem;
		t5 = gwalloc (&pm1data.gwdata);
		if (t5 == NULL) goto lowmem;

/* Compute V_2 and place in pm1data.V.  This is the value we will save if init is aborted and we create a save file. */
/* V_2 will be replaced by V_D later on in this loop.  At all times pm1data.V will be a save-able value! */

		luc_dbl (&pm1data.gwdata, pm1data.V, t1);			// V2 = 2 * V1
		gwswap (t1, pm1data.V);
		V1 = t1;
		V2 = pm1data.V;

/* Compute V_5, V_6, V_7, V_11 */

		V3 = t2;
		luc_add (&pm1data.gwdata, V2, V1, V1, V3);			// V3 = V2 + V1 (diff V1)

		V5 = t3;
		luc_add (&pm1data.gwdata, V3, V2, V1, V5);			// V5 = V3 + V2 (diff V1)

		V6 = V3;
		luc_dbl (&pm1data.gwdata, V3, V6);				// V6 = 2 * V3, V3 no longer needed

		V7 = t4;
		luc_add (&pm1data.gwdata, V6, V1, V5, V7);			// V7 = V6 + V1 (diff V5)

		V11 = t5;
		luc_add (&pm1data.gwdata, V6, V5, V1, V11);			// V11 = V6 + V5 (diff V1)

/* Init structures used in the loop below */

		V1mod6.Vi_minus6 = V1;
		V1mod6.Vi_minus6_is_relp = 1;
		V5mod6.Vi_minus6 = V5;
		V5mod6.Vi_minus6_is_relp = relatively_prime (5, pm1data.D);
		V1mod6.Vi = V7;
		V1mod6.Vi_is_relp = relatively_prime (7, pm1data.D);
		V5mod6.Vi = V11;
		V5mod6.Vi_is_relp = relatively_prime (11, pm1data.D);
		pm1data.nQx[0] = V1; totrels = 1;
		if (V5mod6.Vi_minus6_is_relp) pm1data.nQx[totrels++] = V5;
		if (V1mod6.Vi_is_relp) pm1data.nQx[totrels++] = V7;
		if (V5mod6.Vi_is_relp) pm1data.nQx[totrels++] = V11;

/* Compute the rest of the nQx values (V_i for i >= 7) */

		have_VD = FALSE;
		for (i = 7, i_gap = 4; ; i += i_gap, i_gap = 6 - i_gap) {
			gwnum	next_i;
			int	next_i_is_relp;

/* Point to the i, i-6 pair to work on */

			curr = (i_gap == 4) ? &V1mod6 : &V5mod6;

/* Compute V_D which we will need in computing multiples of V_D.  Do this with a single luc_add call when we reach two values that are 2 or 4 apart that add to D. */

			if (i + (i + i_gap) == pm1data.D) {
				gwnum	Vgap = V2;
				ASSERTG (Vgap == pm1data.V);
				if (i_gap == 4) luc_dbl (&pm1data.gwdata, Vgap, Vgap);			// Vgap = V4 = 2 * V2, V2 no longer needed
				luc_add (&pm1data.gwdata, V1mod6.Vi, V5mod6.Vi, Vgap, pm1data.V);	// VD = V{i} + V{i+gap} (diff Vgap), Vgap no longer needed
				have_VD = TRUE;
			}

/* Break out of loop when we have all our nQx values less than D */

			if (have_VD && (totrels == pm1data.totrels || totrels == pm1data.numrels * 2)) break;

/* See if we are about to compute the next relative prime */

			next_i_is_relp = (relatively_prime (i+6, pm1data.D) && totrels < pm1data.totrels);

/* If so, and if it is the last relprime then free up the some resources to keep maximum number of temporaries to a minimum. */
/* This is of marginal utility as peak will occur in the next loop whenever totrels > numrels * 2. */
/* Also catch the case where we are using the dead minimum number of relative primes - the first i after D/2 is the last i. */

			if ((next_i_is_relp && have_VD && (totrels+1 == pm1data.totrels || totrels+1 == pm1data.numrels * 2)) ||
			    (!have_VD && i+6 > pm1data.D/2 && pm1data.totrels == pm1data.numrels)) {
				notcurr = (curr == &V5mod6) ? &V1mod6 : &V5mod6;
				if (!notcurr->Vi_is_relp) gwfree (&pm1data.gwdata, notcurr->Vi), notcurr->Vi = NULL;
				if (!notcurr->Vi_minus6_is_relp) gwfree (&pm1data.gwdata, notcurr->Vi_minus6), notcurr->Vi_minus6 = NULL;
			}			

/* Get next i value.  Don't destroy Vi_minus6 if it is an nQx value. */

			if (curr->Vi_minus6_is_relp) {
				next_i = gwalloc (&pm1data.gwdata);
				if (next_i == NULL) goto lowmem;
			} else
				next_i = curr->Vi_minus6;

			luc_add (&pm1data.gwdata, curr->Vi, V6, curr->Vi_minus6, next_i);	// Next i
			if (next_i_is_relp) {
				pm1data.nQx[nqxmap(totrels)] = next_i;
				totrels++;
			}

/* Shuffle i values along */

			curr->Vi_minus6 = curr->Vi;
			curr->Vi_minus6_is_relp = curr->Vi_is_relp;
			curr->Vi = next_i;
			curr->Vi_is_relp = next_i_is_relp;

/* Check for errors, user esc, restart for mem changed, etc. */

			if (gw_test_for_error (&pm1data.gwdata)) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}

/* Free memory used in computing nQx values */

		if (!V1mod6.Vi_is_relp) gwfree (&pm1data.gwdata, V1mod6.Vi);
		if (!V1mod6.Vi_minus6_is_relp) gwfree (&pm1data.gwdata, V1mod6.Vi_minus6);
		if (!V5mod6.Vi_is_relp) gwfree (&pm1data.gwdata, V5mod6.Vi);
		if (!V5mod6.Vi_minus6_is_relp) gwfree (&pm1data.gwdata, V5mod6.Vi_minus6);
		gwfree (&pm1data.gwdata, V6);
	}
	#undef nqxmap

/* Add V_D computed above to the D-multiples map */

	{
		auto it_Dmult = Dmultiple_map.find (1);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({1, {0, 1}}).first;
		it_Dmult->second.set_val_buffers (&pm1data.gwdata, pm1data.V, TRUE);

/* Init for computing Q^(multiples of D).  We simply add the starting multiples of D that need to be computed to the D-multiples map. */
/* Vn = V_{Dsection-1}, Vn1 = V_{Dsection} */

//GW: Rather than allocating Vn and Vn1, we should allocate it when we compute it (and only if we aren't deleting one of the lucas inputs)
// Then once all multiples of D are calculated, set pm1data.Vn and Vn1.  Do this for P+1 (and ECM?) too.  Reduces mempeak when L<=2.
		uint64_t D_multiplier = (pm1data.B2_start + pm1data.D / 2) / pm1data.D + pm1data.Dsection;
		ASSERTG (D_multiplier > 2); // Would require special code to the collision between V and (Vn or Vn1)
		it_Dmult = Dmultiple_map.find (D_multiplier - 1);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({D_multiplier - 1, {0, D_multiplier - 1}}).first;
		pm1data.Vn = gwalloc (&pm1data.gwdata);
		if (pm1data.Vn == NULL) goto lowmem;
		it_Dmult->second.set_val_buffers (&pm1data.gwdata, pm1data.Vn, TRUE);

		it_Dmult = Dmultiple_map.find (D_multiplier);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({D_multiplier, {0, D_multiplier}}).first;
		pm1data.Vn1 = gwalloc (&pm1data.gwdata);
		if (pm1data.Vn1 == NULL) goto lowmem;
		it_Dmult->second.set_val_buffers (&pm1data.gwdata, pm1data.Vn1, TRUE);
	}

// Calculate all the needed V_D^(multiples-of-D)

	process_Dmult_map (Dmultiple_map);
	for (auto this_Dmult = Dmultiple_map.begin(); this_Dmult != Dmultiple_map.end(); ++this_Dmult) {
		// Ignore the already computed V_D
		if (this_Dmult->first == 1) continue;
		// If necessary, allocate buffer for this V_D^multiple-of-D
		if (this_Dmult->second.val.x == NULL) {
			gwnum	tmp = gwalloc (&pm1data.gwdata);
			if (tmp == NULL) goto lowmem;
			this_Dmult->second.set_val_buffers (&pm1data.gwdata, tmp, FALSE);
		}
		// Compute via luc_dbl if diff is zero
		auto it_base = Dmultiple_map.find (this_Dmult->second.base);
		if (this_Dmult->second.diff == 0) {
			luc_dbl (&pm1data.gwdata, it_base->second.val.x, this_Dmult->second.val.x);
		}
		// Compute using luc_add if diff is non-zero
		else {
			auto it_addin = Dmultiple_map.find (this_Dmult->second.addin);
			auto it_diff = Dmultiple_map.find (this_Dmult->second.diff);
			luc_add (&pm1data.gwdata, it_base->second.val.x, it_addin->second.val.x, it_diff->second.val.x, this_Dmult->second.val.x);
			// Free addin, diff if no longer needed
			it_addin->second.free_if_Dmult_last_used_by (this_Dmult->first);
			it_diff->second.free_if_Dmult_last_used_by (this_Dmult->first);
		}
		// Free base if no longer needed
		it_base->second.free_if_Dmult_last_used_by (this_Dmult->first);
		// Check for errors, user esc, restart for mem changed, etc.
		if (gw_test_for_error (&pm1data.gwdata)) goto err;
		stop_reason = stopCheck (thread_num);
		if (stop_reason) goto possible_lowmem;
	}

/* We're about to reach peak memory usage, free any gwnum internal memory */

	gwfree_internal_memory (&pm1data.gwdata);

/* Compute relp sets other than 0,-1 */

	int	num_partials;
	num_partials = one_based_modulo (pm1data.totrels, pm1data.numrels);
	// Loop over every relp_set we need to calculate
	for (auto it_relp_set = relp_set_map.begin (); it_relp_set != relp_set_map.end (); ++it_relp_set) {
		// Relp sets 0 and -1 are already computed
		if (it_relp_set->first == 0 || it_relp_set->first == -1) continue;
		// Find the base relp_set and diff relp_set needed to create this relp_set
		auto it_base_relp_set = relp_set_map.find (it_relp_set->second.base);
		auto it_diff_relp_set = relp_set_map.find (it_relp_set->second.diff);
		// Find the Dmultiple needed to create this relp_set
		auto it_Dmultiple = Dmultiple_map.find (it_relp_set->second.Dmultiple);
		// Determine if the diff relps are accessed in reverse order
		bool	diff_access_inverted = (it_base_relp_set->first < 0) ^ (it_diff_relp_set->first < 0);
		// Loop over all relative primes below D/2 that need calculating in this relp_set (backwards is better for peak memory)
		for (i = (it_relp_set->second.partial ? num_partials : pm1data.numrels) - 1; i >= 0; i--) {
			gwnum	*Vbase, *Vdiff, Vnew;
			int	ni;

			// Get the base gwnum from the nQx entry
			ni = it_base_relp_set->second.nQx_store + i;
			Vbase = &pm1data.nQx[ni];
			// Get the diff gwnum from the diff nQx entry
			// If diff has a different sign bit than base, then we need to index the diff relp_set in reverse order
			ni = it_diff_relp_set->second.nQx_store;
			if (!diff_access_inverted) ni += i; else ni += (pm1data.numrels-1)-i;
			Vdiff = &pm1data.nQx[ni];

			// Set flags if Vbase and/or Vdiff or Dmultiple will be freed because they are not used in any more relp_set calculations
			bool Vbase_to_be_freed = (!it_base_relp_set->second.permanent &&
						  it_base_relp_set->second.is_last_use (it_relp_set->first, i, num_partials, pm1data.numrels, FALSE));
			bool Vdiff_to_be_freed = (!it_diff_relp_set->second.permanent &&
						  it_diff_relp_set->second.is_last_use (it_relp_set->first, i, num_partials, pm1data.numrels, diff_access_inverted));
			bool Dmultiple_to_be_freed = (i == 0 && it_Dmultiple->second.will_free_due_to_relp_last_used_by (it_relp_set->first));

			// Allocate the new relp value (or re-use Vbase or Vdiff)
			if (Vbase_to_be_freed) Vnew = *Vbase;
			else if (Vdiff_to_be_freed) Vnew = *Vdiff;
			else if (Dmultiple_to_be_freed) Vnew = it_Dmultiple->second.val.x;
			else {
				Vnew = gwalloc (&pm1data.gwdata);
				if (Vnew == NULL) goto lowmem;
			}

			// Compute the new relp value
			luc_add (&pm1data.gwdata, *Vbase, it_Dmultiple->second.val.x, *Vdiff, Vnew);	// Vnew = Vbase + Dmult, Vdiff e.g. 47+30 or 29+30

			// Add the relp to the nQx array.
			ni = it_relp_set->second.nQx_store + i;
			pm1data.nQx[ni] = Vnew;

			// If base is not needed for further relp_set calculations, then free Vbase
			if (Vbase_to_be_freed && Vnew != *Vbase) {
				gwfree (&pm1data.gwdata, *Vbase);
				*Vbase = NULL;
			}

			// If diff is not needed for further relp_set calculations, then free Vdiff
			if (Vdiff_to_be_freed && Vnew != *Vdiff) {
				gwfree (&pm1data.gwdata, *Vdiff);
				*Vdiff = NULL;
			}

			// Free the Dmultiple if this is the last time it will be used
			if (Dmultiple_to_be_freed) {
				it_Dmultiple->second.do_not_free_val = (Vnew == it_Dmultiple->second.val.x);
				it_Dmultiple->second.free_if_relp_last_used_by (it_relp_set->first);
			}

			// Check for errors, user esc, restart for mem changed, etc.
			if (gw_test_for_error (&pm1data.gwdata)) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}
	}

/* Clear the maps used to build relp sets */

	Dmultiple_map.clear ();
	relp_set_map.clear ();

/* Re-initialize gg */

	ASSERTG (pm1data.gg == NULL);
	pm1data.gg = gwalloc (&pm1data.gwdata);
	if (pm1data.gg == NULL) goto lowmem;
	gianttogw (&pm1data.gwdata, pm1data.gg_binary, pm1data.gg);
	free (pm1data.gg_binary), pm1data.gg_binary = NULL;

/* We're at peak memory usage, free any cached gwnums */

	gwfree_cached (&pm1data.gwdata);
	mallocFreeForOS ();

/* Stage 2 init complete, change the title, output a message */

	sprintf (buf, "%.*f%% of %s P-1 stage 2 (using %uMB)",
		 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pm1data.gwdata), memused);
	title (thread_num, buf);

	end_timer (timers, 0);
	sprintf (buf, "Stage 2 init complete. %" PRIu64 " transforms. Time: ", gw_get_fft_count (&pm1data.gwdata));
	print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&pm1data.gwdata);

// Stage 2 init is successful.  Set interim_C so that continuing from a save file will go to our current B2_end.  This will ensure that the
// relocatable primes just below b2_start will get properly relocated.

	ASSERTG (pm1data.interim_C <= pm1data.B2_start + pm1data.numDsections * pm1data.D);
	pm1data.interim_C = pm1data.B2_start + pm1data.numDsections * pm1data.D;

/* We do prime pairing with each loop iteration handling the range m-Q to m+Q where m is a multiple of D and Q is the */
/* Q-th relative prime to D.  Totrels is often much larger than the number of relative primes less than D.  This Preda/Montgomery */
/* optimization provides us with many more chances to find a prime pairing. */

	pm1data.state = PM1_STATE_STAGE2;
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);
	last_output = last_output_t = last_output_r = 0;
	for ( ; ; ) {

/* Get next pairing from the pairmap */

		pm1data.relp += next_pair (&pm1data.pairmap_ptr);

/* Make a quick check for end of the pairmap and no more pairmaps needed (i.e. we're done with stage 2) */

		if (pm1data.relp >= pm1data.totrels && pm1data.Dsection + pm1data.relp / pm1data.totrels >= pm1data.numDsections) break;

/* Move to next D section when appropriate */

		while (pm1data.relp >= pm1data.totrels) {
			pm1data.Dsection++;
			pm1data.relp -= pm1data.totrels;
			luc_add (&pm1data.gwdata, pm1data.Vn1, pm1data.V, pm1data.Vn, pm1data.Vn);	// Vn2 = Vn1 + V, diff = Vn
			gwswap (pm1data.Vn, pm1data.Vn1);						// Vn = Vn1, Vn1 = Vn2
//GW: check for errors?
		}

/* Check for end of pairing map.  If so, generate next pairing map. */

		if (pm1data.pairmap_ptr == pm1data.pairmap + pm1data.pairmap_size) {
			ASSERTG (pm1data.relp == 0);
			pm1data.C_done = pm1data.B2_start + pm1data.Dsection * pm1data.D;
			pm1data.first_relocatable = calc_new_first_relocatable (pm1data.D, pm1data.C_done);
			int fill_window = pair_window_size (pm1data.gwdata.bit_length, pm1data.relp_sets);
			gwfree_internal_memory (&pm1data.gwdata);
			stop_reason = fill_pairmap (pm1data.thread_num, &pm1data.sieve_info, pm1data.D, fill_window,0,0,0,
						    pm1data.totrels, pm1data.relp_sets+3, pm1data.first_relocatable, pm1data.last_relocatable,
						    pm1data.C_done, pm1data.C, pm1data.max_pairmap_Dsections, &pm1data.pairmap, &pm1data.pairmap_size);
			if (stop_reason) {
//GW:				is save possible here with no pairmap generated?
				pm1_save (&pm1data);
				goto exit;
			}
			pm1data.pairmap_ptr = pm1data.pairmap;
			pm1data.relp = -1;
			continue;
		}

/* Multiply this Vn1 - nQx value into the gg accumulator */

		saving = testSaveFilesFlag (thread_num);
		stop_reason = stopCheck (thread_num);
		gwsubmul4 (&pm1data.gwdata, pm1data.Vn1, pm1data.nQx[pm1data.relp], pm1data.gg, pm1data.gg, (!stop_reason && !saving) ? GWMUL_STARTNEXTFFT : 0);

/* Calculate stage 2 percentage. */

		w->pct_complete = base_pct_complete + (double) (pm1data.Dsection * pm1data.totrels + pm1data.relp) * one_relp_pct;

/* Test for errors */

		if (gw_test_for_error (&pm1data.gwdata) || gw_get_maxerr (&pm1data.gwdata) > allowable_maxerr) goto err;

/* Output the title every so often */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s P-1 stage 2 (using %uMB)",
				 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pm1data.gwdata), memused);
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&pm1data.gwdata);
		}

/* Write out a message every now and then */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s stage 2 is %.*f%% complete.",
				 gwmodulo_as_string (&pm1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, " Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&pm1data.gwdata);
			first_iter_msg = FALSE;
		}

/* Write out a message to the results file every now and then */

		if ((ITER_OUTPUT_RES != 999999999 && gw_get_fft_count (&pm1data.gwdata) >= last_output_r + 2 * ITER_OUTPUT_RES) ||
		    (NO_GUI && stop_reason)) {
			sprintf (buf, "%s stage 2 is %.*f%% complete.\n",
				 gwmodulo_as_string (&pm1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			writeResults (buf);
			last_output_r = gw_get_fft_count (&pm1data.gwdata);
		}

/* Periodicly write a save file */

		if (stop_reason || saving) {
			pm1_save (&pm1data);
			if (stop_reason) goto exit;
		}
	}
	pm1data.C_done = pm1data.interim_C;

/* Free up memory */

	for (i = 0; i < pm1data.totrels; i++) gwfree (&pm1data.gwdata, pm1data.nQx[i]);
	free (pm1data.nQx), pm1data.nQx = NULL;
	free (pm1data.pairmap), pm1data.pairmap = NULL;

/* Check for the rare cases where we need to do even more stage 2.  This happens when continuing a save file in the middle of stage 2 and */
/* the save file's target bound #2 is smaller than our target bound #2. */

	if (pm1data.C > pm1data.C_done) {
		pm1data.state = PM1_STATE_MIDSTAGE;
		goto more_C;
	}

/* All done except for the final GCD, go do it. */

	goto stage2_done;

/*---------------------------------------------------------------------
|	Polymult stage 2!  See
|		AN FFT EXTENSION TO THE P-1 FACTORING ALGORITHM
|		by Montgomery & Silverman
|		Mathematics of Computation
|		Volume 54, number 190
|		April 1990, pages 839-854
|	as well as
|		IMPROVED STAGE 2 TO P±1 FACTORING ALGORITHMS
|		by Montgomery & Kruppa
|		2008, inria-00188192v3
|		https://hal.inria.fr/inria-00188192v3/document
+---------------------------------------------------------------------*/

pm1_polymult:
	uint64_t outpoly_size;			// Called L in the literature.  Make this value as large as we can given amount of free memory.
	gwnum	*poly1, *poly2, *outpoly, r, invr, e;

	// Poly sizes were set by pm1_stage2_impl.  Poly #1 size = pm1data.numrels, called f(X) in the literature.
	// Poly #2 is used to evaluate poly #1 at multiple points.  Make poly #2 as large as we can.  This was calculated in pm1_stage2_cost.
	// Other than poly #1, we must save these values in gwnums (r^2, gg, diff1) plus binary values (x and 1/x) for save file.
	// Poly #2 size = (pm1data.stage2_numvals - 3.5) - pm1data.poly1_size.
//GW: Might it be beneficial to make poly#2 just smaller than a supported polymult FFT size?
	ASSERTG (pm1data.poly2_size > pm1data.poly1_size*2);
	// Compute output poly size -- we use circular convolution with an FFT size supported by the polymult library
	outpoly_size = polymult_fft_size (pm1data.poly2_size);
	// Need numrels coefficients in poly #1.  Need 2*numrels+1 coefficients in poly #2 to eval poly #1 at one point.
	// Each additional coefficient evaluates one more point.
	pm1data.num_points = pm1data.poly2_size - (2*pm1data.poly1_size+1) + 1;
	ASSERTG (pm1data.numDsections % pm1data.num_points == 0);

// Polymult stage 2 does not need a prime sieve

	end_sieve (pm1data.sieve_info), pm1data.sieve_info = NULL;

/* Output a useful message regarding memory usage, poly sizes. */

	sprintf (buf, "Using %uMB of memory.  D: %d, %" PRIu64 "x%" PRIu64 " polynomial multiplication.\n", memused, pm1data.D, pm1data.poly1_size, pm1data.poly2_size);
	OutputStr (thread_num, buf);

/* Allocate array of pointers to gwnums.  This will be the coefficients of our input and output polynomials. */

	poly1 = gwalloc_array (&pm1data.gwdata, pm1data.poly1_size);	// Poly #1, a monic RLP of size numrels
	if (poly1 == NULL) goto lowmem;
	// Allocate output poly array.  This array tells polymult where to write the polymult results.
	pm1data.nQx = (gwnum *) malloc ((size_t) (outpoly_size * sizeof (gwnum)));
	if (pm1data.nQx == NULL) goto lowmem;
	outpoly = pm1data.nQx;					
	memset (outpoly, 0, (size_t) (outpoly_size * sizeof (gwnum)));

// From Montgomery/Silverman paper (near formulas 9.4 & 9.5), D = 2 mod 4.  Calculate monic RLP coefficients
// for u=1..D/4  if (gcd (u, D/2) == 1) compute V(2u).  The monic RLP is (x^2 - V(2u) + 1).
/* A fast 1,5 mod 6 poly #1 initialization for relative primes less than D/4 */

	ASSERTG (pm1data.D % 3 == 0);
	ASSERTG (pm1data.D % 4 == 2);			// See if we can relax this requirement
	{
		gwnum	t1, t2, V1, V2, V3, V5, V6;
		struct {
			gwnum	Vi;
			gwnum	Vi_minus6;
		} V1mod6, V5mod6, *curr;
		int	i_gap, totrels;

/* Allocate memory and init values for computing poly1.  We need V_1, V_5, V_6. */

		t1 = gwalloc (&pm1data.gwdata);
		if (t1 == NULL) goto lowmem;
		t2 = gwalloc (&pm1data.gwdata);
		if (t2 == NULL) goto lowmem;

/* Compute V_2, V_5, V_6 */

		V1 = pm1data.V, pm1data.V = NULL;
		V2 = t1;
		luc_dbl (&pm1data.gwdata, V1, V2);				// V2 = 2 * V1

		V3 = t2;
		luc_add (&pm1data.gwdata, V2, V1, V1, V3);			// V3 = V2 + V1 (diff V1)

		V5 = V2;
		luc_add (&pm1data.gwdata, V3, V2, V1, V5);			// V5 = V3 + V2 (diff V1), V2 no longer needed

		V6 = V3;
		luc_dbl (&pm1data.gwdata, V3, V6);				// V6 = 2 * V3, V3 no longer needed

/* Init structures used in the loop below */

		V1mod6.Vi = V1;
		V1mod6.Vi_minus6 = V5;
		V5mod6.Vi = V5;
		V5mod6.Vi_minus6 = V1;
		totrels = 0;

/* Compute the rest of the poly1 coefficients (V_i for i >= 7) */

		for (i = 1, i_gap = 4; /*i <= pm1data.D/4*/; i += i_gap, i_gap = 6 - i_gap) {
			gwnum	next_i;

/* Point to the i, i-6 pair to work on */

			curr = (i_gap == 4) ? &V1mod6 : &V5mod6;

/* If i is a relp of D, then add V(2*i) to the poly1 array.  All powers-of-two times i are also relp to D/4, add V(2*two_power*i) to the nQx array too. */

			if (relatively_prime (i, pm1data.D)) {
				gwnum	V_to_double = curr->Vi;
				for (int k = 1; i * k <= pm1data.D / 4; k *= 2) {
					luc_dbl (&pm1data.gwdata, V_to_double, poly1[totrels]);
					V_to_double = poly1[totrels];
					totrels++;
				}
				if (totrels >= pm1data.numrels) break;
			}

/* Get next i value.  Don't destroy Vi_minus6 for V1 and V5. */

			if (i < 6) {
				next_i = gwalloc (&pm1data.gwdata);
				if (next_i == NULL) goto lowmem;
			} else
				next_i = curr->Vi_minus6;

			luc_add (&pm1data.gwdata, curr->Vi, V6, curr->Vi_minus6, next_i);	// Next i

/* Shuffle i values along */

			curr->Vi_minus6 = curr->Vi;
			curr->Vi = next_i;

/* Check for errors, user esc, restart for mem changed, etc. */

			if (gw_test_for_error (&pm1data.gwdata)) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}
		ASSERTG (totrels == pm1data.numrels && i <= pm1data.D/4);

/* Free memory used in computing poly1 values */

		gwfree (&pm1data.gwdata, V1mod6.Vi);
		gwfree (&pm1data.gwdata, V1mod6.Vi_minus6);
		gwfree (&pm1data.gwdata, V5mod6.Vi);
		gwfree (&pm1data.gwdata, V5mod6.Vi_minus6);
		gwfree (&pm1data.gwdata, V6);
	}

//	bool use_polymult_multithreading;		// TRUE if we should use polymult multithreading instead of gwnum multithreading
//GW: UNUSED.  Change to always use polymult_multithreading?
//	use_polymult_multithreading = pm1data.polydata.num_threads > 1 && pm1data.poly2_size > 2*pm1data.polydata.num_threads &&
//				      (int) gwfftlen (&pm1data.gwdata) <= IniGetInt (INI_FILE, "PolyThreadingFFTlength", 256) * 1024;
//	if (use_polymult_multithreading) {
		pm1data.helper_count = pm1data.stage2_threads;
//	} else
//		pm1data.helper_count = 1;

// Increase safety margin for proper polymult operation

	gwset_polymult_safety_margin (&pm1data.gwdata, polymult_safety_margin (pm1data.poly1_size, pm1data.poly2_size));

// Multiply monic RLPs into one big monic RLP.  This is done in-place.

	{
		int	num_polys = pm1data.numrels;	// Number of polys to multiply together
		int	poly_size = 1;			// All polys are this size (except maybe the last one)
		int	last_poly_size = 1;		// Size of the last poly
		while (num_polys > 1) {
			for (int i = 0; i < num_polys-1; i += 2) {
				int poly2_size = (i+1 < num_polys-1) ? poly_size : last_poly_size;
				polymult (&pm1data.polydata, &poly1[i*poly_size], poly_size, &poly1[(i+1)*poly_size], poly2_size,
					  &poly1[i*poly_size], poly_size + poly2_size,
					  (poly_size == 1 ? POLYMULT_INVEC1_NEGATE : 0) +
					  (poly2_size == 1 ? POLYMULT_INVEC2_NEGATE : 0) +
					  POLYMULT_INVEC1_MONIC_RLP | POLYMULT_INVEC2_MONIC_RLP | POLYMULT_NO_UNFFT);
			}
			// Multithreaded unfft poly coefficients
			poly_unfft_fft_coefficients (&pm1data.polydata, poly1, pm1data.poly1_size);
			// Poly sizes have doubled
			if ((num_polys & 1) == 0) last_poly_size += poly_size;
			poly_size *= 2;
			num_polys = (num_polys + 1) / 2;
if (IniGetInt (INI_FILE, "PolyVerbose", 0)) {
if (poly_size==2)sprintf (buf, "Round off: %.10g, poly_size: %d, EB: %g, SM: %g\n", gw_get_maxerr (&pm1data.gwdata), poly_size, pm1data.gwdata.EXTRA_BITS, pm1data.gwdata.safety_margin);
else sprintf (buf, "Round off: %.10g, poly_size: %d\n", gw_get_maxerr (&pm1data.gwdata), poly_size);
OutputStr (thread_num, buf); gw_clear_maxerr (&pm1data.gwdata);
}
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}
	}

// Prep monic RLP poly #1 for evaluation at multiple points by polymult/FFT (see Montgomery/Kruppa)
// If j = numrels, multiply the j+1 RLP coefficients by r^(-(j^2)), r^(-(j-1)^2) ... r^(-1^2), r^(-0^2).
// Multipliers are:	       .... r^-9  r^-4  r^-1  r^0
// Differences are:		....  r^-5  r^-3  r^-1
// Differences are:	            ...  r^-2  r^-2

	// Set r = x^(D/2), free x
	r = pm1data.x, pm1data.x = NULL;
	exponentiate (&pm1data.gwdata, r, pm1data.D / 2);
	// Calc r^2 = x^D
	pm1data.r_squared = gwalloc (&pm1data.gwdata);
	if (pm1data.r_squared == NULL) goto lowmem;
	gwsquare2 (&pm1data.gwdata, r, pm1data.r_squared, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
	gwfft (&pm1data.gwdata, pm1data.r_squared, pm1data.r_squared);
	// Set invr = x^-(D/2), free invx
	invr = pm1data.invx, pm1data.invx = NULL;
	exponentiate (&pm1data.gwdata, invr, pm1data.D / 2);
	{
		// Set diff2 to r^-2
		gwnum diff2 = gwalloc (&pm1data.gwdata);
		if (diff2 == NULL) goto lowmem;
		gwsquare2 (&pm1data.gwdata, invr, diff2, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);				// r^-2
		// Set diff1 to r^-1
		gwnum diff1 = gwalloc (&pm1data.gwdata);
		if (diff1 == NULL) goto lowmem;
		gwcopy (&pm1data.gwdata, invr, diff1);									// r^-1
		// Unlike the Montgomery/Kruppa algorithm, multiply all coefficients by an additional r^(j^2) so that RLP remains monic.
		gwnum multiplier = gwalloc (&pm1data.gwdata);
		if (multiplier == NULL) goto lowmem;
		if (pm1data.numrels & 1) {		// Since numrels is often even we save one multiply by exponentiating from r^2
			gwcopy (&pm1data.gwdata, r, multiplier);
			exponentiate (&pm1data.gwdata, multiplier, (uint64_t) pm1data.numrels * (uint64_t) pm1data.numrels);	// r^(j^2)
		} else {
			gwcopy (&pm1data.gwdata, pm1data.r_squared, multiplier);
			exponentiate (&pm1data.gwdata, multiplier, (uint64_t) pm1data.numrels * (uint64_t) pm1data.numrels / 2);// r^(j^2)
		}
		// Apply first multiplier (r^0 * r^(j^2))
		gwmul3 (&pm1data.gwdata, poly1[0], multiplier, poly1[0], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);		// Mul by r^(j^2)
		// Apply second multiplier (r^-1 * r^(j^2))
		gwmul3 (&pm1data.gwdata, multiplier, diff1, multiplier, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);		// r^(j^2)*r^-1
		gwmul3 (&pm1data.gwdata, poly1[1], multiplier, poly1[1], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);		// Mul by r^(j^2-1)
		// Loop multiplying the remaining RLP coefficients
		for (int j = 2; j < pm1data.poly1_size; j++) {
			gwmul3 (&pm1data.gwdata, diff1, diff2, diff1, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			gwmul3 (&pm1data.gwdata, multiplier, diff1, multiplier, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			gwmul3 (&pm1data.gwdata, poly1[j], multiplier, poly1[j], GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}
		gwfree (&pm1data.gwdata, diff2);
		gwfree (&pm1data.gwdata, diff1);
		gwfree (&pm1data.gwdata, multiplier);
	}

// Compress and pre-transpose the monic RLP -- will reduce monic RLP mem usage by maybe 1.6% without compression plus another 12.5% with compression.

	if (IniGetInt (INI_FILE, "Poly1Compress", 2)) {
		int options = POLYMULT_INVEC1_MONIC_RLP | POLYMULT_CIRCULAR;
		if (IniGetInt (INI_FILE, "Poly1Compress", 2) == -1) options |= POLYMULT_PRE_FFT;				// Hidden option (uses lots of memory)
		if (IniGetInt (INI_FILE, "Poly1Compress", 2) == -2) options |= POLYMULT_PRE_FFT | POLYMULT_PRE_COMPRESS;	// Hidden option (uses lots of memory)
		if (IniGetInt (INI_FILE, "Poly1Compress", 2) == 2) options |= POLYMULT_PRE_COMPRESS;
		gwarray tmp = polymult_preprocess (&pm1data.polydata, poly1, pm1data.poly1_size, pm1data.poly2_size, outpoly_size, options);
		gwfree_array (&pm1data.gwdata, poly1);
		poly1 = tmp;
//		sprintf (buf, "Gwnum size: %lu, unpadded gwnum size: %lu, est. compressed size: %6.4f, act. compressed size: %6.4f, excess: %6.4f\n",
//			 gwnum_size (&pm1data.gwdata), gwfftlen (&pm1data.gwdata)*8,
//			 (double) gwfftlen (&pm1data.gwdata)*8 / (double) gwnum_size (&pm1data.gwdata),
//			 (double) preprocessed_poly_size (poly1) / ((double) pm1data.poly1_size * (double) gwnum_size (&pm1data.gwdata)),
//			 ((double) preprocessed_poly_size (poly1) / ((double) pm1data.poly1_size * (double) gwnum_size (&pm1data.gwdata))) /
//			 ((double) gwfftlen (&pm1data.gwdata)*8 / (double) gwnum_size (&pm1data.gwdata)));
//		OutputStr (thread_num, buf);
	}

// Create poly #2

// From the Montgomery/Kruppa paper, to evaluate poly #1 at a single point, do a polymult with these 2*numrels-1 coefficients:
//		e^-j*r^(j^2) ...  e^-2*r^(2^2)  e^-1*r^(1^2)  e^0*r^0  e^1*r^(1^2) ...  e^j*r^(j^2)    j = 0..relps/2
// To evaluate poly #1 at a second point, simply add one more coefficient, e^(j+1)*r^((j+1)^2), and so on.
// Use differences to compute coefficients:   e^-j*r^(j^2) ... e^-2*r^(2^2)  e^-1*r^(1^2)  e^0*r^0  e^1*r^(1^2)  e^2*r^(2^2) ...  e^j*r^(j^2)
// Differences are:			        e*r^((j-1)^2-j^2)  ...  e*r^-3         e*r^-1    e*r^1       e*r^3  ...
// Differences are:						          ...    r^2          r^2       r^2    ...
//
// We could save one coefficient by making the above a monic polynomial.  Simply multiply all coefficients by e^j * r^-(j^2) giving:
//		1  e^1*r^((j-1)^2-j^2) ...  e^(j-2)*r^(2^2-j^2)  e^(j-1)*r^(1^2-j^2)  e^j*r^(0-j^2)  e^(j+1)*r^(1^2-j^2) ...  e^(j+j)*r^(j^2-j^2)
// However, only the first iteration of the main loop can do this.  It is not worth complicating the code for one extra point of evaluation.
// Instead we multiply each coefficient by e^j and diff1*r^(-j^2) so that we do not need to calculate e^-j or r^(j^2).  These multiplications
// do not change the differences calculations.
//
// Calculate first diff1 (e*r^((j-1)^2-j^2)) value so that the main loop can calculate the rest of poly #2.  Allocate gwnums for poly #2.

	// Calculate e (the first polyeval point).  e = x^(B2_start+D/2), this can be computed in terms of r = x^(D/2).  B2_start is a multiple of D.  Free r.
	e = r, r = NULL;
	ASSERTG (pm1data.B2_start % pm1data.D == 0);
	exponentiate (&pm1data.gwdata, e, (pm1data.B2_start + pm1data.D / 2) / (pm1data.D / 2));

	// Calculate first diff1 (e*r^((j-1)^2-j^2)), free e, invr.
	exponentiate (&pm1data.gwdata, invr, pm1data.numrels * 2 - 1);
   	pm1data.diff1 = e, gwmul3 (&pm1data.gwdata, e, invr, pm1data.diff1, GWMUL_STARTNEXTFFT), e = NULL;
	gwfree (&pm1data.gwdata, invr), invr = NULL;

	// Compute r^(2*helper_count) and r^(2*helper_count^2).
	if (pm1data.helper_count == 1) {
		pm1data.r_2helper = pm1data.r_squared;
		pm1data.r_2helper2 = pm1data.r_squared;
	} else {
		pm1data.r_2helper = gwalloc (&pm1data.gwdata);
		if (pm1data.r_2helper == NULL) goto lowmem;
		gwcopy (&pm1data.gwdata, pm1data.r_squared, pm1data.r_2helper);
		exponentiate (&pm1data.gwdata, pm1data.r_2helper, pm1data.helper_count);
		gwfft (&pm1data.gwdata, pm1data.r_2helper, pm1data.r_2helper);
		pm1data.r_2helper2 = gwalloc (&pm1data.gwdata);
		if (pm1data.r_2helper2 == NULL) goto lowmem;
		gwcopy (&pm1data.gwdata, pm1data.r_2helper, pm1data.r_2helper2);
		exponentiate (&pm1data.gwdata, pm1data.r_2helper2, pm1data.helper_count);
		gwfft (&pm1data.gwdata, pm1data.r_2helper2, pm1data.r_2helper2);
	}

// Re-initialize gg.  If helper_count > 1 we reduce peak mem usage by delaying this re-init until r_squared is freed */

	if (pm1data.helper_count == 1) {
		ASSERTG (pm1data.gg == NULL);
		pm1data.gg = gwalloc (&pm1data.gwdata);
		if (pm1data.gg == NULL) goto lowmem;
		gianttogw (&pm1data.gwdata, pm1data.gg_binary, pm1data.gg);
		free (pm1data.gg_binary), pm1data.gg_binary = NULL;
	}

	// Free cached and internal memory for upcoming peak memory usage
	gwfree_internal_memory (&pm1data.gwdata);
	mallocFreeForOS ();

	// Allocate poly #2 in one big array.  Poly #2 is a monic polynomial of size 2 * numrels + num_points - 1.
	poly2 = gwalloc_array (&pm1data.gwdata, pm1data.poly2_size);
	if (poly2 == NULL) goto lowmem;

// The evaluated points during a polymult overwrite some of the poly #2 coefficients.  Take advantage of this by using circular convolution where
// the "wrapped" results are coefficients we don't care about.  The output poly is the first fft size >= poly2_size.

/* Stage 2 init complete, change the title, output a message */

	sprintf (buf, "%.*f%% of %s P-1 stage 2 (using %uMB)", (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pm1data.gwdata), memused);
	title (thread_num, buf);

	end_timer (timers, 0);
	sprintf (buf, "Stage 2 init complete. %" PRIu64 " transforms. Time: ", gw_get_fft_count (&pm1data.gwdata));
	print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&pm1data.gwdata);

// Stage 2 init is successful.  Set interim_C so that continuing from a save file will go to our current B2_end.  This will ensure that the
// relocatable primes just below b2_start will get properly relocated.

	ASSERTG (pm1data.C <= pm1data.B2_start + pm1data.numDsections * pm1data.D);
	ASSERTG (pm1data.interim_C <= pm1data.B2_start + pm1data.numDsections * pm1data.D);
	pm1data.interim_C = pm1data.B2_start + pm1data.numDsections * pm1data.D;

// Calculate the percent completed by previous stage 2 efforts

	base_pct_complete = (double) (pm1data.B2_start - pm1data.last_relocatable) /
			    (double) (pm1data.B2_start + pm1data.numDsections * pm1data.D - pm1data.last_relocatable);

// Before embarking on multithreaded computation of poly #2 coefficients, we must compute the first few coefficients and first few differences.
// This lets each helper thread independently compute their own subset of the remaining poly #2 coefficients.
// The first time poly #2 coefficients are computed by finite differences where diff1 was computed during init and diff2 is r^2.
// Compute one coefficient for each helper thread and if there is more than one helper thread accumulate a "big diff1".

	// Copy diff1 as first poly2 coefficient.  In theory this can be any non-zero value, but we don't use one as polymult can have
	// trouble computing 1*1!  Also compute second poly2 coefficient.
	gwsquare2 (&pm1data.gwdata, pm1data.diff1, poly2[pm1data.poly2_size - 2], GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
	gwcopy (&pm1data.gwdata, pm1data.diff1, poly2[pm1data.poly2_size - 1]);

	// If there are helper threads, compute more coefficients and the "big" diff1.
	if (pm1data.helper_count > 1) {
		gwnum	small_diff1, big_diff1;
		gwcopy (&pm1data.gwdata, pm1data.diff1, small_diff1 = poly2[0]);	// Compute small diff1 values in a scratch gwnum
		big_diff1 = pm1data.diff1;						// Accumlate the first big diff1
		for (int j = 1; j < pm1data.helper_count; j++) {
			// Compute next small diff1
			gwmul3 (&pm1data.gwdata, small_diff1, pm1data.r_squared, small_diff1, GWMUL_STARTNEXTFFT);
			// Compute next poly #2 coefficient
			gwmul3 (&pm1data.gwdata, poly2[pm1data.poly2_size-1-j], small_diff1, poly2[pm1data.poly2_size-2-j], GWMUL_FFT_S12 | GWMUL_STARTNEXTFFT);
			// Accumulate big diff1
			gwmul3 (&pm1data.gwdata, big_diff1, small_diff1, big_diff1, GWMUL_STARTNEXTFFT);
		}
		// Do the re-initialize of gg that we delayed to reduce peak mem usage, free r_squared. */
		pm1data.gg = pm1data.r_squared, pm1data.r_squared = NULL;
		gianttogw (&pm1data.gwdata, pm1data.gg_binary, pm1data.gg);
		free (pm1data.gg_binary), pm1data.gg_binary = NULL;
	}

/* We're at peak memory usage, free any cached gwnums */

	gwfree_cached (&pm1data.gwdata);
	mallocFreeForOS ();

/* Set required_missing for continuation from a polymult PM1_STATE_STAGE2 */

	if (pm1data.B2_start > pm1data.C_done) pm1data.required_missing = map_polyD_to_first_missing_prime (pm1data.D);

// Loop over all the D-sections between B1 and B2

	pm1data.state = PM1_STATE_STAGE2;
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);
	last_output = last_output_t = last_output_r = 0;
	for (bool first_polymult = TRUE; ; first_polymult = FALSE) {

// On first loop iteration, we have num_helper_threads+1 poly2 coefficients already computed.

		if (first_polymult) {
			// Count of remaining poly2 coefficients to compute
			pm1data.remaining_poly2_size = pm1data.poly2_size - (pm1data.helper_count + 1);
		}

// Subsequent loop iterations reuse 2*numrels coefficients computed in the previous iteration.  Diff1 is saved from the previous computation of poly2[0].

		else {
			// Count of remaining poly2 coefficients to compute
			pm1data.remaining_poly2_size = pm1data.num_points;
		}
		
		// Compute next diff1 value for each helper thread
		gwnum	prev_diff;
		for (int j = 0; j < pm1data.helper_count; j++) {
			gwnum this_diff = poly2[(pm1data.remaining_poly2_size - 1 - j) % pm1data.helper_count];
			if (j == 0) gwmul3 (&pm1data.gwdata, pm1data.diff1, pm1data.r_2helper, this_diff, GWMUL_STARTNEXTFFT);
			else gwmul3 (&pm1data.gwdata, prev_diff, pm1data.r_2helper, this_diff, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);
			prev_diff = this_diff;
		}

// Multithreaded computation of remaining poly2 coefficients

		// Initialize data needed for helper routine to access each coefficient
		pm1data.polydata.helper_callback = &pm1_helper;
		pm1data.polydata.helper_callback_data = &pm1data;
		pm1data.helper_work = PM1_COMPUTE_POLY2;				// Work type for helper to do
		pm1data.poly2 = poly2;							// Array of poly2 coefficients

		// If using polymult helper threads to accumulate the evaluated points, set gwnum to use only one thread and launch the helpers
		polymult_launch_helpers (&pm1data.polydata);
//GW: would need to set atomic		// else pm1_helper (0, &pm1data.gwdata, &pm1data);				// Or just have main thread do the work

// Check for ESC before doing expensive polymult

		stop_reason = stopCheck (thread_num);
		if (stop_reason) { pm1_save (&pm1data); goto exit; }

// The evaluated points during a polymult overwrite many of the poly #2 coefficients the rest are preserved.  Take advantage of this by using circular
// convolution where the "wrapped" results are coefficients we don't care about.  The output poly size is the first poly fft size >= poly2_size.

		memcpy (outpoly + 2*pm1data.numrels, poly2 + 2*pm1data.numrels, (size_t) (pm1data.num_points * sizeof (gwnum)));

// Evaluate poly #1 at many points using polymult!

//GW: Does the fft of a RLP have any special format?  Such as half the data?  Can that be meaningfully exploited? See Montgomery/Kruppa 6.3.
		polymult (&pm1data.polydata, poly1, pm1data.poly1_size, poly2, pm1data.poly2_size, outpoly, outpoly_size,
			  POLYMULT_INVEC1_MONIC_RLP | POLYMULT_CIRCULAR | POLYMULT_NO_UNFFT);
if (pm1data.Dsection==0 && IniGetInt (INI_FILE, "PolyVerbose", 0)) {
sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&pm1data.gwdata));
OutputStr (thread_num, buf); gw_clear_maxerr (&pm1data.gwdata);}

// Check for errors, user esc, restart for mem changed, saving, etc.

		if (gw_test_for_error (&pm1data.gwdata)) goto err;
		stop_reason = stopCheck (thread_num);
		saving = testSaveFilesFlag (thread_num);
//GW: Skip accumulating gg if stopping?  Save file prior to accumulating ggs??  All depends on how fast we think these gg accumulates will be

// Multithreaded accumulation of the evaluated points from outpoly into gg for a later GCD

		// Initialize data needed for helper routine to access each coefficient
		pm1data.polydata.helper_callback = &pm1_helper;
		pm1data.polydata.helper_callback_data = &pm1data;
		pm1data.helper_work = PM1_ACCUMULATING_GG;		// Work type for helper to do
		pm1data.points = poly2 + 2*pm1data.numrels;		// Points to accumulate were written over higher coefficients of poly2
//GW:	stop reason/saving?

		// If using polymult helper threads to accumulate the evaluated points, set gwnum to use only one thread and launch the helpers
//GW		if (use_polymult_multithreading)
			polymult_launch_helpers (&pm1data.polydata);
//GW: would need set_atomic		else pm1_helper (0, &pm1data.gwdata, &pm1data);		// Or just have main thread do the work

// Keep track of the sections processed thusfar

		pm1data.Dsection += pm1data.num_points;
		if (pm1data.Dsection >= pm1data.numDsections && pm1data.B2_start + pm1data.Dsection * pm1data.D >= pm1data.C) break;

/* Calculate stage 2 percentage. */

		w->pct_complete = base_pct_complete + (1.0 - base_pct_complete) * (double) pm1data.Dsection / (double) pm1data.numDsections;

/* Test for errors */

		if (gw_test_for_error (&pm1data.gwdata) || gw_get_maxerr (&pm1data.gwdata) > allowable_maxerr) goto err;

/* Output the title every so often */

//GW: The * 3 is a total guess to compensate for time spent in polymult
#define ITER_FUDGE 3
		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) * ITER_FUDGE >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s P-1 stage 2 (using %uMB)",
				 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pm1data.gwdata), memused);
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&pm1data.gwdata) * ITER_FUDGE;
		}

/* Write out a message every now and then */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pm1data.gwdata) * ITER_FUDGE >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s stage 2 at B2=%" PRIu64 " [%.*f%%]",
				 gwmodulo_as_string (&pm1data.gwdata), pm1data.B2_start + pm1data.Dsection * pm1data.D,
				 (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, ".  Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&pm1data.gwdata) * ITER_FUDGE;
			first_iter_msg = FALSE;
		}

/* Write out a message to the results file every now and then */

		if ((ITER_OUTPUT_RES != 999999999 && gw_get_fft_count (&pm1data.gwdata) * ITER_FUDGE >= last_output_r + 2 * ITER_OUTPUT_RES) ||
		    (NO_GUI && stop_reason)) {
			sprintf (buf, "%s stage 2 at B2=%" PRIu64 " [%.*f%%]\n",
				 gwmodulo_as_string (&pm1data.gwdata), pm1data.B2_start + pm1data.Dsection * pm1data.D,
				 (int) PRECISION, trunc_percent (w->pct_complete));
			writeResults (buf);
			last_output_r = gw_get_fft_count (&pm1data.gwdata) * ITER_FUDGE;
		}

/* Periodicly write a save file */

		if (stop_reason || saving) {
			pm1_save (&pm1data);
			if (stop_reason) goto exit;
		}

/* Rotate poly 2 coefficients.  We reuse the numrels*2 coefficients that were preserved during the polymult. */

		memmove (poly2 + pm1data.num_points, poly2, 2*pm1data.numrels * sizeof (gwnum));
		memcpy (poly2, outpoly + 2*pm1data.numrels, (size_t) (pm1data.num_points * sizeof (gwnum)));
	}

/* Set C_done for the final save file */

	pm1data.C = pm1data.C_done = pm1data.B2_start + pm1data.Dsection * pm1data.D;

/* Cleanup and free poly gwnums before GCD.  GCD can use significant amounts of memory. */

	gwfree (&pm1data.gwdata, pm1data.r_squared), pm1data.r_squared = NULL;
	gwfree (&pm1data.gwdata, pm1data.diff1), pm1data.diff1 = NULL;
	polymult_done (&pm1data.polydata);
	gwfree_array (&pm1data.gwdata, poly1);
	gwfree_array (&pm1data.gwdata, poly2);

/* Stage 2 is complete */

stage2_done:
	gwfree_internal_memory (&pm1data.gwdata);
	mallocFreeForOS ();
	set_memory_usage (thread_num, 0, cvt_gwnums_to_mem (&pm1data.gwdata, 1));	// Let other high memory workers resume
	end_timer (timers, 1);
	end_timer (&stage2_timer, 0);
	sprintf (buf, "%s stage 2 complete. %" PRIu64 " transforms. Total time: ", gwmodulo_as_string (&pm1data.gwdata), gw_get_fft_count (&pm1data.gwdata));
	print_timer (timers, 1, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);

/* Adjust fudge factor for stage2 vs. stage1 runtime ratio.  We use a rolling average. */

	if (stage1_timer != 0.0 && stage2_timer != 0.0 && IniGetInt (INI_FILE, "Pm1Stage2AutoAdjust", 1)) {
		double correct_ratio = stage2_timer / stage1_timer;
		double current_adjustment = IniGetFloat (INI_FILE, "Pm1Stage2RatioAdjust", 1.0);
		double correct_adjustment = correct_ratio / pm1data.est_stage2_stage1_ratio * current_adjustment;
		// Sanity check.  Estimation code should not be off by a factor of 3.
		if (correct_adjustment > 3.0) correct_adjustment = 3.0;
		if (correct_adjustment < 0.333) correct_adjustment = 0.333;
		// Set new adjustment for next time
		IniWriteFloat (INI_FILE, "Pm1Stage2RatioAdjust", (float) (0.9 * current_adjustment + 0.1 * correct_adjustment));
	}

/* Print out round off error */

	if (ERRCHK) {
		sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&pm1data.gwdata));
		OutputStr (thread_num, buf);
		gw_clear_maxerr (&pm1data.gwdata);
	}

/* See if we got lucky! */

restart4:
	if (pm1data.gg != NULL) {			// If necessary, convert gg to binary
		ASSERTG (pm1data.gg_binary == NULL);
		pm1data.gg_binary = allocgiant (((int) pm1data.gwdata.bit_length >> 5) + 10);
		if (pm1data.gg_binary == NULL) goto oom;
		gwtogiant (&pm1data.gwdata, pm1data.gg, pm1data.gg_binary);
		gwfree (&pm1data.gwdata, pm1data.gg), pm1data.gg = NULL;
	}
	pm1data.state = PM1_STATE_GCD;
	strcpy (w->stage, "S2");
	w->pct_complete = 1.0;
	if (w->work_type != WORK_PMINUS1) OutputStr (thread_num, "Starting stage 2 GCD - please be patient.\n");
	start_timer_from_zero (timers, 0);
	stop_reason = gcd (thread_num, pm1data.gg_binary, N, &factor);
	if (stop_reason) {
		pm1_save (&pm1data);
		goto exit;
	}
	pm1data.state = PM1_STATE_DONE;
	end_timer (timers, 0);
	strcpy (buf, "Stage 2 GCD complete. Time: ");
	print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	if (factor != NULL) goto bingo;

/* Output line to results file indicating P-1 run */

msg_and_exit:
	sprintf (buf, "%s completed P-1, B1=%" PRIu64, gwmodulo_as_string (&pm1data.gwdata), pm1data.B);
	if (pm1data.C > pm1data.B) sprintf (buf+strlen(buf), ", B2=%" PRIu64, pm1data.C);
	sprintf (buf+strlen(buf), ", Wi%d: %08lX\n", PORT, SEC5 (w->n, pm1data.B, pm1data.C));
	OutputStr (thread_num, buf);
	formatMsgForResultsFile (buf, w);
	writeResults (buf);

/* Format a JSON version of the result.  An example follows: */
/* {"status":"NF", "exponent":45581713, "worktype":"P-1", "b1":50000, "b2":5000000, "d":2310, "poly1-size":240, "poly2-size": 600, */
/* "fft-length":5120, "security-code":"39AB1238", "program":{"name":"prime95", "version":"29.5", "build":"8"}, "timestamp":"2019-01-15 23:28:16", */
/* "user":"gw_2", "cpu":"work_computer", "aid":"FF00AA00FF00AA00FF00AA00FF00AA00"} */

	strcpy (JSONbuf, "{\"status\":\"NF\"");
	JSONaddExponent (JSONbuf, w);
	strcat (JSONbuf, ", \"worktype\":\"P-1\"");
	sprintf (JSONbuf+strlen(JSONbuf), ", \"b1\":%" PRIu64, pm1data.B);
	if (pm1data.C > pm1data.B) {
		sprintf (JSONbuf+strlen(JSONbuf), ", \"b2\":%" PRIu64, pm1data.C);
		sprintf (JSONbuf+strlen(JSONbuf), ", \"d\":%d", pm1data.D);
		if (pm1data.stage2_type == PM1_STAGE2_POLYMULT) {
			sprintf (JSONbuf+strlen(JSONbuf), ", \"poly1-size\":%" PRIu64, pm1data.poly1_size);
			sprintf (JSONbuf+strlen(JSONbuf), ", \"poly2-size\":%" PRIu64, pm1data.poly2_size);
			sprintf (JSONbuf+strlen(JSONbuf), ", \"stage2-fft-length\":%lu", gwfftlen (&pm1data.gwdata));
		}
	}
	sprintf (JSONbuf+strlen(JSONbuf), ", \"fft-length\":%lu", pm1data.stage1_fftlen);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"security-code\":\"%08lX\"", SEC5 (w->n, pm1data.B, pm1data.C > pm1data.B ? pm1data.C : pm1data.B));
	JSONaddProgramTimestamp (JSONbuf);
	JSONaddExponentKnownFactors (JSONbuf, w);
	JSONaddUserComputerAID (JSONbuf, w);
	strcat (JSONbuf, "}");
	if (IniGetInt (INI_FILE, "OutputJSON", 1)) writeResultsJSON (JSONbuf);

/* Send P-1 completed message to the server.  Although don't do it for puny B1 values as this is just the user tinkering with P-1 factoring. */

	if (!QA_IN_PROGRESS && (pm1data.B >= 50000 || IniGetInt (INI_FILE, "SendAllFactorData", 0))) {
		struct primenetAssignmentResult pkt;
		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		strcpy (pkt.assignment_uid, w->assignment_uid);
		strcpy (pkt.message, buf);
		pkt.result_type = PRIMENET_AR_P1_NOFACTOR;
		pkt.k = w->k;
		pkt.b = w->b;
		pkt.n = w->n;
		pkt.c = w->c;
		pkt.B1 = pm1data.B;
		pkt.B2 = pm1data.C;
		pkt.fftlen = pm1data.stage1_fftlen;
		pkt.done = (w->work_type == WORK_PMINUS1 || w->work_type == WORK_PFACTOR);
		strcpy (pkt.JSONmessage, JSONbuf);
		spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
	}

/* If this is pre-factoring for an LL or PRP test, then delete the large save file. */
/* Optionally create save file so that we can expand bound 1 or bound 2 at a later date. */

	unlinkSaveFiles (&pm1data.write_save_file_state);
	if (!QA_IN_PROGRESS && w->work_type == WORK_PMINUS1 && IniGetInt (INI_FILE, "KeepPminus1SaveFiles", 1))
		pm1_save (&pm1data);

/* Return stop code indicating success or work unit complete */ 

done:	if (w->work_type == WORK_PMINUS1 || w->work_type == WORK_PFACTOR)
		stop_reason = STOP_WORK_UNIT_COMPLETE;
	else {
		w->pminus1ed = 1;		// Flag to indicate LL test has completed P-1
		w->tests_saved = 0.0;		// Variable to indicate PRP test has completed P-1
		stop_reason = updateWorkToDoLine (thread_num, w);
		if (stop_reason) goto exit;
	}

/* Free memory and return */

exit:	Dmultiple_map.clear ();
	relp_set_map.clear ();
	pm1_cleanup (&pm1data);
	free (N);
	free (factor);
	free (str);
	free (msg);
	if (exp_initialized) mpz_clear (exp);
	return (stop_reason);

/* Low or possibly low on memory in stage 2 init, create save file, reduce memory settings, and try again */

lowmem:	stop_reason = OutOfMemory (thread_num);
possible_lowmem:
	if (pm1data.state == PM1_STATE_MIDSTAGE) pm1_save (&pm1data);
	if (stop_reason != STOP_OUT_OF_MEM) goto exit;
//GW: saving file twice?
	pm1_save (&pm1data);
	Dmultiple_map.clear ();
	relp_set_map.clear ();
	pm1_cleanup (&pm1data);
	OutputBoth (thread_num, "Memory allocation error.  Trying again using less memory.\n");
	pm1data.pct_mem_to_use *= 0.8;
	goto restart;

/* We've run out of memory.  Print error message and exit. */

oom:	stop_reason = OutOfMemory (thread_num);
	goto exit;

/* Print a message if we found a factor! */

bingo:	if (pm1data.state < PM1_STATE_MIDSTAGE)
		sprintf (buf, "P-1 found a factor in stage #1, B1=%" PRIu64 ".\n", pm1data.B);
	else
		sprintf (buf, "P-1 found a factor in stage #2, B1=%" PRIu64 ", B2=%" PRIu64 ".\n", pm1data.B, pm1data.C);
	OutputBoth (thread_num, buf);

/* Allocate memory for the string representation of the factor and for */
/* a message.  Convert the factor to a string.  Allocate lots of extra space */
/* as formatMsgForResultsFile can append a lot of text. */	

	msglen = factor->sign * 10 + 400;
	str = (char *) malloc (msglen);
	if (str == NULL) {
		stop_reason = OutOfMemory (thread_num);
		goto exit;
	}
	msg = (char *) malloc (msglen);
	if (msg == NULL) {
		stop_reason = OutOfMemory (thread_num);
		goto exit;
	}
	gtoc (factor, str, msglen);

/* Validate the factor we just found */

	if (!testFactor (&pm1data.gwdata, w, factor)) {
		sprintf (msg, "ERROR: Bad factor for %s found: %s\n", gwmodulo_as_string (&pm1data.gwdata), str);
		OutputBoth (thread_num, msg);
		unlinkSaveFiles (&pm1data.write_save_file_state);
		OutputStr (thread_num, "Restarting P-1 from scratch.\n");
		stop_reason = 0;
		goto error_restart;
	}

/* Output the validated factor */

	if (pm1data.state < PM1_STATE_MIDSTAGE)
		sprintf (msg, "%s has a factor: %s (P-1, B1=%" PRIu64 ")\n",
			 gwmodulo_as_string (&pm1data.gwdata), str, pm1data.B);
	else
		sprintf (msg, "%s has a factor: %s (P-1, B1=%" PRIu64 ", B2=%" PRIu64 ")\n",
			 gwmodulo_as_string (&pm1data.gwdata), str, pm1data.B, pm1data.C);
	OutputStr (thread_num, msg);
	formatMsgForResultsFile (msg, w);
	writeResults (msg);

/* Format a JSON version of the result.  An example follows: */
/* {"status":"F", "exponent":45581713, "worktype":"P-1", "factors":["430639100587696027847"], */
/* "b1":50000, "b2":5000000, "d":2310, "poly1-size":240, "poly2-size": 600, "fft-length":5120, */
/* "security-code":"39AB1238", "program":{"name":"prime95", "version":"29.5", "build":"8"}, "timestamp":"2019-01-15 23:28:16", */
/* "user":"gw_2", "cpu":"work_computer", "aid":"FF00AA00FF00AA00FF00AA00FF00AA00"} */

	strcpy (JSONbuf, "{\"status\":\"F\"");
	JSONaddExponent (JSONbuf, w);
	strcat (JSONbuf, ", \"worktype\":\"P-1\"");
	sprintf (JSONbuf+strlen(JSONbuf), ", \"factors\":[\"%s\"]", str);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"b1\":%" PRIu64, pm1data.B);
	if (pm1data.state > PM1_STATE_MIDSTAGE) {
		sprintf (JSONbuf+strlen(JSONbuf), ", \"b2\":%" PRIu64, pm1data.C);
		sprintf (JSONbuf+strlen(JSONbuf), ", \"d\":%d", pm1data.D);
		if (pm1data.stage2_type == PM1_STAGE2_POLYMULT) {
			sprintf (JSONbuf+strlen(JSONbuf), ", \"poly1-size\":%" PRIu64, pm1data.poly1_size);
			sprintf (JSONbuf+strlen(JSONbuf), ", \"poly2-size\":%" PRIu64, pm1data.poly2_size);
			sprintf (JSONbuf+strlen(JSONbuf), ", \"stage2-fft-length\":%lu", gwfftlen (&pm1data.gwdata));
		}
	}
	sprintf (JSONbuf+strlen(JSONbuf), ", \"fft-length\":%lu", pm1data.stage1_fftlen);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"security-code\":\"%08lX\"", SEC5 (w->n, pm1data.B, pm1data.state > PM1_STATE_MIDSTAGE ? pm1data.C : pm1data.B));
	JSONaddProgramTimestamp (JSONbuf);
	JSONaddExponentKnownFactors (JSONbuf, w);
	JSONaddUserComputerAID (JSONbuf, w);
	strcat (JSONbuf, "}");
	if (IniGetInt (INI_FILE, "OutputJSON", 1)) writeResultsJSON (JSONbuf);

/* Send assignment result to the server.  To avoid flooding the server with small factors from users needlessly redoing */
/* factoring work, make sure the factor is more than 67 bits or so. */

	if (!QA_IN_PROGRESS && (strlen (str) >= 21 || IniGetInt (INI_FILE, "SendAllFactorData", 0))) {
		struct primenetAssignmentResult pkt;
		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		strcpy (pkt.assignment_uid, w->assignment_uid);
		strcpy (pkt.message, msg);
		pkt.result_type = PRIMENET_AR_P1_FACTOR;
		pkt.k = w->k;
		pkt.b = w->b;
		pkt.n = w->n;
		pkt.c = w->c;
		truncated_strcpy (pkt.factor, sizeof (pkt.factor), str);
		pkt.B1 = pm1data.B;
		pkt.B2 = (pm1data.state < PM1_STATE_MIDSTAGE ? 0 : pm1data.C);
		pkt.fftlen = gwfftlen (&pm1data.gwdata);
		pkt.done = TRUE;
		strcpy (pkt.JSONmessage, JSONbuf);
		spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
	}

/* Free save files.  If LL testing, free those save files too. */
/* Then create save file so that we can expand bound 1 or bound 2 at a later date. */

	unlinkSaveFiles (&pm1data.write_save_file_state);
	if (w->work_type != WORK_PMINUS1) {
		pm1data.write_save_file_state.base_filename[0] = 'p';
		unlinkSaveFiles (&pm1data.write_save_file_state);
	}
	if (!QA_IN_PROGRESS && w->work_type == WORK_PMINUS1 && IniGetInt (INI_FILE, "KeepPminus1SaveFiles", 1)) {
		pm1data.state = PM1_STATE_DONE;
		ASSERTG (pm1data.C_done >= pm1data.B_done);
		pm1_save (&pm1data);
	}

/* Since we found a factor, then we may have performed less work than */
/* expected.  Make sure we do not update the rolling average with */
/* this inaccurate data. */

	invalidateNextRollingAverageUpdate ();

/* Create a PRP work unit to immediately test the Mersenne cofactor */

	if (USE_PRIMENET && w->k == 1.0 && w->b == 2 && w->c == -1 && strlen (str) >= 21 && (int) w->n < IniGetInt (INI_FILE, "MaxAutoPRPExponent", 5000000)) {
		struct work_unit w_prp;
		generatePRPWorkUnit (w, str, &w_prp);
		// Add work unit before or after this work unit
		w_prp.next = w;
		stop_reason = addWorkToDoLine (thread_num, &w_prp, ADD_AFTER_SPECIFIC);
		if (stop_reason) goto exit;
	}

/* Remove the exponent from the worktodo.txt file */

	stop_reason = STOP_WORK_UNIT_COMPLETE;
	goto exit;

/* Output an error message saying we are restarting. */
/* Sleep five minutes before restarting from last save file. */

err:	if (gw_get_maxerr (&pm1data.gwdata) > allowable_maxerr) {
		sprintf (buf, "Possible roundoff error (%.8g), backtracking to last save file and using larger FFT.\n\n", gw_get_maxerr (&pm1data.gwdata));
		OutputStr (thread_num, buf);
		maxerr_restart_count++;
		maxerr_fftlen = gwfftlen (&pm1data.gwdata);
		pm1data.interim_C = 0;				// We'll get the new interim_C from a save file
	} else {
		OutputBoth (thread_num, "SUMOUT error occurred.\n");
		stop_reason = SleepFive (thread_num);
		if (stop_reason) goto exit;
	}
error_restart:
	Dmultiple_map.clear ();
	relp_set_map.clear ();
	pm1_cleanup (&pm1data);
	free (factor), factor = NULL;
	free (str), str = NULL;
	free (msg), msg = NULL;
	if (exp_initialized) mpz_clear (exp), exp_initialized = FALSE;
	goto restart;
}

/* Read a file of P-1 tests to run as part of a QA process */
/* The format of this file is: */
/*	k, n, c, B1, B2_start, B2_end, factor */
/* Use Advanced/Time 9992 to run the QA suite */

int pminus1_QA (
	int	thread_num,
	struct PriorityInfo *sp_info)	/* SetPriority information */
{
	FILE	*fd;

/* Set the title */

	title (thread_num, "QA");

/* Open QA file */

	fd = fopen ("qa_pm1", "r");
	if (fd == NULL) {
		OutputStr (thread_num, "File named 'qa_pm1' could not be opened.\n");
		return (STOP_FILE_IO_ERROR);
	}

/* Loop until the entire file is processed */

	QA_TYPE = 0;
	for ( ; ; ) {
		struct work_unit w;
		double	k;
		unsigned long b, n, B1, B2;
		signed long c;
		char	fac_str[80];
		int	stop_reason;

/* Read a line from the file */

		n = 0;
		(void) fscanf (fd, "%lf,%lu,%lu,%ld,%lu,%lu,%s\n", &k, &b, &n, &c, &B1, &B2, fac_str);
		if (n == 0) break;

/* If p is 1, set QA_TYPE */

		if (n == 1) {
			QA_TYPE = c;
			continue;
		}

/* Convert the factor we expect to find into a "giant" type */

		QA_FACTOR = allocgiant ((int) strlen (fac_str));
		ctog (fac_str, QA_FACTOR);

/* Do the P-1 */

		memset (&w, 0, sizeof (w));
		w.work_type = WORK_PMINUS1;
		w.k = k;
		w.b = b;
		w.n = n;
		w.c = c;
		w.B1 = B1;
		w.B2 = B2;
		QA_IN_PROGRESS = TRUE;
		stop_reason = pminus1 (0, sp_info, &w);
		QA_IN_PROGRESS = FALSE;
		free (QA_FACTOR);
		if (stop_reason != STOP_WORK_UNIT_COMPLETE) {
			fclose (fd);
			return (stop_reason);
		}
	}

/* Cleanup */

	fclose (fd);
	return (0);
}

/**************************************************************/
/* Routines to compute optimal and test to optimal P-1 bounds */
/**************************************************************/

struct global_pm1_savings_data {
	unsigned long n;				// Exponent being tested
	struct pm1_stage2_cost_data cost_data;		// P-1 cost structure to pass
	double	ll_testing_cost;			// Cost (in squarings) of running LL/PRP tests should we fail to find a factor
	uint64_t vals;					// Number of temporaries we can allocate in pass 2
	pmhandle polydata;				// Partially initialized polymult handle good enough for polymult_mem_required to function
};

#define PFACTOR_BOUND1_ROUNDING	1000			// Round P-1 B1 bound to a multiple of 1000 -- just to be pretty (and faster to calculate)
#define PFACTOR_BOUND2_ROUNDING	5000			// Round P-1 B2 bound to a multiple of 5000 -- just to be pretty (and faster to calculate)

struct pm1_savings_data {
	uint64_t B1;
	uint64_t B2;
	uint64_t actual_B2;
	double	prob;
	double	total_cost;
	double	savings;
};

/* For a given B1,B2 calculate the costs and savings */

void pm1_savings (
	struct global_pm1_savings_data *g,
	struct pm1_savings_data *c)
{
	uint64_t B1 = c->B1 * PFACTOR_BOUND1_ROUNDING;
	uint64_t B2 = c->B2 * PFACTOR_BOUND2_ROUNDING;

/* Handle no stage 2 case */

	if (B2 <= B1) {
		c->prob = pm1prob (g->cost_data.takeAwayBits, g->cost_data.sieve_depth, B1, B1);
		c->total_cost = g->cost_data.c.stage1_cost + g->cost_data.c.gcd_cost;
		c->actual_B2 = B2;
	}

/* Compute how many transforms will be required in the best implementation of pass 2 given V,Vn,Vn1,gg gwnum temporaries will be needed. */

	else {
		double efficiency = 0.0;
		// Cost pairing stage 2 unless there are lots of temporaries available
		if (g->vals < 120) {
			g->cost_data.stage2_type = PM1_STAGE2_PAIRING;
			g->cost_data.c.use_poly_D_data = FALSE;
			g->cost_data.c.centers_on_Dmultiple = TRUE;
			g->cost_data.c.stage2_fftlen = g->cost_data.c.stage1_fftlen;
			efficiency = best_stage2_impl (B1, B1, 0, 0, B2, g->vals - 3, &pm1_stage2_cost, &g->cost_data);
			if (efficiency > 0.0) {
				c->prob = g->cost_data.factor_probability;
				c->total_cost = g->cost_data.c.total_cost;
				c->actual_B2 = g->cost_data.c.B2_start + g->cost_data.c.numDsections *  g->cost_data.c.D;
			}
		}
		// Cost polymult stage 2 unless there aren't many temporaries
		if (g->vals > 40) {
			g->cost_data.stage2_type = PM1_STAGE2_POLYMULT;
			g->cost_data.c.use_poly_D_data = TRUE;
			g->cost_data.c.centers_on_Dmultiple = FALSE;
			// Polymult is likely require a larger FFT length and fewer vals
			g->cost_data.c.stage2_fftlen = (unsigned long) ((double) g->cost_data.c.stage1_fftlen * 1.10);
			double poly_efficiency = best_stage2_impl (B1, B1, 0, 0, B2, (int) (g->vals / 1.10) - 4, &pm1_stage2_cost, &g->cost_data);
			if (poly_efficiency > efficiency) {
				efficiency = poly_efficiency;
				c->prob = g->cost_data.factor_probability;
				c->total_cost = g->cost_data.c.total_cost;
				c->actual_B2 = g->cost_data.c.B2_start + g->cost_data.c.numDsections *  g->cost_data.c.D;
			}
		}
		// Handle case where neither a pairing or poly stage 2 is possible
		if (efficiency == 0.0) {
			c->prob = 0.0;
			c->total_cost = g->cost_data.c.stage1_cost + g->cost_data.c.gcd_cost;
			c->actual_B2 = B2;
		}
	}

/* Calculate our savings using this B1/B2.  Savings is success_probability * cost_of_LL_tests - cost_of_Pminus1. */
/* Optimizing for savings is different than optimizing for efficiency. */

	c->savings = c->prob * g->ll_testing_cost - c->total_cost;
}

/* For a given B1, find the best B2 */

void pminus1_choose_B2 (
	struct global_pm1_savings_data *g,
	struct pm1_savings_data *c)
{
	struct pm1_savings_data best[3];

/* Estimate how many transforms (estimated squarings * 2) will be required in pass 1 */

	g->cost_data.c.stage1_cost = ceil (1.44 * c->B1 * PFACTOR_BOUND1_ROUNDING) * 2.0;

/* Look for the best B2 somewhere between 1*B1 and 100*B1 */

	best[0] = *c;
	best[0].B2 = c->B1 * PFACTOR_BOUND1_ROUNDING / PFACTOR_BOUND2_ROUNDING;
	pm1_savings (g, &best[0]);
	best[1] = *c;
	best[1].B2 = best[0].actual_B2 * 5 / PFACTOR_BOUND2_ROUNDING;
	pm1_savings (g, &best[1]);
	best[2] = *c;
	best[2].B2 = best[1].actual_B2 * 5 / PFACTOR_BOUND2_ROUNDING;
	pm1_savings (g, &best[2]);

/* Handle case where midpoint has worse savings than the start point */
/* The search code requires best[1] is better than best[0] and best[2] */

	while (best[0].savings > best[1].savings) {
		best[2] = best[1];
		best[1].B2 = (best[0].B2 + best[2].B2) / 2;
		pm1_savings (g, &best[1]);
	}

/* Handle case where midpoint has worse savings than the end point */
/* The search code requires best[1] is better than best[0] and best[2] */

	uint64_t max_B2 = IniGetInt (INI_FILE, "MaxOptimalB2Multiplier", 10000) * c->B1 * PFACTOR_BOUND1_ROUNDING / PFACTOR_BOUND2_ROUNDING;
	while (best[2].savings > best[1].savings) {
		if (best[2].B2 == max_B2) {
			best[0] = best[2];
			best[1] = best[2];
			break;
		}
		best[0] = best[1];
		best[1] = best[2];
		best[2].B2 = best[1].actual_B2 * 5 / PFACTOR_BOUND2_ROUNDING;
		if (best[2].B2 > max_B2) best[2].B2 = max_B2;
		pm1_savings (g, &best[2]);
	}

/* Find the best B2.  We use a binary-like search to speed things up (new in version 30.3b3). */

	while (best[2].B2 - best[0].B2 > 2) {
		struct pm1_savings_data midpoint;

		ASSERTG (best[1].savings >= best[0].savings);
		ASSERTG (best[1].savings >= best[2].savings);

/* Work on the bigger of the lower section and upper section */

		if (best[1].B2 - best[0].B2 > best[2].B2 - best[1].B2) {	// Work on lower section
			// If the savings difference is real small, then we've searched this section enough
			if (best[1].savings - best[0].savings < 100.0) {
				best[0] = best[1], best[0].B2--;
				continue;
			}
			midpoint = *c;
			midpoint.B2 = (best[0].B2 + best[1].B2) / 2;
			pm1_savings (g, &midpoint);
			if (midpoint.savings > best[1].savings) {		// Make middle the new end point
				best[2] = best[1];
				best[1] = midpoint;
			} else {						// Create new start point
				best[0] = midpoint;
			}
		} else {							// Work on upper section
			// If the savings difference is real small, then we've searched this section enough
			if (best[1].savings - best[2].savings < 100.0) {
				best[2] = best[1], best[2].B2++;
				continue;
			}
			midpoint = *c;
			midpoint.B2 = (best[1].B2 + best[2].B2) / 2;
			pm1_savings (g, &midpoint);
			if (midpoint.savings > best[1].savings) {		// Make middle the new start point
				best[0] = best[1];
				best[1] = midpoint;
			} else {						// Create new end point
				best[2] = midpoint;
			}
		}
	}

/* Return the best B2 we found */

	*c = best[1];
}

/* Calculate the best B1 and B2 values to use in a P-1 factoring job. */
/* Return the B1 and B2 bounds, execution cost, and chance of success. */

void guess_pminus1_bounds (
	int	thread_num,
	double	k,			/* K in K*B^N+C. Must be a positive integer. */
	unsigned long b,		/* B in K*B^N+C. Must be two. */
	unsigned long n,		/* N in K*B^N+C. Exponent to test. */
	signed long c,			/* C in K*B^N+C. */
	double	how_far_factored,	/* Bit depth of trial factoring */
	double	tests_saved,		/* 1 if doublecheck, 2 if first test */
	uint64_t *bound1,
	uint64_t *bound2,
	uint64_t *squarings,
	double	*success_rate)
{
	struct global_pm1_savings_data g;
	struct pm1_savings_data best[3];

/* Guard against wild tests_saved values.  Huge values will cause this routine to run for a very long time. */
/* This shouldn't happen as auxiliaryWorkUnitInit now has the exact same test. */

	if (tests_saved > 100) tests_saved = 100;

/* Copy exponent, how_far_factored to global costing data.  Init rest of the cost_data structure. */

	memset (&g, 0, sizeof (g));
	g.n = n;
	g.cost_data.takeAwayBits = isMersenne (k, b, n, c) ? log2 (n) + 1.0 : isGeneralizedFermat (k, b, n, c) ? log2 (n) : 0.0;
	g.cost_data.sieve_depth = how_far_factored;
	g.cost_data.c.polydata = &g.polydata;
	g.cost_data.c.poly_compression = 0.984;
	if (IniGetInt (INI_FILE, "Poly1Compress", 2) == 2) g.cost_data.c.poly_compression *= 0.875;
	if (IniGetInt (INI_FILE, "Poly1Compress", 2) == 0) g.cost_data.c.poly_compression = 1.0;
	g.cost_data.c.stage1_fftlen = gwmap_to_fftlen (k, b, n, c);
	g.cost_data.c.stage1_threads = CORES_PER_TEST[thread_num];
	g.cost_data.c.stage2_threads = g.cost_data.c.stage1_threads + IniGetInt (INI_FILE, "Stage2ExtraThreads", 0);
	init_gcd_costs ((n * log ((double) b) + log (k)) / log (2.0), &g.cost_data);

/* Balance P-1 against 1 or 2 LL/PRP tests (actually more since we get a corrupt result reported some of the time).  One squaring equals two FFTs. */

	g.ll_testing_cost = (tests_saved + 2.0 * ERROR_RATE) * n * 2.0;

/* Compute how many temporaries we can use given our memory constraints */

	g.vals = cvt_mem_to_estimated_gwnums (max_mem (thread_num), k, b, n, c);
	if (g.vals < 1) g.vals = 1;

/* Partially initialize the polymult library so that polymult_mem_required returns accurate data */

	gwhandle gwdata;
	memset (&gwdata, 0, sizeof (gwdata));
	gwdata.k = k;
	gwdata.b = b;
	gwdata.n = n;
	gwdata.c = c;
	gwdata.FFTLEN = (unsigned long) (g.cost_data.c.stage1_fftlen * 1.10);
	gwdata.cpu_flags = CPU_FLAGS;
	if (gwdata.FFTLEN < 1024 && gwdata.cpu_flags & CPU_AVX512F) gwdata.cpu_flags &= ~CPU_AVX512F;
	gwdata.num_threads = g.cost_data.c.stage2_threads;
	polymult_init (&g.polydata, &gwdata);
	polymult_default_tuning (&g.polydata,
				 IniGetInt (INI_FILE, "PolymultCacheSize", CPU_NUM_L2_CACHES > 0 ? CPU_TOTAL_L2_CACHE_SIZE / CPU_NUM_L2_CACHES : 256),
				 IniGetInt (INI_FILE, "PolymultCacheSize2", CPU_NUM_L3_CACHES > 0 ? CPU_TOTAL_L3_CACHE_SIZE / CPU_NUM_L3_CACHES : 6144));
//GW:  add more overrides here

/* Find the best B1 somewhere between n/3300 and 250*(n/3300). */

	best[0].B1 = n / 3300 / PFACTOR_BOUND1_ROUNDING;
	if (best[0].B1 < 1) best[0].B1 = 1;
	pminus1_choose_B2 (&g, &best[0]);
	best[1].B1 = 125 * best[0].B1;
	pminus1_choose_B2 (&g, &best[1]);
	best[2].B1 = 250 * best[0].B1;
	pminus1_choose_B2 (&g, &best[2]);

/* Handle case where midpoint has worse savings than the start point */
/* The search code requires best[1] is better than best[0] and best[2] */

	while (best[0].savings > best[1].savings) {
		best[2] = best[1];
		best[1].B1 = (best[0].B1 + best[2].B1) / 2;
		pminus1_choose_B2 (&g, &best[1]);
	}

/* Handle case where midpoint has worse savings than the end point */
/* The search code requires best[1] is better than best[0] and best[2] */

	while (best[2].savings > best[1].savings) {
		best[0] = best[1];
		best[1] = best[2];
		best[2].B1 = best[1].B1 * 2;
		pminus1_choose_B2 (&g, &best[2]);
	}

/* Find the best B1.  We use a binary-like search to speed things up (new in version 30.3b3). */

	while (best[2].B1 - best[0].B1 > 2) {
		struct pm1_savings_data midpoint;

		ASSERTG (best[1].savings >= best[0].savings);
		ASSERTG (best[1].savings >= best[2].savings);

/* Work on the bigger of the lower section and upper section */

		if (best[1].B1 - best[0].B1 > best[2].B1 - best[1].B1) {	// Work on lower section
			// If the savings difference is real small, then we've searched this section enough
			if (best[1].savings - best[0].savings < 100.0) {
				best[0] = best[1], best[0].B1--;
				continue;
			}
			midpoint.B1 = (best[0].B1 + best[1].B1) / 2;
			pminus1_choose_B2 (&g, &midpoint);
			if (midpoint.savings > best[1].savings) {		// Make middle the new end point
				best[2] = best[1];
				best[1] = midpoint;
			} else {						// Create new start point
				best[0] = midpoint;
			}
		} else {							// Work on upper section
			// If the savings difference is real small, then we've searched this section enough
			if (best[1].savings - best[2].savings < 100.0) {
				best[2] = best[1], best[2].B1++;
				continue;
			}
			midpoint.B1 = (best[1].B1 + best[2].B1) / 2;
			pminus1_choose_B2 (&g, &midpoint);
			if (midpoint.savings > best[1].savings) {		// Make middle the new start point
				best[0] = best[1];
				best[1] = midpoint;
			} else {						// Create new end point
				best[2] = midpoint;
			}
		}
	}

/* Return the final best choice */

	if (best[1].savings > 0.0) {
		*bound1 = best[1].B1 * PFACTOR_BOUND1_ROUNDING;
		*bound2 = best[1].B2 * PFACTOR_BOUND2_ROUNDING;
		*squarings = (uint64_t) (best[1].total_cost / 2.0);		// Convert from transforms to squarings
		*success_rate = best[1].prob;
	} else {
		*bound1 = 0;
		*bound2 = 0;
		*squarings = 0;
		*success_rate = 0.0;
	}
}

/* Determine the probability of P-1 finding a factor.  Return the Mihai estimated P-1 success probability */

double guess_pminus1_probability (
	struct work_unit *w)
{
	double	takeAwayBits;
	takeAwayBits = isMersenne (w->k, w->b, w->n, w->c) ? log2 (w->n) + 1.0 :
		       isGeneralizedFermat (w->k, w->b, w->n, w->c) ? log2 (w->n) : 0.0;
	return (pm1prob (takeAwayBits, (unsigned int) w->sieve_depth, w->B1, w->B2));
}

/* Do the P-1 factoring step prior to a Lucas-Lehmer test */
/* Similar to the main P-1 entry point, except bounds are not known */

int pfactor (
	int	thread_num,
	struct PriorityInfo *sp_info,	/* SetPriority information */
	struct work_unit *w)
{
	uint64_t bound1, bound2, squarings;
	double	prob;
	char	buf[120], testnum[120];
	int	stop_reason;

/* Set flag indicating we need to restart if the maximum amount of memory changes (as opposed to currently available memory!) */
/* If maximum memory changes we want to recompute the P-1 bounds. */

	set_restart_if_max_memory_change (thread_num);

/* Output a message that P-1 factoring is about to begin */

	gw_as_string (testnum, w->k, w->b, w->n, w->c);
	sprintf (buf, "Optimal P-1 factoring of %s using up to %luMB of memory.\n", testnum, max_mem (thread_num));
	OutputStr (thread_num, buf);
	if (w->known_factors != NULL) {
		OutputStr (thread_num, "Skipping known factors: ");
		OutputStr (thread_num, w->known_factors);
		OutputStr (thread_num, "\n");
	}
	sprintf (buf, "Assuming no%s factors below 2^%.2g and %.2g primality test%s saved if a factor is found.\n",
		 w->known_factors != NULL ? " other" : "", w->sieve_depth, w->tests_saved, w->tests_saved == 1.0 ? "" : "s");
	OutputStr (thread_num, buf);

/* Deduce the proper P-1 bounds */

	guess_pminus1_bounds (thread_num, w->k, w->b, w->n, w->c, w->sieve_depth, w->tests_saved, &bound1, &bound2, &squarings, &prob);
	if (bound1 == 0) {
		sprintf (buf, "%s does not need P-1 factoring.\n", testnum);
		OutputBoth (thread_num, buf);
		if (w->work_type == WORK_PFACTOR) {
			//bug - do we need to tell the server to cancel the
			//assignment?  In theory, server shouldn't ever send
			//this assignment out.
			return (STOP_WORK_UNIT_COMPLETE);
		} else {
			w->pminus1ed = 1;		// Flag to indicate LL test has completed P-1
			w->tests_saved = 0.0;		// Variable to indicate PRP test has completed P-1
			stop_reason = updateWorkToDoLine (thread_num, w);
			if (stop_reason) return (stop_reason);
			return (0);
		}
	}

/* Output a message that P-1 factoring is about to begin */

	sprintf (buf, "Optimal bounds are B1=%" PRIu64 ", B2=%" PRIu64 "\n", bound1, bound2);
	OutputStr (thread_num, buf);
	sprintf (buf, "Chance of finding a new factor is an estimated %.3g%%\n", prob * 100.0);
	OutputStr (thread_num, buf);

/* Call the P-1 factoring code */

	w->B1 = bound1;
	w->B2_start = bound1;
	w->B2 = bound2;
	return (pminus1 (thread_num, sp_info, w));
}

/**************************************************************
 *	P+1 Functions
 **************************************************************/

/* Data maintained during P+1 process */

#define PP1_STATE_STAGE1	1	/* In stage 1 */
#define PP1_STATE_MIDSTAGE	2	/* Between stage 1 and stage 2 */
#define PP1_STATE_STAGE2	3	/* In middle of stage 2 (processing a pairmap) */
#define PP1_STATE_GCD		4	/* Stage 2 GCD */
#define PP1_STATE_DONE		5	/* P+1 job complete */

typedef struct {
	gwhandle gwdata;	/* GWNUM handle */
	int	thread_num;	/* Worker number */
	int	stage1_threads;	/* Number of threads to use in stage 1 */
	int	stage2_threads;	/* Number of threads to use in stage 2 */
	struct work_unit *w;	/* Worktodo.txt entry */
	int	state;		/* One of the states listed above */
	double	takeAwayBits;	/* Bits we get for free in smoothness of P+1 factor */
	double	success_rate;	/* Percentage of smooth P+1 factors we expect to find.  Percentage goes down with each P+1 run. */
	uint32_t numerator;	/* Starting point numerator */
	uint32_t denominator;	/* Starting point denominator */
	uint64_t B;		/* Bound #1 (a.k.a. B1) */
	uint64_t C;		/* Bound #2 (a.k.a. B2) */
	uint64_t interim_B;	/* B1 we are currently calculating (equals B except when finishing stage 1 from a save file using a different B1). */
	uint64_t interim_C;	/* B2 we are currently calculating (equals C except when finishing stage 2 a save file using different B2) */
	uint64_t B_done;	/* We have completed calculating 3^e to this bound #1 */
	int	optimal_B2;	/* TRUE if we calculate optimal bound #2 given currently available memory.  FALSE for a fixed bound #2. */

	readSaveFileState read_save_file_state;	/* Manage savefile names during reading */
	writeSaveFileState write_save_file_state; /* Manage savefile names during writing */
	void	*sieve_info;	/* Prime number sieve */
	uint64_t stage1_prime;	/* Prime number being processed */
	unsigned long stage1_fftlen; /* FFT length used in stage 1 */

	int	D;		/* Stage 2 loop size */
	int	numrels;	/* Number of relative primes less than D/2 (the number of relative primes in one full relp_set) */
	int	totrels;	/* Number relatively prime nQx values used */
	uint64_t B2_start;	/* Starting point of first D section to be processed in stage 2 (an odd multiple of D/2) */
	uint64_t numDsections;	/* Number of D sections to process in stage 2 */
	uint64_t Dsection;	/* Current D section being processed in stage 2 */
	int	relp;		/* Last relative prime processed in the current D section */

	uint64_t max_pairmap_Dsections;	/* Number of D sections that can fit in a pairing map */
	uint8_t	*pairmap;	/* Pairing map for prime pairings and singles in each D section */
	uint64_t pairmap_size;	/* Size of the pairing map */
	uint8_t *pairmap_ptr;	/* Pointer to the next byte to process in the pairing map */
	uint64_t first_relocatable; /* First relocatable prime (same as B1 unless pairmaps must be split or mem change caused a replan) */
	uint64_t last_relocatable; /* Last relocatable prime for filling pairmaps (unless mem change causes a replan) */
	uint64_t C_done;	/* Stage 2 completed thusfar (updated every D section that is completed) */

	uint64_t stage2_numvals;/* Number of gwnums used in stage 2 */
	int16_t relp_sets[32];	/* The relp sets we are using in stage 2 */
	gwnum	*nQx;		/* Array of relprime data used in stage 2 */

	double	pct_mem_to_use;	/* If we get memory allocation errors, we progressively try using less and less. */

	gwnum	V;		/* V_1 in a Lucas sequence */
	gwnum	Vn;		/* V_n in a Lucas sequence */
	gwnum	Vn1;		/* V_{n+1} in a Lucas sequence */
	gwnum	gg;		/* An accumulator in stage 2 */
} pp1handle;

/* Perform cleanup functions. */

void pp1_cleanup (
	pp1handle *pp1data)
{

/* Free memory */

	free (pp1data->nQx), pp1data->nQx = NULL;
	free (pp1data->pairmap), pp1data->pairmap = NULL;
	gwdone (&pp1data->gwdata);
	end_sieve (pp1data->sieve_info), pp1data->sieve_info = NULL;
}

/* Routines to create and read save files for a P+1 factoring job */

#define PP1_MAGICNUM	0x912a374a
//#define PP1_VERSION	1				/* New in 30.6 */
#define PP1_VERSION	2				/* 30.7.  Improved pairing, compressed bitmaps */

void pp1_save (
	pp1handle *pp1data)
{
	int	fd;
	struct work_unit *w = pp1data->w;
	uint32_t sum = 0;

/* Create the intermediate file */

	fd = openWriteSaveFile (&pp1data->write_save_file_state);
	if (fd < 0) return;

/* Write the file header */

	if (!write_header (fd, PP1_MAGICNUM, PP1_VERSION, w)) goto writeerr;

/* Write the file data */

	if (! write_int (fd, pp1data->state, &sum)) goto writeerr;
	if (! write_int (fd, pp1data->numerator, &sum)) goto writeerr;
	if (! write_int (fd, pp1data->denominator, &sum)) goto writeerr;

	if (pp1data->state == PP1_STATE_STAGE1) {
		if (! write_uint64 (fd, pp1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->interim_B, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->stage1_prime, &sum)) goto writeerr;
	}

	else if (pp1data->state == PP1_STATE_MIDSTAGE) {
		if (! write_uint64 (fd, pp1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->C_done, &sum)) goto writeerr;
	}

	// Save everything necessary to restart stage 2 without calling pp1_stage2_impl again
	else if (pp1data->state == PP1_STATE_STAGE2) {
		uint64_t remaining_pairmap_size;
		if (! write_uint64 (fd, pp1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->C_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->interim_C, &sum)) goto writeerr;
		if (! write_int (fd, (int) pp1data->stage2_numvals, &sum)) goto writeerr;
		if (! write_int (fd, pp1data->totrels, &sum)) goto writeerr;
		if (! write_int (fd, pp1data->D, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->first_relocatable, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->last_relocatable, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->B2_start, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->numDsections, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->Dsection, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->max_pairmap_Dsections, &sum)) goto writeerr;
		if (! write_int (fd, pp1data->relp, &sum)) goto writeerr;
		if (! write_array (fd, (char *) pp1data->relp_sets, 32 * sizeof (int16_t), &sum)) goto writeerr;
		// Output the truncated pairmap
//GW:  handle NULL pairmap?
		remaining_pairmap_size = pp1data->pairmap_size - (pp1data->pairmap_ptr - pp1data->pairmap);
		if (! write_uint64 (fd, remaining_pairmap_size, &sum)) goto writeerr;
		if (! write_array (fd, (char *) pp1data->pairmap_ptr, (size_t) remaining_pairmap_size, &sum)) goto writeerr;
	}

	else if (pp1data->state == PP1_STATE_GCD) {
		if (! write_uint64 (fd, pp1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->C_done, &sum)) goto writeerr;
	}

	else if (pp1data->state == PP1_STATE_DONE) {
		if (! write_uint64 (fd, pp1data->B_done, &sum)) goto writeerr;
		if (! write_uint64 (fd, pp1data->C_done, &sum)) goto writeerr;
	}

/* Write the gwnum value used in stage 1 and stage 2.  There are occasions where x or gg may be in a partially FFTed state. */

	if (! write_gwnum (fd, &pp1data->gwdata, pp1data->V, &sum)) goto writeerr;
	if (pp1data->state >= PP1_STATE_MIDSTAGE && pp1data->state <= PP1_STATE_GCD) {
		int	have_gg = (pp1data->gg != NULL);
		if (! write_int (fd, have_gg, &sum)) goto writeerr;
		if (have_gg && !write_gwnum (fd, &pp1data->gwdata, pp1data->gg, &sum)) goto writeerr;
	}

/* In case we're at peak memory usage, free the cached gwnums that write_gwnum allocated (one for gwunfft and one for gwtobinary result) */

	gwfree_cached (&pp1data->gwdata);
	mallocFreeForOS ();

/* Write the checksum, we're done */

	if (! write_checksum (fd, sum)) goto writeerr;

	closeWriteSaveFile (&pp1data->write_save_file_state, fd);
	return;

/* An error occurred.  Close and delete the current file. */

writeerr:
	deleteWriteSaveFile (&pp1data->write_save_file_state, fd);
}


/* Read a save file */

int pp1_restore (
	pp1handle *pp1data)
{
	int	fd, numerator, denominator;
	struct work_unit *w = pp1data->w;
	uint32_t version, sum = 0, filesum;

/* Open the intermediate file */

	fd = _open (pp1data->read_save_file_state.current_filename, _O_BINARY | _O_RDONLY);
	if (fd < 0) goto err;

/* Read the file header */

	if (! read_magicnum (fd, PP1_MAGICNUM)) goto readerr;
	if (! read_header (fd, &version, w, &filesum)) goto readerr;
	if (version < 1 || version > PP1_VERSION) goto readerr;

/* Read the first part of the save file */

	if (! read_int (fd, &pp1data->state, &sum)) goto readerr;
	if (! read_int (fd, &numerator, &sum)) goto readerr;
	if (! read_int (fd, &denominator, &sum)) goto readerr;
	if (numerator != pp1data->numerator || denominator != pp1data->denominator) {
		if (pp1data->w->nth_run <= 2) goto bad_nth_run;			// User wants to do 2/7 or 6/5 and save file does not match
		if (numerator == 2 && denominator == 7) goto bad_nth_run;	// User wants a random start, not 2/7
		if (numerator == 6 && denominator == 5) goto bad_nth_run;	// User wants a random start, not 6/5
		pp1data->numerator = numerator;					// Replace random start with random start from save file
		pp1data->denominator = denominator;
	}

/* Read state dependent data */

	if (pp1data->state == PP1_STATE_STAGE1) {
		if (! read_uint64 (fd, &pp1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->interim_B, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->stage1_prime, &sum)) goto readerr;
	}

	else if (pp1data->state == PP1_STATE_MIDSTAGE) {
		if (! read_uint64 (fd, &pp1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->C_done, &sum)) goto readerr;
	}

	else if (pp1data->state == PP1_STATE_STAGE2) {
		if (! read_uint64 (fd, &pp1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->C_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->interim_C, &sum)) goto readerr;
		int temp;
		if (! read_int (fd, &temp, &sum)) goto readerr;
		pp1data->stage2_numvals = temp;
		if (! read_int (fd, &pp1data->totrels, &sum)) goto readerr;
		if (! read_int (fd, &pp1data->D, &sum)) goto readerr;
		pp1data->numrels = map_D_to_numrels (pp1data->D);
		if (! read_uint64 (fd, &pp1data->first_relocatable, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->last_relocatable, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->B2_start, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->numDsections, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->Dsection, &sum)) goto readerr;
		if (version == 1) {		// 30.6 save files
			uint64_t bitarraymaxDsections, bitarrayfirstDsection, bitarray_numDsections, bitarray_start, bitarray_len;
			char	*bitarray;
			if (! read_uint64 (fd, &bitarraymaxDsections, &sum)) goto readerr;
			if (! read_uint64 (fd, &bitarrayfirstDsection, &sum)) goto readerr;
			// Read the truncated bit array
			bitarray_numDsections = pp1data->numDsections - bitarrayfirstDsection;
			if (bitarray_numDsections > bitarraymaxDsections) bitarray_numDsections = bitarraymaxDsections;
			bitarray_len = divide_rounding_up (bitarray_numDsections * pp1data->totrels, 8);
			bitarray_start = divide_rounding_down ((pp1data->Dsection - bitarrayfirstDsection) * pp1data->totrels, 8);
			bitarray = (char *) malloc ((size_t) bitarray_len);
			if (bitarray == NULL) goto readerr;
			if (! read_array (fd, bitarray, (size_t) (bitarray_len - bitarray_start), &sum)) goto readerr;
			free (bitarray);
		} else {				// 30.7 save files
			if (! read_uint64 (fd, &pp1data->max_pairmap_Dsections, &sum)) goto readerr;
			if (! read_int (fd, &pp1data->relp, &sum)) goto readerr;
			if (! read_array (fd, (char *) pp1data->relp_sets, 32 * sizeof (int16_t), &sum)) goto readerr;
			if (! read_uint64 (fd, &pp1data->pairmap_size, &sum)) goto readerr;
			pp1data->pairmap = (uint8_t *) malloc ((size_t) pp1data->pairmap_size);
			if (pp1data->pairmap == NULL) goto readerr;
			if (! read_array (fd, (char *) pp1data->pairmap, (size_t) pp1data->pairmap_size, &sum)) goto readerr;
			pp1data->pairmap_ptr = pp1data->pairmap;
		}
	}

	else if (pp1data->state == PP1_STATE_GCD) {
		if (! read_uint64 (fd, &pp1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->C_done, &sum)) goto readerr;
	}

	else if (pp1data->state == PP1_STATE_DONE) {
		if (! read_uint64 (fd, &pp1data->B_done, &sum)) goto readerr;
		if (! read_uint64 (fd, &pp1data->C_done, &sum)) goto readerr;
	}

/* Read the gwnum value used in stage 1 and stage 2 */

	if (! read_gwnum (fd, &pp1data->gwdata, pp1data->V, &sum)) goto readerr;

/* Read stage 2 accumulator gwnum */

	if (pp1data->state >= PP1_STATE_MIDSTAGE && pp1data->state <= PP1_STATE_GCD) {
		int	have_gg;
		if (version == 4) have_gg = TRUE;				// 30.6 save files
		else if (! read_int (fd, &have_gg, &sum)) goto readerr;		// 30.7 save files
		if (have_gg) {
			pp1data->gg = gwalloc (&pp1data->gwdata);
			if (pp1data->gg == NULL) goto readerr;
			if (! read_gwnum (fd, &pp1data->gwdata, pp1data->gg, &sum)) goto readerr;
		}
	}

/* Version 30.6 save files cannot continue in stage 2 */

	if (version == 1 && pp1data->state == PP1_STATE_STAGE2) {
		pp1data->state = PP1_STATE_STAGE1;
		pp1data->stage1_prime = pp1data->B_done;
		pp1data->interim_B = pp1data->B_done;
		OutputStr (pp1data->thread_num, "Old P+1 save file was in stage 2.  Restarting stage 2 from scratch.\n");
		gwfree (&pp1data->gwdata, pp1data->gg);
		pp1data->gg = NULL;
	}

/* Read and compare the checksum */

	if (filesum != sum) goto readerr;
	_close (fd);

/* All done */

	return (TRUE);

/* An error occurred.  Cleanup and return. */

bad_nth_run:
	OutputBoth (pp1data->thread_num, "P+1 starting point in save file does not match nth_run parameter from worktodo.txt\n");
readerr:
	_close (fd);
	gwfree (&pp1data->gwdata, pp1data->gg), pp1data->gg = NULL;
err:
	return (FALSE);
}


/* Compute the cost (in squarings) of a particular P+1 stage 2 implementation. */

struct pp1_stage2_cost_data {
	/* Cost data common to ECM, P-1, P+1 */
	struct common_cost_data c;
	/* P+1 specific data sent to cost function follows */
	/* P+1 specific data returned from cost function follows */
};

double pp1_stage2_cost (
	void	*data)		/* P+1 specific costing data */
{
	struct pp1_stage2_cost_data *cost_data = (struct pp1_stage2_cost_data *) data;

/* Compute the stage 2 init costs */

/* For the relative primes below D there are 2 luc_add calls for each increment of 6 in the nQx setup loop (plus 6 luc_dbls/luc_adds for setup). */
/* Computing VD is another luc_dbl and luc_add.  For the relative primes above D, we perform one luc_add for each rel_prime to calculate. */
/* We assume relp set -1 is not used, which means there are totrels - numrels relative primes above D to calculate.  Also, each nQx value will */
/* be FFTed once but this usually happens as a by-product of the luc_add calls.  Each luc_add/luc_dbl is 2 transforms. */

	cost_data->c.est_init_transforms = 6.0 * 2.0;						// Setup costs, 6 luc_adds at 2 transforms each
	cost_data->c.est_init_transforms += (double) (cost_data->c.D / 6 - 2) * 2.0 * 2.0;	// From 12 to D stepping by 6 is 2 luc_adds at 2 transforms each
	cost_data->c.est_init_transforms += 2.0 * 2.0;						// Cost to calculate VD, 1 luc_add and 1 luc_dbl
	cost_data->c.est_init_transforms += (cost_data->c.totrels - cost_data->c.numrels) * 2.0; // Cost for relprimes above D

/* Any intermediate relp_sets also cost one luc_add for each relprime.  Partial intermediate sets also must be accounted for. */

	cost_data->c.est_init_transforms +=
		cost_data->c.numrels * cost_data->c.relp_sets[1] * 2.0 +					 // Cost for full intermediate relp_sets
		one_based_modulo (cost_data->c.totrels, cost_data->c.numrels) * cost_data->c.relp_sets[2] * 2.0; // Cost for partial intermediate relp_sets

/* Each Dmultiple costs either a luc_dbl or luc_add.  Here we have to guess how many Dmultiples are needed.  The most expensive D-multiple is likely the */
/* calculation of B2_start -- 2*log2(B2_start/D).  In addition, we'll assume one more D-multiple for each relp_set. */

	cost_data->c.est_init_transforms += (2.0 * log2((double)((cost_data->c.B2_start + cost_data->c.D / 2) / cost_data->c.D)) + cost_data->c.multiplier) * 2.0;

/* Start main loop cost with one luc_add for each D section */

	cost_data->c.est_stage2_transforms = (double) cost_data->c.numDsections * 2.0;

/* Each nQx value will be FFTed once, but most nQx values are FFTed during the computation of other nQx values.  We'll assume one */
/* relp_set of nQx values will need this half squaring. */

	cost_data->c.est_stage2_transforms += cost_data->c.numrels;

/* Finally, each prime pair and prime single costs one 2-FFT multiply */

	cost_data->c.est_stage2_transforms += (cost_data->c.est_numpairs + cost_data->c.est_numsingles) * 2.0;

/* Return data P+1 implementation will need. */
/* Stage 2 memory is totrels gwnums for the nQx array, 3 gwnums to calculate multiples of D values, one gwnum for gg. */

	cost_data->c.stage2_numvals = cost_data->c.totrels + 4;

/* Return the resulting efficiency */

	cost_data->c.stage2_cost = cost_data->c.est_pairing_runtime + cost_data->c.est_init_transforms + cost_data->c.est_stage2_transforms;
	cost_data->c.total_cost = cost_data->c.stage1_cost + cost_data->c.stage2_cost + cost_data->c.gcd_cost;
	cost_data->c.efficiency = 1.0 / cost_data->c.total_cost;
	return (cost_data->c.efficiency);
}

/* Choose the most effective B2 for a P+1 run with a fixed B1 given the number of gwnums we are allowed to allocate. */
/* That is, find the B2 such that investing a fixed cost in either a larger B1 or B2 results in the same increase in chance of finding a factor. */

void pp1_choose_B2 (
	pp1handle *pp1data,
	uint64_t numvals)
{
	int	max_B2mult;
	struct pp1_stage2_cost_data cost_data;	/* Extra data passed to P+1 costing function */
	struct pp1_stage2_efficiency {
		int	i;
		double	B2_cost;		/* Cost of stage 2 in squarings */
		double	fac_pct;		/* Percentage chance of finding a factor */
	} best[3];
	int	sieve_depth;
	char	buf[160];

// P+1 probability of success
#define pp1prob(B1,B2)		(pm1prob (pp1data->takeAwayBits, sieve_depth, B1, B2) * pp1data->success_rate)

// Cost out a B2 value
	max_B2mult = IniGetInt (INI_FILE, "MaxOptimalB2Multiplier", 1000);
	cost_data.c.numvals = numvals;
	cost_data.c.use_poly_D_data = FALSE;
	cost_data.c.centers_on_Dmultiple = TRUE;
	cost_data.c.gwdata = &pp1data->gwdata;
	cost_data.c.stage1_fftlen = pp1data->stage1_fftlen;
	cost_data.c.stage2_fftlen = gwfftlen (&pp1data->gwdata);
	cost_data.c.stage1_threads = pp1data->stage1_threads;
	cost_data.c.stage2_threads = pp1data->stage2_threads;
#define p1eval(x,B2mult)	x.i = B2mult; \
				if (x.i > max_B2mult) x.i = max_B2mult; \
				x.B2_cost = best_stage2_impl (pp1data->B, pp1data->B, 0, 0, x.i * pp1data->B, numvals - 4, &pp1_stage2_cost, &cost_data); \
				x.fac_pct = pp1prob (pp1data->B, x.i * pp1data->B);

// Return TRUE if x is better than y.  Determined by seeing if taking the increased cost of y's higher B2 and investing it in increasing x's bounds
// results in a higher chance of finding a factor.
#define B1increase(x,y)	(int) ((y.B2_cost - x.B2_cost) / (1.44*1.52))	// Each B1 increase 1.44x more bits to process at a cost of 1.52 squarings per bit
#define p1compare(x,y)	(pp1prob (pp1data->B + B1increase(x,y), x.i * pp1data->B + B1increase(x,y)) > y.fac_pct)

/* Look for the best B2 which is likely between 10*B1 and 80*B1.  If optimal is not between these bounds, don't worry we'll locate the optimal spot anyway. */

	sieve_depth = (int) pp1data->w->sieve_depth;
	p1eval (best[0], 10);
	p1eval (best[1], 40);
	p1eval (best[2], 80);

/* Handle case where midpoint is worse than the start point */
/* The search code requires best[1] is better than best[0] and best[2] */

	while (p1compare (best[0], best[1])) {
		best[2] = best[1];
		p1eval (best[1], (best[0].i + best[2].i) / 2);
	}

/* Handle case where midpoint is worse than the end point */
/* The search code requires best[1] is better than best[0] and best[2] */

	while (!p1compare (best[1], best[2]) && best[2].i < max_B2mult) {
		best[0] = best[1];
		best[1] = best[2];
		p1eval (best[2], best[1].i * 2);
	}

/* Find the best B2.  We use a binary-like search to speed things up. */

	while (best[2].i - best[0].i > 2) {
		struct pp1_stage2_efficiency midpoint;

		// Work on the bigger of the lower section and upper section
		if (best[1].i - best[0].i > best[2].i - best[1].i) {		// Work on lower section
			p1eval (midpoint, (best[0].i + best[1].i) / 2);
			if (p1compare (midpoint, best[1])) {			// Make middle the new end point
				best[2] = best[1];
				best[1] = midpoint;
			} else {						// Create new start point
				best[0] = midpoint;
			}
		} else {							// Work on upper section
			p1eval (midpoint, (best[1].i + best[2].i) / 2);
			if (!p1compare (best[1], midpoint)) {			// Make middle the new start point
				best[0] = best[1];
				best[1] = midpoint;
			} else {						// Create new end point
				best[2] = midpoint;
			}
		}
	}

/* Return the best B2 */

	pp1data->C = best[1].i * pp1data->B;
	sprintf (buf, "With trial factoring done to 2^%d, optimal B2 is %d*B1 = %" PRIu64 ".\n", sieve_depth, best[1].i, pp1data->C);
	OutputStr (pp1data->thread_num, buf);
	sprintf (buf, "Chance of finding a new factor assuming no ECM has been done is %.3g%%\n", best[1].fac_pct * 100.0);
	OutputStr (pp1data->thread_num, buf);
}
#undef p1eval
#undef B1increase
#undef p1compare

/* Choose the best implementation of P+1 stage 2 given the current memory settings.  We may decide there will never be enough memory. */
/* We may decide to wait for more memory to be available. */
/* We choose the best value for D that reduces the number of multiplications with the current memory constraints. */

int pp1_stage2_impl (
	pp1handle *pp1data)
{
	unsigned int memory, min_memory, desired_memory;	/* Memory is in MB */
	uint64_t numvals;					/* Number of gwnums we can allocate */
	struct pp1_stage2_cost_data cost_data;
	int	stop_reason;

/* Calculate the amount of memory we can use in stage 2.  We must have 1MB for a pairing map + a minimum of 8 temporaries (D = 30). */
/* If not continuing from a stage 2 save file then assume 144 temporaries (D = 210, multiplier = 5) and a few MB for a pairing map will */
/* provide us with a reasonable execution speed.  Otherwise, we desire enough memory to use the save file's pairing map. */

	min_memory = 1 + cvt_gwnums_to_mem (&pp1data->gwdata, 8);
	if (pp1data->state < PP1_STATE_STAGE2) desired_memory = 3 + cvt_gwnums_to_mem (&pp1data->gwdata, 144);
	else desired_memory = (unsigned int) (pp1data->pairmap_size >> 20) + cvt_gwnums_to_mem (&pp1data->gwdata, pp1data->stage2_numvals);
	stop_reason = avail_mem (pp1data->thread_num, min_memory, desired_memory, &memory);
	if (stop_reason) return (stop_reason);

/* Factor in the multiplier that we set to less than 1.0 when we get unexpected memory allocation errors. */
/* Make sure we can still allocate 8 temporaries. */

	memory = (unsigned int) (pp1data->pct_mem_to_use * (double) memory);
	if (memory < min_memory) return (avail_mem_not_sufficient (pp1data->thread_num, min_memory, desired_memory));
	if (memory < 8) memory = 8;

/* Output a message telling us how much memory is available */

	if (NUM_WORKERS > 1) {
		char	buf[100];
		sprintf (buf, "Available memory is %dMB.\n", memory);
		OutputStr (pp1data->thread_num, buf);
	}

/* Compute the number of gwnum temporaries we can allocate.  User nordi had over-allocating memory troubles on Linux testing M1277, presumably */
/* because our estimated gwnum size was too low.  As a work-around limit numvals to 100,000 by default. */

	numvals = cvt_mem_to_gwnums (&pp1data->gwdata, memory);
	if (numvals < 8) numvals = 8;
//GW: Remove this with poly!!!
	if (numvals > 100000) numvals = 100000;
	if (QA_TYPE) numvals = QA_TYPE;			/* Optionally override numvals for QA purposes */

/* Set first_relocatable for future best_stage2_impl calls. */
/* Override B2 with optimal B2 based on amount of memory available. */

	if (pp1data->state == PP1_STATE_MIDSTAGE) {
		if (pp1data->C_done == pp1data->B) {
			pp1data->first_relocatable = pp1data->B;
			pp1data->last_relocatable = 0;
			if (pp1data->optimal_B2) pp1_choose_B2 (pp1data, numvals);
		} else {
			pp1data->first_relocatable = pp1data->C_done;
			pp1data->last_relocatable = 0;
		}
	}

/* If are continuing from a save file that was in stage 2, check to see if we currently have enough memory to continue with the save file's */
/* stage 2 implementation.  Also check if we now have significantly more memory available and stage 2 is not near complete such that a new */
/* stage 2 implementation might give us a faster stage 2 completion. */

//GW: These are rather arbitrary heuristics
	if (pp1data->state >= PP1_STATE_STAGE2 &&				// Continuing a stage 2 save file and
	    numvals >= pp1data->stage2_numvals &&				// We have enough memory and
	    (numvals < pp1data->stage2_numvals * 2 ||				// less than twice as much memory now available or
	     pp1data->Dsection >= pp1data->numDsections / 2))			// stage 2 more than half done
		return (0);							// Use old plan

/* If we are contemplating ditching the save file pairmap, figure out which non-relocatable primes are definitely included in the stage 2 */
/* accumulator.  Set C_done appropriately, but do not change first_relocatable as there is no guarantee which relocatables are in the accumulator. */

	if (pp1data->state == PP1_STATE_STAGE2) {
		int	max_relp_set = get_max_relp_set (pp1data->relp_sets);
		if (pp1data->Dsection > max_relp_set) pp1data->C_done = pp1data->B2_start + (pp1data->Dsection - max_relp_set) * pp1data->D;
		else pp1data->C_done = pp1data->B2_start;
	}

/* Find the least costly stage 2 plan. */
/* Try various values of D until we find the best one.  3 gwnums are required for multiples of V_D calculations and one gwnum for gg. */

	cost_data.c.use_poly_D_data = FALSE;
	cost_data.c.centers_on_Dmultiple = TRUE;
	cost_data.c.gwdata = &pp1data->gwdata;
	cost_data.c.stage1_fftlen = pp1data->stage1_fftlen;
	cost_data.c.stage2_fftlen = gwfftlen (&pp1data->gwdata);
	cost_data.c.stage1_threads = pp1data->stage1_threads;
	cost_data.c.stage2_threads = pp1data->stage2_threads;
	best_stage2_impl (pp1data->B, pp1data->first_relocatable, pp1data->last_relocatable, pp1data->C_done, pp1data->C, numvals - 4, &pp1_stage2_cost, &cost_data);

/* If are continuing from a save file that was in stage 2 and the new plan doesn't look significant better than the old plan, then */
/* we use the old plan and its partially completed pairmap. */

	if (pp1data->state >= PP1_STATE_STAGE2 &&				// Continuing a stage 2 save file and
	    numvals >= pp1data->stage2_numvals &&				// We have enough memory and
	    cost_data.c.stage2_numvals < pp1data->stage2_numvals * 2)		// new plan does not use significantly more memory
		return (0);							// Use old plan

/* If are continuing from a save file that was in stage 2, toss the save file's pair map. */

	if (pp1data->state >= PP1_STATE_STAGE2) {
		free (pp1data->pairmap);
		pp1data->pairmap = NULL;
	}

/* Set all the variables needed for this stage 2 plan */

	pp1data->interim_C = pp1data->C;
	pp1data->stage2_numvals = cost_data.c.stage2_numvals;
	pp1data->D = cost_data.c.D;
	pp1data->totrels = (int) cost_data.c.totrels;
	pp1data->numrels = cost_data.c.numrels;
	pp1data->B2_start = cost_data.c.B2_start;
	pp1data->numDsections = cost_data.c.numDsections;
	pp1data->max_pairmap_Dsections = cost_data.c.max_pairmap_Dsections;
	memcpy (pp1data->relp_sets, cost_data.c.relp_sets, sizeof (pp1data->relp_sets));
	if (pp1data->state < PP1_STATE_STAGE2 || pp1data->last_relocatable > pp1data->B2_start) pp1data->last_relocatable = pp1data->B2_start;

/* Output some debugging data so we can compare estimateds to actuals */

	if (IniGetInt (INI_FILE, "Stage2Estimates", 0)) {
		char	buf[120];
		sprintf (buf, "Est pair%%: %5.2f, init transforms: %.0f, main loop transforms: %.0f\n",
			 cost_data.c.est_pair_pct * 100.0, cost_data.c.est_init_transforms, cost_data.c.est_stage2_transforms);
		OutputStr (pp1data->thread_num, buf);
	}

/* Create a map of (hopefully) close-to-optimal prime pairings */

	int fill_window = pair_window_size (pp1data->gwdata.bit_length, pp1data->relp_sets);
	gwfree_internal_memory (&pp1data->gwdata);
	stop_reason = fill_pairmap (pp1data->thread_num, &pp1data->sieve_info, pp1data->D, fill_window,0,0,0,
				    pp1data->totrels, pp1data->relp_sets+3, pp1data->first_relocatable, pp1data->last_relocatable,
				    pp1data->B2_start, pp1data->C, pp1data->max_pairmap_Dsections, &pp1data->pairmap, &pp1data->pairmap_size);
	if (stop_reason) return (stop_reason);
	pp1data->pairmap_ptr = pp1data->pairmap;
	pp1data->Dsection = 0;
	pp1data->relp = -1;

	return (0);
}

/* Calculate the P+1 start point.  Peter Montgomery suggests 2/7 or 6/5. */

void pp1_calc_start (
	gwhandle *gwdata,
	int	numerator,
	int	denominator,
	giant	N,		/* Number we are factoring */
	gwnum	V)		/* Returned starting point */
{
	mpz_t	__frac, __inv, __N;
	giant	g_frac;

/* Convert number we are factoring to mpz_t */

	mpz_init (__N);
	gtompz (N, __N);

/* Compute inverse */

	mpz_init_set_ui (__inv, denominator);
	mpz_invert (__inv, __inv, __N);

/* Compute fraction and convert to gwnum */

	mpz_init (__frac);
	mpz_mul_ui (__frac, __inv, numerator);
	mpz_mod (__frac, __frac, __N);
	g_frac = allocgiant ((int) divide_rounding_up (mpz_sizeinbase (__frac, 2), 32));
	mpztog (__frac, g_frac);
	gianttogw (gwdata, g_frac, V);
	free (g_frac);

/* Cleanup and return */

	mpz_clear (__N);
	mpz_clear (__inv);
	mpz_clear (__frac);
}

/* Helper routines for implementing a PRAC-like lucas multiply */

/* The costing function assigns a doubling operation a cost of 2 FFTs and an addition operation a cost of 2 FFTs. */
/* The addition should cost a little more as it does more memory accesses. */

#define swap(a,b)	{t=a;a=b;b=t;}
#define PP1_ADD_COST	2
#define PP1_DBL_COST	2

int pp1_lucas_cost (
	uint64_t n,
	uint64_t d)
{
	uint64_t e, t;
	unsigned long c;

	if (d >= n || d <= n/2) return (999999999);		/* Catch invalid costings */

	c = 0;
	e = n - d;
	d = d - e;

	c += PP1_ADD_COST;

	while (d != e) {
		if (d < e) {
			swap (d,e);
		}
		if (100 * d <= 296 * e) {
			d = d-e;
			c += PP1_ADD_COST;
		} else if ((d&1) == (e&1)) {
			d = (d-e) >> 1;
			c += PP1_DBL_COST + PP1_ADD_COST;
		} else if ((d&1) == 0) {
			d = d >> 1;
			c += PP1_DBL_COST + PP1_ADD_COST;
//		} else if (d > 4 * e && d%3 == 0) {
//			d = d/3-e;
//			c += PP1_DBL_COST + 3*PP1_ADD_COST;
//		} else if (d > 4 * e && d%3 == 3 - e%3) {
//			d = (d-e-e)/3;
//			c += PP1_DBL_COST + 3*PP1_ADD_COST;
		} else {
			d = d-e;
			c += PP1_ADD_COST;
//			e = e >> 1;
//			c += PP1_DBL_COST + PP1_ADD_COST;
		}
	}

	return (c);
}

__inline int pp1_lucas_cost_several (uint64_t n, uint64_t *d) {
	int	i, c, min;
	uint64_t testd;
	for (i = 0, testd = *d - PRAC_SEARCH / 2; i < PRAC_SEARCH; i++, testd++) {
		c = pp1_lucas_cost (n, testd);
		if (i == 0 || c < min) min = c, *d = testd;
	}
	return (min);
}

void pp1_lucas_mul (
	pp1handle *pp1data,
	uint64_t n,
	uint64_t d,
	int	last_mul)	// TRUE if the last multiply should not use the GWMUL_STARTNEXTFFT option
{
	uint64_t e, t;
	gwnum	A, B, C;
	A = pp1data->V;
	B = pp1data->Vn;
	C = pp1data->Vn1;

	luc_dbl (&pp1data->gwdata, A, B);				/* B = 2*A */
									/* C = A (but we delay setting that up) */

	e = n - d;
	d = d - e;

	// To save a gwcopy setting C=A, we handle the most common case for the first iteration of the following loop.
	// I've only seen three cases that end up doing the gwcopy, n=3, n=11 and n=17.  With change to 2.96 there are a couple more.

	if (e > d && (100 * e <= 296 * d)) {
		swap (d, e);
		gwswap (A, B);							/* swap A & B, thus diff C = B */
		luc_add (&pp1data->gwdata, A, B, B, C);				/* B = A+B */
		gwswap (B, C);							/* C = B */
		d = d-e;
	}
	else if (d > e && (100 * d <= 296 * e)) {
		luc_add (&pp1data->gwdata, A, B, A, C);				/* B = A+B */
		gwswap (B, C);							/* C = B */
		d = d-e;
	} else {
		gwcopy (&pp1data->gwdata, A, C);				/* C = A */
	}

	while (d != e) {
		if (d < e) {
			swap (d, e);
			gwswap (A, B);
		}
		if (100 * d <= 296 * e) {					/* d <= 2.96 * e (Montgomery used 4.00) */
			luc_add (&pp1data->gwdata, A, B, C, C);			/* B = A+B */
			gwswap (B, C);						/* C = B */
			d = d-e;
		} else if ((d&1) == (e&1)) {
			luc_add (&pp1data->gwdata, A, B, C, B);			/* B = A+B */
			luc_dbl (&pp1data->gwdata, A, A);			/* A = 2*A */
			d = (d-e) >> 1;
		} else if ((d&1) == 0) {
			luc_add (&pp1data->gwdata, A, C, B, C);			/* C = A+C */
			luc_dbl (&pp1data->gwdata, A, A);			/* A = 2*A */
			d = d >> 1;
//		} else if (d > 4 * e && d%3 == 0) {		These provided little benefit, more research needed (adjust d>4*e)? Try remaining PRAC rules?
//			gwnum S = gwalloc (&pp1data->gwdata);
//			gwnum T = gwalloc (&pp1data->gwdata);
//			luc_dbl (&pp1data->gwdata, A, S);			/* S = 2*A */
//			luc_add (&pp1data->gwdata, A, B, C, T);			/* T = A+B */
//			luc_add (&pp1data->gwdata, S, T, C, C);			/* B = S+T */
//			luc_add (&pp1data->gwdata, S, A, A, A);			/* A = S+A */
//			gwswap (B, C);						/* C = B */
//			d = d/3-e;
//			gwfree (&pp1data->gwdata, S);
//			gwfree (&pp1data->gwdata, T);
//		} else if (d > 4 * e && d%3 == 3 - e%3) {
//			gwnum S = gwalloc (&pp1data->gwdata);
//			luc_add (&pp1data->gwdata, A, B, C, S);			/* S = A+B */
//			luc_add (&pp1data->gwdata, A, S, B, B);			/* B = A+S */
//			luc_dbl (&pp1data->gwdata, A, S);			/* S = 2*A */
//			luc_add (&pp1data->gwdata, S, A, A, A);			/* A = S+A */
//			d = (d-e-e)/3;
//			gwfree (&pp1data->gwdata, S);
		} else {
			luc_add (&pp1data->gwdata, A, B, C, C);			/* B = A+B */
			gwswap (B, C);						/* C = B */
			d = d-e;
//			luc_add (&pp1data->gwdata, B, C, A, C);			/* C = C-B */		Montgomery's default is worse than the above
//			luc_dbl (&pp1data->gwdata, B, B);			/* B = 2*B */
//			e = e >> 1;
		}
	}

	if (!last_mul)
		luc_add (&pp1data->gwdata, A, B, C, A);				/* A = A+B */
	else
		luc_add_last (&pp1data->gwdata, A, B, C, A);			/* A = A+B */

	pp1data->V = A;
	pp1data->Vn = B;
	pp1data->Vn1 = C;
}
#undef swap
#undef PP1_ADD_COST
#undef PP1_DBL_COST

/* Compute V_i from V_1 and i.  Routine limited to a multiplier of 2 or an odd number above 2. */

void pp1_mul (
	pp1handle *pp1data,
	uint64_t multiplier,
	int	last_mul)		// TRUE if the last multiply should not use the GWMUL_STARTNEXTFFT option
{
	ASSERTG (multiplier == 2 || (multiplier > 2 && (multiplier & 1)));

/* Perform a P+1 multiply using a modified PRAC algorithm developed by Peter Montgomery.  Basically, we try to find a near optimal Lucas */
/* chain of additions that generates the number we are multiplying by. */

	if (multiplier > 12) {
		int	c, min;
		uint64_t n, d, mind;

/* Cost a series of Lucas chains to find the cheapest.  First try v = (1+sqrt(5))/2, then (2+v)/(1+v), then (3+2*v)/(2+v), then (5+3*v)/(3+2*v), etc. */

		n = multiplier;
		mind = (uint64_t) ceil((double) 0.6180339887498948 * n);		/*v=(1+sqrt(5))/2*/
		min = pp1_lucas_cost_several (n, &mind);

		d = (uint64_t) ceil ((double) 0.7236067977499790 * n);			/*(2+v)/(1+v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.5801787282954641 * n);			/*(3+2*v)/(2+v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6328398060887063 * n);			/*(5+3*v)/(3+2*v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6124299495094950 * n);			/*(8+5*v)/(5+3*v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6201819808074158 * n);			/*(13+8*v)/(8+5*v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6172146165344039 * n);			/*(21+13*v)/(13+8*v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6183471196562281 * n);			/*(34+21*v)/(21+13*v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6179144065288179 * n);			/*(55+34*v)/(34+21*v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

		d = (uint64_t) ceil ((double) 0.6180796684698958 * n);			/*(89+55*v)/(55+34*v)*/
		c = pp1_lucas_cost_several (n, &d);
		if (c < min) min = c, mind = d;

/* Execute the cheapest Lucas chain */

		pp1_lucas_mul (pp1data, n, mind, last_mul);
	}

/* Otherwise, use a standard 2 operations (4 FFTs) per bit algorithm */

	else {
		gwnum	V, Vn, Vn1;
		uint64_t mask;
		int	last_mul_options = last_mul ? GWMUL_STARTNEXTFFT : 0;

		V = pp1data->V;
		Vn = pp1data->Vn;
		Vn1 = pp1data->Vn1;

/* Handle multiplier of 2 */

		if (multiplier == 2) {
			gwsquare2 (&pp1data->gwdata, V, V, GWMUL_ADDINCONST | last_mul_options);
			return;
		}
		if (multiplier == 3) {
			gwsquare2 (&pp1data->gwdata, V, Vn, GWMUL_FFT_S1 | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);	// V2
			gwmulsub4 (&pp1data->gwdata, V, Vn, V, V, last_mul_options);					// V3 = V1 + V2, diff 1
			return;
		}

/* Find top bit in multiplier */

		for (mask = 1ULL << 63; (multiplier & mask) == 0; mask >>= 1);

/* Init V values processing second mask bit */

		mask >>= 1;
		if (multiplier & mask) {
			gwsquare2 (&pp1data->gwdata, V, Vn1, GWMUL_FFT_S1 | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);	// V2
			gwmulsub4 (&pp1data->gwdata, V, Vn1, V, Vn, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);			// next n   = V3 = V1 + V2, diff 1
			gwsquare2 (&pp1data->gwdata, Vn1, Vn1, GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);			// next n+1 = V4 = V2 + V2
		}
		else {
			gwsquare2 (&pp1data->gwdata, V, Vn, GWMUL_FFT_S1 | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);	// next n   = V2
			gwmulsub4 (&pp1data->gwdata, V, Vn, V, Vn1, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);			// next n+1 = V3 = V1 + V2, diff 1
		}

/* Loop processing rest of the mask one multiplier bit at a time.  Each iteration computes V_n = V_{2n+bit}, V_{n+1} = V_{2n+bit+1} */

		gwfft_for_fma (&pp1data->gwdata, V, V);	//GW: option that does this as part of mulsub4 would be better: GWMUL_FFT_FOR_FMA_S3!  Use it during init above.
		for (mask >>= 1; ; mask >>= 1) {
			if (mask == 1) {
				gwmulsub4 (&pp1data->gwdata, Vn, Vn1, V, V, last_mul_options);				// final n   = n + (n+1), diff 1
				break;
			}
			if (multiplier & mask) {
				gwmulsub4 (&pp1data->gwdata, Vn, Vn1, V, Vn, GWMUL_FFT_S2 | GWMUL_STARTNEXTFFT);	// next n   = n + (n+1), diff 1
				gwsquare2 (&pp1data->gwdata, Vn1, Vn1, GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);		// next n+1 = (n+1) + (n+1)
			}
			else {
				gwmulsub4 (&pp1data->gwdata, Vn, Vn1, V, Vn1, GWMUL_FFT_S1 | GWMUL_STARTNEXTFFT);	// next n+1 = n + (n+1), diff 1
				gwsquare2 (&pp1data->gwdata, Vn, Vn, GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);		// next n   = n + n
			}
		}
	}
}

/*****************************************************************************/
/*                         Main P+1 routine				     */
/*****************************************************************************/

int pplus1 (
	int	thread_num,
	struct PriorityInfo *sp_info,	/* SetPriority information */
	struct work_unit *w)
{
	pp1handle pp1data;
	giant	N;		/* Number being factored */
	giant	factor;		/* Factor found, if any */
	unsigned int memused;
	int	i, res, stop_reason, first_iter_msg, saving, near_fft_limit, echk;
	char	filename[32], buf[255], JSONbuf[4000], testnum[100];
	uint64_t sieve_start, next_prime;
	double	one_over_B, base_pct_complete, one_relp_pct;
	uint64_t last_output, last_output_t, last_output_r;
	double	allowable_maxerr, output_frequency, output_title_frequency;
	int	maxerr_restart_count = 0;
	char	*str, *msg;
	int	msglen;
	double	timers[2];
	relp_set_data_map relp_set_map;
	Dmultiple_data_map Dmultiple_map;

/* Output a blank line to separate multiple P+1 runs making the result more readable */

	OutputStr (thread_num, "\n");

/* Clear pointers to allocated memory (so common error exit code knows what to free) */

	N = NULL;
	factor = NULL;
	str = NULL;
	msg = NULL;

/* Begin initializing P+1 data structure */
/* Choose a default value for the second bound if none was specified */

	memset (&pp1data, 0, sizeof (pp1handle));
	pp1data.thread_num = thread_num;
	pp1data.stage1_threads = get_worker_num_threads (thread_num, HYPERTHREAD_LL);
	pp1data.stage2_threads = pp1data.stage1_threads + IniGetInt (INI_FILE, "Stage2ExtraThreads", 0);
	pp1data.w = w;
	pp1data.B = (uint64_t) w->B1;
	pp1data.C = (uint64_t) w->B2;
	if (pp1data.B < 90) {
		OutputStr (thread_num, "Using minimum bound #1 of 90\n");
		pp1data.B = 90;
	}
	if (pp1data.C == 0) pp1data.C = pp1data.B * 100;
	if (pp1data.C < pp1data.B) pp1data.C = pp1data.B;
	pp1data.pct_mem_to_use = 1.0;				// Use as much memory as we can unless we get allocation errors

/* Convert run number into starting point */

	if (w->nth_run <= 1) {				// Per Peter Montgomery, make 2/7 the first starting point to try
		pp1data.numerator = 2;
		pp1data.denominator = 7;
		pp1data.takeAwayBits = log2 (6.0);	// 2/7 finds all factors that are 5 mod 6.  That is, p+1 is divisible by 6.
		pp1data.success_rate = 1.0;
	} else if (w->nth_run == 2) {			// Per Peter Montgomery, make 6/5 the second starting point to try
		pp1data.numerator = 6;
		pp1data.denominator = 5;
		pp1data.takeAwayBits = log2 (4.0);	// 6/5 finds all factors that are 3 mod 4.  That is, p+1 is divisible by 4.
		pp1data.success_rate = 0.5;		// Assume 2/7 has already been run.  It will have found half our 3 mod 4 factors.
	} else {
		unsigned char small_primes[16] = {11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71};
		srand ((unsigned) time (NULL));
		pp1data.numerator = 72 + (rand () & 0x7F);
		pp1data.denominator = small_primes[rand() & 0xF];
		pp1data.takeAwayBits = log2 (2.0);	// Random start point has no special characteristics.  That is, p+1 is divisible by 2.
		pp1data.success_rate = pow (0.5, (double) w->nth_run); // This run and each previous P+1 run find only half the smooth factors.
	}

/* Decide if we will calculate an optimal B2 when stage 2 begins.  We do this by default for P+1 work where we know how much TF has been done. */

	pp1data.optimal_B2 = (!QA_IN_PROGRESS && w->sieve_depth > 50 && IniGetInt (INI_FILE, "Pplus1BestB2", 1));
	if (pp1data.optimal_B2 && pp1data.C <= pp1data.B) pp1data.C = 100 * pp1data.B;	// A guess to use for calling start_sieve_with_limit

/* Compute the number we are factoring */

	stop_reason = setN (thread_num, w, &N);
	if (stop_reason) goto exit;

/* Other initialization */

	PRAC_SEARCH = IniGetInt (INI_FILE, "PracSearch", 7);
	if (PRAC_SEARCH < 1) PRAC_SEARCH = 1;
	if (PRAC_SEARCH > 50) PRAC_SEARCH = 50;

/* Output startup message */

	gw_as_string (testnum, w->k, w->b, w->n, w->c);
	sprintf (buf, "%s P+1", testnum);
	title (thread_num, buf);
	// Assemble startup message
	sprintf (buf, "P+1 on %s, ", testnum);
	// Don't output random start value because on a restart the starting point will be overwritten with data from the save file.
	// User can see the start point once the run completes by looking at the results.json.txt file.
	if (w->nth_run <= 2) sprintf (buf+strlen(buf), "start=%" PRIu32 "/%" PRIu32 ", ", pp1data.numerator, pp1data.denominator);
	else sprintf (buf+strlen(buf), "random start, ");
	if (pp1data.C <= pp1data.B) sprintf (buf+strlen(buf), "B1=%" PRIu64 "\n", pp1data.B);
	else if (pp1data.optimal_B2) sprintf (buf+strlen(buf), "B1=%" PRIu64 ", B2=TBD\n", pp1data.B);
	else sprintf (buf+strlen(buf), "B1=%" PRIu64 ", B2=%" PRIu64 "\n", pp1data.B, pp1data.C);
	OutputStr (thread_num, buf);
	if (w->sieve_depth > 0.0 && !pp1data.optimal_B2) {
		double prob = pm1prob (pp1data.takeAwayBits, (unsigned int) w->sieve_depth, pp1data.B, pp1data.C) * pp1data.success_rate;
		sprintf (buf, "Chance of finding a new factor assuming no ECM has been done is an estimated %.3g%%\n", prob * 100.0);
		OutputStr (thread_num, buf);
	}

/* Init filename */

restart:
	tempFileName (w, filename);
	filename[0] = 'n';

/* Perform setup functions.  This includes decding how big an FFT to use, allocating memory, calling the FFT setup code, etc. */

/* Setup the assembly code */

	gwinit (&pp1data.gwdata);
	if (IniGetInt (INI_FILE, "UseLargePages", 0)) gwset_use_large_pages (&pp1data.gwdata);
	if (HYPERTHREAD_LL) sp_info->normal_work_hyperthreading = TRUE, gwset_will_hyperthread (&pp1data.gwdata, 2);
	gwset_bench_cores (&pp1data.gwdata, HW_NUM_CORES);
	gwset_bench_workers (&pp1data.gwdata, NUM_WORKERS);
	if (ERRCHK) gwset_will_error_check (&pp1data.gwdata);
	else gwset_will_error_check_near_limit (&pp1data.gwdata);
	gwset_num_threads (&pp1data.gwdata, pp1data.stage1_threads);
	gwset_thread_callback (&pp1data.gwdata, SetAuxThreadPriority);
	gwset_thread_callback_data (&pp1data.gwdata, sp_info);
	gwset_safety_margin (&pp1data.gwdata, IniGetFloat (INI_FILE, "ExtraSafetyMargin", 0.0));
	gwset_larger_fftlen_count (&pp1data.gwdata, maxerr_restart_count < 3 ? maxerr_restart_count : 3);
	gwset_minimum_fftlen (&pp1data.gwdata, w->minimum_fftlen);
	gwset_use_spin_wait (&pp1data.gwdata, IniGetInt (INI_FILE, "SpinWait", 0));
	res = gwsetup (&pp1data.gwdata, w->k, w->b, w->n, w->c);
	if (res) {
		sprintf (buf, "Cannot initialize FFT code, errcode=%d\n", res);
		OutputBoth (thread_num, buf);
		return (STOP_FATAL_ERROR);
	}
	gwerror_checking (&pp1data.gwdata, ERRCHK);
	pp1data.stage1_fftlen = gwfftlen (&pp1data.gwdata);
	gwsetaddin (&pp1data.gwdata, -2);

/* Allocate gwnums for computing Lucas sequences */

	pp1data.V = gwalloc (&pp1data.gwdata);
	if (pp1data.V == NULL) goto oom;
	pp1data.Vn = gwalloc (&pp1data.gwdata);
	if (pp1data.Vn == NULL) goto oom;
	pp1data.Vn1 = gwalloc (&pp1data.gwdata);
	if (pp1data.Vn1 == NULL) goto oom;

/* More miscellaneous initializations */

	last_output = last_output_t = last_output_r = 0;
	gw_clear_fft_count (&pp1data.gwdata);
	first_iter_msg = TRUE;
	calc_output_frequencies (&pp1data.gwdata, &output_frequency, &output_title_frequency);

/* Output message about the FFT length chosen */

	{
		char	fft_desc[200];
		gwfft_description (&pp1data.gwdata, fft_desc);
		sprintf (buf, "Using %s\n", fft_desc);
		OutputStr (thread_num, buf);
	}

/* If we are near the maximum exponent this fft length can test, then we will roundoff check all multiplies */

	near_fft_limit = exponent_near_fft_limit (&pp1data.gwdata);
	gwerror_checking (&pp1data.gwdata, ERRCHK || near_fft_limit);

/* Figure out the maximum round-off error we will allow.  By default this is 28/64 when near the FFT limit and 26/64 otherwise. */
/* We've found that this default catches errors without raising too many spurious error messages.  The user can override this default. */
/* Since stage 2 may aggressively push EXTRA_BITS with gwsubmul4, assume we can always get large-ish round off errors */

	allowable_maxerr = IniGetFloat (INI_FILE, "MaxRoundoffError", (float) 0.4375);

/* Check for a save file and read the save file.  If there is an error reading the file then restart the P+1 factoring job from scratch. */
/* Limit number of backup files we try to read in case there is an error deleting bad save files. */

	readSaveFileStateInit (&pp1data.read_save_file_state, thread_num, filename);
	writeSaveFileStateInit (&pp1data.write_save_file_state, filename, 0);
	for ( ; ; ) {
		if (! saveFileExists (&pp1data.read_save_file_state)) {
			/* If there were save files, they are all bad.  Report a message */
			/* and temporarily abandon the work unit.  We do this in hopes that */
			/* we can successfully read one of the bad save files at a later time. */
			/* This sounds crazy, but has happened when OSes get in a funky state. */
			if (pp1data.read_save_file_state.a_non_bad_save_file_existed) {
				OutputBoth (thread_num, ALLSAVEBAD_MSG);
				return (0);
			}
			/* No save files existed, start from scratch. */
			break;
		}

		if (!pp1_restore (&pp1data)) {
			/* Close and rename the bad save file */
			saveFileBad (&pp1data.read_save_file_state);
			continue;
		}

/* Handle stage 1 save files.  If the save file that had a higher B1 target then we can reduce the target B1 to the desired B1. */

		if (pp1data.state == PP1_STATE_STAGE1) {
			sieve_start = pp1data.stage1_prime + 1;
			if (pp1data.interim_B > pp1data.B) pp1data.interim_B = pp1data.B;
			goto restart1;
		}

/* Handle between stages save files */

		if (pp1data.state == PP1_STATE_MIDSTAGE) {
			if (pp1data.B > pp1data.B_done) {
				gwfree (&pp1data.gwdata, pp1data.gg), pp1data.gg = NULL;
				goto more_B;
			}
			goto restart3b;
		}

/* Handle stage 2 save files */

		if (pp1data.state == PP1_STATE_STAGE2) {

/* If B is larger than the one in the save file, then do more stage 1 processing. */

			if (pp1data.B > pp1data.B_done) {
				gwfree (&pp1data.gwdata, pp1data.gg), pp1data.gg = NULL;
				free (pp1data.pairmap), pp1data.pairmap = NULL;
				goto more_B;
			}

/* If B is different than the one in the save file, then use the one in the save file rather than discarding all the work done thusfar in stage 2. */

			if (pp1data.B != pp1data.B_done) {
				pp1data.B = pp1data.B_done;
				sprintf (buf, "Ignoring suggested B1 value, using B1=%" PRIu64 " from the save file\n", pp1data.B);
				OutputStr (thread_num, buf);
			}

/* If bound #2 is larger in the save file then use the original bound #2.  The user that wants to discard the stage 2 work he */
/* has done thusfar and reduce the stage 2 bound must manually delete the save file. */

			if (pp1data.C < pp1data.interim_C) {
				pp1data.C = pp1data.interim_C;
				sprintf (buf, "Ignoring suggested B2 value, using B2=%" PRIu64 " from the save file\n", pp1data.C);
				OutputStr (thread_num, buf);
			}

/* Resume stage 2 */

			if (pp1data.optimal_B2) {
				pp1data.C = pp1data.interim_C;
				sprintf (buf, "Resuming P+1 in stage 2 with B2=%" PRIu64 "\n", pp1data.interim_C);
				OutputStr (thread_num, buf);
			}
			goto restart3b;
		}

/* Handle stage 2 GCD save files */

		if (pp1data.state == PP1_STATE_GCD) {
			if (pp1data.optimal_B2) pp1data.C = pp1data.C_done;
			goto restart4;
		}

/* Handle case where we have a completed save file (the PP1_STATE_DONE state) */

		ASSERTG (pp1data.state == PP1_STATE_DONE);
		if (pp1data.B > pp1data.B_done) goto more_B;
		if (pp1data.C > pp1data.C_done) {
			pp1data.state = PP1_STATE_MIDSTAGE;
			goto restart3a;
		}

/* The save file indicates we've tested to these bounds already */

		sprintf (buf, "%s already tested to B1=%" PRIu64 " and B2=%" PRIu64 ".\n",
			 gwmodulo_as_string (&pp1data.gwdata), pp1data.B_done, pp1data.C_done);
		OutputBoth (thread_num, buf);
		goto done;
	}

/* Start this P+1 run from scratch starting with V = 2/7 or 6/5 or random */

	pp1_calc_start (&pp1data.gwdata, pp1data.numerator, pp1data.denominator, N, pp1data.V);	/* V = 2/7 or 6/5 or random mod N */
	pp1data.B_done = 0;
	pp1data.interim_B = pp1data.B;
	sieve_start = 2;

/* The stage 1 restart point */

restart1:
	strcpy (w->stage, "S1");
	pp1data.state = PP1_STATE_STAGE1;
	set_memory_usage (thread_num, 0, cvt_gwnums_to_mem (&pp1data.gwdata, 3));
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);
	stop_reason = start_sieve_with_limit (thread_num, sieve_start, (uint32_t) sqrt ((double) pp1data.C), &pp1data.sieve_info);
	if (stop_reason) goto exit;
	pp1data.stage1_prime = sieve (pp1data.sieve_info);
	one_over_B = 1.0 / (double) pp1data.B;
	w->pct_complete = (double) pp1data.stage1_prime * one_over_B;
	for ( ; pp1data.stage1_prime <= pp1data.interim_B; pp1data.stage1_prime = next_prime) {
		uint64_t mult, max;
		int	count;

		next_prime = sieve (pp1data.sieve_info);

/* Test for user interrupt, save files, and error checking */

		stop_reason = stopCheck (thread_num);
		saving = testSaveFilesFlag (thread_num);
		echk = stop_reason || saving || ERRCHK || near_fft_limit || ((pp1data.stage1_prime & 255) == 255);
		gwerror_checking (&pp1data.gwdata, echk);

/* Count the number of prime powers where B_done < prime^n <= interim_B */

		count = 0;
		max = pp1data.interim_B / pp1data.stage1_prime;
		for (mult = pp1data.stage1_prime; ; mult *= pp1data.stage1_prime) {
			if (mult > pp1data.B_done) count++;
			if (mult > max) break;
		}

/* Adjust the count of prime powers.  All p+1 factors are a multiple of 2.  For primes 2 and 3, we increase the count by one since a */
/* starting fraction of 2/7 has p+1 factors that are a multiple of 6 and fraction of 6/5 has p+1 factors that are a multiple of 4. */

		if (pp1data.stage1_prime <= 3 && pp1data.B_done == 0) count += (pp1data.stage1_prime == 2) ? 2 : 1;

/* For prime 2, we use square_carefully as the fraction used in calculating the initial V value can result in pathological data. */

		if (pp1data.stage1_prime == 2 && pp1data.B_done == 0) {
			if (count < (int) log2 (w->n) + 3) count = (int) log2 (w->n) + 3;
			for ( ; count; count--) {
				gwmul3_carefully (&pp1data.gwdata, pp1data.V, pp1data.V, pp1data.V, GWMUL_ADDINCONST);
			}
			/* Free the memory allocated for GW_RANDOM */
			gwfree_internal_memory (&pp1data.gwdata);
			/* Include the Mersenne (or generalized Fermat) exponent.  I was against this (encourages using P+1 to find P-1 factors */
			/* rather than the more efficient P-1 factoring algorithm).  But just in case a P-1 run was done and a hardware error */
			/* occurred, give P+1 a chance to find a missed factor. */
			if (w->n > pp1data.B && (isMersenne (w->k, w->b, w->n, w->c) || isGeneralizedFermat (w->k, w->b, w->n, w->c)))
				pp1_mul (&pp1data, w->n, FALSE);
		}

/* Apply the proper number of primes */

		else {
			for ( ; count; count--) {
				int	last_mul = (count == 1 && (saving || stop_reason || next_prime > pp1data.B));
				pp1_mul (&pp1data, pp1data.stage1_prime, last_mul);
			}
		}

/* Test for an error */

		if (gw_test_for_error (&pp1data.gwdata) || gw_get_maxerr (&pp1data.gwdata) > allowable_maxerr) goto err;

/* Calculate our stage 1 percentage complete */

		w->pct_complete = (double) pp1data.stage1_prime * one_over_B;

/* Output the title every so often */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pp1data.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s P+1 stage 1",
				 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pp1data.gwdata));
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&pp1data.gwdata);
		}

/* Every N squarings, output a progress report */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pp1data.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s stage 1 is %.*f%% complete.",
				 gwmodulo_as_string (&pp1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, " Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			if (pp1data.stage1_prime != 2 || pp1data.B_done != 0) OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&pp1data.gwdata);
			first_iter_msg = FALSE;
		}

/* Every N squarings, output a progress report to the results file */

		if ((ITER_OUTPUT_RES != 999999999 && gw_get_fft_count (&pp1data.gwdata) >= last_output_r + 2 * ITER_OUTPUT_RES) ||
		    (NO_GUI && stop_reason)) {
			sprintf (buf, "%s stage 1 is %.*f%% complete.\n",
				 gwmodulo_as_string (&pp1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			writeResults (buf);
			last_output_r = gw_get_fft_count (&pp1data.gwdata);
		}

/* Check for escape and/or if its time to write a save file */

		if (stop_reason || saving) {
			pp1_save (&pp1data);
			if (stop_reason) goto exit;
		}
	}

	pp1data.B_done = pp1data.interim_B;
	end_timer (timers, 0);
	end_timer (timers, 1);

/* Check for the rare case where we need to do even more stage 1.  This happens using a save file created with a smaller bound #1. */

	if (pp1data.B > pp1data.B_done) {
more_B:		pp1data.interim_B = pp1data.B;
		sieve_start = 1;
//GW - pct_complete resets  to 0 because of setting stage1_prime to 2
		goto restart1;
	}
	pp1data.C_done = pp1data.B;

/* Do stage 1 cleanup, resume "standard" error checking */

	gwerror_checking (&pp1data.gwdata, ERRCHK || near_fft_limit);

/* Stage 1 complete, print a message */

	sprintf (buf, "%s stage 1 complete. %" PRIu64 " transforms. Total time: ", gwmodulo_as_string (&pp1data.gwdata), gw_get_fft_count (&pp1data.gwdata));
	print_timer (timers, 1, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&pp1data.gwdata);

/* Print out round off error */

	if (ERRCHK) {
		sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&pp1data.gwdata));
		OutputStr (thread_num, buf);
		gw_clear_maxerr (&pp1data.gwdata);
	}

/* Check to see if we found a factor - do GCD (V-2, N) */

	strcpy (w->stage, "S1");
	w->pct_complete = 1.0;
	if (pp1data.C <= pp1data.B || (!QA_IN_PROGRESS && IniGetInt (INI_FILE, "Stage1GCD", 1))) {
		start_timer_from_zero (timers, 0);
		gwsmalladd (&pp1data.gwdata, -2, pp1data.V);
		stop_reason = gcd (&pp1data.gwdata, thread_num, pp1data.V, N, &factor);
		gwsmalladd (&pp1data.gwdata, 2, pp1data.V);
		if (stop_reason) {
			pp1_save (&pp1data);
			goto exit;
		}
		end_timer (timers, 0);
		strcpy (buf, "Stage 1 GCD complete. Time: ");
		print_timer (timers, 0, buf, TIMER_NL);
		OutputStr (thread_num, buf);
		if (factor != NULL) goto bingo;
	}

/* Skip second stage if so requested */

	if (pp1data.C <= pp1data.B) goto msg_and_exit;

/*
   Stage 2:  Use ideas from Crandall, Zimmermann, Montgomery, Preda, and Atnashev on each prime below C.
   This code is more efficient the more memory you can give it.
   Inputs: V, the value at the end of stage 1
*/

/* This is the entry point when using the save file from a completed P+1 run to go to a new bound #2. */

restart3a:
//	strcpy (w->stage, "S1");
//	w->pct_complete = 1.0;
// Unlike P-1, no work to do here transitioning from stage 1 to stage 2

/* Stage 1 is now complete */

	pp1data.state = PP1_STATE_MIDSTAGE;
	strcpy (w->stage, "S2");
	w->pct_complete = 0.0;

/* Some users have requested that a save file be written here.  Sometimes the vast amount of stage 2 memory allocated makes their machine thrash which */
/* may require a reboot.  Upon resumption, the tail end of stage 1 calculations then need to be repeated.  For now, we'll make this the default behavior. */

	if (IniGetInt (INI_FILE, "SaveBeforeStage2", 1)) pp1_save (&pp1data);

/* Restart here when in the middle of stage 2 */

restart3b:
more_C:
	start_timer_from_zero (timers, 0);
	sprintf (buf, "%s P+1 stage 2 init", gwmodulo_as_string (&pp1data.gwdata));
	title (thread_num, buf);

/* Clear flag indicating we need to restart if the maximum amount of memory changes. */

	clear_restart_if_max_memory_change (thread_num);

/* Test if we will ever have enough memory to do stage 2 based on the maximum available memory. */
/* Our minimum working set is one gwnum for gg, 4 for nQx, 3 for VD, Vn, Vn1. */

replan:	{
		unsigned long min_memory = cvt_gwnums_to_mem (&pp1data.gwdata, 8);
		if (max_mem (thread_num) < min_memory) {
			sprintf (buf, "Insufficient memory to ever run stage 2 -- %luMB needed.\n", min_memory);
			OutputStr (thread_num, buf);
			pp1data.C = pp1data.B_done;
			goto restart4;
		}
	}

/* Choose the best plan implementation given the currently available memory. */
/* This implementation could be "wait until we have more memory". */

	stop_reason = pp1_stage2_impl (&pp1data);
	if (stop_reason) {
		if (pp1data.state == PP1_STATE_MIDSTAGE) pp1_save (&pp1data);
		goto exit;
	}

/* Record the amount of memory this thread will be using in stage 2. */

	memused = cvt_gwnums_to_mem (&pp1data.gwdata, pp1data.stage2_numvals);
	memused += (int) (pp1data.pairmap_size >> 20);
	// To dodge possible infinite loop if pp1_stage2_impl allocates too much memory (it shouldn't), decrease the percentage of memory we are allowed to use
	// Beware that replaning may allocate a larger pairmap
	if (set_memory_usage (thread_num, MEM_VARIABLE_USAGE, memused)) {
		pp1data.pct_mem_to_use *= 0.99;
		free (pp1data.pairmap); pp1data.pairmap = NULL;
		goto replan;
	}

/* Output a useful message regarding memory usage */

	sprintf (buf, "Using %uMB of memory.\n", memused);
	OutputStr (thread_num, buf);

/* Do preliminary processing of the relp_sets */

	process_relp_sets (pp1data.totrels, pp1data.numrels, pp1data.relp_sets, relp_set_map, Dmultiple_map);

/* Initialize variables for second stage */

	// Calculate the percent completed by previous pairmaps
	base_pct_complete = (double) (pp1data.B2_start - pp1data.last_relocatable) / (double) (pp1data.C - pp1data.last_relocatable);
	// Calculate the percent completed by each relative prime in this pairmap
	one_relp_pct = (1.0 - base_pct_complete) / (double) (pp1data.numDsections * pp1data.totrels);
	// Calculate the percent completed by previous pairmaps and the current pairmap
	w->pct_complete = base_pct_complete + (double) (pp1data.Dsection * pp1data.totrels) * one_relp_pct;

/* Allocate nQx array of pointers to relative prime gwnums.  Allocate an array large enough to hold values for all relp sets. */
/* This is more than we will need once stage 2 init completes. */

	int	num_relp_sets;		// Total number of relp_sets including intermediate relp_sets
	num_relp_sets = (int) relp_set_map.size();
	pp1data.nQx = (gwnum *) malloc (num_relp_sets * pp1data.numrels * sizeof (gwnum));
	if (pp1data.nQx == NULL) goto lowmem;

/* Calculate some handy values for computing the first two relp sets: (0,-1) */

	int	set_minus1_nQx_index;	// Index into nQx array for the -1 relp set
	set_minus1_nQx_index = relp_set_map.find(-1)->second.nQx_store;
	// Map nth relp to either set 0 nQx index or set -1 nQx index
	#define nqxmap(relp)	((relp) < pp1data.numrels ? (relp) : set_minus1_nQx_index + (relp) - pp1data.numrels)

/* A fast 1,5 mod 6 nQx initialization for relative primes less than D */

	ASSERTG (pp1data.D % 3 == 0);
	{
		gwnum	t1, t2, t3, t4, t5;
		gwnum	V1, V2, V3, V5, V6, V7, V11;
		struct {
			gwnum	Vi;
			gwnum	Vi_minus6;
			int	Vi_is_relp;
			int	Vi_minus6_is_relp;
		} V1mod6, V5mod6, *curr, *notcurr;
		int	i_gap, totrels, have_VD;

/* Allocate memory and init values for computing nQx.  We need V_1, V_5, V_6, V_7, V_11. */
/* We also need V_2 or V_4 to compute V_D in the middle of the nQx init loop. */
/* NOTE:  By loop's end, V_D is stored in pp1data.V */

		t1 = gwalloc (&pp1data.gwdata);
		if (t1 == NULL) goto lowmem;
		t2 = gwalloc (&pp1data.gwdata);
		if (t2 == NULL) goto lowmem;
		t3 = gwalloc (&pp1data.gwdata);
		if (t3 == NULL) goto lowmem;
		t4 = pp1data.Vn;
		t5 = pp1data.Vn1;

/* Compute V_2 and place in pp1data.V.  This is the value we will save if init is aborted and we create a save file. */
/* V_2 will be replaced by V_D later on in this loop.  At all times pp1data.V will be a save-able value! */

		luc_dbl (&pp1data.gwdata, pp1data.V, t1);			// V2 = 2 * V1
		gwswap (t1, pp1data.V);
		V1 = t1;
		V2 = pp1data.V;

/* Compute V_5, V_6, V_7, V_11 */

		V3 = t2;
		luc_add (&pp1data.gwdata, V2, V1, V1, V3);			// V3 = V2 + V1 (diff V1)

		V5 = t3;
		luc_add (&pp1data.gwdata, V3, V2, V1, V5);			// V5 = V3 + V2 (diff V1)

		V6 = V3;
		luc_dbl (&pp1data.gwdata, V3, V6);				// V6 = 2 * V3, V3 no longer needed

		V7 = t4;
		luc_add (&pp1data.gwdata, V6, V1, V5, V7);			// V7 = V6 + V1 (diff V5)

		V11 = t5;
		luc_add (&pp1data.gwdata, V6, V5, V1, V11);			// V11 = V6 + V5 (diff V1)

/* Init structures used in the loop below */

		V1mod6.Vi_minus6 = V1;
		V1mod6.Vi_minus6_is_relp = 1;
		V5mod6.Vi_minus6 = V5;
		V5mod6.Vi_minus6_is_relp = relatively_prime (5, pp1data.D);
		V1mod6.Vi = V7;
		V1mod6.Vi_is_relp = relatively_prime (7, pp1data.D);
		V5mod6.Vi = V11;
		V5mod6.Vi_is_relp = relatively_prime (11, pp1data.D);
		pp1data.nQx[0] = V1; totrels = 1;
		if (V5mod6.Vi_minus6_is_relp) pp1data.nQx[totrels++] = V5;
		if (V1mod6.Vi_is_relp) pp1data.nQx[totrels++] = V7;
		if (V5mod6.Vi_is_relp) pp1data.nQx[totrels++] = V11;

/* Compute the rest of the nQx values (V_i for i >= 7) */

		have_VD = FALSE;
		for (i = 7, i_gap = 4; ; i += i_gap, i_gap = 6 - i_gap) {
			gwnum	next_i;
			int	next_i_is_relp;

/* Point to the i, i-6 pair to work on */

			curr = (i_gap == 4) ? &V1mod6 : &V5mod6;

/* Compute V_D which we will need in computing multiples of V_D.  Do this with a single luc_add call when we reach two values that */
/* are 2 or 4 apart that add to D. */

			if (i + (i + i_gap) == pp1data.D) {
				gwnum	Vgap = V2;
				ASSERTG (Vgap == pp1data.V);
				if (i_gap == 4) luc_dbl (&pp1data.gwdata, Vgap, Vgap);			// Vgap = V4 = 2 * V2, V2 no longer needed
				luc_add (&pp1data.gwdata, V1mod6.Vi, V5mod6.Vi, Vgap, pp1data.V);	// VD = V{i} + V{i+gap} (diff Vgap), Vgap no longer needed
				have_VD = TRUE;
			}

/* Break out of loop when we have all our nQx values less than D */

			if (have_VD && (totrels == pp1data.totrels || totrels == pp1data.numrels * 2)) break;

/* See if we are about to compute the next relative prime */

			next_i_is_relp = (relatively_prime (i+6, pp1data.D) && totrels < pp1data.totrels);

/* If so, and if it is the last relprime then free up the some resources to keep maximum number of temporaries to a minimum. */
/* This is of marginal utility as peak will occur in the next loop whenever totrels > numrels * 2. */
/* Also catch the case where we are using the dead minimum number of relative primes - the first i after D/2 is the last i. */

			if ((next_i_is_relp && have_VD && (totrels+1 == pp1data.totrels || totrels+1 == pp1data.numrels * 2)) ||
			    (!have_VD && i+6 > pp1data.D/2 && pp1data.totrels == pp1data.numrels)) {
				notcurr = (curr == &V5mod6) ? &V1mod6 : &V5mod6;
				if (!notcurr->Vi_is_relp) gwfree (&pp1data.gwdata, notcurr->Vi), notcurr->Vi = NULL;
				if (!notcurr->Vi_minus6_is_relp) gwfree (&pp1data.gwdata, notcurr->Vi_minus6), notcurr->Vi_minus6 = NULL;
			}			

/* Get next i value.  Don't destroy Vi_minus6 if it is an nQx value. */

			if (curr->Vi_minus6_is_relp) {
				next_i = gwalloc (&pp1data.gwdata);
				if (next_i == NULL) goto lowmem;
			} else
				next_i = curr->Vi_minus6;

			luc_add (&pp1data.gwdata, curr->Vi, V6, curr->Vi_minus6, next_i);	// Next i
			if (next_i_is_relp) {
				pp1data.nQx[nqxmap(totrels)] = next_i;
				totrels++;
			}

/* Shuffle i values along */

			curr->Vi_minus6 = curr->Vi;
			curr->Vi_minus6_is_relp = curr->Vi_is_relp;
			curr->Vi = next_i;
			curr->Vi_is_relp = next_i_is_relp;

/* Check for errors, user ESC, restart for mem changed, etc. */

			if (gw_test_for_error (&pp1data.gwdata)) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}

/* Free memory used in computing nQx values */

		if (!V1mod6.Vi_is_relp) gwfree (&pp1data.gwdata, V1mod6.Vi);
		if (!V1mod6.Vi_minus6_is_relp) gwfree (&pp1data.gwdata, V1mod6.Vi_minus6);
		if (!V5mod6.Vi_is_relp) gwfree (&pp1data.gwdata, V5mod6.Vi);
		if (!V5mod6.Vi_minus6_is_relp) gwfree (&pp1data.gwdata, V5mod6.Vi_minus6);
		gwfree (&pp1data.gwdata, V6);

/* Replace the Vn and Vn1 gwnums that we used as temporaries. */

		pp1data.Vn = gwalloc (&pp1data.gwdata);
		pp1data.Vn1 = gwalloc (&pp1data.gwdata);
	}
	#undef nqxmap

/* Add V_D computed above to the D-multiples map */

	{
		auto it_Dmult = Dmultiple_map.find (1);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({1, {0, 1}}).first;
		it_Dmult->second.set_val_buffers (&pp1data.gwdata, pp1data.V, TRUE);

/* Init for computing Q^(multiples of D).  We simply add the starting multiples of D that need to be computed to the D-multiples map. */
/* Vn = V_{Dsection-1}, Vn1 = V_{Dsection} */

		uint64_t D_multiplier = (pp1data.B2_start + pp1data.D / 2) / pp1data.D + pp1data.Dsection;
		ASSERTG (D_multiplier > 2); // Would require special code to the collision between V and (Vn or Vn1)
		it_Dmult = Dmultiple_map.find (D_multiplier - 1);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({D_multiplier - 1, {0, D_multiplier - 1}}).first;
		it_Dmult->second.set_val_buffers (&pp1data.gwdata, pp1data.Vn, TRUE);

		it_Dmult = Dmultiple_map.find (D_multiplier);
		if (it_Dmult == Dmultiple_map.end()) it_Dmult = Dmultiple_map.insert({D_multiplier, {0, D_multiplier}}).first;
		it_Dmult->second.set_val_buffers (&pp1data.gwdata, pp1data.Vn1, TRUE);
	}

// Calculate all the needed V_D^(multiples-of-D)

	process_Dmult_map (Dmultiple_map);
	for (auto this_Dmult = Dmultiple_map.begin(); this_Dmult != Dmultiple_map.end(); ++this_Dmult) {
		// Ignore the already computed V_D
		if (this_Dmult->first == 1) continue;
		// If necessary, allocate buffer for this V_D^multiple-of-D
		if (this_Dmult->second.val.x == NULL) {
			gwnum	tmp = gwalloc (&pp1data.gwdata);
			if (tmp == NULL) goto lowmem;
			this_Dmult->second.set_val_buffers (&pp1data.gwdata, tmp, FALSE);
		}
		// Compute via luc_dbl if diff is zero
		auto it_base = Dmultiple_map.find (this_Dmult->second.base);
		if (this_Dmult->second.diff == 0) {
			luc_dbl (&pp1data.gwdata, it_base->second.val.x, this_Dmult->second.val.x);
		}
		// Compute using luc_add if diff is non-zero
		else {
			auto it_addin = Dmultiple_map.find (this_Dmult->second.addin);
			auto it_diff = Dmultiple_map.find (this_Dmult->second.diff);
			luc_add (&pp1data.gwdata, it_base->second.val.x, it_addin->second.val.x, it_diff->second.val.x, this_Dmult->second.val.x);
			// Free addin, diff if no longer needed
			it_addin->second.free_if_Dmult_last_used_by (this_Dmult->first);
			it_diff->second.free_if_Dmult_last_used_by (this_Dmult->first);
		}
		// Free base if no longer needed
		it_base->second.free_if_Dmult_last_used_by (this_Dmult->first);
		// Check for errors, user ESC, restart for mem changed, etc.
		if (gw_test_for_error (&pp1data.gwdata)) goto err;
		stop_reason = stopCheck (thread_num);
		if (stop_reason) goto possible_lowmem;
	}
//GW: are xz buffers allocated by Dmultiple map properly cleaned up (especially in oom case)?  use class to do it? 

/* We're about to reach peak memory usage, free any gwnum internal memory */

	gwfree_internal_memory (&pp1data.gwdata);

/* Compute relp sets other than 0,-1 */

	int	num_partials;
	num_partials = one_based_modulo (pp1data.totrels, pp1data.numrels);
	// Loop over every relp_set we need to calculate
	for (auto it_relp_set = relp_set_map.begin (); it_relp_set != relp_set_map.end (); ++it_relp_set) {
		// Relp sets 0 and -1 are already computed
		if (it_relp_set->first == 0 || it_relp_set->first == -1) continue;
		// Find the base relp_set and diff relp_set needed to create this relp_set
		auto it_base_relp_set = relp_set_map.find (it_relp_set->second.base);
		auto it_diff_relp_set = relp_set_map.find (it_relp_set->second.diff);
		// Find the Dmultiple needed to create this relp_set
		auto it_Dmultiple = Dmultiple_map.find (it_relp_set->second.Dmultiple);
		// Determine if the diff relps are accessed in reverse order
		bool	diff_access_inverted = (it_base_relp_set->first < 0) ^ (it_diff_relp_set->first < 0);
		// Loop over all relative primes below D/2 that need calculating in this relp_set (backwards is better for peak memory)
		for (i = (it_relp_set->second.partial ? num_partials : pp1data.numrels) - 1; i >= 0; i--) {
			gwnum	*Vbase, *Vdiff, Vnew;
			int	ni;

			// Get the base gwnum from the nQx entry
			ni = it_base_relp_set->second.nQx_store + i;
			Vbase = &pp1data.nQx[ni];
			// Get the diff gwnum from the diff nQx entry
			// If diff has a different sign bit than base, then we need to index the diff relp_set in reverse order
			ni = it_diff_relp_set->second.nQx_store;
			if (!diff_access_inverted) ni += i; else ni += (pp1data.numrels-1)-i;
			Vdiff = &pp1data.nQx[ni];

			// Set flags if Vbase and/or Vdiff will be freed because they are not used in any more relp_set calculations
			bool Vbase_to_be_freed = (!it_base_relp_set->second.permanent &&
						  it_base_relp_set->second.is_last_use (it_relp_set->first, i, num_partials, pp1data.numrels, FALSE));
			bool Vdiff_to_be_freed = (!it_diff_relp_set->second.permanent &&
						  it_diff_relp_set->second.is_last_use (it_relp_set->first, i, num_partials, pp1data.numrels, diff_access_inverted));
			bool Dmultiple_to_be_freed = (i == 0 && it_Dmultiple->second.will_free_due_to_relp_last_used_by (it_relp_set->first));

			// Allocate the new relp value (or re-use Vbase or Vdiff)
			if (Vbase_to_be_freed) Vnew = *Vbase;
			else if (Vdiff_to_be_freed) Vnew = *Vdiff;
			else if (Dmultiple_to_be_freed) Vnew = it_Dmultiple->second.val.x;
			else {
				Vnew = gwalloc (&pp1data.gwdata);
				if (Vnew == NULL) goto lowmem;
			}

			// Compute the new relp value
			luc_add (&pp1data.gwdata, *Vbase, it_Dmultiple->second.val.x, *Vdiff, Vnew);	// Vnew = Vbase + Dmult, Vdiff e.g. 47+30 or 29+30

			// Add the relp to the nQx array.
			ni = it_relp_set->second.nQx_store + i;
			pp1data.nQx[ni] = Vnew;

			// If base is not needed for further relp_set calculations, then free Vbase
			if (Vbase_to_be_freed && Vnew != *Vbase) {
				gwfree (&pp1data.gwdata, *Vbase);
				*Vbase = NULL;
			}

			// If diff is not needed for further relp_set calculations, then free Vdiff
			if (Vdiff_to_be_freed && Vnew != *Vdiff) {
				gwfree (&pp1data.gwdata, *Vdiff);
				*Vdiff = NULL;
			}

			// Free the Dmultiple if this is the last time it will be used
			if (Dmultiple_to_be_freed) {
				it_Dmultiple->second.do_not_free_val = (Vnew == it_Dmultiple->second.val.x);
				it_Dmultiple->second.free_if_relp_last_used_by (it_relp_set->first);
			}

			// Check for errors, user ESC, restart for mem changed, etc.
			if (gw_test_for_error (&pp1data.gwdata)) goto err;
			stop_reason = stopCheck (thread_num);
			if (stop_reason) goto possible_lowmem;
		}
	}

/* Clear the maps used to build relp sets */

	Dmultiple_map.clear ();
	relp_set_map.clear ();

/* Initialize gg to a multiple of V-2 in case the user opted to skip the GCD after stage 1.  We used to init gg to V-2 at the beginning */
/* of stage 2 init.  This more complicated delayed initialization reduces peak memory utilization by one gwnum. */

	if (pp1data.gg == NULL) {
		pp1data.gg = gwalloc (&pp1data.gwdata);
		if (pp1data.gg == NULL) goto oom;
		dbltogw (&pp1data.gwdata, 1.0, pp1data.gg);
		gwmul3 (&pp1data.gwdata, pp1data.Vn1, pp1data.gg, pp1data.gg, GWMUL_FFT_S1 | GWMUL_ADDINCONST | GWMUL_STARTNEXTFFT);
	}

/* We're at peak memory usage, free any cached gwnums */

	gwfree_cached (&pp1data.gwdata);
	mallocFreeForOS ();

/* Stage 2 init complete, change the title, output a message */

	sprintf (buf, "%.*f%% of %s P+1 stage 2 (using %uMB)",
		 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pp1data.gwdata), memused);
	title (thread_num, buf);

	end_timer (timers, 0);
	sprintf (buf, "Stage 2 init complete. %" PRIu64 " transforms. Time: ", gw_get_fft_count (&pp1data.gwdata));
	print_timer (timers, 0, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);
	gw_clear_fft_count (&pp1data.gwdata);

/* We do prime pairing with each loop iteration handling the range m-Q to m+Q where m is a multiple of D and Q is the */
/* Q-th relative prime to D.  Totrels is often much larger than the number of relative primes less than D.  This Preda/Montgomery */
/* optimization provides us with many more chances to find a prime pairing. */

	pp1data.state = PP1_STATE_STAGE2;
	start_timer_from_zero (timers, 0);
	start_timer_from_zero (timers, 1);
	last_output = last_output_t = last_output_r = 0;
	for ( ; ; ) {

/* Get next pairing from the pairmap */

		pp1data.relp += next_pair (&pp1data.pairmap_ptr);

/* Make a quick check for end of the pairmap and no more pairmaps needed (i.e. we're done with stage 2) */

		if (pp1data.relp >= pp1data.totrels && pp1data.Dsection + pp1data.relp / pp1data.totrels >= pp1data.numDsections) break;

/* Move to next D section when appropriate */

		while (pp1data.relp >= pp1data.totrels) {
			pp1data.Dsection++;
			pp1data.relp -= pp1data.totrels;
			luc_add (&pp1data.gwdata, pp1data.Vn1, pp1data.V, pp1data.Vn, pp1data.Vn);	// Vn2 = Vn1 + V, diff = Vn
			gwswap (pp1data.Vn, pp1data.Vn1);						// Vn = Vn1, Vn1 = Vn2
//GW: check for errors?
		}

/* Check for end of pairing map.  If so, generate next pairing map. */

		if (pp1data.pairmap_ptr == pp1data.pairmap + pp1data.pairmap_size) {
			ASSERTG (pp1data.relp == 0);
			pp1data.C_done = pp1data.B2_start + pp1data.Dsection * pp1data.D;
			pp1data.first_relocatable = calc_new_first_relocatable (pp1data.D, pp1data.C_done);
			int fill_window = pair_window_size (pp1data.gwdata.bit_length, pp1data.relp_sets);
			stop_reason = fill_pairmap (pp1data.thread_num, &pp1data.sieve_info, pp1data.D, fill_window,0,0,0,
						    pp1data.totrels, pp1data.relp_sets+3, pp1data.first_relocatable, pp1data.last_relocatable,
						    pp1data.C_done, pp1data.C, pp1data.max_pairmap_Dsections, &pp1data.pairmap, &pp1data.pairmap_size);
			if (stop_reason) {
//GW:				is save possible here with no pairmap generated?
				pp1_save (&pp1data);
				goto exit;
			}
			pp1data.pairmap_ptr = pp1data.pairmap;
			pp1data.relp = -1;
			continue;
		}

/* Multiply this Vn1 - nQx value into the gg accumulator */

		saving = testSaveFilesFlag (thread_num);
		stop_reason = stopCheck (thread_num);
		gwsubmul4 (&pp1data.gwdata, pp1data.Vn1, pp1data.nQx[pp1data.relp], pp1data.gg, pp1data.gg, (!stop_reason && !saving) ? GWMUL_STARTNEXTFFT : 0);

/* Calculate stage 2 percentage. */

		w->pct_complete = base_pct_complete + (double) (pp1data.Dsection * pp1data.totrels + pp1data.relp) * one_relp_pct;

/* Test for errors */

		if (gw_test_for_error (&pp1data.gwdata) || gw_get_maxerr (&pp1data.gwdata) > allowable_maxerr) goto err;

/* Output the title every so often */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pp1data.gwdata) >= last_output_t + 2 * ITER_OUTPUT * output_title_frequency)) {
			sprintf (buf, "%.*f%% of %s P+1 stage 2 (using %uMB)",
				 (int) PRECISION, trunc_percent (w->pct_complete), gwmodulo_as_string (&pp1data.gwdata), memused);
			title (thread_num, buf);
			last_output_t = gw_get_fft_count (&pp1data.gwdata);
		}

/* Write out a message every now and then */

		if (first_iter_msg ||
		    (ITER_OUTPUT != 999999999 && gw_get_fft_count (&pp1data.gwdata) >= last_output + 2 * ITER_OUTPUT * output_frequency)) {
			sprintf (buf, "%s stage 2 is %.*f%% complete.",
				 gwmodulo_as_string (&pp1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			end_timer (timers, 0);
			if (first_iter_msg) {
				strcat (buf, "\n");
			} else {
				strcat (buf, " Time: ");
				print_timer (timers, 0, buf, TIMER_NL);
			}
			OutputStr (thread_num, buf);
			start_timer_from_zero (timers, 0);
			last_output = gw_get_fft_count (&pp1data.gwdata);
			first_iter_msg = FALSE;
		}

/* Write out a message to the results file every now and then */

		if ((ITER_OUTPUT_RES != 999999999 && gw_get_fft_count (&pp1data.gwdata) >= last_output_r + 2 * ITER_OUTPUT_RES) ||
		    (NO_GUI && stop_reason)) {
			sprintf (buf, "%s stage 2 is %.*f%% complete.\n",
				 gwmodulo_as_string (&pp1data.gwdata), (int) PRECISION, trunc_percent (w->pct_complete));
			writeResults (buf);
			last_output_r = gw_get_fft_count (&pp1data.gwdata);
		}

/* Periodicly write a save file */

		if (stop_reason || saving) {
			pp1_save (&pp1data);
			if (stop_reason) goto exit;
		}
	}
	pp1data.C_done = pp1data.interim_C;

/* Free up memory */

	for (i = 0; i < pp1data.totrels; i++) gwfree (&pp1data.gwdata, pp1data.nQx[i]);
	free (pp1data.nQx), pp1data.nQx = NULL;
	free (pp1data.pairmap), pp1data.pairmap = NULL;

/* Check for the rare cases where we need to do even more stage 2.  This happens when continuing a save file in the middle of stage 2 and */
/* the save file's target bound #2 is smaller than our target bound #2. */

	if (pp1data.C > pp1data.C_done) {
		pp1data.state = PP1_STATE_MIDSTAGE;
		goto more_C;
	}

/* Set memory usage so other high memory workers can resume */

	gwfree_internal_memory (&pp1data.gwdata);
	mallocFreeForOS ();
	set_memory_usage (thread_num, 0, cvt_gwnums_to_mem (&pp1data.gwdata, 1));	// Let other high memory workers resume

/* Stage 2 is complete */

	end_timer (timers, 1);
	sprintf (buf, "%s stage 2 complete. %" PRIu64 " transforms. Total time: ", gwmodulo_as_string (&pp1data.gwdata), gw_get_fft_count (&pp1data.gwdata));
	print_timer (timers, 1, buf, TIMER_NL | TIMER_CLR);
	OutputStr (thread_num, buf);

/* Print out round off error */

	if (ERRCHK) {
		sprintf (buf, "Round off: %.10g\n", gw_get_maxerr (&pp1data.gwdata));
		OutputStr (thread_num, buf);
		gw_clear_maxerr (&pp1data.gwdata);
	}

/* See if we got lucky! */

restart4:
	pp1data.state = PP1_STATE_GCD;
	strcpy (w->stage, "S2");
	w->pct_complete = 1.0;
	start_timer_from_zero (timers, 0);
	stop_reason = gcd (&pp1data.gwdata, thread_num, pp1data.gg, N, &factor);
	if (stop_reason) {
		pp1_save (&pp1data);
		goto exit;
	}
	pp1data.state = PP1_STATE_DONE;
	end_timer (timers, 0);
	strcpy (buf, "Stage 2 GCD complete. Time: ");
	print_timer (timers, 0, buf, TIMER_NL);
	OutputStr (thread_num, buf);
	if (factor != NULL) goto bingo;

/* Output line to results file indicating P+1 run */

msg_and_exit:
	sprintf (buf, "%s completed P+1, B1=%" PRIu64, gwmodulo_as_string (&pp1data.gwdata), pp1data.B);
	if (pp1data.C > pp1data.B) sprintf (buf+strlen(buf), ", B2=%" PRIu64, pp1data.C);
	sprintf (buf+strlen(buf), ", Wi%d: %08lX\n", PORT, SEC5 (w->n, pp1data.B, pp1data.C));
	OutputStr (thread_num, buf);
	formatMsgForResultsFile (buf, w);
	writeResults (buf);

/* Format a JSON version of the result.  An example follows: */
/* {"status":"NF", "exponent":45581713, "worktype":"P+1", "b1":50000, "b2":5000000, "start":"2/7", */
/* "fft-length":5120, "security-code":"39AB1238", */
/* "program":{"name":"prime95", "version":"30.6", "build":"1"}, "timestamp":"2021-04-15 23:28:16", */
/* "user":"gw_2", "cpu":"work_computer", "aid":"FF00AA00FF00AA00FF00AA00FF00AA00"} */

	strcpy (JSONbuf, "{\"status\":\"NF\"");
	JSONaddExponent (JSONbuf, w);
	strcat (JSONbuf, ", \"worktype\":\"P+1\"");
	sprintf (JSONbuf+strlen(JSONbuf), ", \"b1\":%" PRIu64, pp1data.B);
	if (pp1data.C > pp1data.B) sprintf (JSONbuf+strlen(JSONbuf), ", \"b2\":%" PRIu64, pp1data.C);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"start\":\"%" PRIu32 "/%" PRIu32 "\"", pp1data.numerator, pp1data.denominator);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"fft-length\":%lu", pp1data.stage1_fftlen);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"security-code\":\"%08lX\"", SEC5 (w->n, pp1data.B, pp1data.C > pp1data.B ? pp1data.C : pp1data.B));
	JSONaddProgramTimestamp (JSONbuf);
	JSONaddExponentKnownFactors (JSONbuf, w);
	JSONaddUserComputerAID (JSONbuf, w);
	strcat (JSONbuf, "}");
	if (IniGetInt (INI_FILE, "OutputJSON", 1)) writeResultsJSON (JSONbuf);

/* Send P+1 completed message to the server.  Although don't do it for puny B1 values as this is just the user tinkering with P+1 factoring. */

	if (!QA_IN_PROGRESS && (pp1data.B >= 50000 || IniGetInt (INI_FILE, "SendAllFactorData", 0))) {
		struct primenetAssignmentResult pkt;
		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		strcpy (pkt.assignment_uid, w->assignment_uid);
		strcpy (pkt.message, buf);
		pkt.result_type = PRIMENET_AR_PP1_NOFACTOR;
		pkt.k = w->k;
		pkt.b = w->b;
		pkt.n = w->n;
		pkt.c = w->c;
		pkt.pp1_numerator = pp1data.numerator;
		pkt.pp1_denominator = pp1data.denominator;
		pkt.B1 = pp1data.B;
		pkt.B2 = pp1data.C;
		pkt.fftlen = pp1data.stage1_fftlen;
		pkt.done = TRUE;
		strcpy (pkt.JSONmessage, JSONbuf);
		spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
	}

/* Free save files (especially big ones created during stage 2) */
/* Create save file so that we can expand bound 1 or bound 2 at a later date.  Only allow this for 2/7 and 6/5 start. */
/* If we don't delete random start save files, then a new random start attempt would use the existing random start save file. */ 

	unlinkSaveFiles (&pp1data.write_save_file_state);
	if (!QA_IN_PROGRESS && IniGetInt (INI_FILE, "KeepPplus1SaveFiles", 0) && w->nth_run <= 2)
		pp1_save (&pp1data);

/* Return stop code indicating success or work unit complete */ 

done:	stop_reason = STOP_WORK_UNIT_COMPLETE;

/* Free memory and return */

exit:	Dmultiple_map.clear ();
	relp_set_map.clear ();
	pp1_cleanup (&pp1data);
	free (N);
	free (factor);
	free (str);
	free (msg);
	return (stop_reason);

/* Low or possibly low on memory in stage 2 init, create save file, reduce memory settings, and try again */

lowmem:	stop_reason = OutOfMemory (thread_num);
possible_lowmem:
	if (pp1data.state == PP1_STATE_MIDSTAGE) pp1_save (&pp1data);
	if (stop_reason != STOP_OUT_OF_MEM) goto exit;
//GW: saving file twice?
	pp1_save (&pp1data);
	Dmultiple_map.clear ();
	relp_set_map.clear ();
	pp1_cleanup (&pp1data);
	OutputBoth (thread_num, "Memory allocation error.  Trying again using less memory.\n");
	pp1data.pct_mem_to_use *= 0.8;
	goto restart;

/* We've run out of memory.  Print error message and exit. */

oom:	stop_reason = OutOfMemory (thread_num);
	goto exit;

/* Print a message if we found a factor! */

bingo:	if (pp1data.state < PP1_STATE_MIDSTAGE)
		sprintf (buf, "P+1 found a factor in stage #1, B1=%" PRIu64 ".\n", pp1data.B);
	else
		sprintf (buf, "P+1 found a factor in stage #2, B1=%" PRIu64 ", B2=%" PRIu64 ".\n", pp1data.B, pp1data.C);
	OutputBoth (thread_num, buf);

/* Allocate memory for the string representation of the factor and for */
/* a message.  Convert the factor to a string.  Allocate lots of extra space */
/* as formatMsgForResultsFile can append a lot of text. */	

	msglen = factor->sign * 10 + 400;
	str = (char *) malloc (msglen);
	if (str == NULL) {
		stop_reason = OutOfMemory (thread_num);
		goto exit;
	}
	msg = (char *) malloc (msglen);
	if (msg == NULL) {
		stop_reason = OutOfMemory (thread_num);
		goto exit;
	}
	gtoc (factor, str, msglen);

/* Validate the factor we just found */

	if (!testFactor (&pp1data.gwdata, w, factor)) {
		sprintf (msg, "ERROR: Bad factor for %s found: %s\n", gwmodulo_as_string (&pp1data.gwdata), str);
		OutputBoth (thread_num, msg);
		unlinkSaveFiles (&pp1data.write_save_file_state);
		OutputStr (thread_num, "Restarting P+1 from scratch.\n");
		stop_reason = 0;
		goto error_restart;
	}

/* Output the validated factor */

	if (pp1data.state < PP1_STATE_MIDSTAGE)
		sprintf (msg, "%s has a factor: %s (P+1, B1=%" PRIu64 ")\n",
			 gwmodulo_as_string (&pp1data.gwdata), str, pp1data.B);
	else
		sprintf (msg, "%s has a factor: %s (P+1, B1=%" PRIu64 ", B2=%" PRIu64 ")\n",
			 gwmodulo_as_string (&pp1data.gwdata), str, pp1data.B, pp1data.C);
	OutputStr (thread_num, msg);
	formatMsgForResultsFile (msg, w);
	writeResults (msg);

/* Format a JSON version of the result.  An example follows: */
/* {"status":"F", "exponent":45581713, "worktype":"P+1", "factors":["430639100587696027847"], */
/* "b1":50000, "b2":5000000, "start":"2/7", "fft-length":5120, "security-code":"39AB1238", */
/* "program":{"name":"prime95", "version":"30.6", "build":"1"}, "timestamp":"2021-01-15 23:28:16", */
/* "user":"gw_2", "cpu":"work_computer", "aid":"FF00AA00FF00AA00FF00AA00FF00AA00"} */

	strcpy (JSONbuf, "{\"status\":\"F\"");
	JSONaddExponent (JSONbuf, w);
	strcat (JSONbuf, ", \"worktype\":\"P+1\"");
	sprintf (JSONbuf+strlen(JSONbuf), ", \"factors\":[\"%s\"]", str);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"b1\":%" PRIu64, pp1data.B);
	if (pp1data.state > PP1_STATE_MIDSTAGE) sprintf (JSONbuf+strlen(JSONbuf), ", \"b2\":%" PRIu64, pp1data.C);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"start\":\"%" PRIu32 "/%" PRIu32 "\"", pp1data.numerator, pp1data.denominator);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"fft-length\":%lu", pp1data.stage1_fftlen);
	sprintf (JSONbuf+strlen(JSONbuf), ", \"security-code\":\"%08lX\"", SEC5 (w->n, pp1data.B, pp1data.state > PP1_STATE_MIDSTAGE ? pp1data.C : pp1data.B));
	JSONaddProgramTimestamp (JSONbuf);
	JSONaddExponentKnownFactors (JSONbuf, w);
	JSONaddUserComputerAID (JSONbuf, w);
	strcat (JSONbuf, "}");
	if (IniGetInt (INI_FILE, "OutputJSON", 1)) writeResultsJSON (JSONbuf);

/* Send assignment result to the server.  To avoid flooding the server with small factors from users needlessly redoing */
/* factoring work, make sure the factor is more than 67 bits or so. */

	if (!QA_IN_PROGRESS && (strlen (str) >= 21 || IniGetInt (INI_FILE, "SendAllFactorData", 0))) {
		struct primenetAssignmentResult pkt;
		memset (&pkt, 0, sizeof (pkt));
		strcpy (pkt.computer_guid, COMPUTER_GUID);
		strcpy (pkt.assignment_uid, w->assignment_uid);
		strcpy (pkt.message, msg);
		pkt.result_type = PRIMENET_AR_PP1_FACTOR;
		pkt.k = w->k;
		pkt.b = w->b;
		pkt.n = w->n;
		pkt.c = w->c;
		pkt.pp1_numerator = pp1data.numerator;
		pkt.pp1_denominator = pp1data.denominator;
		truncated_strcpy (pkt.factor, sizeof (pkt.factor), str);
		pkt.B1 = pp1data.B;
		pkt.B2 = (pp1data.state < PP1_STATE_MIDSTAGE ? 0 : pp1data.C);
		pkt.fftlen = pp1data.stage1_fftlen;
		pkt.done = TRUE;
		strcpy (pkt.JSONmessage, JSONbuf);
		spoolMessage (PRIMENET_ASSIGNMENT_RESULT, &pkt);
	}

/* Free save files (especially big ones created during stage 2) */
/* Then create save file so that we can expand bound 1 or bound 2 at a later date. */

	unlinkSaveFiles (&pp1data.write_save_file_state);
	if (!QA_IN_PROGRESS && IniGetInt (INI_FILE, "KeepPplus1SaveFiles", 0) && w->nth_run <= 2)
		pp1_save (&pp1data);

/* Since we found a factor, then we may have performed less work than */
/* expected.  Make sure we do not update the rolling average with */
/* this inaccurate data. */

	invalidateNextRollingAverageUpdate ();

/* Create a PRP work unit to immediately test the Mersenne cofactor */

	if (USE_PRIMENET && w->k == 1.0 && w->b == 2 && w->c == -1 && strlen (str) >= 21 && (int) w->n < IniGetInt (INI_FILE, "MaxAutoPRPExponent", 5000000)) {
		struct work_unit w_prp;
		generatePRPWorkUnit (w, str, &w_prp);
		// Add work unit before or after this work unit
		w_prp.next = w;
		stop_reason = addWorkToDoLine (thread_num, &w_prp, ADD_AFTER_SPECIFIC);
		if (stop_reason) goto exit;
	}

/* Remove the exponent from the worktodo.txt file */

	stop_reason = STOP_WORK_UNIT_COMPLETE;
	goto exit;

/* Output an error message saying we are restarting. */
/* Sleep five minutes before restarting from last save file. */

err:	if (gw_get_maxerr (&pp1data.gwdata) > allowable_maxerr) {
		sprintf (buf, "Possible roundoff error (%.8g), backtracking to last save file and using larger FFT.\n", gw_get_maxerr (&pp1data.gwdata));
		OutputStr (thread_num, buf);
		maxerr_restart_count++;
	} else {
		OutputBoth (thread_num, "SUMOUT error occurred.\n");
		stop_reason = SleepFive (thread_num);
		if (stop_reason) goto exit;
	}
error_restart:
	Dmultiple_map.clear ();
	relp_set_map.clear ();
	pp1_cleanup (&pp1data);
	free (factor), factor = NULL;
	free (str), str = NULL;
	free (msg), msg = NULL;
	goto restart;
}

/* Read a file of P+1 tests to run as part of a QA process */
/* The format of this file is: */
/*	k, n, c, B1, B2, start, factor */
/* Use Advanced/Time 9993 to run the QA suite */

int pplus1_QA (
	int	thread_num,
	struct PriorityInfo *sp_info)	/* SetPriority information */
{
	FILE	*fd;

/* Set the title */

	title (thread_num, "QA");

/* Open QA file */

	fd = fopen ("qa_pp1", "r");
	if (fd == NULL) {
		OutputStr (thread_num, "File named 'qa_pp1' could not be opened.\n");
		return (STOP_FILE_IO_ERROR);
	}

/* Loop until the entire file is processed */

	QA_TYPE = 0;
	for ( ; ; ) {
		struct work_unit w;
		double	k;
		unsigned long b, n, B1, B2, start;
		signed long c;
		char	fac_str[80];
		int	stop_reason;

/* Read a line from the file */

		n = 0;
		(void) fscanf (fd, "%lf,%lu,%lu,%ld,%lu,%lu,%lu,%s\n", &k, &b, &n, &c, &B1, &B2, &start, fac_str);
		if (n == 0) break;

/* If p is 1, set QA_TYPE */

		if (n == 1) {
			QA_TYPE = c;
			continue;
		}

/* Convert the factor we expect to find into a "giant" type */

		QA_FACTOR = allocgiant ((int) strlen (fac_str));
		ctog (fac_str, QA_FACTOR);

/*test various num_tmps
test 4 (or more?) stage 2 code paths
print out each test case (all relevant data)*/

/* Do the P+1 */

		memset (&w, 0, sizeof (w));
		w.work_type = WORK_PPLUS1;
		w.k = k;
		w.b = b;
		w.n = n;
		w.c = c;
		w.B1 = B1;
		w.B2 = B2;
		w.nth_run = start;
		QA_IN_PROGRESS = TRUE;
		stop_reason = pplus1 (0, sp_info, &w);
		QA_IN_PROGRESS = FALSE;
		free (QA_FACTOR);
		if (stop_reason != STOP_WORK_UNIT_COMPLETE) {
			fclose (fd);
			return (stop_reason);
		}
	}

/* Cleanup */

	fclose (fd);
	return (0);
}
