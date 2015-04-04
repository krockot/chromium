// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/shell/shell_impl.h"

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "core/application_host/application_host_impl.h"
#include "core/common/application_connection_impl.h"
#include "core/process/shell_process.h"
#include "core/public/common/core_client.h"
#include "core/public/interfaces/shell_private.mojom.h"
#include "url/gurl.h"

namespace core {

namespace {

// TODO(core): Kill this global ref to Shell.
static Shell* g_shell;

}  // namespace

Shell* Shell::Get() {
  return g_shell;
}

class ShellImpl::ShellPrivateImpl : public ShellPrivate {
 public:
  ShellPrivateImpl(ShellImpl* shell,
                   mojo::InterfaceRequest<ShellPrivate> request);
  ~ShellPrivateImpl() override;

 private:
  // ShellPrivate:
  void GetShellProcess(mojo::InterfaceRequest<Process> shell_process) override;
  void CreateProcess(mojo::InterfaceRequest<Process> process,
                     SandboxDelegatePtr sandbox_delegate) override;

  ShellImpl* shell_;
  mojo::Binding<ShellPrivate> binding_;
};

ShellImpl::ShellPrivateImpl::ShellPrivateImpl(
    ShellImpl* shell,
    mojo::InterfaceRequest<ShellPrivate> request)
    : shell_(shell),
      binding_(this, request.Pass()) {
}

ShellImpl::ShellPrivateImpl::~ShellPrivateImpl() {
}

void ShellImpl::ShellPrivateImpl::GetShellProcess(
    mojo::InterfaceRequest<Process> shell_process) {
  shell_->process_->BindRequest(shell_process.Pass());
}

void ShellImpl::ShellPrivateImpl::CreateProcess(
    mojo::InterfaceRequest<Process> process,
    SandboxDelegatePtr sandbox_delegate) {
  NOTIMPLEMENTED();
}

// TODO(core): Move application instance tracking over to init.
class ShellImpl::ApplicationInstance : public mojo::ErrorHandler,
                                       public mojo::Shell {
 public:
  ApplicationInstance(ShellImpl* shell, const GURL& url);
  ~ApplicationInstance() override;

  void Initialize();

  mojo::Application* application() { return proxy_.get(); }

 private:
  // mojo::Shell:
  void ConnectToApplication(
      const mojo::String& application_url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services) override;

  // mojo::ErrorHandler:
  void OnConnectionError() override;

  void OnApplicationLaunched(LaunchError result);

  ShellImpl* shell_;
  GURL url_;
  mojo::Binding<mojo::Shell> binding_;
  mojo::ApplicationPtr proxy_;

  base::WeakPtrFactory<ApplicationInstance> weak_factory_;
};

ShellImpl::ApplicationInstance::ApplicationInstance(ShellImpl* shell,
                                                    const GURL& url)
    : shell_(shell), url_(url), binding_(this), weak_factory_(this) {
  binding_.set_error_handler(this);
  shell_->in_process_application_host_->LaunchApplication(
      mojo::String::From(url_.spec()),
      mojo::GetProxy(&proxy_),
      base::Bind(&ApplicationInstance::OnApplicationLaunched,
                 weak_factory_.GetWeakPtr()));
  mojo::ScopedMessagePipeHandle handle(proxy_.PassMessagePipe());
  proxy_.Bind(handle.Pass());
}

ShellImpl::ApplicationInstance::~ApplicationInstance() {
}

void ShellImpl::ApplicationInstance::Initialize() {
  mojo::ShellPtr shell_proxy;
  binding_.Bind(mojo::GetProxy(&shell_proxy));
  proxy_->Initialize(shell_proxy.Pass(), mojo::Array<mojo::String>::New(0),
                     mojo::String::From(url_.spec()));
}

void ShellImpl::ApplicationInstance::OnConnectionError() {
  shell_->DestroyApplicationInstance(url_.spec());
}

void ShellImpl::ApplicationInstance::OnApplicationLaunched(LaunchError result) {
  if (result != LAUNCH_ERROR_OK) {
    LOG(ERROR) << "Failed to launch " << url_.spec() << ": " << result;
    shell_->DestroyApplicationInstance(url_.spec());
  }
}

void ShellImpl::ApplicationInstance::ConnectToApplication(
    const mojo::String& application_url,
    mojo::InterfaceRequest<mojo::ServiceProvider> services,
    mojo::ServiceProviderPtr exposed_services) {
  shell_->Connect(GURL(application_url.To<std::string>()), services.Pass(),
                  exposed_services.Pass(), url_);
}

scoped_ptr<Shell> Shell::Create() {
  g_shell = new ShellImpl;
  return scoped_ptr<Shell>(g_shell);
}

ShellImpl::ShellImpl()
    : process_(new ShellProcess),
      private_services_binding_(this),
      weak_factory_(this) {
  process_->BindRequest(mojo::GetProxy(&process_proxy_));
  process_proxy_->GetApplicationHost(
      mojo::GetProxy(&in_process_application_host_));

  mojo::ServiceProviderPtr private_services;
  private_services_binding_.Bind(mojo::GetProxy(&private_services));
  Connect(GURL("system:init"), mojo::GetProxy(&init_services_),
      private_services.Pass(), GURL("system:shell"));
}

ShellImpl::~ShellImpl() {
  STLDeleteContainerPairSecondPointers(running_applications_.begin(),
                                       running_applications_.end());
}

void ShellImpl::ConnectToService(
    const mojo::String& interface_name,
    mojo::ScopedMessagePipeHandle handle) {
  if (interface_name != "ShellPrivate") {
    LOG(ERROR) << "Invalid interface request from system:init: "
               << interface_name;
    return;
  }

  DCHECK(!private_);
  private_.reset(new ShellPrivateImpl(
          this, mojo::MakeRequest<ShellPrivate>(handle.Pass())));
}

ShellImpl::ApplicationInstance* ShellImpl::Launch(const GURL& url) {
  DCHECK(running_applications_.find(url.spec()) == running_applications_.end());
  ApplicationInstance* instance = new ApplicationInstance(this, url);
  running_applications_[url.spec()] = instance;
  instance->Initialize();
  return instance;
}

void ShellImpl::Connect(const GURL& to_url,
                        mojo::InterfaceRequest<mojo::ServiceProvider> services,
                        mojo::ServiceProviderPtr exposed_services,
                        const GURL& from_url) {
  // TODO(core): Delegate connection behavior to init.
  auto iter = running_applications_.find(to_url.spec());
  ApplicationInstance* instance = nullptr;
  if (iter != running_applications_.end()) {
    instance = iter->second;
  } else {
    VLOG(1) << "Target application " << to_url.spec()
            << " not running. Launching now.";
    instance = Launch(to_url);
  }
  instance->application()->AcceptConnection(
      mojo::String::From(from_url.spec()), services.Pass(),
      exposed_services.Pass(), mojo::String::From(to_url.spec()));
}

void ShellImpl::DestroyApplicationInstance(const std::string& url_spec) {
  auto iter = running_applications_.find(url_spec);
  DCHECK(iter != running_applications_.end());
  running_applications_.erase(iter);
  delete iter->second;
}

}  // namespace core
