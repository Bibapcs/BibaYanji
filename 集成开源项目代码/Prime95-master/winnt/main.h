
/* Function prototypes */

void GetIniSettings ();
void prime_main ();

#undef GetProfileInt
#undef GetProfileString
#undef WriteProfileString
#define GetProfileInt(k,v) GetPrivateProfileInt(INI_SECTION,k,v,INI_FILE)
#define WriteProfileInt(k,v) {char c[20]; sprintf (c, "%ld", v); WritePrivateProfileString(INI_SECTION,k,c,INI_FILE);}
#define GetProfileString(k,v,o) GetPrivateProfileString(INI_SECTION,k,v,o,256,INI_FILE)
#define WriteProfileString(k,v) WritePrivateProfileString(INI_SECTION,k,v,INI_FILE)
