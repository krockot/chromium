// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PROXIMITY_AUTH_FAKE_SECURE_MESSAGE_DELEGATE_H
#define COMPONENTS_PROXIMITY_AUTH_FAKE_SECURE_MESSAGE_DELEGATE_H

#include "base/macros.h"
#include "components/proximity_auth/cryptauth/secure_message_delegate.h"

namespace proximity_auth {

// Fake implementation of SecureMessageDelegate used in tests.
// For clarity in tests, all functions in this delegate will invoke their
// callback with the result before returning.
class FakeSecureMessageDelegate : public SecureMessageDelegate {
 public:
  FakeSecureMessageDelegate();
  ~FakeSecureMessageDelegate() override;

  // SecureMessageDelegate:
  void GenerateKeyPair(const GenerateKeyPairCallback& callback) override;
  void DeriveKey(const std::string& private_key,
                 const std::string& public_key,
                 const DeriveKeyCallback& callback) override;
  void CreateSecureMessage(
      const std::string& payload,
      const std::string& key,
      const CreateOptions& create_options,
      const CreateSecureMessageCallback& callback) override;
  void UnwrapSecureMessage(
      const std::string& serialized_message,
      const std::string& key,
      const UnwrapOptions& unwrap_options,
      const UnwrapSecureMessageCallback& callback) override;

  // Sets the next public key to be returned by GenerateKeyPair(). The
  // corresponding private key will be derived from this public key.
  void set_next_public_key(const std::string& public_key) {
    next_public_key_ = public_key;
  }

 private:
  std::string next_public_key_;

  DISALLOW_COPY_AND_ASSIGN(FakeSecureMessageDelegate);
};

}  // namespace proximity_auth

#endif  // COMPONENTS_PROXIMITY_AUTH_FAKE_SECURE_MESSAGE_DELEGATE_H
