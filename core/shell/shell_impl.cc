// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/shell/shell_impl.h"

#include "base/callback.h"
#include "base/logging.h"
#include "core/application_host/application_host_impl.h"
#include "core/public/common/application_urls.h"
#include "core/shell/application_manager.h"
#include "url/gurl.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/error_handler.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/string.h"

namespace core {

namespace {

// TODO(CORE): Remove this along with core::Shell::GetProxy().
static ShellImpl* g_shell;

// Shell interface exposed globally to virtual browser app.
class BrowserShell : public mojo::Shell {
 public:
  BrowserShell(ShellImpl* shell, mojo::InterfaceRequest<mojo::Shell> request)
      : shell_(shell), binding_(this, request.Pass()) {}
  ~BrowserShell() override {}

 private:
  // mojo::Shell:
  void ConnectToApplication(
      const mojo::String& url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services) override {
    shell_->ConnectApplications(GURL(url.To<std::string>()), services.Pass(),
                                exposed_services.Pass(),
                                GURL(kSystemBrowserUrl));
  }

  ShellImpl* shell_;
  mojo::Binding<mojo::Shell> binding_;
};

}  // namespace

mojo::Shell* Shell::GetProxy() {
  return g_shell->browser_shell_proxy();
}

scoped_ptr<Shell> Shell::Create(
    scoped_refptr<base::TaskRunner> io_task_runner) {
  g_shell = new ShellImpl(io_task_runner);
  return scoped_ptr<Shell>(g_shell);
}

ShellImpl::ShellImpl(scoped_refptr<base::TaskRunner> io_task_runner)
    : browser_shell_(
          new BrowserShell(this, mojo::GetProxy(&browser_shell_proxy_))),
      application_manager_(new ApplicationManager(this, io_task_runner)),
      weak_factory_(this) {
}

ShellImpl::~ShellImpl() {
}

void ShellImpl::ConnectApplications(
    const GURL& to_url,
    mojo::InterfaceRequest<mojo::ServiceProvider> services,
    mojo::ServiceProviderPtr exposed_services,
    const GURL& from_url) {
  application_manager_->ConnectApplications(to_url, services.Pass(),
                                            exposed_services.Pass(), from_url);
}

}  // namespace core
