/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/* ------------------------------------------------------------------
* Copyright (C) 2020 ewan xu<ewan_xu@outlook.com>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied.
* See the License for the specific language governing permissions
* and limitations under the License.
* -------------------------------------------------------------------
*/

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "eigen3/Eigen/Core"
#include "eigen3/unsupported/Eigen/FFT"

#include <vector>
#include <complex>
#include <iostream>
#include <fstream>
#include <algorithm>

#include "audio-converter-engine.h"

typedef void (* ConvertFunc) (const void * s, void * d, size_t in, size_t out);
typedef Eigen::Matrix<float, 1, Eigen::Dynamic, Eigen::RowMajor> Vectorf;
typedef Eigen::Matrix<std::complex<float>, 1, Eigen::Dynamic, Eigen::RowMajor> Vectorcf;
typedef Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> Matrixf;
typedef Eigen::Matrix<std::complex<float>, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> Matrixcf;

#define DEFINE_CONVERTER(srctype, max_val, desttype)                        \
static void                                                                 \
do_convert_##srctype##_##desttype (const void * s, void * d,                \
    size_t in, size_t out)                                                  \
{                                                                           \
  size_t i = (out < in) ? out : in;                                         \
  const srctype *src = (srctype *)s;                                        \
  desttype *dest = (desttype *)d;                                           \
  while (i-- > 0) {                                                         \
    *dest++ = ((desttype)(*src++)) / (max_val);                             \
  }                                                                         \
  while (in < out) {                                                        \
    *dest++ = 0.0;                                                          \
    in++;                                                                   \
  }                                                                         \
} extern void glib_dummy_decl (void)

DEFINE_CONVERTER (gint8, G_MAXINT8, gfloat);
DEFINE_CONVERTER (guint8, G_MAXUINT8, gfloat);
DEFINE_CONVERTER (gint16, G_MAXINT16, gfloat);
DEFINE_CONVERTER (guint16, G_MAXUINT16, gfloat);
DEFINE_CONVERTER (gint32, G_MAXINT32, gfloat);
DEFINE_CONVERTER (guint32, G_MAXUINT32, gfloat);
DEFINE_CONVERTER (gfloat, 1.0, gfloat);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif // M_PI

#define DEFAULT_N_FFT                512
#define DEFAULT_N_MELS               64
#define DEFAULT_N_HOP                160
#define DEFAULT_MIN_HZ               125
#define DEFAULT_MAX_HZ               7500
#define DEFAULT_SAMPLE_NUMBER        15360

#define GST_CAT_DEFAULT mlac_engine_debug
GST_DEBUG_CATEGORY (mlac_engine_debug);

namespace AudioPreprocess {
static Vectorf
pad (Vectorf &x, int left, int right, const std::string &mode, float value)
{
  Vectorf x_paded = Vectorf::Constant (left + x.size () + right, value);
  x_paded.segment (left, x.size ()) = x;

  if (mode.compare ("reflect") == 0) {
    for (int i = 0; i < left; ++i)
      x_paded[i] = x[left-i];

    for (int i = left; i < left + right; ++i)
     x_paded[i + x.size()] = x[x.size () - 2 - i + left];

  }

  if (mode.compare ("symmetric") == 0) {
    for (int i = 0; i < left; ++i)
      x_paded[i] = x[left - i - 1];

    for (int i = left; i < left + right; ++i)
      x_paded[i + x.size ()] = x[x.size() - 1 - i + left];

  }

  if (mode.compare ("edge") == 0){
    for (int i = 0; i < left; ++i)
      x_paded[i] = x[0];

    for (int i = left; i < left + right; ++i)
    x_paded[i + x.size ()] = x[x.size () - 1];
  }
  return x_paded;
}

static Matrixcf
stft (Vectorf &x, int n_fft, int n_hop, bool center, const std::string &mode)
{
  // hanning
  Vectorf window = 0.5 * (1.f - (Vectorf::LinSpaced (n_fft, 0.f,
      static_cast<float>(n_fft - 1)) * 2.f * M_PI / n_fft).array ().cos ());

  int pad_len = center ? n_fft / 2 : 0;
  Vectorf x_paded = pad (x, pad_len, pad_len, mode, 0.f);

  int n_f = n_fft / 2 + 1;
  int n_frames = 1 + (x_paded.size () - n_fft) / n_hop;

  Matrixcf X(n_frames, n_fft);
  Eigen::FFT<float> fft;

  for (int i = 0; i < n_frames; ++i){
    Vectorf x_frame = window.array () * x_paded.segment (
        i * n_hop, n_fft).array ();
    X.row (i) = fft.fwd (x_frame);
  }

  return X.leftCols (n_f);
}

static Matrixf
spectrogram (Matrixcf &X, float power = 2.f)
{
  return X.cwiseAbs ().array ().pow (power);
}

static Matrixf
melfilter_from_file (const std::string &mel_file, int n_mels)
{
  Matrixf melfilter = Matrixf::Zero (n_mels, 500);

  std::ifstream melfile (mel_file, std::ios::binary);
  float * filtervalues = NULL;

  if (!melfile) {
    std::cerr << "Invalid / Corrupt MelBin file" << std::endl;
    return melfilter;
  }

  melfile.seekg (0, std::ios::end);
  std::streamsize melbin_size = melfile.tellg ();
  melfile.seekg (0, std::ios::beg);

  filtervalues = new float[melbin_size / sizeof (float)];

  melfilter.conservativeResize (n_mels, (melbin_size / sizeof (float)) / n_mels);

  //read melfilters in binary format
  if (melfile.read (reinterpret_cast<char *>(filtervalues), melbin_size)) {
    for(int i = 0; i < melfilter.size(); i++)
      *(melfilter.data() + i) = filtervalues[i];
  }

  delete[] filtervalues;
  return melfilter;
}

static Matrixf
melfilter (int sr, int n_fft, int n_mels, int fmin, int fmax)
{
  int n_f = n_fft / 2 + 1;
  Vectorf fft_freqs = (Vectorf::LinSpaced (n_f, 0.f, static_cast<float>(
      n_f - 1)) * sr) / n_fft;

  float f_min = 0.f;
  float f_sp = 200.f / 3.f;
  float min_log_hz = 1000.f;
  float min_log_mel = (min_log_hz - f_min) / f_sp;
  float logstep = logf(6.4f) / 27.f;

  auto hz_to_mel = [=](int hz, bool htk = false) -> float {
    if (htk)
      return 2595.0f * log10f (1.0f + hz / 700.0f);

    float mel = (hz - f_min) / f_sp;

    if (hz >= min_log_hz)
      mel = min_log_mel + logf (hz / min_log_hz) / logstep;

    return mel;
  };

  auto mel_to_hz = [=](Vectorf &mels, bool htk = false) -> Vectorf {
    if (htk)
      return 700.0f * (Vectorf::Constant(n_mels + 2, 10.f)
          .array ().pow (mels.array () / 2595.0f) - 1.0f);

    return (mels.array () > min_log_mel)
        .select(((mels.array () - min_log_mel) * logstep)
        .exp () * min_log_hz, (mels * f_sp).array () + f_min);
  };

  float min_mel = hz_to_mel (fmin);
  float max_mel = hz_to_mel (fmax);
  Vectorf mels = Vectorf::LinSpaced (n_mels + 2, min_mel, max_mel);
  Vectorf mel_f = mel_to_hz (mels);
  Vectorf fdiff = mel_f.segment (1, mel_f.size () - 1)
      - mel_f.segment (0, mel_f.size () - 1);
  Matrixf ramps = mel_f.replicate (n_f, 1).transpose ().array ()
      - fft_freqs.replicate (n_mels + 2, 1).array ();

  Matrixf lower = -ramps.topRows (n_mels).array () /
      fdiff.segment (0, n_mels).transpose ().replicate (1, n_f).array ();

  Matrixf upper = ramps.bottomRows (n_mels).array () /
      fdiff.segment (1, n_mels).transpose ().replicate (1, n_f).array ();

  Matrixf weights = (lower.array () < upper.array ())
      .select (lower, upper).cwiseMax (0);

  auto enorm = (2.0 / (mel_f.segment (2, n_mels) - mel_f.segment (0, n_mels))
      .array ()).transpose ().replicate (1, n_f);

  weights = weights.array () * enorm;

  return weights;
}

static Matrixf
melspectrogram (Vectorf &x, int sr, int n_fft, int n_hop,
    bool center, const std::string &mode, float power,
    int n_mels, const std::string &mel_file, int fmin, int fmax)
{
  Matrixcf X = stft (x, n_fft, n_hop, center, mode);
  Matrixf mel_basis;
  if (!mel_file.empty ())
    mel_basis = melfilter_from_file (mel_file, n_mels);
  else
    mel_basis = melfilter (sr, n_fft, n_mels, fmin, fmax);

  Matrixf sp = spectrogram (X, power);
  Matrixf mel = mel_basis * sp.transpose ();
  return mel;
}

static Matrixf
power2db (Matrixf& x)
{
  auto log_sp = 10.0f * x.array ().max (1e-10).log10 ();
  return log_sp.cwiseMax (log_sp.maxCoeff () - 80.0f);
}

static Matrixf
dct (Matrixf& x, bool norm, int type)
{
  int N = x.cols ();
  Matrixf xi = Matrixf::Zero (N, N);
  xi.rowwise() += Vectorf::LinSpaced (N, 0.f, static_cast<float> (N - 1));
  // type 2
  Matrixf coeff = 2 * (M_PI * xi.transpose ().array () /
      N * (xi.array () + 0.5)).cos ();
  Matrixf dct = x * coeff.transpose ();
  // ortho
  if (norm) {
    Vectorf ortho = Vectorf::Constant (N, std::sqrt (0.5f / N));
    ortho[0] = std::sqrt (0.25f / N);
    dct = dct * ortho.asDiagonal ();
  }

  return dct;
}

class Feature
{
public:
  /// \brief      short-time fourier transform similar with librosa.feature.stft
  /// \param      x             input audio signal
  /// \param      n_fft         length of the FFT size
  /// \param      n_hop         number of samples between successive frames
  /// \param      win           window function. currently only supports 'hann'
  /// \param      center        same as librosa
  /// \param      mode          pad mode. support "reflect","symmetric","edge"
  /// \return     complex-valued matrix of short-time fourier transform coefficients.
  static std::vector<std::vector<std::complex<float>>>
  stft (std::vector<float> &x, int n_fft, int n_hop, const std::string &win,
      bool center, const std::string &mode)
  {
    Vectorf map_x = Eigen::Map<Vectorf> (x.data (), x.size ());
    Matrixcf X = AudioPreprocess::stft (map_x, n_fft, n_hop, center, mode);
    std::vector<std::vector<std::complex<float>>> X_vector (
        X.rows (), std::vector<std::complex<float>> (X.cols (), 0));

    for (int i = 0; i < X.rows(); ++i){
      auto &row = X_vector[i];
      Eigen::Map<Vectorcf> (row.data (), row.size ()) = X.row (i);
    }

    return X_vector;
  }

  /// \brief      Frequency domain representation of audio signal
  /// \param      x             input audio signal
  /// \param      n_fft         length of the FFT size
  /// \param      n_hop         number of samples between successive frames
  /// \param      win           window function. currently only supports 'hann'
  /// \param      center        same as librosa
  /// \param      mode          pad mode. support "reflect","symmetric","edge"
  /// \param      power         exponent for the magnitude spectrogram
  /// \return     complex-valued matrix of frequency domain representation
  static std::vector<std::vector<float>>
  spectrogram (std::vector<float> &x, int sr, int n_fft, int n_hop,
      const std::string &win, bool center, const std::string &mode,
      float power)
  {
    Vectorf map_x = Eigen::Map<Vectorf> (x.data (), x.size ());
    Matrixcf X = AudioPreprocess::stft (map_x, n_fft, n_hop, center, mode);
    Matrixf sp = AudioPreprocess::spectrogram (X, power);

    std::vector<std::vector<float>> sp_vector (sp.rows (),
        std::vector<float> (sp.cols (), 0.f));

    for (int i = 0; i < sp.rows(); ++i) {
      auto &row = sp_vector[i];
      Eigen::Map<Vectorf> (row.data (), row.size ()) = sp.row (i);
    }

    return sp_vector;
  }

  /// \brief      compute mel-spectrogram/ mfe similar with librosa.feature.melspectrogram
  /// \param      x             input audio signal
  /// \param      sr            sample rate of 'x'
  /// \param      n_fft         length of the FFT size
  /// \param      n_hop         number of samples between successive frames
  /// \param      win           window function. currently only supports 'hann'
  /// \param      center        same as librosa
  /// \param      mode          pad mode. support "reflect","symmetric","edge"
  /// \param      power         exponent for the magnitude melspectrogram
  /// \param      n_mels        number of mel bands
  /// \param      f_min         lowest frequency (in Hz)
  /// \param      f_max         highest frequency (in Hz)
  /// \return     mel spectrogram matrix
  static std::vector<std::vector<float>>
  mfe (std::vector<float> &x, int sr, int n_fft, int n_hop,
      const std::string &win, bool center, const std::string &mode,
      float power, int n_mels, std::string &mel_file, int fmin, int fmax)
  {
    Vectorf map_x = Eigen::Map<Vectorf> (x.data (), x.size ());
    Matrixf mel = AudioPreprocess::melspectrogram (map_x, sr, n_fft, n_hop, center, mode,
        power, n_mels, mel_file, fmin, fmax).transpose ();
    std::vector<std::vector<float>> mel_vector (mel.rows (),
        std::vector<float> (mel.cols (), 0.f));

    for (int i = 0; i < mel.rows(); ++i){
      auto &row = mel_vector[i];
      Eigen::Map<Vectorf> (row.data (), row.size ()) = mel.row (i);
    }

    return mel_vector;
  }

  /// \brief      compute mfcc similar with librosa.feature.mfcc
  /// \param      x             input audio signal
  /// \param      sr            sample rate of 'x'
  /// \param      n_fft         length of the FFT size
  /// \param      n_hop         number of samples between successive frames
  /// \param      win           window function. currently only supports 'hann'
  /// \param      center        same as librosa
  /// \param      mode          pad mode. support "reflect","symmetric","edge"
  /// \param      power         exponent for the magnitude melspectrogram
  /// \param      n_mels        number of mel bands
  /// \param      f_min         lowest frequency (in Hz)
  /// \param      f_max         highest frequency (in Hz)
  /// \param      n_mfcc        number of mfccs
  /// \param      norm          ortho-normal dct basis
  /// \param      type          dct type. currently only supports 'type-II'
  /// \return     mfcc matrix
  static std::vector<std::vector<float>>
  mfcc (std::vector<float> &x, int sr, int n_fft, int n_hop,
      const std::string &win, bool center, const std::string &mode,
      float power, int n_mels, std::string &mel_file, int fmin, int fmax,
      int n_mfcc, bool norm, int type)
  {
    Vectorf map_x = Eigen::Map<Vectorf> (x.data (), x.size ());
    Matrixf mel = AudioPreprocess::melspectrogram (map_x, sr, n_fft, n_hop, center, mode,
        power, n_mels, mel_file, fmin, fmax).transpose ();
    Matrixf mel_db = AudioPreprocess::power2db (mel);
    Matrixf dct = AudioPreprocess::dct (mel_db, norm, type).leftCols (n_mfcc);
    std::vector<std::vector<float>> mfcc_vector (dct.rows (),
        std::vector<float> (dct.cols (), 0.f));

    for (int i = 0; i < dct.rows (); ++i) {
      auto &row = mfcc_vector[i];
      Eigen::Map<Vectorf> (row.data (), row.size ()) = dct.row (i);
    }

    return mfcc_vector;
  }

  /// \brief      compute log mel spectrogram / lmfe
  /// \param      x             input audio signal
  /// \param      sr            sample rate of 'x'
  /// \param      n_fft         length of the FFT size
  /// \param      n_hop         number of samples between successive frames
  /// \param      center        same as librosa
  /// \param      mode          pad mode. support "reflect","symmetric","edge"
  /// \param      power         exponent for the magnitude melspectrogram
  /// \param      n_mels        number of mel bands
  /// \param      f_min         lowest frequency (in Hz)
  /// \param      f_max         highest frequency (in Hz)
  /// \return     mel spectrogram matrix
  static std::vector<std::vector<float>>
  lmfe (std::vector<float> &x, int sr, int n_fft, int n_hop,
      bool center, const std::string &mode, float power,
      int n_mels, std::string &mel_file, int fmin, int fmax)
  {
    Vectorf map_x = Eigen::Map<Vectorf> (x.data (), x.size ());
    Matrixf mel = AudioPreprocess::melspectrogram(map_x, sr, n_fft, n_hop,
        center, mode, power, n_mels, mel_file, fmin, fmax).transpose ();
    Matrixf mel_db = (AudioPreprocess::power2db (mel).array () + 4.0) * 0.25;

    //for(int i = 0; i < mel_db.size(); i++)
    //  *(mel_db.data() + i) = (*(mel_db.data() + i) + 4) / 4;

    std::vector<std::vector<float>> mel_vector (mel_db.rows (),
      std::vector<float> (mel_db.cols (), 0.f));

    for (int i = 0; i < mel_db.rows (); ++i){
      auto &row = mel_vector[i];
      Eigen::Map<Vectorf> (row.data (), row.size ()) = mel_db.row (i);
    }

    return mel_vector;
  }
}; // Feature
} // AudioPreprocess

struct _GstAudioConvEngine {
  // audio sample rate
  int                sample_rate;
  // audio sample number
  int                sample_number;
  // audio feature
  GstAudioFeature     feature;
  // audio bytes per sample
  int                bps;
  // number of ffts to prepare for in frequency-time domain
  int                n_fft;
  // hop length number of samples to skip to decide window positioning
  int                n_hop;
  // number of melbins from spectrogram
  int                n_mels;
  // min frequency for mel filter
  int                min_hz;
  // max frequency for mel filter
  int                max_hz;
  // mel filter file
  const gchar         *mel_filter;

  // tensor type
  GstMLType           tensor_type;
  // Input Audioformat
  GstAudioFormat      format;

  // data converter function
  ConvertFunc         convert;
};

static inline void
gst_mlaconverter_engine_initialize_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (mlac_engine_debug, "mlac-engine-debug", 0,
        "audio converter engine");
    g_once_init_leave (&catonce, TRUE);
  }
}

const gchar *
gst_audio_feature_to_string (GstAudioFeature feature)
{
  switch (feature)
  {
    case GST_AUDIO_FEATURE_STFT:
      return GST_AUDIO_FEATURE_STFT_NAME;
    case GST_AUDIO_FEATURE_SPECTROGRAM:
      return GST_AUDIO_FEATURE_SPECTROGRAM_NAME;
    case GST_AUDIO_FEATURE_MFE:
      return GST_AUDIO_FEATURE_MFE_NAME;
    case GST_AUDIO_FEATURE_LMFE:
      return GST_AUDIO_FEATURE_LMFE_NAME;
    case GST_AUDIO_FEATURE_MFCC:
      return GST_AUDIO_FEATURE_MFCC_NAME;
    case GST_AUDIO_FEATURE_RAW:
    default:
      return GST_AUDIO_FEATURE_RAW_NAME;
  }
}

GstAudioFeature
gst_audio_feature_from_string (const gchar * feature)
{
  g_return_val_if_fail (feature != NULL, GST_AUDIO_FEATURE_UNKNOWN);

  if (strcmp (GST_AUDIO_FEATURE_RAW_NAME, feature) == 0)
    return GST_AUDIO_FEATURE_RAW;
  else if (strcmp (GST_AUDIO_FEATURE_STFT_NAME, feature) == 0)
    return GST_AUDIO_FEATURE_STFT;
  else if (strcmp (GST_AUDIO_FEATURE_MFE_NAME, feature) == 0)
    return GST_AUDIO_FEATURE_MFE;
  else if (strcmp (GST_AUDIO_FEATURE_LMFE_NAME, feature) == 0)
    return GST_AUDIO_FEATURE_LMFE;
  else if (strcmp (GST_AUDIO_FEATURE_MFCC_NAME, feature) == 0)
    return GST_AUDIO_FEATURE_MFCC;

  return GST_AUDIO_FEATURE_UNKNOWN;
}

GstAudioConvEngine *
gst_mlaconverter_engine_new (const GstStructure * settings)
{
  GstAudioConvEngine *engine;
  GstCaps *incaps = NULL, *outcaps = NULL;
  GstAudioInfo ininfo;
  GstMLInfo mlinfo;
  gboolean success = TRUE;
  GstStructure * structure = NULL;

  g_return_val_if_fail (settings != NULL, NULL);

  gst_mlaconverter_engine_initialize_debug_category ();

  engine = g_slice_new0 (GstAudioConvEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  if (!(success = gst_structure_has_field (settings,
      GST_AUDIO_CONVERTER_OPT_INCAPS))) {
    GST_ERROR ("Audio Converter didn't receive settings with Audio Caps!");
    goto cleanup;
  }

  gst_structure_get (settings, GST_AUDIO_CONVERTER_OPT_INCAPS, GST_TYPE_CAPS,
      &incaps, NULL);

  if (!(success = gst_audio_info_from_caps (&ininfo, incaps))) {
    GST_ERROR ("Audio Converter received invalid caps in settings!");
    goto cleanup;
  }

  engine->sample_rate = GST_AUDIO_INFO_RATE (&ininfo);
  engine->format = GST_AUDIO_INFO_FORMAT (&ininfo);
  engine->bps    = GST_AUDIO_INFO_BPS (&ininfo);

  if (!(success = gst_structure_has_field (settings,
      GST_AUDIO_CONVERTER_OPT_MLCAPS))) {
    GST_ERROR ("Audio Converter didn't receive settings with ML Caps!");
    goto cleanup;
  }

  gst_structure_get (settings, GST_AUDIO_CONVERTER_OPT_MLCAPS, GST_TYPE_CAPS,
      &outcaps, NULL);

  if (!(success = gst_ml_info_from_caps (&mlinfo, outcaps))) {
    GST_ERROR ("Audio Converter has received invalid outcaps in settings");
    goto cleanup;
  }

  engine->tensor_type = GST_ML_INFO_TYPE (&mlinfo);

  if (!(success = (engine->tensor_type == GST_ML_TYPE_FLOAT32))) {
    GST_ERROR ("Audio Converter can produce tensors of type FLOAT presently!"
        "Found an MLType which is not FLOAT");
    goto cleanup;
  }

  switch (engine->format)
  {
    case GST_AUDIO_FORMAT_S8:
      engine->convert = do_convert_gint8_gfloat;
      break;
    case GST_AUDIO_FORMAT_U8:
      engine->convert = do_convert_guint8_gfloat;
      break;
    case GST_AUDIO_FORMAT_S16LE:
      engine->convert = do_convert_gint16_gfloat;
      break;
    case GST_AUDIO_FORMAT_U16LE:
      engine->convert = do_convert_guint16_gfloat;
      break;
    case GST_AUDIO_FORMAT_S32LE:
      engine->convert = do_convert_gint32_gfloat;
      break;
    case GST_AUDIO_FORMAT_U32LE:
      engine->convert = do_convert_guint32_gfloat;
      break;
    case GST_AUDIO_FORMAT_F32LE:
      engine->convert = do_convert_gfloat_gfloat;
      break;
    default:
      break;
  }

  if (!(success = gst_structure_has_field (settings,
    GST_AUDIO_CONVERTER_OPT_FEATURE))) {
  GST_ERROR ("Audio Converter didn't receive settings with feature spec!");
  goto cleanup;
  }

  engine->feature = gst_audio_feature_from_string (gst_structure_get_string (
      settings, GST_AUDIO_CONVERTER_OPT_FEATURE));

  structure = gst_structure_from_string (gst_structure_get_string (settings,
    GST_AUDIO_CONVERTER_OPT_PARAMS), NULL);

  if ((structure != NULL ) && gst_structure_has_field (structure, "nfft"))
    engine->n_fft = g_value_get_int (gst_structure_get_value
        (structure, "nfft"));
  else
    engine->n_fft = DEFAULT_N_FFT;

  if ((structure != NULL ) && gst_structure_has_field (structure, "nmels"))
    engine->n_mels = g_value_get_int (gst_structure_get_value
        (structure, "nmels"));
  else
    engine->n_mels = DEFAULT_N_MELS;

  if ((structure != NULL ) && gst_structure_has_field (structure, "nhop"))
    engine->n_hop = g_value_get_int (gst_structure_get_value
        (structure, "nhop"));
  else
    engine->n_hop = DEFAULT_N_HOP;

  if ((structure != NULL ) && gst_structure_has_field (structure, "fmin"))
    engine->min_hz = g_value_get_int (gst_structure_get_value
        (structure, "fmin"));
  else
    engine->min_hz = DEFAULT_MIN_HZ;

  if ((structure != NULL ) && gst_structure_has_field (structure, "fmax"))
    engine->max_hz = g_value_get_int (gst_structure_get_value
        (structure, "fmax"));
  else
    engine->max_hz = DEFAULT_MAX_HZ;

  if ((structure != NULL ) && gst_structure_has_field (structure, "chunklen"))
    engine->sample_number = g_value_get_double (gst_structure_get_value
        (structure, "chunklen")) * engine->sample_rate;
  else
    engine->sample_number = DEFAULT_SAMPLE_NUMBER;

  if ((structure != NULL ))
    engine->mel_filter = (gst_structure_get_string (structure, "melfilter"));

cleanup:
  if (incaps != NULL)
    gst_caps_unref (incaps);

  if (outcaps != NULL)
    gst_caps_unref (outcaps);

  if (structure != NULL)
    structure = NULL;

  if (success) {
    GST_LOG ("Created Audio Converter Engine");
    return engine;
  }
  else {
    gst_mlaconverter_engine_free (engine);
    GST_LOG ("Couldn't create audio converter engine");
    return NULL;
  }
}

void
gst_mlaconverter_engine_free (GstAudioConvEngine * engine)
{
  if (NULL == engine)
    return;

  g_slice_free (GstAudioConvEngine, engine);

  return;
}

gboolean
gst_mlaconverter_engine_process (GstAudioConvEngine * engine,
    GstAudioBuffer *audioframe, GstMLFrame *mlframe)
{
  size_t audio_num, tensor_num, process_num;
  GstMapInfo *audioinfo = &(audioframe->map_infos[0]);
  gpointer outdata = NULL;
  GstMLType mltype = GST_ML_TYPE_UNKNOWN;
  gboolean success = TRUE;

  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail (audioframe != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);

  audio_num = audioinfo->size / engine->bps;

  mltype = GST_ML_FRAME_TYPE (mlframe);
  outdata = GST_ML_FRAME_BLOCK_DATA (mlframe, 0);

  GST_LOG ("Engine Processing %s", gst_audio_feature_to_string(engine->feature));

  if (mltype == GST_ML_TYPE_FLOAT32) {
    tensor_num = GST_ML_FRAME_BLOCK_SIZE (mlframe, 0) /
        gst_ml_type_get_size (mltype);

    switch (engine->feature)
    {
      case GST_AUDIO_FEATURE_RAW:
      {
        process_num = (audio_num >= tensor_num) ? (tensor_num) : (audio_num);
        engine->convert ((const void *)audioinfo->data, (void * )outdata,
            process_num, process_num);
        success = TRUE;
        break;
      }
      case GST_AUDIO_FEATURE_LMFE:
      {
        std::vector<float> lmfe_input;
        int i = 0, n_windows = 1;
        std::string melfilter;
        std::string mode("symmetric");
        n_windows = engine->sample_number / engine->n_hop;

        if (tensor_num != (size_t)engine->n_mels * n_windows) {
          GST_ERROR ("LogMelSpectrogram is misconfigured!");
          success = FALSE;
          break;
        }

        float * temp = new float[engine->sample_number];

        engine->convert ((const void *)audioinfo->data, (void * )temp,
            audio_num, engine->sample_number);

        if (NULL != engine->mel_filter)
          melfilter.append(engine->mel_filter);

        lmfe_input.resize (engine->sample_number);
        std::copy(temp, temp + (engine->sample_number - 1), lmfe_input.begin());
        delete[] temp;

        std::vector<std::vector<float>> lmfe_result =
            AudioPreprocess::Feature::lmfe (lmfe_input, engine->sample_rate,
            engine->n_fft, engine->n_hop, TRUE, mode, 2, engine->n_mels,
            melfilter, engine->min_hz, engine->max_hz);

        for (auto x : lmfe_result) {
          std::copy (x.begin(), x.end(), (gfloat *)outdata + (i * engine->n_mels));
          i++;
          if (i > (n_windows - 1))
            break;
        }

        GST_LOG ("LMFE Done processing");
        success = TRUE;
        break;
      }
      case GST_AUDIO_FEATURE_STFT:
      case GST_AUDIO_FEATURE_SPECTROGRAM:
      case GST_AUDIO_FEATURE_MFE:
      case GST_AUDIO_FEATURE_MFCC:
      default:
        GST_ERROR ("Audio Converter hasn't received valid feature settings");
        success = FALSE;
    }
  } else {
    GST_ERROR ("Supporting only FLOAT32 Type tensors");
    // not supporting other input types to model right now!
    success = FALSE;
  }

  return success;
}
