/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include <fstream>
#include "qtimuxserialize.h"

using namespace std;

//below are required functions during gst_meta_register()
static gboolean test_init_func (GstMeta * meta, gpointer params, GstBuffer * buffer);
static void test_free_func (GstMeta * meta, GstBuffer * buffer);
static gboolean test_transform_func (GstBuffer * transbuf, GstMeta * meta, GstBuffer \
                                     *buffer, GQuark type, gpointer data);
static GType gst_meta_test_api_get_type (void);

//dump binary of memory buffer for test verify
void dump_binary(void *buff, guint size, const gchar *name, guint no)
{
    gchar filename[256];

    snprintf(filename, sizeof(filename), "./ut/%s_%d.bin", name, no);
    fprintf(stdout, "dump binary %s\n", filename);

    std::ofstream qbuffile(filename);
    if(qbuffile.is_open())
        qbuffile.write((gchar*)buff, size);
    qbuffile.close();
}

//dump gst memory to file for test verify
void dump_gst_memory(GstBuffer *buf, gboolean first_mem_only, const gchar *name) {
    guint total_mem  = gst_buffer_n_memory(buf);
    for(guint i=0; i<total_mem; i++) {
        GstMemory * i_mem = gst_buffer_get_memory (buf, i);
        gsize memsize, offsize, maxsize;
        memsize = gst_memory_get_sizes(i_mem, &offsize, &maxsize);
        g_print("dump %d gstmemory: sizeof: %ld, memsize=%ld, offsize=%ld, \
            maxsize=%ld\n", i, sizeof(GstMemory), memsize, offsize, maxsize);

        dump_binary(i_mem, memsize, name, i);
        if(first_mem_only) break;
    }
}

//required func of gst_meta_register()
static gboolean test_init_func (GstMeta * meta, gpointer params, GstBuffer * buffer) {
    GST_DEBUG ("init called on buffer %p, meta %p", buffer, meta);
    // nothing to init really, the init function is mostly for allocating
    // additional memory or doing special setup as part of adding the metadata to
    // the buffer
    return TRUE;
}

//required func of gst_meta_register()
static void test_free_func (GstMeta * meta, GstBuffer * buffer) {
    GST_DEBUG ("free called on buffer %p, meta %p", buffer, meta);
    // nothing to free really
}

//required func of gst_meta_register()
static gboolean test_transform_func (GstBuffer * transbuf, GstMeta * meta,
      GstBuffer * buffer, GQuark type, gpointer data) {

    GST_DEBUG ("transform %s called from buffer %p to %p, meta %p",
        g_quark_to_string (type), buffer, transbuf, meta);

    if (GST_META_TRANSFORM_IS_COPY (type)) {
      g_print("GST_META_TRANSFORM_IS_COPY = TRUE\n");
    } else {
      // return FALSE, if transform type is not supported
      return FALSE;
    }
    return TRUE;
}

//return meta api type
static GType gst_meta_test_api_get_type (void) {
    static GType type;
    static const gchar *tags[] = { "timing", NULL };

    if (g_once_init_enter (&type)) {
        GType _type = gst_meta_api_type_register ("GstMetaTestAPI", tags);
        g_once_init_leave (&type, _type);
    }
    return type;
}

//append the input buffer from one of sink pads to the output buffer,
//and populate meta as well
guint append_to_outbuf (GstBuffer *inbuf, GstBuffer *outbuf) {
    guint ret = 0;
    guint cnt = 0;
    guint channel_id=0, frame_id=0, obj_id=0;
    GstVideoFrameMeta *roi_meta = NULL;
    static GType i_api_type, o_api_type;
    GstMeta *meta = NULL, *i_meta=NULL;
    gpointer state = NULL;

    //validate input, output buffers
    if(NULL == inbuf || NULL == outbuf) {
        g_print("NULL input or output buffers passed in\n");
        ret++;
    } else {
        GstMapInfo i_info;
    }

    // register meta API type

    o_api_type = gst_meta_test_api_get_type();
    static const GstMetaInfo *meta_test_info = NULL;
    if (g_once_init_enter ((GstMetaInfo **) & meta_test_info)) {
        const GstMetaInfo *mi = gst_meta_register (o_api_type,
                                               "GstVideoFrameMeta",
                                               sizeof (GstVideoFrameMeta),
                                               test_init_func, test_free_func,
                                               test_transform_func);
       g_once_init_leave ((GstMetaInfo **) & meta_test_info, (GstMetaInfo *) mi);
    }

    i_api_type = INPUT_META_API_TYPE;
    guint meta_num = gst_buffer_get_n_meta (inbuf, i_api_type);
    g_print("input buffer contains %d meta of api_type\n", meta_num);

    // if multiple meta attached by retriving input buffer
    while ((meta = gst_buffer_iterate_meta(inbuf, &state)) != NULL) {
         cnt++;
         g_print("input buffer meta #: %d\n", cnt);
         if (meta->info->api != INPUT_META_API_TYPE){
             continue;
         }

         //parse below info from input buffer meta assoicated, only accept 1 meta now
         i_meta = meta;
         channel_id = 1;   // hard code ???, should read from input buffer meta
         frame_id = 2;
         obj_id = cnt;
    }

    // hard code ???, should read from input buffer meta
    channel_id = 1;
    frame_id = 2;
    if(g_save) frame_id=8888;
    obj_id = g_total_memory;

    // append output meta to output buffer only once ???
    roi_meta = (GstVideoFrameMeta *)gst_buffer_add_meta (outbuf, meta_test_info, NULL);
    if (NULL != roi_meta) {
        roi_meta->channel_id = channel_id;
        roi_meta->frame_id = frame_id;
        roi_meta->obj_id = obj_id;
    } else{
        g_print("roi_meta is null, fail to add meta to outbuf !\n");
        ret++;
    }

    // append memory blocks to output buffer
    gsize total_size = gst_buffer_get_size(inbuf);
    guint total_mem  = gst_buffer_n_memory(inbuf);
    guint max_mem = gst_buffer_get_max_memory();
    g_print("total_mem =%d, max_mem =%d, total_size =%ld\n", total_mem, max_mem, total_size);
    if(g_total_memory >= max_mem) {  // can not exceed the MAX memory blocks in a single buffer
        g_print("reach MAX_MEMORY_BLOCK(%d) of outbuf ! ignored\n", max_mem);
    } else{
        for(guint i=0; i<total_mem; i++) {
            GstMemory * i_mem = gst_buffer_get_memory (inbuf, i);
            //copy to local
            GstMemory * o_mem = gst_memory_copy (i_mem, 0, -1);
            if(NULL != i_mem && NULL != o_mem && i < max_mem) {
                gst_buffer_insert_memory (outbuf, -1, o_mem);     //append to the end
                g_total_memory++;
            } else {
                g_print("alloc memory failed =NULL or exceed max_memory in buffer\n");
                ret++;
            }
        }
    }

    return ret;
}

//read and verify the buffer to be passed downstream
void verify_outbuf(GstBuffer * outbuf) {
    if(NULL == outbuf) {
        g_print("NULL output buffer to be push downstream\n");
    } else {
            {
                // meta API type
                static GType o_api_type;
                o_api_type = gst_meta_test_api_get_type();
                guint num = gst_buffer_get_n_meta(outbuf, o_api_type);
                g_print("outbuf contains %d meta of specified api type\n", num);

                // memory blocks
                gsize total_size = gst_buffer_get_size(outbuf);
                guint total_mem  = gst_buffer_n_memory(outbuf);
                guint max_mem = gst_buffer_get_max_memory();
                g_print("outbuf total_mem =%d, max_mem =%d, total_size =%ld\n", \
                        total_mem, max_mem, total_size);
            }
    }

    return;
}
