// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_BACKGROUND_SYNC_BACKGROUND_SYNC_MANAGER_H_
#define CONTENT_BROWSER_BACKGROUND_SYNC_BACKGROUND_SYNC_MANAGER_H_

#include <list>
#include <map>

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/background_sync/background_sync.pb.h"
#include "content/browser/cache_storage/cache_storage_scheduler.h"
#include "content/browser/service_worker/service_worker_context_observer.h"
#include "content/browser/service_worker/service_worker_storage.h"
#include "content/common/content_export.h"
#include "content/common/service_worker/service_worker_status_code.h"
#include "url/gurl.h"

namespace content {

class BackgroundSyncNetworkObserver;
class ServiceWorkerContextWrapper;

// BackgroundSyncManager manages and stores the set of background sync
// registrations across all registered service workers for a profile.
// Registrations are stored along with their associated Service Worker
// registration in ServiceWorkerStorage. If the ServiceWorker is unregistered,
// the sync registrations are removed. This class expects to be run on the IO
// thread. The asynchronous methods are executed sequentially.

// TODO(jkarlin): Check permissions when registering, scheduling, and firing
// background sync. In the meantime, --enable-service-worker-sync is required to
// fire a sync event.
// TODO(jkarlin): Unregister syncs when permission is revoked.
// TODO(jkarlin): Create a background sync scheduler to actually run the
// registered events.
// TODO(jkarlin): Keep the browser alive if "Let Google Chrome Run in the
// Background" is true and a sync is registered.
class CONTENT_EXPORT BackgroundSyncManager
    : NON_EXPORTED_BASE(public ServiceWorkerContextObserver) {
 public:
  enum ErrorType {
    ERROR_TYPE_OK = 0,
    ERROR_TYPE_STORAGE,
    ERROR_TYPE_NOT_FOUND
  };

  // TODO(jkarlin): Remove this and use the struct from IPC messages once it
  // lands.
  struct CONTENT_EXPORT BackgroundSyncRegistration {
    using RegistrationId = int64;
    static const RegistrationId kInvalidRegistrationId;
    static const RegistrationId kInitialId;
    BackgroundSyncRegistration() {}

    bool Equals(const BackgroundSyncRegistration& other) {
      return this->tag == other.tag && this->periodicity == other.periodicity &&
             this->min_period == other.min_period &&
             network_state == other.network_state &&
             power_state == other.power_state;
    }

    RegistrationId id = kInvalidRegistrationId;
    std::string tag;
    SyncPeriodicity periodicity = SYNC_ONE_SHOT;
    int64 min_period = 0;
    SyncNetworkState network_state = NETWORK_STATE_ONLINE;
    SyncPowerState power_state = POWER_STATE_AVOID_DRAINING;
  };

  using StatusCallback = base::Callback<void(ErrorType)>;
  using StatusAndRegistrationCallback =
      base::Callback<void(ErrorType, const BackgroundSyncRegistration&)>;

  static scoped_ptr<BackgroundSyncManager> Create(
      const scoped_refptr<ServiceWorkerContextWrapper>& service_worker_context);
  ~BackgroundSyncManager() override;

  // Stores the given background sync registration and adds it to the scheduling
  // queue. It will overwrite an existing registration with the same tag and
  // periodicity unless they're identical (save for the id). Calls |callback|
  // with ErrorTypeOK and the accepted registration on success. The accepted
  // registration will have a unique id. It may also have altered parameters if
  // the user or UA chose different parameters than those supplied.
  void Register(const GURL& origin,
                int64 sw_registration_id,
                const BackgroundSyncRegistration& sync_registration,
                const StatusAndRegistrationCallback& callback);

  // Removes the background sync with tag |sync_registration_tag|, periodicity
  // |periodicity|, and id |sync_registration_id|. Calls |callback| with
  // ErrorTypeNotFound if no match is found. Calls |callback| with ErrorTypeOK
  // on success.
  void Unregister(
      const GURL& origin,
      int64 sw_registration_id,
      const std::string& sync_registration_tag,
      SyncPeriodicity periodicity,
      BackgroundSyncRegistration::RegistrationId sync_registration_id,
      const StatusCallback& callback);

  // Finds the background sync registration associated with
  // |sw_registration_id| with periodicity |periodicity|. Calls
  // |callback| with ErrorTypeNotFound if it doesn't exist. Calls |callback|
  // with ErrorTypeOK on success.
  void GetRegistration(const GURL& origin,
                       int64 sw_registration_id,
                       const std::string sync_registration_tag,
                       SyncPeriodicity periodicity,
                       const StatusAndRegistrationCallback& callback);

  // ServiceWorkerContextObserver overrides.
  void OnRegistrationDeleted(int64 registration_id,
                             const GURL& pattern) override;
  void OnStorageWiped() override;

 protected:
  explicit BackgroundSyncManager(
      const scoped_refptr<ServiceWorkerContextWrapper>& context);

  // Init must be called before any public member function. Only call it once.
  void Init();

  // The following methods are virtual for testing.
  virtual void StoreDataInBackend(
      int64 sw_registration_id,
      const GURL& origin,
      const std::string& backend_key,
      const std::string& data,
      const ServiceWorkerStorage::StatusCallback& callback);
  virtual void GetDataFromBackend(
      const std::string& backend_key,
      const ServiceWorkerStorage::GetUserDataForAllRegistrationsCallback&
          callback);

 private:
  class RegistrationKey {
   public:
    explicit RegistrationKey(const BackgroundSyncRegistration& registration);
    RegistrationKey(const std::string& tag, SyncPeriodicity periodicity);
    RegistrationKey(const RegistrationKey& other) = default;
    RegistrationKey& operator=(const RegistrationKey& other) = default;

    bool operator<(const RegistrationKey& rhs) const {
      return value_ < rhs.value_;
    }

   private:
    std::string value_;
  };

  struct BackgroundSyncRegistrations {
    using RegistrationMap =
        std::map<RegistrationKey, BackgroundSyncRegistration>;

    BackgroundSyncRegistrations();
    explicit BackgroundSyncRegistrations(
        BackgroundSyncRegistration::RegistrationId next_id);
    ~BackgroundSyncRegistrations();

    RegistrationMap registration_map;
    BackgroundSyncRegistration::RegistrationId next_id;
  };

  using PermissionStatusCallback = base::Callback<void(bool)>;
  using SWIdToRegistrationsMap = std::map<int64, BackgroundSyncRegistrations>;

  // Disable the manager. Already queued operations will abort once they start
  // to run (in their impl methods). Future operations will not queue. Any
  // registrations are cleared from memory and the backend (if it's still
  // functioning). The manager will reenable itself once it receives the
  // OnStorageWiped message or on browser restart.
  void DisableAndClearManager(const base::Closure& callback);
  void DisableAndClearDidGetRegistrations(
      const base::Closure& callback,
      const std::vector<std::pair<int64, std::string>>& user_data,
      ServiceWorkerStatusCode status);
  void DisableAndClearManagerClearedOne(const base::Closure& barrier_closure,
                                        ServiceWorkerStatusCode status);

  // Returns the existing registration in |existing_registration| if it is not
  // null.
  bool LookupRegistration(int64 sw_registration_id,
                          const RegistrationKey& registration_key,
                          BackgroundSyncRegistration* existing_registration);

  // Store all registrations for a given |sw_registration_id|.
  void StoreRegistrations(const GURL& origin,
                          int64 sw_registration_id,
                          const ServiceWorkerStorage::StatusCallback& callback);

  // Removes the registration if it is in the map.
  void RemoveRegistrationFromMap(int64 sw_registration_id,
                                 const RegistrationKey& registration_key);

  void AddRegistrationToMap(
      int64 sw_registration_id,
      const BackgroundSyncRegistration& sync_registration);

  void InitImpl(const base::Closure& callback);
  void InitDidGetDataFromBackend(
      const base::Closure& callback,
      const std::vector<std::pair<int64, std::string>>& user_data,
      ServiceWorkerStatusCode status);

  // Register callbacks
  void RegisterImpl(const GURL& origin,
                    int64 sw_registration_id,
                    const BackgroundSyncRegistration& sync_registration,
                    const StatusAndRegistrationCallback& callback);
  void RegisterDidStore(int64 sw_registration_id,
                        const BackgroundSyncRegistration& sync_registration,
                        const StatusAndRegistrationCallback& callback,
                        ServiceWorkerStatusCode status);

  // Unregister callbacks
  void UnregisterImpl(
      const GURL& origin,
      int64 sw_registration_id,
      const RegistrationKey& registration_key,
      BackgroundSyncRegistration::RegistrationId sync_registration_id,
      const StatusCallback& callback);
  void UnregisterDidStore(
      int64 sw_registration_id,
      const StatusCallback& callback,
      ServiceWorkerStatusCode status);

  // GetRegistration callbacks
  void GetRegistrationImpl(const GURL& origin,
                           int64 sw_registration_id,
                           const RegistrationKey& registration_key,
                           const StatusAndRegistrationCallback& callback);

  // OnRegistrationDeleted callbacks
  void OnRegistrationDeletedImpl(int64 registration_id,
                                 const base::Closure& callback);

  // OnStorageWiped callbacks
  void OnStorageWipedImpl(const base::Closure& callback);

  void OnNetworkChanged();

  // Operation Scheduling callbacks
  void PendingStatusAndRegistrationCallback(
      const StatusAndRegistrationCallback& callback,
      ErrorType error,
      const BackgroundSyncRegistration& sync_registration);
  void PendingStatusCallback(const StatusCallback& callback, ErrorType error);
  void PendingClosure(const base::Closure& closure);

  StatusAndRegistrationCallback MakeStatusAndRegistrationCompletion(
      const StatusAndRegistrationCallback& callback);
  BackgroundSyncManager::StatusCallback MakeStatusCompletion(
      const StatusCallback& callback);
  base::Closure MakeEmptyCompletion();

  SWIdToRegistrationsMap sw_to_registrations_map_;
  CacheStorageScheduler op_scheduler_;
  scoped_refptr<ServiceWorkerContextWrapper> service_worker_context_;
  bool disabled_;

  scoped_ptr<BackgroundSyncNetworkObserver> network_observer_;

  base::WeakPtrFactory<BackgroundSyncManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundSyncManager);
};

}  // namespace content

#endif  // CONTENT_BROWSER_BACKGROUND_SYNC_BACKGROUND_SYNC_MANAGER_H_
