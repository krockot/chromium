// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_COMMON_APPLICATION_CONNECTION_IMPL_H_
#define CORE_COMMON_APPLICATION_CONNECTION_IMPL_H_

#include "base/macros.h"
#include "core/public/common/application_connection.h"
#include "core/public/common/service_provider.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/error_handler.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/service_provider.mojom.h"

namespace core {

class ServiceProviderImpl;

class ApplicationConnectionImpl : public ApplicationConnection,
                                  public mojo::ErrorHandler {
 public:
  ApplicationConnectionImpl(
      mojo::InterfaceRequest<mojo::ServiceProvider> request,
      mojo::ServiceProviderPtr remote_services);
  ~ApplicationConnectionImpl() override;

  // ApplicationConnection:
  void AddService(const std::string& name,
                  const ServiceProvider::RequestHandler& handler) override;
  void ExposeServices() override;
  void ConnectToService(const std::string& name,
                        mojo::ScopedMessagePipeHandle handle) override;

  void SetErrorHandler(mojo::ErrorHandler* error_handler);

 private:
  // mojo::ErrorHandler:
  void OnConnectionError() override;

  mojo::ErrorHandler* error_handler_;
  mojo::InterfaceRequest<mojo::ServiceProvider> pending_request_;
  scoped_ptr<ServiceProviderImpl> services_;
  mojo::ServiceProviderPtr remote_services_;

  DISALLOW_COPY_AND_ASSIGN(ApplicationConnectionImpl);
};

}  // namespace core

#endif  // CORE_COMMON_APPLICATION_CONNECTION_IMPL_H_
