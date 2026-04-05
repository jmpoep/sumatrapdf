/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// Filter test: loads PdfFilter2.dll, creates an IFilter for a file,
// and prints the extracted text chunks.
// Activated with -test-filter <filename.ext>
// Only available in debug builds.

#include "utils/BaseUtil.h"
#include "utils/CmdLineArgsIter.h"
#include "utils/WinUtil.h"

#include "RegistrySearchFilter.h"

#include "utils/Log.h"

#include <ObjBase.h>
#include <Shlwapi.h>
#include <filter.h>
#include <filterr.h>

typedef HRESULT DllGetClassObjectFn(REFCLSID rclsid, REFIID riid, void** ppv);

constexpr const char* kPdfFilterDllName = "PdfFilter2.dll";

void TestFilter(const WCHAR* cmdLine) {
    StrVec argList;
    ParseCmdLine(cmdLine, argList);

    // find args after -test-filter
    int idx = -1;
    for (int i = 0; i < argList.Size(); i++) {
        if (str::EqI(argList.At(i), "-test-filter")) {
            idx = i;
            break;
        }
    }

    if (idx < 0 || idx + 1 >= argList.Size()) {
        printf("Usage: SumatraPDF.exe -test-filter <filename>\n");
        return;
    }

    const char* filePathA = argList.At(idx + 1);
    TempWStr filePath = ToWStrTemp(filePathA);

    GUID clsid{};
    TempWStr clsidW = ToWStrTemp(kPdfFilter2Clsid);
    IIDFromString(clsidW, &clsid);

    HMODULE dll = LoadLibraryA(kPdfFilterDllName);
    if (!dll) {
        printf("Can't load %s\n", kPdfFilterDllName);
        return;
    }

    auto fn = (DllGetClassObjectFn*)GetProcAddress(dll, "DllGetClassObject");
    if (!fn) {
        printf("Can't find DllGetClassObject in %s\n", kPdfFilterDllName);
        FreeLibrary(dll);
        return;
    }

    IClassFactory* pFactory = nullptr;
    HRESULT hr = fn(clsid, IID_IClassFactory, (void**)&pFactory);
    if (hr != S_OK || !pFactory) {
        printf("DllGetClassObject failed: 0x%08x\n", (unsigned)hr);
        FreeLibrary(dll);
        return;
    }

    IFilter* pFilter = nullptr;
    hr = pFactory->CreateInstance(nullptr, IID_IFilter, (void**)&pFilter);
    pFactory->Release();
    if (hr != S_OK || !pFilter) {
        printf("CreateInstance(IFilter) failed: 0x%08x\n", (unsigned)hr);
        FreeLibrary(dll);
        return;
    }

    // try IInitializeWithStream first
    IInitializeWithStream* pInitStream = nullptr;
    hr = pFilter->QueryInterface(IID_IInitializeWithStream, (void**)&pInitStream);
    if (hr == S_OK && pInitStream) {
        IStream* pStream = nullptr;
        hr = SHCreateStreamOnFileEx(filePath, STGM_READ, 0, FALSE, nullptr, &pStream);
        if (hr != S_OK || !pStream) {
            printf("Can't open file: %s\n", filePathA);
            pInitStream->Release();
            pFilter->Release();
            FreeLibrary(dll);
            return;
        }
        hr = pInitStream->Initialize(pStream, 0);
        pStream->Release();
        pInitStream->Release();
        if (hr != S_OK) {
            printf("IInitializeWithStream::Initialize() failed: 0x%08x\n", (unsigned)hr);
            pFilter->Release();
            FreeLibrary(dll);
            return;
        }
    } else {
        // fall back to IPersistFile
        IPersistFile* pPersistFile = nullptr;
        hr = pFilter->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
        if (hr != S_OK || !pPersistFile) {
            printf("Filter supports neither IInitializeWithStream nor IPersistFile\n");
            pFilter->Release();
            FreeLibrary(dll);
            return;
        }
        hr = pPersistFile->Load(filePath, 0);
        pPersistFile->Release();
        if (hr != S_OK) {
            printf("IPersistFile::Load() failed: 0x%08x\n", (unsigned)hr);
            pFilter->Release();
            FreeLibrary(dll);
            return;
        }
    }

    ULONG flags2 = 0;
    hr = pFilter->Init(0, 0, nullptr, &flags2);
    if (hr != S_OK) {
        printf("IFilter::Init() failed: 0x%08x\n", (unsigned)hr);
        pFilter->Release();
        FreeLibrary(dll);
        return;
    }

    printf("Extracting text from: %s\n\n", filePathA);

    int chunkCount = 0;
    STAT_CHUNK stat{};
    while (true) {
        hr = pFilter->GetChunk(&stat);
        if (hr == FILTER_E_END_OF_CHUNKS) {
            break;
        }
        if (hr != S_OK) {
            printf("\nGetChunk() failed: 0x%08x\n", (unsigned)hr);
            break;
        }
        chunkCount++;

        if (stat.flags == CHUNK_TEXT) {
            WCHAR buf[4096];
            while (true) {
                ULONG bufSize = dimof(buf);
                hr = pFilter->GetText(&bufSize, buf);
                if (hr == FILTER_E_NO_MORE_TEXT || hr == FILTER_S_LAST_TEXT) {
                    if (bufSize > 0) {
                        buf[bufSize] = 0;
                        TempStr text = ToUtf8Temp(buf);
                        printf("%s", text);
                    }
                    break;
                }
                if (hr != S_OK) {
                    break;
                }
                buf[bufSize] = 0;
                TempStr text = ToUtf8Temp(buf);
                printf("%s", text);
            }
        } else if (stat.flags == CHUNK_VALUE) {
            PROPVARIANT* pVal = nullptr;
            hr = pFilter->GetValue(&pVal);
            if (hr == S_OK && pVal) {
                if (pVal->vt == VT_LPWSTR && pVal->pwszVal) {
                    TempStr text = ToUtf8Temp(pVal->pwszVal);
                    printf("[value] %s\n", text);
                } else {
                    printf("[value] (type=%d)\n", (int)pVal->vt);
                }
                PropVariantClear(pVal);
                CoTaskMemFree(pVal);
            }
        }
    }

    printf("\n\nExtracted %d chunks\n", chunkCount);

    pFilter->Release();
    FreeLibrary(dll);
}
