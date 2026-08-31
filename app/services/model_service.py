import os
import io
import numpy as np
from PIL import Image
from app.config import Config

class ModelService:
    _instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super(ModelService, cls).__new__(cls)
            cls._instance.model = None
            cls._instance._load_model()
        return cls._instance

    def _load_model(self):
        if self.model is not None:
            return

        print(f"[*] Loading Keras model from {Config.MODEL_PATH}...")
        try:
            import tensorflow as tf
            import keras
            self.model = keras.models.load_model(Config.MODEL_PATH)
            
            if os.path.exists(Config.WEIGHTS_PATH):
                print(f"[*] Loading best weights from {Config.WEIGHTS_PATH}...")
                self.model.load_weights(Config.WEIGHTS_PATH)
            else:
                print("[!] Weights file not found, using base model weights.")
            
            print("[+] Model and weights loaded successfully.")
        except Exception as e:
            print(f"[!] Error loading model: {e}")
            raise e

    def preprocess_image(self, image_input):
        """
        Accepts PIL Image or file-like bytes, resizes to target input size (224x224),
        converts to RGB and normalizes pixel values to [0, 1].
        """
        if isinstance(image_input, bytes):
            image = Image.open(io.BytesIO(image_input)).convert('RGB')
        elif isinstance(image_input, str):
            image = Image.open(image_input).convert('RGB')
        elif isinstance(image_input, Image.Image):
            image = image_input.convert('RGB')
        else:
            raise ValueError("Unsupported image input format.")

        image = image.resize(Config.IMAGE_SIZE)
        img_array = np.array(image, dtype=np.float32) / 255.0
        img_batch = np.expand_dims(img_array, axis=0)
        return img_batch

    def predict(self, image_input):
        if self.model is None:
            self._load_model()

        img_batch = self.preprocess_image(image_input)
        raw_pred = self.model.predict(img_batch, verbose=0)
        score = float(raw_pred[0][0])
        
        # Binary classification threshold = 0.5
        if score >= 0.5:
            class_id = 1
            confidence = score
        else:
            class_id = 0
            confidence = 1.0 - score

        class_name = Config.CLASS_LABELS.get(class_id, 'Unknown')
        is_wet = class_name.lower() == 'wet'

        # Confidence Threshold Validation (Minimum required: 0.70 / 70%)
        min_threshold = getattr(Config, 'CONFIDENCE_THRESHOLD', 0.70)
        is_accepted = confidence >= min_threshold
        confidence_status = "HIGH" if is_accepted else "LOW_CONFIDENCE_FALLBACK"

        if is_accepted:
            action_taken = "Sorted to WET Waste Bin" if is_wet else "Sorted to DRY Waste Bin"
            servo_angle = 180 if is_wet else 0
        else:
            # Safety Fallback: Default to DRY bin (0° full flip) if model confidence is low (< 70%)
            action_taken = f"Low Confidence ({confidence*100:.1f}% < {min_threshold*100:.0f}%): Defaulted to DRY Waste Bin (Safety Fallback)"
            servo_angle = 0

        servo_position = f"Servo 1: {servo_angle}°"

        return {
            'class_id': class_id,
            'class': class_name,
            'confidence': round(confidence, 4),
            'min_threshold': min_threshold,
            'is_accepted': is_accepted,
            'confidence_status': confidence_status,
            'raw_score': round(score, 4),
            'action_taken': action_taken,
            'servo_position': servo_position,
            'servo_angle': servo_angle
        }

# Global singleton helper getter
def get_model_service():
    return ModelService()
