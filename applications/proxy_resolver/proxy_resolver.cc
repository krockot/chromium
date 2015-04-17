// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "applications/proxy_resolver/proxy_resolver.h"

#include "base/logging.h"
#include "net/proxy/mojo_proxy_resolver_factory_impl.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"

namespace proxy_resolver {

namespace {

void CreateProxyResolverFactory(
    mojo::InterfaceRequest<net::interfaces::ProxyResolverFactory> request) {
  // MojoProxyResolverFactoryImpl is strongly bound to the Mojo message pipe it
  // is connected to. When that message pipe is closed, either explicitly on the
  // other end (in the browser process), or by a connection error, this object
  // will be destroyed.
  new net::MojoProxyResolverFactoryImpl(request.Pass());
}

}  // namespace

ProxyResolver::ProxyResolver() {
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
  connection->AddService(base::Bind(&CreateProxyResolverFactory));
  connection->ExposeServices();
}

void ProxyResolver::Quit() {
}

}  // namespace proxy_resolver
