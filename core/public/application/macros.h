// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_APPLICATION_MACROS_H_
#define CORE_PUBLIC_APPLICATION_MACROS_H_

#if defined(CORE_APPLICATION_IMPL)

#if defined(WIN32)
#define APPLICATION_MAIN_EXPORT __declspec(dllexport)
#else
#define APPLICATION_MAIN_EXPORT __attribute__((visibility("default")))
#endif

#include "base/memory/scoped_ptr.h"
#include "core/public/application/application_runner.h"
#include "third_party/mojo/src/mojo/public/c/system/types.h"

#define APPLICATION_MAIN(delegate_type) \
    extern "C" MojoResult APPLICATION_MAIN_EXPORT \
    CoreApplicationMain(MojoHandle application_request_handle) { \
      core::ApplicationRunner runner; \
      return runner.Run(\
          make_scoped_ptr<core::ApplicationDelegate>(new delegate_type), \
          application_request_handle); \
    }

#endif  // defined(CORE_APPLICATION_IMPL)

#endif  // CORE_PUBLIC_APPLICATION_MACROS_H_
