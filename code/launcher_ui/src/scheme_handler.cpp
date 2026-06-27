#include "scheme_handler.h"

#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace HogwartsMP::LauncherUI {
    namespace {
        CefRefPtr<CefResourceHandler> NotFound() {
            static const char kBody[] = "Not Found";
            return new CefStreamResourceHandler(404, "Not Found", "text/plain", {}, CefStreamReader::CreateForData(const_cast<char *>(kBody), sizeof(kBody) - 1));
        }

        // A minimal page baked into the exe — shown only on the worst case (remote
        // unreachable AND nothing ever cached). The window is frameless, so it carries its
        // own drag strip + close button (via the same window.cefQuery bridge the real UI
        // uses); otherwise it could only be closed from the taskbar.
        CefRefPtr<CefResourceHandler> OfflinePlaceholder() {
            static const char kBody[] = R"HTML(<!doctype html><meta charset=utf-8><title>HogwartsMP</title>
<style>
  html,body{margin:0;height:100%}
  body{background:#15110c;color:#e8dcc0;font:16px system-ui;display:grid;place-items:center;text-align:center}
  #bar{position:fixed;top:0;left:0;right:0;height:36px;-webkit-app-region:drag}
  #x{position:fixed;top:0;right:0;width:46px;height:36px;-webkit-app-region:no-drag;
     border:0;background:transparent;color:#9c8a63;font:18px system-ui;cursor:pointer}
  #x:hover{background:rgba(165,75,65,.55);color:#fff}
  h1{color:#c9a25a;font-weight:600}
</style>
<div id=bar></div><button id=x title=Close>&#10005;</button>
<div><h1>Launcher offline</h1>
<p>Couldn't reach the UI and nothing is cached yet.<br>Check your connection and relaunch.</p></div>
<script>
  document.getElementById('x').onclick=function(){
    if(window.cefQuery){window.cefQuery({request:JSON.stringify({action:'window',op:'close'}),onSuccess:function(){},onFailure:function(){}});}
  };
</script>)HTML";
            return new CefStreamResourceHandler(200, "OK", "text/html", {}, CefStreamReader::CreateForData(const_cast<char *>(kBody), sizeof(kBody) - 1));
        }

        enum class FetchResult { Ok, HttpError, NetworkError };

        // GET `url` into `outPath` (HTTPS, follows GitHub's 302). HttpError = reached the
        // server but it answered non-200 (don't go offline — remote is up). NetworkError =
        // couldn't reach it at all (resolve/connect/send) — caller latches offline so the
        // rest of the page serves straight from cache without per-asset stalls.
        FetchResult FetchToFile(const std::wstring &url, const std::filesystem::path &outPath) {
            URL_COMPONENTS uc{};
            wchar_t host[256]   = {};
            wchar_t path[2048]  = {};
            uc.dwStructSize     = sizeof(uc);
            uc.lpszHostName     = host;
            uc.dwHostNameLength = ARRAYSIZE(host);
            uc.lpszUrlPath      = path;
            uc.dwUrlPathLength  = ARRAYSIZE(path);
            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
                return FetchResult::NetworkError;
            }

            HINTERNET hSession = WinHttpOpen(L"HogwartsMPLauncher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession) {
                return FetchResult::NetworkError;
            }
            WinHttpSetTimeouts(hSession, /*resolve*/ 3000, /*connect*/ 3000, /*send*/ 5000, /*receive*/ 8000);

            FetchResult result = FetchResult::NetworkError;
            HINTERNET hConnect  = WinHttpConnect(hSession, host, uc.nPort, 0);
            if (hConnect) {
                const DWORD secure = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
                HINTERNET hReq     = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
                if (hReq) {
                    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(hReq, nullptr)) {
                        DWORD status = 0;
                        DWORD len    = sizeof(status);
                        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
                        if (status == 200) {
                            std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                            if (out) {
                                result = FetchResult::Ok;
                                std::vector<char> buf(64 * 1024);
                                for (;;) {
                                    DWORD read = 0;
                                    if (!WinHttpReadData(hReq, buf.data(), static_cast<DWORD>(buf.size()), &read)) {
                                        result = FetchResult::NetworkError;
                                        break;
                                    }
                                    if (read == 0) {
                                        break;
                                    }
                                    out.write(buf.data(), static_cast<std::streamsize>(read));
                                    if (!out) {
                                        result = FetchResult::NetworkError;
                                        break;
                                    }
                                }
                            }
                        }
                        else {
                            result = FetchResult::HttpError; // server up, resource missing/blocked
                        }
                    }
                    WinHttpCloseHandle(hReq);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
            return result;
        }

        // URL path (decoded) -> the cache/remote-relative path. "/" maps to index.html;
        // leading slashes stripped so it joins cleanly under both the cache dir and base URL.
        std::string RelFromUrlPath(std::string urlPath) {
            while (!urlPath.empty() && (urlPath.front() == '/' || urlPath.front() == '\\')) {
                urlPath.erase(0, 1);
            }
            if (urlPath.empty()) {
                urlPath = "index.html";
            }
            return urlPath;
        }

        // Resolve `rel` under `root`, rejecting any path that escapes the root (empty on
        // rejection). Shared by the cache write and the cache read so they agree.
        std::filesystem::path ResolveUnderRoot(const std::filesystem::path &root, const std::string &rel) {
            std::error_code ec;
            const std::filesystem::path canonRoot = std::filesystem::weakly_canonical(root, ec);
            if (ec) {
                return {};
            }
            const std::filesystem::path candidate = std::filesystem::weakly_canonical(canonRoot / std::filesystem::u8path(rel), ec);
            if (ec) {
                return {};
            }

            // Reject anything not strictly under the root. The boundary char check stops a
            // bare-prefix escape (root\cache vs a sibling root\cache_evil whose string also
            // starts with the root); equal length = the root itself, which serves as no file.
            const auto &rootStr = canonRoot.native();
            const auto &candStr = candidate.native();
            if (candStr.size() <= rootStr.size() || candStr.compare(0, rootStr.size(), rootStr) != 0 ||
                candStr[rootStr.size()] != std::filesystem::path::preferred_separator) {
                return {}; // escaped the root (or is the root)
            }
            return candidate;
        }

        CefRefPtr<CefResourceHandler> ServeFromCache(const std::filesystem::path &cacheFile) {
            CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(cacheFile.wstring());
            if (!stream) {
                return nullptr;
            }

            std::string ext = cacheFile.extension().string();
            if (!ext.empty() && ext.front() == '.') {
                ext.erase(0, 1);
            }
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            std::string mime = CefGetMimeType(ext).ToString();
            if (mime.empty()) {
                mime = "application/octet-stream";
            }

            CefResponse::HeaderMap headers;
            headers.emplace("Cache-Control", "no-store");
            return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
        }

        // hmp://app/<path> -> mirror remoteBase/<rel> into cacheDir/<rel>, then serve the
        // cached file. Online: refresh then serve fresh. Offline: serve last-good cache.
        class SchemeHandlerFactory final : public CefSchemeHandlerFactory {
          public:
            SchemeHandlerFactory(std::string remoteBase, std::filesystem::path cacheDir, std::filesystem::path localDir)
                : _remoteBase(std::move(remoteBase)), _cacheDir(std::move(cacheDir)), _localDir(std::move(localDir)) {
                if (!_remoteBase.empty() && _remoteBase.back() != '/') {
                    _remoteBase.push_back('/');
                }
            }

            CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString &, CefRefPtr<CefRequest> request) override {
                CefURLParts urlParts;
                if (!request || !CefParseURL(request->GetURL(), urlParts)) {
                    return NotFound();
                }
                const std::string urlPath = CefURIDecode(CefString(&urlParts.path), false, UU_SPACES).ToString();
                const std::string rel     = RelFromUrlPath(urlPath);

                // local/ -> shipped assets next to the exe (fonts, etc.). Always local;
                // never touched by the remote mirror so they survive any outage.
                constexpr const char kLocalPrefix[] = "local/";
                if (rel.compare(0, sizeof(kLocalPrefix) - 1, kLocalPrefix) == 0) {
                    const std::filesystem::path localFile = ResolveUnderRoot(_localDir, rel.substr(sizeof(kLocalPrefix) - 1));
                    if (!localFile.empty()) {
                        if (CefRefPtr<CefResourceHandler> handler = ServeFromCache(localFile)) {
                            return handler;
                        }
                    }
                    return NotFound();
                }

                const std::filesystem::path cacheFile = ResolveUnderRoot(_cacheDir, rel);
                if (cacheFile.empty()) {
                    return NotFound();
                }

                RefreshFromRemote(rel, cacheFile);

                if (std::filesystem::exists(cacheFile)) {
                    if (CefRefPtr<CefResourceHandler> handler = ServeFromCache(cacheFile)) {
                        return handler;
                    }
                }

                // Nothing cached and the remote was unreachable: show the placeholder for
                // the document, 404 for sub-resources.
                if (rel == "index.html") {
                    return OfflinePlaceholder();
                }
                return NotFound();
            }

          private:
            // Best-effort refresh of one cache file from the remote. Atomic (tmp + rename)
            // so a partial download never replaces a good cached copy.
            void RefreshFromRemote(const std::string &rel, const std::filesystem::path &cacheFile) {
                if (_offline.load(std::memory_order_relaxed) || _remoteBase.empty()) {
                    return;
                }

                std::error_code ec;
                std::filesystem::create_directories(cacheFile.parent_path(), ec);
                std::filesystem::path tmp = cacheFile;
                tmp += L".tmp";

                const std::wstring url = CefString(_remoteBase + rel).ToWString();
                const FetchResult r    = FetchToFile(url, tmp);
                if (r == FetchResult::Ok) {
                    std::filesystem::rename(tmp, cacheFile, ec);
                }
                else {
                    std::filesystem::remove(tmp, ec);
                    if (r == FetchResult::NetworkError) {
                        _offline.store(true, std::memory_order_relaxed); // serve the rest from cache, no stalls
                        OutputDebugStringA("[launcher] remote unreachable — serving UI from cache\n");
                    }
                }
            }

            std::string _remoteBase;
            std::filesystem::path _cacheDir;
            std::filesystem::path _localDir;
            std::atomic<bool> _offline {false};
            IMPLEMENT_REFCOUNTING(SchemeHandlerFactory);
        };
    } // namespace

    void RegisterLauncherSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        registrar->AddCustomScheme(kAppScheme, CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE | CEF_SCHEME_OPTION_CORS_ENABLED | CEF_SCHEME_OPTION_FETCH_ENABLED);
    }

    void RegisterLauncherSchemeHandlerFactory(const std::string &remoteBase, const std::filesystem::path &cacheDir, const std::filesystem::path &localDir) {
        CefRegisterSchemeHandlerFactory(kAppScheme, kAppHost, new SchemeHandlerFactory(remoteBase, cacheDir, localDir));
    }
} // namespace HogwartsMP::LauncherUI
