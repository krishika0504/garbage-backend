import os
import threading
from app import create_app

app = create_app()

# Start background warm-up thread for AI model so Gunicorn binds to Cloud Run port instantly
def warmup_model():
    try:
        from app.services.model_service import get_model_service
        print("[*] Background AI model initialization started...")
        get_model_service()
        print("[+] AI model loaded and ready.")
    except Exception as e:
        print(f"[!] Background model loading warning: {e}")

threading.Thread(target=warmup_model, daemon=True).start()

if __name__ == '__main__':
    port = int(os.environ.get('PORT', 8080))
    print(f"[*] Starting Flask server on 0.0.0.0:{port}...")
    app.run(host='0.0.0.0', port=port, debug=False)
