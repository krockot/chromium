// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/keyed_service/content/browser_context_keyed_base_factory.h"

#include "base/prefs/pref_service.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"

BrowserContextKeyedBaseFactory::BrowserContextKeyedBaseFactory(
    const char* name,
    BrowserContextDependencyManager* manager)
    : KeyedServiceBaseFactory(name, manager) {
}

BrowserContextKeyedBaseFactory::~BrowserContextKeyedBaseFactory() {
}

content::BrowserContext* BrowserContextKeyedBaseFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  DCHECK(CalledOnValidThread());

#ifndef NDEBUG
  AssertContextWasntDestroyed(context);
#endif

  // Safe default for the Incognito mode: no service.
  if (context->IsOffTheRecord())
    return NULL;

  return context;
}

void BrowserContextKeyedBaseFactory::RegisterUserPrefsOnBrowserContextForTest(
    content::BrowserContext* context) {
  KeyedServiceBaseFactory::RegisterUserPrefsOnContextForTest(context);
}

bool BrowserContextKeyedBaseFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return KeyedServiceBaseFactory::ServiceIsCreatedWithContext();
}

bool BrowserContextKeyedBaseFactory::ServiceIsNULLWhileTesting() const {
  return KeyedServiceBaseFactory::ServiceIsNULLWhileTesting();
}

void BrowserContextKeyedBaseFactory::BrowserContextDestroyed(
    content::BrowserContext* context) {
  KeyedServiceBaseFactory::ContextDestroyed(context);
}

user_prefs::PrefRegistrySyncable*
BrowserContextKeyedBaseFactory::GetAssociatedPrefRegistry(
    base::SupportsUserData* context) const {
  PrefService* prefs = user_prefs::UserPrefs::Get(context);
  user_prefs::PrefRegistrySyncable* registry =
      static_cast<user_prefs::PrefRegistrySyncable*>(
          prefs->DeprecatedGetPrefRegistry());
  return registry;
}

base::SupportsUserData* BrowserContextKeyedBaseFactory::GetContextToUse(
    base::SupportsUserData* context) const {
  return GetBrowserContextToUse(static_cast<content::BrowserContext*>(context));
}

bool BrowserContextKeyedBaseFactory::ServiceIsCreatedWithContext() const {
  return ServiceIsCreatedWithBrowserContext();
}

void BrowserContextKeyedBaseFactory::ContextShutdown(
    base::SupportsUserData* context) {
  BrowserContextShutdown(static_cast<content::BrowserContext*>(context));
}

void BrowserContextKeyedBaseFactory::ContextDestroyed(
    base::SupportsUserData* context) {
  BrowserContextDestroyed(static_cast<content::BrowserContext*>(context));
}

void BrowserContextKeyedBaseFactory::RegisterPrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  RegisterProfilePrefs(registry);
}

void BrowserContextKeyedBaseFactory::SetEmptyTestingFactory(
    base::SupportsUserData* context) {
  SetEmptyTestingFactory(static_cast<content::BrowserContext*>(context));
}

bool BrowserContextKeyedBaseFactory::HasTestingFactory(
    base::SupportsUserData* context) {
  return HasTestingFactory(static_cast<content::BrowserContext*>(context));
}

void BrowserContextKeyedBaseFactory::CreateServiceNow(
    base::SupportsUserData* context) {
  CreateServiceNow(static_cast<content::BrowserContext*>(context));
}
