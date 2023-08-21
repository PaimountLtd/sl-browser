#include <obs.hpp>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-config.h>

#include <Windows.h>
#include <stdio.h>
#include <string>
#include <filesystem>

#include <QMainWindow>
#include <QDockWidget>

#include "GrpcPlugin.h"

#include "C:\github\obs-studio\UI\window-dock-browser.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("sl-browser-plugin", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "Streamlabs OBS";
}

bool obs_module_load(void)
{
	return true;
}

BOOL launched = FALSE;
PROCESS_INFORMATION slProcessInfo;

void workerThread()
{
	QMainWindow *mainWindow = (QMainWindow *)obs_frontend_get_main_window();

	while (true) {

		// Execute this lambda in the context of the main GUI thread.
		QMetaObject::invokeMethod(
			mainWindow,
			[mainWindow]() {
				// This code is executed in the context of the QMainWindow's thread.

				// For example:
				QList<QDockWidget *> docks = mainWindow->findChildren<QDockWidget *>();
				foreach(QDockWidget * dock, docks)
				{
					if (dock->property("uuid").isValid()) {

						printf("Dock with objectName: %s has a uuid property.\n",
						       dock->objectName().toStdString().c_str());


						BrowserDock *ptr = (BrowserDock *)dock;

						ptr->cefWidget->setURL("google.com");
						ptr->cefWidget->executeJavaScript("etc");
						//...

					}
				}

				// Or add a dock widget
				// QDockWidget *newDock = new QDockWidget();
				// mainWindow->addDockWidget(Qt::LeftDockWidgetArea, newDock);
			},
			Qt::BlockingQueuedConnection); // BlockingQueuedConnection waits until the slot (in this case, the lambda) has been executed.


		Sleep(10000);
	}
}

std::thread workerthr;

void obs_module_post_load(void)
{
	if (launched)
		return;

	AllocConsole();
	freopen("conin$", "r", stdin);
	freopen("conout$", "w", stdout);
	freopen("conout$", "w", stderr);
	printf("Debugging Window:\n");

	workerthr = std::thread(workerThread);

	auto chooseProxyPort = []()
	{
		int32_t result = 0;
		struct sockaddr_in local;
		SOCKET sockt = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

		if (sockt == INVALID_SOCKET)
			return result;

		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_family = AF_INET;
		local.sin_port = htons(0);

		// Bind
		if (::bind(sockt, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR)
			return result;

		struct sockaddr_in my_addr;
		memset(&my_addr, NULL, sizeof(my_addr));
		int len = sizeof(my_addr);
		getsockname(sockt, (struct sockaddr *)&my_addr, &len);
		result = ntohs(my_addr.sin_port);

		closesocket(sockt);
		return result;
	};

	int32_t myListenPort = chooseProxyPort();
	int32_t targetListenPort = chooseProxyPort();

	printf("Sending %d and %d to proxy\n", myListenPort, targetListenPort);

	STARTUPINFOW si;
	memset(&si, NULL, sizeof(si));
	si.cb = sizeof(si);

	if (GrpcPlugin::instance().startServer(myListenPort)) {

		try {
			const char *module_path = obs_get_module_binary_path(obs_current_module());

			if (!module_path)
				return;

			std::wstring process_path =
				std::filesystem::u8path(module_path).remove_filename().wstring() + L"/sl-browser.exe";

			std::wstring startparams = L"sl-browser " + std::to_wstring(myListenPort) + L" " + std::to_wstring(targetListenPort);

			wprintf(L"start params = %s\n", startparams.c_str());
			launched = CreateProcessW(process_path.c_str(), (LPWSTR)startparams.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE,
						  NULL, NULL, &si, &slProcessInfo);
		} catch (...) {
			blog(LOG_ERROR, "Streamlabs: obs_module_post_load catch while launching server");
		}

		if (launched && !GrpcPlugin::instance().connectToClient(targetListenPort))
		{
			launched = FALSE;
			blog(LOG_ERROR, "Streamlabs: obs_module_post_load can't connect to process, GetLastError = %d", GetLastError());

			// Terminates the process
			TerminateProcess(slProcessInfo.hProcess, EXIT_SUCCESS);
			CloseHandle(slProcessInfo.hProcess);
			CloseHandle(slProcessInfo.hThread);
		}
	}

	std::string errorMsg =
		"Failed to initialize plugin " + std::string(obs_module_description()) + "\nRestart the application and try again.";

	if (!launched) {
		::MessageBoxA(NULL, errorMsg.c_str(), "Streamlabs Error", MB_ICONERROR | MB_TOPMOST);
		blog(LOG_ERROR, "Streamlabs: obs_module_post_load can't start proxy process, GetLastError = %d", GetLastError());
		return;
	}
}

void obs_module_unload(void)
{

	// Tell process to shut down and wait?
	// ;
		
	GrpcPlugin::instance().stop();

	// Terminates the process (it shouldn't exist)
	TerminateProcess(slProcessInfo.hProcess, EXIT_SUCCESS);
	CloseHandle(slProcessInfo.hProcess);
	CloseHandle(slProcessInfo.hThread);
}
