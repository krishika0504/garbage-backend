# Deploying Waste Classification Backend to Google Cloud Run

Google Cloud Run is the **ideal production host** for this project because your Firebase project (`garbage-fa1b3`) is already a Google Cloud project!

---

## Benefits of Google Cloud Run:
1. **Free Tier**: 2 Million free requests per month, 360,000 GB-seconds compute free.
2. **Direct Firebase Integration**: Latency to Firebase Storage and Realtime Database is near 0ms because they run in the same Google Cloud datacenter.
3. **Automatic HTTPS**: Provides a production HTTPS endpoint out of the box (e.g., `https://waste-ai-xxxxxx-uc.a.run.app/predict`).
4. **Scales to Zero**: Costs $0 when the bin is idle; boots up instantly on object detection.

---

## Method 1: Deploy in 2 Minutes via Google Cloud Console (No CLI needed)

1. Open **[Google Cloud Console](https://console.cloud.google.com/)**.
2. Select your project: **`garbage-fa1b3`**.
3. In the top search bar, search for **Cloud Run** and click on it.
4. Click **+ CREATE SERVICE**.
5. Select **Continuously deploy from a repository** (click *Set up with Cloud Build* and link your GitHub repo).
6. Build Configuration:
   - Build type: **Dockerfile**
   - Source location: `/Dockerfile`
7. In the Service settings:
   - **Service name**: `waste-classifier-api`
   - **Region**: `us-central1` (or your nearest region)
   - **Authentication**: Select **Allow unauthenticated invocations** (so ESP32-CAM and web UI can connect).
8. Under **Container, Volumes, Networking, Security**:
   - **Container port**: `5000`
   - **Memory**: `1 GiB` or `2 GiB` (for MobileNetV2 TensorFlow inference)
   - **CPU**: `1`
   - **Environment variables**:
     - `FIREBASE_DATABASE_URL` = `https://garbage-fa1b3-default-rtdb.firebaseio.com`
     - `FIREBASE_STORAGE_BUCKET` = `garbage-fa1b3.firebasestorage.app`
9. Click **CREATE**.

---

## Method 2: Deploy in 1 Command via Google Cloud Shell

1. Open **[Google Cloud Shell](https://shell.cloud.google.com/?project=garbage-fa1b3)** in your browser.
2. Run this command:
   ```bash
   gcloud run deploy waste-classifier-api \
     --source . \
     --region us-central1 \
     --allow-unauthenticated \
     --port 5000 \
     --memory 1Gi \
     --set-env-vars FIREBASE_DATABASE_URL=https://garbage-fa1b3-default-rtdb.firebaseio.com,FIREBASE_STORAGE_BUCKET=garbage-fa1b3.firebasestorage.app
   ```
3. Cloud Run will build and output your production URL:
   ```text
   Service URL: https://waste-classifier-api-xxxxxx-uc.a.run.app
   ```

---

## Update ESP32-CAM to use Google Cloud Run URL

Once deployed, simply update the `SERVER_HOST` in your ESP32-CAM sketch:
```cpp
const char *SERVER_HOST = "waste-classifier-api-xxxxxx-uc.a.run.app";
const int   SERVER_PORT = 443;
const char *SERVER_PATH = "/predict";
```
