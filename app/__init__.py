import os
from flask import Flask, render_template
from flask_cors import CORS
from app.config import Config
from app.routes.predict import predict_bp

def create_app():
    app = Flask(__name__, template_folder='templates')
    app.config.from_object(Config)

    # Enable CORS for all routes
    CORS(app)

    # Ensure uploads directory exists
    os.makedirs(app.config['UPLOAD_FOLDER'], exist_ok=True)

    # Register routes
    app.register_blueprint(predict_bp)

    @app.route('/', methods=['GET'])
    @app.route('/dashboard', methods=['GET'])
    def index():
        return render_template('index.html')

    return app
