// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_PUBLIC_SHELL_SHELL_H_
#define CORE_PUBLIC_SHELL_SHELL_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "third_party/mojo/src/mojo/public/interfaces/application/shell.mojom.h"

class GURL;

namespace core {

class Shell {
 public:
  Shell() {}
  virtual ~Shell() {}

  static scoped_ptr<Shell> Create();

  // DO NOT USE THIS without first consulting OWNERS. Also probably don't use
  // after that either.
  //
  // TODO(CORE): Remove this once it's no longer needed, i.e., once all code
  // which connects to an app also lives in an app.
  static mojo::Shell* GetProxy();

 private:
  DISALLOW_COPY_AND_ASSIGN(Shell);
};

}  // namespace core

#endif  // CORE_PUBLIC_SHELL_SHELL_H_
