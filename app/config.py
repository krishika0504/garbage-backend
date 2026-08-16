import os
from typing import List, Set
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    APP_NAME: str = "Garbage Classification API"
    DEBUG: bool = False

    # Model settings
    MODEL_PATH: str = os.path.join("models", "garbage_classifier.keras")
    CLASS_NAMES_PATH: str = os.path.join("models", "class_names.json")
    CONFIDENCE_THRESHOLD: float = 0.60

    # Image upload limits
    MAX_UPLOAD_SIZE_MB: int = 10
    ALLOWED_EXTENSIONS: Set[str] = {"jpg", "jpeg", "png", "webp"}
    ALLOWED_MIME_TYPES: Set[str] = {
        "image/jpeg",
        "image/pjpeg",
        "image/png",
        "image/webp"
    }

    # CORS settings
    CORS_ORIGINS: List[str] = ["*"]

    # Storage settings
    SAVE_UPLOADS: bool = False
    UPLOAD_DIR: str = "uploads"

    model_config = {
        "env_file": ".env",
        "case_sensitive": True,
        "extra": "ignore"
    }


settings = Settings()
