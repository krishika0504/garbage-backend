import os
import base64
import time
from flask import Blueprint, request, jsonify
from werkzeug.utils import secure_filename
from app.config import Config
from app.services.model_service import get_model_service
from app.services.firebase_service import FirebaseService

predict_bp = Blueprint('predict', __name__)

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in Config.ALLOWED_EXTENSIONS

@predict_bp.route('/health', methods=['GET'])

def health():
    return jsonify({
        'status': 'online',
        'message': 'Waste Classification API is running',
        'model_path': Config.MODEL_PATH,
        'weights_path': Config.WEIGHTS_PATH,
        'firebase_url': Config.FIREBASE_DATABASE_URL
    }), 200

@predict_bp.route('/camera/config', methods=['GET'])
def camera_config():
    """Return supported camera module presets and resolution configurations."""
    return jsonify({
        'status': 'success',
        'modules': [
            {
                'id': 'AI_THINKER',
                'name': 'ESP32-CAM (AI-Thinker OV2640)',
                'sensor': 'OV2640 2MP',
                'features': ['Onboard Flash LED', 'PSRAM Support', 'SD Card Slot'],
                'default_resolution': 'VGA (640x480)'
            },
            {
                'id': 'ESP_EYE',
                'name': 'ESP32-CAM (ESP-EYE / OV3660)',
                'sensor': 'OV2640 / OV3660 3MP',
                'features': ['PSRAM Support', 'Microphone', 'Dual Buttons'],
                'default_resolution': 'VGA (640x480)'
            },
            {
                'id': 'M5STACK_PSRAM',
                'name': 'ESP32-CAM (M5Stack PSRAM)',
                'sensor': 'OV2640 2MP',
                'features': ['PSRAM Support', 'Grove Connector', 'Compact Size'],
                'default_resolution': 'VGA (640x480)'
            },
            {
                'id': 'TTGO_T_JOURNAL',
                'name': 'ESP32-CAM (TTGO T-Journal)',
                'sensor': 'OV2640 2MP',
                'features': ['OLED Display', 'Antenna Connector', 'I2C OLED'],
                'default_resolution': 'VGA (640x480)'
            },
            {
                'id': 'WROVER_KIT',
                'name': 'ESP32-CAM (WROVER-KIT)',
                'sensor': 'OV2640 2MP',
                'features': ['Dual Core', 'External RGB LED', 'PSRAM Support'],
                'default_resolution': 'VGA (640x480)'
            },
            {
                'id': 'WEBCAM_LOCAL',
                'name': 'Local Browser WebCam (Integrated / USB)',
                'sensor': 'HTML5 Video Stream',
                'features': ['Zero Hardware Setup', 'HTML5 Canvas Capture', 'Real-time FPS'],
                'default_resolution': 'VGA (640x480)'
            },
            {
                'id': 'CUSTOM_MJPEG',
                'name': 'Custom MJPEG / RTSP Stream URL',
                'sensor': 'IP Camera / RTSP Feed',
                'features': ['Network IP Camera', 'Phone IP Webcam', 'RTSP / HTTP'],
                'default_resolution': 'Auto'
            }
        ],
        'resolutions': [
            {'label': 'UXGA (1600x1200)', 'width': 1600, 'height': 1200, 'framesize': 'FRAMESIZE_UXGA'},
            {'label': 'SXGA (1280x1024)', 'width': 1280, 'height': 1024, 'framesize': 'FRAMESIZE_SXGA'},
            {'label': 'HD 720p (1280x720)', 'width': 1280, 'height': 720, 'framesize': 'FRAMESIZE_HD'},
            {'label': 'SVGA (800x600)', 'width': 800, 'height': 600, 'framesize': 'FRAMESIZE_SVGA'},
            {'label': 'VGA (640x480)', 'width': 640, 'height': 480, 'framesize': 'FRAMESIZE_VGA'},
            {'label': 'CIF (400x296)', 'width': 400, 'height': 296, 'framesize': 'FRAMESIZE_CIF'},
            {'label': 'QVGA (320x240)', 'width': 320, 'height': 240, 'framesize': 'FRAMESIZE_QVGA'}
        ],
        'frame_rates': [5, 10, 15, 30]
    }), 200

@predict_bp.route('/predict', methods=['POST'])
def predict():
    image_bytes = None
    filename = None

    # Option A: Form Data File Upload
    if 'file' in request.files:
        file = request.files['file']
        if file.filename != '' and allowed_file(file.filename):
            filename = secure_filename(file.filename)
            image_bytes = file.read()

    # Option B: JSON payload with Base64 image string (WebCam snapshot)
    if image_bytes is None and request.is_json:
        data = request.get_json()
        image_data = data.get('image') or data.get('image_base64')
        if image_data:
            if ',' in image_data:
                image_data = image_data.split(',', 1)[1]
            try:
                image_bytes = base64.b64decode(image_data)
                filename = f"webcam_snap_{int(time.time())}.jpg"
            except Exception as e:
                return jsonify({'error': f'Failed to decode base64 image: {str(e)}'}), 400

    # Option C: Direct Raw Binary Stream (ESP32-CAM)
    if image_bytes is None:
        raw_data = request.get_data()
        if raw_data and len(raw_data) > 0:
            # Check for JPEG magic bytes 0xFF 0xD8
            jpeg_start = raw_data.find(b'\xff\xd8')
            if jpeg_start != -1:
                image_bytes = raw_data[jpeg_start:]
            else:
                image_bytes = raw_data
            filename = f"esp32_capture_{int(time.time())}.jpg"

    if image_bytes is None:
        return jsonify({'error': 'No valid image file, base64 payload, or binary image stream provided in request'}), 400

    try:
        model_service = get_model_service()
        result = model_service.predict(image_bytes)

        # Upload image to Firebase Storage bucket (garbage-fa1b3.firebasestorage.app)
        image_url = FirebaseService.upload_to_storage(image_bytes, filename)
        if image_url:
            result['image_url'] = image_url

        os.makedirs(Config.UPLOAD_FOLDER, exist_ok=True)
        save_path = os.path.join(Config.UPLOAD_FOLDER, filename)
        with open(save_path, 'wb') as f:
            f.write(image_bytes)

        # Log prediction to Firebase Realtime Database
        FirebaseService.log_prediction(filename, result, image_url=image_url)

        return jsonify({
            'status': 'success',
            'filename': filename,
            'image_url': image_url or '',
            'prediction': result
        }), 200

    except Exception as e:
        return jsonify({'error': f'Failed to process image: {str(e)}'}), 500

