/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <string>
#include <dlfcn.h>
#include <utils/Vector.h>

#include <gst/gst.h>
#include <glib-unix.h>
#include <libxml/parser.h>
#include <curl/curl.h>

#include <media/drm/DrmAPI.h>

#define DASH_LINE  "-------------------------------------------------------"
#define SPACE      "                                                       "

// Manifest will be downloaded here.
#define MANIFEST_DOWNLOAD_PATH "/data/manifest.xml"

#define DRM_LIB_PATH           "/usr/lib/libprdrmengine.so"

// Type : PERSIST_FALSE_SECURESTOP_FALSE_SL150
#define CONTENT_TYPE           "Content-Type: text/xml; charset=utf-8"
#define SOAP_ACTION            "SOAPAction: ""\"http://schemas.microsoft.com/" \
    "DRM/2007/03/protocols/AcquireLicense\""
#define LA_URL                 "https://test.playready.microsoft.com/service/" \
    "rightsmanager.asmx?cfg=(securestop:false,persist:false,sl:150)"

// DRM UUIDs
#define PLAYREADY_UUID         "urn:uuid:9a04f079-9840-4286-ab92-e65be0885f95"
#define WIDEVINE_UUID          "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"

// Menu options
#define PLAY                   "p"
#define STOP                   "s"
#define QUIT                   "q"

// For inter-thread communication
#define TERMINATE_MESSAGE      "APP_TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE "APP_PIPELINE_STATE_MSG"
#define STDIN_MESSAGE          "APP_STDIN_MSG"

#define OPENING_TAG_HLS        "#EXTM3U"
#define OPENING_TAG_DASH       "<?xml"

#define PRDRM_SUCCESS          0
#define PRDRM_FAILED           -1

typedef enum {
  LICENSE_NONE,
  LICENSE_PLAYREADY,
  LICENSE_WIDEVINE,
  LICENSE_BOTH,
  LICENSE_INVALID
} DrmLicense;

typedef struct _GstAppContext GstAppContext;
struct _GstAppContext {
  // GStreamer pipeline instance
  GstElement    *pipeline;

  // Main application event loop
  GMainLoop     *mloop;

  // Queue for asynchronous communication b/w threads
  GAsyncQueue   *messages;

  // Current state of pipeline
  GstState      current_state;

  // State the pipeline is desired to switch to after buffering is done
  GstState      desired_state;

  // Boolean variable indicating whether the pipeline is buffering
  gboolean      buffering;

  // Boolean variable indicating whether the pipeline is live
  gboolean      live;
};

typedef struct _DrmPlayer DrmPlayer;
struct _DrmPlayer {
  // Handle for PRDRMEngine library
  void                   *lib_handle;

  // DRMPlugin object instance
  android::DrmPlugin     *drm_plugin;

  // PlayReady object header
  gchar                  *pro_header;

  // Session id returned after opening DRM session
  std::string            drm_session_id;

  // License challenge used to request license
  std::string            la_request;

  // License response returned by license server
  std::string            la_response;
};

// To store license request and response data
struct soapbuf {
  gchar   *pdata;
  size_t  sdata;
};

// PlayReady UUID in hex
static const uint8_t pr_uuid[16] = {
  0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86,
  0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95
};

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = NULL;
  g_return_val_if_fail ((ctx = g_new0 (GstAppContext, 1)) != NULL, NULL);

  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->messages = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);

  ctx->current_state = GST_STATE_NULL;
  ctx->desired_state = GST_STATE_PLAYING;
  ctx->buffering = FALSE;
  ctx->live = FALSE;

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx == NULL)
    return;

  if (ctx->pipeline != NULL)
    gst_object_unref (ctx->pipeline);

  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  g_async_queue_unref (ctx->messages);

  g_free (ctx);
  return;
}

static void
str_to_vec (std::string s, android::Vector<uint8_t> & v)
{
  v.appendArray (reinterpret_cast<const uint8_t*> (s.data()), s.size());
}

static std::string
vec_to_str (android::Vector<uint8_t> v)
{
  std::string s (v.begin(), v.end());
  return s;
}

static DrmPlayer *
drm_player_new (gchar * pro_header)
{
  DrmPlayer *player = NULL;
  g_return_val_if_fail ((player = g_new0 (DrmPlayer, 1)) != NULL, NULL);

  player->lib_handle = NULL;
  player->drm_plugin = NULL;
  player->pro_header = pro_header;

  return player;
}

static void
drm_player_free (DrmPlayer * player)
{
  android::Vector<uint8_t> session_id;

  if (player == NULL)
    return;

  g_free (player->pro_header);

  if (player->lib_handle == NULL) {
    g_free (player);
    return;
  }

  str_to_vec (player->drm_session_id, session_id);

  if (player->drm_plugin->closeSession (session_id) != PRDRM_SUCCESS)
    g_printerr ("ERROR: Close session failed\n");
  else
    g_print ("Session closed successfully\n");

  delete player->drm_plugin;
  dlclose (player->lib_handle);

  g_free (player);
  return;
}

static gboolean
wait_stdin_message (GAsyncQueue * queue, gchar ** input)
{
  GstStructure *message = NULL;

  // Clear input from previous use.
  g_free (*input);
  *input = NULL;

  // Keep executing the loop until eos/error msg or user input is provided.
  while ((message = (GstStructure *) g_async_queue_pop (queue)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      // Returning FALSE will cause menu thread to terminate.
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      gst_structure_free (message);
      return TRUE;
    }

    gst_structure_free (message);
  }

  return TRUE;
}

static gboolean
wait_pipeline_state_message (GAsyncQueue * messages, GstState state)
{
  GstStructure *message = NULL;

  // Pipeline does not notify us when changing to NULL state, skip wait.
  if (state == GST_STATE_NULL)
    return TRUE;

  // Wait for either a PIPELINE_STATE or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      GstState newstate = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "new", (guint*) &newstate);

      if (newstate == state)
        break;
    }

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
update_pipeline_state (GstAppContext * appctx, GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (appctx->pipeline, &current, &pending, 0);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: Failed to retrieve pipeline state!\n");
    return FALSE;
  }

  if (state == current) {
    g_print ("Already in %s state\n", gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("Pending %s state\n", gst_element_state_get_name (state));
    return TRUE;
  }

  g_print ("Setting pipeline to %s\n", gst_element_state_get_name (state));
  ret = gst_element_set_state (appctx->pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to %s state!\n",
          gst_element_state_get_name (state));

      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      appctx->live = TRUE;
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      ret = gst_element_get_state (appctx->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("ERROR: Pipeline failed to PREROLL!\n");
        return FALSE;
      }

      break;
    case GST_STATE_CHANGE_SUCCESS:
      break;
  }

  if (!wait_pipeline_state_message (appctx->messages, state))
    return FALSE;

  return TRUE;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;

  g_print ("\n\nReceived an interrupt signal, terminate ...\n");

  // Not sending EOS because the pipeline used doesn't receive EOS.
  g_async_queue_push (appctx->messages,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  return TRUE;
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition, gpointer data)
{
  GstAppContext *appctx = (GstAppContext *) data;
  gchar *input;
  GIOStatus status = G_IO_STATUS_NORMAL;

  // Keep trying to read the data until resource not available.
  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((status == G_IO_STATUS_ERROR) && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse input: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      return FALSE;
    } else if ((status == G_IO_STATUS_ERROR) && (error == NULL)) {
      g_printerr ("UNKNOWN ERROR: Failed to parse input! %.30s\n", SPACE);

      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  if (strlen (input) > 1)
    input = g_strchomp (input);

  g_async_queue_push (appctx->messages, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, input, NULL));

  g_free (input);
  return TRUE;
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * msg, gpointer data)
{
  GstAppContext *appctx = (GstAppContext *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
    {
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (msg));

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (TERMINATE_MESSAGE));

      break;
    }

    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      g_printerr ("ERROR: %s\n", err->message);

      if (dbg != NULL)
        g_printerr ("Debug information: %s\n", dbg);

      g_clear_error (&err);
      g_free (dbg);

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (TERMINATE_MESSAGE));

      break;
    }

    case GST_MESSAGE_WARNING:
    {
      GError *err = NULL;
      gchar *dbg = NULL;

      gst_message_parse_warning (msg, &err, &dbg);
      g_printerr ("WARNING %s\n", err->message);

      if (dbg != NULL)
        g_print ("WARNING debug information: %s\n", dbg);

      g_clear_error (&err);
      g_free (dbg);
      break;
    }

    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old_state, new_state, pending_state;

      if (GST_MESSAGE_SRC (msg) != GST_OBJECT_CAST (appctx->pipeline))
        break;

      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      g_print ("Pipeline state changed from %s to %s, pending: %s\n",
          gst_element_state_get_name (old_state),
          gst_element_state_get_name (new_state),
          gst_element_state_get_name (pending_state));

      g_async_queue_push (appctx->messages, gst_structure_new (
          PIPELINE_STATE_MESSAGE, "new", G_TYPE_UINT, new_state,
          "pending", G_TYPE_UINT, pending_state, NULL));

      appctx->current_state = new_state;
      break;
    }

    case GST_MESSAGE_BUFFERING:
    {
      gint percent;
      gst_message_parse_buffering (msg, &percent);

      if (percent == 100) {
        // Buffering is done, set the pipeline to previous state or state requested by user.
        if (!appctx->live)
          gst_element_set_state (appctx->pipeline, appctx->desired_state);

        appctx->buffering = FALSE;
      } else if (!appctx->buffering) {
        // Buffering started, set the pipeline to PAUSED.
        if (!appctx->live)
          gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);

        appctx->buffering = TRUE;
      }
      break;
    }

    case GST_MESSAGE_CLOCK_LOST:
    {
      // Clock is lost, set the pipeline to PAUSED and then to PLAYING again to select a new one.
      gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);
      break;
    }

    default:
      break;
  }

  // Keep listening to the bus.
  return TRUE;
}

static gint
init_playready (DrmPlayer * player)
{
  // For PR3.0 and above
  android::DrmFactory *drm_factory = nullptr;
  gint result = PRDRM_FAILED;

  {
    // Load library.
    gchar *libpath = (gchar *) DRM_LIB_PATH;

    g_print ("Trying to load %s\n", libpath);

    if ((player->lib_handle = dlopen (libpath, RTLD_NOW)) == NULL) {
      g_printerr ("ERROR: Cannot load library, dlerror = %s\n", dlerror());
      return result;
    } else {
      g_print ("Library loaded successfully.\n");
    }
  }

  {
    // Create DRMFactory object.
    gchar *err = NULL;

    typedef android::DrmFactory *(*createDrmFactoryFunc)();
    createDrmFactoryFunc createDrmFactory =
      (createDrmFactoryFunc) dlsym (player->lib_handle, "createDrmFactory");

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
    } else {
      g_print ("Created DRMFactory.\n");
    }
  }

  {
    // Create DRMPlugin object.
    result = drm_factory->createDrmPlugin (pr_uuid, &player->drm_plugin);
    delete drm_factory;

    if (result != PRDRM_SUCCESS) {
      g_printerr ("ERROR: Couldn't create DrmPlugin \n");
      return result;
    } else {
      g_print ("Created DrmPlugin.\n");
    }
  }

  {
    // Open DRM session.
    android::Vector<uint8_t> session_id;

    if ((result = player->drm_plugin->openSession (session_id)) != PRDRM_SUCCESS) {
      g_printerr ("ERROR: Couldn't create session \n");
      return result;
    } else {
      std::string sid (session_id.begin(), session_id.end());
      g_print ("Opened DRM Session with session ID %s\n", sid.c_str());
      player->drm_session_id.assign (sid);
    }
  }

  return result;
}

static gint
create_license_request (DrmPlayer * player)
{
  guchar *decoded_str = NULL;
  android::Vector<uint8_t> init_data, request, session_id;
  android::KeyedVector<android::String8, android::String8> const optional_parameters;
  android::DrmPlugin::KeyType key_type = android::DrmPlugin::kKeyType_Streaming;
  android::DrmPlugin::KeyRequestType key_request_type;
  android::String8 mime_type, default_url;
  gint result = PRDRM_FAILED;
  gsize out_len;

  // Decode base64 encoded PlayReady object.
  decoded_str = g_base64_decode (player->pro_header, &out_len);
  init_data.appendArray (reinterpret_cast<const uint8_t*> (decoded_str),
      out_len);
  g_free (decoded_str);

  str_to_vec (player->drm_session_id, session_id);

  g_print ("Creating license request...\n");

  if ((result = player->drm_plugin->getKeyRequest (session_id, init_data,
      mime_type, key_type, optional_parameters, request, default_url,
      &key_request_type)) == PRDRM_SUCCESS) {
    g_print ("License request created successfully.\n");
    player->la_request = vec_to_str (request);
  }

  return result;
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
acquire_license (gchar * url, struct curl_slist * http_header,
    gchar * content_type, gchar ** post_data, size_t * post_data_size)
{
  CURL *curl = NULL;
  struct soapbuf soapbuf;
  glong response_code = -1;
  gint ret = -1;

  if (post_data == NULL || *post_data_size == 0)
    return ret;

  if (curl_global_init (CURL_GLOBAL_ALL) != CURLE_OK) {
    g_printerr ("ERROR: Curl global init failed.\n");
    return ret;
  }

  if ((curl = curl_easy_init()) == NULL) {
    g_printerr ("ERROR: Curl easy init failed.\n");
    curl_global_cleanup();
    return ret;
  }

  http_header = curl_slist_append (http_header, content_type);
  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, http_header);
  curl_easy_setopt (curl, CURLOPT_POST, 1L);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, *post_data_size);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, *post_data);

  soapbuf.pdata = *post_data;
  soapbuf.sdata = 0;

  curl_easy_setopt (curl, CURLOPT_WRITEDATA, &soapbuf);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, soap_callback);

  g_print ("Acquiring license from server...\n");

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

static gint
create_soap_request (DrmPlayer * player)
{
  struct curl_slist *http_header = NULL;
  gchar *content_type = (gchar *) CONTENT_TYPE;
  gchar *url = (gchar *) LA_URL;
  gchar *req_buf = NULL;
  size_t req_buf_size;
  gint result = PRDRM_FAILED;

  http_header = curl_slist_append (http_header, SOAP_ACTION);

  if (player->la_request.empty()) {
    g_print ("License request object is empty.\n");
    return result;
  }

  {
    req_buf_size = player->la_request.length();
    req_buf = g_strndup (player->la_request.c_str(), req_buf_size);

    if ((result = acquire_license (url, http_header, content_type,
        &req_buf, &req_buf_size)) == PRDRM_SUCCESS) {
      g_print ("License acquired from license server successfully.\n");
      player->la_response.assign (req_buf, req_buf + req_buf_size);
    }

    g_free (req_buf);
  }

  return result;
}

static gint
provide_key_response (DrmPlayer * player)
{
  android::Vector<uint8_t> req_id;
  android::Vector<uint8_t> session_id;
  android::Vector<uint8_t> response;
  gint result = PRDRM_FAILED;

  str_to_vec (player->drm_session_id, session_id);
  str_to_vec (player->la_response, response);

  if ((result = player->drm_plugin->provideKeyResponse (session_id,
      response, req_id)) == PRDRM_SUCCESS)
    g_print ("Provided license response to DRMPlugin successfully.\n");

  return result;
}

static gint
playready_usecase (DrmPlayer * player)
{
  gint result = PRDRM_FAILED;

  if ((result = init_playready (player)) != PRDRM_SUCCESS) {
    g_print ("PlayReady session init failed.\n");
    return result;
  }

  if ((result = create_license_request (player)) != PRDRM_SUCCESS) {
    g_print ("Creation of license request failed.\n");
    return result;
  }

  if ((result = create_soap_request (player)) != PRDRM_SUCCESS) {
    g_print ("Creation of soap request failed.\n");
    return result;
  }

  if ((result = provide_key_response (player)) != PRDRM_SUCCESS) {
    g_print ("Provide key response failed.\n");
    return result;
  }

  return result;
}

static xmlNodePtr
find_xml_sibling_with_name (xmlNodePtr node, gchar * child_name)
{
  xmlNodePtr cur = node->next;

  while (cur != NULL) {
    if ((!xmlStrcmp(cur->name, (const xmlChar *)child_name)))
      return cur;
    cur = cur->next;
  }

  return NULL;
}

static xmlNodePtr
find_xml_child_with_name (xmlNodePtr root, gchar * child_name)
{
  xmlNodePtr cur = root->xmlChildrenNode;

  while (cur != NULL) {
    if ((!xmlStrcmp(cur->name, (const xmlChar *)child_name)))
      return cur;
    cur = cur->next;
  }

  return NULL;
}

static DrmLicense
parse_dash_key_tag (xmlNodePtr node, gchar ** pro_header)
{
  xmlChar *scheme_id_uri = NULL;
  DrmLicense license = LICENSE_NONE;
  xmlNodePtr child_node = node->xmlChildrenNode, cur = NULL;

  // Parse AdaptationSet's children to find all ContentProtection tags.
  while (child_node != NULL) {
    if ((xmlStrcasecmp (child_node->name, (const xmlChar *)"ContentProtection"))) {
      child_node = child_node->next;
      continue;
    }

    // Found a ContentProtection tag, content is encrypted.
    license = (license == LICENSE_NONE ? LICENSE_INVALID : license);
    g_print ("Found ContentProtection tag, it's encrypted content..\n");

    xmlFree (scheme_id_uri);

    // ContentProtection tag has property schemeIdUri with uuid.
    scheme_id_uri = xmlGetProp (child_node, (const xmlChar *)"schemeIdUri");

    if (xmlStrstr (scheme_id_uri, (const xmlChar *)"uuid") == NULL) {
      child_node = child_node->next;
      continue;
    }

    // Found the ContentProtection tag with uuid.
    if (!xmlStrcasecmp (scheme_id_uri, (const xmlChar *) PLAYREADY_UUID)) {
      g_print ("Found PlayReady UUID\n");

      // Parse PlayReady header.
      if ((cur = find_xml_child_with_name (child_node, (gchar *)"pro")) == NULL) {
        g_printerr ("ERROR: Didn't find PlayReady header!\n");
        child_node = child_node->next;
        continue;
      }

      license = (license == LICENSE_WIDEVINE ? LICENSE_BOTH : LICENSE_PLAYREADY);
      *pro_header = (gchar *) xmlNodeGetContent (cur);
    } else if (!xmlStrcasecmp (scheme_id_uri, (const xmlChar *) WIDEVINE_UUID)) {
      license = (license == LICENSE_PLAYREADY ? LICENSE_BOTH : LICENSE_WIDEVINE);
      g_print ("Found Widevine UUID\n");
    }

    child_node = child_node->next;
  }

  xmlFree (scheme_id_uri);
  return license;
}

static DrmLicense
parse_dash_manifest (gchar ** pro_header)
{
  DrmLicense license = LICENSE_INVALID;
  xmlNodePtr root, period, adapset;
  xmlDocPtr doc = xmlParseFile (MANIFEST_DOWNLOAD_PATH);

  g_print ("Parsing XML document...\n");

  if (doc == NULL) {
    g_printerr ("ERROR: Document not parsed successfully. \n");
    return license;
  }

  root = xmlDocGetRootElement (doc);
  if (root == NULL) {
    g_print ("Empty document.\n");
    goto exit;
  }

  if (xmlStrcmp (root->name, (const xmlChar *) "MPD")) {
    g_print ("Document of the wrong type, root node != MPD\n");
    goto exit;
  }

  // Manifest is supposed to have Period tag with one/multiple AdaptationSets as children.
  if ((period = find_xml_child_with_name (root, (gchar *)"Period")) == NULL) {
    g_print ("Couldn't find Period tag\n");
    goto exit;
  }

  if ((adapset = find_xml_child_with_name (period, (gchar *)"AdaptationSet")) == NULL) {
    g_print ("Couldn't find AdaptationSet tag\n");
    goto exit;
  }

  license = parse_dash_key_tag (adapset, pro_header);

  // TODO: If manifest has multiple Period tags, parse all of them.

  g_print ("Document parsed successfully.\n");

exit:
  // Freeing doc will free all descendent nodes recursively.
  xmlFreeDoc (doc);
  return license;
}

static gboolean
split_string (gchar ** input_str, const gchar * delim, gint num_of_splits, gint output_index)
{
  gchar **split_str = g_strsplit (*input_str, delim, num_of_splits);
  g_free (*input_str);

  if (g_strv_length (split_str) != num_of_splits) {
    g_strfreev (split_str);
    return FALSE;
  }

  *input_str = g_strdup (split_str[output_index]);
  g_strfreev (split_str);

  g_strstrip (*input_str);
  return TRUE;
}

// Parse the manifest to find key tag for media segment found at line number 'index'
static DrmLicense
parse_hls_key_tag (gchar ** split_content, gint index, gchar ** pro_header)
{
  gchar *method = NULL, *keyformat = NULL, *uri = NULL;
  DrmLicense license = LICENSE_NONE;

  // EXT-X-KEY or EXT-X-PLAYREADYHEADER tag contains the decryption info for
  // all the media segments that follow it.
  for (int i = index; i >= 0; i--) {
    if (!g_str_has_prefix (split_content[i], "#EXT-X-KEY") &&
        !g_str_has_prefix (split_content[i], "#EXT-X-SESSION-KEY") &&
        !g_str_has_prefix (split_content[i], "#EXT-X-PLAYREADYHEADER"))
      continue;

    if (g_str_has_prefix (split_content[i], "#EXT-X-PLAYREADYHEADER")) {
      // Only the first preceding license (of one type) can be used for
      // decrypting a media segment. Hence, if found same type again, ignore.
      if (license == LICENSE_PLAYREADY)
        continue;

      license = (license == LICENSE_NONE ? LICENSE_INVALID : license);
      g_print ("Found key tag, it's encrypted content..\n");

      g_print ("Found PlayReady UUID\n");

      // Parse PlayReady header.
      uri = split_content[i];
      if (!split_string (&uri, ":", 2, 1)) {
        g_printerr ("ERROR: Didn't find PlayReady header!\n");
        continue;
      }

      *pro_header = g_strdup (uri);
      license = (license == LICENSE_WIDEVINE ? LICENSE_BOTH : LICENSE_PLAYREADY);
      g_free (uri);

      // If both licenses found already, any repeating license of same type
      // will be invalid.
      if (license == LICENSE_BOTH)
        break;

      continue;
    }

    // It's an EXT-X-KEY or EXT-X-SESSION-KEY.
    if ((method = g_strrstr (split_content[i], "METHOD=")) == NULL)
      continue;

    if (!split_string (&method, "=", 2, 1))
      continue;

    if (g_str_has_prefix (method, "NONE")) {
      g_free (method);
      continue;
    }
    g_free (method);

    // If method is not NONE, it's encrypted.
    license = (license == LICENSE_NONE ? LICENSE_INVALID : license);
    g_print ("Found key tag, it's encrypted content..\n");

    if ((keyformat = g_strrstr (split_content[i], "KEYFORMAT=")) == NULL)
      continue;

    if (!split_string (&keyformat, "=", 2, 1))
      continue;

    if (!split_string (&keyformat, "\"", 3, 1))
      continue;

    if (g_str_equal (keyformat, "com.microsoft.playready") ||
        g_str_equal (keyformat, PLAYREADY_UUID)) {
      g_free (keyformat);

      if (license == LICENSE_PLAYREADY)
        continue;

      g_print ("Found PlayReady UUID\n");

      // Parse PlayReady header.
      if ((uri = g_strrstr (split_content[i], "URI=")) == NULL)
        continue;

      if (!split_string (&uri, "=", 2, 1))
        continue;

      if (!split_string (&uri, "\"", 3, 1))
        continue;

      if (!split_string (&uri, ",", 2, 1))
        continue;

      *pro_header = g_strdup (uri);
      license = (license == LICENSE_WIDEVINE ? LICENSE_BOTH : LICENSE_PLAYREADY);
      g_free (uri);

      // If both licenses found already, any repeating license of same type
      // will be invalid.
      if (license == LICENSE_BOTH)
        break;
    }

    if (g_str_equal (keyformat, "com.widevine") ||
        g_str_equal (keyformat, WIDEVINE_UUID)) {
      g_free (keyformat);

      if (license == LICENSE_WIDEVINE)
        continue;

      license = (license == LICENSE_PLAYREADY ? LICENSE_BOTH : LICENSE_WIDEVINE);
      g_print ("Found Widevine UUID\n");

      if (license == LICENSE_BOTH)
        break;
    }
  }

  return license;
}

static DrmLicense
parse_hls_manifest (gchar ** pro_header, gchar * manifest_content)
{
  gchar **split_content = NULL;
  gchar *codec = NULL;
  DrmLicense license = LICENSE_INVALID;
  gboolean found_uuid = FALSE;
  gint i;

  g_return_val_if_fail (manifest_content != NULL, license);

  // 'split_content' array stores each line of the manifest as its elements.
  split_content = g_strsplit (manifest_content, "\n", -1);

  for (i = 0; i < g_strv_length (split_content); i++) {
    // EXT-X-STREAM-INF tag specifies a stream, which is a set
    // of renditions that can be combined to play.
    if (!g_str_has_prefix (split_content[i], "#EXT-X-STREAM-INF"))
      continue;

    if ((codec = g_strrstr (split_content[i], "CODECS")) == NULL)
      continue;

    if (!split_string (&codec, "=", 2, 1))
      continue;

    if (!split_string (&codec, "\"", 3, 1))
      continue;

    // Select the first stream which has codec avc or hevc.
    if (g_str_has_prefix (codec, "avc") || g_str_has_prefix (codec, "hevc")) {
      g_print ("Selecting codec %s stream to play\n", codec);
      g_free (codec);
      break;
    }

    g_free (codec);
  }

  if (i >= g_strv_length (split_content)) {
    g_printerr ("ERROR: Didn't find any playable stream in the content\n");
    g_strfreev (split_content);
    return license;
  }

  license = parse_hls_key_tag (split_content, i, pro_header);

  g_print ("Document parsed successfully.\n");

  g_strfreev (split_content);
  return license;
}

static gboolean
parse_license (DrmLicense license, DrmPlayer * player)
{
  gchar *endptr = NULL;
  gchar input_str;
  gint input;

  switch (license) {
    case LICENSE_NONE:
      return TRUE;

    case LICENSE_PLAYREADY:
      if (playready_usecase (player) != PRDRM_SUCCESS)
        return FALSE;
      return TRUE;

    case LICENSE_WIDEVINE:
      g_print ("Not doing anything for Widevine yet!\n");
      return TRUE;

    case LICENSE_BOTH:
      break;

    default:
      return FALSE;
  }

  g_print ("Content can be played with PlayReady as well as Widevine.\n"
      "Please enter '1' for PlayReady or '2' for Widevine: " );

  input_str = fgetc (stdin);
  input = g_ascii_strtoll ((const gchar *) input_str,
      &endptr, 0);

  switch (input) {
    case LICENSE_PLAYREADY:
      if (playready_usecase (player) != PRDRM_SUCCESS)
        return FALSE;
    case LICENSE_WIDEVINE:
        g_print ("Not doing anything for Widevine yet!\n");
        break;
    default:
      g_print ("Invalid choice!");
      return FALSE;
  }

  return TRUE;
}

static gboolean
decide_dash_or_hls (gchar ** content)
{
  FILE *f;
  glong fsize;

  if ((f = fopen (MANIFEST_DOWNLOAD_PATH, "r")) == NULL) {
    g_printerr ("ERROR: Couldn't open manifest file!\n");
    return TRUE;
  }

  fseek (f, 0, SEEK_END);
  fsize = ftell(f);
  fseek (f, 0, SEEK_SET);

  *content = g_new0 (gchar, fsize + 1);
  fread (*content, fsize, 1, f);
  fclose (f);

  (*content)[fsize] = 0;
  *content = g_strstrip (*content);

  // If <?xml then DASH, if m3u8 then HLS
  if (g_str_has_prefix (*content, OPENING_TAG_HLS)) {
    g_print ("Parsing manifest..... it's HLS\n");
    return FALSE;
  } else if (g_str_has_prefix (*content, OPENING_TAG_DASH)) {
    g_print ("Parsing manifest..... it's DASH\n");
  }

  return TRUE;
}

static DrmLicense
parse_manifest (gchar ** pro_header)
{
  gchar *manifest_content = NULL;
  DrmLicense license = LICENSE_INVALID;

  if (decide_dash_or_hls (&manifest_content))
    license = parse_dash_manifest (pro_header);
  else
    license = parse_hls_manifest (pro_header, manifest_content);

  g_free (manifest_content);
  return license;
}

static CURLcode
fetch_manifest (gchar * manifest_url)
{
  CURL *curl = NULL;
  FILE *fp;
  gchar outfilename[FILENAME_MAX] = MANIFEST_DOWNLOAD_PATH;
  CURLcode res = CURLE_FAILED_INIT;

  g_print ("Trying to fetch manifest from the url %s...\n", manifest_url);

  if (curl_global_init (CURL_GLOBAL_ALL) != CURLE_OK) {
    g_printerr ("ERROR: Curl global init failed.\n");
    return res;
  }

  if ((curl = curl_easy_init ()) == NULL) {
    g_printerr ("ERROR: Curl easy init failed\n");
    curl_global_cleanup ();
    return res;
  }

  if ((fp = fopen (outfilename, "wb")) == NULL) {
    g_printerr ("ERROR: Couldn't open file for output\n");
    goto io_error;
  }

  // Uncomment this line to print curl outputs
  // curl_easy_setopt (curl, CURLOPT_VERBOSE, 1L);

  curl_easy_setopt (curl, CURLOPT_URL, manifest_url);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, fp);
  res = curl_easy_perform (curl);
  fclose (fp);

  if (res != CURLE_OK)
    g_print ("Curl error %d\n", res);
  else
    g_print ("Manifest downloaded and saved to %s\n", MANIFEST_DOWNLOAD_PATH);

io_error:
  curl_easy_cleanup (curl);
  curl_global_cleanup ();
  return res;
}

static void
toggle_play (GstAppContext * appctx)
{
  appctx->desired_state = (appctx->current_state == GST_STATE_PLAYING) ?
      GST_STATE_PAUSED : GST_STATE_PLAYING;

  // If buffering, state change will happen after buffering has finished.
  if (appctx->buffering) {
    g_print ("Pipeline is buffering, will toggle state when done\n");
    return;
  }

  if (update_pipeline_state (appctx, appctx->desired_state))
    (appctx->desired_state == GST_STATE_PLAYING) ?
        g_print ("Playing... %.30s\n", SPACE) :
        g_print ("Paused %.30s\n", SPACE);

  appctx->desired_state = appctx->current_state;
}

static gboolean
decide_mp4 (gchar * pipeline, gchar ** manifest_url, gboolean * mp4_content)
{
  gchar *str = g_strdup (pipeline);

  if (!split_string (&str, "!", 2, 0))
    return FALSE;

  if (g_str_has_suffix (str, "mp4")) {
    *mp4_content = TRUE;
    g_free (str);
    return TRUE;
  }

  // Parse the string to get manifest url.
  if (!split_string (&str, "=", 2, 1))
    return FALSE;

  *manifest_url = g_strdup (str);

  g_free (str);
  return TRUE;
}

static GstElement *
create_pipeline (gchar * pipeline_des)
{
  GstElement *pipeline;
  GError *error = NULL;

  g_print ("\nCreating pipeline %s %.30s\n", pipeline_des, SPACE);
  pipeline = gst_parse_launch ((const gchar *) pipeline_des, &error);

  if (error != NULL) {
    g_printerr ("ERROR: %s\n", GST_STR_NULL (error->message));
    g_clear_error (&error);
    return NULL;
  }

  return pipeline;
}

static void
print_menu ()
{
  g_print ("\n%.15s MENU %.15s\n", DASH_LINE, DASH_LINE);

  g_print ("%.2s %s %.2s : %.2s %s\n", SPACE, PLAY, SPACE, SPACE, "Play/Pause");
  g_print ("%.2s %s %.2s : %.2s %s\n", SPACE, STOP, SPACE, SPACE, "Stop");
  g_print ("%.2s %s %.2s : %.2s %s\n", SPACE, QUIT, SPACE, SPACE, "Quit");

  g_print ("\nChoose an option: ");
}

static gpointer
main_menu (gpointer data)
{
  GstAppContext *appctx = (GstAppContext *) data;
  gchar *str = NULL;
  gboolean active = TRUE;

  if (!update_pipeline_state (appctx, GST_STATE_PAUSED)) {
    g_main_loop_quit (appctx->mloop);
    return NULL;
  }

  while (active) {
    print_menu ();

    if (!wait_stdin_message (appctx->messages, &str) || g_str_equal (str, QUIT))
      active = FALSE;
    else if (g_str_equal (str, PLAY))
      toggle_play (appctx);
    else if (g_str_equal (str, STOP))
      update_pipeline_state (appctx, GST_STATE_NULL);
  }
  g_free (str);

  update_pipeline_state (appctx, GST_STATE_NULL);

  g_main_loop_quit (appctx->mloop);

  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *optctx = NULL;
  GstAppContext *appctx = NULL;
  DrmPlayer *player = NULL;
  GIOChannel *gio = NULL;
  GThread *mthread = NULL;
  GError *err = NULL;
  gchar **args = NULL;
  gchar *mp4_pro_header = NULL, *pro_header = NULL, *manifest_url = NULL;
  DrmLicense license = LICENSE_INVALID;
  guint bus_watch_id = 0, intrpt_watch_id = 0, stdin_watch_id = 0;
  gint status = -1;
  gboolean mp4_content = FALSE;

  gst_init (&argc, &argv);

  GOptionEntry options[] = {
      {"pro-header", 'p', 0, G_OPTION_ARG_STRING, &mp4_pro_header,
          "MP4 content PlayReady header", NULL},
      {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args, NULL},
      {NULL}
  };

  g_set_prgname ("gst-drm-player-example");

  optctx = g_option_context_new ("<pipeline>");
  g_option_context_set_summary (optctx,
      "You must provide a valid pipeline (enclosed within quotes) to play.\n");

  g_option_context_add_main_entries (optctx, options, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());

  if (!g_option_context_parse (optctx, &argc, &argv, &err)) {
    g_printerr ("ERROR: Couldn't initialize: %s\n",
        GST_STR_NULL (err->message));

    g_option_context_free (optctx);
    g_clear_error (&err);

    return -1;
  }
  g_option_context_free (optctx);

  if (args == NULL) {
    g_print ("Usage: gst-drm-player-example <pipeline> [OPTION]\n");
    g_print ("\nFor help: gst-drm-player-example [-h | --help]\n\n");

    goto exit;
  }

  // Parse args to decide whether it's an MP4 content.
  if (!decide_mp4 (*args, &manifest_url, &mp4_content)) {
    g_print ("Erroneous pipeline!\n");
    goto exit;
  }

  // If MP4 content is provided, PRO header is mandatory.
  if (mp4_content && mp4_pro_header == NULL) {
    g_print ("You must give PlayReady header with MP4 content.\n");
    g_print ("\nFor help: gst-drm-player-example [-h | --help]\n\n");

    goto exit;
  } else if (mp4_content) {
    license = LICENSE_PLAYREADY;
    pro_header = g_strdup (mp4_pro_header);
  }

  // Download manifest from the given url using libcurl.
  if (!mp4_content && fetch_manifest (manifest_url) != CURLE_OK)
    goto exit;

  // Parse manifest to detect license type and get license header.
  if (!mp4_content &&
      ((license = parse_manifest (&pro_header)) == LICENSE_INVALID)) {
    g_printerr ("ERROR: Invalid license! Can't proceed...\n");
    goto exit;
  }

  // If content is encrypted, create DrmPlayer context.
  if (license != LICENSE_NONE && ((player = drm_player_new (pro_header)) == NULL)) {
    g_printerr ("ERROR: Couldn't create drm player context!\n");
    g_free (pro_header);
    goto exit;
  }

  // Execute APIs according to license type found.
  if (!parse_license (license, player))
    goto exit;

  // Create app context.
  if ((appctx = gst_app_context_new ()) == NULL) {
    g_printerr ("ERROR: Couldn't create app context!\n");
    goto exit;
  }

  // Create the pipeline.
  if ((appctx->pipeline = create_pipeline (*args)) == NULL)
    goto exit;

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    goto exit;
  }

  // Initiate the menu thread.
  if ((mthread = g_thread_new ("MainMenu", main_menu, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create menu thread!\n");
    goto exit;
  }

  // Create a GIOChannel to listen to the standard input stream.
  if ((gio = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize I/O support! %.30s\n", SPACE);
    goto exit;
  }

  // Watch for user's input on stdin.
  stdin_watch_id = g_io_add_watch (gio,
      GIOCondition (G_IO_PRI | G_IO_IN), handle_stdin_source, appctx);
  g_io_channel_unref (gio);

  // Watch for messages on the pipeline's bus.
  bus_watch_id = gst_bus_add_watch (GST_ELEMENT_BUS (appctx->pipeline),
      handle_bus_message, appctx);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  // Wait until main menu thread finishes.
  g_thread_join (mthread);

  g_source_remove (bus_watch_id);
  g_source_remove (intrpt_watch_id);
  g_source_remove (stdin_watch_id);

  status = 0;

exit:
  gst_app_context_free (appctx);
  drm_player_free (player);
  g_free (mp4_pro_header);
  g_free (manifest_url);

  gst_deinit ();
  return status;
}
