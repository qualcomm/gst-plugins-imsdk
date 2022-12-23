/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <data/QCMAP_Client.h>
#include <glib-unix.h>

#include "wifi.h"

#define TAG "\ngst-hibernate-example (Wi-Fi): "

gboolean
enable_mobile_ap (QCMAP_Client * qcmap_client)
{
  qmi_error_type_v01 qmi_err_num = QMI_ERR_NONE_V01;
  gboolean result;

  result = qcmap_client->EnableMobileAP (&qmi_err_num);
  if (!result)
    g_printerr ("%sFailed to EnableMobileAP: %x.\n", TAG, qmi_err_num);

  return result;
}

gboolean
disable_mobile_ap (QCMAP_Client * qcmap_client)
{
  qmi_error_type_v01 qmi_err_num = QMI_ERR_NONE_V01;
  gboolean result;

  result = qcmap_client->DisableMobileAP (&qmi_err_num);
  if (!result)
    g_printerr ("%sFailed to DisableMobileAP: %x.\n", TAG, qmi_err_num);

  return result;
}

gboolean
is_wifi_on ()
{
  QCMAP_Client *qcmap_client;
  qcmap_msgr_wlan_mode_enum_v01 wifi_status;
  qmi_error_type_v01 qmi_err_num = QMI_ERR_NONE_V01;
  gboolean result;

  g_print ("%sEnter is_wifi_on.\n", TAG);

  qcmap_client = new QCMAP_Client (NULL);

  enable_mobile_ap (qcmap_client);

  result = qcmap_client->GetWLANStatus (&wifi_status, &qmi_err_num);
  if (!result)
    g_printerr ("%sFailed to GetWLANStatus: %x.\n", TAG, qmi_err_num);
  else
    g_print ("%sSuccess GetWLANStatus, wifi_status is: %d.\n", TAG,
        wifi_status);

  disable_mobile_ap (qcmap_client);

  delete qcmap_client;
  g_print ("%sExit is_wifi_on.\n", TAG);

  return (wifi_status == -2147483647) ? false : true;
}

gboolean
enable_wifi ()
{
  QCMAP_Client *qcmap_client;
  qmi_error_type_v01 qmi_err_num1 = QMI_ERR_NONE_V01;
  gboolean result1;
  qmi_error_type_v01 qmi_err_num2 = QMI_ERR_NONE_V01;
  gboolean result2;

  g_print ("%sEnter enable_wifi.\n", TAG);

  qcmap_client = new QCMAP_Client (NULL);

  enable_mobile_ap (qcmap_client);

  result1 = qcmap_client->EnableWLAN (&qmi_err_num1);
  if (!result1)
    g_printerr ("%sFailed to EnableWLAN: %x.\n", TAG, qmi_err_num1);
  else
    g_print ("%sSuccess EnableWLAN.\n", TAG);

  result2 = qcmap_client->SetAlwaysOnWLAN (true, &qmi_err_num2);
  if (!result2)
    g_printerr ("%sFailed to SetAlwaysOnWLAN: %x.\n", TAG, qmi_err_num2);
  else
    g_print ("%sSuccess SetAlwaysOnWLAN.\n", TAG);

  disable_mobile_ap (qcmap_client);

  delete qcmap_client;
  g_print ("%sExit enable_wifi.\n", TAG);

  return (result1 && result2);
}

gboolean
disable_wifi ()
{
  QCMAP_Client *qcmap_client;
  qmi_error_type_v01 qmi_err_num = QMI_ERR_NONE_V01;
  gboolean result;

  g_print ("%sEnter disable_wifi.\n", TAG);

  qcmap_client = new QCMAP_Client (NULL);

  enable_mobile_ap (qcmap_client);

  result = qcmap_client->DisableWLAN (&qmi_err_num);
  if (!result)
    g_printerr ("%sFailed to DisableWLAN: %x.\n", TAG, qmi_err_num);
  else
    g_print ("%sSuccess DisableWLAN.\n", TAG);

  disable_mobile_ap (qcmap_client);

  delete qcmap_client;
  g_print ("%sExit disable_wifi.\n", TAG);

  return result;
}
