// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_SERVICE_PROVIDER_H_
#define CORE_PUBLIC_COMMON_SERVICE_PROVIDER_H_

#include <string>

#include "base/callback_forward.h"
#include "third_party/mojo/src/mojo/public/cpp/system/message_pipe.h"

namespace core {

class ServiceProvider {
 public:
  using RequestHandler = base::Callback<void(mojo::ScopedMessagePipeHandle)>;

  virtual ~ServiceProvider() {}

  virtual void RegisterService(const std::string&,
                               const RequestHandler& handler) = 0;
};

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_SERVICE_PROVIDER_H_
