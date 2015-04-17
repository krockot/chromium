// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATIONS_PROXY_RESOLVER_PROXY_RESOLVER_H_
#define APPLICATIONS_PROXY_RESOLVER_PROXY_RESOLVER_H_

#include "base/macros.h"
#include "core/public/application/application_delegate.h"

namespace proxy_resolver {

class ProxyResolver : public core::ApplicationDelegate {
 public:
  ProxyResolver();
  ~ProxyResolver() override;

 private:
  class DummyService;

  // core::ApplicationDelegate:
  void InitializeApplication(mojo::Shell* shell,
                             const std::vector<std::string>& args) override;
  void AcceptConnection(const GURL& requestor_url,
                        core::ApplicationConnection* connection,
                        const GURL& resolved_url) override;
  void Quit() override;

  DISALLOW_COPY_AND_ASSIGN(ProxyResolver);
};

}  // namespace proxy_resolver

#endif  // APPLICATIONS_PROXY_RESOLVER_PROXY_RESOLVER_H_
