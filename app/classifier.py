import io
import time
import logging
from typing import Tuple, Dict, Any, List, Optional
import numpy as np
from PIL import Image
import tensorflow as tf

from app.config import settings
from app.model import classifier_model

logger = logging.getLogger("garbage_api.classifier")

CATEGORY_MAP: Dict[str, str] = {
    "plastic": "dry",
    "paper": "dry",
    "cardboard": "dry",
    "biological": "wet",
    "metal": "dry",
    "glass": "dry",
    "battery": "dry",
    "clothes": "dry",
    "shoes": "dry",
    "trash": "dry"
}

SERVO_ANGLE_MAP: Dict[str, int] = {
    "dry": 45,    # 45° angle for Dry Waste container
    "wet": 135,   # 135° angle for Wet Waste container
    "unknown": 90 # 90° neutral home position
}


def validate_file_metadata(filename: str, content_type: Optional[str] = None) -> None:
    """
    Validates the file extension and MIME type.
    Raises ValueError on validation failure.
    """
    if filename:
        ext = filename.split(".")[-1].lower() if "." in filename else ""
        if ext not in settings.ALLOWED_EXTENSIONS:
            raise ValueError(
                f"Unsupported file extension '.{ext}'. Allowed extensions: {', '.join(settings.ALLOWED_EXTENSIONS)}"
            )

    if content_type and content_type.lower() not in settings.ALLOWED_MIME_TYPES:
        raise ValueError(
            f"Unsupported MIME type '{content_type}'. Allowed types: {', '.join(settings.ALLOWED_MIME_TYPES)}"
        )


def preprocess_image_bytes(image_bytes: bytes) -> np.ndarray:
    """
    Reads bytes using Pillow, converts to RGB, resizes to 224x224,
    converts to NumPy float32 array, adds batch dimension,
    and applies MobileNetV2 preprocess_input.
    """
    try:
        image = Image.open(io.BytesIO(image_bytes))
    except Exception as e:
        raise ValueError(f"Invalid or corrupted image file: {str(e)}") from e

    # Convert to RGB (handles RGBA, grayscale, palette images)
    image = image.convert("RGB")

    # Resize to 224x224
    image = image.resize((224, 224), Image.Resampling.BILINEAR)

    # Convert to NumPy array
    img_array = np.array(image, dtype=np.float32)

    # Add batch dimension: (224, 224, 3) -> (1, 224, 224, 3)
    img_batch = np.expand_dims(img_array, axis=0)

    # Apply MobileNetV2 preprocessing (scales pixels to range [-1, 1])
    preprocessed = tf.keras.applications.mobilenet_v2.preprocess_input(img_batch)

    return preprocessed


def classify_single_image(image_bytes: bytes, endpoint: str = "/predict") -> Dict[str, Any]:
    """
    Preprocesses uploaded image, runs prediction, maps category and servo angle,
    checks confidence threshold, logs inference metrics, and returns structured result dictionary.
    """
    start_time = time.perf_counter()

    preprocessed_img = preprocess_image_bytes(image_bytes)

    # Run inference
    probabilities = classifier_model.predict(preprocessed_img)

    inference_time_ms = round((time.perf_counter() - start_time) * 1000, 2)

    top_idx = int(np.argmax(probabilities))
    top_confidence = float(probabilities[top_idx])
    confidence_percent = round(top_confidence * 100, 2)
    decimal_confidence = round(top_confidence, 4)

    raw_class = classifier_model.class_names[top_idx]
    mapped_category = CATEGORY_MAP.get(raw_class, "other")
    servo_angle = SERVO_ANGLE_MAP.get(mapped_category, 90)

    # Logging requirement
    logger.info(
        f"Endpoint: {endpoint} | Prediction: {raw_class} | Category: {mapped_category} | "
        f"Confidence: {confidence_percent}% | Inference: {inference_time_ms} ms"
    )

    # Threshold evaluation
    if top_confidence < settings.CONFIDENCE_THRESHOLD:
        return {
            "success": True,
            "class": "unknown",
            "prediction": "unknown",
            "original_class": "unknown",
            "category": "unknown",
            "confidence": decimal_confidence,
            "confidence_percent": confidence_percent,
            "servo_angle": 90,
            "message": "Low confidence prediction"
        }

    return {
        "success": True,
        "class": raw_class,
        "prediction": mapped_category,
        "original_class": raw_class,
        "category": mapped_category,
        "confidence": decimal_confidence,
        "confidence_percent": confidence_percent,
        "servo_angle": servo_angle
    }


def classify_top_k_image(image_bytes: bytes, top_k: int = 3, endpoint: str = "/predict/top") -> Dict[str, Any]:
    """
    Preprocesses uploaded image, runs prediction, ranks top-k classes,
    logs inference metrics, and returns formatted predictions list.
    """
    start_time = time.perf_counter()

    preprocessed_img = preprocess_image_bytes(image_bytes)
    probabilities = classifier_model.predict(preprocessed_img)

    inference_time_ms = round((time.perf_counter() - start_time) * 1000, 2)

    # Sort indices by probability descending
    top_indices = np.argsort(probabilities)[::-1][:top_k]

    predictions_list = []
    for idx in top_indices:
        prob = float(probabilities[idx])
        raw_cls = classifier_model.class_names[idx]
        cat = CATEGORY_MAP.get(raw_cls, "other")
        predictions_list.append({
            "class": raw_cls,
            "category": cat,
            "confidence": round(prob, 4),
            "confidence_percent": round(prob * 100, 2)
        })

    top_pred = predictions_list[0]
    logger.info(
        f"Endpoint: {endpoint} | Top Prediction: {top_pred['class']} | Category: {top_pred['category']} | "
        f"Confidence: {top_pred['confidence_percent']}% | Inference: {inference_time_ms} ms"
    )

    return {
        "success": True,
        "predictions": predictions_list
    }
