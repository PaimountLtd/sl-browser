#include "PluginJsHandler.h"

// Local
#include "JavascriptApi.h"
#include "GrpcPlugin.h"
#include "deps/minizip/unzip.h"

// Windows
#include <wininet.h>
#include <ShlObj.h>

// Stl
#include <chrono>
#include <fstream>
#include <functional>
#include <codecvt>

// Obs
#include <obs.hpp>
#include <obs-frontend-api.h>

#include "../obs-browser/panel/browser-panel-internal.hpp"

// Qt
#include <QMainWindow>
#include <QDockWidget>
#include <QCheckBox>
#include <QMessageBox>

#pragma comment(lib, "wininet.lib")

using namespace json11;

PluginJsHandler::PluginJsHandler() {}

PluginJsHandler::~PluginJsHandler()
{
	stop();
}

std::wstring PluginJsHandler::getDownloadsDir() const
{
	wchar_t path[MAX_PATH];

	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path)))
		return std::wstring(path) + L"\\StreamlabsOBS";

	return L"";
}

void PluginJsHandler::start()
{
	m_running = true;
	m_workerThread = std::thread(&PluginJsHandler::workerThread, this);
}

void PluginJsHandler::stop()
{
	m_running = false;

	if (m_workerThread.joinable())
		m_workerThread.join();
}

void PluginJsHandler::pushApiRequest(const std::string &funcName, const std::string &params)
{
	std::lock_guard<std::mutex> grd(m_queueMtx);
	m_queudRequests.push_back({funcName, params});
}

void PluginJsHandler::workerThread()
{
	while (m_running)
	{
		std::vector<std::pair<std::string, std::string>> latestBatch;

		{
			std::lock_guard<std::mutex> grd(m_queueMtx);
			latestBatch.swap(m_queudRequests);
		}

		if (latestBatch.empty())
		{
			using namespace std::chrono;
			std::this_thread::sleep_for(1ms);
		}
		else
		{
			for (auto &itr : latestBatch)
				executeApiRequest(itr.first, itr.second);
		}
	}
}

void PluginJsHandler::executeApiRequest(const std::string &funcName, const std::string &params)
{
	std::string err;
	Json jsonParams = Json::parse(params, err);

	if (!err.empty())
	{
		blog(LOG_ERROR, "PluginJsHandler::executeApiRequest invalid params %s", params.c_str());
		return;
	}

	const auto &param1Value = jsonParams["param1"];

	if (param1Value.is_null())
	{
		blog(LOG_ERROR, "PluginJsHandler::executeApiRequest Error: 'param1' key not found. %s", params.c_str());
		return;
	}

	blog(LOG_INFO, "executeApiRequest (start) %s: %s\n", funcName.c_str(), params.c_str());

	std::string jsonReturnStr;

	switch (JavascriptApi::getFunctionId(funcName))
	{
	case JavascriptApi::JS_DOCK_EXECUTEJAVASCRIPT: {
		JS_DOCK_EXECUTEJAVASCRIPT(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DOCK_SETURL: {
		JS_DOCK_SETURL(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_QUERY_DOCKS: {
		JS_QUERY_DOCKS(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DOWNLOAD_ZIP: {
		JS_DOWNLOAD_ZIP(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_READ_FILE: {
		JS_READ_FILE(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DELETE_FILES: {
		JS_DELETE_FILES(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DROP_FOLDER: {
		JS_DROP_FOLDER(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_QUERY_DOWNLOADS_FOLDER: {
		JS_QUERY_DOWNLOADS_FOLDER(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_OBS_SOURCE_CREATE: {
		JS_OBS_SOURCE_CREATE(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_OBS_SOURCE_DESTROY: {
		JS_OBS_SOURCE_DESTROY(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DOCK_NEW_BROWSER_DOCK: {
		JS_DOCK_NEW_BROWSER_DOCK(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_GET_MAIN_WINDOW_GEOMETRY: {
		JS_GET_MAIN_WINDOW_GEOMETRY(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DOCK_SETAREA: {
		JS_DOCK_SETAREA(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DOCK_RESIZE: {
		JS_DOCK_RESIZE(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_TOGGLE_USER_INPUT: {
		JS_TOGGLE_USER_INPUT(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_TOGGLE_DOCK_VISIBILITY: {
		JS_TOGGLE_DOCK_VISIBILITY(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DOCK_SETTITLE: {
		JS_DOCK_SETTITLE(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_DOCK_RENAME: {
		JS_DOCK_RENAME(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_SET_STREAMSETTINGS: {
		JS_SET_STREAMSETTINGS(jsonParams, jsonReturnStr);
		break;
	}
	case JavascriptApi::JS_GET_STREAMSETTINGS: {
		JS_GET_STREAMSETTINGS(jsonParams, jsonReturnStr);
		break;
	}	
	}

	blog(LOG_INFO, "executeApiRequest (finish) %s: %s\n", funcName.c_str(), jsonReturnStr.c_str());

	// We're done, send callback
	if (param1Value.int_value() > 0)
		GrpcPlugin::instance().getClient()->send_executeCallback(param1Value.int_value(), jsonReturnStr);
}

void PluginJsHandler::JS_GET_STREAMSETTINGS(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &out_jsonReturn]() {
			obs_service_t *service_t = obs_frontend_get_streaming_service();

			if (service_t)
			{
				obs_data_t *settings = obs_service_get_settings(service_t);

				std::string service = obs_data_get_string(settings, "service");
				std::string protocol = obs_data_get_string(settings, "protocol");
				std::string server = obs_data_get_string(settings, "server");
				bool use_auth = obs_data_get_bool(settings, "use_auth");
				std::string username = obs_data_get_string(settings, "username");
				std::string password = obs_data_get_string(settings, "password");
				std::string key = obs_data_get_string(settings, "key");

				out_jsonReturn = Json(Json::object{{"service", service}, {"protocol", protocol}, {"server", server}, {"use_auth", use_auth}, {"username", username}, {"password", password}, {"key", key}}).dump();
			}
			else
			{
				out_jsonReturn = Json(Json::object({{"error", "No service exists"}})).dump();
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_SET_STREAMSETTINGS(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];
	const auto &param5Value = params["param5"];
	const auto &param6Value = params["param6"];
	const auto &param7Value = params["param7"];
	const auto &param8Value = params["param8"];

	// "rtmp_custom" : "rtmp_common"
	std::string service = param2Value.string_value();
	std::string protocol = param3Value.string_value();
	std::string server = param4Value.string_value();
	bool use_auth = param5Value.bool_value();
	std::string username = param6Value.string_value();
	std::string password = param7Value.string_value();
	std::string key = param8Value.string_value();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &service, &protocol, &server, use_auth, &username, &password, &key, &out_jsonReturn]() {

			obs_service_t *oldService = obs_frontend_get_streaming_service();
			OBSDataAutoRelease hotkeyData = obs_hotkeys_save_service(oldService);

			OBSDataAutoRelease settings = obs_data_create();

			obs_data_set_string(settings, "service", service.c_str());
			obs_data_set_string(settings, "protocol", protocol.c_str());
			obs_data_set_string(settings, "server", server.c_str());
			obs_data_set_bool(settings, "use_auth", use_auth);
			obs_data_set_string(settings, "username", username.c_str());
			obs_data_set_string(settings, "password", password.c_str());
			obs_data_set_string(settings, "key", key.c_str());

			OBSServiceAutoRelease newService = obs_service_create(service.c_str(), "default_service", settings, hotkeyData);

			if (!newService)
				return;

			obs_frontend_set_streaming_service(newService);
			obs_frontend_save_streaming_service();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_QUERY_DOCKS(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &out_jsonReturn]() {
			std::vector<Json> dockInfo;

			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				bool isSlabs = false;
				std::string name = dock->objectName().toStdString();
				std::string url;

				// Translate the global coordinates to coordinates relative to the main window
				QRect globalGeometry = dock->geometry();
				QRect mainWindowGeometry = mainWindow->geometry();
				int x = globalGeometry.x() - mainWindowGeometry.x();
				int y = globalGeometry.y() - mainWindowGeometry.y();
				int width = dock->width();
				int height = dock->height();
				bool floating = dock->isFloating();
				bool visible = dock->isVisible();
				std::string dockName = dock->objectName().toStdString();
				std::string dockTitle = dock->windowTitle().toStdString();

				if (dock->property("isSlabs").isValid())
				{
					isSlabs = true;
					QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();

					if (widget->cefBrowser != nullptr)
						url = widget->cefBrowser->GetMainFrame()->GetURL();
				}

				// Create a Json object for this dock widget and add it to the panelInfo vector
				dockInfo.push_back(Json::object{{"name", name}, {"x", x}, {"y", y}, {"width", width}, {"height", height}, {"floating", floating},
					{"isSlabs", isSlabs}, {"url", url}, {"visible", visible}, {"title", dockTitle}});
			}

			// Convert the panelInfo vector to a Json object and dump string
			Json ret = dockInfo;
			out_jsonReturn = ret.dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_RESIZE(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string objectName = param2Value.string_value();
	int width = param3Value.int_value();
	int height = param3Value.int_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, width, height, &out_jsonReturn]() {
			// Find the panel by name (assuming the name is stored as a property)
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					dock->resize(width, height);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_SETAREA(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	int areaMask = param3Value.int_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, areaMask, &out_jsonReturn]() {
			// Find the panel by name (assuming the name is stored as a property)
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					if (dock->isFloating())
						dock->setFloating(false);

					// Map the input area mask to the corresponding Qt::DockWidgetArea
					Qt::DockWidgetArea dockArea = static_cast<Qt::DockWidgetArea>(areaMask & Qt::DockWidgetArea_Mask);
					mainWindow->addDockWidget(dockArea, dock);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_EXECUTEJAVASCRIPT(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string javascriptcode = param3Value.string_value();

	if (javascriptcode.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid params"}})).dump();
		return;
	}

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, javascriptcode, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName && dock->property("isSlabs").isValid())
				{
					QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();

					if (widget->cefBrowser != nullptr)
					{
						widget->cefBrowser->GetMainFrame()->ExecuteJavaScript(javascriptcode.c_str(), widget->cefBrowser->GetMainFrame()->GetURL(), 0);
						out_jsonReturn = Json(Json::object{{"status", "Found dock and ran ExecuteJavaScript on " + widget->cefBrowser->GetMainFrame()->GetURL().ToString()}}).dump();
					}

					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_TOGGLE_USER_INPUT(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	bool enable = params["param2"].bool_value();

	QMetaObject::invokeMethod(
		mainWindow, [mainWindow, enable]() { ::EnableWindow(reinterpret_cast<HWND>(mainWindow->winId()), enable); }, Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_NEW_BROWSER_DOCK(const json11::Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];
	const auto &param4Value = params["param4"];

	std::string title = param2Value.string_value();
	std::string url = param3Value.string_value();
	std::string objectName = param4Value.string_value();

	if (objectName.empty() || title.empty() || url.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid params"}})).dump();
		return;
	}

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, title, url, &out_jsonReturn]() {
			// Check duplication
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					out_jsonReturn = Json(Json::object({{"error", "Already exists"}})).dump();
					return;
				}
			}

			static QCef *qcef = obs_browser_init_panel();

			QDockWidget *dock = new QDockWidget(mainWindow);
			QCefWidget *browser = qcef->create_widget(dock, url, nullptr);
			dock->setWidget(browser);
			dock->setWindowTitle(title.c_str());
			dock->setObjectName(objectName.c_str());
			dock->setProperty("isSlabs", true);

			// obs_frontend_add_dock and keep the pointer to it
			dock->setProperty("actionptr", (uint64_t)obs_frontend_add_dock(dock));

			dock->resize(460, 600);
			dock->setMinimumSize(80, 80);
			dock->setWindowTitle(title.c_str());
			dock->setAllowedAreas(Qt::AllDockWidgetAreas);
			dock->setWidget(browser);

			mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);

			// Can't use yet
			//obs_frontend_add_dock_by_id(objectName.c_str(), title.c_str(), nullptr);
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_GET_MAIN_WINDOW_GEOMETRY(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, &out_jsonReturn]() {
			int x = mainWindow->geometry().x();
			int y = mainWindow->geometry().y();
			int width = mainWindow->width();
			int height = mainWindow->height();
			out_jsonReturn = Json(Json::object{{{"x", x}, {"y", y}, {"width", width}, {"height", height}}}).dump();
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_SETURL(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string url = param3Value.string_value();

	if (url.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid params"}})).dump();
		return;
	}

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, url, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName && dock->property("isSlabs").isValid())
				{
					QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();
					widget->setURL(url.c_str());
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_TOGGLE_DOCK_VISIBILITY(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	bool visible = param3Value.bool_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, visible, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					dock->setVisible(visible);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_SETTITLE(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string newTitle = param3Value.string_value();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, newTitle, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					QAction *action = reinterpret_cast<QAction *>(dock->property("actionptr").toULongLong());
					action->setText(newTitle.c_str());
					dock->setWindowTitle(newTitle.c_str());
					out_jsonReturn = Json(Json::object({{"error", "success"}})).dump();
					break;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOCK_RENAME(const json11::Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	const auto &param3Value = params["param3"];

	std::string objectName = param2Value.string_value();
	std::string newName = param3Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// An error for now, if we succeed this is overwritten
	out_jsonReturn = Json(Json::object({{"error", "Did not find dock with objectName: " + objectName}})).dump();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, newName, objectName, &out_jsonReturn]() {
			QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();

			// Check for match against the new name
			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == newName)
				{
					out_jsonReturn = Json(Json::object({{"error", "Dock with that already exists: " + objectName}})).dump();
					return;
				}
			}

			foreach(QDockWidget * dock, docks)
			{
				if (dock->objectName().toStdString() == objectName)
				{
					dock->setObjectName(newName);
					out_jsonReturn = Json(Json::object{{"status", "success"}}).dump();
					return;
				}
			}
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DESTROY_DOCK(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];
	std::string objectName = param2Value.string_value();

	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, objectName, &out_jsonReturn]() {
			//obs_frontend_remove_dock(objectName.c_str());
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_DOWNLOAD_ZIP(const Json &params, std::string &out_jsonReturn)
{
	auto downloadFile = [](const std::string &url, const std::string &filename) {
		HINTERNET connect = InternetOpenA("Streamlabs", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);

		if (!connect)
			return false;

		HINTERNET hOpenAddress = InternetOpenUrlA(connect, url.c_str(), NULL, 0, INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_KEEP_CONNECTION, 0);

		if (!hOpenAddress)
		{
			InternetCloseHandle(connect);
			return false;
		}

		DWORD contentLength;
		DWORD contentLengthSize = sizeof(contentLength);
		if (!HttpQueryInfo(hOpenAddress, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &contentLengthSize, NULL))
		{
			blog(LOG_ERROR, "JS_DOWNLOAD_ZIP: HttpQueryInfo failed, last error = %d\n", GetLastError());
			InternetCloseHandle(hOpenAddress);
			InternetCloseHandle(connect);
			return false;
		}

		char dataReceived[4096];
		DWORD numberOfBytesRead = 0;

		std::ofstream outFile(filename, std::ios::binary);
		if (!outFile.is_open())
		{
			blog(LOG_ERROR, "JS_DOWNLOAD_ZIP: Could not open std::ofstream outFile, last error = %d\n", GetLastError());
			InternetCloseHandle(hOpenAddress);
			InternetCloseHandle(connect);
			return false;
		}

		DWORD totalBytesRead = 0;
		while (InternetReadFile(hOpenAddress, dataReceived, 4096, &numberOfBytesRead) && numberOfBytesRead > 0)
		{
			outFile.write(dataReceived, numberOfBytesRead);
			totalBytesRead += numberOfBytesRead;
		}

		outFile.close();
		InternetCloseHandle(hOpenAddress);
		InternetCloseHandle(connect);

		if (totalBytesRead != contentLength)
		{
			blog(LOG_ERROR, "JS_DOWNLOAD_ZIP: Incomplete download, last error = %d\n", GetLastError());
			std::remove(filename.c_str());
			return false;
		}

		return true;
	};

	auto unzip = [](const std::string &filepath, std::vector<std::string> &output) {
		unzFile zipFile = unzOpen(filepath.c_str());
		if (!zipFile)
		{
			blog(LOG_ERROR, "Unable to open zip file: %s", filepath.c_str());
			return;
		}

		std::filesystem::path outputDir = std::filesystem::path(filepath).parent_path();
		char buffer[4096];

		unz_global_info globalInfo;
		if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK)
		{
			blog(LOG_ERROR, "Could not read file global info: %s", filepath.c_str());
			unzClose(zipFile);
			return;
		}

		for (uLong i = 0; i < globalInfo.number_entry; ++i)
		{
			unz_file_info fileInfo;
			char filename[256];
			if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, 256, NULL, 0, NULL, 0) != UNZ_OK)
			{
				blog(LOG_ERROR, "Could not read file info: %s", filepath.c_str());
				unzClose(zipFile);
				return;
			}

			const std::string fullOutputPath = outputDir.string() + '/' + filename;

			// If the file in the zip archive is a directory, continue to next file
			if (filename[strlen(filename) - 1] == '/')
			{
				if ((i + 1) < globalInfo.number_entry)
				{
					if (unzGoToNextFile(zipFile) != UNZ_OK)
					{
						blog(LOG_ERROR, "Could not read next file: %s", filepath.c_str());
						unzClose(zipFile);
						return;
					}
				}
				continue;
			}

			if (unzOpenCurrentFile(zipFile) != UNZ_OK)
			{
				blog(LOG_ERROR, "Could not open current file: %s", filepath.c_str());
				unzClose(zipFile);
				return;
			}

			// Create necessary directories
			std::filesystem::path pathToFile(fullOutputPath);
			if (fileInfo.uncompressed_size > 0)
			{
				std::filesystem::create_directories(pathToFile.parent_path());
			}

			std::ofstream outFile(fullOutputPath, std::ios::binary);
			int error = UNZ_OK;
			do
			{
				error = unzReadCurrentFile(zipFile, buffer, sizeof(buffer));
				if (error < 0)
				{
					blog(LOG_ERROR, "Error %d with zipfile in unzReadCurrentFile: %s", error, filepath.c_str());
					break;
				}
				if (error > 0)
				{
					outFile.write(buffer, error);
				}
			} while (error > 0);

			outFile.close();

			// Adding the file path to the output vector
			output.push_back(fullOutputPath);

			if (unzCloseCurrentFile(zipFile) != UNZ_OK)
			{
				blog(LOG_ERROR, "Could not close file: %s", filepath.c_str());
			}

			if ((i + 1) < globalInfo.number_entry)
			{
				if (unzGoToNextFile(zipFile) != UNZ_OK)
				{
					blog(LOG_ERROR, "Could not read next file: %s", filepath.c_str());
					unzClose(zipFile);
					return;
				}
			}
		}

		unzClose(zipFile);
	};

	const auto &param2Value = params["param2"];
	std::string url = param2Value.string_value();
	std::wstring folderPath = getDownloadsDir();

	if (!folderPath.empty())
	{
		using namespace std::chrono;

		// Current time in miliseconds
		system_clock::time_point now = system_clock::now();
		auto duration = now.time_since_epoch();
		auto millis = duration_cast<milliseconds>(duration).count();
		std::wstring millis_str = std::to_wstring(millis);

		// ThreadID + MsTime should be unique, same threaID within 1ms window is a statistical improbability
		std::wstring subFolderPath = folderPath + L"\\" + std::to_wstring(GetCurrentThreadId()) + millis_str;
		std::wstring zipFilepath = subFolderPath + L"\\download.zip";

		CreateDirectoryW(folderPath.c_str(), NULL);
		CreateDirectoryW(subFolderPath.c_str(), NULL);

		auto wstring_to_utf8 = [](const std::wstring &str) {
			std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
			return myconv.to_bytes(str);
		};

		if (downloadFile(url, wstring_to_utf8(zipFilepath)))
		{
			std::vector<std::string> filepaths;
			unzip(wstring_to_utf8(zipFilepath), filepaths);

			// Build json string now
			Json::array json_array;

			for (const auto &filepath : filepaths)
			{
				Json::object obj;
				obj["path"] = filepath;
				json_array.push_back(obj);
			}

			out_jsonReturn = Json(json_array).dump();
		}
		else
		{
			out_jsonReturn = Json(Json::object({{"error", "Http download file failed"}})).dump();
		}

		// zip file itself not needed
		::_wremove(zipFilepath.c_str());
	}
	else
	{
		out_jsonReturn = Json(Json::object({{"error", "File system can't access Local AppData folder"}})).dump();
	}
}

void PluginJsHandler::JS_READ_FILE(const Json &params, std::string &out_jsonReturn)
{
	const auto &param2Value = params["param2"];

	std::string filepath = param2Value.string_value();
	std::string filecontents;

	std::ifstream file(filepath, std::ios::binary | std::ios::ate);

	Json ret;

	if (file)
	{
		try
		{
			// Get the file size
			std::streamsize fileSize = file.tellg();
			file.seekg(0, std::ios::beg);

			// Check if file size is 1MB or higher
			if (fileSize >= 1048576)
			{
				ret = Json::object({{"error", "File size is 1MB or higher"}});
			}
			else
			{
				std::stringstream buffer;
				buffer << file.rdbuf();
				filecontents = buffer.str();
				ret = Json::object({{"contents", filecontents}});
			}
		}
		catch (...)
		{
			ret = Json::object({{"error", "Unable to read file. Checking for windows errors: '" + std::to_string(GetLastError()) + "'"}});
		}
	}
	else
	{
		ret = Json::object({{"error", "Unable to open file. Checking for windows errors: '" + std::to_string(GetLastError()) + "'"}});
	}

	out_jsonReturn = ret.dump();
}

void PluginJsHandler::JS_DELETE_FILES(const Json &params, std::string &out_jsonReturn)
{
	Json ret;
	std::vector<std::string> errors;
	std::vector<std::string> success;

	const auto &param2Value = params["param2"];

	std::string err;
	Json jsonArray = Json::parse(params["param2"].string_value().c_str(), err);

	if (!err.empty())
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid parameter: " + err}})).dump();
		return;
	}

	const auto &filepaths = jsonArray.array_items();

	for (const auto &filepathJson : filepaths)
	{
		if (filepathJson.is_object())
		{
			const auto &filepath = filepathJson["path"].string_value();

			std::filesystem::path downloadsDir = std::filesystem::path(getDownloadsDir());
			std::filesystem::path fullPath = downloadsDir / filepath;
			std::filesystem::path normalizedPath = fullPath.lexically_normal();

			// Check if filepath contains relative components that move outside the downloads directory
			if (normalizedPath.string().find(downloadsDir.string()) != 0)
			{
				errors.push_back("Invalid path: " + filepath);
			}
			else
			{
				try
				{
					if (std::filesystem::exists(normalizedPath))
					{
						std::filesystem::remove(normalizedPath);
						success.push_back(filepath);
					}
					else
					{
						errors.push_back("File not found: " + filepath);
					}
				}
				catch (const std::filesystem::filesystem_error &e)
				{
					errors.push_back("Error deleting file '" + filepath + "': " + e.what());
				}
			}
		}
	}

	out_jsonReturn = Json(Json::object({{"success", success}, {"errors", errors}})).dump();
}

void PluginJsHandler::JS_DROP_FOLDER(const Json &params, std::string &out_jsonReturn)
{
	const auto &filepath = params["param2"].string_value();

	std::filesystem::path downloadsDir = std::filesystem::path(getDownloadsDir());
	std::filesystem::path fullPath = downloadsDir / filepath;
	std::filesystem::path normalizedPath = fullPath.lexically_normal();

	// Check if filepath contains relative components that move outside the downloads directory
	if (normalizedPath.string().find(downloadsDir.string()) != 0)
	{
		out_jsonReturn = Json(Json::object({{"error", "Invalid path: " + filepath}})).dump();
	}
	else
	{
		try
		{
			std::filesystem::remove_all(normalizedPath);
		}
		catch (const std::filesystem::filesystem_error &e)
		{
			out_jsonReturn = Json(Json::object({{"error", "Failed to delete '" + filepath + "': " + e.what()}})).dump();
		}
	}
}

void PluginJsHandler::JS_QUERY_DOWNLOADS_FOLDER(const Json &params, std::string &out_jsonReturn)
{
	std::wstring downloadsFolderFullPath = getDownloadsDir();
	std::vector<Json> pathsList;

	try
	{
		for (const auto &entry : std::filesystem::directory_iterator(downloadsFolderFullPath))
			pathsList.push_back(entry.path().string());

		out_jsonReturn = Json(Json::array(pathsList)).dump();
	}
	catch (const std::filesystem::filesystem_error &e)
	{
		out_jsonReturn = Json(Json::object({{"error", "Failed to query downloads folder: " + std::string(e.what())}})).dump();
	}
}

void PluginJsHandler::JS_OBS_SOURCE_CREATE(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, this, &params, &out_jsonReturn]() {
			const auto &id = params["param2"].string_value();
			const auto &name = params["param3"].string_value();
			const auto &settings_jsonStr = params["param4"].string_value();
			const auto &hotkey_data_jsonStr = params["param5"].string_value();

			// Name is also the guid, duplicates can't exist
			//	see "bool AddNew(QWidget *parent, const char *id, const char *name," in obs gui code
			OBSSourceAutoRelease existingSource = obs_get_source_by_name(name.c_str());

			if (existingSource != nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "name already exists, " + name}})).dump();
				return;
			}

			obs_data_t *settings = obs_data_create_from_json(settings_jsonStr.c_str());
			obs_data_t *hotkeys = obs_data_create_from_json(hotkey_data_jsonStr.c_str());

			obs_data_release(hotkeys);
			obs_data_release(settings);

			obs_source_t *source = obs_source_create(id.c_str(), name.c_str(), settings, hotkeys);

			if (!source)
			{
				out_jsonReturn = Json(Json::object({{"error", "obs_source_create returned null"}})).dump();
				return;
			}

			obs_data_t *settingsSource = obs_source_get_settings(source);

			Json jsonReturnValue = Json::object({{"settings", Json(obs_data_get_json(settingsSource))},
							     {"audio_mixers", Json(std::to_string(obs_source_get_audio_mixers(source)))},
							     {"deinterlace_mode", Json(std::to_string(obs_source_get_deinterlace_mode(source)))},
							     {"deinterlace_field_order", Json(std::to_string(obs_source_get_deinterlace_field_order(source)))}});

			out_jsonReturn = jsonReturnValue.dump();
			obs_data_release(settingsSource);
		},
		Qt::BlockingQueuedConnection);
}

void PluginJsHandler::JS_OBS_SOURCE_DESTROY(const Json &params, std::string &out_jsonReturn)
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	// This code is executed in the context of the QMainWindow's thread.
	QMetaObject::invokeMethod(
		mainWindow,
		[mainWindow, this, params, &out_jsonReturn]() {
			const auto &name = params["param2"].string_value();

			OBSSourceAutoRelease src = obs_get_source_by_name(name.c_str());

			auto itr = m_sources.find(name);

			if (itr != m_sources.end())
				m_sources.erase(itr);

			if (src == nullptr)
			{
				out_jsonReturn = Json(Json::object({{"error", "Can't find source with name " + name}})).dump();
				return;
			}

			if (obs_source_get_type(src) == OBS_SOURCE_TYPE_TRANSITION)
			{
				obs_source_release(src);
			}
			else if (obs_source_get_type(src) == OBS_SOURCE_TYPE_SCENE)
			{
				blog(LOG_INFO, "Releasing scene %s", obs_source_get_name(src));
				std::list<obs_sceneitem_t *> items;
				auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
					if (item)
					{
						std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
						obs_sceneitem_addref(item);
						items->push_back(item);
					}
					return true;
				};
				obs_scene_t *scene = obs_scene_from_source(src);
				if (scene)
					obs_scene_enum_items(scene, cb, &items);

				for (auto item : items)
				{
					obs_sceneitem_remove(item);
					obs_sceneitem_release(item);
				}

				obs_source_release(src);
			}
			else
			{
				obs_source_remove(src);
			}
		},
		Qt::BlockingQueuedConnection);
}

/***
* Save/Load
**/

// Handle the close event here before Qt sees it
LRESULT CALLBACK HandleWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CLOSE)
	{
		PluginJsHandler::instance().saveSlabsBrowserDocks();
	}

	// Allow Qt to process the message as well
	WNDPROC origWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	return CallWindowProc(origWndProc, hwnd, uMsg, wParam, lParam);
}

void PluginJsHandler::saveSlabsBrowserDocks()
{
	Json::array jarray;
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();

	foreach(QDockWidget * dock, docks)
	{
		if (dock->property("isSlabs").isValid())
		{
			QCefWidgetInternal *widget = (QCefWidgetInternal *)dock->widget();

			std::string url;

			if (widget->cefBrowser != nullptr)
				url = widget->cefBrowser->GetMainFrame()->GetURL();

			Json::object obj{
				{"title", dock->windowTitle().toStdString()},
				{"url", url},
				{"objectName", dock->objectName().toStdString()},
			};

			jarray.push_back(obj);
		}
	}

	std::string output = Json(jarray).dump();
	config_set_string(obs_frontend_get_global_config(), "BasicWindow", "SlabsBrowserDocks", output.c_str());
}

void PluginJsHandler::loadSlabsBrowserDocks()
{
	// This is to intercept the shutdown event so that we can save it before OBS does anything
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();
	WNDPROC origWndProc = (WNDPROC)SetWindowLongPtr(reinterpret_cast<HWND>(mainWindow->winId()), GWLP_WNDPROC, (LONG_PTR)HandleWndProc);
	SetWindowLongPtr(reinterpret_cast<HWND>(mainWindow->winId()), GWLP_USERDATA, (LONG_PTR)origWndProc);

	const char *jsonStr = config_get_string(obs_frontend_get_global_config(), "BasicWindow", "SlabsBrowserDocks");

	std::string err;
	Json json = Json::parse(jsonStr, err);

	if (!err.empty())
		return;

	Json::array array = json.array_items();

	for (Json &item : array)
	{
		std::string title = item["title"].string_value();
		std::string url = item["url"].string_value();
		std::string objectName = item["objectName"].string_value();

		static QCef *qcef = obs_browser_init_panel();

		QDockWidget *dock = new QDockWidget(mainWindow);
		QCefWidget *browser = qcef->create_widget(dock, url, nullptr);
		dock->setWidget(browser);
		dock->setWindowTitle(title.c_str());
		dock->setObjectName(objectName.c_str());
		dock->setProperty("isSlabs", true);
		dock->setObjectName(objectName.c_str());

		// obs_frontend_add_dock and keep the pointer to it
		dock->setProperty("actionptr", (uint64_t)obs_frontend_add_dock(dock));

		dock->resize(460, 600);
		dock->setMinimumSize(80, 80);
		dock->setWindowTitle(title.c_str());
		dock->setAllowedAreas(Qt::AllDockWidgetAreas);
		dock->setWidget(browser);
	}
}
