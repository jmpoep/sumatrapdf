/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/WinUtil.h"

#include "RegistrySearchFilter.h"

#include "utils/Log.h"

bool InstallSearchFilter(const char* dllPath, bool allUsers) {
    struct {
        const char *key, *value, *data;
    } regVals[] = {
        {"Software\\Classes\\CLSID\\" kPdfFilter2Clsid, nullptr, "SumatraPDF IFilter"},
        {"Software\\Classes\\CLSID\\" kPdfFilter2Clsid "\\InProcServer32", nullptr, dllPath},
        {"Software\\Classes\\CLSID\\" kPdfFilter2Clsid "\\InProcServer32", "ThreadingModel", "Both"},
        {"Software\\Classes\\CLSID\\" kPdfFilter2Handler, nullptr, "SumatraPDF IFilter Persistent Handler"},
        {"Software\\Classes\\CLSID\\" kPdfFilter2Handler "\\PersistentAddinsRegistered", nullptr, ""},
        {"Software\\Classes\\CLSID"
         "\\" kPdfFilter2Handler "\\PersistentAddinsRegistered\\{89BCB740-6119-101A-BCB7-00DD010655AF}",
         nullptr, kPdfFilter2Clsid},
        {"Software\\Classes\\.pdf\\PersistentHandler", nullptr, kPdfFilter2Handler},
        {"Software\\Classes\\CLSID\\" kTexFilter2Clsid, nullptr, "SumatraPDF IFilter"},
        {"Software\\Classes\\CLSID\\" kTexFilter2Clsid "\\InProcServer32", nullptr, dllPath},
        {"Software\\Classes\\CLSID\\" kTexFilter2Clsid "\\InProcServer32", "ThreadingModel", "Both"},
        {"Software\\Classes\\CLSID\\" kTexFilter2Handler, nullptr, "SumatraPDF LaTeX IFilter Persistent Handler"},
        {"Software\\Classes\\CLSID\\" kTexFilter2Handler "\\PersistentAddinsRegistered", nullptr, ""},
        {"Software\\Classes\\CLSID"
         "\\" kTexFilter2Handler "\\PersistentAddinsRegistered\\{89BCB740-6119-101A-BCB7-00DD010655AF}",
         nullptr, kTexFilter2Clsid},
        {"Software\\Classes\\.tex\\PersistentHandler", nullptr, kTexFilter2Handler},
        {"Software\\Classes\\CLSID\\" kEpubFilter2Clsid, nullptr, "SumatraPDF IFilter"},
        {"Software\\Classes\\CLSID\\" kEpubFilter2Clsid "\\InProcServer32", nullptr, dllPath},
        {"Software\\Classes\\CLSID\\" kEpubFilter2Clsid "\\InProcServer32", "ThreadingModel", "Both"},
        {"Software\\Classes\\CLSID\\" kEpubFilter2Handler, nullptr, "SumatraPDF EPUB IFilter Persistent Handler"},
        {"Software\\Classes\\CLSID\\" kEpubFilter2Handler "\\PersistentAddinsRegistered", nullptr, ""},
        {"Software\\Classes\\CLSID"
         "\\" kEpubFilter2Handler "\\PersistentAddinsRegistered\\{89BCB740-6119-101A-BCB7-00DD010655AF}",
         nullptr, kEpubFilter2Clsid},
        {"Software\\Classes\\.epub\\PersistentHandler", nullptr, kEpubFilter2Handler},
    };
    HKEY hkey = allUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    for (int i = 0; i < dimof(regVals); i++) {
        auto key = regVals[i].key;
        auto val = regVals[i].value;
        auto data = regVals[i].data;
        bool ok = LoggedWriteRegStr(hkey, key, val, data);
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool UninstallSearchFilterKeys(const char* pdfClsid, const char* pdfHandler, const char* texClsid,
                                      const char* texHandler, const char* epubClsid, const char* epubHandler) {
    const char* regKeys[] = {
        pdfClsid, pdfHandler, texClsid, texHandler, epubClsid, epubHandler,
    };
    bool ok = true;
    for (int i = 0; i < dimof(regKeys); i++) {
        TempStr key = str::FormatTemp("Software\\Classes\\CLSID\\%s", regKeys[i]);
        LoggedDeleteRegKey(HKEY_LOCAL_MACHINE, key);
        ok &= LoggedDeleteRegKey(HKEY_CURRENT_USER, key);
    }
    const char* extKeys[] = {
        "Software\\Classes\\.pdf\\PersistentHandler",
        "Software\\Classes\\.tex\\PersistentHandler",
        "Software\\Classes\\.epub\\PersistentHandler",
    };
    for (int i = 0; i < dimof(extKeys); i++) {
        LoggedDeleteRegKey(HKEY_LOCAL_MACHINE, extKeys[i]);
        ok &= LoggedDeleteRegKey(HKEY_CURRENT_USER, extKeys[i]);
    }
    return ok;
}

// Note: for compat with pre-3.4 removes HKLM and HKCU keys
bool UninstallSearchFilter2() {
    return UninstallSearchFilterKeys(kPdfFilter2Clsid, kPdfFilter2Handler, kTexFilter2Clsid, kTexFilter2Handler,
                                     kEpubFilter2Clsid, kEpubFilter2Handler);
}

bool UninstallSearchFilter() {
    return UninstallSearchFilterKeys(kPdfFilterClsid, kPdfFilterHandler, kTexFilterClsid, kTexFilterHandler,
                                     kEpubFilterClsid, kEpubFilterHandler);
}

bool IsSearchFilterInstalled() {
    const char* key = ".pdf\\PersistentHandler";
    char* iid = LoggedReadRegStrTemp(HKEY_CLASSES_ROOT, key, nullptr);
    bool isInstalled = str::EqI(iid, kPdfFilter2Handler) || str::EqI(iid, kPdfFilterHandler);
    logf("IsSearchFilterInstalled() isInstalled=%d\n", (int)isInstalled);
    return isInstalled;
}
