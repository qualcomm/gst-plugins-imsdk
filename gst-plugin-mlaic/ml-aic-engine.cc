/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ml-aic-engine.h"

#include <memory>
#include <vector>
#include <algorithm>

#include <QAicApi.hpp>
#include <QAicApi.pb.h>

#define GST_ML_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

#define GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN(expression, value, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

#define GST_ML_RETURN_IF_FAIL(expression, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return; \
  } \
}

#define GST_ML_RETURN_IF_FAIL_WITH_CLEAN(expression, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return; \
  } \
}

#define GET_OPT_MODEL(s) get_opt_string (s, \
    GST_ML_AIC_ENGINE_OPT_MODEL)
#define GET_OPT_NUM_ACTIVATIONS(s) get_opt_uint (s, \
    GST_ML_AIC_ENGINE_OPT_NUM_ACTIVATIONS, 1)
#define GET_OPT_DEVICES(s) get_opt_uint_list (s, \
    GST_ML_AIC_ENGINE_OPT_DEVICES)

#define GST_AIC_GET_LOCK(obj) (&((GstMLAicEngine *)obj)->lock)
#define GST_AIC_LOCK(obj)     g_mutex_lock (GST_AIC_GET_LOCK(obj))
#define GST_AIC_UNLOCK(obj)   g_mutex_unlock (GST_AIC_GET_LOCK(obj))

#define GST_CAT_DEFAULT gst_ml_aic_engine_debug_category()

struct _GstMLAicEngine
{
  // Mutex lock synchronizing between threads
  GMutex       lock;

  GstMLInfo    *ininfo;
  GstMLInfo    *outinfo;

  GstStructure *settings;

  // List containing the order in which in/out buffers need to be filled.
  std::vector<::aicapi::bufferIoDirectionEnum> buforder;

  // AIC100 Primary object that links all other core objects.
  ::qaic::rt::shContext context;
  // AIC100 Program Container object containing the loaded model.
  ::qaic::rt::shQpc qpc;

  // List of programs on a AIC100 device.
  std::map<::qaic::rt::shProgram, uint32_t> programs;
  // List of user-level queue for enqueuing ExecObj for execution.
  std::map<::qaic::rt::shQueue, uint32_t> queues;
  // Map of ExecObj and the activation queue used to enqueue that object.
  std::map<::qaic::rt::shExecObj, ::qaic::rt::shQueue> objects;
  // Map of ExecObj and the buffers associated with it.
  std::map<::qaic::rt::shExecObj, std::vector<QBuffer>> buffers;
};

static GstDebugCategory *
gst_ml_aic_engine_debug_category (void)
{
  static GstDebugCategory *category = NULL;

  if (g_once_init_enter (&category)) {
    GstDebugCategory *cat = NULL;

    GST_DEBUG_CATEGORY_INIT (cat, "ml-aic-engine", 0,
        "Machine Learning AIC100 Engine");
    g_once_init_leave (&category, cat);
  }
  return category;
}

static const gchar *
get_opt_string (GstStructure * settings, const gchar * opt)
{
  return gst_structure_get_string (settings, opt);
}

static guint
get_opt_uint (GstStructure * settings, const gchar * opt, guint dval)
{
  guint result;
  return gst_structure_get_uint (settings, opt, &result) ?
    result : dval;
}

static GArray *
get_opt_uint_list (GstStructure * settings, const gchar * opt)
{
  GArray *list = NULL;
  const GValue *value = NULL;
  guint idx = 0;

  if ((value = gst_structure_get_value (settings, opt)) == NULL)
    return NULL;

  list = g_array_new (FALSE, FALSE, sizeof (guint));

  for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
    guint entry = g_value_get_uint (gst_value_array_get_value (value, idx));
    list = g_array_append_val (list, entry);
  }

  return list;
}

static GstMLType
gst_ml_aic_to_ml_type (::aicapi::bufferIoDataTypeEnum type)
{
  switch (type) {
    case ::aicapi::FLOAT_TYPE:
      return GST_ML_TYPE_FLOAT32;
    case ::aicapi::INT8_Q_TYPE:
      return GST_ML_TYPE_INT8;
    case ::aicapi::UINT8_Q_TYPE:
      return GST_ML_TYPE_UINT8;
    default:
      GST_ERROR ("Unsupported format %d!", static_cast<int32_t>(type));
      break;
  }

  return GST_ML_TYPE_UNKNOWN;
}

static void
gst_ml_aic_populate_info (GstMLAicEngine * engine, gint idx,
    const ::aicapi::IoSet &ioset)
{
  GstMLInfo *mlinfo = NULL;
  const ::aicapi::IoBinding *binding = NULL;
  const ::aicapi::DmaBufInfo *info = NULL, *previnfo = NULL;
  gboolean input = FALSE;
  gint num = 0, dim = 0;

  binding = &ioset.bindings(idx);

  if (::aicapi::BUFFER_IO_TYPE_INPUT == binding->dir()) {
    mlinfo = engine->ininfo;
    input = TRUE;
  } else if (::aicapi::BUFFER_IO_TYPE_OUTPUT == binding->dir()) {
    mlinfo = engine->outinfo;
    input = FALSE;
  }

  info = &binding->dma_buf_info(0);

  GST_INFO ("%s tensor at index %u, name: %s, size %u, DMA index: %u, DMA "
      "offset: %u and DMA size: %u", input ? "Input" : "Output",
      binding->index(), binding->name().c_str(), binding->size(),
      info->dma_buf_index(), info->dma_offset(), info->dma_size());

  // Fill information about current tensor.
  num = mlinfo->n_tensors;
  mlinfo->n_dimensions[num] = binding->dims_size();

  for (dim = 0; dim < binding->dims_size(); ++dim)
    mlinfo->tensors[num][dim] = binding->dims(dim);

  mlinfo->type = gst_ml_aic_to_ml_type (binding->type());
  mlinfo->n_tensors++;

  // Reorder tensors in case they are not sorted by DMA offset values.
  while (--idx >= 0) {
    binding = &ioset.bindings(idx);
    previnfo = &binding->dma_buf_info(0);

    if (previnfo->dma_buf_index() == info->dma_buf_index() &&
        previnfo->dma_offset() > info->dma_offset()) {
      gint n_dimensions, dimensions[GST_ML_TENSOR_MAX_DIMS];

      n_dimensions = mlinfo->n_dimensions[num];
      mlinfo->n_dimensions[num] = mlinfo->n_dimensions[num - 1];
      mlinfo->n_dimensions[num - 1] = n_dimensions;

      for (dim = 0; dim < n_dimensions; ++dim)
        dimensions[dim] = mlinfo->tensors[num][dim];

      for (dim = 0; dim < (gint) mlinfo->n_dimensions[num]; ++dim)
        mlinfo->tensors[num][dim] = mlinfo->tensors[num - 1][dim];

      for (dim = 0; dim < (gint) mlinfo->n_dimensions[num - 1]; ++dim)
        mlinfo->tensors[num - 1][dim] = dimensions[dim];
    }

    if ((input && (::aicapi::BUFFER_IO_TYPE_INPUT == binding->dir())) ||
        (!input && (::aicapi::BUFFER_IO_TYPE_OUTPUT == binding->dir())))
      num--;

    info = previnfo;
  }
}

static gboolean
gst_ml_aic_set_qbuffer (const GstMLFrame * frame, guint idx, QBuffer& qbuffer)
{
  GstMemory *memory = gst_buffer_peek_memory (frame->buffer, idx);

  g_return_val_if_fail (gst_is_fd_memory (memory), FALSE);

  qbuffer.type = QBUFFER_TYPE_DMABUF;
  qbuffer.buf = GST_ML_FRAME_BLOCK_DATA (frame, idx);
  qbuffer.size = GST_ML_FRAME_BLOCK_SIZE (frame, idx);

  qbuffer.handle = gst_fd_memory_get_fd (memory);
  qbuffer.offset = memory->offset;

  return TRUE;
}

static ::qaic::rt::shExecObj
gst_ml_aic_retrieve_execution_object (GstMLAicEngine * engine,
    std::vector<QBuffer> &qbuffers)
{
  ::qaic::rt::shExecObj object;

  GST_AIC_LOCK (engine);

  // Try to find and reuse an existing ExecObj.
  for (auto const& pair : engine->buffers) {
    std::vector<QBuffer> buffers = pair.second;
    bool match = true;

    if (buffers.size() != qbuffers.size())
      continue;

    for (uint32_t idx = 0; idx < buffers.size(); idx++)
      match &= (buffers[idx].handle == qbuffers[idx].handle) ? true : false;

    if (!match) continue;

    object = pair.first;
    break;
  }

  GST_AIC_UNLOCK (engine);

  if (object)
    return object;

  // If we reach this point then there is no ExecObj that can be reused.
  GST_AIC_LOCK (engine);

  // Initialize the usage and program with the 1st activation before search.
  ::qaic::rt::shProgram program = engine->programs.begin()->first;
  uint32_t n_usage = engine->programs.begin()->second;

  // Find the least loaded program.
  for (auto& pair : engine->programs) {
    if (pair.second >= n_usage)
      continue;

    program = pair.first;
    n_usage = pair.second;
  }

  GST_LOG ("Using program with ID %u and usage %u", program->getId(), n_usage);

  // Increase the usage count for this program.
  engine->programs[program] += 1;

  GST_AIC_UNLOCK (engine);

  try {
    object = ::qaic::rt::ExecObj::Factory(engine->context, program, qbuffers);

    GST_AIC_LOCK (engine);
    engine->objects.emplace(object, nullptr);
    engine->buffers.emplace(object, qbuffers);
    GST_AIC_UNLOCK (engine);

    GST_LOG ("Created execution object for program ID %u", program->getId());
  } catch (const ::qaic::rt::CoreExceptionInit &e) {
    GST_ERROR ("Caught exception during object creation: %s", e.what());

    GST_AIC_LOCK (engine);
    engine->programs[program] -= 1;
    GST_AIC_UNLOCK (engine);

    return nullptr;
  }

  return object;
}

static void
gst_ml_aic_internal_maps_cleanup (GstMLAicEngine * engine,
    ::qaic::rt::shExecObj object)
{
  GST_AIC_LOCK (engine);

  // Decrease the usage count for the activation associated with the object.
  engine->queues[engine->objects[object]] -= 1;
  // Erase the ExecObj associated with the give index.
  engine->objects[object].reset();

  GST_AIC_UNLOCK (engine);
}

// Helper function for AIC context.
static void
gst_ml_aic_log_callback (QLogLevel level, const char * message)
{
  if (level == QL_DEBUG || level == QL_INFO)
    GST_TRACE ("AIC: %s", message);
  else if (level == QL_WARN)
    GST_WARNING ("AIC: %s", message);
  else if (level == QL_ERROR)
    GST_ERROR ("AIC: %s", message);
}

static void
gst_ml_aic_error_handler (QAicContextID ctxid, const char * message,
    QAicErrorType type, const void * data, size_t size, void * userdata)
{
  std::ignore = data;
  std::ignore = size;
  std::ignore = userdata;

  GST_ERROR ("Received Error for context ID: %d, type: %d, message: '%s'",
      ctxid, type, message);
}

GstMLAicEngine *
gst_ml_aic_engine_new (GstStructure * settings)
{
  GstMLAicEngine *engine = NULL;
  GArray *devices = NULL;
  guint idx = 0, num = 0, n_activations = 0;

  engine = new GstMLAicEngine;
  g_return_val_if_fail (engine != NULL, NULL);

  g_mutex_init (&engine->lock);

  engine->ininfo = gst_ml_info_new ();
  engine->outinfo = gst_ml_info_new ();

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  try {
    ::qaic::rt::Util utils;

    QStatus status = utils.checkLibraryVersion();
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == QS_SUCCESS, NULL,
        gst_ml_aic_engine_free (engine), "Failed to verify AIC library "
        "version, status %d!", status);

    std::vector<QID> ids;

    // Get vector with supported AIC device IDs, used later for sanity checks.
    status = utils.getDeviceIds(ids);
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == QS_SUCCESS, NULL,
        gst_ml_aic_engine_free (engine), "Failed to retrieve AIC device IDs, "
        "status %d!", status);

    // Get temporary list with chosen AIC device IDs.
    devices = GET_OPT_DEVICES (engine->settings);
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (devices != NULL, NULL,
        gst_ml_aic_engine_free (engine), "AIC device(s) not specified!");

    // Vector which will contain the devices IDs from which we create a context.
    std::vector<QID> device_ids;

    for (idx = 0; idx < devices->len; idx++) {
      QID id = g_array_index (devices, guint, idx);

      // Verify that a device with the given ID exists.
      auto it = std::find(ids.begin(), ids.end(), id);
      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (it != ids.end(), NULL,
          g_array_free (devices, TRUE); gst_ml_aic_engine_free (engine),
          "Device with ID %u does not exists!", id);

      QDevInfo info;

      // Retieve details regarding the chosen device.
      status = utils.getDeviceInfo(id, info);
      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == QS_SUCCESS, NULL,
          g_array_free (devices, TRUE); gst_ml_aic_engine_free (engine),
          "Failed to retrieve AIC device info, status %d!", status);

      GST_INFO ("Using device with ID: %u, PCI Class: %s, PCI Vendor: %s, "
          "PCI Device: %s", info.qid, info.pciInfo.classname,
          info.pciInfo.vendorname, info.pciInfo.devicename);

      device_ids.push_back(id);
    }

    // Free the temporary  devices list, it is not needed anymore.
    g_array_free (devices, TRUE);

    // Create context from the chosen devices and set the log & error handlers.
    engine->context = ::qaic::rt::Context::Factory(nullptr, device_ids,
        gst_ml_aic_log_callback, gst_ml_aic_error_handler);

    // Set the highest debug level, it will be filtered by the GST log system.
    engine->context->setLogLevel(QL_DEBUG);

    GST_INFO ("Created AIC context %s with ID %u",
        engine->context->objNameCstr(), engine->context->getId());

    std::string dirname(g_path_get_dirname (GET_OPT_MODEL (engine->settings)));
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (!dirname.empty(), NULL,
        gst_ml_aic_engine_free (engine), "No model directory!");

    std::string filename(g_path_get_basename (GET_OPT_MODEL (engine->settings)));
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (!filename.empty(), NULL,
        gst_ml_aic_engine_free (engine), "No model file name!");

    engine->qpc = ::qaic::rt::Qpc::Factory(dirname, filename);
    GST_INFO ("Loaded model file '%s'", filename.c_str());

    // Get the chosen number of activations to utilize in this engine.
    n_activations = GET_OPT_NUM_ACTIVATIONS (engine->settings);
    GST_INFO ("Number of activations: %d", n_activations);

    QAicProgramProperties_t pprops;
    ::qaic::rt::Program::initProperties(pprops);

    QAicQueueProperties_t qprops;
    ::qaic::rt::Queue::initProperties(qprops);
    qprops.numThreadsPerQueue = 4;

    // Create program and queue for each activation.
    for (idx = 0; idx < n_activations; idx++) {
      std::string name(filename);
      QID device_id = device_ids[idx % device_ids.size()];

      // Extract only the model name and add the activation index.
      name.substr(name.find_last_of("/\\") + 1);
      name += "_" + std::to_string(idx);

      // Create a program from the QPC object.
      ::qaic::rt::shProgram program = ::qaic::rt::Program::Factory(
          engine->context, device_id, name.c_str(), engine->qpc, &pprops);

      GST_INFO ("Created AIC program %s with ID %u and assigned device ID %u",
          program->objNameCstr(), program->getId(), device_id);

      // Load and Activate program.
      status = program->load();
      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == QS_SUCCESS, NULL,
          gst_ml_aic_engine_free (engine), "Failed to load program %s, "
          "status %d!", program->objNameCstr(), status);

      GST_INFO ("Loaded AIC program %s with ID %u", program->objNameCstr(),
          program->getId());

      status = program->activate();
      GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == QS_SUCCESS, NULL,
          gst_ml_aic_engine_free (engine), "Failed to activate program %s, "
          "status %d!", program->objNameCstr(), status);

      GST_INFO ("Activated AIC program %s with ID %u",
          program->objNameCstr(), program->getId());

      ::qaic::rt::shQueue queue = ::qaic::rt::Queue::Factory(
          engine->context, device_id, &qprops);

      GST_INFO ("Created AIC queue %s and assigned device ID %u",
          queue->objNameCstr(), device_id);

      engine->programs.emplace(program, 0);
      engine->queues.emplace(queue, 0);
    }

    QData iodata = {0, nullptr};

    // Retrieve IO descriptor using 1st activation.
    status = engine->programs.begin()->first->getIoDescriptor(&iodata);
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == QS_SUCCESS, NULL,
        gst_ml_aic_engine_free (engine), "Failed to retrieve IO descriptor, "
        "status %d!", status);

    // Parse the descriptor data using ProtoBuf protocol.
    ::aicapi::IoDesc *iodesc = new (::std::nothrow) aicapi::IoDesc();
    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (iodesc != NULL, NULL,
        gst_ml_aic_engine_free (engine), "Failed to create IoDesc!");

    if (!iodesc->ParseFromArray(iodata.data, iodata.size)) {
      GST_ERROR ("Failed to parse data from IO descriptor!");
      gst_ml_aic_engine_free (engine);
      return NULL;
    }

    // Find the 'default' IO set which contains information about the tensors.
    auto it = std::find_if(iodesc->io_sets().begin(), iodesc->io_sets().end(),
        [] (const ::aicapi::IoSet& set) { return set.name() == "dma"; } );

    GST_ML_RETURN_VAL_IF_FAIL_WITH_CLEAN (it != iodesc->io_sets().end(), NULL,
        gst_ml_aic_engine_free (engine), "Failed to find 'default' IO set!");

    const ::aicapi::IoSet &ioset = *it;

    // Iterate through all bindings and populate input and output ML info.
    for (idx = 0; idx < (guint) ioset.bindings_size(); idx++)
      gst_ml_aic_populate_info (engine, idx, ioset);

    // Fill the order (in/out) in which the buffers are expected to be set.
    for (idx = 0; idx < (guint) iodesc->dma_buf_size(); idx++)
      engine->buforder.push_back(iodesc->dma_buf(idx).dir());

  } catch (const ::qaic::rt::CoreExceptionInit &e) {
    GST_ERROR ("Caught exception during initialization: %s", e.what());
    gst_ml_aic_engine_free (engine);
    return NULL;
  }

  GST_DEBUG ("Number of input tensors: %u", engine->ininfo->n_tensors);
  GST_DEBUG ("Input tensors type: %s",
      gst_ml_type_to_string (engine->ininfo->type));

  for (idx = 0; idx < GST_ML_INFO_N_TENSORS (engine->ininfo); idx++) {
    for (num = 0; num < GST_ML_INFO_N_DIMENSIONS (engine->ininfo, idx); ++num)
      GST_DEBUG ("Input tensor[%u] Dimension[%u]: %u", idx, num,
          engine->ininfo->tensors[idx][num]);
  }

  GST_DEBUG ("Number of output tensors: %u", engine->outinfo->n_tensors);
  GST_DEBUG ("Output tensors type: %s",
      gst_ml_type_to_string (engine->outinfo->type));

  for (idx = 0; idx < GST_ML_INFO_N_TENSORS (engine->outinfo); idx++) {
    for (num = 0; num < GST_ML_INFO_N_DIMENSIONS (engine->outinfo, idx); ++num)
      GST_DEBUG ("Output tensor[%u] Dimension[%u]: %u", idx, num,
          engine->outinfo->tensors[idx][num]);
  }

  GST_INFO ("Created ML AIC engine: %p", engine);
  return engine;
}

void
gst_ml_aic_engine_free (GstMLAicEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->outinfo != NULL) {
    gst_ml_info_free (engine->outinfo);
    engine->outinfo = NULL;
  }

  if (engine->ininfo != NULL) {
    gst_ml_info_free (engine->ininfo);
    engine->ininfo = NULL;
  }

  if (engine->settings != NULL) {
    gst_structure_free (engine->settings);
    engine->settings = NULL;
  }

  g_mutex_clear (&engine->lock);

  GST_INFO ("Destroyed ML AIC engine: %p", engine);
  delete engine;
}

const GstMLInfo *
gst_ml_aic_engine_get_input_info  (GstMLAicEngine * engine)
{
  return (engine == NULL) ? NULL : engine->ininfo;
}

const GstMLInfo *
gst_ml_aic_engine_get_output_info  (GstMLAicEngine * engine)
{
  return (engine == NULL) ? NULL : engine->outinfo;
}

gint
gst_ml_aic_engine_submit_request (GstMLAicEngine * engine,
    GstMLFrame * inframe, GstMLFrame * outframe)
{
  guint idx = 0, num = 0;

  g_return_val_if_fail (engine != NULL, GST_ML_AIC_INVALID_ID);
  g_return_val_if_fail (inframe != NULL, GST_ML_AIC_INVALID_ID);
  g_return_val_if_fail (outframe != NULL, GST_ML_AIC_INVALID_ID);

  // Set of both input and output buffers used in the AIC ExecObj.
  std::vector<QBuffer> qbuffers;

  for (auto const& direction : engine->buforder) {
    gboolean success = FALSE;

    qbuffers.emplace_back();

    if (::aicapi::BUFFER_IO_TYPE_INPUT == direction)
      success = gst_ml_aic_set_qbuffer (inframe, idx++, qbuffers.back());
    else // (::aicapi::BUFFER_IO_TYPE_OUTPUT == direction)
      success = gst_ml_aic_set_qbuffer (outframe, num++, qbuffers.back());

    GST_ML_RETURN_VAL_IF_FAIL (success, GST_ML_AIC_INVALID_ID,
        "Failed to fill QBuffer!");
  }

  ::qaic::rt::shExecObj object =
      gst_ml_aic_retrieve_execution_object (engine, qbuffers);

  GST_ML_RETURN_VAL_IF_FAIL (object, GST_ML_AIC_INVALID_ID,
      "Failed to retrieve ExecObj!");

  GST_AIC_LOCK (engine);

  // Initialize the usage and queus with the 1st activation before search.
  ::qaic::rt::shQueue queue = engine->queues.begin()->first;
  uint32_t n_usage = engine->queues.begin()->second;

  // Find the least loaded queue.
  for (auto const& pair : engine->queues) {
    queue = (pair.second < n_usage) ? pair.first : queue;
    n_usage = (pair.second < n_usage) ? pair.second : n_usage;
  }

  GST_LOG ("Using queue %s with usage %u", queue->objNameCstr(), n_usage);

  // Increase the usage count for this queue.
  engine->queues[queue] += 1;
  // Set the queue on which this object will be enqued.
  engine->objects[object] = queue;

  GST_AIC_UNLOCK (engine);

  try {
    QStatus status = queue->enqueue(object);

    if (status != QS_SUCCESS) {
      GST_ERROR ("Failed to enqueue AIC ExecObj with ID %u, status %d!",
          object->getId(), status);
      gst_ml_aic_internal_maps_cleanup (engine, object);
      return GST_ML_AIC_INVALID_ID;
    }

    GST_LOG ("Enqueued AIC ExecObj with ID: %u", object->getId());
  } catch (const ::qaic::rt::CoreExceptionRuntime &e) {
    GST_ERROR ("Caught exception during execution: %s", e.what());
    gst_ml_aic_internal_maps_cleanup (engine, object);
    return GST_ML_AIC_INVALID_ID;
  }

  return object->getId();
}

gboolean
gst_ml_aic_engine_wait_request (GstMLAicEngine * engine, guint request_id)
{
  gboolean success = TRUE;

  GST_AIC_LOCK (engine);

  auto it = std::find_if(engine->objects.begin(), engine->objects.end(),
      [&] (const std::pair<::qaic::rt::shExecObj, ::qaic::rt::shQueue> &pair)
      { return pair.first->getId() == request_id; } );

  if (it == engine->objects.end()) {
    GST_ERROR ("Unable to find object ID %x!", request_id);
    return FALSE;
  }

  // Get the reference to ExecObj and the activation index used to enqueue it.
  ::qaic::rt::shExecObj object = it->first;

  GST_AIC_UNLOCK (engine);

  GST_LOG ("Waiting ExecObj with ID %u", object->getId());

  try {
    QStatus status = object->waitForCompletion();

    if (status != QS_SUCCESS) {
      GST_ERROR ("Wait for ExecObj with ID %u failed, status: %d!",
          object->getId(), status);
      success = FALSE;
    }
  } catch (const ::qaic::rt::CoreExceptionRuntime &e) {
    GST_ERROR ("Caught exception during execution: %s", e.what());
    success = FALSE;
  }

  GST_LOG ("Finished waiting ExecObj with ID %u", object->getId());
  gst_ml_aic_internal_maps_cleanup (engine, object);

  return success;
}
