// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "applications/net_service/net_service.h"

#include "base/bind.h"
#include "base/logging.h"
#include "core/public/application/macros.h"
#include "net/base/net_log.h"
#include "net/dns/host_resolver.h"
#include "url/gurl.h"

namespace net_service {

NetService::NetService()
    : net_log_(new net::NetLog),
      host_resolver_(net::HostResolver::CreateDefaultResolver(net_log_.get())),
      mojo_host_resolver_(new net::MojoHostResolverImpl(host_resolver_.get())),
      next_connection_id_(0) {
}

NetService::~NetService() {
}

void NetService::InitializeApplication(mojo::Shell* shell,
                                       const std::vector<std::string>& args) {
}

void NetService::AcceptConnection(
    const GURL& requestor_url,
    scoped_ptr<core::ApplicationConnection> connection,
    const GURL& resolved_url) {
  InitializeConnection(connection.Pass());
}

void NetService::Quit() {
}

void NetService::InitializeConnection(
    scoped_ptr<core::ApplicationConnection> connection) {
  int id = next_connection_id_++;
  auto result = connections_.insert(
      std::make_pair(id, ActiveConnection(connection.Pass())));
  DCHECK(result.second);
  ActiveConnection& active_connection = result.first->second;

  active_connection.connection->ExposeService(
      base::Bind(&NetService::CreateHostResolverService, base::Unretained(this),
                 base::Unretained(&active_connection)));
}

void NetService::CreateHostResolverService(
    ActiveConnection* connection,
    mojo::InterfaceRequest<net::interfaces::HostResolver> request) {
  connection->resolvers.push_back(
      new mojo::Binding<net::interfaces::HostResolver>(
          mojo_host_resolver_.get(), request.Pass()));
}

NetService::ActiveConnection::ActiveConnection(
    scoped_ptr<core::ApplicationConnection> connection)
    : connection(connection.Pass()) {
}

NetService::ActiveConnection::ActiveConnection(
    NetService::ActiveConnection&& other)
    : connection(other.connection.Pass()) {
}

NetService::ActiveConnection::~ActiveConnection() {
}

NetService::ActiveConnection& NetService::ActiveConnection::operator=(
    NetService::ActiveConnection&& other) {
  connection = other.connection.Pass();
  return *this;
}

}  // namespace net_service

APPLICATION_MAIN(net_service::NetService)
