#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
import typing
import gi
import numpy as np

gi.require_version('Gst', '1.0')

from gi.repository import Gst
from gi.overrides import override
from gi.overrides import _gi_gst_qti  # type: ignore[attr-defined]
_gi_gst_qti


if typing.TYPE_CHECKING:
    # Import stubs for type checking this file.
    from gi.repository import GstQtiML
else:
    from gi.module import get_introspection_module
    GstQtiML = get_introspection_module('GstQtiML')

__all__ = []

class Frame(GstQtiML.Frame):
    def get_tensor(self, index: int) -> np.ndarray:  # type: ignore[override]
        """
        Build a zero-copy NumPy array from GstMapInfo of GstMLFrame:
        """
        mv = _gi_gst_qti.ml_frame_get_memory_view(self, index)

        if mv is None:
            return np.empty(0, dtype=np.uint8)

        tensor = np.frombuffer(mv, dtype={
            GstQtiML.Type.INT8:'int8',
            GstQtiML.Type.UINT8:'uint8',
            GstQtiML.Type.INT16:'int16',
            GstQtiML.Type.UINT16:'uint16',
            GstQtiML.Type.INT32:'int32',
            GstQtiML.Type.UINT32:'uint32',
            GstQtiML.Type.INT64:'int64',
            GstQtiML.Type.UINT64:'uint64',
            GstQtiML.Type.FLOAT16:'float16',
            GstQtiML.Type.FLOAT32:'float32'
        }[self.info.type])

        n_dimensions = int(self.info.n_dimensions[index])

        # Since multidimensional arrays are squahed when translated calculate offset
        offset = index * int(GstQtiML.TENSOR_MAX_DIMS) if (index > 0) else 0

        dims = [int(self.info.tensors[offset + i]) for i in range(n_dimensions)]
        tensor = tensor.reshape(tuple(dims))  # zero-copy reshape if contiguous

        return tensor

override(Frame)
__all__.append('Frame')


class Keypoint(GstQtiML.Keypoint):
    def __str__(self) -> str:
        string = f"name: {self.name}, confidence: {self.confidence}, "
        string += f"color: {self.color}, x: {self.x}, y: {self.y}"
        return string

    @property
    def name(self) -> str:
        return _gi_gst_qti.ml_keypoint_get_name(self)

    @name.setter
    def name(self, value: str) -> None:
        _gi_gst_qti.ml_keypoint_set_name(self, value)

override(Keypoint)
__all__.append("Keypoint")


class KeypointLink(GstQtiML.KeypointLink):
    def __str__(self) -> str:
        return f"link: [{self.l_kp}] <-> [{self.r_kp}], color: {self.color}"

override(KeypointLink)
__all__.append("KeypointLink")


class Classification(GstQtiML.Classification):
    def __str__(self) -> str:
        string = f"name: {self.name}, confidence: {self.confidence}, "
        string += f"color: {self.color}, xtraparams: {self.xtraparams}"
        return string

    @property
    def name(self) -> str:
        return _gi_gst_qti.ml_classification_get_name(self)

    @name.setter
    def name(self, value: str) -> None:
        _gi_gst_qti.ml_classification_set_name(self, value)

    @property
    def xtraparams(self) -> Gst.Structure:
        return _gi_gst_qti.ml_classification_get_xtraparams(self)

    @xtraparams.setter
    def xtraparams(self, value: Gst.Structure) -> None:
        _gi_gst_qti.ml_classification_set_xtraparams(self, value)

override(Classification)
__all__.append("Classification")


class Detection(GstQtiML.Detection):
    def __str__(self) -> str:
        string = f"name: {self.name}, confidence: {self.confidence}, "
        string += f"color: {self.color}, left: {self.left}, top: {self.top}, "
        string += f"right: {self.right}, bottom: {self.bottom}, landmarks: ["

        if self.landmarks is not None:
            string += ' | '.join(str(k) for k in self.landmarks)
        else:
            string += "None"

        string += f"], xtraparams: {self.xtraparams}"
        return string

    @property
    def name(self) -> str:
        return _gi_gst_qti.ml_detection_get_name(self)

    @name.setter
    def name(self, value: str) -> None:
        _gi_gst_qti.ml_detection_set_name(self, value)

    @property
    def landmarks(self) -> list:
        return _gi_gst_qti.ml_detection_get_landmarks(self)

    @landmarks.setter
    def landmarks(self, value) -> None:
        _gi_gst_qti.ml_detection_set_landmarks(self, value)

    @property
    def xtraparams(self) -> Gst.Structure:
        return _gi_gst_qti.ml_detection_get_xtraparams(self)

    @xtraparams.setter
    def xtraparams(self, value: Gst.Structure) -> None:
        _gi_gst_qti.ml_detection_set_xtraparams(self, value)

override(Detection)
__all__.append("Detection")


class Pose(GstQtiML.Pose):
    def __str__(self) -> str:
        string = f"name: {self.name}, confidence: {self.confidence}, keypoints: ["

        if self.keypoints is not None:
            string += ' | '.join(str(k) for k in self.keypoints)
        else:
            string += "None"

        string += "], links: ["

        if self.links is not None:
            string += ' | '.join(str(k) for k in self.links)
        else:
            string += "None"

        string += f"], xtraparams: {self.xtraparams}"
        return string

    @property
    def name(self) -> str:
        return _gi_gst_qti.ml_pose_get_name(self)

    @name.setter
    def name(self, value: str) -> None:
        _gi_gst_qti.ml_pose_set_name(self, value)

    @property
    def keypoints(self) -> list:
        return _gi_gst_qti.ml_pose_get_keypoints(self)

    @keypoints.setter
    def keypoints(self, value) -> None:
        _gi_gst_qti.ml_pose_set_keypoints(self, value)

    @property
    def links(self) -> list:
        return _gi_gst_qti.ml_pose_get_links(self)

    @links.setter
    def links(self, value) -> None:
        _gi_gst_qti.ml_pose_set_links(self, value)

    @property
    def xtraparams(self) -> Gst.Structure:
        return _gi_gst_qti.ml_pose_get_xtraparams(self)

    @xtraparams.setter
    def xtraparams(self, value: Gst.Structure) -> None:
        _gi_gst_qti.ml_pose_set_xtraparams(self, value)

override(Pose)
__all__.append("Pose")
