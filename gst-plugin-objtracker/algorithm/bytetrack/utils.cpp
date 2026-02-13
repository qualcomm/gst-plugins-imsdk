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
#include "lapjv.h"

using namespace std;

vector<STrack*> BYTETracker::joint_stracks(vector<STrack*> &tlista,
                                           vector<STrack> &tlistb) {

    map<uint32_t, uint32_t> exists;
    vector<STrack*> res;
    for (size_t i = 0; i < tlista.size(); i++) {
        exists.insert(pair<uint32_t, uint32_t>(tlista[i]->track_id, 1));
        res.push_back(tlista[i]);
    }
    for (size_t i = 0; i < tlistb.size(); i++) {
        uint32_t tid = tlistb[i].track_id;
        if (!exists[tid] || exists.count(tid) == 0) {
            exists[tid] = 1;
            res.push_back(&tlistb[i]);
        }
    }
    return res;
}

vector<STrack> BYTETracker::joint_stracks(vector<STrack> &tlista,
                                          vector<STrack> &tlistb) {
    map<uint32_t, uint32_t> exists;
    vector<STrack> res;
    for (size_t i = 0; i < tlista.size(); i++) {
        exists.insert(pair<uint32_t, uint32_t>(tlista[i].track_id, 1));
        res.push_back(tlista[i]);
    }
    for (size_t i = 0; i < tlistb.size(); i++) {
        uint32_t tid = tlistb[i].track_id;
        if (!exists[tid] || exists.count(tid) == 0) {
            exists[tid] = 1;
            res.push_back(tlistb[i]);
        }
    }
    return res;
}

vector<STrack*> BYTETracker::joint_stracks(vector<STrack*> &tlista,
                                           vector<STrack*> &tlistb) {

    map<uint32_t, uint32_t> exists;
    vector<STrack*> res;
    for (size_t i = 0; i < tlista.size(); i++) {
        exists.insert(pair<uint32_t, uint32_t>(tlista[i]->track_id, 1));
        res.push_back(tlista[i]);
    }
    for (size_t i = 0; i < tlistb.size(); i++) {
        uint32_t tid = tlistb[i]->track_id;
        if (!exists[tid] || exists.count(tid) == 0) {
            exists[tid] = 1;
            res.push_back(tlistb[i]);
        }
    }
    return res;
}

vector<STrack> BYTETracker::sub_stracks(vector<STrack> &tlista,
                                        vector<STrack> &tlistb) {

    map<uint32_t, STrack> stracks;
    for (size_t i = 0; i < tlista.size(); i++)
      stracks.insert(pair<uint32_t, STrack>(tlista[i].track_id, tlista[i]));

    for (size_t i = 0; i < tlistb.size(); i++) {
        uint32_t tid = tlistb[i].track_id;
        if (stracks.count(tid) != 0)
          stracks.erase(tid);
    }

    vector<STrack> res;
    std::map<uint32_t, STrack>::iterator  it;
    for (it = stracks.begin(); it != stracks.end(); ++it)
      res.push_back(it->second);

    return res;
}

vector<STrack*> BYTETracker::sub_stracks(vector<STrack*> &tlista,
                                         vector<STrack*> &tlistb) {

    map<uint32_t, STrack*> stracks;
    for (size_t i = 0; i < tlista.size(); i++)
        stracks.insert(pair<uint32_t, STrack*>(tlista[i]->track_id, tlista[i]));

    for (size_t i = 0; i < tlistb.size(); i++) {
        uint32_t tid = tlistb[i]->track_id;
        if (stracks.count(tid) != 0) {
            stracks.erase(tid);
        }
    }

    vector<STrack*> res;
    std::map<uint32_t, STrack*>::iterator  it;
    for (it = stracks.begin(); it != stracks.end(); ++it)
        res.push_back(it->second);

    return res;
}


//remove tracks that have large overlap (i.e. small iou distance),
//keep the one with longer tracker history, return the remaining once
void BYTETracker::remove_duplicate_stracks(std::vector<STrack> &resa,
                                           std::vector<STrack> &resb,
                                           std::vector<STrack> &stracksa,
                                           std::vector<STrack> &stracksb) {

    std::vector<std::vector<float> > pdist = iou_distance(stracksa, stracksb);  // dist = 1 - iou
    std::vector<std::pair<uint32_t, uint32_t> > pairs;
    for (size_t i = 0; i < pdist.size(); i++) {
        for (size_t j = 0; j < pdist[i].size(); j++) {
            if (pdist[i][j] < 0.15)
              pairs.push_back(pair<uint32_t, uint32_t>(i, j));
        }
    }

    std::vector<uint32_t> dupa, dupb;
    for (size_t i = 0; i < pairs.size(); i++) {
        uint32_t timep = stracksa[pairs[i].first].frame_id - stracksa[pairs[i].first].start_frame;
        uint32_t timeq = stracksb[pairs[i].second].frame_id - stracksb[pairs[i].second].start_frame;
        if (timep >= timeq)
          dupb.push_back(pairs[i].second);
        else
          dupa.push_back(pairs[i].first);
    }

    for (size_t i = 0; i < stracksa.size(); i++) {
        std::vector<uint32_t>::iterator iter = find(dupa.begin(), dupa.end(), i);
        if (iter == dupa.end())
          resa.push_back(stracksa[i]);
    }

    for (size_t i = 0; i < stracksb.size(); i++) {
        std::vector<uint32_t>::iterator iter = find(dupb.begin(), dupb.end(), i);
        if (iter == dupb.end())
          resb.push_back(stracksb[i]);
    }
}

void BYTETracker::linear_assignment(vector<vector<float> > &cost_matrix,
                                    uint32_t cost_matrix_size,
                                    uint32_t cost_matrix_size_size, float thresh,
                                    vector<vector<uint32_t> > &matches,
                                    vector<uint32_t> &unmatched_a,
                                    vector<uint32_t> &unmatched_b) {

    if (cost_matrix.size() == 0) {
        for (size_t i = 0; i < cost_matrix_size; i++)
          unmatched_a.push_back(i);

        for (size_t i = 0; i < cost_matrix_size_size; i++)
          unmatched_b.push_back(i);

        return;
    }

    vector<uint32_t> rowsol; vector<uint32_t> colsol;
    float c = (float) lapjv(cost_matrix, rowsol, colsol, true, thresh);
    for (size_t i = 0; i < rowsol.size(); i++) {
        if (rowsol[i] >= 0)  {
            vector<uint32_t> match;
            match.push_back(i);
            match.push_back(rowsol[i]);
            matches.push_back(match);
        } else {
            unmatched_a.push_back(i);
        }
    }

    for (size_t i = 0; i < colsol.size(); i++) {
        if (colsol[i] < 0)
          unmatched_b.push_back(i);
    }
}

vector<vector<float> > BYTETracker::ious(vector<vector<float> > &atlbrs, vector<vector<float> > &btlbrs)
{
    vector<vector<float> > ious;
    if (atlbrs.size()*btlbrs.size() == 0)
      return ious;

    ious.resize(atlbrs.size());
    for (size_t i = 0; i < ious.size(); i++)
      ious[i].resize(btlbrs.size());

    //bbox_ious
    for (size_t k = 0; k < btlbrs.size(); k++) {
        vector<float> ious_tmp;
        float box_area = (btlbrs[k][2] - btlbrs[k][0] + 1)*(btlbrs[k][3] - btlbrs[k][1] + 1);
        for (size_t n = 0; n < atlbrs.size(); n++) {
            float iw = min(atlbrs[n][2], btlbrs[k][2]) - max(atlbrs[n][0], btlbrs[k][0]) + 1;
            if (iw > 0) {
                float ih = min(atlbrs[n][3], btlbrs[k][3]) - max(atlbrs[n][1], btlbrs[k][1]) + 1;
                if(ih > 0) {
                    float ua = (atlbrs[n][2] - atlbrs[n][0] + 1)*(atlbrs[n][3] - atlbrs[n][1] + 1) + box_area - iw * ih;
                    ious[n][k] = iw * ih / ua;
                } else {
                    ious[n][k] = 0.0;
                }
            } else {
                ious[n][k] = 0.0;
            }
        }
    }

    return ious;
}

vector<vector<float> > BYTETracker::iou_distance(vector<STrack*> &atracks,
                                                 vector<STrack> &btracks,
                                                 uint32_t &dist_size,
                                                 uint32_t &dist_size_size) {

    vector<vector<float> > cost_matrix;
    if (atracks.size() * btracks.size() == 0) {
        dist_size = (uint32_t) atracks.size();
        dist_size_size = (uint32_t) btracks.size();

        return cost_matrix;
    }

    vector<vector<float> > atlbrs, btlbrs;

    for (size_t i = 0; i < atracks.size(); i++)
      atlbrs.push_back(atracks[i]->tlbr);
    for (size_t i = 0; i < btracks.size(); i++)
      btlbrs.push_back(btracks[i].tlbr);

    dist_size = (uint32_t) atracks.size();
    dist_size_size = (uint32_t) btracks.size();

    vector<vector<float> > _ious = ious(atlbrs, btlbrs);

    for (size_t i = 0; i < _ious.size();i++) {
        vector<float> _iou;
        for (size_t j = 0; j < _ious[i].size(); j++)
          _iou.push_back(1 - _ious[i][j]);
        cost_matrix.push_back(_iou);
    }

    return cost_matrix;
}

vector<vector<float> > BYTETracker::iou_distance(vector<STrack*> &atracks,
                                                 vector<STrack*> &btracks,
                                                 uint32_t &dist_size,
                                                 uint32_t &dist_size_size) {

    vector<vector<float> > cost_matrix;
    if (atracks.size() * btracks.size() == 0) {
        dist_size = (uint32_t) atracks.size();
        dist_size_size = (uint32_t) btracks.size();

        return cost_matrix;
    }

    vector<vector<float> > atlbrs, btlbrs;
    for (size_t i = 0; i < atracks.size(); i++)
      atlbrs.push_back(atracks[i]->tlbr);

    for (size_t i = 0; i < btracks.size(); i++)
      btlbrs.push_back(btracks[i]->tlbr);

    dist_size = (uint32_t) atracks.size();
    dist_size_size = (uint32_t) btracks.size();

    vector<vector<float> > _ious = ious(atlbrs, btlbrs);

    for (size_t i = 0; i < _ious.size();i++) {
        vector<float> _iou;
        for (size_t j = 0; j < _ious[i].size(); j++) {
            _iou.push_back(1 - _ious[i][j]);
        }
        cost_matrix.push_back(_iou);
    }

    return cost_matrix;
}

vector<vector<float> > BYTETracker::iou_distance(vector<STrack> &atracks,
                                                 vector<STrack> &btracks) {

    vector<vector<float> > atlbrs, btlbrs;
    for (size_t i = 0; i < atracks.size(); i++)
      atlbrs.push_back(atracks[i].tlbr);
    for (size_t i = 0; i < btracks.size(); i++)
      btlbrs.push_back(btracks[i].tlbr);

    vector<vector<float> > _ious = ious(atlbrs, btlbrs);
    vector<vector<float> > cost_matrix;
    for (size_t i = 0; i < _ious.size(); i++) {
        vector<float> _iou;
        for (size_t j = 0; j < _ious[i].size(); j++) {
            _iou.push_back(1 - _ious[i][j]);
        }
        cost_matrix.push_back(_iou);
    }

    return cost_matrix;
}

double BYTETracker::lapjv(const vector<vector<float> > &cost,
                          vector<uint32_t> &rowsol, vector<uint32_t> &colsol,
                          bool extend_cost, float cost_limit, bool return_cost) {

    vector<vector<float> > cost_c;
    cost_c.assign(cost.begin(), cost.end());

    vector<vector<float> > cost_c_extended;

    uint32_t n_rows = (uint32_t) cost.size();
    uint32_t n_cols = (uint32_t) cost[0].size();
    rowsol.resize(n_rows);
    colsol.resize(n_cols);

    int n = 0;
    if (n_rows == n_cols) {
        n = n_rows;
    }
    else {
        if (!extend_cost) {
            printf("set extend_cost=True\n");
            system("pause");
            exit(0);
        }
    }

    if (extend_cost || cost_limit < LONG_MAX) {
        n = n_rows + n_cols;
        cost_c_extended.resize(n);
        for (size_t i = 0; i < cost_c_extended.size(); i++)
            cost_c_extended[i].resize(n);

        if (cost_limit < LONG_MAX) {
            for (size_t i = 0; i < cost_c_extended.size(); i++) {
                for (size_t j = 0; j < cost_c_extended[i].size(); j++) {
                    cost_c_extended[i][j] = cost_limit / 2.0f;
                }
            }
        } else {
            float cost_max = -1;
            for (size_t i = 0; i < cost_c.size(); i++) {
                for (size_t j = 0; j < cost_c[i].size(); j++) {
                    if (cost_c[i][j] > cost_max)
                        cost_max = cost_c[i][j];
                }
            }
            for (size_t i = 0; i < cost_c_extended.size(); i++) {
                for (size_t j = 0; j < cost_c_extended[i].size(); j++) {
                    cost_c_extended[i][j] = cost_max + 1;
                }
            }
        }

        for (size_t i = n_rows; i < cost_c_extended.size(); i++) {
            for (size_t j = n_cols; j < cost_c_extended[i].size(); j++) {
                cost_c_extended[i][j] = 0;
            }
        }
        for (size_t i = 0; i < n_rows; i++) {
            for (size_t j = 0; j < n_cols; j++) {
                cost_c_extended[i][j] = cost_c[i][j];
            }
        }

        cost_c.clear();
        cost_c.assign(cost_c_extended.begin(), cost_c_extended.end());
    }

    double **cost_ptr;
    cost_ptr = new double *[sizeof(double *) * n];
    for (size_t i = 0; i < n; i++)
        cost_ptr[i] = new double[sizeof(double) * n];

    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            cost_ptr[i][j] = cost_c[i][j];
        }
    }

    int* x_c = new int[sizeof(int) * n];
    int *y_c = new int[sizeof(int) * n];

    int ret = lapjv::lapjv_internal(n, cost_ptr, x_c, y_c);
    if (ret != 0) {
        printf("Calculate Wrong!\n");
        system("pause");
        exit(0);
    }

    double opt = 0.0;

    if (n != n_rows) {
        for (size_t i = 0; i < n; i++) {
            if (x_c[i] >= n_cols)
                x_c[i] = -1;
            if (y_c[i] >= n_rows)
                y_c[i] = -1;
        }

        for (size_t i = 0; i < n_rows; i++)
          rowsol[i] = x_c[i];

        for (size_t i = 0; i < n_cols; i++)
          colsol[i] = y_c[i];

        if (return_cost) {
            for (size_t i = 0; i < rowsol.size(); i++) {
                if (rowsol[i] != -1)
                  opt += cost_ptr[i][rowsol[i]];
            }
        }
    } else if (return_cost) {
        for (size_t i = 0; i < rowsol.size(); i++) {
            opt += cost_ptr[i][rowsol[i]];
        }
    }

    for (size_t i = 0; i < n; i++) {
        delete[]cost_ptr[i];
    }

    delete[]cost_ptr;
    delete[]x_c;
    delete[]y_c;

    return opt;
}

/*
Scalar BYTETracker::get_color(uint32_t idx)
{
    idx += 3;
    return Scalar(37 * idx % 255, 17 * idx % 255, 29 * idx % 255);
}
*/

float BYTETracker::compute_iou(float box1_x1, float box1_y1, float box1_x2,
                               float box1_y2, float box2_x1, float box2_y1,
                               float box2_x2, float box2_y2) {

    float area1 = (box1_x2 - box1_x1) * (box1_y2 - box1_y1);
    float area2 = (box2_x2 - box2_x1) * (box2_y2 - box2_y1);
    float w_intersect = max(min(box1_x2, box2_x2) - max(box1_x1, box2_x1), 0.0f);
    float h_intersect = max(min(box1_y2, box2_y2) - max(box1_y1, box2_y1), 0.0f);
    float area_intersect = w_intersect * h_intersect;
    float area_union = area1 + area2 - area_intersect;
    float iou = area_intersect / (area_union + 1e-8);

    return iou;
}

float BYTETracker::compute_intersection_over_self(float box1_x1, float box1_y1,
                                                  float box1_x2, float box1_y2,
                                                  float box2_x1, float box2_y1,
                                                  float box2_x2, float box2_y2) {

    float area1 = (box1_x2 - box1_x1) * (box1_y2 - box1_y1);
    float area2 = (box2_x2 - box2_x1) * (box2_y2 - box2_y1);
    float w_intersect = max(min(box1_x2, box2_x2) - max(box1_x1, box2_x1), 0.0f);
    float h_intersect = max(min(box1_y2, box2_y2) - max(box1_y1, box2_y1), 0.0f);
    float area_intersect = w_intersect * h_intersect;

    return area_intersect / (area1 + 1e-8);
}
