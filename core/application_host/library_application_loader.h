// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_APPLICATION_HOST_LIBRARY_APPLICATION_LOADER_H_
#define CORE_APPLICATION_HOST_LIBRARY_APPLICATION_LOADER_H_

#include <vector>

#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_native_library.h"
#include "core/public/application_host/application_loader.h"
#include "core/public/application_host/entry_point.h"
#include "third_party/mojo/src/mojo/public/c/system/types.h"

namespace core {

class LibraryApplicationLoader : public ApplicationLoader {
 public:
  LibraryApplicationLoader(const std::string& library_name);
  ~LibraryApplicationLoader() override;

  // ApplicationLoader:
  void Load(mojo::InterfaceRequest<mojo::Application> application_request,
            const LoadCallback& callback) override;

 private:
  class PendingLoad;

  void CompletePendingLoads();

  // This is static so we can clean up the native library if necessary, i.e.,
  // if this loader died on its own thread before the native library was loaded
  // successfully.
  static void OnLibraryLoaded(
      base::WeakPtr<LibraryApplicationLoader> weak_loader,
      base::NativeLibrary library,
      const base::NativeLibraryLoadError& error);

  void FinalizeLibraryLoad(base::NativeLibrary library,
                           const base::NativeLibraryLoadError& error);

  ScopedVector<PendingLoad> pending_loads_;
  std::string library_name_;
  base::ScopedNativeLibrary library_;
  EntryPoint::MainFunction main_function_;

  base::WeakPtrFactory<LibraryApplicationLoader> weak_factory_;
};

}  // namespace core

#endif  // CORE_APPLICATION_HOST_LIBRARY_APPLICATION_LOADER_H_
