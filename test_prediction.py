import os
import io
import time
import requests
from PIL import Image
import numpy as np
from app.services.model_service import get_model_service

def create_sample_image(filename="test_sample.jpg"):
    # Create a simple synthetic RGB test image
    img_array = (np.random.rand(224, 224, 3) * 255).astype(np.uint8)
    image = Image.fromarray(img_array)
    filepath = os.path.join("uploads", filename)
    os.makedirs("uploads", exist_ok=True)
    image.save(filepath)
    print(f"[+] Created test image: {filepath}")
    return filepath

def test_direct_model_inference(filepath):
    print("\n--- 1. Testing Direct Model Inference ---")
    model_service = get_model_service()
    result = model_service.predict(filepath)
    print("[+] Model Prediction Result:")
    print(f"    Class: {result['class']} (ID: {result['class_id']})")
    print(f"    Confidence: {result['confidence'] * 100:.2f}% (Min Threshold: {result.get('min_threshold', 0.7) * 100:.0f}%)")
    print(f"    Accepted: {result.get('is_accepted')} [{result.get('confidence_status')}]")
    print(f"    Action Taken: {result['action_taken']}")
    print(f"    Servo Angle: {result['servo_angle']}°")
    print(f"    Raw Sigmoid Score: {result['raw_score']}")
    return result

def test_api_endpoint(filepath):
    print("\n--- 2. Testing Live Flask API Endpoint ---")
    url = "http://127.0.0.1:5000/predict"
    
    with open(filepath, 'rb') as f:
        files = {'file': (os.path.basename(filepath), f, 'image/jpeg')}
        try:
            response = requests.post(url, files=files)
            print(f"[+] HTTP Status Code: {response.status_code}")
            print(f"[+] API Response JSON: {response.json()}")
        except Exception as e:
            print(f"[!] API test failed: {e}")

if __name__ == '__main__':
    sample_file = create_sample_image()
    test_direct_model_inference(sample_file)
    test_api_endpoint(sample_file)
