/*
 * Copyright (c) 2021 Yifu Zhang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <map>

#include "kalmanFilter.h"

//using namespace cv;
using namespace std;

enum TrackState { New = 0, Tracked, Lost, Removed };

class STrack
{
public:
    STrack(vector<float> tlwh_, float score, int det_id = -1);
	~STrack();

	vector<float> static tlbr_to_tlwh(vector<float> &tlbr);
	void static multi_predict(vector<STrack*> &stracks, byte_kalman::KalmanFilter &kalman_filter);
	void static_tlwh();
	void static_tlbr();
	vector<float> tlwh_to_xyah(vector<float> tlwh_tmp);
	vector<float> to_xyah();
	void mark_lost();
	void mark_removed();
	int next_id();
	int end_frame();

	void activate(byte_kalman::KalmanFilter &kalman_filter, int frame_id);
	void re_activate(STrack &new_track, int frame_id, bool new_id = false, float iou_score = 0);
	void update(STrack &new_track, int frame_id, float iou_score = 0, float sz_smooth_factor = 0.9f);

public:
	bool is_activated; //flag for confirmed and unconfirmed tracks
	int track_id;
	int state;

	vector<float> _tlwh;
	vector<float> tlwh;
	vector<float> tlbr;
	int frame_id;
	int tracklet_len;
	int start_frame;

    vector<float> smoothed_wh; //size of two

	KAL_MEAN mean;
	KAL_COVA covariance;
	float score;

    //ADDED members
    int   matched_detection_id; //original index of the matched detection
    float iou_with_det;



private:
	byte_kalman::KalmanFilter kalman_filter;
};
