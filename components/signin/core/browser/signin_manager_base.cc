// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/core/browser/signin_manager_base.h"

#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/memory/ref_counted.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/signin/core/browser/account_tracker_service.h"
#include "components/signin/core/browser/signin_client.h"
#include "components/signin/core/common/signin_pref_names.h"
#include "components/signin/core/common/signin_switches.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"

using namespace signin_internals_util;

SigninManagerBase::SigninManagerBase(
    SigninClient* client,
    AccountTrackerService* account_tracker_service)
    : client_(client),
      account_tracker_service_(account_tracker_service),
      initialized_(false),
      weak_pointer_factory_(this) {
  DCHECK(client_);
  DCHECK(account_tracker_service_);
}

SigninManagerBase::~SigninManagerBase() {}

void SigninManagerBase::Initialize(PrefService* local_state) {
  // Should never call Initialize() twice.
  DCHECK(!IsInitialized());
  initialized_ = true;

  // If the user is clearing the token service from the command line, then
  // clear their login info also (not valid to be logged in without any
  // tokens).
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  if (cmd_line->HasSwitch(switches::kClearTokenService)) {
    client_->GetPrefs()->ClearPref(prefs::kGoogleServicesAccountId);
    client_->GetPrefs()->ClearPref(prefs::kGoogleServicesUsername);
    client_->GetPrefs()->ClearPref(prefs::kGoogleServicesUserAccountId);
  }

  std::string account_id =
      client_->GetPrefs()->GetString(prefs::kGoogleServicesAccountId);

  // Handle backward compatibility: if kGoogleServicesAccountId is empty, but
  // kGoogleServicesUsername is not, then this is an old profile that needs to
  // be updated.  kGoogleServicesUserAccountId should not be empty, and contains
  // the gaia_id.  Use both properties to prime the account tracker before
  // proceeding.
  if (account_id.empty()) {
    std::string pref_account_username =
        client_->GetPrefs()->GetString(prefs::kGoogleServicesUsername);
    std::string pref_gaia_id =
        client_->GetPrefs()->GetString(prefs::kGoogleServicesUserAccountId);

    // If kGoogleServicesUserAccountId is empty, then this is either a chromeos
    // machine or a really old profile on one of the other platforms.  However
    // in this case the account tracker should have the gaia_id so fetch it
    // from there.
    if (!pref_account_username.empty() && pref_gaia_id.empty()) {
      AccountTrackerService::AccountInfo info =
          account_tracker_service_->GetAccountInfo(pref_account_username);
      DCHECK(!info.gaia.empty());
      pref_gaia_id = info.gaia;
    }

    if (!pref_account_username.empty() && !pref_gaia_id.empty()) {
      account_id = account_tracker_service_->SeedAccountInfo(
          pref_gaia_id, pref_account_username);

      // Now remove obsolete preferences.
      client_->GetPrefs()->ClearPref(prefs::kGoogleServicesUsername);

      // TODO(rogerta): once migration to gaia id is complete, remove
      // kGoogleServicesUserAccountId and change all uses of that pref to
      // kGoogleServicesAccountId.
    }
  }

  if (!account_id.empty())
    SetAuthenticatedAccountId(account_id);
}

bool SigninManagerBase::IsInitialized() const { return initialized_; }

bool SigninManagerBase::IsSigninAllowed() const {
  return client_->GetPrefs()->GetBoolean(prefs::kSigninAllowed);
}

std::string SigninManagerBase::GetAuthenticatedUsername() const {
  return account_tracker_service_->GetAccountInfo(
      GetAuthenticatedAccountId()).email;
}

const std::string& SigninManagerBase::GetAuthenticatedAccountId() const {
  return authenticated_account_id_;
}

void SigninManagerBase::SetAuthenticatedAccountInfo(const std::string& gaia_id,
                                                    const std::string& email) {
  std::string account_id =
      account_tracker_service_->SeedAccountInfo(gaia_id, email);
  SetAuthenticatedAccountId(account_id);
}

void SigninManagerBase::SetAuthenticatedAccountId(
    const std::string& account_id) {
  DCHECK(!account_id.empty());
  if (!authenticated_account_id_.empty()) {
    DLOG_IF(ERROR, account_id != authenticated_account_id_)
        << "Tried to change the authenticated id to something different: "
        << "Current: " << authenticated_account_id_ << ", New: " << account_id;
    return;
  }

  std::string pref_account_id =
      client_->GetPrefs()->GetString(prefs::kGoogleServicesAccountId);

  DCHECK(pref_account_id.empty() || pref_account_id == account_id)
      << "account_id=" << account_id
      << " pref_account_id=" << pref_account_id;
  authenticated_account_id_ = account_id;
  client_->GetPrefs()->SetString(prefs::kGoogleServicesAccountId, account_id);

  // This preference is set so that code on I/O thread has access to the
  // Gaia id of the signed in user.
  AccountTrackerService::AccountInfo info =
      account_tracker_service_->GetAccountInfo(account_id);
  DCHECK(!info.gaia.empty());
  client_->GetPrefs()->SetString(prefs::kGoogleServicesUserAccountId,
                                 info.gaia);

  // Go ahead and update the last signed in account info here as well. Once a
  // user is signed in the two preferences should match. Doing it here as
  // opposed to on signin allows us to catch the upgrade scenario.
  client_->GetPrefs()->SetString(prefs::kGoogleServicesLastUsername,
                                 info.email);
}

bool SigninManagerBase::IsAuthenticated() const {
  return !authenticated_account_id_.empty();
}

bool SigninManagerBase::AuthInProgress() const {
  // SigninManagerBase never kicks off auth processes itself.
  return false;
}

void SigninManagerBase::Shutdown() {}

void SigninManagerBase::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void SigninManagerBase::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void SigninManagerBase::AddSigninDiagnosticsObserver(
    SigninDiagnosticsObserver* observer) {
  signin_diagnostics_observers_.AddObserver(observer);
}

void SigninManagerBase::RemoveSigninDiagnosticsObserver(
    SigninDiagnosticsObserver* observer) {
  signin_diagnostics_observers_.RemoveObserver(observer);
}

void SigninManagerBase::NotifyDiagnosticsObservers(
    const TimedSigninStatusField& field,
    const std::string& value) {
  FOR_EACH_OBSERVER(SigninDiagnosticsObserver,
                    signin_diagnostics_observers_,
                    NotifySigninValueChanged(field, value));
}
