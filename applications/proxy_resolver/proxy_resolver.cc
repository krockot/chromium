// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "applications/proxy_resolver/proxy_resolver.h"

#include "base/logging.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"

namespace proxy_resolver {

class ProxyResolver::DummyService : public core::Dummy {
 public:
  DummyService(mojo::InterfaceRequest<core::Dummy> request)
      : binding_(this, request.Pass()) {}

  ~DummyService() override {}

 private:
  void DoSomething(const DoSomethingCallback& callback) override {
    callback.Run();
  }

  mojo::Binding<core::Dummy> binding_;
};

ProxyResolver::ProxyResolver() : weak_factory_(this) {
}

ProxyResolver::~ProxyResolver() {
}

void ProxyResolver::InitializeApplication(
    mojo::Shell* shell,
    const std::vector<std::string>& args) {
}

void ProxyResolver::AcceptConnection(const GURL& requestor_url,
                                     core::ApplicationConnection* connection,
                                     const GURL& resolved_url) {
  connection->AddService(base::Bind(&ProxyResolver::CreateDummyService,
                                    weak_factory_.GetWeakPtr()));
  connection->ExposeServices();
}

void ProxyResolver::Quit() {
}

void ProxyResolver::CreateDummyService(
    mojo::InterfaceRequest<core::Dummy> request) {
  DCHECK(!dummy_service_);
  dummy_service_.reset(new DummyService(request.Pass()));
}

}  // namespace proxy_resolver
