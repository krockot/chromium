// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_SHELL_SHELL_H_
#define CORE_PUBLIC_SHELL_SHELL_H_

#include <string>

#include "base/memory/scoped_ptr.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/application.mojom.h"

class GURL;

namespace core {

class ApplicationConnection;
class ShellDelegate;

class Shell {
 public:
  virtual ~Shell() {}

  // TODO(core): Kill this.
  static Shell* Get();

  static scoped_ptr<Shell> Create();
};

}  // namespace core

#endif  // CORE_PUBLIC_SHELL_SHELL_H_
