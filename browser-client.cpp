/******************************************************************************
 Copyright (C) 2014 by John R. Bradley <jrb@turrettech.com>
 Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************/

#include "browser-client.hpp"
#include "base64/base64.hpp"
#include "json11/json11.hpp"

#include <QApplication>
#include <QThread>
#include <QToolTip>

#include "GrpcProxy.h"

inline bool BrowserClient::valid() const
{
	return true;
}

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler()
{
	return this;
}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler()
{
	return this;
}

CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler()
{
	return this;
}

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler()
{
	return this;
}

CefRefPtr<CefContextMenuHandler> BrowserClient::GetContextMenuHandler()
{
	return this;
}

CefRefPtr<CefAudioHandler> BrowserClient::GetAudioHandler()
{
	return reroute_audio ? this : nullptr;
}

#if CHROME_VERSION_BUILD >= 4638
CefRefPtr<CefRequestHandler> BrowserClient::GetRequestHandler()
{
	return this;
}

CefRefPtr<CefResourceRequestHandler> BrowserClient::GetResourceRequestHandler(
	CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
	CefRefPtr<CefRequest> request, bool, bool, const CefString &, bool &)
{
	if (request->GetHeaderByName("origin") == "null") {
		return this;
	}

	return nullptr;
}

CefResourceRequestHandler::ReturnValue
BrowserClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
				    CefRefPtr<CefRequest>,
				    CefRefPtr<CefCallback>)
{
	return RV_CONTINUE;
}
#endif

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
				  const CefString &, const CefString &,
				  cef_window_open_disposition_t, bool,
				  const CefPopupFeatures &, CefWindowInfo &,
				  CefRefPtr<CefClient> &, CefBrowserSettings &,
				  CefRefPtr<CefDictionaryValue> &, bool *)
{
	/* block popups */
	return true;
}

void BrowserClient::OnBeforeContextMenu(CefRefPtr<CefBrowser>,
					CefRefPtr<CefFrame>,
					CefRefPtr<CefContextMenuParams>,
					CefRefPtr<CefMenuModel> model)
{
	/* remove all context menu contributions */
	model->Clear();
}

/*static*/
json11::Json convertCefValueToJSON(CefRefPtr<CefValue> value)
{
	switch (value->GetType()) {
	case VTYPE_NULL:
		return nullptr;

	case VTYPE_BOOL:
		return value->GetBool();

	case VTYPE_INT:
		return value->GetInt();

	case VTYPE_DOUBLE:
		return value->GetDouble();

	case VTYPE_STRING:
		return value->GetString().ToString();

	case VTYPE_LIST: {
		const auto &list = value->GetList();
		std::vector<json11::Json> jsonList;
		for (size_t i = 0; i < list->GetSize(); ++i) {
			jsonList.push_back(convertCefValueToJSON(list->GetValue(i)));
		}
		return jsonList;
	}

	case VTYPE_DICTIONARY: {
		const auto &dict = value->GetDictionary();
		std::map<std::string, json11::Json> jsonMap;
		CefDictionaryValue::KeyList keys;
		dict->GetKeys(keys);
		for (const auto &key : keys) {
			jsonMap[key] = convertCefValueToJSON(dict->GetValue(key));
		}
		return jsonMap;
	}

	default:
		return nullptr;
	}
}

/*static*/
std::string BrowserClient::cefListValueToJSONString(CefRefPtr<CefListValue> listValue)
{
	std::map<std::string, json11::Json> jsonMap;
	for (size_t i = 0; i < listValue->GetSize(); ++i) {
		// Convert index to string key like "param1", "param2", ...
		std::string key = "param" + std::to_string(i + 1);
		jsonMap[key] = convertCefValueToJSON(listValue->GetValue(i));
	}

	std::string json_str;
	json_str = json11::Json(jsonMap).dump();
	return json_str;
}

bool BrowserClient::OnProcessMessageReceived(
	CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, CefProcessId,
	CefRefPtr<CefProcessMessage> message)
{
	const std::string &name = message->GetName();
	CefRefPtr<CefListValue> input_args = message->GetArgumentList();
	
	if (!valid()) {
		return false;
	}

	if (!GrpcProxy::instance().getClient()->send_js_api(name, cefListValueToJSONString(input_args)))
	{
		// todo; handle
		abort();
		return false;
	}

	// Sends back to the render proxy process
	//CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("executeCallback");
	//CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();
	//execute_args->SetInt(0, input_args->GetInt(0));
	//execute_args->SetString(1, "{}");
	//SendBrowserProcessMessage(browser, PID_RENDERER, msg);

	return true;
}

void BrowserClient::GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect)
{
	if (!valid()) {
		rect.Set(0, 0, 16, 16);
		return;
	}

	rect.Set(0, 0, 1920, 1080);
}

bool BrowserClient::OnTooltip(CefRefPtr<CefBrowser>, CefString &text)
{
	std::string str_text = text;
	QMetaObject::invokeMethod(
		QCoreApplication::instance()->thread(), [str_text]() {
			QToolTip::showText(QCursor::pos(), str_text.c_str());
		});
	return true;
}

void BrowserClient::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
			    const RectList &, const void *buffer, int width,
			    int height)
{
	if (type != PET_VIEW) {
		// TODO Overlay texture on top of bs->texture
		return;
	}

	if (!valid()) {
		return;
	}
}

void BrowserClient::OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
					 const CefAudioParameters &params_,
					 int channels_)
{
	channels = channels_;
	channel_layout = (ChannelLayout)params_.channel_layout;
	sample_rate = params_.sample_rate;
	frames_per_buffer = params_.frames_per_buffer;
}

void BrowserClient::OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
					const float **data, int frames,
					int64_t pts)
{
	if (!valid()) {
		return;
	}
}

void BrowserClient::OnAudioStreamStopped(CefRefPtr<CefBrowser> browser)
{

}

void BrowserClient::OnAudioStreamError(CefRefPtr<CefBrowser> browser,
				       const CefString &message)
{

}

static CefAudioHandler::ChannelLayout Convert2CEFSpeakerLayout(int channels)
{
	switch (channels) {
	case 1:
		return CEF_CHANNEL_LAYOUT_MONO;
	case 2:
		return CEF_CHANNEL_LAYOUT_STEREO;
	case 3:
		return CEF_CHANNEL_LAYOUT_2_1;
	case 4:
		return CEF_CHANNEL_LAYOUT_4_0;
	case 5:
		return CEF_CHANNEL_LAYOUT_4_1;
	case 6:
		return CEF_CHANNEL_LAYOUT_5_1;
	case 8:
		return CEF_CHANNEL_LAYOUT_7_1;
	default:
		return CEF_CHANNEL_LAYOUT_UNSUPPORTED;
	}
}

bool BrowserClient::GetAudioParameters(CefRefPtr<CefBrowser> browser,
				       CefAudioParameters &params)
{
	int channels = 2;
	params.channel_layout = CEF_CHANNEL_LAYOUT_MONO;
	params.sample_rate = 48000;
	params.frames_per_buffer = kFramesPerBuffer;
	return true;
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int)
{
	if (!valid()) {
		return;
	}

	if (frame->IsMain()) {
	}
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser>,
				     cef_log_severity_t level,
				     const CefString &message,
				     const CefString &source, int line)
{
	return false;
}
