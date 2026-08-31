/*----------------------------------------------------------------------
| Copyright 2020 Mersenne Research, Inc.  All rights reserved
|
| Auxiliary hash routines for proof generator and proof verifier
+---------------------------------------------------------------------*/

#include "memory.h"
#include "sha3.h"
#include "sha3.c"

typedef unsigned char hash256_t[32];		// A 256-bit hash value

int sha3_hash_and_gwnum (
	gwhandle *gwdata,
	hash256_t *prepend_hash,
	gwnum	x,
	hash256_t *result)
{
	int	residue_size, arraylen, i, len;
	uint32_t *array = NULL;
	sha3_ctx_t sha3;
	int	retcode = 0;

	// Init the sha3 structure.  Process the prepend hash.
	sha3_init (&sha3, 32);
	if (prepend_hash != NULL) sha3_update (&sha3, prepend_hash, 32);

	// Allocate an array for binary value
	residue_size = divide_rounding_up ((int) ceil (gwdata->bit_length), 8);
	arraylen = divide_rounding_up (residue_size, 4);
	array = (uint32_t *) malloc (divide_rounding_up (residue_size, 4) * sizeof(uint32_t));
	if (array == NULL) goto err;

	// Convert gwnum to binary
	len = gwtobinary (gwdata, x, array, arraylen);
	if (len < 0) goto err;

	// Convert from an array of uint32_t to an array of bytes (LSB to MSB).  Zero-pad if necessary.
	for (i = 0; i < len; i++) {
		uint32_t val = array[i];
		((unsigned char *)&array[i])[0] = val & 0xFF;
		((unsigned char *)&array[i])[1] = (val >> 8) & 0xFF;
		((unsigned char *)&array[i])[2] = (val >> 16) & 0xFF;
		((unsigned char *)&array[i])[3] = (val >> 24) & 0xFF;
	}
	for ( ; i < (int) arraylen; i++) array[i] = 0;

	// Finish generating 256-bit sha-3 hash
	sha3_update (&sha3, array, residue_size);
	sha3_final (result, &sha3);

	// Return success
	retcode = 1;

	// Free memory and return
err:	if (array != NULL) free (array);
	return (retcode);
}


// Return 256-bit sha3 hash of a gwnum converted to binary
int sha3_gwnum (
	gwhandle *gwdata,
	gwnum	x,
	hash256_t *result)
{
	return (sha3_hash_and_gwnum (gwdata, NULL, x, result));
}

// Build the proof root hash using the final residue
int roothash (
	gwhandle *gwdata,
	gwnum	x,
	hash256_t *result)
{
	return (sha3_hash_and_gwnum (gwdata, NULL, x, result));
}

// Build next hash from previous hash prepended to a gwnum residue
int hash (
	gwhandle *gwdata,
	hash256_t *prevh,
	gwnum	x,
	hash256_t *result)
{
	return (sha3_hash_and_gwnum (gwdata, prevh, x, result));
}

// Truncate hash to given number of bits (up to 64)
uint64_t truncate_hash (
	hash256_t h,			// 256-bit hash value
	int	hashlen)		// Length in bits (up to 64) for returned hash value
{
	int	i;
	uint64_t temp64 = 0;

	// Create a 64-bit hash from the first 8 bytes of the 256-bit hash
	for (i = 7; i >= 0; i--) temp64 = (temp64 << 8) + h[i];

	// Mask the value and return
	if (hashlen >= 64) return (temp64);
	return (temp64 & ((1ULL << hashlen) - 1));
}

// Convert hash to hex string
char *hash_to_string (
	hash256_t h)			// 256-bit hash value
{
	int	i;
static	char retbuf[65];		// returned character string

	for (i = 31; i >= 0; i--) sprintf (retbuf+(31-i)*2, "%02X", (int) h[i]);
	return (retbuf);
}
