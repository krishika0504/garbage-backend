import os
import time
import logging
from contextlib import asynccontextmanager
from fastapi import FastAPI, File, UploadFile, HTTPException, status, Request
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware

from app.config import settings
from app.model import classifier_model
from app.schemas import (
    HealthResponse,
    PredictionResponse,
    TopPredictionsResponse,
    ErrorResponse
)
from app.classifier import (
    validate_file_metadata,
    classify_single_image,
    classify_top_k_image
)

# Configure logging format
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S"
)
logger = logging.getLogger("garbage_api.main")


@asynccontextmanager
async def lifespan(app: FastAPI):
    """
    Application lifespan context manager: Loads Keras model once on server startup.
    """
    logger.info("Initializing Garbage Classification REST API...")
    if settings.SAVE_UPLOADS:
        os.makedirs(settings.UPLOAD_DIR, exist_ok=True)

    try:
        classifier_model.load()
    except Exception as e:
        logger.critical(f"Startup failure: Model loading failed! Error: {str(e)}")
        # Raise exception to block app startup if model fails to load
        raise e

    yield
    logger.info("Shutting down Garbage Classification REST API.")


app = FastAPI(
    title=settings.APP_NAME,
    version="1.0.0",
    description=(
        "Production-ready REST API for smart garbage classification using MobileNetV2 and TensorFlow. "
        "Receives images captured by ESP32-CAM AI Thinker, runs inference, and returns target waste categories "
        "and MG90S servo angles for ESP32 DevKit sorting mechanism."
    ),
    lifespan=lifespan
)

# CORS Configuration
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.CORS_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# Global Custom Exception Handlers to avoid python stack trace leaks
@app.exception_handler(HTTPException)
async def custom_http_exception_handler(request: Request, exc: HTTPException):
    return JSONResponse(
        status_code=exc.status_code,
        content={"success": False, "error": str(exc.detail)}
    )


@app.exception_handler(Exception)
async def global_unhandled_exception_handler(request: Request, exc: Exception):
    logger.error(f"Unhandled server error on {request.url.path}: {str(exc)}", exc_info=True)
    return JSONResponse(
        status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
        content={"success": False, "error": "Internal server error occurred during request processing."}
    )


@app.get(
    "/health",
    response_model=HealthResponse,
    summary="Health check",
    tags=["System"]
)
async def health_check():
    """
    Health check endpoint returning system status and model load state.
    """
    return HealthResponse(
        status="ok",
        model_loaded=classifier_model.is_loaded
    )


async def _read_and_validate_upload(image: UploadFile) -> bytes:
    """
    Helper function to validate file metadata and read binary contents safely under file size limits.
    """
    if not image or not image.filename:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="No image file provided in upload request."
        )

    # 1. Validate Extension and MIME type
    try:
        validate_file_metadata(image.filename, image.content_type)
    except ValueError as val_err:
        raise HTTPException(
            status_code=status.HTTP_415_UNSUPPORTED_MEDIA_TYPE,
            detail=str(val_err)
        )

    # 2. Enforce File Size Limit (in-memory read)
    max_bytes = settings.MAX_UPLOAD_SIZE_MB * 1024 * 1024
    content = await image.read()

    if len(content) == 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Uploaded file is empty (0 bytes)."
        )

    if len(content) > max_bytes:
        raise HTTPException(
            status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
            detail=f"Image size ({round(len(content) / (1024*1024), 2)}MB) exceeds maximum limit of {settings.MAX_UPLOAD_SIZE_MB}MB."
        )

    # 3. Optional persistent storage
    if settings.SAVE_UPLOADS:
        try:
            timestamp = int(time.time() * 1000)
            safe_filename = f"{timestamp}_{image.filename}"
            save_path = os.path.join(settings.UPLOAD_DIR, safe_filename)
            with open(save_path, "wb") as f:
                f.write(content)
        except Exception as save_err:
            logger.warning(f"Failed to save upload image to disk: {str(save_err)}")

    return content


@app.post(
    "/predict",
    response_model=PredictionResponse,
    responses={
        400: {"model": ErrorResponse, "description": "Invalid image file"},
        413: {"model": ErrorResponse, "description": "Image file too large"},
        415: {"model": ErrorResponse, "description": "Unsupported file format"},
        500: {"model": ErrorResponse, "description": "Model inference error"}
    },
    summary="Classify garbage image",
    tags=["Inference"]
)
async def predict_garbage(image: UploadFile = File(..., description="JPEG, PNG, or WebP image file uploaded from ESP32-CAM")):
    """
    Primary garbage classification endpoint.
    Accepts multipart/form-data with image payload, preprocesses in memory,
    runs MobileNetV2 inference, evaluates confidence threshold, and returns category and servo angle.
    """
    image_bytes = await _read_and_validate_upload(image)

    try:
        result = classify_single_image(image_bytes, endpoint="/predict")
        return result
    except ValueError as ve:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=str(ve)
        )
    except Exception as e:
        logger.error(f"Inference error during /predict: {str(e)}", exc_info=True)
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Error occurred during model inference."
        )


@app.post(
    "/predict/top",
    response_model=TopPredictionsResponse,
    responses={
        400: {"model": ErrorResponse, "description": "Invalid image file"},
        413: {"model": ErrorResponse, "description": "Image file too large"},
        415: {"model": ErrorResponse, "description": "Unsupported file format"},
        500: {"model": ErrorResponse, "description": "Model inference error"}
    },
    summary="Get top-3 garbage classification predictions",
    tags=["Inference"]
)
async def predict_top_garbage(image: UploadFile = File(..., description="JPEG, PNG, or WebP image file uploaded from ESP32-CAM")):
    """
    Top predictions endpoint returning top 3 ranked predicted classes and categories with confidence scores.
    """
    image_bytes = await _read_and_validate_upload(image)

    try:
        result = classify_top_k_image(image_bytes, top_k=3, endpoint="/predict/top")
        return result
    except ValueError as ve:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=str(ve)
        )
    except Exception as e:
        logger.error(f"Inference error during /predict/top: {str(e)}", exc_info=True)
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail="Error occurred during model inference."
        )
