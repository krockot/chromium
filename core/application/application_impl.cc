// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application/application_impl.h"

#include "base/stl_util.h"
#include "core/common/application_connection_impl.h"
#include "url/gurl.h"

namespace core {

// static
scoped_ptr<Application> Application::Create(
    scoped_ptr<ApplicationDelegate> delegate,
    mojo::InterfaceRequest<mojo::Application> request) {
  return make_scoped_ptr<Application>(
      new ApplicationImpl(delegate.Pass(), request.Pass()));
}

class ApplicationImpl::InboundConnection : public mojo::ErrorHandler {
 public:
  InboundConnection(scoped_ptr<ApplicationConnectionImpl> connection,
                    const base::Closure& error_closure);
  ~InboundConnection() override;

 private:
  // mojo::ErrorHandler:
  void OnConnectionError() override;

  scoped_ptr<ApplicationConnectionImpl> connection_;
  base::Closure error_closure_;
};

ApplicationImpl::InboundConnection::InboundConnection(
    scoped_ptr<ApplicationConnectionImpl> connection,
    const base::Closure& error_closure)
    : connection_(connection.Pass()), error_closure_(error_closure) {
  connection_->SetErrorHandler(this);
}

ApplicationImpl::InboundConnection::~InboundConnection() {
}

void ApplicationImpl::InboundConnection::OnConnectionError() {
  error_closure_.Run();
}

ApplicationImpl::ApplicationImpl(
    scoped_ptr<ApplicationDelegate> delegate,
    mojo::InterfaceRequest<mojo::Application> request)
    : delegate_(delegate.Pass()),
      binding_(this, request.Pass()),
      next_connection_id_(0),
      weak_factory_(this) {
  binding_.set_error_handler(this);
}

ApplicationImpl::~ApplicationImpl() {
  STLDeleteContainerPairSecondPointers(inbound_connections_.begin(),
                                       inbound_connections_.end());
}

void ApplicationImpl::Init(const std::vector<std::string>& args) {
  delegate_->InitializeApplication(shell_proxy_.get(), args);
}

void ApplicationImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ApplicationImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ApplicationImpl::Initialize(mojo::ShellPtr shell,
                                 mojo::Array<mojo::String> args,
                                 const mojo::String& url) {
  shell_proxy_ = shell.Pass();
  Init(args.To<std::vector<std::string>>());
}

void ApplicationImpl::AcceptConnection(
    const mojo::String& requestor_url,
    mojo::InterfaceRequest<mojo::ServiceProvider> services,
    mojo::ServiceProviderPtr exposed_services,
    const mojo::String& resolved_url) {
  scoped_ptr<ApplicationConnectionImpl> connection(
      new ApplicationConnectionImpl(services.Pass(), exposed_services.Pass()));
  delegate_->AcceptConnection(GURL(requestor_url.To<std::string>()),
                              connection.get(),
                              GURL(resolved_url.To<std::string>()));
  int64_t id = next_connection_id_++;
  inbound_connections_.insert(std::make_pair(
      id, new InboundConnection(connection.Pass(),
                                base::Bind(&ApplicationImpl::PurgeConnection,
                                           weak_factory_.GetWeakPtr(), id))));
}

void ApplicationImpl::RequestQuit() {
  delegate_->Quit();
  FOR_EACH_OBSERVER(Observer, observers_, OnQuit());
}

void ApplicationImpl::OnConnectionError() {
  RequestQuit();
}

void ApplicationImpl::PurgeConnection(int64_t id) {
  auto iter = inbound_connections_.find(id);
  DCHECK(iter != inbound_connections_.end());
  delete iter->second;
  inbound_connections_.erase(iter);
}

}  // namespace core
