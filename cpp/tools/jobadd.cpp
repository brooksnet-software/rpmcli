// jobadd - submit a job file to an RPM queue by name.
//
// Runs on the RPM server: RPM accepts job-add over RPC only from programs on
// that computer. RPM 7 reads only a copy of the file in a folder of its own in
// RPM's Incoming folder (as the RPM GUI makes when a file is dropped on a
// queue), or a file shared with the RPM service account. Any user may create a
// folder in Incoming, so jobadd copies the file there; the service moves the
// copy into its spool and removes the folder.
#include <cwctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <objbase.h>   // CoCreateGuid

#include "common.hpp"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

std::wstring widen(const std::string& text, UINT codepage) {
  if (text.empty()) return std::wstring();
  int n = MultiByteToWideChar(codepage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(n, L'\0');
  MultiByteToWideChar(codepage, 0, text.data(), static_cast<int>(text.size()), &wide[0], n);
  return wide;
}

std::string utf8(const std::wstring& text) {
  if (text.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n, nullptr, nullptr);
  return out;
}

std::wstring folderOf(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
  for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
    s.replace(pos, from.size(), to);
}

// RPM's settings folder on this computer, found as the RPM service finds it:
// the folder of the database named in config.xml beside RpmSrv.exe, else
// %ProgramData%\Brooks Internet Software\RPM.
std::wstring rpmDataDir() {
  wchar_t programData[MAX_PATH] = L"C:\\ProgramData";
  GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH);
  const std::wstring fallback = std::wstring(programData) + L"\\Brooks Internet Software\\RPM";

  wchar_t image[1024] = {};
  DWORD size = sizeof(image);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\rpm", L"ImagePath",
                   RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, image, &size) != ERROR_SUCCESS)
    return fallback;
  std::wstring exe = image;
  if (!exe.empty() && exe[0] == L'"') {
    exe = exe.substr(1, exe.find(L'"', 1) - 1);
  } else {
    std::wstring lower = exe;
    for (auto& ch : lower) ch = static_cast<wchar_t>(towlower(ch));
    const size_t dot = lower.find(L".exe");
    if (dot != std::wstring::npos) exe = exe.substr(0, dot + 4);
  }

  std::ifstream in(folderOf(exe) + L"\\config.xml", std::ios::binary);
  if (!in) return fallback;
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string xml = ss.str();
  const std::string tag = "<dbpath value=\"";
  size_t a = xml.find(tag);
  if (a == std::string::npos) return fallback;
  a += tag.size();
  const size_t b = xml.find('"', a);
  if (b == std::string::npos) return fallback;
  std::string db = xml.substr(a, b - a);
  replaceAll(db, "&quot;", "\"");
  replaceAll(db, "&apos;", "'");
  replaceAll(db, "&lt;", "<");
  replaceAll(db, "&gt;", ">");
  replaceAll(db, "&amp;", "&");
  // config.xml is UTF-8 when the GUI writes it, ANSI when older setups did
  const bool isUtf8 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, db.data(),
                                          static_cast<int>(db.size()), nullptr, 0) != 0;
  const std::wstring folder = folderOf(widen(db, isUtf8 ? CP_UTF8 : CP_ACP));
  return folder.empty() ? fallback : folder;
}

// Remove a staged copy the service did not take, and its folder.
void unstage(const std::wstring& staged) {
  DeleteFileW(staged.c_str());
  RemoveDirectoryW(folderOf(staged).c_str());
}

// Copy the job file into a new folder of its own in RPM's Incoming folder, or
// print why not and exit.
std::wstring stage(const std::wstring& original) {
  const std::wstring incoming = rpmDataDir() + L"\\Incoming";
  if (GetFileAttributesW(incoming.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::cout << "RPM's Incoming folder " << utf8(incoming)
              << " is missing. Restart the RPM service and try again.\n";
    std::exit(1);
  }
  GUID guid{};
  wchar_t guidText[64] = {};
  if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, guidText, _countof(guidText))) {
    std::cout << "Could not create a folder name.\n";
    std::exit(1);
  }
  const std::wstring folder = incoming + L"\\" + guidText;
  const size_t slash = original.find_last_of(L"\\/");
  const std::wstring staged = folder + L"\\" + original.substr(slash + 1);
  if (!CreateDirectoryW(folder.c_str(), nullptr) || !CopyFileW(original.c_str(), staged.c_str(), TRUE)) {
    const DWORD code = GetLastError();
    unstage(staged);
    std::cout << "Could not copy the job file into " << utf8(incoming) << " (error " << code << ").\n";
    std::exit(1);
  }
  return staged;
}

}  // namespace

int main(int argc, char** argv) {
  rpmtools::Connection conn;
  const auto args = rpmtools::parseArgs(argc, argv, conn);
  if (args.size() != 2) {
    std::cerr << "usage: jobadd [connection options] <queue> <job-file>\n" << rpmtools::connectionUsage();
    return 2;
  }
  const std::string queue = args[0];

  // argv is in the ANSI code page; RPM takes paths as UTF-8.
  const std::wstring job = widen(args[1], CP_ACP);
  wchar_t full[MAX_PATH * 2];
  const DWORD n = GetFullPathNameW(job.c_str(), _countof(full), full, nullptr);
  const DWORD attrs = n && n < _countof(full) ? GetFileAttributesW(full) : INVALID_FILE_ATTRIBUTES;
  if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    std::cout << "Job File Not Found.\n";
    return 1;
  }
  const std::wstring original = full;

  auto r = rpmtools::connectOrExit("rpmcli jobadd", conn);
  rpmtools::require(*r, conn, "job-add");
  const long long qid = rpmtools::queueId(*r, conn, queue);
  const std::wstring staged = stage(original);
  const rpm::Json result = r->call("job-add", {{"qid", rpm::Json(qid)},
                                               {"path", rpm::Json(utf8(staged))},
                                               {"original-path", rpm::Json(utf8(original))}});
  if (!result.at("success").asBool()) unstage(staged);
  return rpmtools::finish(result);
}
