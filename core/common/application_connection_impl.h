// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_COMMON_APPLICATION_CONNECTION_IMPL_H_
#define CORE_COMMON_APPLICATION_CONNECTION_IMPL_H_

#include "core/public/common/application_connection.h"
#include "core/public/common/service_provider.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/service_provider.mojom.h"

namespace core {

class ApplicationConnectionImpl : public ApplicationConnection {
 public:
  ApplicationConnectionImpl(
      mojo::InterfaceRequest<mojo::ServiceProvider> request,
      mojo::ServiceProviderPtr remote_services);
  ~ApplicationConnectionImpl() override;

  // ApplicationConnection:
  void ExposeService(const std::string& name,
                     const ServiceProvider::RequestHandler& handler) override;
  void ConnectToService(const std::string& name,
                        mojo::ScopedMessagePipeHandle handle) override;

 private:
  scoped_ptr<ServiceProvider> services_;
  mojo::ServiceProviderPtr remote_services_;
};

}  // namespace core

#endif  // CORE_COMMON_APPLICATION_CONNECTION_IMPL_H_
