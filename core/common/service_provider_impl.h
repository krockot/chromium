// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_COMMON_SERVICE_PROVIDER_IMPL_H_
#define CORE_COMMON_SERVICE_PROVIDER_IMPL_H_

#include "base/bind.h"
#include "core/public/common/service_provider.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/service_provider.mojom.h"

namespace core {

class ServiceProviderImpl : public ServiceProvider,
                            public mojo::ServiceProvider {
 public:
  template <typename Interface>
  using ServiceFactory =
      base::Callback<void(mojo::InterfaceRequest<Interface>)>;

  ServiceProviderImpl(mojo::InterfaceRequest<mojo::ServiceProvider> request);
  ~ServiceProviderImpl() override;

  // ServiceProvider:
  void RegisterService(const std::string& name,
                       const RequestHandler& handler) override;

 private:
  // mojo::ServiceProvider:
  void ConnectToService(const mojo::String& name,
                        mojo::ScopedMessagePipeHandle handle) override;

  mojo::Binding<mojo::ServiceProvider> binding_;
  std::map<std::string, RequestHandler> request_handlers_;
};

}  // namespace core

#endif  // CORE_COMMON_SERVICE_PROVIDER_IMPL_H_
