// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/applications/browser/browser.h"

#include <stdint.h>

#include "base/callback.h"
#include "base/containers/hash_tables.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/binding.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/error_handler.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace chrome_browser {

namespace {

class ApplicationInstance : public mojo::ErrorHandler {
 public:
  explicit ApplicationInstance(mojo::ApplicationPtr proxy);
  ~ApplicationInstance() override;

  void SetErrorHandler(const base::Closure& error_handler);

 private:
  // mojo::ErrorHandler:
  void OnConnectionError() override;

  mojo::ApplicationPtr proxy_;
  base::Closure error_handler_;
};

ApplicationInstance::ApplicationInstance(mojo::ApplicationPtr proxy)
    : proxy_(proxy.Pass()) {
  proxy_.set_error_handler(this);
}

ApplicationInstance::~ApplicationInstance() {
}

void ApplicationInstance::SetErrorHandler(const base::Closure& error_handler) {
  DCHECK(error_handler_.is_null());
  error_handler_ = error_handler;
}

void ApplicationInstance::OnConnectionError() {
  if (!error_handler_.is_null())
    error_handler_.Run();
}

}  // namespace

class Browser::ApplicationManager : public core::ApplicationBroker {
 public:
  ApplicationManager(core::ShellPrivatePtr shell_private);
  ~ApplicationManager() override;

  void BindRequest(mojo::InterfaceRequest<core::ApplicationBroker> request);

 private:
  // core::ApplicationBroker:
  void ConnectApplications(
      const mojo::String& to_url,
      mojo::InterfaceRequest<mojo::ServiceProvider> services,
      mojo::ServiceProviderPtr exposed_services,
      const mojo::String& from_url) override;

  void OnApplicationLaunched(int64_t instance_id, core::LaunchError result);
  void PurgeApplicationInstance(int64_t id);

  int64_t next_instance_id_;
  base::hash_map<int64_t, ApplicationInstance*> running_applications_;

  core::ShellPrivatePtr shell_private_;
  core::ApplicationHostPtr shell_host_;
  mojo::Binding<core::ApplicationBroker> binding_;

  base::WeakPtrFactory<ApplicationManager> weak_factory_;
};

Browser::ApplicationManager::ApplicationManager(
    core::ShellPrivatePtr shell_private)
    : next_instance_id_(0),
      shell_private_(shell_private.Pass()),
      binding_(this),
      weak_factory_(this) {
  core::ProcessPtr shell_process;
  shell_private_->GetShellProcess(mojo::GetProxy(&shell_process));
  shell_process->GetApplicationHost(mojo::GetProxy(&shell_host_));
}

Browser::ApplicationManager::~ApplicationManager() {
  STLDeleteContainerPairSecondPointers(running_applications_.begin(),
                                       running_applications_.end());
}

void Browser::ApplicationManager::BindRequest(
    mojo::InterfaceRequest<core::ApplicationBroker> request) {
  binding_.Bind(request.Pass());
}

void Browser::ApplicationManager::ConnectApplications(
    const mojo::String& to_url,
    mojo::InterfaceRequest<mojo::ServiceProvider> services,
    mojo::ServiceProviderPtr exposed_services,
    const mojo::String& from_url) {
  mojo::ApplicationPtr application;
  mojo::InterfaceRequest<mojo::Application> application_request =
      mojo::GetProxy(&application);
  application->AcceptConnection(from_url, services.Pass(),
                                exposed_services.Pass(), to_url);

  int64_t instance_id = next_instance_id_++;
  ApplicationInstance* instance = new ApplicationInstance(application.Pass());
  running_applications_.insert(std::make_pair(instance_id, instance));

  shell_host_->LaunchApplication(
      to_url, application_request.Pass(),
      base::Bind(&Browser::ApplicationManager::OnApplicationLaunched,
                 weak_factory_.GetWeakPtr(), instance_id));
}

void Browser::ApplicationManager::OnApplicationLaunched(
    int64_t instance_id,
    core::LaunchError result) {
  if (result != core::LAUNCH_ERROR_OK) {
    PurgeApplicationInstance(instance_id);
    return;
  }

  auto iter = running_applications_.find(instance_id);
  DCHECK(iter != running_applications_.end());
  ApplicationInstance* instance = iter->second;
  DCHECK(instance);

  instance->SetErrorHandler(
      base::Bind(&ApplicationManager::PurgeApplicationInstance,
                 weak_factory_.GetWeakPtr(), instance_id));
}

void Browser::ApplicationManager::PurgeApplicationInstance(int64_t id) {
  auto iter = running_applications_.find(id);
  DCHECK(iter != running_applications_.end());
  delete iter->second;
  running_applications_.erase(iter);
}

Browser::Browser() : weak_factory_(this) {
}

Browser::~Browser() {
}

void Browser::InitializeApplication(mojo::Shell* shell,
                                    const std::vector<std::string>& args) {
}

void Browser::AcceptConnection(const GURL& requestor_url,
                               core::ApplicationConnection* connection,
                               const GURL& resolved_url) {
  DCHECK(!manager_);
  core::ShellPrivatePtr shell_private =
      connection->ConnectToService<core::ShellPrivate>();
  manager_.reset(new ApplicationManager(shell_private.Pass()));
  connection->AddService(
      base::Bind(&Browser::BindBrokerRequest, weak_factory_.GetWeakPtr()));
  connection->ExposeServices();
}

void Browser::Quit() {
}

void Browser::BindBrokerRequest(
    mojo::InterfaceRequest<core::ApplicationBroker> request) {
  manager_->BindRequest(request.Pass());
}

}  // namespace chrome_browser
