// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/shell/shell_impl.h"

#include "base/callback.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "core/common/application_connection_impl.h"
#include "core/process/shell_process.h"
#include "core/public/shell/core_shell_client.h"
#include "url/gurl.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/error_handler.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/string.h"

namespace core {

namespace {

// TODO(rockot): Remove this along with core::Shell::GetProxy().
static ShellImpl* g_shell;

const char kShellUrl[] = "system:shell";
const char kSystemBrowserUrl[] = "system:browser";

// Lightweight |mojo::ErrorHandler| implementation that forwards to a callback.
class SimpleErrorHandler : public mojo::ErrorHandler {
 public:
  explicit SimpleErrorHandler(const base::Closure& callback)
      : callback_(callback) {
  }

  ~SimpleErrorHandler() override {}

 private:
  void OnConnectionError() override { callback_.Run(); }

  base::Closure callback_;
};

// Shell interface exposed globally to virtual browser app.
class BrowserShell : public mojo::Shell {
 public:
  BrowserShell(ShellImpl* shell, mojo::InterfaceRequest<mojo::Shell> request)
      : shell_(shell), binding_(this, request.Pass()) {
  }
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

//
class ShellPrivateService : public ShellPrivate {
 public:
  ShellPrivateService(ShellImpl* shell,
                      mojo::InterfaceRequest<ShellPrivate> request)
      : shell_(shell), binding_(this, request.Pass()) {
  }
  ~ShellPrivateService() override {}

 private:
  // ShellPrivate:
  void GetShellProcess(mojo::InterfaceRequest<Process> shell_process) override {
    shell_->GetShellProcess(shell_process.Pass());
  }

  void CreateProcess(mojo::InterfaceRequest<Process> process,
                     SandboxDelegatePtr sandbox_delegate) override {
    // TODO(CORE): This.
    NOTIMPLEMENTED();
  }

  ShellImpl* shell_;
  mojo::Binding<ShellPrivate> binding_;
};

}  // namespace

mojo::Shell* Shell::GetProxy() {
  return g_shell->browser_shell_proxy();
}

scoped_ptr<Shell> Shell::Create() {
  g_shell = new ShellImpl;
  return scoped_ptr<Shell>(g_shell);
}

ShellImpl::ShellImpl()
    : process_(new ShellProcess),
      browser_shell_(
          new BrowserShell(this, mojo::GetProxy(&browser_shell_proxy_))),
      weak_factory_(this) {
  fatal_error_handler_.reset(new SimpleErrorHandler(
      base::Bind(&ShellImpl::OnFatalError, weak_factory_.GetWeakPtr())));

  process_->BindRequest(mojo::GetProxy(&process_proxy_));
  process_proxy_.set_error_handler(fatal_error_handler_.get());
  process_proxy_->GetApplicationHost(
      mojo::GetProxy(&in_process_application_host_));
  in_process_application_host_.set_error_handler(fatal_error_handler_.get());

  mojo::String startup_url =
      mojo::String::From(core::CoreShellClient::Get()->GetStartupURL().spec());
  in_process_application_host_->LaunchApplication(
      startup_url, mojo::GetProxy(&startup_application_proxy_),
      base::Bind(&ShellImpl::OnStartupApplicationLaunched,
                 weak_factory_.GetWeakPtr()));
  startup_application_proxy_.set_error_handler(fatal_error_handler_.get());

  mojo::ServiceProviderPtr startup_application_services;
  mojo::ServiceProviderPtr shell_services;
  mojo::InterfaceRequest<mojo::ServiceProvider> shell_services_request =
      mojo::GetProxy(&shell_services);
  startup_application_proxy_->AcceptConnection(
      mojo::String::From(kShellUrl),
      mojo::GetProxy(&startup_application_services), shell_services.Pass(),
      startup_url);

  startup_application_connection_.reset(new ApplicationConnectionImpl(
      shell_services_request.Pass(), startup_application_services.Pass()));
  startup_application_connection_->ApplicationConnection::AddService(
      base::Bind(&ShellImpl::CreatePrivateService, weak_factory_.GetWeakPtr()));
  startup_application_connection_->ExposeServices();

  application_broker_ =
      startup_application_connection_->ConnectToService<ApplicationBroker>();
  application_broker_.set_error_handler(fatal_error_handler_.get());
}

ShellImpl::~ShellImpl() {
}

void ShellImpl::GetShellProcess(mojo::InterfaceRequest<Process> request) {
  process_->BindRequest(request.Pass());
}

void ShellImpl::ConnectApplications(
    const GURL& to_url,
    mojo::InterfaceRequest<mojo::ServiceProvider> services,
    mojo::ServiceProviderPtr exposed_services,
    const GURL& from_url) {
  DCHECK(application_broker_);
  application_broker_->ConnectApplications(
      mojo::String::From(to_url.spec()), services.Pass(),
      exposed_services.Pass(), mojo::String::From(from_url.spec()));
}

void ShellImpl::CreatePrivateService(
    mojo::InterfaceRequest<core::ShellPrivate> request) {
  DCHECK(!private_service_);
  private_service_.reset(new ShellPrivateService(this, request.Pass()));
}

void ShellImpl::OnStartupApplicationLaunched(LaunchError result) {
  // If this application fails to launch, it's curtains for everyone.
  CHECK(result == LAUNCH_ERROR_OK);
}

void ShellImpl::OnFatalError() {
  NOTREACHED();
}

}  // namespace core
