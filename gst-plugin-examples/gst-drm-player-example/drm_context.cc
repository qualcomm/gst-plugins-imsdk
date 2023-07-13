/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "drm_context.h"

#include <curl/curl.h>
#include <dlfcn.h>
#include <utils/Vector.h>

#define PRDRM_SUCCESS          0
#define PRDRM_FAILED           -1

#define DRM_LIB_PATH           "/usr/lib/libprdrmengine.so"

// Type : PERSIST_FALSE_SECURESTOP_FALSE_SL150
#define CONTENT_TYPE           "Content-Type: text/xml; charset=utf-8"
#define SOAP_ACTION            "SOAPAction: ""\"http://schemas.microsoft.com/" \
    "DRM/2007/03/protocols/AcquireLicense\""
#define LA_URL                 "https://test.playready.microsoft.com/service/" \
    "rightsmanager.asmx?cfg=(securestop:false,persist:false,sl:150)"

// To be obtained and specified post Widevine license agreement.
#define CDM_PROV_URL           ""
#define CDM_LIC_URL            ""

const std::string kProductName = "DRMPlayer";
const std::string kCompanyName = "QTI";
const std::string kModelName   = "QRB5165";

// To store license request and response data
struct soapbuf {
  gchar   *pdata;
  size_t  sdata;
};

static void
str_to_vec (std::string s, android::Vector<uint8_t> & v)
{
  v.appendArray (reinterpret_cast<const uint8_t*> (s.data()), s.size());
}

static std::string
vec_to_str (android::Vector<uint8_t> v)
{
  std::string s (v.begin(), v.end());
  return std::move (s);
}

// WRITEFUNCTION callback for curl for fetching license
static size_t
soap_callback (gchar * buffer, size_t size, size_t nitems, void * outstream)
{
  struct soapbuf *write_buf = (soapbuf *) outstream;
  size_t write_buf_size = size * nitems;

  write_buf->pdata = (gchar *) realloc (write_buf->pdata,
      write_buf->sdata + write_buf_size + 1);

  if (write_buf->pdata == NULL) {
    g_printerr ("ERROR: Memory allocation failed\n");
    return 0;
  }

  memcpy (write_buf->pdata + write_buf->sdata, buffer, write_buf_size);
  write_buf->sdata += write_buf_size;
  write_buf->pdata [write_buf->sdata] = '\0';

  return write_buf_size;
}

static gint
perform_curl (gchar * url, struct curl_slist * http_header,
    gchar ** post_data, size_t * post_data_size)
{
  CURL *curl = NULL;
  struct soapbuf soapbuf;
  glong response_code = -1;
  gint ret = -1;

  if (post_data == NULL || *post_data_size == 0) {
    g_print ("ERROR: Post data not available.\n");
    return ret;
  }

  if (curl_global_init (CURL_GLOBAL_ALL) != CURLE_OK) {
    g_printerr ("ERROR: Curl global init failed.\n");
    return ret;
  }

  if ((curl = curl_easy_init()) == NULL) {
    g_printerr ("ERROR: Curl easy init failed.\n");
    curl_global_cleanup();
    return ret;
  }

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, http_header);
  curl_easy_setopt (curl, CURLOPT_POST, 1L);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE_LARGE, *post_data_size);
  curl_easy_setopt (curl, CURLOPT_COPYPOSTFIELDS, *post_data);

  soapbuf.pdata = *post_data;
  soapbuf.sdata = 0;

  curl_easy_setopt (curl, CURLOPT_WRITEDATA, &soapbuf);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, soap_callback);

  g_print ("Acquiring message from server...\n");

  if ((ret = curl_easy_perform (curl)) != CURLE_OK) {
    g_print ("Curl error %d\n", ret);
    goto curl_error;
  }

  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &response_code);

  if (response_code != 200) {
    g_printerr ("Response error: %ld", response_code);
    ret = -1;
    goto curl_error;
  }

  // Response data
  *post_data = soapbuf.pdata;
  *post_data_size = soapbuf.sdata;

curl_error:
  curl_slist_free_all (http_header);
  curl_easy_cleanup (curl);
  curl_global_cleanup();

  return ret;
}

gint
PlayreadyContext::InitSession ()
{
  // PlayReady UUID in hex
  const uint8_t pr_uuid[16] = {
    0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86,
    0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95
  };

  // For PR3.0 and above
  android::DrmFactory *drm_factory = nullptr;
  gint result = PRDRM_FAILED;

  {
    // Load library.
    gchar *libpath = (gchar *) DRM_LIB_PATH;

    g_print ("Trying to load %s\n", libpath);

    if ((lib_handle_ = dlopen (libpath, RTLD_NOW)) == NULL) {
      g_printerr ("ERROR: Cannot load library, dlerror = %s\n", dlerror());
      return result;
    }
    g_print ("Library loaded successfully.\n");
  }

  {
    // Create DRMFactory object.
    gchar *err = NULL;

    typedef android::DrmFactory *(*createDrmFactoryFunc)();
    createDrmFactoryFunc createDrmFactory =
      (createDrmFactoryFunc) dlsym (lib_handle_, "createDrmFactory");

    if ((drm_factory = createDrmFactory()) == NULL &&
        (err = dlerror()) != NULL) {
      g_printerr ("ERROR: Cannot find symbol, dlerror = %s\n", err);

      g_free (err);
      return result;
    } else if (drm_factory == NULL) {
      return result;
    }

    if (!drm_factory->isCryptoSchemeSupported (pr_uuid)) {
      g_printerr ("ERROR: Check given PR UUID\n");
      delete drm_factory;
      return result;
    }
    g_print ("Created DRMFactory.\n");
  }

  {
    // Create DRMPlugin object.
    result = drm_factory->createDrmPlugin (pr_uuid, &drm_plugin_);
    delete drm_factory;

    if (result != PRDRM_SUCCESS) {
      g_printerr ("ERROR: Couldn't create DrmPlugin \n");
      return result;
    }
    g_print ("Created DrmPlugin.\n");
  }

  {
    // Open DRM session.
    android::Vector<uint8_t> sid;

    if ((result = drm_plugin_->openSession (sid)) != PRDRM_SUCCESS) {
      g_printerr ("ERROR: Couldn't create session \n");
      return result;
    }

    session_id_ = vec_to_str (sid);
    g_print ("Opened DRM Session with session ID %s\n", session_id_.c_str());
  }

  return result;
}

gint
PlayreadyContext::CreateLicenseRequest ()
{
  guchar *decoded_str = NULL;
  android::Vector<uint8_t> pro_header, request, sid;
  android::KeyedVector<android::String8, android::String8> const optional_parameters;
  android::DrmPlugin::KeyType key_type = android::DrmPlugin::kKeyType_Streaming;
  android::DrmPlugin::KeyRequestType key_request_type;
  android::String8 mime_type, default_url;
  gint result = PRDRM_FAILED;
  gsize out_len;

  // Decode base64 encoded PlayReady object.
  decoded_str = g_base64_decode (init_data_, &out_len);
  pro_header.appendArray (reinterpret_cast<const uint8_t*> (decoded_str),
      out_len);
  g_free (decoded_str);

  str_to_vec (session_id_, sid);

  g_print ("Creating license request...\n");

  if ((result = drm_plugin_->getKeyRequest (sid, pro_header,
      mime_type, key_type, optional_parameters, request, default_url,
      &key_request_type)) == PRDRM_SUCCESS) {
    g_print ("License request created successfully.\n");
    license_request_ = vec_to_str (request);
  }

  return result;
}

gint
PlayreadyContext::FetchLicense ()
{
  struct curl_slist *http_header = NULL;
  gchar *content_type = (gchar *) CONTENT_TYPE;
  gchar *url = (gchar *) LA_URL;
  gchar *req_buf = NULL;
  size_t req_buf_size;
  gint result = PRDRM_FAILED;

  http_header = curl_slist_append (http_header, SOAP_ACTION);
  http_header = curl_slist_append (http_header, content_type);

  if (license_request_.empty()) {
    g_print ("License request object is empty.\n");
    return result;
  }

  req_buf_size = license_request_.length();
  req_buf = g_strndup (license_request_.c_str(), req_buf_size);

  if ((result = perform_curl (url, http_header, &req_buf, &req_buf_size))
      == PRDRM_SUCCESS) {
    g_print ("License acquired from license server successfully.\n");
    license_response_.assign (req_buf, req_buf + req_buf_size);
  }

  g_free (req_buf);

  return result;
}

gint
PlayreadyContext::ProvideKeyResponse ()
{
  android::Vector<uint8_t> req_id;
  android::Vector<uint8_t> sid;
  android::Vector<uint8_t> response;
  gint result = PRDRM_FAILED;

  str_to_vec (session_id_, sid);
  str_to_vec (license_response_, response);

  if ((result = drm_plugin_->provideKeyResponse (sid,
      response, req_id)) == PRDRM_SUCCESS)
    g_print ("Provided license response to DRMPlugin successfully.\n");

  return result;
}

PlayreadyContext::~PlayreadyContext ()
{
  android::Vector<uint8_t> sid;

  if (lib_handle_ == NULL)
    return;

  if (drm_plugin_ == nullptr) {
    dlclose (lib_handle_);
    return;
  }

  str_to_vec (session_id_, sid);

  if (drm_plugin_->closeSession (sid) != PRDRM_SUCCESS)
    g_printerr ("ERROR: Close session failed\n");
  else
    g_print ("Session closed successfully\n");

  delete drm_plugin_;
  dlclose (lib_handle_);
}

#ifdef ENABLE_WIDEVINE
std::string
WidevineContext::FetchProvisioningResponse (std::string request)
{
  struct curl_slist *http_header = NULL;
  gchar *url = NULL;
  gchar *req_buf = NULL;
  std::string response;
  size_t req_buf_size;

  req_buf_size = request.length();
  req_buf = g_strndup (request.c_str(), req_buf_size);

  url = g_strconcat ((const gchar *) CDM_PROV_URL, req_buf, NULL);

  http_header = curl_slist_append (http_header, "Host: www.googleapis.com");
  http_header = curl_slist_append (http_header, "Connection: close");
  http_header = curl_slist_append (http_header, "User-Agent: Widevine CDM v1.0");

  if (perform_curl (url, http_header, &req_buf, &req_buf_size) != 0) {
    g_free (url);
    return nullptr;
  }

  response.assign (req_buf, req_buf + req_buf_size);
  g_free (req_buf);
  g_free (url);

  return response;
}

gint
WidevineContext::InitSession ()
{
  std::string prov_request, prov_response;
  widevine::Cdm::Status status = widevine::Cdm::kTypeError;
  widevine::Cdm::ClientInfo client_info;
  client_info.product_name = kProductName;
  client_info.company_name = kCompanyName;
  client_info.model_name = kModelName;

  // Initialize the CDM Library.
  if ((status = widevine::Cdm::initialize (widevine::Cdm::kOpaqueHandle,
      client_info, storage_impl, clock_impl, timer_impl, widevine::Cdm::kErrors))
      != widevine::Cdm::kSuccess) {
    g_printerr ("ERROR: Couldn't initialize the CDM Library! \n");
    return status;
  }
  g_print ("Initialized the CDM Library.\n");

  // Create a CDM instance.
  if ((cdm_ = widevine::Cdm::create (this, storage_impl, FALSE)) == nullptr) {
    g_printerr ("ERROR: Couldn't create new CDM instance! \n");
    return widevine::Cdm::kTypeError;
  }
  g_print ("Created new CDM instance.\n");

  // Provision the device if not provisioned.
  if (!cdm_->isProvisioned()) {
    g_print ("Device is not provisioned. Provisioning first...\n");

    if ((status = cdm_->getProvisioningRequest (&prov_request))
        != widevine::Cdm::kSuccess) {
      g_printerr ("ERROR: Creation of Provisioning Request message failed! \n");
      return status;
    }

    prov_response = FetchProvisioningResponse (prov_request);
    if (prov_response.empty()) {
      g_printerr ("ERROR: Couldn't fetch provisioning response! \n");
      return widevine::Cdm::kTypeError;
    }

    if ((status = cdm_->handleProvisioningResponse (prov_response))
        != widevine::Cdm::kSuccess) {
      g_printerr ("ERROR: Provisioning device failed! \n");
      return status;
    }

    g_print ("Device provisioned successfully! \n");
  }

  // Create a new CDM session.
  if ((status = cdm_->createSession (widevine::Cdm::kTemporary,
      &session_id_)) != widevine::Cdm::kSuccess) {
    g_printerr ("ERROR: Couldn't create session \n");
    return status;
  }
  g_print ("Opened DRM Session with session ID %s\n", session_id_.c_str());

  return status;
}

gint
WidevineContext::CreateLicenseRequest ()
{
  guchar *decoded_str = NULL;
  widevine::Cdm::Status status = widevine::Cdm::kTypeError;
  std::string wv_header;
  gsize out_len;

  // Decode base64 encoded Widevine object.
  decoded_str = g_base64_decode (init_data_, &out_len);
  wv_header.assign (decoded_str, decoded_str + out_len);
  g_free (decoded_str);

  if ((status = cdm_->generateRequest (session_id_, widevine::Cdm::kCenc, wv_header)) !=
      widevine::Cdm::kSuccess) {
    g_printerr ("ERROR: Creation of license request failed.\n");
    return status;
  }

  // Block and wait for onMessage callback to be triggered
  // to get the license request message.
  std::future<std::string> req_message = on_message_.get_future();
  license_request_ = req_message.get();
  if (license_request_.empty()) {
    g_printerr ("ERROR: Received empty license request message.\n");
    return widevine::Cdm::kTypeError;
  }

  g_print ("License request created successfully.\n");
  return status;
}

gint
WidevineContext::FetchLicense ()
{
  struct curl_slist *http_header = NULL;
  gchar *url = (gchar *) CDM_LIC_URL;
  gchar *req_buf = NULL;
  size_t req_buf_size;

  req_buf_size = license_request_.length();
  req_buf = const_cast <gchar *> (license_request_.c_str());

  http_header = curl_slist_append (http_header, "Host: proxy.uat.widevine.com");
  http_header = curl_slist_append (http_header, "Connection: close");
  http_header = curl_slist_append (http_header, "User-Agent: Widevine CDM v1.0");

  if (perform_curl (url, http_header, &req_buf, &req_buf_size) != CURLE_OK)
    return widevine::Cdm::kTypeError;

  license_response_.assign (req_buf, req_buf + req_buf_size);

  g_print ("License fetched from license server successfully.\n");

  return widevine::Cdm::kSuccess;
}

gint
WidevineContext::ProvideKeyResponse ()
{
  widevine::Cdm::Status status = widevine::Cdm::kTypeError;

  if ((status = cdm_->update (session_id_, license_response_)) == widevine::Cdm::kSuccess)
      g_print ("Provided license response to CDM successfully.\n");

  return status;
}

WidevineContext::~WidevineContext ()
{
  delete storage_impl;
  delete clock_impl;
  delete timer_impl;

  if (cdm_ == nullptr)
    return;

  if (cdm_->close (session_id_) != widevine::Cdm::kSuccess)
    g_printerr ("ERROR: Close session failed\n");
  else
    g_print ("Session closed successfully\n");
}
#endif
