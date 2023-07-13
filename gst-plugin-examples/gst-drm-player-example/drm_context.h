/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <glib-unix.h>

#include <chrono>
#include <future>
#include <map>
#include <string>
#include <vector>

#ifdef ENABLE_WIDEVINE
#include <ce_cdm/cdm.h>
#endif
#include <media/drm/DrmAPI.h>

class DrmContext {
  public:
    DrmContext(gchar * header) : init_data_ (header) {}
    virtual ~DrmContext() { g_free (init_data_); }

    virtual gint InitSession() = 0;
    virtual gint CreateLicenseRequest() = 0;
    virtual gint FetchLicense() = 0;
    virtual gint ProvideKeyResponse() = 0;

    const char* GetSessionId() { return session_id_.c_str(); }
    virtual void* GetCdmInstance() = 0;

    // Header parsed from manifest
    gchar                  *init_data_;

    // Session id returned after opening DRM session
    std::string            session_id_;

    // License challenge used to request license
    std::string            license_request_;

    // License response returned by license server
    std::string            license_response_;
};

class PlayreadyContext : public DrmContext {
  public:
    PlayreadyContext(gchar * header) : DrmContext (header),
                                       lib_handle_ (NULL),
                                       drm_plugin_ (nullptr) {}
    ~PlayreadyContext();

  private:
    gint InitSession() override;
    gint CreateLicenseRequest() override;
    gint FetchLicense() override;
    gint ProvideKeyResponse() override;

    void* GetCdmInstance() { return NULL; }

    void                           *lib_handle_;
    android::DrmPlugin             *drm_plugin_;
};

#ifdef ENABLE_WIDEVINE
class WidevineContext : public DrmContext, public widevine::Cdm::IEventListener {
  private:
    class WVStorageImpl : public widevine::Cdm::IStorage {
      public:
        WVStorageImpl() { cert_map_.clear(); }

        bool read(const std::string& name, std::string* data) override {
          auto it = cert_map_.find (name);
          if (it == cert_map_.end())
            return false;

          *data = it->second;
          return true;
        }

        bool write(const std::string& name, const std::string& data) override {
          cert_map_[name] = data;
          return true;
        }

        bool exists(const std::string& name) override {
          return (cert_map_.find(name) != cert_map_.end());
        }

        bool remove(const std::string& name) override {
          if (name.empty()) {
            cert_map_.clear();
            return true;
          }

          return cert_map_.erase(name) > 0;
        }

        int32_t size(const std::string& name) override {
          auto it = cert_map_.find(name);

          if (it == cert_map_.end())
            return -1;

          return it->second.size();
        }

        bool list(std::vector<std::string>* names) override {
          names->clear();
          for (auto it = cert_map_.begin(); it != cert_map_.end(); it++)
            names->push_back (it->first);

          return true;
        }

      private:
        std::map<std::string, std::string> cert_map_;
    };

    class WVClockImpl : public widevine::Cdm::IClock {
      public:
        WVClockImpl() {
          auto now = std::chrono::steady_clock().now();
          curr_time_ = now.time_since_epoch() / std::chrono::milliseconds(1);
        }

        int64_t now() override { return curr_time_; }

      private:
        int64_t curr_time_;
    };

    class WVTimerImpl : public widevine::Cdm::ITimer {
      public:
        void setTimeout(int64_t delay_ms, IClient* client, void* context) override {}
        void cancel(IClient* client) override {}
    };

    gint InitSession() override;
    gint CreateLicenseRequest() override;
    gint FetchLicense() override;
    gint ProvideKeyResponse() override;

    widevine::Cdm* GetCdmInstance() { return cdm_; }
    std::string FetchProvisioningResponse(std::string request);

    widevine::Cdm                  *cdm_;
    std::promise<std::string>      on_message_;

  public:
    WidevineContext(gchar * header) : DrmContext (header), cdm_ (nullptr) {
      storage_impl = new WVStorageImpl();
      clock_impl = new WVClockImpl();
      timer_impl = new WVTimerImpl();
    }
    ~WidevineContext();

    // widevine::Cdm::IEventListener
    void onMessage(const std::string& session_id,
                    widevine::Cdm::MessageType message_type,
                    const std::string& message) override {
      if (session_id == session_id_ && message_type == widevine::Cdm::kLicenseRequest)
        on_message_.set_value (message);
      else
        on_message_.set_value (nullptr);
    }
    void onKeyStatusesChange (const std::string& session_id,
        bool has_new_usable_key) override {}
    void onRemoveComplete(const std::string& session_id) override {}

    WVStorageImpl *storage_impl;
    WVClockImpl   *clock_impl;
    WVTimerImpl   *timer_impl;
};
#endif
