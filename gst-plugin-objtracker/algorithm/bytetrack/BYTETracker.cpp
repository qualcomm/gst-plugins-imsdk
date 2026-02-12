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
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "BYTETracker.h"


BYTETracker::BYTETracker(const ByteTrackerConfig &config)
{

    track_thresh = config.track_thresh; //0.5
    high_thresh = config.high_thresh; // 0.6;

    match_thresh = 0.8f;

    frame_id = 0;
    max_time_lost = int(config.frame_rate / 30.0f * config.track_buffer);

    QMOT_LOG_DEBUG("BYTETracker constructor, max_time_lost = %d", max_time_lost);
    QMOT_LOG_DEBUG("BYTETracker constructor, config.frame_rate = %d", config.frame_rate);
    QMOT_LOG_DEBUG("BYTETracker constructor, config.track_buffer = %d", config.track_buffer);

    track_wh_smooth_factor = config.wh_smooth_factor;
}

BYTETracker::~BYTETracker()
{
}

vector<STrack> BYTETracker::update(const vector<ByteTrackerObject>& objects)
{
    // QMOT_LOG_DEBUG("--------------------------- frame %d ---------------------------", this->frame_id);

    ////////////////// Step 1: Get detections //////////////////
    this->frame_id++;
    vector<STrack*> activated_stracks;
    vector<STrack*> refind_stracks;
    vector<STrack*> removed_stracks;
    vector<STrack*> lost_stracks;

    vector<STrack*> detections_all;
    vector<STrack*> detections;
    vector<STrack*> detections_low;
    vector<STrack*> detections_cp;
    vector<STrack*> detections_ambiguous; // detections which are close to more than one tracks in first association

    vector<STrack> output_stracks;

    vector<STrack*> unconfirmed;
    vector<STrack*> tracked_stracks;
    vector<STrack*> strack_pool;
    vector<STrack*> r_tracked_stracks;

    // Calculate maximum overlap(IOU) between each detection box
    // Our detection boxes are from instance mask, so basically there won't be large overlapped boxes
    vector<float> adjacency_ious(objects.size(), 0.f);
    for (size_t i = 0; i < adjacency_ious.size(); i++) {
        for (size_t j = 0; j < adjacency_ious.size(); j++) {
            if (i != j) {
                // compute "intersection over self"
                float iou = compute_intersection_over_self (
                    objects[i].bounding_box[0],
                    objects[i].bounding_box[1],
                    objects[i].bounding_box[2],
                    objects[i].bounding_box[3],
                    objects[j].bounding_box[0],
                    objects[j].bounding_box[1],
                    objects[j].bounding_box[2],
                    objects[j].bounding_box[3]);
                adjacency_ious[i] = max(adjacency_ious[i], iou);
            }
        }
    }

    if (objects.size() > 0) {
        for (size_t i = 0; i < objects.size(); i++) {
            vector<float> tlbr_;
            tlbr_.resize(4);
            /*tlbr_[0] = objects[i].rect.x;
            tlbr_[1] = objects[i].rect.y;
            tlbr_[2] = objects[i].rect.x + objects[i].rect.width;
            tlbr_[3] = objects[i].rect.y + objects[i].rect.height;*/
            tlbr_[0] = objects[i].bounding_box[0];
            tlbr_[1] = objects[i].bounding_box[1];
            tlbr_[2] = objects[i].bounding_box[2];
            tlbr_[3] = objects[i].bounding_box[3];

            float score = objects[i].prob;

            STrack *strack = new STrack(STrack::tlbr_to_tlwh(tlbr_), score, objects[i].label);
            strack->adjacency_overlap = adjacency_ious[i];

            detections_all.push_back(strack);

            if (score >= track_thresh)
                detections.push_back(strack);
            else
                detections_low.push_back(strack);
        }
    }

    // Add newly detected tracklets to tracked_stracks
    for (size_t i = 0; i < this->m_tracked_stracks.size(); i++) {
        if (this->m_tracked_stracks[i]->state == TrackState::New)
            unconfirmed.push_back(this->m_tracked_stracks[i]);
        else
            tracked_stracks.push_back(this->m_tracked_stracks[i]);
    }

    ////////////////// Step 2: First association, with IoU //////////////////
    strack_pool = joint_stracks(tracked_stracks, this->m_lost_stracks);

    STrack::multi_predict(strack_pool, this->kalman_filter);

    //change object bounding box to prediction
    for (size_t i = 0; i < strack_pool.size(); i++) {
        strack_pool[i]->static_tlwh();
        strack_pool[i]->static_tlbr();
    }

    vector<vector<float> > dists;
    uint32_t dist_size = 0, dist_size_size = 0;
    dists = iou_distance(strack_pool, detections, dist_size, dist_size_size);

    // 1. find ambiguous detections which are close to more than one tracks
    // 2. find ambiguous tracks which are close to more than one detections

    // recompute distance without ambiguous detections
    dists = iou_distance(strack_pool, detections, dist_size, dist_size_size);

    vector<vector<uint32_t> > matches;
    vector<uint32_t> u_track, u_detection;
    linear_assignment(dists, dist_size, dist_size_size, match_thresh, matches, u_track, u_detection);

    for (size_t i = 0; i < matches.size(); i++) {
        STrack *track = strack_pool[matches[i][0]];
        STrack *det = detections[matches[i][1]];
        //convert from distance to score, the larger the better
        float iou_score = 1 - dists[matches[i][0]][matches[i][1]];
        if (track->state == TrackState::Tracked) {
            //update the tracker with matched detection
            track->update(*det, this->frame_id, iou_score, this->track_wh_smooth_factor);
            // update corresponding detection id
            track->matched_detection_id = det->matched_detection_id;
            activated_stracks.push_back(track);
        }
        else {
            track->re_activate(*det, this->frame_id, false, iou_score);
            // update corresponding detection id
            track->matched_detection_id = det->matched_detection_id;
            refind_stracks.push_back(track);
        }
    }

    ////////////////// Step 3: Second association, using low score dets //////////////////
    for (size_t i = 0; i < u_detection.size(); i++) {
        detections_cp.push_back(detections[u_detection[i]]);
    }
    detections.clear();
    detections.assign(detections_low.begin(), detections_low.end());

    for (size_t i = 0; i < u_track.size(); i++) {
        if (strack_pool[u_track[i]]->state == TrackState::Tracked) {
            r_tracked_stracks.push_back(strack_pool[u_track[i]]);
        }
        else {
            strack_pool[u_track[i]]->matched_detection_id = -1;
            lost_stracks.push_back(strack_pool[u_track[i]]);
        }
    }

    dists.clear();
    dists = iou_distance(r_tracked_stracks, detections, dist_size, dist_size_size);

    matches.clear();
    u_track.clear();
    u_detection.clear();
    linear_assignment(dists, dist_size, dist_size_size, 0.5, matches, u_track, u_detection);

    for (size_t i = 0; i < matches.size(); i++) {
        STrack *track = r_tracked_stracks[matches[i][0]];
        STrack *det = detections[matches[i][1]];
        float iou_score = 1 - dists[matches[i][0]][matches[i][1]]; //convert from distance to score, the larger the better
        if (track->state == TrackState::Tracked) {
            track->update(*det, this->frame_id, iou_score, this->track_wh_smooth_factor);
            track->matched_detection_id = det->matched_detection_id; // update corresponding detection id
            activated_stracks.push_back(track);
        }
        else {
            track->re_activate(*det, this->frame_id, false, iou_score);
            track->matched_detection_id = det->matched_detection_id; // update corresponding detection id
            refind_stracks.push_back(track);
        }
    }

    for (size_t i = 0; i < u_track.size(); i++) {
        STrack *track = r_tracked_stracks[u_track[i]];
        track->mark_lost();
        track->matched_detection_id = -1;
        lost_stracks.push_back(track);
    }

    // Deal with unconfirmed tracks, usually tracks with only one beginning frame
    detections.clear();
    detections.assign(detections_cp.begin(), detections_cp.end());

    dists.clear();
    dists = iou_distance(unconfirmed, detections, dist_size, dist_size_size);

    matches.clear();
    vector<uint32_t> u_unconfirmed;
    u_detection.clear();
    linear_assignment(dists, dist_size, dist_size_size, 0.7f, matches, u_unconfirmed, u_detection);

    for (size_t i = 0; i < matches.size(); i++) {
        float iou_score = 1 - dists[matches[i][0]][matches[i][1]]; //convert from distance to score, the larger the better

        STrack *track = unconfirmed[matches[i][0]];
        STrack *det = detections[matches[i][1]];
        track->update(*det, this->frame_id, iou_score, this->track_wh_smooth_factor);
        track->matched_detection_id = det->matched_detection_id; // update corresponding detection id
        activated_stracks.push_back(track);
    }

    for (size_t i = 0; i < u_unconfirmed.size(); i++) {
        STrack *track = unconfirmed[u_unconfirmed[i]];
        track->mark_removed();
        removed_stracks.push_back(track);
    }

    ////////////////// Step 4: Init new stracks //////////////////
    //activation is only on newly unmatched high-confidence detection
    for (size_t i = 0; i < u_detection.size(); i++) {
        if (detections[u_detection[i]]->score < this->high_thresh)
            continue;

        STrack *track = new STrack(*detections[u_detection[i]]); // copy constructor
        track->activate(this->kalman_filter, this->frame_id);
        activated_stracks.push_back(track);

        QMOT_LOG_DEBUG("Init new track: %d", track->track_id);
    }

    // Delete all detections
    for (size_t i = 0; i < detections_all.size(); i++) {
        delete detections_all[i];
    }

    ////////////////// Step 5: Update state - ychiao //////////////////
    // (1) update this->m_tracked_stracks: combine activated_stracks and refind_stracks
    // unconfirmed tracks are already in activated_stracks
    this->m_tracked_stracks.clear();
    this->m_tracked_stracks = joint_stracks(this->m_tracked_stracks, activated_stracks);
    this->m_tracked_stracks = joint_stracks(this->m_tracked_stracks, refind_stracks);

    // (2) update this->m_lost_tracks and this->m_removed_stracks
    this->m_lost_stracks.clear();
    this->m_removed_stracks.clear();
    for (size_t i = 0; i < lost_stracks.size(); i++) {
        if (this->frame_id - lost_stracks[i]->end_frame() > this->max_time_lost)
          this->m_removed_stracks.push_back(lost_stracks[i]);
        else
          this->m_lost_stracks.push_back(lost_stracks[i]);
    }

    for (size_t i = 0; i < removed_stracks.size(); i++)
      this->m_removed_stracks.push_back(removed_stracks[i]);

    // Do not keep removed tracks for memory efficiency
    for (size_t i = 0; i < this->m_removed_stracks.size(); i++)
      delete this->m_removed_stracks[i];

    this->m_removed_stracks.clear();

    // returned the tracked tracks
    for (size_t i = 0; i < this->m_tracked_stracks.size(); i++)
      output_stracks.push_back(*this->m_tracked_stracks[i]);

    // add the lost tracks (no matched detections)
    for (size_t i = 0; i < this->m_lost_stracks.size(); i++)
      output_stracks.push_back(*this->m_lost_stracks[i]);

    // print_statistics();
    return output_stracks;
}

void BYTETracker::print_statistics()
{
    QMOT_LOG_DEBUG("print_statistics");

    // Prsize_t tracked tracks
    string str = "tracked ID: ";
    for (size_t i = 0; i < this->m_tracked_stracks.size(); i++) {
        if (this->m_tracked_stracks[i]->state == TrackState::Tracked) {
            str += to_string(this->m_tracked_stracks[i]->track_id) + ", ";
        }
    }
    QMOT_LOG_DEBUG("%s", str.c_str());

    // Prsize_t unconfirmed tracks
    str = "unconfirmed ID: ";
    for (size_t i = 0; i < this->m_tracked_stracks.size(); i++) {
        if (this->m_tracked_stracks[i]->state == TrackState::New) {
            str += to_string(this->m_tracked_stracks[i]->track_id) + ", ";
        }
    }

    QMOT_LOG_DEBUG("%s", str.c_str());

    // Prsize_t lost tracks
    str = "lost ID: ";
    for (size_t i = 0; i < this->m_lost_stracks.size(); i++) {
        str += to_string(this->m_lost_stracks[i]->track_id) + ", ";
    }

    QMOT_LOG_DEBUG("%s", str.c_str());
}
