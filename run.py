import os
from app import create_app
from app.services.model_service import get_model_service

app = create_app()

# Pre-load model on startup for fast responses
try:
    print("[*] Pre-loading AI classification model...")
    get_model_service()
    print("[+] Model loaded successfully.")
except Exception as e:
    print(f"[!] Model load warning: {e}")

if __name__ == '__main__':
    port = int(os.environ.get('PORT', 5000))
    print(f"[*] Starting Flask server on 0.0.0.0:{port}...")
    app.run(host='0.0.0.0', port=port, debug=False)
