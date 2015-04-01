// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
#define CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "base/task_runner.h"
#include "core/public/application_host/entry_point.h"
#include "third_party/mojo/src/mojo/public/cpp/bindings/interface_request.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

namespace core {

class ApplicationLoader {
 public:
  // The result of an application load request. If the load was successful,
  // this will yield a valid EntryPoint which can be used to launch the app.
  // Otherwise the original Application request can be recovered.
  class Result {
   public:
    explicit Result(scoped_ptr<EntryPoint> entry_point);
    explicit Result(
        mojo::InterfaceRequest<mojo::Application> application_request);
    ~Result();

    // Indicates if loading failed. If true, |GetEntryPoint()| will return an
    // invalid pointer and |PassRequest()| can be use to recover the original
    // Application request.
    bool Failed() const;

    // Pass the EntryPoint off to the caller. Returns null if loading failed.
    scoped_ptr<EntryPoint> PassEntryPoint();

    // Pass the original Application request off to the caller if loading
    // failed. Otherwise this returns an unbound request.
    mojo::InterfaceRequest<mojo::Application> PassRequest();

   private:
    scoped_ptr<EntryPoint> entry_point_;
    mojo::InterfaceRequest<mojo::Application> application_request_;
  };

  using LoadCallback = base::Callback<void(scoped_ptr<Result> result)>;

  virtual ~ApplicationLoader() {}

  // Constructs an ApplicationLoader which loads from a dynamic library.
  static scoped_ptr<ApplicationLoader> CreateForLibrary(
      const std::string& name);

  // These are for convenient construction of Result objects for success or
  // failure.
  static scoped_ptr<Result> CreateSuccessResult(
      mojo::InterfaceRequest<mojo::Application> application_request,
      const EntryPoint::MainFunction& main_function);
  static scoped_ptr<Result> CreateFailureResult(
      mojo::InterfaceRequest<mojo::Application> aplplication_request);

  virtual void Load(
      mojo::InterfaceRequest<mojo::Application> application_request,
      const LoadCallback& callback) = 0;
};

}  // namespace core

#endif  // CORE_PUBLIC_APPLICATION_HOST_APPLICATION_LOADER_H_
