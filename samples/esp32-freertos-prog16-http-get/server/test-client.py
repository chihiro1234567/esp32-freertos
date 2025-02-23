import requests
import time

# server url
url = "http://127.0.0.1:8000/"

while True:
    try:
        response = requests.get(url)
        print("Response:", response.json())
    except Exception as e:
        print("Error occurred:", e)
    time.sleep(1)
