/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#define kPdfPreviewClsid "{3D3B1846-CC43-42AE-BFF9-D914083C2BA3}"
#define kXpsPreviewClsid "{D427A82C-6545-4FBE-8E87-030EDB3BE46D}"
#define kDjVuPreviewClsid "{6689D0D4-1E9C-400A-8BCA-FA6C56B2C3B5}"
#define kEpubPreviewClsid "{80C4E4B1-2B0F-40D5-95AF-BE7B57FEA4F9}"
#define kFb2PreviewClsid "{D5878036-E863-403E-A62C-7B9C7453336A}"
#define kMobiPreviewClsid "{42CA907E-BDF5-4A75-994A-E1AEC8A10954}"
#define kCbxPreviewClsid "{C29D3E2B-8FF6-4033-A4E8-54221D859D74}"
#define kTgaPreviewClsid "{CB1D63A6-FE5E-4DED-BEA5-3F6AF1A70D08}"

#define kPdfPreview2Clsid "{F0FE6374-D0B4-4751-AE36-C57B96999E87}"
#define kXpsPreview2Clsid "{B055DBB8-B29D-4E86-8E69-C649CE044B35}"
#define kDjVuPreview2Clsid "{DB0BCEC8-57CE-4D21-97B8-E1DE9B8510BF}"
#define kEpubPreview2Clsid "{C744BA15-7166-483E-9B2F-80F93F62C7FF}"
#define kFb2Preview2Clsid "{58F5CCAA-36A9-413A-81BC-9F899AD3271B}"
#define kMobiPreview2Clsid "{C21FF5DF-9AD7-43D8-A979-608C77CAC4AA}"
#define kCbxPreview2Clsid "{886AD8B3-550D-4710-81B7-D5D422313B65}"
#define kTgaPreview2Clsid "{A81391FC-C68F-4292-9ACC-F11F9484E95C}"

bool InstallPreviewDll(const char* dllPath, bool allUsers);
bool UninstallPreviewDll();
bool UninstallPreviewDll2();
void DisablePreviewInstallExts(const char* cmdLine);
bool IsPreviewInstalled();
