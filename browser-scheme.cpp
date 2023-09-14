#include "browser-scheme.hpp"
#include "wide-string.hpp"
#include <include/wrapper/cef_stream_resource_handler.h>

#if !ENABLE_LOCAL_FILE_URL_SCHEME
CefRefPtr<CefResourceHandler>
BrowserSchemeHandlerFactory::Create(CefRefPtr<CefBrowser> browser,
				    CefRefPtr<CefFrame>, const CefString &,
				    CefRefPtr<CefRequest> request)
{
	if (!browser || !request)
		return nullptr;

	CefURLParts parts;
	CefParseURL(request->GetURL(), parts);

	std::string path = CefString(&parts.path);

	path = CefURIDecode(path, true, cef_uri_unescape_rule_t::UU_SPACES);
	path = CefURIDecode(
		path, true,
		cef_uri_unescape_rule_t::
			UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS);

	std::string fileExtension = path.substr(path.find_last_of(".") + 1);

	for (char &ch : fileExtension)
		ch = (char)tolower(ch);
	if (fileExtension.compare("woff2") == 0)
		fileExtension = "woff";

#ifdef _WIN32
	CefRefPtr<CefStreamReader> stream =
		CefStreamReader::CreateForFile(path.substr(1));
#else
	CefRefPtr<CefStreamReader> stream =
		CefStreamReader::CreateForFile(path);
#endif

	if (stream) {
		CefString mimeType = CefGetMimeType(fileExtension);
		if (mimeType.empty())
			mimeType = "application/octet-stream";
		return new CefStreamResourceHandler(mimeType, stream);
	} else {
		return nullptr;
	}
}
#endif
