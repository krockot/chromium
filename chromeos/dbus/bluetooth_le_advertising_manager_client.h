// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_BLUETOOTH_LE_ADVERTISING_MANAGER_CLIENT_H_
#define CHROMEOS_DBUS_BLUETOOTH_LE_ADVERTISING_MANAGER_CLIENT_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/values.h"
#include "chromeos/chromeos_export.h"
#include "chromeos/dbus/dbus_client.h"
#include "dbus/object_path.h"

namespace chromeos {

// BluetoothAdvertisingManagerClient is used to communicate with the advertising
// manager object of the BlueZ daemon.
class CHROMEOS_EXPORT BluetoothLEAdvertisingManagerClient : public DBusClient {
 public:
  // Interface for observing changes to advertising managers.
  class Observer {
   public:
    virtual ~Observer() {}

    // Called when an advertising manager with object path |object_path| is
    // added to the system.
    virtual void AdvertisingManagerAdded(const dbus::ObjectPath& object_path) {}

    // Called when an advertising manager with object path |object_path| is
    // removed from the system.
    virtual void AdvertisingManagerRemoved(
        const dbus::ObjectPath& object_path) {}
  };

  ~BluetoothLEAdvertisingManagerClient() override;

  // Adds and removes observers for events which change the advertising
  // managers on the system.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  // The ErrorCallback is used by advertising manager methods to indicate
  // failure. It receives two arguments: the name of the error in |error_name|
  // and an optional message in |error_message|.
  using ErrorCallback = base::Callback<void(const std::string& error_name,
                                            const std::string& error_message)>;

  // Registers an advertisement with the DBus object path
  // |advertisement_object_path| with BlueZ's advertising manager.
  virtual void RegisterAdvertisement(
      const dbus::ObjectPath& manager_object_path,
      const dbus::ObjectPath& advertisement_object_path,
      const base::Closure& callback,
      const ErrorCallback& error_callback) = 0;

  // Unregisters an advertisement with the DBus object path
  // |advertisement_object_path| with BlueZ's advertising manager.
  virtual void UnregisterAdvertisement(
      const dbus::ObjectPath& manager_object_path,
      const dbus::ObjectPath& advertisement_object_path,
      const base::Closure& callback,
      const ErrorCallback& error_callback) = 0;

  // Creates the instance.
  static BluetoothLEAdvertisingManagerClient* Create();

  // Constants used to indicate exceptional error conditions.
  static const char kNoResponseError[];

 protected:
  BluetoothLEAdvertisingManagerClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(BluetoothLEAdvertisingManagerClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_BLUETOOTH_LE_ADVERTISING_MANAGER_CLIENT_H_
