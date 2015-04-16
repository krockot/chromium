// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/shell/application_manager.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "core/shell/shell_impl.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/string.h"
#include "url/gurl.h"

namespace core {

namespace {

using ExitCallback = base::Callback<void(int32 exit_code)>;

}

class ApplicationManager::RunningApplication : public mojo::ErrorHandler,
                                               public mojo::Shell {
 public:
  RunningApplication(ShellImpl* shell,
                     const GURL& url,
                     mojo::ApplicationPtr proxy,
                     const ExitCallback& exit_callback)
      : shell_(shell),
        shell_binding_(this),
        url_(url),
        proxy_(proxy.Pass()),
        exit_callback_(exit_callback),
        weak_factory_(this) {
  }
  ~RunningApplication() override {}

  base::WeakPtr<RunningApplication> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  void Connect(const GURL& from_url,
               mojo::InterfaceRequest<mojo::ServiceProvider> services,
               mojo::ServiceProviderPtr exposed_services) {
    proxy_->AcceptConnection(
        mojo::String::From(from_url.spec()), services.Pass(),
        exposed_services.Pass(), mojo::String::From(url_.spec()));
  }

  void Initialize() {
    mojo::ShellPtr shell_proxy;
    shell_binding_.Bind(mojo::GetProxy(&shell_proxy));

    proxy_.set_error_handler(this);
    proxy_->Initialize(shell_proxy.Pass(), mojo::Array<mojo::String>(),
        mojo::String::From(url_.spec()));
  }

 private:
  // mojo::ErrorHandler:
  void OnConnectionError() override {
    exit_callback_.Run(-1);
  }

  // mojo::Shell:
  void ConnectToApplication(
      const mojo::String& application_url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services) override {
    shell_->ConnectApplications(
        GURL(application_url.To<std::string>()),
        services.Pass(), exposed_services.Pass(), url_);
  }

  ShellImpl* shell_;

  mojo::Binding<mojo::Shell> shell_binding_;
  GURL url_;
  mojo::ApplicationPtr proxy_;
  ExitCallback exit_callback_;

  base::WeakPtrFactory<RunningApplication> weak_factory_;
};

ApplicationManager::ApplicationManager(ShellImpl* shell)
    : shell_(shell), next_app_id_(1), weak_factory_(this) {
}

ApplicationManager::~ApplicationManager() {
  STLDeleteContainerPairSecondPointers(apps_.begin(), apps_.end());
}

void ApplicationManager::ConnectApplications(
    const GURL& to_url,
    mojo::InterfaceRequest<mojo::ServiceProvider> services,
    mojo::ServiceProviderPtr exposed_services,
    const GURL& from_url) {
  // TODO(CORE): Something more intelligent than launching a new application
  // instance every time a new connection is requested.
  LaunchApplication(to_url,
      base::Bind(&ApplicationManager::ConnectToRunningApplication,
                 weak_factory_.GetWeakPtr(), from_url, base::Passed(&services),
                 base::Passed(&exposed_services)));
}

void ApplicationManager::LaunchApplication(const GURL& url,
                                           const LaunchCallback& callback) {
  mojo::ApplicationPtr proxy;
  shell_->in_process_application_host()->LaunchApplication(
      mojo::String::From(url.spec()), mojo::GetProxy(&proxy),
      base::Bind(&ApplicationManager::OnApplicationLaunched,
                 weak_factory_.GetWeakPtr(), callback, url,
                 base::Passed(&proxy)));
}

void ApplicationManager::ConnectToRunningApplication(
    const GURL& from_url,
    mojo::InterfaceRequest<mojo::ServiceProvider> services,
    mojo::ServiceProviderPtr exposed_services,
    base::WeakPtr<RunningApplication> application) {
  if (!application)
    return;
  application->Connect(from_url, services.Pass(), exposed_services.Pass());
}

void ApplicationManager::OnApplicationLaunched(
    const LaunchCallback& callback,
    const GURL& url,
    mojo::ApplicationPtr proxy,
    LaunchError reuslt) {
  int32_t app_id = next_app_id_++;
  RunningApplication* app = new RunningApplication(shell_, url, proxy.Pass(),
      base::Bind(&ApplicationManager::OnApplicationExited,
                 weak_factory_.GetWeakPtr(), app_id));
  apps_.insert(std::make_pair(app_id, app));

  // It's possible that calling Initialize will trigger a pipe error and
  // immediately destroy the RunningApplication instance, so we give the
  // callback a WeakPtr.
  auto weak_app = app->GetWeakPtr();
  app->Initialize();
  callback.Run(weak_app);
}

void ApplicationManager::OnApplicationExited(int32_t app_id,
                                             int32_t exit_code) {
  auto iter = apps_.find(app_id);
  DCHECK(iter != apps_.end());
  delete iter->second;
  apps_.erase(iter);
}

}  // namespace core
