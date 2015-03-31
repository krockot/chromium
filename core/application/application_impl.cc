// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application/application_impl.h"

#include "base/message_loop/message_loop.h"
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

ApplicationImpl::ApplicationImpl(
    scoped_ptr<ApplicationDelegate> delegate,
    mojo::InterfaceRequest<mojo::Application> request)
    : delegate_(delegate.Pass()),
      binding_(this, request.Pass()) {
  binding_.set_error_handler(this);
}

ApplicationImpl::~ApplicationImpl() {
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
  scoped_ptr<ApplicationConnection> connection(
      new ApplicationConnectionImpl(services.Pass(), exposed_services.Pass()));
  delegate_->AcceptConnection(GURL(requestor_url.To<std::string>()),
                              connection.Pass(),
                              GURL(resolved_url.To<std::string>()));
}

void ApplicationImpl::RequestQuit() {
  delegate_->Quit();
  FOR_EACH_OBSERVER(Observer, observers_, OnQuit());
  Terminate();
}

void ApplicationImpl::OnConnectionError() {
  FOR_EACH_OBSERVER(Observer, observers_, OnConnectionError());
  Terminate();
}

void ApplicationImpl::Terminate() {
  if (base::MessageLoop::current()->is_running())
    base::MessageLoop::current()->Quit();
}

}  // namespace core
