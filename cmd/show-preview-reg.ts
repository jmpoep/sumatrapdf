import { spawnSync } from "node:child_process";

// Must match src/RegistryPreview.h and src/RegistryPreview.cpp
const kThumbnailProviderClsid = "{e357fccd-a995-4576-b01f-234630154e96}";
const kPreviewHandlerClsid = "{8895b1c6-b41f-4c1c-a562-0d564250836f}";

const kPreviewClsids: Record<string, string> = {
  PDF: "{F0FE6374-D0B4-4751-AE36-C57B96999E87}",
  XPS: "{B055DBB8-B29D-4E86-8E69-C649CE044B35}",
  DjVu: "{DB0BCEC8-57CE-4D21-97B8-E1DE9B8510BF}",
  EPUB: "{C744BA15-7166-483E-9B2F-80F93F62C7FF}",
  FB2: "{58F5CCAA-36A9-413A-81BC-9F899AD3271B}",
  MOBI: "{C21FF5DF-9AD7-43D8-A979-608C77CAC4AA}",
  CBX: "{886AD8B3-550D-4710-81B7-D5D422313B65}",
  TGA: "{A81391FC-C68F-4292-9ACC-F11F9484E95C}",
};

const kThumbClsids: Record<string, string> = {
  PDF: "{939AD615-AF47-4BE9-AFBC-497D87F6E3F5}",
  XPS: "{27DD6B96-3304-4F8B-BD11-BF5D2F287841}",
  DjVu: "{8289C669-9D43-48B1-998F-6F436822E433}",
  EPUB: "{E85A8AB9-DCED-414F-A273-7A9A36F0396F}",
  FB2: "{828BB6D4-D5A9-4D3C-8B2B-771501ABBC92}",
  MOBI: "{5147A526-BA31-44E7-BC06-53CC4A0034BF}",
  CBX: "{B268E7B6-F8A9-4181-B133-943061C83CA7}",
  TGA: "{8F12A606-9623-4692-A7A7-58CB0111D371}",
};

const previewers = [
  { name: "PDF", previewClsid: kPreviewClsids.PDF, thumbClsid: kThumbClsids.PDF, exts: [".pdf"] },
  { name: "CBX", previewClsid: kPreviewClsids.CBX, thumbClsid: kThumbClsids.CBX, exts: [".cbz", ".cbr", ".cb7", ".cbt"] },
  { name: "TGA", previewClsid: kPreviewClsids.TGA, thumbClsid: kThumbClsids.TGA, exts: [".tga"] },
  { name: "DjVu", previewClsid: kPreviewClsids.DjVu, thumbClsid: kThumbClsids.DjVu, exts: [".djvu"] },
  { name: "XPS", previewClsid: kPreviewClsids.XPS, thumbClsid: kThumbClsids.XPS, exts: [".xps", ".oxps"] },
  { name: "EPUB", previewClsid: kPreviewClsids.EPUB, thumbClsid: kThumbClsids.EPUB, exts: [".epub"] },
  { name: "FB2", previewClsid: kPreviewClsids.FB2, thumbClsid: kThumbClsids.FB2, exts: [".fb2", ".fb2z"] },
  { name: "MOBI", previewClsid: kPreviewClsids.MOBI, thumbClsid: kThumbClsids.MOBI, exts: [".mobi"] },
];

function regQuery(key: string, valueName?: string): string | null {
  const args = ["query", key];
  if (valueName !== undefined) {
    args.push("/v", valueName);
  } else {
    args.push("/ve"); // default value
  }
  const result = spawnSync("reg", args, { encoding: "utf-8", stdio: ["pipe", "pipe", "pipe"] });
  if (result.status !== 0) {
    return null;
  }
  // parse output: look for REG_SZ line
  const lines = result.stdout.split("\n");
  for (const line of lines) {
    const match = line.match(/REG_SZ\s+(.+)/);
    if (match) {
      return match[1].trim();
    }
  }
  return null;
}

function checkExt(ext: string, expectedPreviewClsid: string, expectedThumbClsid: string) {
  const roots = [
    { name: "HKCR", key: `HKEY_CLASSES_ROOT\\${ext}` },
    { name: "HKCU", key: `HKEY_CURRENT_USER\\Software\\Classes\\${ext}` },
    { name: "HKLM", key: `HKEY_LOCAL_MACHINE\\Software\\Classes\\${ext}` },
  ];

  // Check IThumbnailProvider
  console.log(`  IThumbnailProvider (${kThumbnailProviderClsid}):`);
  for (const root of roots) {
    const key = `${root.key}\\shellex\\${kThumbnailProviderClsid}`;
    const value = regQuery(key);
    if (value) {
      const match = value.toLowerCase() === expectedThumbClsid.toLowerCase();
      const status = match ? "OK" : `MISMATCH (expected ${expectedThumbClsid})`;
      console.log(`    ${root.name}: ${value} - ${status}`);
    } else {
      console.log(`    ${root.name}: (not set)`);
    }
  }

  // Check IPreviewHandler
  console.log(`  IPreviewHandler (${kPreviewHandlerClsid}):`);
  for (const root of roots) {
    const key = `${root.key}\\shellex\\${kPreviewHandlerClsid}`;
    const value = regQuery(key);
    if (value) {
      const match = value.toLowerCase() === expectedPreviewClsid.toLowerCase();
      const status = match ? "OK" : `MISMATCH (expected ${expectedPreviewClsid})`;
      console.log(`    ${root.name}: ${value} - ${status}`);
    } else {
      console.log(`    ${root.name}: (not set)`);
    }
  }
}

function checkClsidRegistration(name: string, clsid: string) {
  const roots = [
    { name: "HKCU", key: `HKEY_CURRENT_USER\\Software\\Classes\\CLSID\\${clsid}` },
    { name: "HKLM", key: `HKEY_LOCAL_MACHINE\\Software\\Classes\\CLSID\\${clsid}` },
  ];

  for (const root of roots) {
    const displayName = regQuery(root.key);
    const dllPath = regQuery(`${root.key}\\InprocServer32`);
    const appId = regQuery(root.key, "AppId");
    if (displayName || dllPath) {
      console.log(`  CLSID ${root.name}: ${displayName || "(no display name)"}`);
      if (dllPath) {
        console.log(`    InprocServer32: ${dllPath}`);
      }
      if (appId) {
        console.log(`    AppId: ${appId}`);
      }
    } else {
      console.log(`  CLSID ${root.name}: (not registered)`);
    }
  }
}

function checkPreviewHandlersKey(clsid: string, name: string) {
  const roots = [
    { name: "HKCU", key: "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers" },
    { name: "HKLM", key: "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers" },
  ];
  for (const root of roots) {
    const value = regQuery(root.key, clsid);
    if (value) {
      console.log(`  PreviewHandlers ${root.name}: ${value}`);
    }
  }
}

function main() {
  console.log("SumatraPDF Preview/Thumbnail Registration Status\n");

  for (const prev of previewers) {
    console.log(`=== ${prev.name} ===`);
    console.log(`  Preview CLSID: ${prev.previewClsid}`);
    checkClsidRegistration(prev.name + " Preview", prev.previewClsid);
    checkPreviewHandlersKey(prev.previewClsid, prev.name);
    console.log(`  Thumb CLSID: ${prev.thumbClsid}`);
    checkClsidRegistration(prev.name + " Thumb", prev.thumbClsid);
    for (const ext of prev.exts) {
      console.log(`  --- ${ext} ---`);
      checkExt(ext, prev.previewClsid, prev.thumbClsid);
    }
    console.log("");
  }
}

main();
