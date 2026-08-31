from app import create_app
from app.services.model_service import get_model_service

app = create_app()

if __name__ == '__main__':
    print("[*] Pre-loading model...")
    get_model_service()
    print("[*] Starting Flask server on http://127.0.0.1:5000...")
    app.run(host='0.0.0.0', port=5000, debug=False)
