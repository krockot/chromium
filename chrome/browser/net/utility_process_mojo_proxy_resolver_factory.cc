// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/utility_process_mojo_proxy_resolver_factory.h"

#include "base/logging.h"
#include "base/memory/singleton.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_thread.h"
#include "core/public/common/application_connection.h"
#include "core/public/common/application_urls.h"
#include "core/public/shell/shell.h"
#include "net/proxy/mojo_proxy_resolver_factory.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

// static
UtilityProcessMojoProxyResolverFactory*
UtilityProcessMojoProxyResolverFactory::GetInstance() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  return Singleton<
      UtilityProcessMojoProxyResolverFactory,
      LeakySingletonTraits<UtilityProcessMojoProxyResolverFactory>>::get();
}

UtilityProcessMojoProxyResolverFactory::
    UtilityProcessMojoProxyResolverFactory() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
}

UtilityProcessMojoProxyResolverFactory::
    ~UtilityProcessMojoProxyResolverFactory() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
}

void UtilityProcessMojoProxyResolverFactory::Create(
    mojo::InterfaceRequest<net::interfaces::ProxyResolver> req,
    net::interfaces::HostResolverPtr host_resolver) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  if (!resolver_factory_) {
    application_connection_ = core::ApplicationConnection::Create(
        core::Shell::GetProxy(), GURL(core::kSystemProxyResolverUrl));
    application_connection_
        ->ConnectToService<net::interfaces::ProxyResolverFactory>(
            &resolver_factory_);
    resolver_factory_.set_error_handler(this);
  }
  resolver_factory_->CreateResolver(req.Pass(), host_resolver.Pass());
}

void UtilityProcessMojoProxyResolverFactory::OnConnectionError() {
  DVLOG(1) << "Disconnection from utility process detected";
  resolver_factory_.reset();
}
