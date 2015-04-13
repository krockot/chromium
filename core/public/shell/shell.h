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

  // TODO(rockot): Remove this once all browser code belongs to one or more
  // real Applications. This serves as a global Shell proxy for the implicit
  // monolithic application. Please DO NOT USE THIS without first consulting
  // OWNERS of //core.
  static mojo::Shell* GetProxy();

 private:
  DISALLOW_COPY_AND_ASSIGN(Shell);
};

}  // namespace core

#endif  // CORE_PUBLIC_SHELL_SHELL_H_
