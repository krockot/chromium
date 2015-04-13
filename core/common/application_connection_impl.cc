// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/common/application_connection_impl.h"

#include "core/common/service_provider_impl.h"
#include "url/gurl.h"

namespace core {

// static
scoped_ptr<ApplicationConnection> ApplicationConnection::Create(
    mojo::Shell* shell,
    const GURL& url) {
  mojo::ServiceProviderPtr exposed_services;
  mojo::InterfaceRequest<mojo::ServiceProvider> request =
      mojo::GetProxy(&exposed_services);
  mojo::ServiceProviderPtr remote_services;
  shell->ConnectToApplication(mojo::String::From(url.spec()),
                              mojo::GetProxy(&remote_services),
                              exposed_services.Pass());
  return scoped_ptr<ApplicationConnection>(
      new ApplicationConnectionImpl(request.Pass(), remote_services.Pass()));
}

ApplicationConnectionImpl::ApplicationConnectionImpl(
    mojo::InterfaceRequest<mojo::ServiceProvider> request,
    mojo::ServiceProviderPtr remote_services)
    : error_handler_(nullptr),
      pending_request_(request.Pass()),
      services_(new ServiceProviderImpl()),
      remote_services_(remote_services.Pass()) {
  services_->SetErrorHandler(this);
  remote_services_.set_error_handler(this);
}

ApplicationConnectionImpl::~ApplicationConnectionImpl() {
}

void ApplicationConnectionImpl::AddService(
    const std::string& name,
    const ServiceProvider::RequestHandler& handler) {
  services_->RegisterService(name, handler);
}

void ApplicationConnectionImpl::ExposeServices() {
  if (pending_request_.is_pending()) {
    services_->BindToRequest(pending_request_.Pass());
  }
}

void ApplicationConnectionImpl::ConnectToService(
    const std::string& name,
    mojo::ScopedMessagePipeHandle handle) {
  if (!remote_services_) {
    VLOG(1) << "Attempting to connect to service " << name << " with no pipe.";
    return;
  }
  remote_services_->ConnectToService(name, handle.Pass());
}

void ApplicationConnectionImpl::SetErrorHandler(
    mojo::ErrorHandler* error_handler) {
  DCHECK(!error_handler_);
  error_handler_ = error_handler;
}

void ApplicationConnectionImpl::OnConnectionError() {
  if (error_handler_)
    error_handler_->OnConnectionError();
}

}  // namespace core
