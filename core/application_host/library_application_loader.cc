// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/application_host/library_application_loader.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/strings/stringprintf.h"
#include "core/public/application_host/core_application_host_client.h"
#include "third_party/mojo/src/mojo/public/platform/native/system_thunks.h"

namespace core {

namespace {

typedef MojoResult (*RawMainFunctionType)(MojoHandle);

const char kMainFunctionName[] = "MojoMain";
const char kMojoSetSystemThunksFunctionName[] = "MojoSetSystemThunks";

base::FilePath GetLibraryPath(const std::string& library_name) {
  // TODO(core): Handle other runtime environments...
  return base::FilePath(
      base::StringPrintf("lib%s_library.so", library_name.c_str()));
}

using ResponseCallback = base::Callback<
    void(base::NativeLibrary, const base::NativeLibraryLoadError&)>;

void Respond(const ResponseCallback& callback,
             base::NativeLibrary library,
             base::NativeLibraryLoadError error) {
  callback.Run(library, error);
}

void LoadApplicationLibrary(
    scoped_refptr<base::TaskRunner> response_task_runner,
    const std::string& library_name,
    const ResponseCallback& callback) {
  base::NativeLibraryLoadError error;
  base::NativeLibrary library = base::LoadNativeLibrary(
      GetLibraryPath(library_name), &error);
  response_task_runner->PostTask(
      FROM_HERE, base::Bind(&Respond, callback, library, error));
}

void RunMainFunction(RawMainFunctionType main_function,
                     MojoHandle handle) {
  main_function(handle);
}

EntryPoint::MainFunction BindMainFunction(RawMainFunctionType main_function) {
  return base::Bind(&RunMainFunction, main_function);
}

}  // namespace

class LibraryApplicationLoader::PendingLoad {
 public:
  PendingLoad(mojo::InterfaceRequest<mojo::Application> application_request,
              const LoadCallback& callback);
  ~PendingLoad();

  void Complete(EntryPoint::MainFunction main_function);

 private:
  mojo::InterfaceRequest<mojo::Application> application_request_;
  LoadCallback callback_;
};

LibraryApplicationLoader::PendingLoad::PendingLoad(
    mojo::InterfaceRequest<mojo::Application> application_request,
    const LoadCallback& callback)
    : application_request_(application_request.Pass()), callback_(callback) {
}

LibraryApplicationLoader::PendingLoad::~PendingLoad() {
}

void LibraryApplicationLoader::PendingLoad::Complete(
    EntryPoint::MainFunction main_function) {
  if (main_function.is_null())
    callback_.Run(CreateFailureResult(application_request_.Pass()));
  else
    callback_.Run(CreateSuccessResult(application_request_.Pass(),
                                      main_function));
}

scoped_ptr<ApplicationLoader> ApplicationLoader::CreateForLibrary(
    const std::string& library_name) {
  return make_scoped_ptr<ApplicationLoader>(
      new LibraryApplicationLoader(library_name));
}

LibraryApplicationLoader::LibraryApplicationLoader(
    const std::string& library_name)
    : library_name_(library_name),
      weak_factory_(this) {
}

LibraryApplicationLoader::~LibraryApplicationLoader() {
  // Make sure to notify any pending loads (of failure) if we disappear before
  // the library loads.
  CompletePendingLoads();
}

void LibraryApplicationLoader::Load(
    mojo::InterfaceRequest<mojo::Application> application_request,
    const LoadCallback& callback) {
  if (library_.is_valid()) {
    DCHECK(!main_function_.is_null());
    callback.Run(CreateSuccessResult(application_request.Pass(),
                                     main_function_));
    return;
  }

  pending_loads_.push_back(new PendingLoad(application_request.Pass(),
                                           callback));
  // If this isn't the only pending load, there's already one in progress.
  if (pending_loads_.size() > 1)
    return;

  core::CoreApplicationHostClient::Get()
      ->GetTaskRunnerForLibraryLoad()->PostTask(
          FROM_HERE,
          base::Bind(&LoadApplicationLibrary,
                     base::MessageLoopProxy::current(),
                     library_name_,
                     base::Bind(&LibraryApplicationLoader::OnLibraryLoaded,
                                weak_factory_.GetWeakPtr())));
}

void LibraryApplicationLoader::CompletePendingLoads() {
  ScopedVector<PendingLoad> pending_loads;
  pending_loads_.swap(pending_loads);
  for (const auto& pending_load : pending_loads)
    pending_load->Complete(main_function_);
}

// static
void LibraryApplicationLoader::OnLibraryLoaded(
    base::WeakPtr<LibraryApplicationLoader> weak_loader,
    base::NativeLibrary library,
    const base::NativeLibraryLoadError& error) {
  // The loader may have died before the library load completed.
  if (!weak_loader) {
    if (library)
      base::UnloadNativeLibrary(library);
    return;
  }
  weak_loader->FinalizeLibraryLoad(library, error);
}

void LibraryApplicationLoader::FinalizeLibraryLoad(
    base::NativeLibrary library,
    const base::NativeLibraryLoadError& error) {
  base::ScopedNativeLibrary scoped_library(library);
  if (!library) {
    LOG(ERROR) << "Failed to load library " << library_name_ << ": "
               << error.message;
    CompletePendingLoads();
    return;
  }

  // Fixup Mojo system thunks.
  typedef MojoResult (*MojoSetSystemThunksFn)(MojoSystemThunks*);
  MojoSetSystemThunksFn mojo_set_system_thunks_fn =
      reinterpret_cast<MojoSetSystemThunksFn>(
          scoped_library.GetFunctionPointer(kMojoSetSystemThunksFunctionName));
  if (!mojo_set_system_thunks_fn) {
    LOG(ERROR) << "Unable to load application from library " << library_name_
               << ": " << kMojoSetSystemThunksFunctionName
               << " symbol not found!";
    CompletePendingLoads();
    return;
  }
  MojoSystemThunks system_thunks = MojoMakeSystemThunks();
  size_t expected_size = mojo_set_system_thunks_fn(&system_thunks);
  if (expected_size > sizeof(MojoSystemThunks)) {
    LOG(ERROR) << "Invalid application library. Expected MojoSystemThunks size "
                << expected_size;
    CompletePendingLoads();
    return;
  }

  RawMainFunctionType main_function = reinterpret_cast<RawMainFunctionType>(
      scoped_library.GetFunctionPointer(kMainFunctionName));
  if (!main_function) {
    LOG(ERROR) << "Unable to load application from library " << library_name_
               << ": " << kMainFunctionName << " symbol not found!";
    CompletePendingLoads();
    return;
  }

  library_.Reset(scoped_library.Release());
  main_function_ = BindMainFunction(main_function);
  CompletePendingLoads();
}

}  // namespace core
