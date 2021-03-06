// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/launcher_search_provider/service.h"

#include "base/memory/scoped_vector.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/launcher_search_provider/service_factory.h"
#include "chrome/browser/ui/app_list/search/launcher_search/launcher_search_provider.h"
#include "chrome/browser/ui/app_list/search/launcher_search/launcher_search_result.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/permissions/permissions_data.h"

namespace api_launcher_search_provider =
    extensions::api::launcher_search_provider;
using extensions::ExtensionId;
using extensions::ExtensionSet;

namespace chromeos {
namespace launcher_search_provider {

Service::Service(Profile* profile,
                 extensions::ExtensionRegistry* extension_registry)
    : profile_(profile),
      extension_registry_(extension_registry),
      provider_(nullptr),
      query_id_(0),
      is_query_running_(false) {
}

Service::~Service() {
}

// static
Service* Service::Get(content::BrowserContext* context) {
  return ServiceFactory::Get(context);
}

void Service::OnQueryStarted(app_list::LauncherSearchProvider* provider,
                             const std::string& query,
                             const int max_result) {
  DCHECK(!is_query_running_);
  is_query_running_ = true;
  provider_ = provider;

  ++query_id_;

  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(profile_);

  std::set<ExtensionId> extension_ids = GetListenerExtensionIds();
  for (const ExtensionId extension_id : extension_ids) {
    // Convert query_id_ to string here since queryId is defined as string in
    // javascript side API while we use uint32 internally to generate it.
    // TODO(yawano): Consider if we can change it to integer at javascript side.
    event_router->DispatchEventToExtension(
        extension_id,
        make_scoped_ptr(new extensions::Event(
            api_launcher_search_provider::OnQueryStarted::kEventName,
            api_launcher_search_provider::OnQueryStarted::Create(
                std::to_string(query_id_), query, max_result))));
  }
}

void Service::OnQueryEnded() {
  DCHECK(is_query_running_);
  provider_ = nullptr;

  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(profile_);

  std::set<ExtensionId> extension_ids = GetListenerExtensionIds();
  for (const ExtensionId extension_id : extension_ids) {
    event_router->DispatchEventToExtension(
        extension_id,
        make_scoped_ptr(new extensions::Event(
            api_launcher_search_provider::OnQueryEnded::kEventName,
            api_launcher_search_provider::OnQueryEnded::Create(
                std::to_string(query_id_)))));
  }

  is_query_running_ = false;
}

void Service::OnOpenResult(const ExtensionId& extension_id,
                           const std::string& item_id) {
  CHECK(ContainsValue(GetListenerExtensionIds(), extension_id));

  extensions::EventRouter* event_router =
      extensions::EventRouter::Get(profile_);
  event_router->DispatchEventToExtension(
      extension_id,
      make_scoped_ptr(new extensions::Event(
          api_launcher_search_provider::OnOpenResult::kEventName,
          api_launcher_search_provider::OnOpenResult::Create(item_id))));
}

void Service::SetSearchResults(
    const extensions::Extension* extension,
    const std::string& query_id,
    const std::vector<linked_ptr<
        extensions::api::launcher_search_provider::SearchResult>>& results) {
  // If query is not running or query_id is different from current query id,
  // discard the results.
  if (!is_query_running_ || query_id != std::to_string(query_id_))
    return;

  // If |extension| is not in the listener extensions list, ignore it.
  if (!ContainsValue(GetListenerExtensionIds(), extension->id()))
    return;

  // Set search results to provider.
  DCHECK(provider_);
  ScopedVector<app_list::LauncherSearchResult> search_results;
  for (const auto& result : results) {
    const int relevance =
        std::min(kMaxSearchResultScore, std::max(result->relevance, 0));
    const GURL icon_url =
        result->icon_url ? GURL(*result->icon_url.get()) : GURL();

    app_list::LauncherSearchResult* search_result =
        new app_list::LauncherSearchResult(result->item_id, icon_url, relevance,
                                           profile_, extension);
    search_result->set_title(base::UTF8ToUTF16(result->title));
    search_results.push_back(search_result);
  }
  provider_->SetSearchResults(extension->id(), search_results.Pass());
}

bool Service::IsQueryRunning() const {
  return is_query_running_;
}

std::set<ExtensionId> Service::GetListenerExtensionIds() {
  std::set<ExtensionId> extension_ids;

  const ExtensionSet& extension_set = extension_registry_->enabled_extensions();
  for (scoped_refptr<const extensions::Extension> extension : extension_set) {
    const extensions::PermissionsData* permission_data =
        extension->permissions_data();
    const bool has_permission = permission_data->HasAPIPermission(
        extensions::APIPermission::kLauncherSearchProvider);
    if (has_permission)
      extension_ids.insert(extension->id());
  }

  return extension_ids;
}

}  // namespace launcher_search_provider
}  // namespace chromeos
