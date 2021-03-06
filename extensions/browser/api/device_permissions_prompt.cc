// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/device_permissions_prompt.h"

#include "base/barrier_closure.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/browser_thread.h"
#include "device/core/device_client.h"
#include "device/usb/usb_device.h"
#include "device/usb/usb_device_filter.h"
#include "device/usb/usb_ids.h"
#include "device/usb/usb_service.h"
#include "extensions/browser/api/device_permissions_manager.h"
#include "extensions/common/extension.h"
#include "extensions/strings/grit/extensions_strings.h"
#include "ui/base/l10n/l10n_util.h"

using content::BrowserThread;
using device::UsbDevice;
using device::UsbDeviceFilter;
using device::UsbService;

namespace extensions {

DevicePermissionsPrompt::Delegate::~Delegate() {
}

DevicePermissionsPrompt::Prompt::DeviceInfo::DeviceInfo(
    scoped_refptr<UsbDevice> device)
    : device(device) {
  base::string16 manufacturer_string = device->manufacturer_string();
  if (manufacturer_string.empty()) {
    const char* vendor_name =
        device::UsbIds::GetVendorName(device->vendor_id());
    if (vendor_name) {
      manufacturer_string = base::UTF8ToUTF16(vendor_name);
    } else {
      base::string16 vendor_id =
          base::ASCIIToUTF16(base::StringPrintf("0x%04x", device->vendor_id()));
      manufacturer_string =
          l10n_util::GetStringFUTF16(IDS_DEVICE_UNKNOWN_VENDOR, vendor_id);
    }
  }

  base::string16 product_string = device->product_string();
  if (product_string.empty()) {
    const char* product_name = device::UsbIds::GetProductName(
        device->vendor_id(), device->product_id());
    if (product_name) {
      product_string = base::UTF8ToUTF16(product_name);
    } else {
      base::string16 product_id = base::ASCIIToUTF16(
          base::StringPrintf("0x%04x", device->product_id()));
      product_string =
          l10n_util::GetStringFUTF16(IDS_DEVICE_UNKNOWN_PRODUCT, product_id);
    }
  }

  name = l10n_util::GetStringFUTF16(IDS_DEVICE_PERMISSIONS_DEVICE_NAME,
                                    product_string, manufacturer_string);
}

DevicePermissionsPrompt::Prompt::DeviceInfo::~DeviceInfo() {
}

DevicePermissionsPrompt::Prompt::Observer::~Observer() {
}

DevicePermissionsPrompt::Prompt::Prompt(Delegate* delegate,
                                        const Extension* extension,
                                        content::BrowserContext* context)
    : extension_(extension),
      browser_context_(context),
      delegate_(delegate),
      usb_service_observer_(this) {
}

void DevicePermissionsPrompt::Prompt::SetObserver(Observer* observer) {
  observer_ = observer;

  if (observer_) {
    UsbService* service = device::DeviceClient::Get()->GetUsbService();
    if (service && !usb_service_observer_.IsObserving(service)) {
      service->GetDevices(base::Bind(
          &DevicePermissionsPrompt::Prompt::OnDevicesEnumerated, this));
      usb_service_observer_.Add(service);
    }
  }
}

base::string16 DevicePermissionsPrompt::Prompt::GetHeading() const {
  return l10n_util::GetStringUTF16(
      multiple_ ? IDS_DEVICE_PERMISSIONS_PROMPT_TITLE_MULTIPLE
                : IDS_DEVICE_PERMISSIONS_PROMPT_TITLE_SINGLE);
}

base::string16 DevicePermissionsPrompt::Prompt::GetPromptMessage() const {
  return l10n_util::GetStringFUTF16(multiple_
                                        ? IDS_DEVICE_PERMISSIONS_PROMPT_MULTIPLE
                                        : IDS_DEVICE_PERMISSIONS_PROMPT_SINGLE,
                                    base::UTF8ToUTF16(extension_->name()));
}

base::string16 DevicePermissionsPrompt::Prompt::GetDeviceName(
    size_t index) const {
  DCHECK_LT(index, devices_.size());
  return devices_[index].name;
}

base::string16 DevicePermissionsPrompt::Prompt::GetDeviceSerialNumber(
    size_t index) const {
  DCHECK_LT(index, devices_.size());
  return devices_[index].device->serial_number();
}

void DevicePermissionsPrompt::Prompt::GrantDevicePermission(size_t index) {
  DCHECK_LT(index, devices_.size());
  devices_[index].granted = true;
}

void DevicePermissionsPrompt::Prompt::Dismissed() {
  DevicePermissionsManager* permissions_manager =
      DevicePermissionsManager::Get(browser_context_);
  std::vector<scoped_refptr<UsbDevice>> devices;
  for (const DeviceInfo& device : devices_) {
    if (device.granted) {
      devices.push_back(device.device);
      if (permissions_manager) {
        permissions_manager->AllowUsbDevice(extension_->id(), device.device);
      }
    }
  }
  delegate_->OnUsbDevicesChosen(devices);
}

void DevicePermissionsPrompt::Prompt::set_filters(
    const std::vector<UsbDeviceFilter>& filters) {
  filters_ = filters;
}

DevicePermissionsPrompt::Prompt::~Prompt() {
}

void DevicePermissionsPrompt::Prompt::OnDeviceAdded(
    scoped_refptr<UsbDevice> device) {
  if (!(filters_.empty() || UsbDeviceFilter::MatchesAny(device, filters_))) {
    return;
  }

  device->CheckUsbAccess(base::Bind(
      &DevicePermissionsPrompt::Prompt::AddCheckedUsbDevice, this, device));
}

void DevicePermissionsPrompt::Prompt::OnDeviceRemoved(
    scoped_refptr<UsbDevice> device) {
  bool removed_entry = false;
  for (std::vector<DeviceInfo>::iterator it = devices_.begin();
       it != devices_.end(); ++it) {
    if (it->device == device) {
      devices_.erase(it);
      removed_entry = true;
      break;
    }
  }
  if (observer_ && removed_entry) {
    observer_->OnDevicesChanged();
  }
}

void DevicePermissionsPrompt::Prompt::OnDevicesEnumerated(
    const std::vector<scoped_refptr<UsbDevice>>& devices) {
  for (const auto& device : devices) {
    if (filters_.empty() || UsbDeviceFilter::MatchesAny(device, filters_)) {
      device->CheckUsbAccess(base::Bind(
          &DevicePermissionsPrompt::Prompt::AddCheckedUsbDevice, this, device));
    }
  }
}

void DevicePermissionsPrompt::Prompt::AddCheckedUsbDevice(
    scoped_refptr<UsbDevice> device,
    bool allowed) {
  if (allowed) {
    devices_.push_back(DeviceInfo(device));
    if (observer_) {
      observer_->OnDevicesChanged();
    }
  }
}

DevicePermissionsPrompt::DevicePermissionsPrompt(
    content::WebContents* web_contents)
    : web_contents_(web_contents) {
}

DevicePermissionsPrompt::~DevicePermissionsPrompt() {
}

void DevicePermissionsPrompt::AskForUsbDevices(
    Delegate* delegate,
    const Extension* extension,
    content::BrowserContext* context,
    bool multiple,
    const std::vector<UsbDeviceFilter>& filters) {
  prompt_ = new Prompt(delegate, extension, context);
  prompt_->set_multiple(multiple);
  prompt_->set_filters(filters);

  ShowDialog();
}

}  // namespace extensions
