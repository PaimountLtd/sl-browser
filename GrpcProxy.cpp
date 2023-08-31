#include "GrpcProxy.h"
#include "browser-client.hpp"

#include <filesystem>

extern CefRefPtr<BrowserClient> browserClient;

	/***
* Server
* Receiving messages from the plugin
*/

class grpc_proxy_objImpl final : public grpc_proxy_obj::Service {
	grpc::Status com_grpc_js_executeCallback(grpc::ServerContext *context, const grpc_js_api_ExecuteCallback *request,
						 grpc_js_api_Reply *response) override
	{
		CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("executeCallback");
		CefRefPtr<CefListValue> execute_args = msg->GetArgumentList();
		execute_args->SetInt(0, request->funcid());
		execute_args->SetString(1, request->jsonstr());

		if (auto ptr = browserClient->PopCallback(request->funcid())) {
			SendBrowserProcessMessage(ptr, PID_RENDERER, msg);
		}
		else {
			printf("com_grpc_js_executeCallback failed to find browser for function");
		}
			

		return grpc::Status::OK;
	}
};

/***
* Client
* Sending messages to the plugin
*/

grpc_proxy_objClient::grpc_proxy_objClient(std::shared_ptr<grpc::Channel> channel) :
	stub_(grpc_plugin_obj::NewStub(channel))
{
	m_connected = channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(3));
}

bool grpc_proxy_objClient::send_js_api(const std::string &funcName, const std::string &params)
{
	grpc_js_api_Request request;
	request.set_funcname(funcName);
	request.set_params(params);

	grpc_js_api_Reply reply;
	grpc::ClientContext context;
	grpc::Status status = stub_->com_grpc_js_api(&context, request, &reply);

	if (!status.ok())
		return m_connected = false;

	return true;
}

// Grpc
//

GrpcProxy::GrpcProxy() {

}

GrpcProxy::~GrpcProxy() {

}

bool GrpcProxy::startServer(int32_t listenPort)
{
	// Don't repeat
	if (m_listenPort != 0)
		return false;

	m_listenPort = listenPort;

	grpc::EnableDefaultHealthCheckService(true);
	grpc::reflection::InitProtoReflectionServerBuilderPlugin();

	m_builder = std::make_unique<grpc::ServerBuilder>();
	m_builder->AddListeningPort(std::string("localhost:") + std::to_string(m_listenPort), grpc::InsecureServerCredentials());

	m_serverObj = std::make_unique<grpc_proxy_objImpl>();
	m_builder->RegisterService(m_serverObj.get());

	m_server = m_builder->BuildAndStart();

	return m_server != nullptr;
}

bool GrpcProxy::connectToClient(int32_t portNumber)
{
	m_clientObj = std::make_unique<grpc_proxy_objClient>(
		grpc::CreateChannel("localhost:" + std::to_string(portNumber), grpc::InsecureChannelCredentials()));

	return m_clientObj != nullptr;
}

void GrpcProxy::stop() {

}
