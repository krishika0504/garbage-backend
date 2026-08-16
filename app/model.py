import json
import logging
import os
from typing import List, Optional
import numpy as np
import tensorflow as tf
from app.config import settings

logger = logging.getLogger("garbage_api.model")


class GarbageClassifierModel:
    """
    Singleton wrapper for loading and running inference on the Keras garbage classifier model.
    """

    def __init__(self):
        self.model: Optional[tf.keras.Model] = None
        self.class_names: List[str] = []

    def load(self) -> None:
        """
        Loads class names and the Keras model. Validates output dimensions.
        Fails with clear startup errors if loading fails.
        """
        # 1. Load class names
        class_file = settings.CLASS_NAMES_PATH
        if not os.path.exists(class_file):
            err = f"Class names file not found at: {os.path.abspath(class_file)}"
            logger.critical(err)
            raise FileNotFoundError(err)

        try:
            with open(class_file, "r", encoding="utf-8") as f:
                self.class_names = json.load(f)
            logger.info(f"Loaded {len(self.class_names)} classes from {class_file}")
        except Exception as e:
            err = f"Failed to parse class names JSON from {class_file}: {str(e)}"
            logger.critical(err)
            raise ValueError(err) from e

        if not self.class_names:
            err = f"Class names file {class_file} is empty."
            logger.critical(err)
            raise ValueError(err)

        # 2. Load Keras model
        model_file = settings.MODEL_PATH
        if not os.path.exists(model_file):
            err = f"Model file not found at: {os.path.abspath(model_file)}"
            logger.critical(err)
            raise FileNotFoundError(err)

        try:
            logger.info(f"Loading Keras model from {model_file}...")
            self.model = tf.keras.models.load_model(model_file, compile=False)
            logger.info("Keras model loaded successfully.")
        except Exception as e:
            err = f"Critical error loading Keras model from {model_file}: {str(e)}"
            logger.critical(err)
            raise RuntimeError(err) from e

        # 3. Validate output shape matches class names count
        try:
            output_shape = self.model.output_shape
            # output_shape is typically (None, N)
            num_output_classes = output_shape[-1] if output_shape else None
        except Exception as e:
            err = f"Could not determine model output shape: {str(e)}"
            logger.critical(err)
            raise ValueError(err) from e

        if num_output_classes != len(self.class_names):
            err = (
                f"Model output classes ({num_output_classes}) does not match "
                f"class_names.json length ({len(self.class_names)})."
            )
            logger.critical(err)
            raise ValueError(err)

        logger.info(f"Model shape validated: {output_shape} matches {len(self.class_names)} class names.")

    @property
    def is_loaded(self) -> bool:
        return self.model is not None and len(self.class_names) > 0

    def predict(self, preprocessed_image: np.ndarray) -> np.ndarray:
        """
        Runs model prediction on preprocessed image batch (1, 224, 224, 3).
        Returns raw probability vector.
        """
        if not self.is_loaded:
            raise RuntimeError("Model is not loaded.")

        # Verbose=0 for fast performance without log clutter
        predictions = self.model.predict(preprocessed_image, verbose=0)
        return predictions[0]


classifier_model = GarbageClassifierModel()
