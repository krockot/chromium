// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_COMMON_APPLICATION_DELEGATE_H_
#define CORE_PUBLIC_COMMON_APPLICATION_DELEGATE_H_

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "core/public/common/application_connection.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/shell.mojom.h"

class GURL;

namespace core {

class ApplicationDelegate {
 public:
  virtual ~ApplicationDelegate() {}

  virtual void InitializeApplication(mojo::Shell* shell,
                                     const std::vector<std::string>& args) = 0;
  virtual void AcceptConnection(const GURL& requestor_url,
                                scoped_ptr<ApplicationConnection> connection,
                                const GURL& resolved_url) = 0;
  virtual void Quit() = 0;
};

using ApplicationFactory = base::Callback<scoped_ptr<ApplicationDelegate>()>;

}  // namespace core

#endif  // CORE_PUBLIC_COMMON_APPLICATION_DELEGATE_H_
