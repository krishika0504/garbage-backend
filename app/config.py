import os

class Config:
    BASE_DIR = os.path.abspath(os.path.dirname(__file__))
    MODEL_PATH = os.path.join(BASE_DIR, 'models', 'waste_classification_model.keras')
    WEIGHTS_PATH = os.path.join(BASE_DIR, 'models', 'best_model.weights.h5')
    UPLOAD_FOLDER = os.path.join(os.path.dirname(BASE_DIR), 'uploads')
    ALLOWED_EXTENSIONS = {'png', 'jpg', 'jpeg', 'webp'}
    IMAGE_SIZE = (224, 224)
    CLASS_LABELS = {0: 'Dry', 1: 'Wet'}
    CONFIDENCE_THRESHOLD = 0.70
    FIREBASE_DATABASE_URL = 'https://garbage-fa1b3-default-rtdb.firebaseio.com'
    FIREBASE_STORAGE_BUCKET = 'garbage-fa1b3.firebasestorage.app'
