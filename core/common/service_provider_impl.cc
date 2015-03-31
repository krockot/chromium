// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/common/service_provider_impl.h"

#include "base/logging.h"

namespace core {

ServiceProviderImpl::ServiceProviderImpl(
    mojo::InterfaceRequest<mojo::ServiceProvider> request)
    : binding_(this, request.Pass()) {
}

ServiceProviderImpl::~ServiceProviderImpl() {
}

void ServiceProviderImpl::RegisterService(const std::string& name,
                                          const RequestHandler& handler) {
  auto result = request_handlers_.insert(std::make_pair(name, handler));
  DCHECK(result.second) << "Registered duplicate handler: " << name;
}

void ServiceProviderImpl::ConnectToService(
    const mojo::String& name,
    mojo::ScopedMessagePipeHandle handle) {
  const auto& iter = request_handlers_.find(name.To<std::string>());
  if (iter == request_handlers_.end()) {
    VLOG(1) << "Attempt to connect to unregistered service: " << name;
    return;
  }
  iter->second.Run(handle.Pass());
}

}  // namespace core
