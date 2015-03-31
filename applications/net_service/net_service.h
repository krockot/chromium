// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATIONS_NET_SERVICE_NET_SERVICE_APPLICATION_DELEGATE_H_
#define APPLICATIONS_NET_SERVICE_NET_SERVICE_APPLICATION_DELEGATE_H_

#include <map>

#include "base/memory/scoped_vector.h"
#include "core/public/application/application_delegate.h"
#include "net/dns/mojo_host_resolver_impl.h"
#include "net/interfaces/host_resolver_service.mojom.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"

namespace net {
class HostResolver;
class NetLog;
}

namespace net_service {

class NetService : public core::ApplicationDelegate {
 public:
  NetService();
  ~NetService() override;

  // core::ApplicationDelegate:
  void InitializeApplication(mojo::Shell* shell,
                             const std::vector<std::string>& args) override;
  void AcceptConnection(const GURL& requestor_url,
                        scoped_ptr<core::ApplicationConnection> connection,
                        const GURL& resolved_url) override;
  void Quit() override;

 private:
  void InitializeConnection(scoped_ptr<core::ApplicationConnection> connection);

  struct ActiveConnection {
    ActiveConnection(scoped_ptr<core::ApplicationConnection> connection);
    ActiveConnection(ActiveConnection&& other);
    ActiveConnection(const ActiveConnection&) = delete;
    ~ActiveConnection();

    ActiveConnection& operator=(ActiveConnection&& other);
    ActiveConnection& operator=(const ActiveConnection&) = delete;

    scoped_ptr<core::ApplicationConnection> connection;
    ScopedVector<mojo::Binding<net::interfaces::HostResolver>> resolvers;
  };

  void CreateHostResolverService(
      ActiveConnection* connection,
      mojo::InterfaceRequest<net::interfaces::HostResolver> request);

  scoped_ptr<net::NetLog> net_log_;
  scoped_ptr<net::HostResolver> host_resolver_;
  scoped_ptr<net::MojoHostResolverImpl> mojo_host_resolver_;

  int next_connection_id_;
  std::map<int, ActiveConnection> connections_;
};

}  // namespace net_service

#endif  // APPLICATIONS_NET_SERVICE_NET_SERVICE_APPLICATION_DELEGATE_H_
