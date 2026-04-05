/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#define kPdfFilterClsid "{55808EA8-81FE-43c6-AAE8-1D8149F941D3}"
#define kPdfFilterHandler "{26CA6565-F22A-4f5e-B688-0AD051D56E96}"

#define kTexFilterClsid "{AF57F784-ED93-4f2c-8C1D-CCDCB6E27CA6}"
#define kTexFilterHandler "{3FAB27F8-08EC-4b9e-9EEE-181A6E846B8D}"

#define kEpubFilterClsid "{FE4C7847-4260-43e3-A449-08ED76009F94}"
#define kEpubFilterHandler "{FF68D1A0-DA54-4fbf-A406-06CFDB764CA9}"

#define kPdfFilter2Clsid "{7D90CD1B-C715-481A-A161-556F14357D34}"
#define kPdfFilter2Handler "{656FFB1C-6682-4159-8016-008B3DCE726D}"

#define kTexFilter2Clsid "{374F3C10-52C2-4956-BA0C-0552F36A5441}"
#define kTexFilter2Handler "{35FC6811-8F1A-4666-81BD-30F3FDB4AB95}"

#define kEpubFilter2Clsid "{662DA36A-2CD3-472B-AA23-34E4F097BDE1}"
#define kEpubFilter2Handler "{46F3CCFE-3E33-4B68-B927-6839A1A13C72}"

bool InstallSearchFilter(const char* dllPath, bool allUsers);
bool UninstallSearchFilter();
bool UninstallSearchFilter2();
bool IsSearchFilterInstalled();