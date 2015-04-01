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
#include "core/public/common/core_client.h"
#include "url/gurl.h"

namespace core {

namespace {

// TODO(core): Kill this global ref to Shell.
static Shell* g_shell;

}  // namespace

Shell* Shell::Get() {
  return g_shell;
}

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

  ShellImpl* shell_;
  GURL url_;
  mojo::Binding<mojo::Shell> binding_;
  mojo::ApplicationPtr proxy_;
};

ShellImpl::ApplicationInstance::ApplicationInstance(ShellImpl* shell,
                                                    const GURL& url)
    : shell_(shell), url_(url), binding_(this) {
  binding_.set_error_handler(this);
  shell_->in_process_application_host_->LaunchApplication(
      url_, mojo::GetProxy(&proxy_));
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

ShellImpl::ShellImpl() : in_process_application_host_(new ApplicationHostImpl) {
}

ShellImpl::~ShellImpl() {
  STLDeleteContainerPairSecondPointers(running_applications_.begin(),
                                       running_applications_.end());
}

void ShellImpl::Launch(const GURL& url) {
  DCHECK(running_applications_.find(url.spec()) == running_applications_.end());
  ApplicationInstance* instance = new ApplicationInstance(this, url);
  running_applications_[url.spec()] = instance;
  instance->Initialize();
}

void ShellImpl::Connect(const GURL& to_url,
                        mojo::InterfaceRequest<mojo::ServiceProvider> services,
                        mojo::ServiceProviderPtr exposed_services,
                        const GURL& from_url) {
  auto iter = running_applications_.find(to_url.spec());
  if (iter == running_applications_.end()) {
    VLOG(1) << "Target application " << to_url.spec()
            << " not running. Launching now.";
    Launch(to_url);
  }

  iter = running_applications_.find(to_url.spec());
  if (iter == running_applications_.end()) {
    LOG(ERROR) << "Unable to launch target application: " << to_url.spec();
    return;
  }

  ApplicationInstance* instance = iter->second;
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
