import time
import urllib.parse
import requests
from app.config import Config

class FirebaseService:
    @staticmethod
    def upload_to_storage(image_bytes, filename="esp32_capture.jpg"):
        """
        Uploads image bytes to Firebase Storage bucket (garbage-fa1b3.firebasestorage.app)
        and returns the accessible download URL.
        """
        bucket = getattr(Config, 'FIREBASE_STORAGE_BUCKET', 'garbage-fa1b3.firebasestorage.app')
        if not bucket:
            return None

        timestamp = int(time.time() * 1000)
        storage_path = f"captures/{timestamp}_{filename}"
        encoded_name = urllib.parse.quote(storage_path, safe='')
        upload_url = f"https://firebasestorage.googleapis.com/v0/b/{bucket}/o?uploadType=media&name={encoded_name}"
        download_url = f"https://firebasestorage.googleapis.com/v0/b/{bucket}/o/{encoded_name}?alt=media"

        try:
            headers = {'Content-Type': 'image/jpeg'}
            resp = requests.post(upload_url, data=image_bytes, headers=headers, timeout=10)
            if resp.ok:
                resp_data = resp.json()
                token = resp_data.get('downloadTokens')
                if token:
                    download_url += f"&token={token}"
                print(f"[+] Uploaded image to Firebase Storage: {download_url}")
                return download_url
            else:
                print(f"[!] Firebase Storage upload response ({resp.status_code}): {resp.text}")
                return download_url
        except Exception as e:
            print(f"[!] Firebase Storage upload failed: {e}")
            return download_url

    @staticmethod
    def log_prediction(filename, prediction_result, image_url=None):
        """
        Logs a waste classification record to Firebase Realtime Database at /predictions.json.
        """
        if not Config.FIREBASE_DATABASE_URL:
            return None

        url = f"{Config.FIREBASE_DATABASE_URL.rstrip('/')}/predictions.json"
        
        record = {
            'filename': filename,
            'image_url': image_url or prediction_result.get('image_url', ''),
            'class': prediction_result.get('class'),
            'class_id': prediction_result.get('class_id'),
            'confidence': prediction_result.get('confidence'),
            'min_threshold': prediction_result.get('min_threshold', 0.70),
            'is_accepted': prediction_result.get('is_accepted', True),
            'confidence_status': prediction_result.get('confidence_status', 'HIGH'),
            'raw_score': prediction_result.get('raw_score'),
            'action_taken': prediction_result.get('action_taken'),
            'servo_position': prediction_result.get('servo_position', f"Servo 1: {prediction_result.get('servo_angle', 0)}°"),
            'servo_angle': prediction_result.get('servo_angle', 0),
            'timestamp': int(time.time() * 1000)
        }

        try:
            response = requests.post(url, json=record, timeout=5)
            if response.ok:
                print(f"[+] Logged prediction to Firebase Realtime DB: {record['class']} ({record['confidence']*100:.1f}%)")
                return response.json()
            else:
                print(f"[!] Firebase log returned status {response.status_code}: {response.text}")
        except Exception as e:
            print(f"[!] Failed to log to Firebase Realtime DB: {e}")

        return None

