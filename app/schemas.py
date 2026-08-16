from typing import List, Optional
from pydantic import BaseModel, Field


class HealthResponse(BaseModel):
    status: str = Field(..., example="ok")
    model_loaded: bool = Field(..., example=True)


class PredictionResponse(BaseModel):
    success: bool = Field(True, example=True)
    class_name: Optional[str] = Field(None, alias="class", description="Predicted waste class", example="plastic")
    prediction: str = Field(..., description="Predicted category name or 'unknown'", example="plastic")
    original_class: Optional[str] = Field(None, description="Raw class output from model", example="plastic")
    category: str = Field(..., description="Mapped project waste category", example="plastic")
    confidence: float = Field(..., description="Confidence score as decimal [0.0 - 1.0]", example=0.9412)
    confidence_percent: Optional[float] = Field(None, description="Confidence as percentage [0.0 - 100.0]", example=94.12)
    servo_angle: Optional[int] = Field(None, description="Servo angle for ESP32 MG90S motor", example=30)
    message: Optional[str] = Field(None, description="Optional status message (e.g. Low confidence)", example="Low confidence prediction")

    model_config = {
        "populate_by_name": True
    }


class PredictionItem(BaseModel):
    raw_class: str = Field(..., alias="class", description="Raw model class prediction", example="plastic")
    category: str = Field(..., description="Mapped category", example="plastic")
    confidence: float = Field(..., description="Confidence decimal", example=0.9412)
    confidence_percent: float = Field(..., description="Confidence percentage", example=94.12)

    model_config = {
        "populate_by_name": True
    }


class TopPredictionsResponse(BaseModel):
    success: bool = Field(True, example=True)
    predictions: List[PredictionItem]


class ErrorResponse(BaseModel):
    success: bool = Field(False, example=False)
    error: str = Field(..., example="Invalid image file format")
