import json
import os
from datetime import datetime

from megadetector.detection.run_detector import load_detector
from PIL import Image, ImageDraw

# Results directory
RESULTS_DIR = "./detection_results"
os.makedirs(RESULTS_DIR, exist_ok=True)

# Load MegaDetector
MODEL_PATH = "MDV5A"
detector = load_detector(MODEL_PATH)

# Category mapping
CATEGORY_MAP = {"1": "animal", "2": "person", "3": "vehicle"}

# Confidence thresholds
MIN_CONFIDENCE = {
    "animal": 0.5,
    "person": 0.3,
    "vehicle": 0.7,
}

# Bounding box colors per category
BBOX_COLORS = {"animal": "red", "person": "blue", "vehicle": "green"}


def draw_bboxes(img: Image.Image, detections: list) -> Image.Image:
    """Draw bounding boxes with labels on a copy of the image."""
    annotated = img.copy().convert("RGB")
    draw = ImageDraw.Draw(annotated)

    for det in detections:
        x_min, y_min, x_max, y_max = det["bbox_pixels"]
        color = BBOX_COLORS.get(det["category"], "yellow")
        label = f"{det['category']} {det['confidence']:.2f}"

        draw.rectangle([x_min, y_min, x_max, y_max], outline=color, width=3)
        draw.text((x_min + 4, y_min + 4), label, fill=color)

    return annotated


def detect_from_image(filepath: str) -> dict:
    """Run MegaDetector on a given image file and save results + annotated image."""

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = os.path.splitext(os.path.basename(filepath))[0]

    img = Image.open(filepath)
    img_width, img_height = img.size

    result = detector.generate_detections_one_image(img)

    detections = []

    if result and "detections" in result:
        for det in result["detections"]:
            try:
                category_name = CATEGORY_MAP.get(str(det["category"]), "unknown")
                confidence = float(det["conf"])

                if confidence < MIN_CONFIDENCE.get(category_name, 0.5):
                    continue

                bbox_norm = det["bbox"]

                x_min = bbox_norm[0] * img_width
                y_min = bbox_norm[1] * img_height
                x_max = (bbox_norm[0] + bbox_norm[2]) * img_width
                y_max = (bbox_norm[1] + bbox_norm[3]) * img_height

                detections.append({
                    "category": category_name,
                    "confidence": round(confidence, 3),
                    "bbox_pixels": [round(x_min, 2), round(y_min, 2), round(x_max, 2), round(y_max, 2)],
                    "bbox_normalized": bbox_norm,
                })

            except Exception as e:
                print(f"Warning: could not parse detection: {e}")

    # Save annotated image
    annotated_img = draw_bboxes(img, detections)
    annotated_path = os.path.join(RESULTS_DIR, f"{filename}_{timestamp}_annotated.jpg")
    annotated_img.save(annotated_path)

    # Save JSON result
    result_data = {
        "timestamp": timestamp,
        "filename": os.path.basename(filepath),
        "image_width": img_width,
        "image_height": img_height,
        "num_detections": len(detections),
        "detections": detections,
        "summary": {
            "animals":  sum(1 for d in detections if d["category"] == "animal"),
            "people":   sum(1 for d in detections if d["category"] == "person"),
            "vehicles": sum(1 for d in detections if d["category"] == "vehicle"),
        },
    }

    json_path = os.path.join(RESULTS_DIR, f"{filename}_{timestamp}.json")
    with open(json_path, "w") as f:
        json.dump(result_data, f, indent=2)

    return result_data


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("Usage: python wildlens_main.py <image_path>")
        sys.exit(1)

    image_path = sys.argv[1]
    if not os.path.exists(image_path):
        print(f"Error: file not found: {image_path}")
        sys.exit(1)

    result = detect_from_image(image_path)
    print(json.dumps(result, indent=2))