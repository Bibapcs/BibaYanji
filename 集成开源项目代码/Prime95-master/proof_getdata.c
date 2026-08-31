/*---------------------------------------------------------------------------------
| Copyright 2020-2023 Mersenne Research, Inc.  All rights reserved
|
| This file contains routines to get proof initial residue from the Primenet server
+--------------------------------------------------------------------------------*/

/* This callback routine assembles the server's response to our proof_get_data request */

struct GetDataArg {
	void	*buf;
	int	bufsize;
	int	received;
	char	*MD5;		// Buffer for the MD5 hash of the residue - first 32 bytes of the result
};

size_t GetDataWriteMemoryCallback (
	void	*ptr,
	size_t	size,
	size_t	nmemb,
	void	*data)
{
	struct GetDataArg *info = (struct GetDataArg *) data;
	size_t realsize = size * nmemb;
	size_t bytes_to_copy = realsize;

/* Copy the first 32 bytes of the response to MD5 */

	if (info->received < 32) {
		size_t len = (info->received + bytes_to_copy > 32) ? 32 - info->received : bytes_to_copy;
		memcpy ((char *) info->MD5 + info->received, ptr, len);
		info->received += (int) len;
		ptr = (char *) ptr + len;
		bytes_to_copy -= (int) len;
	}

/* If there are more bytes to copy, fill the write buffer. */
/* Truncate response to fit in our buffer (even though should not be necessary) */

	if (bytes_to_copy) {
		size_t	buf_received = info->received - 32;
		if ((int) buf_received + (int) bytes_to_copy <= info->bufsize) {
			memcpy ((char *) info->buf + buf_received, ptr, bytes_to_copy);
		} else {
			memcpy ((char *) info->buf + buf_received, ptr, info->bufsize - buf_received);
		}
		info->received += (int) bytes_to_copy;
	}
	return realsize;
}

/* This routine gets the initial certification residue from the server. */
/* Much of it is a copy of primenet.c's pnHttpServerCURL routine */

int ProofGetData (char *aid, void *pbuf, int bufsize, char *md5)
{
	CURL	*curl;
	CURLcode res;
	int	try_proxy, debug;
	char	url[512], buf[600], errbuf[CURL_ERROR_SIZE];
	char	szProxyHost[PROXY_HOST_BUFSIZE];
	char	szProxyUser[PROXY_USER_BUFSIZE];
	char	szProxyPassword[PROXY_PASSWORD_BUFSIZE];
	unsigned short nProxyPort;
	float	bandwidth_rate_limit_flt = 0.0;
	uint64_t bandwidth_rate_limit;

/* Get optional download bandwidth rate limit */

	bandwidth_rate_limit_flt = IniSectionGetFloat (INI_FILE, SEC_PrimeNet, KEY_DownloadRateLimit, 0.0);	// Rate limit in Mbps
	if (bandwidth_rate_limit_flt < 0.0) bandwidth_rate_limit_flt = 0.0;
	if (bandwidth_rate_limit_flt > 10000.0) bandwidth_rate_limit_flt = 10000.0;	// Max out at 10Gbps
	bandwidth_rate_limit = (uint64_t) (bandwidth_rate_limit_flt * 131072.0);	// Convert from Mbps to bytes-per-second

/* Get debug logging flag */

	debug = IniSectionGetInt (INI_FILE, SEC_PrimeNet, KEY_Debug, 0);
 
/* Loop to try with proxy, then after a failure without proxy.  Ixfd64 requested this */
/* feature here:  https://www.mersenneforum.org/showpost.php?p=505557&postcount=415 */

	for (try_proxy = 1; ; try_proxy = 0) {

/* Init the cURL structures */

		curl = curl_easy_init ();
		if (curl == NULL) return (PRIMENET_ERROR_CURL_INIT);
		curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 1);

/* Give curl library the HTTP string to send */

		sprintf (url, "http://www.mersenne.org/proof_get_data/?aid=%s", aid);
		curl_easy_setopt (curl, CURLOPT_URL, url);
		curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, FALSE);
		if (debug) {
			sprintf (buf, "URL: %s\n", url);
			LogMsg (buf);
		}

		// Apply bandwidth limit
		if (bandwidth_rate_limit)
			curl_easy_setopt (curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t) bandwidth_rate_limit);

/* Get information about the optional proxy server */

		if (try_proxy) {
			getProxyInfo (szProxyHost, &nProxyPort, szProxyUser, szProxyPassword);
			if (szProxyHost[0]) {
				curl_easy_setopt (curl, CURLOPT_PROXY, szProxyHost);
				curl_easy_setopt (curl, CURLOPT_PROXYPORT, (long) nProxyPort);
//bug?				curl_easy_setopt (curl, CURLOPT_PROXYTYPE, ???);
				if (szProxyUser[0]) {
					sprintf (buf, "%s:%s", szProxyUser, szProxyPassword);
					curl_easy_setopt (curl, CURLOPT_PROXYUSERPWD, buf);
					curl_easy_setopt (curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
				}
			}
		}

/* Setup function to receive the response */

		struct GetDataArg info;
		info.received = 0;
		info.buf = pbuf;
		info.bufsize = bufsize;
		info.MD5 = md5;
		memset (md5, 0, 33);
		curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, GetDataWriteMemoryCallback);
		curl_easy_setopt (curl, CURLOPT_WRITEDATA, (void *) &info);

/* Output verbose debug information */

		if (debug >= 2) {
			curl_easy_setopt (curl, CURLOPT_DEBUGFUNCTION, curl_trace);
			curl_easy_setopt (curl, CURLOPT_DEBUGDATA, NULL);
			/* the DEBUGFUNCTION has no effect until we enable VERBOSE */
			curl_easy_setopt (curl, CURLOPT_VERBOSE, 1);
		}

/* Send the URL request */

		curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);
		curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1);
		curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 180);
		curl_easy_setopt (curl, CURLOPT_TIMEOUT, 180);
		curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, errbuf);
		res = curl_easy_perform (curl);
		if (res != CURLE_OK) {
			sprintf (buf, "CURL library error: %s\n", errbuf);
			LogMsg (buf);
			OutputStr (COMM_THREAD_NUM, buf);
			curl_easy_cleanup (curl);
			// By default, try again without using a proxy server
			if (try_proxy && szProxyHost[0] && IniSectionGetInt (INI_FILE, SEC_PrimeNet, "TryNoProxyAfterProxyFailure", 1))
				continue;
			return (PRIMENET_ERROR_CURL_PERFORM);
		}

/* Success, break out of loop */

		break;
	}

/* Cleanup */

	curl_easy_cleanup (curl);

/* Return success */

	return (PRIMENET_NO_ERROR);
}

